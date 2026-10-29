// ============================================================================
// tst_ModbusEchoAddress —— 写响应的 echo 地址,必须真的跟请求比对
//
// 【病】
//   ModbusMaster::handleWriteResponse 里原来这一行:
//
//       if (echoAddr != quint16(req->reply ? /*addr 通过 reply 拿*/0 : 0)) {
//           qCDebug(hmiComm) << "[ModbusMaster] 写响应 echo addr 与请求不一致";
//       }
//
//   三元的【两个分支都是 0】,占位从来没填上 —— 于是这一行实际判的是
//   "echoAddr != 0",**不是**注释说的"与请求地址比对"。
//   两个方向都错:
//
//     · 请求地址非 0、回包 echo 也非 0(= 一切正常)→ 判成"不一致",
//       于是【每一次正常的写】都打一条"不一致"(内容是假的)。
//       而这行是 qCDebug —— 默认 INFO 级别下一行都不落,现场看不见。
//     · 请求地址非 0、回包 echo 恰是 0(= 真的不一致)→ 判成"一致",
//       真的不一致反而【一声不吭】。
//
//   ★ 换成"别的写法"也一样错:把三元改成 `req->reply ? addr : 0` 也救不了 ——
//     QModbusReply 上【根本没有】请求地址这个信息,回包时是"拿"不到的。
//     唯一正确的做法是:发出去的时候把地址存进 Pending,回包时拿它比。
//
// 【Modbus 协议事实(本文件的全部依据)】
//   写请求 PDU(0x05/0x06/0x0F/0x10):起始地址(2) + 值/数量(2)
//   写响应 PDU(0x05/0x06/0x0F/0x10):★ 回显★ 起始地址(2) + 值/数量(2)
//   读响应 PDU(0x01~0x04):只有 byteCount(1) + 数据 —— 【没有】地址字段。
//   ⇒ 只有【写】响应有 echo 地址可对。这条别搞错:
//     给读响应也加一个"地址比对"是【不可能】的,那是把协议读错了。
//
// 【为什么必须自己开一个从站】
//   本仓库没有真 PLC。而这一条要验的恰恰是【回包内容】——
//   mock 出来的"回包"是测试自己写的,拿它跟测试自己比,
//   就是这个仓库反复吃的那个坑("自己拿自己当参照")。
//   所以下面这个从站是【按 Modbus 协议现写的】:
//   它回什么、错在哪,都由测试显式指定(echoOverride / fixedEcho),
//   跟被测代码没有任何共享逻辑。
//
// 【负向对照 —— 实测两轮,2026-10-21】
//   轮次一:把比对改回【老代码那个空占位】
//           (三元两分支都是 0,判的其实是 "echoAddr != 0";
//            顺带把日志级别和措辞也恢复成老样子 —— qCDebug + "echo addr")
//     → 红 2:mismatchedEchoAddress_isFlagged
//             mismatchedEchoAddress_toZero_isFlagged
//       绿 2:matchingEchoAddress_isNotFlagged
//             writeToZeroAddress_withZeroEcho_isNotFlagged
//     ★ 老代码唯一能抓到的"不一致",是回包地址【非 0】的那种 ——
//       而那恰恰是【正常的写】。它抓的全是不该抓的,
//       漏的("回包地址恰是 0")全是该抓的。
//
//   轮次二:把条件改成【恒真】(永远报),其余与修好之后完全一样
//     → 红 2:matchingEchoAddress_isNotFlagged
//             writeToZeroAddress_withZeroEcho_isNotFlagged
//       绿 2:mismatchedEchoAddress_isFlagged
//             mismatchedEchoAddress_toZero_isFlagged
//     ★ 这一轮证明另外两条也有牙:一个"永远报"的实现会被它们抓住。
//       它在现场的危害是【每一次正常的写都刷一条告警】,日志没法看。
//
//   ★ 两轮的红集【互不相交】—— 这才是关键:
//     轮次一钉"该报的必须报",轮次二钉"不该报的必须安静",
//     两条合起来才说明这是一个【真的比对】,
//     而不是"碰巧在某些地址上对"。
//
//   ★ 轮次一里那两条"绿"还带出一个观察:
//     老代码确实会把正常的写报成不一致 —— 但它报在【Debug 级】、
//     措辞也不同("echo addr" vs "echo 地址"),于是连测试都看不见。
//     这正说明"看得见"本身就是这次要修的一半:
//     一条没人看得见的告警,跟没有告警是一样的。
//
// 【为什么"匹配的那两条"也必须有】
//   只测"不匹配要报",一个"永远报"的实现也能全绿 ——
//   而那个实现会在每一次正常的写上刷警告,现场根本没法用。
//   ⇒ 匹配时【必须安静】,这是这条修复的一半。
//
// 关联:../ModbusMaster.cpp handleWriteResponse / sendWriteRequest,
//      ../ModbusMaster.h Pending::startAddress
// ============================================================================

