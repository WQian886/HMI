// ============================================================================
// 2026-10-21 · BUGFIX-C1 回归测试 —— "从站失败之后,应用必须还能继续轮询"
//
//  【这条测试是为了什么】
//    2026-10-21 查"16.5 小时零轮询流量",根因是:
//      ModbusMaster 只在【成功】时发 readFinished;
//      协议级失败(异常响应 / 读响应帧太短 / 数据不足 / 超时)一条都不发;
//      而 PollScheduler 清 inFlight_ 的唯一入口就是 readFinished。
//      ⇒ 一次失败 → 那批变量永久卡在在途态 → 那台 PLC 再也收不到任何读请求。
//      而且这些错误全是 qCDebug,默认 INFO 级别下一行日志都不落,
//      从外面看:进程活着、日志在长、内存平稳 —— 一切正常。
//
//  【为什么当时没人发现】
//    改之前,这条路径【一次都没被测过】:失败分支从来没被断言过,
//    因为它压根不产生任何可观测的信号。
//    现在这个文件把那三个不可观测的洞补上,并且钉成断言。
//
//  【这三条用例分别钉住哪一段】
//    1. exceptionResponseReportsFailure
//       从站回 Modbus 异常响应(FC|0x80, ExCode=0x03) —— 这正是 mbslave
//       现在对每个 FC3 的真实回答。必须发 readFinished(success=false)。
//    2. timeoutReportsFailure
//       从站收下请求但不回包(3 秒超时)。必须发 readFinished(success=false)。
//    3. schedulerKeepsPollingAfterFailure
//       ★ 根因本体 ★ 调度器在连续失败之后【必须继续发请求】。
//       修之前:发出计数停在 1(变量卡在在途态),再也不会涨。
//       修之后:发出计数持续涨,失败计数跟着涨,成功计数保持 0。
//       —— 这条就是"16.5 小时零流量"的那张验伤单。
//
//  【从站是自己写的,不是 mbslave】
//    mbslave 是 GUI 程序,行为不可控、还会被别的会话改。
//    这里用 QTcpServer 自己实现一个最小从站,行为完全由测试说了算。
//    跑测试不需要装任何东西,也不碰现场的任何进程。
//
//  关联:ModbusMaster.cpp(finalizeErrorReply / onTimeoutScan),
//        PollScheduler.cpp(onDriverReadFinished), src/drivers/ModbusDriver.h
// ============================================================================

#include <QtTest>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QHostAddress>
#include <QModbusDataUnit>

#include "ModbusMaster.h"
#include "drivers/ModbusDriver.h"
#include "Variable.h"
#include "VariableManager.h"
#include "PollScheduler.h"
#include "Project.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDir>

using namespace qwhmi::drivers;

// ============================================================================
// 最小 Modbus TCP 从站 —— 行为可切换:正常回答 / 回异常 / 装死不回
// ============================================================================
class FakeSlave : public QTcpServer {
    Q_OBJECT
public:
    enum Mode {
        ReplyOk,        // 正常回答 FC3
        ReplyException, // 回 Modbus 异常响应(FC|0x80, ExCode=0x03)
        Silent          // 收下请求但不回包 → 逼出超时路径
    };

    explicit FakeSlave(QObject* parent = nullptr) : QTcpServer(parent) {
        connect(this, &QTcpServer::newConnection, this, &FakeSlave::onNewConnection);
    }

    Mode mode = ReplyException;
    int  requestCount = 0;   // 收到多少个读请求(用来证明"还在发")

protected:
    void onNewConnection() {
        while (QTcpSocket* s = nextPendingConnection()) {
            connect(s, &QTcpSocket::readyRead, this, [this, s] { onData(s); });
            connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
        }
    }

    void onData(QTcpSocket* s) {
        buf_ += s->readAll();
        while (buf_.size() >= 6) {
            const quint16 len = (quint16(quint8(buf_[4])) << 8) | quint16(quint8(buf_[5]));
            const int frameSize = 6 + len;
            if (buf_.size() < frameSize) break;
            const QByteArray req = buf_.left(frameSize);
            buf_.remove(0, frameSize);
            ++requestCount;
            if (mode == Silent) continue;      // 装死 → 交给超时路径

            const quint8 uid = quint8(req[6]);
            const quint8 fc  = quint8(req[7]);
            QByteArray resp;
            if (mode == ReplyException) {
                // MBAP: tid(2) proto(2)=0 len(2)=3 | uid(1) fc|0x80(1) exCode(1)
                resp  = req.left(2);
                resp += QByteArray::fromHex("00000003");
                resp += char(uid);
                resp += char(fc | 0x80);
                resp += char(0x03);              // Illegal Data Value
            } else {
                // FC3 正常响应:len = uid(1)+fc(1)+byteCount(1)+2N
                const quint16 addr  = (quint16(quint8(req[8])) << 8) | quint16(quint8(req[9]));
                const quint16 count = (quint16(quint8(req[10])) << 8) | quint16(quint8(req[11]));
                QByteArray data;
                for (int i = 0; i < count; ++i) {
                    const quint16 v = quint16(addr + i);
                    data += char(v & 0xFF);
                    data += char((v >> 8) & 0xFF);
                }
                const int pduLen = 3 + data.size();
                resp  = req.left(2);
                resp += QByteArray::fromHex("0000");
                resp += char((pduLen >> 8) & 0xFF);
                resp += char(pduLen & 0xFF);
                resp += char(uid);
                resp += char(fc);
                resp += char(data.size());
                resp += data;
            }
            s->write(resp);
        }
    }

private:
    QByteArray buf_;
};

