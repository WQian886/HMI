// ============================================================================
// tst_WriteSchedulerMultiPlc —— 多 PLC 2b(2026-10-19):写侧按 plcId 路由
//
// 【这个 bug 为什么是全套里最严重的】
//   2b 之前 WriteScheduler 全代码【零 plcId】。构造函数收一个 ModbusMaster*,
//   onTick 里 modbus_->sendWriteRequest(unit, 1, reqId) —— 那一个 driver 是
//   plcs[0] 建出来的,那个 slaveId 是写死的字面量 1。
//
//   于是:用户在变量上填 plc2,界面上值变了、审计日志记了、
//   WriteFailChecker 还可能为它报过警 —— 而字节从 plc1 的 socket 发了出去。
//
//   ★ 没有任何一行报错。操作员点 2 号机的按钮,1 号机的数值变了。
//   这比 2a 那个"文件里少两台"严重一个量级:那个是丢数据,这个是【写错设备】。
//
// 【本文件钉的是什么】
//   ★ 值本身编码身份。plc1 的变量填 0x1111、plc2 填 0x2222、plc3 填 0x3333,
//     然后断言【每个 driver 收到的正是它自己那台的地址和值】。
//     只比"每台都发了一次"太弱 —— 一个把三台的值都发给 plc1 的实现也能过。
//
// 【为什么 MockWriteDriver 是新写的,不复用 tst_IPollDriver 的 MockDriver】
//   那边那个 MockDriver 的 sendReadRequest 带了 serialInFlight /
//   replyWithRequestAddr 这些读路径专用开关,是为了钉 2a 阶段 PollScheduler
//   的合并读行为。写路径一个都不需要,复用它等于把读路径的断言语义一起搬过来 ——
//   改那边的会波及那 4 条已有断言。新写一份,两边互不干扰。
//
// 【负向对照 —— 实测记录(2026-10-19,两轮都真跑过,数字是原样抄的)】
//
// ★ 为什么要两轮:两处判据判错的后果【不对称】,只破一个方向证明不了另一方向。
//   第一轮破"按 plcId 找 driver",第二轮破"每台 PLC 各自算在途"。
//   两轮红的用例集几乎不重叠 —— 这就是必须做两轮的实证理由。
//
// ── 第一轮:把 driver 查找退化成"拿第一个 driver" ────────────────
//   改法:`if (!driver) driver = provider_->driverFor(plcIds.first());`
//         —— 正是 2b 之前的行为(那时只有一个 driver,没有"按 plcId 找"这回事)。
//   结果:16 条里【只红 1 条】
//     ✗ plc2DriverMissing_doesNotBlockPlc1
//         失败信息:d1_ 收到的不是 0x1111。读出来是 plc2 的 0x2222 ——
//         "plc2 没配 driver,于是它的值从 plc1 的 socket 发了出去"。
//         ★ 这就是那个静默写错设备的 bug 的样子,和修复前现场完全一致。
//   ★ 其余 15 条【全绿】。包括 eachPlc_receivesItsOwnVariable ——
//     因为那三台 driver 都存在,退化逻辑根本不会被触发。
//     【诚实说明】所以"第一条用例红了"不等于"其余用例没价值",
//     只等于【它们各自守的是别的判据】。第一轮单独做是不够的。
//
// ── 第二轮:把 per-PLC 在途退回成 2b 之前的"全局单在途" ──────────
//   改法:循环开头加 `if (!pending_.isEmpty()) continue;`
//         —— 那时是 onTick 开头判一次 modbus_->pendingCount()。
//   结果:16 条里【红 7 条】
//     ✗ eachPlc_receivesItsOwnVariable
//     ✗ slaveId_comesFromProvider_notHardcodedOne
//     ✗ plc2DriverMissing_doesNotBlockPlc1
//     ✗ onePlcDisconnected_othersStillWrite
//     ✗ onePlcHasInflight_othersStillWrite
//     ✗ oneWritePerPlcPerTick
//     ✗ retryExhausted_stopsAndAlarms
//     ✓ sendRefused / unknownRequestId / variableDeletedWhileInflight /
//       variableReplacedWhileInflight / auditEntry / 两个 writeFailed 全绿
//         —— 合理:那几条测的是"发出去之后"的账,与并发度无关。
//
// ── 两轮的交集只有 plc2DriverMissing 一条 ────────────────────────
//   结论(以及它为什么值得记下来):这两处判据【各自独立地】承重。
//   只测第一轮会漏掉"一台卡住的 PLC 能瘫痪全部写入";
//   只测第二轮会漏掉"缺 driver 的 PLC 把值写进别的机器"。
//
// ★★ 过程中修掉的一个【测试自身的】缺陷(值得单列,因为它伪装成了产品 bug):
//   第二轮第一次跑,slaveId 那条直接 QFATAL abort,整个进程挂了 ——
//   Totals 只报 "1 passed, 2 failed",后面十几条一条都没跑到。
//   原因:`QCOMPARE(d2_->sent().size(), 1)` 失败【不中断本函数】
//   (Qt Test 只记一条 FAIL 就继续往下走),于是下一行
//   `d2_->sent().at(0)` 在空列表上触发 Q_ASSERT → abort。
//   → 在所有 at(0)/last() 之前补 QVERIFY(!isEmpty()) 挡一道。
//   这跟负向对照无关,是用例写法问题:一条红炸掉整个文件,比它红本身更糟。
//
// 关联:WriteScheduler(本文件的主角), IPlcDriverProvider(被问的那个接口),
//      Variable / VariableManager(脏变量的来源)
// ============================================================================

