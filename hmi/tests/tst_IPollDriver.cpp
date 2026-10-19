// ============================================================================
// 阶段 C · C.1 · IPollDriver 接口契约单测
//
// 验证 PollScheduler 跟具体协议(Modbus/S7/EIP)解耦:
//   1. Mock driver 能装进 PollScheduler(说明 PollScheduler 不依赖 Modbus-specific 类型)
//   2. PollScheduler 调 sendReadRequest → driver 收到正确参数(area/slaveId/addr/count/reqId)
//   3. driver 发 readFinished → PollScheduler 把 registers 分发到对应的 Variable
//   4. 4 种 RegisterArea(HoldingReg/InputReg/Coils/DiscreteInput)都路由对
//   5. driver.disconnect() 不崩
//   6. DriverFactory::create("modbus") 返回非空
//   7. DriverFactory::create("unknown") 返回 nullptr
//
// 关联:IPollDriver.h, ModbusDriver.h, DriverFactory.h, PollScheduler.h
// ============================================================================

#include <QtTest>
#include <QSignalSpy>
#include "drivers/IPollDriver.h"
#include "drivers/DriverFactory.h"
#include "Variable.h"
#include "VariableManager.h"
#include "PollScheduler.h"

using namespace qwhmi::drivers;

// ★ 测试关键:Mock driver,完全不依赖 ModbusMaster
//   只要实现 IPollDriver 接口就能跑,证明 PollScheduler 协议无关
class MockDriver : public IPollDriver {
    Q_OBJECT
public:
    explicit MockDriver(QObject* parent = nullptr) : IPollDriver(parent) {}

    // 测试用:记录最近一次调用
    IPollDriver::ReadRequest lastReadReq;
    QVector<quint16> nextReplyRegisters;
    bool nextReplySuccess = true;
    int sendReadCount = 0;

    // ★ 2026-10-10:延迟回包开关。
    //   默认 false = 同步立刻回(原有 20 条用例靠它,不依赖事件循环时序)。
    //   打开后请求只记录不回,由测试自己调 flushReply() 决定什么时候回 ——
    //   这才能造出"请求在途"那个窗口,而它正是悬空指针 bug 的必要条件。
    bool manualReply = false;
    QVector<quint32> heldRequestIds;

    // ★ 2b(2026-10-19):模仿 ModbusMaster 的单 in-flight 拒收。
    //   默认 false = 原来的老行为(pendingCount 恒 0、从不拒收),20 条老用例不受影响。
    //   打开后行为与 ModbusMaster.cpp:231-237 一致:
    //     上一帧未完成 → 【拒收】+ 只 emit errorOccurred,【不发 readFinished】。
    //   不照抄这个行为就测不出 BUG-2(地址不连续的变量永久卡住),
    //   因为那个 bug 的成因正是"拒收的那几帧永远等不到 readFinished"。
    bool serialInFlight = false;
    // 回包内容用"请求的地址"填充 —— 不这样就没法证明"三组变量各自都轮到了":
    //   全用同一个 nextReplyRegisters 的话,哪怕调度器只轮询了第一组,
    //   后面两组停在默认值,也可能碰巧跟期望值一样。
    bool replyWithRequestAddr = false;

    void connectTo(const QString& host, quint16 port) override {
        lastHost = host;
        lastPort = port;
        connectedFlag = true;
        emit connected();
    }
    void disconnect() override {
        connectedFlag = false;
        emit disconnected();
    }
    bool isConnected() const override { return connectedFlag; }
    int pendingCount() const override { return serialInFlight ? serialInflight_ : 0; }

    // ★ 2b(2026-10-19):返回"是否被接收",与 ModbusDriver 同形。
    //   false = 已拒绝,【不会】来 readFinished —— PollScheduler 必须回滚。
    bool sendReadRequest(const ReadRequest& req) override {
        // ★ 与 ModbusMaster.cpp:233-237 同形:拒收 + 只报错,不回 readFinished
        if (serialInFlight && serialInflight_ > 0) {
            emit errorOccurred(QStringLiteral("上一帧未完成,稍后再发"));
            return false;
        }
        lastReadReq = req;
        ++sendReadCount;
        if (serialInFlight) {
            serialInflight_ = 1;
            heldAddrs_.insert(req.requestId, req.addr);
        }
        if (manualReply) {
            heldRequestIds << req.requestId;
            return true;   // ★ 接收了,但请求留在"在途"状态等 flushReply
        }
        if (serialInFlight) serialInflight_ = 0;
        // 模拟立刻回复(同步)
        emit readFinished(req.requestId, replyFor(req), nextReplySuccess);
        return true;
    }

