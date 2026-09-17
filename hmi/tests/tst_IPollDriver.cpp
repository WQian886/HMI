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
        // 模拟立刻回复(同步)
        emit readFinished(req.requestId, nextReplyRegisters, nextReplySuccess);
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
    QVERIFY(!DriverFactory::isSupported("s7"));      // C.2 还没实现
    QVERIFY(!DriverFactory::isSupported("eip"));     // C.3 还没实现
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

QTEST_MAIN(TestIPollDriver)
#include "tst_IPollDriver.moc"