#include <QtTest>
#include <QPointer>
#include <QVector>

#include "WriteScheduler.h"
#include "Variable.h"
#include "VariableManager.h"
#include "drivers/IPollDriver.h"
#include "drivers/IPlcDriverProvider.h"

using qwhmi::drivers::IPollDriver;
using qwhmi::drivers::IPlcDriverProvider;
using qwhmi::drivers::RegisterArea;

namespace {

// ★ 全新写的写路径 mock,理由见文件头。
//
//   它跟真实 driver 的关键差别:sendWriteRequest 【不】发信号,
//   由测试显式调 completeWrite() 来模拟 reply 回来。
//   真实 ModbusMaster 的 writeFinished 有三种来源(成功应答 / 异常应答 /
//   超时扫描),本文件不需要覆盖那三种 —— 那属于 ModbusMaster 的测试
//   (tst_ModbusFrame / tst_TcpClient 已经在管),这里只管"调度器派得对不对"。
struct SentWrite {
    RegisterArea area;
    int          slaveId;
    int          addr;
    QVector<quint16> values;
    quint32      requestId;
};

class MockWriteDriver : public IPollDriver {
    Q_OBJECT
public:
    explicit MockWriteDriver(bool connected = true, QObject* parent = nullptr)
        : IPollDriver(parent), connected_(connected) {}

    void connectTo(const QString&, quint16) override {}
    void disconnect() override { connected_ = false; }
    bool isConnected() const override { return connected_; }
    int  pendingCount() const override { return inflight_; }
    QString protocolName() const override { return QStringLiteral("modbus"); }

    bool sendReadRequest(const ReadRequest&) override { return true; }

    bool sendWriteRequest(RegisterArea area, int slaveId, int addr,
                          const QVector<quint16>& values, quint32 requestId) override {
        // ★ acceptNext_ = false 用来模拟"driver 拒收"(真实 ModbusMaster 在
        //   上一帧未完成时会这样返回 nullptr 且【不发】writeFinished)。
        //   【拒一次就恢复】—— refuseNext 的字面意思。别写成永久拒绝:
        //   那会让"下一帧还能不能发"这类断言永远红,而红的��因是 mock 而不是产品。
        if (!acceptNext_) {
            acceptNext_ = true;
            ++refused_;
            return false;
        }
        SentWrite w{area, slaveId, addr, values, requestId};
        sent_.append(w);
        ++inflight_;
        return true;
    }

    // ★ 只拒【下一次】。名字叫 refuseNext 就是这个意思 ——
    //   我第一版写成"永久拒绝"(忘了在 sendWriteRequest 里恢复),
    //   于是 sendRefused 那条用例第二帧也发不出去,
    //   现象是 QCOMPARE(sent().size(), 1) 拿到 0,看起来像调度器不再发,
    //   实际是 mock 自己把路堵死了。这类"mock 的 bug 伪装成产品 bug"最难查。
    void refuseNext() { acceptNext_ = false; }

    // 模拟 reply 回来
    void completeWrite(quint32 requestId, bool success = true) {
        if (inflight_ > 0) --inflight_;
        emit writeFinished(requestId, success);
    }