#include <QtTest>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QHostAddress>
#include <QModbusDataUnit>
#include <cstdio>

#include "ModbusMaster.h"

// ============================================================================
// 日志捕获 —— 要断言的正是"默认级别下看不看得见"
// ============================================================================
namespace {

struct LogLine {
    QtMsgType type;
    QString   text;
};

QList<LogLine> g_log;
QtMessageHandler g_prevHandler = nullptr;   // 装之前的 handler(可能是"默认的" = nullptr)

void captureHandler(QtMsgType type, const QMessageLogContext& ctx, const QString& msg) {
    // 只收 Warning 及以上 —— Debug 级的那些在默认日志级别下【根本不落盘】,
    // 而"现场看不见"正是这个 bug 的一半。
    if (type >= QtWarningMsg) g_log.append(LogLine{type, msg});
    // 仍然打到控制台,便于事后看。
    // ★ Qt 6 里已经没有 qDefaultMessageHandler 了(本机 Qt 6.8.3 的
    //   qlogging.h 里查不到这个符号,编译报 C3861)——
    //   所以只能自己转发:装之前的 handler 还在就转发给它;
    //   不在(说明当时用的是默认的)就自己写 stderr。
    //   【不能】用 qDebug 转发 —— 那会再次走进这个 handler,自己调自己。
    if (g_prevHandler) {
        g_prevHandler(type, ctx, msg);
    } else {
        const QByteArray line = msg.toLocal8Bit();
        fprintf(stderr, "%s\n", line.constData());
    }
}

// 有没有一条日志提到了 echo 不一致
bool sawEchoMismatch(const QList<LogLine>& lines, QString* which = nullptr) {
    for (const LogLine& l : lines) {
        if (l.text.contains(QStringLiteral("echo 地址与请求不一致"))) {
            if (which) *which = l.text;
            return true;
        }
    }
    return false;
}

} // namespace

// ============================================================================
// 最小写从站 —— 回包里的 echo 地址由测试显式指定
// ============================================================================
class FakeWriteSlave : public QTcpServer {
    Q_OBJECT
public:
    explicit FakeWriteSlave(QObject* parent = nullptr) : QTcpServer(parent) {
        connect(this, &QTcpServer::newConnection, this, &FakeWriteSlave::onNewConnection);
    }

    // ★ 回包里的 echo 地址怎么来:
    //   echoOverride == false → 原样回显请求里的地址(= 正常从站)
    //   echoOverride == true  → 回 fixedEcho(= 模拟回错地址的从站)
    // 显式开关而不是"自动" —— 负向对照要的就是"明确回一个错的地址"。
    bool    echoOverride = false;
    quint16 fixedEcho    = 0;

    // ---- 观测口 ----
    int     requestCount    = 0;
    quint16 lastRequestAddr = 0;

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

            const quint8 uid = quint8(req[6]);
            const quint8 fc  = quint8(req[7]);
            // 只管写。读响应里【没有】地址字段(见文件头),不在这儿造。
            if (fc != 0x05 && fc != 0x06 && fc != 0x0F && fc != 0x10) continue;

            const quint16 addr = (quint16(quint8(req[8])) << 8) | quint16(quint8(req[9]));
            lastRequestAddr = addr;
            const quint16 echo = echoOverride ? fixedEcho : addr;

            // 回包:MBAP(7) + fc(1) + addr(2) + value(2) = 12 字节
            //   Length = uid(1) + fc(1) + addr(2) + value(2) = 6
            //   ★ Length 从【Unit ID】开始算,不含 Length 自己那 2 字节
            //     (写成 resp.size()-6 会少算 uid,ModbusMaster 会判"数据不足"
            //      然后整帧丢掉 —— 现象是 3 秒后超时。tst_PlcMultiServerE2E
            //      的文件头记过这个坑。)
            QByteArray resp;
            resp += req.left(2);                    // TID 回原
            resp += QByteArray::fromHex("0000");    // Protocol = 0
            resp += QByteArray::fromHex("0006");    // Length = 6
            resp += char(uid);
            resp += char(fc);
            resp += char((echo >> 8) & 0xFF);        // ★ 回显的地址
            resp += char(echo & 0xFF);
            resp += req.mid(10, 2);                // value 原样回显
            s->write(resp);
        }
    }