    // 把攒着的请求一次性回掉(或只回指定的那一个)
    void flushReply(quint32 onlyThisId = 0) {
        const QVector<quint32> ids = heldRequestIds;
        heldRequestIds.clear();
        for (quint32 id : ids) {
            if (onlyThisId && id != onlyThisId) {
                heldRequestIds << id;   // 没轮到的留下次
                continue;
            }
            emit readFinished(id, replyForAddr(heldAddrs_.value(id)), nextReplySuccess);
            heldAddrs_.remove(id);
        }
        if (serialInFlight) serialInflight_ = 0;   // 全回完了,没有在途的了
    }

private:
    QVector<quint16> replyFor(const ReadRequest& r) const {
        return replyForAddr(r.addr);
    }
    QVector<quint16> replyForAddr(int addr) const {
        return replyWithRequestAddr ? QVector<quint16>{quint16(addr & 0xFFFF)}
                                    : nextReplyRegisters;
    }
    int serialInflight_ = 0;
    QHash<quint32, int> heldAddrs_;   // reqId → 该请求的起始地址
public:

    bool sendWriteRequest(RegisterArea, int, int,
                           const QVector<quint16>&, quint32) override { return true; }

    QString protocolName() const override { return QStringLiteral("mock"); }

    QString lastHost;
    quint16 lastPort = 0;
    bool connectedFlag = false;
};

class TestIPollDriver : public QObject {
    Q_OBJECT
private slots:
    void init();      // 每个用例前清空
    void cleanup();

    // IPollDriver 接口基本契约
    void connectTo_emitsConnected();
    void disconnect_emitsDisconnected();
    void isConnected_tracksState();

    // PollScheduler 集成(Mock driver 装进去能跑)
    void pollScheduler_acceptsMockDriver();
    void pollScheduler_readRequest_reachesDriver();
    void pollScheduler_mockReply_distributesToVariable();

    // DriverFactory
    void driverFactory_createModbus_returnsNonNull();
    void driverFactory_createUnknown_returnsNull();
    void driverFactory_isSupported_modbus();

    // 4 种 RegisterArea 都能路由
    void readRequest_passesAllFourAreas();

    // ---- ★ 2026-10-10:变量在"请求在途"时被删/被替换 ----
    void deletingVariableWhileRequestInFlight_doesNotCrash();
    void deletingVariableWhileRequestInFlight_removedFromPending();
    void replacingVariable_keepsItBeingPolled();
    void deletingActiveVariable_removesItFromActiveSet();

    // ---- ★ 2b(2026-10-19):多 driver 挂接 + onTick 丢帧 ----
    void driverAddedAfterStart_receivesReplies();
    void addDriver_sameIdTwice_doesNotDoubleConnect();
    void stopThenAddDriver_doesNotResurrectConnections();
    void start_stop_start_doesNotAccumulateConnections();
    void nonContiguousAddresses_allStillPoll();
    void sendReadRequestRefused_rollsBackInFlight();
};

void TestIPollDriver::init() {
    // 每个用例前清空(目前没有需要清的全局状态)
}

void TestIPollDriver::cleanup() {
    // cleanup:目前没需要清的
}

void TestIPollDriver::connectTo_emitsConnected() {
    MockDriver d;
    QSignalSpy spy(&d, &IPollDriver::connected);
    d.connectTo("127.0.0.1", 502);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(d.lastHost, QStringLiteral("127.0.0.1"));
    QCOMPARE(d.lastPort, quint16(502));
    QVERIFY(d.isConnected());
}

void TestIPollDriver::disconnect_emitsDisconnected() {
    MockDriver d;
    d.connectTo("127.0.0.1", 502);
    QSignalSpy spy(&d, &IPollDriver::disconnected);
    d.disconnect();
    QCOMPARE(spy.count(), 1);
    QVERIFY(!d.isConnected());
}

void TestIPollDriver::isConnected_tracksState() {
    MockDriver d;
    QVERIFY(!d.isConnected());
    d.connectTo("127.0.0.1", 502);
    QVERIFY(d.isConnected());
    d.disconnect();
    QVERIFY(!d.isConnected());
}

void TestIPollDriver::pollScheduler_acceptsMockDriver() {
    // 关键验证:PollScheduler 构造函数接受 IPollDriver*,不要求 Modbus-specific 类型
    VariableManager vm;
    MockDriver driver;
    PollScheduler ps(&vm, &driver);  // 不崩 = PollScheduler 真协议无关
    Q_UNUSED(ps);
}