    const QList<SentWrite>& sent() const { return sent_; }
    int refusedCount() const { return refused_; }
    void clearSent() { sent_.clear(); }

private:
    bool connected_;
    bool acceptNext_ = true;
    int  inflight_   = 0;
    int  refused_    = 0;
    QList<SentWrite> sent_;
};

// ★ 假的 provider —— 证明 WriteScheduler 不依赖 PlcConnectionManager 那个实现类。
//   只查哈希,查不到回 nullptr(【不】退化成取第一个 —— 那正是要消灭的 bug)。
class MockProvider : public IPlcDriverProvider {
public:
    void add(const QString& plcId, IPollDriver* d, int slaveId) {
        drivers_.insert(plcId, d);
        slaves_.insert(plcId, slaveId);
    }
    void remove(const QString& plcId) {
        drivers_.remove(plcId);
        slaves_.remove(plcId);
    }

    IPollDriver* driverFor(const QString& plcId) const override {
        return drivers_.value(plcId, nullptr);
    }
    int slaveIdFor(const QString& plcId) const override {
        return slaves_.value(plcId, 1);   // 缺省 1,跟生产实现一致
    }

private:
    QHash<QString, IPollDriver*> drivers_;
    QHash<QString, int> slaves_;
};

// 建一个可写的变量(ReadWrite + 已标脏),这是 WriteScheduler 唯一会发的组合
//
// ★ setValue 必须喂【精确类型】。Variable::setValue 先过 matchesDataType
//   (Variable.cpp:35-45),Int16 那一支判的是 v.typeId() == QMetaType::Short ——
//   喂 int 会被 Rejected,而 Rejected 是【静默】的:值没设上,只打一条 qWarning。
//   症状是 buildWritePayload 拿到 toUInt() == 0,写出去的寄存器全是 0。
//   这里显式转 short,不靠隐式转换。
Variable* makeDirtyVar(const QString& name, const QString& plcId, int addr, int value) {
    auto* v = new Variable(name,
                           static_cast<quint16>(addr),
                           Variable::DataType::Int16,
                           Variable::Access::ReadWrite);
    v->setPlcId(plcId);
    v->setValue(QVariant::fromValue(static_cast<short>(value)));
    v->markDirty();
    return v;
}

} // namespace

// ============================================================================

class tst_WriteSchedulerMultiPlc : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    // ---------- ★★★ 核心:各回各的 ----------
    void eachPlc_receivesItsOwnVariable();
    void slaveId_comesFromProvider_notHardcodedOne();

    // ---------- 故障隔离 ----------
    void plc2DriverMissing_doesNotBlockPlc1();
    void onePlcDisconnected_othersStillWrite();
    void onePlcHasInflight_othersStillWrite();

    // ---------- 不许泄漏 ----------
    void sendRefused_leavesNoPendingEntry();
    void unknownRequestId_replyIsSafe();
    void variableDeletedWhileInflight_replyIsSafe();
    void variableReplacedWhileInflight_replyStillClearsDirty();

    // ---------- 每帧一发 ----------
    void oneWritePerPlcPerTick();

    // ---------- 审计 / 报警 ----------
    void auditEntry_recordsTheActualPlcId();
    void writeFailed_oldSignatureStillEmitted();
    void writeFailed_newSignatureCarriesPlcId();
    void retryExhausted_stopsAndAlarms();

private:
    VariableManager*  vm_    = nullptr;
    MockProvider*     prov_  = nullptr;
    MockWriteDriver*  d1_    = nullptr;
    MockWriteDriver*  d2_    = nullptr;
    MockWriteDriver*  d3_    = nullptr;
    WriteScheduler*   ws_    = nullptr;
};

// ---------------------------------------------------------------------------

void tst_WriteSchedulerMultiPlc::init() {
    vm_   = new VariableManager(this);
    prov_ = new MockProvider;
    d1_ = new MockWriteDriver(true);
    d2_ = new MockWriteDriver(true);
    d3_ = new MockWriteDriver(true);
    prov_->add(QStringLiteral("plc1"), d1_, 1);
    prov_->add(QStringLiteral("plc2"), d2_, 5);
    prov_->add(QStringLiteral("plc3"), d3_, 9);

    ws_ = new WriteScheduler(vm_, prov_, this);
    // ★ 三台都要 hookDriver —— writeFinished 的连接是运行期建的(生产里由
    //   PlcConnectionManager 的 driverAdded 驱动),不 hook 就没人接 reply。
    //   我第一版漏了这三行,现象很有教育意义:9 条用例红,但红的地方全是
    //   "reply 之后"的断言(sent 的断言全绿)。—— 【发得出去,收不回来】。
    ws_->hookDriver(QStringLiteral("plc1"), d1_);
    ws_->hookDriver(QStringLiteral("plc2"), d2_);
    ws_->hookDriver(QStringLiteral("plc3"), d3_);
    // ★ 不 start() —— onTick 是 private slot,靠 QMetaObject::invokeMethod 驱动。
    //   走真实定时器的话每条用例都得 QTRY 100ms,25 条就是好几秒的纯等待,
    //   而且引入"这一帧有没有跑"的时序不确定性。手动 invoke 是确定性的。
}