class tst_ModbusFailureRecovery : public QObject {
    Q_OBJECT
private slots:
    void exceptionResponseReportsFailure();
    void timeoutReportsFailure();
    void schedulerKeepsPollingAfterFailure();
    void schedulerRecoversWhenSlaveComesBack();
    void soakProjectFileIsLoadable();
};

// 从当前目录往上找仓库根(测试的 ctest 工作目录是 build_ninja)
static QString findRepoFile(const QString& relPath) {
    QDir d = QDir::current();
    for (int i = 0; i < 6; ++i) {
        const QString cand = d.absoluteFilePath(relPath);
        if (QFile::exists(cand)) return cand;
        if (!d.cdUp()) break;
    }
    return QString();
}

// ---------------------------------------------------------------------------
// 1. 异常响应必须报失败
// ---------------------------------------------------------------------------
void tst_ModbusFailureRecovery::exceptionResponseReportsFailure() {
    FakeSlave slave;
    QVERIFY(slave.listen(QHostAddress::LocalHost, 0));
    slave.mode = FakeSlave::ReplyException;

    ModbusMaster master;
    QSignalSpy connectedSpy(&master, &ModbusMaster::connected);
    QSignalSpy finishedSpy(&master, &ModbusMaster::readFinished);

    master.connectTo(QStringLiteral("127.0.0.1"), slave.serverPort());
    QVERIFY2(connectedSpy.wait(3000), "连不上自己开的从站");

    QModbusDataUnit unit(QModbusDataUnit::HoldingRegisters, 0, 3);
    master.sendReadRequest(unit, 1, 42);

    QVERIFY2(finishedSpy.wait(3000),
             "从站回了异常响应,但 3 秒内没有 readFinished —— 这就是 BUGFIX-C1 修的那个洞");
    QCOMPARE(finishedSpy.count(), 1);
    QCOMPARE(finishedSpy.at(0).at(0).toUInt(), 42u);   // requestId 要原样带回
    QCOMPARE(finishedSpy.at(0).at(2).toBool(), false);  // success 必须是 false
}

// ---------------------------------------------------------------------------
// 2. 超时必须报失败(从站收下不回)
// ---------------------------------------------------------------------------
void tst_ModbusFailureRecovery::timeoutReportsFailure() {
    FakeSlave slave;
    QVERIFY(slave.listen(QHostAddress::LocalHost, 0));
    slave.mode = FakeSlave::Silent;   // 收下请求,一个字节都不回

    ModbusMaster master;
    QSignalSpy connectedSpy(&master, &ModbusMaster::connected);
    QSignalSpy finishedSpy(&master, &ModbusMaster::readFinished);

    master.connectTo(QStringLiteral("127.0.0.1"), slave.serverPort());
    QVERIFY2(connectedSpy.wait(3000), "连不上自己开的从站");

    QModbusDataUnit unit(QModbusDataUnit::HoldingRegisters, 0, 3);
    master.sendReadRequest(unit, 1, 7);

    // ModbusMaster 的请求超时是 3 秒(硬编码在 sendReadRequest 里),这里给 6 秒
    QVERIFY2(finishedSpy.wait(6000),
             "从站不回包,超时之后仍然没有 readFinished —— 超时路径也漏了失败通知");
    QCOMPARE(finishedSpy.count(), 1);
    QCOMPARE(finishedSpy.at(0).at(0).toUInt(), 7u);
    QCOMPARE(finishedSpy.at(0).at(2).toBool(), false);
}

// ---------------------------------------------------------------------------
// 3. 根因本体:连续失败之后,调度器必须【继续发请求】
//
//    修之前:第一次失败 → 变量卡在在途态 → pollSentCount_ 停在 1。
//            (现场表现:那台 PLC 从此再也没收到过任何读请求)
//    修之后:发出计数持续涨,失败计数跟着涨,成功计数保持 0。
// ---------------------------------------------------------------------------
void tst_ModbusFailureRecovery::schedulerKeepsPollingAfterFailure() {
    FakeSlave slave;
    QVERIFY(slave.listen(QHostAddress::LocalHost, 0));
    slave.mode = FakeSlave::ReplyException;

    VariableManager vm;
    auto* v = new Variable(QStringLiteral("Speed"), 0x0000,
                            Variable::DataType::UInt16, Variable::Access::ReadOnly, &vm);
    v->setPlcId(QStringLiteral("plc1"));
    v->setAlwaysPoll(true);
    v->setPollIntervalMs(100);        // 100ms 一帧,1.5 秒里能发十几帧
    vm.addVariable(v);

    ModbusDriver driver;
    QSignalSpy connectedSpy(&driver, &IPollDriver::connected);
    driver.connectTo(QStringLiteral("127.0.0.1"), slave.serverPort());
    QVERIFY2(connectedSpy.wait(3000), "driver 连不上自己开的从站");

    PollScheduler sched(&vm, &driver);
    sched.setAllActive();
    sched.start();

    QTest::qWait(1500);
    sched.stop();

    const quint64 sent = sched.pollSentCount();
    const quint64 fail = sched.pollFailCount();
    const quint64 ok   = sched.pollOkCount();

    QVERIFY2(sent >= 2,
             qPrintable(QStringLiteral("发出计数只涨到 %1 —— 变量卡在在途态了,"
                                        "这正是 2026-10-21 查到的那个 16.5 小时零流量的 bug"
                                        "(发出=%1 失败=%2 成功=%3)")
                            .arg(sent).arg(fail).arg(ok)));
    QVERIFY2(fail >= 1, "从站一直在回异常,失败计数却没涨 —— 失败通知没上报");
    QCOMPARE(ok, 0u);   // 从站没给出一次成功读,成功计数必须是 0
}