void TestIPollDriver::pollScheduler_readRequest_reachesDriver() {
    // 关键验证:PollScheduler 调 sendReadRequest 后,driver 收到正确 ReadRequest
    VariableManager vm;
    MockDriver driver;
    driver.connectTo("127.0.0.1", 502);   // ★ 必须先连,否则 PollScheduler::onTick 看到 !isConnected() 立刻 return

    // 加 1 个 Variable(地址 0x0002)
    auto* v = new Variable("Speed", 0x0002, Variable::DataType::UInt16, Variable::Access::ReadWrite, &vm);
    vm.addVariable(v);

    PollScheduler ps(&vm, &driver);
    ps.setAllActive();

    // 触发 1 次 tick:手动 emit timeout 不可,直接调 start + 手动驱动 timer
    // 简化:PollScheduler 内部 timer 是 100ms,等 200ms 应该触发至少 1 次
    ps.start();
    QTest::qWait(250);
    ps.stop();

    QVERIFY(driver.sendReadCount >= 1);
    QCOMPARE(driver.lastReadReq.area, RegisterArea::HoldingReg);
    QCOMPARE(driver.lastReadReq.slaveId, 1);
    QCOMPARE(driver.lastReadReq.addr, 0x0002);
    QCOMPARE(driver.lastReadReq.count, 1);
}

void TestIPollDriver::pollScheduler_mockReply_distributesToVariable() {
    // 关键验证:driver 发 readFinished 后,PollScheduler 把 registers 分发到 Variable
    VariableManager vm;
    MockDriver driver;
    driver.connectTo("127.0.0.1", 502);   // ★ 同上
    driver.nextReplyRegisters = {1234};  // Speed 应该是 1234

    auto* v = new Variable("Speed", 0x0002, Variable::DataType::UInt16, Variable::Access::ReadWrite, &vm);
    vm.addVariable(v);

    PollScheduler ps(&vm, &driver);
    ps.setAllActive();
    ps.start();
    QTest::qWait(250);
    ps.stop();

    // ★ 关键:Variable 的值应该是 1234(从 mock reply 来的)
    QCOMPARE(v->value().toUInt(), quint16(1234));
    QCOMPARE(v->quality(), Variable::Quality::Good);
}

void TestIPollDriver::driverFactory_createModbus_returnsNonNull() {
    // C.1 阶段工厂应该能造 ModbusDriver
    IPollDriver* driver = DriverFactory::create("modbus");
    QVERIFY(driver != nullptr);
    QCOMPARE(driver->protocolName(), QStringLiteral("modbus"));
    delete driver;
}

void TestIPollDriver::driverFactory_createUnknown_returnsNull() {
    // 不支持的协议返回 nullptr + 警告
    IPollDriver* driver = DriverFactory::create("xyz");
    QVERIFY(driver == nullptr);
}

void TestIPollDriver::driverFactory_isSupported_modbus() {
    QVERIFY(DriverFactory::isSupported("modbus"));
    QVERIFY(DriverFactory::isSupported("MODBUS"));    // 大小写不敏感
    QVERIFY(DriverFactory::isSupported("modbus_tcp"));
    // ★ 阶段 C · C.2 (2026-09-18):s7 已实现(骨架,真连接在 C.2.4)
    QVERIFY(DriverFactory::isSupported("s7"));       // C.2 已支持(骨架阶段,真连接 PLCSIM 在 C.2.7)
    // ★ 阶段 C · C.3 (2026-09-18):eip 已实现(骨架,真连接在 C.3.4)
    QVERIFY(DriverFactory::isSupported("eip"));      // C.3 已支持(骨架阶段,真连接 Logix Designer 在 C.6a)
    QVERIFY(DriverFactory::isSupported("ethernet_ip")); // 大小写/别名
}

void TestIPollDriver::readRequest_passesAllFourAreas() {
    // 验证 ReadRequest::area 字段能存 4 种 enum 值(IPollDriver 协议无关验证)
    MockDriver driver;
    QVector<quint16> regs = {0};
    driver.nextReplyRegisters = regs;

    IPollDriver::ReadRequest req;
    req.slaveId = 1;
    req.addr = 0x0001;
    req.count = 1;
    req.requestId = 1;

    // HoldingReg
    req.area = RegisterArea::HoldingReg;
    driver.sendReadRequest(req);
    QCOMPARE(driver.lastReadReq.area, RegisterArea::HoldingReg);

    // InputReg
    req.area = RegisterArea::InputReg;
    req.requestId = 2;
    driver.sendReadRequest(req);
    QCOMPARE(driver.lastReadReq.area, RegisterArea::InputReg);

    // Coils
    req.area = RegisterArea::Coils;
    req.requestId = 3;
    driver.sendReadRequest(req);
    QCOMPARE(driver.lastReadReq.area, RegisterArea::Coils);

    // DiscreteInput
    req.area = RegisterArea::DiscreteInput;
    req.requestId = 4;
    driver.sendReadRequest(req);
    QCOMPARE(driver.lastReadReq.area, RegisterArea::DiscreteInput);
}