private:
    QByteArray buf_;
};

// ============================================================================
class tst_ModbusEchoAddress : public QObject {
    Q_OBJECT

private slots:
    void init();

    // ---- 正常回显:必须【安静】 ----
    void matchingEchoAddress_isNotFlagged();
    void writeToZeroAddress_withZeroEcho_isNotFlagged();

    // ---- 回显错地址:必须【报出来】 ----
    void mismatchedEchoAddress_isFlagged();
    void mismatchedEchoAddress_toZero_isFlagged();

private:
    // 连上从站并发一次写,等 writeFinished 回来。
    // ★ 故意返回 void:这个 helper 里【不能】用 QVERIFY ——
    //   它展开是 `if (!ok) return;`,放在有返回值的函数里【编译不过】。
    //   (tst_PlcMultiServerE2E 的文件头记过这个坑。)
    //   结果全放成员里,由调用方去断言。
    void doWrite(FakeWriteSlave* slave, quint16 addr);

    QList<LogLine> m_logs;        // 这次写期间收到的日志
    bool m_writeCameBack = false; // writeFinished 有没有来
    bool m_writeOk       = false; // 写被判成成功没有
};

void tst_ModbusEchoAddress::init() {
    g_log.clear();
}

// ---------------------------------------------------------------------------
// 一次完整的写:连上 → 发 → 等回包 → 把期间的日志收回来
// ---------------------------------------------------------------------------
void tst_ModbusEchoAddress::doWrite(FakeWriteSlave* slave, quint16 addr) {
    m_logs.clear();
    m_writeCameBack = false;
    m_writeOk = false;

    ModbusMaster master;
    QSignalSpy connectedSpy(&master, &ModbusMaster::connected);
    QSignalSpy finishedSpy(&master, &ModbusMaster::writeFinished);

    master.connectTo(QStringLiteral("127.0.0.1"), slave->serverPort());
    if (!connectedSpy.wait(3000)) {
        qWarning("★ 连不上自己开的从站");
        return;
    }

    g_log.clear();
    g_prevHandler = qInstallMessageHandler(captureHandler);

    QModbusDataUnit unit(QModbusDataUnit::HoldingRegisters, int(addr),
                          QList<quint16>{0x1234});
    QModbusReply* reply = master.sendWriteRequest(unit, 1, 7);

    // ★ 必须等到 writeFinished 才收日志:handleWriteResponse 里
    //   【先打日志再发信号】,所以信号到了,日志就一定已经落进 g_log。
    m_writeCameBack = finishedSpy.wait(3000);
    if (m_writeCameBack) m_writeOk = finishedSpy.at(0).at(1).toBool();

    qInstallMessageHandler(g_prevHandler);
    g_prevHandler = nullptr;
    m_logs = g_log;

    if (!reply) {
        qWarning("★ sendWriteRequest 返回空 —— 请求根本没发出去");
    }
}

// ---------------------------------------------------------------------------
// 1. 正常回显:不许报不一致
//
//    ★ 这是老代码【每次都错】的那一种:请求地址非 0、回包 echo 也非 0,
//      老代码判成"不一致",于是每一次正常的写都打一条假警报。
//      老代码在这条上是"绿的" —— 但那是巧合(它判的是 echoAddr != 0),
//      轮次一的负向对照会把这层巧合揭穿。
// ---------------------------------------------------------------------------
void tst_ModbusEchoAddress::matchingEchoAddress_isNotFlagged() {
    FakeWriteSlave slave;
    QVERIFY(slave.listen(QHostAddress::LocalHost, 0));

    doWrite(&slave, 0x0100);

    QVERIFY2(m_writeCameBack, "写没有回包 —— 从站或协议栈不对");
    QVERIFY2(m_writeOk, "写被判成失败(这条修复不改变成功/失败的判定)");
    QCOMPARE(slave.requestCount, 1);
    QCOMPARE(slave.lastRequestAddr, quint16(0x0100));

    QString which;
    QVERIFY2(!sawEchoMismatch(m_logs, &which),
             qPrintable(QStringLiteral("★ 一次完全正常的写被报成 echo 地址不一致:%1。"
                                       "老代码就是这样的 —— 它判的是 echoAddr != 0,"
                                       "而正常写的 echo 恰恰非 0。").arg(which)));
}

