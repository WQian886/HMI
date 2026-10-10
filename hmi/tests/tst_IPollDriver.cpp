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
    int pendingCount() const override { return 0; }  // mock 永远单 in-flight 0

    void sendReadRequest(const ReadRequest& req) override {
        lastReadReq = req;
        ++sendReadCount;
        if (manualReply) {
            heldRequestIds << req.requestId;
            return;      // ★ 不回包,请求留在"在途"状态
        }
        // 模拟立刻回复(同步)
        emit readFinished(req.requestId, nextReplyRegisters, nextReplySuccess);
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
            emit readFinished(id, nextReplyRegisters, nextReplySuccess);
        }
    }

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

QTEST_MAIN(TestIPollDriver)
#include "tst_IPollDriver.moc"