// ============================================================================
// ★ 2026-10-10:请求在途时变量被删 —— use-after-free
//
// 【真 bug,不是假设】
//   PollScheduler 把【裸 Variable*】存进四个容器:
//     nextPollAtMs_(key) / inFlight_(key) / activeVariables_(key) / pending_(value)
//   而 QObject 析构【不通知任何人】。于是:
//     1. tick 发出读请求,变量进了 pending_
//     2. 那个 Variable 被 delete —— 生产里最常见的触发是【换工程】:
//        Project::loadFromFile 第 4 步 varMgr_->clear() 一次删光全部变量。
//        ★ 用例里用 removeVariable() 代替,两者 emit 的是同一个信号,
//          对 PollScheduler 是同一条路径;换成 clear() 跑结论一样。
//        (顺带更正:这组注释原来写的是"用户在配置对话框里删变量",
//         那句话是错的 —— hmi.ui 里没有删变量的入口,照着找会找不到。)
//     3. PLC 回包到达,onDriverReadFinished 拿到悬空指针
//     4. v->setQuality(...) / v->address() → use-after-free
//
//   两个条件都不罕见:轮询 100ms 一次、Modbus 往返几毫秒到几百毫秒,
//   "请求在途"是【常态】而不是极端情况。
//
// 【为什么测试要专门造"延迟回包"】
//   MockDriver 默认同步立刻回包 —— 那样 sendReadRequest 返回时请求已经不在
//   pending_ 里了,"在途"这个窗口根本不存在,测不到任何东西。
//   manualReply 开关让测试自己掌握回包时机,窗口才是真的。
//
// 【这些用例怎么才算数】
//   1 号直接跑,不崩就算过 —— 但它只在 AddressSanitizer 下能真正证伪;
//   所以 2/3/4 号断的是【可观察的后果】,不依赖内存检查器。
// ============================================================================

void TestIPollDriver::deletingVariableWhileRequestInFlight_doesNotCrash() {
    VariableManager vm;
    MockDriver driver;
    driver.connectTo("127.0.0.1", 502);
    driver.manualReply = true;            // ★ 请求发出去但不回

    auto* v = new Variable(QStringLiteral("Speed"), 0x0002,
                           Variable::DataType::UInt16,
                           Variable::Access::ReadWrite, &vm);
    vm.addVariable(v);

    PollScheduler ps(&vm, &driver);
    ps.setAllActive();
    ps.start();
    QTest::qWait(250);                    // 让它真的发出去
    ps.stop();

    QVERIFY2(driver.sendReadCount >= 1, "请求根本没发出去,后面测的不是在途窗口");
    QVERIFY(!driver.heldRequestIds.isEmpty());

    // ★ 在途期间删掉变量 —— 这一步以前会让 PollScheduler 的容器里留下悬空指针
    QVERIFY(vm.removeVariable(QStringLiteral("Speed")));

    // ★ 回包到达。这在修复前是对已释放内存调用 v->setQuality()。
    //   不断言具体值 —— 这一条的作用是"跑到这里不崩"。
    driver.nextReplyRegisters = {4321};
    driver.flushReply();

    // ★ 变量确实已经不在了(证明上面那次 removeVariable 是真删,不是摆设)
    QCOMPARE(vm.count(), 0);
    QVERIFY(vm.findVariable(QStringLiteral("Speed")) == nullptr);
}

void TestIPollDriver::deletingVariableWhileRequestInFlight_removedFromPending() {
    // 上一条的加强版:不只"不崩",还得证明【后续 tick 不再碰它】。
    //
    // 悬空指针留在 QHash 的 key 里,危害不止于这一次解引用 ——
    // 下一轮 onTick 遍历 nextPollAtMs_ 时会重算哈希,同样踩已释放内存。
    // 所以这里多跑几轮 tick,证明清理是【彻底】的而不是【一次性绕过】。
    VariableManager vm;
    MockDriver driver;
    driver.connectTo("127.0.0.1", 502);
    driver.manualReply = true;

    auto* v = new Variable(QStringLiteral("Speed"), 0x0002,
                           Variable::DataType::UInt16,
                           Variable::Access::ReadWrite, &vm);
    vm.addVariable(v);

    PollScheduler ps(&vm, &driver);
    ps.setAllActive();
    ps.start();
    QTest::qWait(250);
    QVERIFY(!driver.heldRequestIds.isEmpty());   // ★ 确认请求确实还在途

    QVERIFY(vm.removeVariable(QStringLiteral("Speed")));

    // 回包 + 再跑几轮:两件事都要经得起
    driver.flushReply();
    const int afterDelete = driver.sendReadCount;
    QTest::qWait(350);      // 3+ 轮 tick,内部哈希容器会被反复遍历/重排

    // ★ 变量没了,一个变量都没有,就不该再发任何读请求。
    //   这条比"地址对不对"更硬:悬空 key 留在哈希里时,任何一次遍历/重排
    //   都可能踩到它,而症状(偶尔崩)恰恰是最难复现的那种。
    QCOMPARE(vm.count(), 0);
    QCOMPARE(driver.sendReadCount, afterDelete);
}