void tst_WriteSchedulerMultiPlc::cleanup() {
    delete ws_;    ws_ = nullptr;
    delete prov_;  prov_ = nullptr;
    delete d1_;    d1_ = nullptr;
    delete d2_;    d2_ = nullptr;
    delete d3_;    d3_ = nullptr;
    delete vm_;    vm_ = nullptr;
}

// 一帧。onTick 是 private slot,用 invokeMethod 调(QMetaObject::invokeMethod
// 能调 private slot —— 这是 Qt 的既有行为,不是我们 hack 出来的)。
void tick(WriteScheduler* ws) {
    QMetaObject::invokeMethod(ws, "onTick", Qt::DirectConnection);
}

// ---------------------------------------------------------------------------
// 核心
// ---------------------------------------------------------------------------

void tst_WriteSchedulerMultiPlc::eachPlc_receivesItsOwnVariable() {
    // ★★★ 值本身编码身份 —— 这是"各回各的"最强的证明形式。
    //   只断言"每台都发了一次"太弱:一个把三台的值全发给 plc1 的实现也能过。
    vm_->addVariable(makeDirtyVar(QStringLiteral("V1"), QStringLiteral("plc1"), 100, 0x1111));
    vm_->addVariable(makeDirtyVar(QStringLiteral("V2"), QStringLiteral("plc2"), 200, 0x2222));
    vm_->addVariable(makeDirtyVar(QStringLiteral("V3"), QStringLiteral("plc3"), 300, 0x3333));

    tick(ws_);

    QCOMPARE(d1_->sent().size(), 1);
    QCOMPARE(d1_->sent().at(0).addr, 100);
    QCOMPARE(d1_->sent().at(0).values.at(0), quint16(0x1111));

    QCOMPARE(d2_->sent().size(), 1);
    // ★ 这里【不能】直接写 d2_->sent().at(0) —— QCOMPARE(sent().size(), 1)
    //   失败时 Qt Test 不会中断本函数(它只是记录一条 FAIL 就往下走),
    //   于是 at(0) 在空列表上触发 QFATAL,整个进程 abort,
    //   后面十几条用例【一条都跑不到】。一条红炸掉全文件,比它红本身更糟。
    //   先 QVERIFY 把"根本没发"这个事实钉死,后面的 at(0) 才有东西可取。
    QVERIFY2(!d2_->sent().isEmpty(), "plc2 的 driver 一条都没收到 —— 先看这个。");
    QVERIFY2(d2_->sent().at(0).addr == 200 && d2_->sent().at(0).values.at(0) == quint16(0x2222),
             "★ plc2 的 driver 必须收到 plc2 的变量。改之前没有 plcId 这个概念,"
             "三个变量全都从 plcs[0] 的 socket 发出去 —— 界面无异常,现场写错设备。");

    QCOMPARE(d3_->sent().size(), 1);
    QVERIFY2(!d3_->sent().isEmpty(), "plc3 的 driver 一条都没收到 —— 先看这个。");
    QVERIFY2(d3_->sent().at(0).addr == 300 && d3_->sent().at(0).values.at(0) == quint16(0x3333),
             "★ 同上,plc3 必须是 300 / 0x3333。");
}

void tst_WriteSchedulerMultiPlc::slaveId_comesFromProvider_notHardcodedOne() {
    // ★ 三个 provider 里 slaveId 分别是 1 / 5 / 9,全都 != 1。
    //   改之前那行是 modbus_->sendWriteRequest(unit, 1, reqId) —— 字面量 1。
    //   一台 PLC 一个 unit id 的场景(串口网关 / 多从站)下,
    //   发出去的 unit id 全是 1 = 每次都打到同一台从站,另外几台永远收不到。
    vm_->addVariable(makeDirtyVar(QStringLiteral("V1"), QStringLiteral("plc1"), 10, 1));
    vm_->addVariable(makeDirtyVar(QStringLiteral("V2"), QStringLiteral("plc2"), 20, 2));
    vm_->addVariable(makeDirtyVar(QStringLiteral("V3"), QStringLiteral("plc3"), 30, 3));

    tick(ws_);

    // ★ 同 eachPlc_receivesItsOwnVariable:at(0) 前必须先挡住空列表,
    //   否则这条一红就 QFATAL abort,整个文件的用例都跑不到。
    QVERIFY(!d1_->sent().isEmpty());
    QVERIFY(!d2_->sent().isEmpty());
    QVERIFY(!d3_->sent().isEmpty());
    QCOMPARE(d1_->sent().at(0).slaveId, 1);
    QCOMPARE(d2_->sent().at(0).slaveId, 5);
    QCOMPARE(d3_->sent().at(0).slaveId, 9);
}