// ---------------------------------------------------------------------------
// 2. 写到 0 号地址、从站也回 0 —— 不许报
//
//    ★ 这条是"它真的在【比对】"的另一半证据。
//      如果实现是"回显非 0 就报"或者"回显为 0 就不报",
//      这条都会红。它跟第 1 条一起,把"比对"这个性质钉死。
// ---------------------------------------------------------------------------
void tst_ModbusEchoAddress::writeToZeroAddress_withZeroEcho_isNotFlagged() {
    FakeWriteSlave slave;
    QVERIFY(slave.listen(QHostAddress::LocalHost, 0));

    doWrite(&slave, 0x0000);

    QVERIFY2(m_writeCameBack, "写没有回包 —— 从站或协议栈不对");
    QVERIFY2(m_writeOk, "写被判成失败");
    QCOMPARE(slave.requestCount, 1);
    QCOMPARE(slave.lastRequestAddr, quint16(0x0000));

    QString which;
    QVERIFY2(!sawEchoMismatch(m_logs, &which),
             qPrintable(QStringLiteral("★ 写到 0 号地址、从站也回 0(完全一致),"
                                       "却被报成不一致:%1").arg(which)));
}

// ---------------------------------------------------------------------------
// 3. 回显了别的地址:必须报出来,而且要说清是哪个对哪个
// ---------------------------------------------------------------------------
void tst_ModbusEchoAddress::mismatchedEchoAddress_isFlagged() {
    FakeWriteSlave slave;
    QVERIFY(slave.listen(QHostAddress::LocalHost, 0));
    slave.echoOverride = true;
    slave.fixedEcho    = 0x0200;      // 请求的是 0x0100,从站却回 0x0200

    doWrite(&slave, 0x0100);

    QVERIFY2(m_writeCameBack, "写没有回包 —— 从站或协议栈不对");
    QCOMPARE(slave.requestCount, 1);
    QCOMPARE(slave.lastRequestAddr, quint16(0x0100));

    QString which;
    QVERIFY2(sawEchoMismatch(m_logs, &which),
             "★ 从站回显的地址跟请求的对不上,却一声不吭 —— "
             "现场表现是'写到了别的点'而日志里什么都没有。");
    // ★ 消息里必须【带出两个地址】—— 只说"不一致"的话,
    //   看日志的人还得自己翻请求那一行才知道差在哪。
    QVERIFY2(which.contains(QStringLiteral("0x0200")),
             qPrintable(QStringLiteral("告警里没有回包地址:%1").arg(which)));
    QVERIFY2(which.contains(QStringLiteral("0x0100")),
             qPrintable(QStringLiteral("告警里没有请求地址:%1").arg(which)));
    // ★ 而且必须是【默认级别就看得见】的那一级 ——
    //   老代码这里是 qCDebug,默认 INFO 级别下一行都不落。
    bool isWarning = false;
    for (const LogLine& l : m_logs) {
        if (l.text.contains(QStringLiteral("echo 地址与请求不一致"))
            && l.type == QtWarningMsg) { isWarning = true; }
    }
    QVERIFY2(isWarning,
             qPrintable(QStringLiteral("★ 这条告警不是 WARNING 级:%1。"
                                       "默认日志级别下它就等于没有 —— "
                                       "而这正是老代码的毛病。").arg(which)));
}

// ---------------------------------------------------------------------------
// 4. 回显地址恰是 0:也必须报
//
//    ★★ 这是老代码【完全看不见】的那一种:
//      请求地址是 0x0100,回包 echo 却是 0x0000 ——
//      老代码判的是 "echoAddr != 0",于是这一格【一声不吭】。
//      换句话说:老代码唯一能抓到的"不一致",是回包地址非 0 的那种,
//      而那种恰恰是【正常的写】。它抓的全是不该抓的,漏的全是该抓的。
// ---------------------------------------------------------------------------
void tst_ModbusEchoAddress::mismatchedEchoAddress_toZero_isFlagged() {
    FakeWriteSlave slave;
    QVERIFY(slave.listen(QHostAddress::LocalHost, 0));
    slave.echoOverride = true;
    slave.fixedEcho    = 0x0000;      // 请求的是 0x0100,从站却回 0x0000

    doWrite(&slave, 0x0100);

    QVERIFY2(m_writeCameBack, "写没有回包 —— 从站或协议栈不对");
    QCOMPARE(slave.requestCount, 1);

    QString which;
    QVERIFY2(sawEchoMismatch(m_logs, &which),
             "★ 回包地址是 0x0000、请求地址是 0x0100,却没报 —— "
             "老代码判的是 echoAddr != 0,这一格它【永远看不见】。");
    QVERIFY2(which.contains(QStringLiteral("0x0000")),
             qPrintable(QStringLiteral("告警里没有回包地址:%1").arg(which)));
}

QTEST_MAIN(tst_ModbusEchoAddress)
#include "tst_ModbusEchoAddress.moc"