void TestIPollDriver::replacingVariable_keepsItBeingPolled() {
    // 同名【替换】:新的变量必须【继续被轮询】。
    //
    // ★ 这是删除路径和替换路径的唯一区别,也是最容易漏的一处:
    //   如果清理时只 remove 不 insert,替换一个正在显示的变量之后,
    //   UI 上的控件会【永久停止刷新】,而界面上看不出任何异常 ——
    //   用户看到的是一个"死掉"的画面,不会想到跟变量替换有关。
    VariableManager vm;
    MockDriver driver;
    driver.connectTo("127.0.0.1", 502);

    auto* v1 = new Variable(QStringLiteral("Speed"), 0x0002,
                            Variable::DataType::UInt16,
                            Variable::Access::ReadWrite, &vm);
    vm.addVariable(v1);

    PollScheduler ps(&vm, &driver);
    ps.setAllActive();

    // 同名替换 → emit variableReplaced(v1, v2)
    auto* v2 = new Variable(QStringLiteral("Speed"), 0x0002,
                            Variable::DataType::UInt16,
                            Variable::Access::ReadWrite, &vm);
    QVERIFY(vm.addVariable(v2));
    QCOMPARE(vm.findVariable(QStringLiteral("Speed")), v2);

    // ★ 替换之后还得能读到值 —— 这就是"还在被轮询"的可观察证据。
    driver.nextReplyRegisters = {777};
    ps.start();
    QTest::qWait(250);
    ps.stop();

    QVERIFY2(driver.sendReadCount >= 1, "替换之后完全不再轮询了 —— 画面会永久停住");
    QCOMPARE(v2->value().toUInt(), quint16(777));
}

void TestIPollDriver::deletingActiveVariable_removesItFromActiveSet() {
    // 删掉一个正在显示(active)的变量之后,新变量不能被加进 active 集合。
    // 反过来讲:删除之后 active 集合里不该再有那个地址对应的变量 ——
    // 表现就是"再加一个新变量,它的第一帧数据永远不来"。
    VariableManager vm;
    MockDriver driver;
    driver.connectTo("127.0.0.1", 502);

    auto* v = new Variable(QStringLiteral("Speed"), 0x0002,
                           Variable::DataType::UInt16,
                           Variable::Access::ReadWrite, &vm);
    vm.addVariable(v);

    PollScheduler ps(&vm, &driver);
    ps.setAllActive();

    QVERIFY(vm.removeVariable(QStringLiteral("Speed")));

    // 删掉之后再加一个【别的】变量,它必须能被正常轮询 ——
    // 证明 active 集合的清理没有把整个机制弄坏。
    auto* v2 = new Variable(QStringLiteral("Temp"), 0x0003,
                            Variable::DataType::UInt16,
                            Variable::Access::ReadWrite, &vm);
    vm.addVariable(v2);
    ps.setAllActive();

    driver.nextReplyRegisters = {999};
    ps.start();
    QTest::qWait(250);
    ps.stop();

    QVERIFY2(driver.sendReadCount >= 1, "删掉一个变量之后,别的变量也不再被轮询了");
    QCOMPARE(v2->value().toUInt(), quint16(999));
}