// ---------------------------------------------------------------------------
// 故障隔离
// ---------------------------------------------------------------------------

void tst_WriteSchedulerMultiPlc::plc2DriverMissing_doesNotBlockPlc1() {
    // ★ 这条挡的是最坏的一种退化:driverFor 返回 null 时
    //   "退而取第一个 driver"(2b 之前没有这个接口,退化是自然发生的)。
    //   那会把 plc2 的值写进 plc1 —— 比不写更糟。
    vm_->addVariable(makeDirtyVar(QStringLiteral("V1"), QStringLiteral("plc1"), 100, 0x1111));
    vm_->addVariable(makeDirtyVar(QStringLiteral("V2"), QStringLiteral("plc2"), 200, 0x2222));
    vm_->addVariable(makeDirtyVar(QStringLiteral("V3"), QStringLiteral("plc3"), 300, 0x3333));

    prov_->remove(QStringLiteral("plc2"));   // plc2 那台没建起来

    tick(ws_);

    QVERIFY2(d2_->sent().isEmpty(), "plc2 没有 driver,就不该有东西发到它那儿。");
    QVERIFY2(d1_->sent().size() == 1 && d1_->sent().at(0).values.at(0) == quint16(0x1111),
             "★ plc2 缺 driver 【绝不能】挡住 plc1 —— 一台没配好的 PLC 会让全部写入静默停摆。");
    QVERIFY2(d1_->sent().at(0).addr != 200,
             "★ plc1 的 driver 绝不能收到 plc2 的变量(200 / 0x2222)。"
             "这是 2b 要消灭的核心 bug:给 plc2 的值从 plc1 的 socket 发出去,零日志。");
    QCOMPARE(d3_->sent().size(), 1);       // plc3 不受影响
}

void tst_WriteSchedulerMultiPlc::onePlcDisconnected_othersStillWrite() {
    vm_->addVariable(makeDirtyVar(QStringLiteral("V1"), QStringLiteral("plc1"), 100, 0x1111));
    vm_->addVariable(makeDirtyVar(QStringLiteral("V2"), QStringLiteral("plc2"), 200, 0x2222));
    vm_->addVariable(makeDirtyVar(QStringLiteral("V3"), QStringLiteral("plc3"), 300, 0x3333));

    d2_->disconnect();   // plc2 掉线

    tick(ws_);

    QCOMPARE(d1_->sent().size(), 1);
    QCOMPARE(d3_->sent().size(), 1);
    QVERIFY(d2_->sent().isEmpty());
    // 变量保持 dirty,连上后下一帧自然就发了 —— 所以这里【不能】clearDirty
    Variable* v2 = vm_->findVariable(QStringLiteral("V2"));
    QVERIFY(v2->isDirty());
}

void tst_WriteSchedulerMultiPlc::onePlcHasInflight_othersStillWrite() {
    // ★ 改之前是【全局单在途】:onTick 开头判一次 pendingCount(),
    //   一台卡住 = 全部停止写入。多 PLC 之后那等于一台 PLC 能瘫痪整条写通路。
    vm_->addVariable(makeDirtyVar(QStringLiteral("V1a"), QStringLiteral("plc1"), 100, 1));
    vm_->addVariable(makeDirtyVar(QStringLiteral("V1b"), QStringLiteral("plc1"), 101, 2));
    // ★ plc2 也要两个变量。写成唯一一个的话,回掉它的写之后就没有脏变量了,
    //   "plc2 还能不能继续发"这个问题就失去了对象 ——
    //   而"卡住的 plc1 不挡 plc2"恰恰要在【双方都有活干】时才看得出来。
    vm_->addVariable(makeDirtyVar(QStringLiteral("V2a"), QStringLiteral("plc2"), 200, 3));
    vm_->addVariable(makeDirtyVar(QStringLiteral("V2b"), QStringLiteral("plc2"), 201, 4));

    tick(ws_);   // 第一帧:两个桶各发一个 → plc1 和 plc2 各有一个在途

    QCOMPARE(d1_->sent().size(), 1);
    QCOMPARE(d2_->sent().size(), 1);

    // ★ 先把 plc2 自己那条回掉 —— 不然 plc2 也有在途,
    //   "plc2 还能不能发"这个问题的答案会是"它自己也有一个",跟 plc1 无关。
    //   回掉之后 plc2 空闲、plc1 卡着,才真正构成本用例想说的局面。
    d2_->completeWrite(d2_->sent().at(0).requestId);

    tick(ws_);

    QVERIFY2(d1_->sent().size() == 1,
             "plc1 自己有在途,本帧不该再发 —— 协议串行,每台每帧一个。");
    QVERIFY2(d2_->sent().size() == 2,
             "★ plc1 有在途不该挡住 plc2。改之前是全局单在途,plc1 卡住 = plc2 也停写。");

    // 回掉 plc2 第二条,plc1 依旧卡着 → 仍然应该只有 plc2 在动
    d2_->completeWrite(d2_->sent().at(1).requestId);
    tick(ws_);
    QCOMPARE(d1_->sent().size(), 1);   // plc1 从头到尾只发过一次

    // 现在回掉 plc1 的在途 → plc1 恢复
    d1_->completeWrite(d1_->sent().at(0).requestId);
    tick(ws_);
    QCOMPARE(d1_->sent().size(), 2);
}