// ---------------------------------------------------------------------------
// 4. 从站恢复之后,调度器必须能重新读到数据
//    (只测"不卡死"不够 —— 还得证明"能回来")
// ---------------------------------------------------------------------------
void tst_ModbusFailureRecovery::schedulerRecoversWhenSlaveComesBack() {
    FakeSlave slave;
    QVERIFY(slave.listen(QHostAddress::LocalHost, 0));
    slave.mode = FakeSlave::ReplyException;

    VariableManager vm;
    auto* v = new Variable(QStringLiteral("Speed"), 0x0000,
                            Variable::DataType::UInt16, Variable::Access::ReadOnly, &vm);
    v->setPlcId(QStringLiteral("plc1"));
    v->setAlwaysPoll(true);
    v->setPollIntervalMs(100);
    vm.addVariable(v);

    ModbusDriver driver;
    QSignalSpy connectedSpy(&driver, &IPollDriver::connected);
    driver.connectTo(QStringLiteral("127.0.0.1"), slave.serverPort());
    QVERIFY2(connectedSpy.wait(3000), "driver 连不上自己开的从站");

    PollScheduler sched(&vm, &driver);
    sched.setAllActive();
    sched.start();

    QTest::qWait(600);
    const quint64 sentBefore = sched.pollSentCount();
    const quint64 failBefore = sched.pollFailCount();

    // ★ 从站恢复正常
    slave.mode = FakeSlave::ReplyOk;
    QTest::qWait(800);
    sched.stop();

    const quint64 okAfter = sched.pollOkCount();
    QVERIFY2(sentBefore >= 1, "前置条件不成立:一次请求都没发出去");
    QVERIFY2(failBefore >= 1, "前置条件不成立:一次失败都没发生");
    QVERIFY2(okAfter >= 1,
             qPrintable(QStringLiteral("从站恢复之后仍然一次都没读成功(发出=%1 失败=%2 成功=%3)"
                                        "—— 说明恢复路径没通")
                            .arg(sched.pollSentCount()).arg(failBefore).arg(okAfter)));
}

// ---------------------------------------------------------------------------
// 5. 长跑用的工程文件必须能被 Project 打开
//
//    【为什么这条也要测】--project 之前是个空壳(只打一行日志)。
//    现在它真的能打开工程了,于是"工程文件本身写错了"成了新的失败模式 ——
//    而这个失败模式【很安静】:程序打不开就退出(exit 2),长跑脚本只看到
//    "启动后 5 秒就退出了"。提前在这儿验一遍,比在长跑现场验便宜得多。
// ---------------------------------------------------------------------------
void tst_ModbusFailureRecovery::soakProjectFileIsLoadable() {
    const QString path = findRepoFile(QStringLiteral("scripts/soak_project.json"));
    QVERIFY2(!path.isEmpty(), "找不到 scripts/soak_project.json");
    QVERIFY2(QFile::exists(path), qPrintable(QStringLiteral("工程文件不存在: %1").arg(path)));

    // 先看内容:端口必须是 1502(自建从站),不能是 502(mbslave 那个空的)
    QFile f(path);
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    f.close();
    const QJsonObject comms = root.value(QStringLiteral("communications")).toObject();
    QCOMPARE(comms.value(QStringLiteral("host")).toString(), QStringLiteral("127.0.0.1"));
    QCOMPARE(comms.value(QStringLiteral("port")).toInt(), 1502);
    const QJsonArray vars = root.value(QStringLiteral("variables")).toArray();
    QCOMPARE(vars.size(), 3);

    // 再让真正的 Project 去加载它(schema v1 → v3 迁移就在这条路上)
    Project p(nullptr, nullptr, nullptr, nullptr, nullptr);
    QVERIFY2(p.loadFromFile(path),
             qPrintable(QStringLiteral("Project 加载不了 %1 —— 长跑一启动就会 exit 2").arg(path)));
}

QTEST_MAIN(tst_ModbusFailureRecovery)
#include "tst_ModbusFailureRecovery.moc"