// ============================================================================
// ★ 2b(2026-10-19):多 driver 挂接的两条真 bug
//
// BUG-1  addDriver 收不到回包 —— C.5 的多 driver 骨架自己就是坏的
//   PollScheduler::addDriver 只往哈希里塞指针,【不接 readFinished】;
//   那个 connect 只写在 start() 里。而 hmi.cpp 的顺序是
//   pollScheduler_->start()(:412)在 project_->loadFromFile()(:640)【之前】。
//   于是运行期打开工程 / 应用 PLC 配置加进来的 driver,永远收不到任何回包。
//   现象:配置里有 plc2、变量也绑了 plc2,就是不出数,而且日志一声不吭。
//   —— 这不是"多 PLC 功能没做完",是【已有的多 driver 接口不能用】。
//
// BUG-2  地址不连续的变量永久卡住 —— 与多 PLC 无关,现在就存在的活 bug
//   onTick 的合并循环(PollScheduler.cpp:282-305)遇地址不连续会连发多帧,
//   但单 in-flight 防御只在循环【之前】判过一次。于是第 2 帧起
//   ModbusMaster 拒收(ModbusMaster.cpp:233-237):只 emit errorOccurred,
//   【不发 readFinished】。而 pending_[reqId] / inFlight_ 在发送前就登记了,
//   于是这批变量永远等不到 reply,inFlight_ 里再也出不来。
//   现场表现:同一台 PLC 上地址连续的那几个变量有值,
//             中间跳过几个地址的那几个永远停在旧值。
//
// 【负向对照的诚实说明 —— 为什么这些用例断言的是计数而不是"值变了两次"】
//   重复连接(readFinished 连了两次)的【数据层后果是静默 no-op】:
//   onDriverReadFinished 第一行就 pending_.take(reqId),第二次拿到空 batch 直接 return。
//   所以"连了两次"今天【不会】让值被写两遍,只表现为连接泄漏 + 未来脆弱。
//   照直觉写一条"值被处理了两次"的断言会得到一条【永远绿】的假测试。
//   这里改断言两样可观测量:
//     hookedDriverCount()   —— 净连接数(注意:它是 QSet,重复 insert 不增长,
//                              所以它证明不了"没多连",只能证明"没少连")
//     replyDispatchCount()  —— 实际进入分派的次数,断言它 == driver 的发帧数
//
// 【负向对照实测记录(2026-10-19),三轮全部实跑】
//   轮次一  拆掉 addDriver 里的 hookDriver 调用(BUG-1 的修复)
//            → 红 2:driverAddedAfterStart_receivesReplies
//                     addDriver_sameIdTwice_doesNotDoubleConnect
//            15 条老用例全绿。
//
//   轮次二  拆掉 onTick 里的返回值检查(BUG-2 的修复)
//            → 红 2:nonContiguousAddresses_allStillPoll
//                     sendReadRequestRefused_rollsBackInFlight
//            20 条全绿(含轮次一那两条)—— 与轮次一是【完全不同的一组】,
//            所以两轮都必须做:只做一轮的话,会误以为"已经测全了"。
//
//   轮次三  拆掉 hookDriver 里的幂等 disconnect
//            → 【全绿,22 条一条没红】。
//            ★ 这条不成立,如实记下来:那行 disconnect 今天【不可达】——
//              每个调用方都已经先摘干净了(addDriver 先 unhookDriver;
//              start() 有 if (timer_.isActive()) return 挡着,且 stop() 已全摘)。
//              它防的是"将来有人新加一个调用方忘了先摘",不是当前的行为。
//              留着是因为成本一行、漏掉的代价是静默的连接泄漏。
//              ★ 不要以为 hookDriver 那行被测试守着 —— 它没有。
//
//   诚实的定性:BUG-1/BUG-2 修的是【确定的、可观测的】后果(红绿分明);
//               幂等 disconnect 修的是【当前不可达】的隐患(无测试覆盖)。
// ============================================================================

void TestIPollDriver::driverAddedAfterStart_receivesReplies() {
    // ★ 本组用例的主线:钉死 BUG-1。
    //   改之前这条【必红】 —— addDriver 之后 driver 发多少回包,
    //   PollScheduler 一个都收不到,变量永远是默认值。
    VariableManager vm;
    MockDriver d1, d2;
    d1.connectTo("127.0.0.1", 502);
    d2.connectTo("127.0.0.1", 503);

    auto* v1 = new Variable(QStringLiteral("A"), 0x0001,
                            Variable::DataType::UInt16,
                            Variable::Access::ReadWrite, &vm);
    v1->setPlcId(QStringLiteral("plc1"));
    vm.addVariable(v1);

    auto* v2 = new Variable(QStringLiteral("B"), 0x0001,
                            Variable::DataType::UInt16,
                            Variable::Access::ReadWrite, &vm);
    v2->setPlcId(QStringLiteral("plc2"));     // ★ 关键:绑到运行期才加进来的那台
    vm.addVariable(v2);

    PollScheduler ps(&vm, &d1);               // 构造时只有 plc1
    ps.setAllActive();
    ps.start();                               // ★ start 在 addDriver 之前,复现真实顺序

    d2.connectTo("127.0.0.1", 503);           // driver 自己得先"连上",否则 onTick 跳过
    d1.nextReplyRegisters = {1111};
    d2.nextReplyRegisters = {2222};
    ps.addDriver(QStringLiteral("plc2"), &d2);   // ★ 运行期加入

    QCOMPARE(ps.hookedDriverCount(), 2);
    QTest::qWait(250);
    ps.stop();

    // plc1 是构造时就有的,本来就通 —— 它是这条用例的对照组:
    // 没有它的话,"两组都没值"和"只有新加的那组没值"分不清。
    QCOMPARE(v1->value().toUInt(), quint16(1111));
    QVERIFY2(v2->value().toUInt() == quint16(2222),
             "运行期 addDriver 进来的 driver 收不到回包 —— 这就是 BUG-1");
}