// ---------------------------------------------------------------------------
// 不许泄漏
// ---------------------------------------------------------------------------

void tst_WriteSchedulerMultiPlc::sendRefused_leavesNoPendingEntry() {
    // ★★★ 这条钉的是 2b 发现的一个存量泄漏。
    //
    //   改之前 onTick 是"先 pending_[reqId] = v,再 modbus_->sendWriteRequest(...)"。
    //   而 ModbusMaster 拒收请求时【只 emit errorOccurred,不发 writeFinished】——
    //   于是那个 pending_ 条目永远等不到 reply,留在哈希里。
    //   泄漏本身当时不致命(没有"pending 非空就跳过"的逻辑),
    //   但它是 2b 引入 per-PLC 在途计数的前置地雷:
    //   一旦加了 inflightPerPlc_[reqId] = 1,那条永不消失的条目
    //   会让【那台 PLC 从此再也不写任何东西】。
    vm_->addVariable(makeDirtyVar(QStringLiteral("V1"), QStringLiteral("plc1"), 100, 42));

    d1_->refuseNext();
    tick(ws_);

    QVERIFY2(ws_->pendingCount() == 0,
             "★ driver 拒收请求时【不许】登记 pending —— 它不会发 writeFinished,"
             "登记进去就是一条永远等不到的记录。");
    QVERIFY(d1_->refusedCount() == 1);

    // ★ 关键:下一帧必须还能发。拒收不是"这台 PLC 死了"。
    tick(ws_);
    QCOMPARE(d1_->sent().size(), 1);
    QVERIFY2(ws_->pendingCount() == 1, "第二次成功发出后应该有且只有一条在途。");
}

void tst_WriteSchedulerMultiPlc::unknownRequestId_replyIsSafe() {
    // 收到了一个从没用过的 reqId —— 不能崩,不能误动别的变量。
    vm_->addVariable(makeDirtyVar(QStringLiteral("V1"), QStringLiteral("plc1"), 100, 42));
    tick(ws_);
    QVERIFY(ws_->pendingCount() == 1);

    d1_->completeWrite(0xDEADBEEF, true);   // 从没用过的 id

    // 原先那条还在,不该被这条 reply 顺手清掉
    QCOMPARE(ws_->pendingCount(), 1);
    Variable* v1 = vm_->findVariable(QStringLiteral("V1"));
    QVERIFY2(v1->isDirty(), "未知 reqId 的 reply 不该碰任何变量。");
}

void tst_WriteSchedulerMultiPlc::variableDeletedWhileInflight_replyIsSafe() {
    // ★ pending_ 的值改成 QPointer 就是为这条:
    //   变量在途时被删掉(用户换个工程),reply 回来时不能 UAF。
    //   而且 —— inflightPerPlc_ 的计数【必须无条件减掉】,
    //   不然 plc1 会永远停在"有在途",从此再也不写任何东西。
    vm_->addVariable(makeDirtyVar(QStringLiteral("V1"), QStringLiteral("plc1"), 100, 42));
    vm_->addVariable(makeDirtyVar(QStringLiteral("V2"), QStringLiteral("plc1"), 101, 43));

    tick(ws_);
    QCOMPARE(d1_->sent().size(), 1);
    const quint32 reqId = d1_->sent().at(0).requestId;
    QCOMPARE(ws_->pendingCount(), 1);

    // 清空 VariableManager —— V1 被 delete
    vm_->clear();
    QVERIFY(vm_->findVariable(QStringLiteral("V1")) == nullptr);

    d1_->completeWrite(reqId, true);   // reply 回来,变量已经不在了

    QVERIFY2(ws_->pendingCount() == 0, "reply 之后 pending_ 应该清空。");

    // ★ 换个新工程继续写:这台 PLC 必须还能发
    vm_->addVariable(makeDirtyVar(QStringLiteral("V3"), QStringLiteral("plc1"), 102, 44));
    tick(ws_);
    QVERIFY2(d1_->sent().size() == 2,
             "★ 变量被删后 plc1 必须还能写。若这里红,说明 inflightPerPlc_ "
             "没有在 reply 时无条件递减 —— 那台 PLC 从此静默停止所有写入。");
}

void tst_WriteSchedulerMultiPlc::variableReplacedWhileInflight_replyStillClearsDirty() {
    // loadFromFile 重建 Variable 时发 variableReplaced(old, new)。
    //   这时那个请求【本来是能写成功的】,不该因为"指针换了"就被静默丢弃。
    vm_->addVariable(makeDirtyVar(QStringLiteral("V1"), QStringLiteral("plc1"), 100, 42));
    tick(ws_);
    QCOMPARE(d1_->sent().size(), 1);
    const quint32 reqId = d1_->sent().at(0).requestId;

    Variable* oldVar = vm_->findVariable(QStringLiteral("V1"));
    auto* newVar = makeDirtyVar(QStringLiteral("V1"), QStringLiteral("plc1"), 100, 42);
    vm_->addVariable(newVar);   // 同名替换,内部会 emit variableReplaced

    d1_->completeWrite(reqId, true);

    QVERIFY2(!newVar->isDirty(),
             "★ 变量在途时被同名替换,reply 成功必须把【新的那个】清脏。"
             "不清的话它会被反复重写 —— 而那个请求早就成功了。");
}

// ---------------------------------------------------------------------------
// 每帧一发
// ---------------------------------------------------------------------------

void tst_WriteSchedulerMultiPlc::oneWritePerPlcPerTick() {
    // 改之前的策略是"每帧只发一个(全局)"。多 PLC 之后改成"每台每帧一个",
    // 这样三台的总吞吐不变,单台的节奏也不变。
    for (int i = 0; i < 5; ++i) {
        vm_->addVariable(makeDirtyVar(QStringLiteral("V1_%1").arg(i), QStringLiteral("plc1"), 100 + i, i));
    }
    vm_->addVariable(makeDirtyVar(QStringLiteral("V2"), QStringLiteral("plc2"), 200, 9));

    tick(ws_);
    QCOMPARE(d1_->sent().size(), 1);
    QCOMPARE(d2_->sent().size(), 1);

    // 逐个回掉,每回掉一个 plc1 才有下一个的名额
    for (int i = 0; i < 5; ++i) {
        tick(ws_);
        d1_->completeWrite(d1_->sent().last().requestId);
    }
    QCOMPARE(d1_->sent().size(), 5);
    // ★ 五个变量一个不落,而且【地址和顺序都对】
    for (int i = 0; i < 5; ++i) {
        QCOMPARE(d1_->sent().at(i).addr, 100 + i);
    }
}

// ---------------------------------------------------------------------------
// 审计 / 报警
// ---------------------------------------------------------------------------

void tst_WriteSchedulerMultiPlc::auditEntry_recordsTheActualPlcId() {
    // ★ 单 PLC 时"这值写到哪去了"不是问题,所以历史上没有这个字段。
    //   现在必须记【发出时】的 plcId,不是回复时 v->plcId() ——
    //   变量在途时被改成别的 plcId 的话,后者会把账记到错的机器上。
    vm_->addVariable(makeDirtyVar(QStringLiteral("V2"), QStringLiteral("plc2"), 200, 7));
    tick(ws_);
    const quint32 reqId = d2_->sent().at(0).requestId;

    Variable* v = vm_->findVariable(QStringLiteral("V2"));
    v->setPlcId(QStringLiteral("plc3"));   // 在途时用户改了归属

    d2_->completeWrite(reqId, true);

    QCOMPARE(ws_->auditLog().size(), 1);
    QVERIFY2(ws_->auditLog().at(0).plcId == QStringLiteral("plc2"),
             "★ 审计必须记【发出时】的 plcId。变量在途时被改成 plc3 的话,"
             "记 plc3 就是把一笔写记到了没发过那台机器上 —— 事故分析时这个错误很贵。");
}