void TestIPollDriver::addDriver_sameIdTwice_doesNotDoubleConnect() {
    // 同 id 二次 addDriver(换 PLC 配置时会发生:同一个 plcId 指向新的 driver 对象)。
    //
    // ★ 不摘旧连接的后果:旧 driver 继续把回包打进同一个 onDriverReadFinished,
    //   而 requestId 是 PollScheduler 全局唯一的 —— 现象是
    //   "明明已经切到新 PLC 了,偶尔还会读到旧 PLC 的值"。
    //
    // 断言 replyDispatchCount() 而不是值,因为重复连接的数据层是 no-op(见文件头)。
    // ★ 已知局限(实测记录在文件头轮次三):把 hookDriver 的幂等 disconnect 拆掉,
    //   本用例【依然全绿】。真正保证不重复连接的是 addDriver 里那步 unhookDriver。
    //   所以本用例守的是"每个 requestId 恰好分派一次"这条不变量,
    //   而不是"那行 disconnect 生效"。
    VariableManager vm;
    MockDriver oldD, newD;
    oldD.connectTo("127.0.0.1", 502);
    newD.connectTo("127.0.0.1", 503);

    auto* v = new Variable(QStringLiteral("A"), 0x0001,
                           Variable::DataType::UInt16,
                           Variable::Access::ReadWrite, &vm);
    vm.addVariable(v);

    PollScheduler ps(&vm, &oldD);
    ps.setAllActive();
    ps.start();
    ps.addDriver(QStringLiteral("plc1"), &oldD);    // 先加一次

    const quint64 before = ps.replyDispatchCount();
    ps.addDriver(QStringLiteral("plc1"), &newD);    // ★ 同一个 id,换成新 driver
    ps.addDriver(QStringLiteral("plc1"), &newD);    // ★ 再加一次同样的
    QCOMPARE(ps.hookedDriverCount(), 1);

    newD.nextReplyRegisters = {3333};
    QTest::qWait(250);
    ps.stop();

    const quint64 dispatched = ps.replyDispatchCount() - before;
    QVERIFY2(newD.sendReadCount >= 1, "新 driver 根本没被用 —— 还在走旧的");
    QCOMPARE(v->value().toUInt(), quint16(3333));
    // ★ 每个 requestId 分派一次,被重复连接的话这里会是 dispatched 的整数倍(≥2 倍)。
    QCOMPARE(dispatched, quint64(newD.sendReadCount));
}

void TestIPollDriver::stopThenAddDriver_doesNotResurrectConnections() {
    // stop() 之后 addDriver 不该把 readFinished 接回来。
    //
    // 【为什么这条重要】tst_IPollDriver 里约 7 条用例把 ps.stop() 当"拆解"用 ——
    //   跑完就当没在轮询了。如果 stop() 之后 addDriver 又连上,
    //   那些用例会看到本不该有的回包流入,整组测试的语义就废了。
    VariableManager vm;
    MockDriver d1, d2;
    d1.connectTo("127.0.0.1", 502);
    d2.connectTo("127.0.0.1", 503);

    auto* v2 = new Variable(QStringLiteral("B"), 0x0001,
                            Variable::DataType::UInt16,
                            Variable::Access::ReadWrite, &vm);
    v2->setPlcId(QStringLiteral("plc2"));
    vm.addVariable(v2);

    PollScheduler ps(&vm, &d1);
    ps.setAllActive();
    ps.start();
    ps.stop();                                  // ★ 拆解

    QCOMPARE(ps.hookedDriverCount(), 0);         // stop 把连接摘干净了
    ps.addDriver(QStringLiteral("plc2"), &d2);

    QCOMPARE(ps.hookedDriverCount(), 0);
    QCOMPARE(ps.replyDispatchCount(), quint64(0));
}

void TestIPollDriver::start_stop_start_doesNotAccumulateConnections() {
    // start → stop → start 三轮之后,净连接数仍然是 1。
    // 幂等挂接的意义就在这里:不靠调用方约定顺序。
    VariableManager vm;
    MockDriver d;
    d.connectTo("127.0.0.1", 502);
    PollScheduler ps(&vm, &d);
    ps.setAllActive();

    ps.start();
    QCOMPARE(ps.hookedDriverCount(), 1);
    ps.stop();
    QCOMPARE(ps.hookedDriverCount(), 0);
    ps.start();
    QCOMPARE(ps.hookedDriverCount(), 1);

    // 再来一轮,确认不是"凑巧"
    ps.stop();
    ps.start();
    QCOMPARE(ps.hookedDriverCount(), 1);

    const quint64 before = ps.replyDispatchCount();
    d.nextReplyRegisters = {9};
    QTest::qWait(250);
    ps.stop();
    QCOMPARE(ps.replyDispatchCount() - before, quint64(d.sendReadCount));
}