void tst_WriteSchedulerMultiPlc::writeFailed_oldSignatureStillEmitted() {
    // ★ 老签名必须继续发:WriteFailChecker 和 hmi.cpp 都按它接着,
    //   一行都不用改。这是"加能力"而不是"改契约"。
    QString gotName, gotReason;
    connect(ws_, &WriteScheduler::writeFailed, this,
            [&](const QString& n, const QString& r) { gotName = n; gotReason = r; });

    vm_->addVariable(makeDirtyVar(QStringLiteral("V2"), QStringLiteral("plc2"), 200, 7));
    tick(ws_);
    quint32 reqId = d2_->sent().at(0).requestId;

    for (int i = 0; i < 3; ++i) {   // kMaxWriteRetries = 3
        d2_->completeWrite(reqId, false);
        tick(ws_);
        if (i < 2) {
            reqId = d2_->sent().last().requestId;
        }
    }

    QVERIFY2(!gotName.isEmpty(), "重试到上限必须 emit writeFailed —— WriteFailChecker 靠它报 Critical 报警。");
    QCOMPARE(gotName, QStringLiteral("V2"));
    QVERIFY(!gotReason.isEmpty());
}

void tst_WriteSchedulerMultiPlc::writeFailed_newSignatureCarriesPlcId() {
    QString gotPlc, gotName;
    connect(ws_, &WriteScheduler::writeFailedOnPlc, this,
            [&](const QString& p, const QString& n, const QString&) { gotPlc = p; gotName = n; });

    vm_->addVariable(makeDirtyVar(QStringLiteral("V2"), QStringLiteral("plc2"), 200, 7));
    tick(ws_);
    quint32 reqId = d2_->sent().at(0).requestId;

    for (int i = 0; i < 3; ++i) {
        d2_->completeWrite(reqId, false);
        tick(ws_);
        reqId = d2_->sent().last().requestId;
    }

    QCOMPARE(gotPlc, QStringLiteral("plc2"));
    QCOMPARE(gotName, QStringLiteral("V2"));
}

void tst_WriteSchedulerMultiPlc::retryExhausted_stopsAndAlarms() {
    // 工业铁律:失败不能无限重发。重试到上限后清 dirty + 标 Bad。
    //   多 PLC 之后这条依然【逐台独立】—— plc2 的失败不该让 plc1 也停写。
    vm_->addVariable(makeDirtyVar(QStringLiteral("V2"), QStringLiteral("plc2"), 200, 7));
    vm_->addVariable(makeDirtyVar(QStringLiteral("V1"), QStringLiteral("plc1"), 100, 5));

    tick(ws_);
    QVERIFY2(!d2_->sent().isEmpty(), "plc2 第一帧就该发出一条 —— 没发出来就别往下走了。");
    quint32 req2 = d2_->sent().at(0).requestId;
    const quint32 req1 = d1_->sent().at(0).requestId;
    d1_->completeWrite(req1, true);

    for (int i = 0; i < 3; ++i) {
        d2_->completeWrite(req2, false);
        tick(ws_);
        QVERIFY2(!d2_->sent().isEmpty(),
                 "plc2 回掉一次之后下一帧必须还能重发。重试链断在这里 = "
                 "重试上限永远到不了 = 坏变量被无限重发打满总线。");
        req2 = d2_->sent().last().requestId;
    }

    Variable* v2 = vm_->findVariable(QStringLiteral("V2"));
    QVERIFY2(!v2->isDirty(), "重试到上限必须清 dirty,否则会无限重发打满总线。");
    QCOMPARE(v2->quality(), Variable::Quality::Bad);

    // plc1 不受影响
    // ★ 注意 quality 不能拿来判:V1 出生时 quality 就是 Bad(Variable 构造函数
    //   的既定默认 —— 没读过值的变量本来就不可信),而写成功【只】清 dirty、
    //   不把 quality 提成 Good(那是读通路的责任)。拿 quality 当"未被波及"
    //   的判据,是在断言一件从来没成立过的事。
    //   真正能证明"没波及"的是失败计数和 dirty —— 它们是写通路自己的账。
    Variable* v1 = vm_->findVariable(QStringLiteral("V1"));
    QVERIFY2(!v1->isDirty(), "★ plc2 写失败时 plc1 的变量不该被重发。");
    QCOMPARE(v1->writeFailCount(), 0);   // ★ plc2 的失败【绝不能】计到 plc1 头上
}

QTEST_MAIN(tst_WriteSchedulerMultiPlc)
#include "tst_WriteSchedulerMultiPlc.moc"