void TestIPollDriver::nonContiguousAddresses_allStillPoll() {
    // ★ 钉死 BUG-2。
    //
    // 三个变量地址【不连续】(0x01 / 0x05 / 0x0A),所以 onTick 的合并循环会
    // 拆成三批、连发三帧。而 serialInFlight 让 driver 照 ModbusMaster 的样子
    // 拒收第 2、3 帧(只 errorOccurred,不发 readFinished)。
    //
    // 改之前:第 2、3 批的变量永久卡在 inFlight_ 里,永远是默认值。
    // 改之后:每轮都被回滚,100ms 后重试,三批最终都轮得到。
    //
    // 回包内容 = 请求的地址(replyWithRequestAddr),所以断言的是
    // "每个变量都拿到了【自己那个地址】的值" —— 不是"三组都有值"。
    // 用同一个 nextReplyRegisters 的话,哪怕调度器只轮了第一批,
    // 后面两组停在默认值也可能碰巧看着像对的。
    VariableManager vm;
    MockDriver d;
    d.connectTo("127.0.0.1", 502);
    d.serialInFlight = true;        // 模仿 ModbusMaster 的单 in-flight
    d.manualReply = true;           // 请求留在途,由测试决定何时回
    d.replyWithRequestAddr = true;

    auto* a = new Variable(QStringLiteral("A"), 0x0001,
                           Variable::DataType::UInt16,
                           Variable::Access::ReadWrite, &vm);
    auto* b = new Variable(QStringLiteral("B"), 0x0005,
                           Variable::DataType::UInt16,
                           Variable::Access::ReadWrite, &vm);
    auto* c = new Variable(QStringLiteral("C"), 0x000A,
                           Variable::DataType::UInt16,
                           Variable::Access::ReadWrite, &vm);
    vm.addVariable(a); vm.addVariable(b); vm.addVariable(c);

    PollScheduler ps(&vm, &d);
    ps.setAllActive();
    ps.start();

    // ★ 多跑几轮:第一轮只有第一批能发出去,后面两批要等回滚后重试。
    //   期间还要手动把在途的回掉,否则 pendingCount 一直是 1,后面全被跳过。
    for (int round = 0; round < 12; ++round) {
        QTest::qWait(60);
        d.flushReply();
    }
    ps.stop();

    QCOMPARE(a->value().toUInt(), quint16(0x01));
    QVERIFY2(b->value().toUInt() == quint16(0x05),
             "地址不连续的第二个变量组永久卡住了 —— 这就是 BUG-2");
    QVERIFY2(c->value().toUInt() == quint16(0x0A),
             "地址不连续的第三个变量组永久卡住了 —— 这就是 BUG-2");
}

void TestIPollDriver::sendReadRequestRefused_rollsBackInFlight() {
    // BUG-2 的最小版:一个请求被拒之后,变量必须【回到可轮询状态】。
    //
    // 断言的是"下一轮还能再发",而不是"发了几次":
    // 改之前那批变量在 inFlight_ 里出不来,sendReadCount 会【停在 1】。
    VariableManager vm;
    MockDriver d;
    d.connectTo("127.0.0.1", 502);
    d.serialInFlight = true;
    d.manualReply = true;
    d.replyWithRequestAddr = true;

    // 两个不连续的变量 → 每次 tick 第一批成功、第二批被拒
    auto* a = new Variable(QStringLiteral("A"), 0x0001,
                           Variable::DataType::UInt16,
                           Variable::Access::ReadWrite, &vm);
    auto* b = new Variable(QStringLiteral("B"), 0x0009,
                           Variable::DataType::UInt16,
                           Variable::Access::ReadWrite, &vm);
    vm.addVariable(a); vm.addVariable(b);

    PollScheduler ps(&vm, &d);
    ps.setAllActive();
    ps.start();

    for (int round = 0; round < 12; ++round) {
        QTest::qWait(60);
        d.flushReply();
    }
    ps.stop();

    QVERIFY2(d.sendReadCount >= 2,
             "被拒的请求没有回滚,变量卡死在 inFlight_,之后再也不发了 —— BUG-2");
    QCOMPARE(b->value().toUInt(), quint16(0x09));
}

QTEST_MAIN(TestIPollDriver)
#include "tst_IPollDriver.moc"
