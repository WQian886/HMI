// ============================================================================
// tst_ModbusWriteQueue —— "读和写抢同一个在途槽"导致写【永远出不去】
//
// 【这个文件在防什么:一条可能整轮 24 小时都不生效的"停机"联动】
//
//   2026-10-26 短跑实测(soaks/sweep_m85)挖出来的:
//     · 604 次写尝试【全部】被 driver 拒收,0 帧到达从站
//     · 同一时刻 547 次读【全部】成功
//     · 写被拒的时刻 → 读回包到达的时刻:平均 2.18 ms(94.2% 在 5 ms 以内)
//     ⇒ 写尝试【落在读的在途窗口里】。
//
//   ★ 而同样配置的另外六轮(sweep_m70/m99/m88/m92/m95/m97)写【一次都没被拒】。
//     决定成败的是两个 100ms 节拍的【启动相位差】,而它一辈子不变(互相锁相)。
//     ⇒ 这不是"概率问题",是【开机掷一次硬币】:
//       掷到坏的那一面,24 小时一个字节都写不出去,而且【日志里看不出异常】。
//
//   ★★ 为什么这个洞特别危险(跟 G-15 / G-16 同一类:假绿)★★
//     读照常成功 ⇒ 长跑的活体判据(轮询统计:发出/成功 在涨)【照样绿】。
//     进程活着、内存平稳、报警照响、审计照记"已执行"——
//     而 PLC 上一个字节都没收到。
//     ⇒ 唯一能抓住它的判据是【从站那侧收到的字节】,不是应用的日志,也不是读的计数。
//
// 【修法】
//   ModbusMaster 的在途槽只有一个(Modbus 协议串行),读和写共用。
//   改之前:槽被读占着 → 写【被拒收】(return nullptr)→ dirty 保留 → 下一帧再被拒。
//   改之后:槽被读占着 → 写【排队】→ 槽一空就发出去。
//   ⇒ 写从"跟读抢"变成"等读让出来",相位不再是决定因素。
//
// 【本文件钉住哪几件事】
//   ① 槽被读占着时,写【不许】被拒收 —— 必须返回"已接受"。
//      (这是修之前那个洞的本体:拒收 ⇒ 写永远出不去。)
//   ② 槽空出来之后,排队里的写【真的发出去了】(从站收到字节)。
//   ③ 排队期间本地值变了 —— 发出去的还是【入队那一刻】的值。
//      (跟 BUGFIX-D1 同一类错:那时候错在"reply 时刻",这次错在"发送时刻"。)
//   ④ 排队里的写在连接断开时【全部作废,且必须发 writeFinished(false)】。
//      不发通知 ⇒ WriteScheduler 永远等一个不来的 reply ⇒ pending_ 泄漏
//      ⇒ 那台 PLC 从此再也不写任何东西(跟 2026-10-21 的"卡在在途态"同形)。
//   ⑤ 【校验必须在排队【前面】】—— 不合法的写请求【当场拒收】,不许进队列。
//      (这一条是本修复第一版自己写错的地方:校验排在排队后面,
//       于是"values 为空"的写会被排队,flush 时按多寄存器写组装出一个
//       byteCount=0 的畸形帧发给 PLC。)
//
// 【从站是自己写的,不是 mbslave】
//   mbslave 的数据表现在是空的(FC3 一律回 ExCode=0x03),行为不可控。
//   这里用 QTcpServer 自己实现一个最小从站,行为完全由测试说了算,
//   并且它会【把收到的 FC6 写帧记下来】—— 那是唯一不会撒谎的证据。
//
//  关联:ModbusMaster.cpp(sendWriteRequest / flushQueuedWrites / failQueuedWrites),
//        src/drivers/ModbusDriver.h
// ============================================================================

#include <QtTest>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QHostAddress>
#include <QModbusDataUnit>
#include <QList>

#include "ModbusMaster.h"
#include "drivers/ModbusDriver.h"

// ============================================================================
// 最小 Modbus TCP 从站 —— 记下它收到的每一个 FC6 写帧
// ============================================================================
class RecordingSlave : public QTcpServer {
    Q_OBJECT
public:
    explicit RecordingSlave(QObject* parent = nullptr) : QTcpServer(parent) {
        connect(this, &QTcpServer::newConnection, this, &RecordingSlave::onNewConnection);
    }

    // 从站收到的写帧:每个元素是 {addr, value}
    struct WriteRecord {
        int addr = 0;
        int value = 0;
    };
    QList<WriteRecord> writes;
    int readCount = 0;

    // ★ 从站收到的【每一个】请求帧的功能码(不只是 FC6)。
    //   为什么单列一个:发出去的帧【可能】是个畸形的 FC(比如 byteCount=0 的 0x10),
    //   那种帧不是"写"、但它【确实到了从站】——
    //   只数 FC6 的话,畸形帧会被当成"没发出去",判据就漏了。
    QList<quint8> receivedFc;

    // ★ 让从站【慢一点】回答 —— 这是本测试的关键。
    //   Modbus 主站发完请求就发下一个,只有从站回包了槽才空。
    //   如果从站【立刻】回包,那槽几乎总是空的,写永远撞不上读 ——
    //   那样这个测试就【测不到那个洞】,会一直绿,跟 G-15 一个下场。
    //   replyDelayMs 让槽被占住一段时间,稳定地制造出"槽被占"的场景。
    int replyDelayMs = 0;

    // ★ 2026-10-27(BUGFIX-E3):让从站回【异常响应】(FC | 0x80)。
    //   ExCode:0x02 = Illegal Data Address(地址越界)—— 现场最常见的一种。
    //   0 = 不注入(正常回包)。
    int exceptionExCode = 0;

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

            const quint8 uid = quint8(req[6]);
            const quint8 fc  = quint8(req[7]);
            receivedFc.append(fc);        // ★ 每个帧都记,不管是什么功能码

            if (fc == 0x06) {
                // ★ 写单寄存器 —— 这就是要被钉住的那条路
                const quint16 addr  = (quint16(quint8(req[8])) << 8) | quint16(quint8(req[9]));
                const quint16 value = (quint16(quint8(req[10])) << 8) | quint16(quint8(req[11]));
                writes.append({int(addr), int(value)});
            } else if (fc == 0x03) {
                ++readCount;
            }

            // 回包:FC6 回显 addr+value;FC3 回 3 个寄存器
            QByteArray resp;
            if (exceptionExCode != 0) {
                // ★ 2026-10-27(BUGFIX-E3):异常响应 = 原功能码 | 0x80 + 一个 ExCode。
                //   ★ 这是【从站明确地回答了"不行"】—— 跟"没回答"是两件事。
                //     调用方【必须】收到通知,否则它永远等一个不来的 reply。
                //
                // ★★ MBAP 的 LEN 字段 = 后面还有多少字节(UID + FC + ExCode = 3)★★
                //   LEN 是【2 字节】,大端。所以是 0x00 0x03,不是 0x03 0x03。
                //   ★ 这里第一版写错了(0x03 0x03 = 771)——
                //     主站按 frameSize = 6 + len 算,于是等 777 字节,
                //     而从站只发了 9 字节 ⇒ 主站【永远等不到这一帧】
                //     ⇒ writeFinished 是【超时路径】发的(3000 ms 之后),
                //       不是异常响应路径发的。
                //     ⇒ 那条测试是【假绿】:它绿,但绿的原因跟它要测的东西无关。
                //     ⇒ 判据的形态跟 2026-10-21 那次一模一样:
                //       "判据坏掉的样子,和程序坏掉的样子,一模一样。"
                //   所以现在:LEN 写成 0x00 0x03,并且下面的用例断言
                //   【不是超时】发出来的。
                resp  = req.left(2);           // TID(2)
                resp += QByteArray::fromHex("0000");   // PROTO(2)
                resp += char(0x00);            // LEN 高字节
                resp += char(0x03);            // LEN 低字节 = 3 (UID + FC + ExCode)
                resp += char(uid);
                resp += char(fc | 0x80);
                resp += char(quint8(exceptionExCode));
            } else if (fc == 0x06) {
                resp  = req.left(2);
                resp += QByteArray::fromHex("00000006");
                resp += char(uid);
                resp += char(fc);
                resp += req.mid(8, 4);           // addr(2) + value(2)
            } else {
                QByteArray data;
                for (int i = 0; i < 3; ++i) {
                    data += char(0x00);
                    data += char(0x11);
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

            if (replyDelayMs > 0) {
                // ★ 延迟回包 —— 制造"槽被占住"的那段时间
                QTimer::singleShot(replyDelayMs, s, [s, resp]() { s->write(resp); });
            } else {
                s->write(resp);
            }
        }
    }

private:
    QByteArray buf_;
};

// ============================================================================
class tst_ModbusWriteQueue : public QObject {
    Q_OBJECT
private slots:
    void writeIsNotRejectedWhenSlotIsBusy();      // ① 本体
    void queuedWriteReachesTheSlave();            // ②
    void queuedWriteKeepsTheValueItWasQueuedWith(); // ③
    void disconnectVoidsQueuedWrites();            // ④
    void invalidWriteIsRejectedNotQueued();        // ⑤
    void reconnectVoidsQueuedWrites();             // ⑥
    void inFlightWriteGetsFailureOnDisconnect();   // ⑦
    void inFlightReadGetsFailureOnDisconnect();    // ⑧
    void inFlightWriteGetsFailureOnReconnect();    // ⑨
    void exceptionResponseGetsFailure();          // ⑩ BUGFIX-E3
};

// ---------------------------------------------------------------------------
// ① 本体:槽被读占着的时候,写【不许】被拒收
//
//   修之前:sendWriteRequest 在 pending_ 非空时 return nullptr
//           ⇒ WriteScheduler 认为"没发出去" ⇒ 下一帧再试 ⇒ 再被拒 ⇒
//             604 次全被拒,0 帧到达从站。
//   修之后:写进队列,返回非空(已受理)⇒ 必定会有一个 writeFinished 回来。
// ---------------------------------------------------------------------------
void tst_ModbusWriteQueue::writeIsNotRejectedWhenSlotIsBusy() {
    RecordingSlave slave;
    QVERIFY(slave.listen(QHostAddress::LocalHost, 0));
    slave.replyDelayMs = 300;   // 回包慢 ⇒ 槽被占住 ⇒ 稳定撞上那个场景

    ModbusMaster master;
    QSignalSpy connectedSpy(&master, &ModbusMaster::connected);
    master.connectTo(QStringLiteral("127.0.0.1"), slave.serverPort());
    QVERIFY2(connectedSpy.wait(3000), "连不上自己开的从站");

    // 先发一个读,把槽占住
    QModbusDataUnit readUnit(QModbusDataUnit::HoldingRegisters, 0, 3);
    QVERIFY(master.sendReadRequest(readUnit, 1, 1) != nullptr);

    // ★ 立刻发写 —— 此刻读还在路上,槽是满的
    QModbusDataUnit writeUnit(QModbusDataUnit::HoldingRegisters, 2, QList<quint16>{0});
    QModbusReply* reply = master.sendWriteRequest(writeUnit, 1, 100);

    QVERIFY2(reply != nullptr,
             "★ 槽被读占着的时候写被【拒收】了 —— 这就是 2026-10-26 那个洞。"
             "拒收 ⇒ WriteScheduler 每帧重试 ⇒ 可能整轮(24 小时)一个字节都写不出去,"
             "而读照常成功、日志照常长、活体判据照常绿。");

    // ★ 而且它必须【最终】有个 writeFinished 回来 —— 不然调用方永远等。
    QSignalSpy writeSpy(&master, &ModbusMaster::writeFinished);
    QVERIFY2(writeSpy.wait(5000),
             "排队中的写 5 秒内没有 writeFinished —— 调用方会永远等一个不来的 reply,"
             "WriteScheduler 的 pending_ 就永远清不掉(那台 PLC 从此再也不写任何东西)");
}

// ---------------------------------------------------------------------------
// ② 槽空出来之后,排队里的写【真的发出去了】
//
//   判据用【从站收到的字节】—— 那是唯一不会撒谎的证据。
//   (应用自己的日志可以骗人:CX1 那轮里应用日志写的是 0,从站收到的是 1。)
// ---------------------------------------------------------------------------
void tst_ModbusWriteQueue::queuedWriteReachesTheSlave() {
    RecordingSlave slave;
    QVERIFY(slave.listen(QHostAddress::LocalHost, 0));
    slave.replyDelayMs = 300;

    ModbusMaster master;
    QSignalSpy connectedSpy(&master, &ModbusMaster::connected);
    master.connectTo(QStringLiteral("127.0.0.1"), slave.serverPort());
    QVERIFY2(connectedSpy.wait(3000), "连不上自己开的从站");

    // 先占住槽
    QModbusDataUnit readUnit(QModbusDataUnit::HoldingRegisters, 0, 3);
    QVERIFY(master.sendReadRequest(readUnit, 1, 1) != nullptr);

    // 排队一个写
    QModbusDataUnit writeUnit(QModbusDataUnit::HoldingRegisters, 2, QList<quint16>{0});
    QVERIFY(master.sendWriteRequest(writeUnit, 1, 100) != nullptr);

    // ★ 等它真的到从站
    QVERIFY2(QTest::qWaitFor([&]{ return !slave.writes.isEmpty(); }, 5000),
             "★ 排队中的写【没有到达从站】 —— 槽空出来之后它没被发出去。"
             "这正是 2026-10-26 那个洞的现场形态:应用日志一切正常,从站一个字节没收到。");

    QCOMPARE(slave.writes.size(), 1);
    QCOMPARE(slave.writes.first().addr, 2);
    QCOMPARE(slave.writes.first().value, 0);
}

// ---------------------------------------------------------------------------
// ③ 排队期间本地值变了 —— 发出去的还是【入队那一刻】的值
//
//   跟 BUGFIX-D1 是同一类错:那次错在"reply 时刻取 v->value()",
//   这次错在"发送时刻取 v->value()"。排队可能跨过好几拍,
//   中间本地值完全可能变(比如操作员又改了一次)。
//   ⇒ 发出去的必须是【入队那一刻】定下来的那个值。
// ---------------------------------------------------------------------------
void tst_ModbusWriteQueue::queuedWriteKeepsTheValueItWasQueuedWith() {
    RecordingSlave slave;
    QVERIFY(slave.listen(QHostAddress::LocalHost, 0));
    slave.replyDelayMs = 400;

    ModbusMaster master;
    QSignalSpy connectedSpy(&master, &ModbusMaster::connected);
    master.connectTo(QStringLiteral("127.0.0.1"), slave.serverPort());
    QVERIFY2(connectedSpy.wait(3000), "连不上自己开的从站");

    // 占住槽
    QModbusDataUnit readUnit(QModbusDataUnit::HoldingRegisters, 0, 3);
    QVERIFY(master.sendReadRequest(readUnit, 1, 1) != nullptr);

    // 排队一个写:值 = 7
    QModbusDataUnit writeUnit(QModbusDataUnit::HoldingRegisters, 2, QList<quint16>{7});
    QVERIFY(master.sendWriteRequest(writeUnit, 1, 100) != nullptr);

    // ★ 等它出去
    QVERIFY2(QTest::qWaitFor([&]{ return !slave.writes.isEmpty(); }, 5000),
             "排队中的写没到达从站");

    // 从站收到的必须是 7(入队时的值),不是别的
    QCOMPARE(slave.writes.first().value, 7);
}

// ---------------------------------------------------------------------------
// ④ 断线时,排队里的写【全部作废,且必须发 writeFinished(false)】
//
//   不发通知 ⇒ WriteScheduler 永远等一个不来的 reply ⇒ pending_ 泄漏
//   ⇒ 那台 PLC 从此再也不写任何东西 —— 跟 2026-10-21 查到的
//     "变量卡在在途态"是同一个形状的故障(而且一样安静)。
// ---------------------------------------------------------------------------
void tst_ModbusWriteQueue::disconnectVoidsQueuedWrites() {
    RecordingSlave slave;
    QVERIFY(slave.listen(QHostAddress::LocalHost, 0));
    slave.replyDelayMs = 5000;   // 很久才回包 —— 槽一直被占着

    ModbusMaster master;
    QSignalSpy connectedSpy(&master, &ModbusMaster::connected);
    master.connectTo(QStringLiteral("127.0.0.1"), slave.serverPort());
    QVERIFY2(connectedSpy.wait(3000), "连不上自己开的从站");

    // 占住槽
    QModbusDataUnit readUnit(QModbusDataUnit::HoldingRegisters, 0, 3);
    QVERIFY(master.sendReadRequest(readUnit, 1, 1) != nullptr);

    // 排队一个写
    QSignalSpy writeSpy(&master, &ModbusMaster::writeFinished);
    QModbusDataUnit writeUnit(QModbusDataUnit::HoldingRegisters, 2, QList<quint16>{0});
    QVERIFY(master.sendWriteRequest(writeUnit, 1, 100) != nullptr);

    // ★ 断线
    master.disconnect();

    QVERIFY2(writeSpy.wait(3000),
             "★ 断线后排队的写没有收到 writeFinished —— 调用方会永远等一个不来的 reply,"
             "WriteScheduler 的 pending_ 永远清不掉(那台 PLC 从此再也不写任何东西)");
    QCOMPARE(writeSpy.count(), 1);
    QCOMPARE(writeSpy.at(0).at(0).toUInt(), 100u);   // requestId 要原样带回
    QCOMPARE(writeSpy.at(0).at(1).toBool(), false);  // 失败
}

// ---------------------------------------------------------------------------
// ⑤ 【校验必须在排队【前面】】—— 不合法的写请求【当场拒收】,不许进队列
//
//   【这一条是本修复第一版自己写错的地方】
//     第一版把"槽被占就排队"的分支放在了参数校验【前面】,
//     于是【不合法的写请求也会被排队】:
//       · values 为空 → 排进队 → flush 时按"多寄存器写"(0x10)组装,
//         byteCount=0 ⇒ 发出一个【畸形的帧】给 PLC;
//       · serverAddress 越界 → 排进队 → flush 时不校验 ⇒ 帧的 unit id 是乱的;
//       · 不支持的 RegisterType → 排进队 → flush 时才发现,白跑一趟。
//     ⇒ 校验在前、排队在后:不合法的请求【当场拒收】(跟改之前一样),
//       只有【合法的】写才会进队列。
//
//   【怎么测才有意义】
//     判据不能只看"返回了 nullptr"(那跟"槽被占"那条路也返回 nullptr,分不开)。
//     要看的是:【从站那边有没有收到畸形帧】——
//     如果不合法写被排了队,它迟早会被 flush 出去,变成从站收到的一个怪帧。
//     所以:先占住槽,再发一个 values 为空的写,
//     然后【等一会儿】,断言从站一个字节都没收到。
// ---------------------------------------------------------------------------
void tst_ModbusWriteQueue::invalidWriteIsRejectedNotQueued() {
    RecordingSlave slave;
    QVERIFY(slave.listen(QHostAddress::LocalHost, 0));
    slave.replyDelayMs = 200;

    ModbusMaster master;
    QSignalSpy connectedSpy(&master, &ModbusMaster::connected);
    master.connectTo(QStringLiteral("127.0.0.1"), slave.serverPort());
    QVERIFY2(connectedSpy.wait(3000), "连不上自己开的从站");

    // 先占住槽 —— 只有槽被占着的时候,校验顺序才有意义
    QModbusDataUnit readUnit(QModbusDataUnit::HoldingRegisters, 0, 3);
    QVERIFY(master.sendReadRequest(readUnit, 1, 1) != nullptr);

    // values 为空的写 —— 按改之前的语义,这必须【当场拒收】
    QModbusDataUnit emptyUnit(QModbusDataUnit::HoldingRegisters, 2, QList<quint16>());
    QModbusReply* reply = master.sendWriteRequest(emptyUnit, 1, 200);
    QVERIFY2(reply == nullptr,
             "★ values 为空的写被【接受】了 —— 它被排进了队列。"
             "校验排在排队后面 ⇒ flush 时会按多寄存器写组装出一个 byteCount=0 的畸形帧。");

    // ★ 判据用【从站收到的字节】:等它有足够时间被 flush 出去
    //   (如果它进了队列,槽一空就会被发出去)
    QTest::qWait(1200);

    QCOMPARE(slave.writes.size(), 0);      // 从站一个写帧都没收到
    QCOMPARE(slave.readCount, 1);          // 只有那一个读
    // ★ 而且从站【总共】只该收到那一个读帧 —— 一个畸形帧都不许有。
    //   (只数 FC6 是不够的:畸形帧可能是 FC0x10,不是写,但它确实到了从站。)
    QCOMPARE(slave.receivedFc.size(), 1);
    QCOMPARE(slave.receivedFc.first(), quint8(0x03));
}

// ---------------------------------------------------------------------------
// ⑥ 重连时,排队里的写【必须作废】—— 它要发给【旧连接】
//
//   【为什么这条跟 ④(断线作废)不是一回事】
//     ④ 断线:连接已经没了,队列里的写发不出去 —— 作废是【唯一】合理的选择。
//     ⑥ 重连:此时【旧连接还在】(tcp_->connectTo 里先 abort 再连),
//       所以如果【不】作废,排队里的写会在新连接建好之后被 flush 出去 ——
//       而它要发的是【旧连接】的那台 PLC!
//       ⇒ 现场表现:操作员把工程从 PLC-A 切到 PLC-B,
//         而 PLC-A 还会收到一次"停机"写 —— 动作打在了【错的机器】上。
//
//   【怎么测才有意义】
//     判据必须是"作废通知到了",而不是"从站没收到" ——
//     因为旧连接到底还通不通,取决于 socket 状态,时序上不牢靠。
//     可靠的那条:writeFinished(false) 必须在重连【之后】回来,
//     而且 requestId 要原样带回。
// ---------------------------------------------------------------------------
void tst_ModbusWriteQueue::reconnectVoidsQueuedWrites() {
    RecordingSlave slave;
    QVERIFY(slave.listen(QHostAddress::LocalHost, 0));
    slave.replyDelayMs = 5000;   // 很久才回包 —— 槽一直被占着

    ModbusMaster master;
    QSignalSpy connectedSpy(&master, &ModbusMaster::connected);
    master.connectTo(QStringLiteral("127.0.0.1"), slave.serverPort());
    QVERIFY2(connectedSpy.wait(3000), "连不上自己开的从站");

    // 占住槽
    QModbusDataUnit readUnit(QModbusDataUnit::HoldingRegisters, 0, 3);
    QVERIFY(master.sendReadRequest(readUnit, 1, 1) != nullptr);

    // 排队一个写
    QSignalSpy writeSpy(&master, &ModbusMaster::writeFinished);
    QModbusDataUnit writeUnit(QModbusDataUnit::HoldingRegisters, 2, QList<quint16>{0});
    QVERIFY(master.sendWriteRequest(writeUnit, 1, 100) != nullptr);

    // ★ 重连到【另一台】从站(端口不同 = 另一台机器)
    //
    //   ★★ 判据的写法要注意:writeFinished 是在 connectTo() 里【同步】发出来的,
    //      所以 QSignalSpy::wait()("等计数增加")会【错过】它 ——
    //      wait() 只看"从调用那一刻起有没有新信号",而信号在 wait() 之前就发完了。
    //      ⇒ 判据必须用【计数】,而且要在 connectTo() 【之前】记下基准值。
    //      (我自己第一版就栽在这儿:信号确实发了,测试却报红 ——
    //       判据自己写错的时候,长得跟被测代码坏了一模一样。)
    const int before = writeSpy.count();
    RecordingSlave other;
    QVERIFY(other.listen(QHostAddress::LocalHost, 0));
    master.connectTo(QStringLiteral("127.0.0.1"), other.serverPort());

    // ★ 排队里的写必须被作废,并且【明确】通知调用方失败
    QVERIFY2(writeSpy.count() == before + 1,
             "★ 重连后排队的写没有收到 writeFinished —— 它被留在了队列里,"
             "而它要发的是【旧连接】的那台 PLC(动作会打在错的机器上)。"
             "而且调用方永远等一个不来的 reply ⇒ WriteScheduler 的 pending_ 永远清不掉。");
    QCOMPARE(writeSpy.at(before).at(0).toUInt(), 100u);
    QCOMPARE(writeSpy.at(before).at(1).toBool(), false);

    // ★ 而且【绝不能】被发到新连接上去
    QTest::qWait(800);
    QCOMPARE(other.writes.size(), 0);     // 新从站一个写帧都没收到
}

// ---------------------------------------------------------------------------
// ⑦ 断线时,【在途】的写(不是排队的)也必须收到 writeFinished(false)
//
//   ★ 这条跟 ④ 是两回事,别混:
//     ④ 讲的是【排队】里的写 —— 还没发出去,作废掉就行。
//     ⑦ 讲的是【已经发出去了】、正等着回包的那个写。
//
//   【为什么要单独钉】
//     ModbusMaster 断线时把 pending_ 整个 clear 掉了,
//     而【在途】的写(已经发出去了)在那次 clear 里【没有】收到任何失败通知:
//       · reply 被 setError 了(但那只是给调用方的 QModbusReply 句柄),
//         而 WriteScheduler 等的是【writeFinished 信号】,不是那个句柄;
//       · 于是 pending_ 里的那条写就【永远等不到 reply】。
//     ⇒ WriteScheduler 那边 pending_[reqId] 永远留着,
//       inflightPerPlc_[plc] 永远降不下来 ——
//       那台 PLC 从此【再也不写任何东西】,而且零日志。
//     这跟 2026-10-21 查到的"变量卡在在途态"是同一个形状的故障。
//
//   【怎么测才有意义】
//     判据用 writeFinished 信号 —— 那是 WriteScheduler 真正等的东西。
//     (用 QModbusReply 的 error() 判定不算数:那是另一条通路,
//      WriteScheduler 根本不监听它。)
// ---------------------------------------------------------------------------
void tst_ModbusWriteQueue::inFlightWriteGetsFailureOnDisconnect() {
    RecordingSlave slave;
    QVERIFY(slave.listen(QHostAddress::LocalHost, 0));
    slave.replyDelayMs = 5000;   // 收下但不回 —— 写一直卡在途

    ModbusMaster master;
    QSignalSpy connectedSpy(&master, &ModbusMaster::connected);
    master.connectTo(QStringLiteral("127.0.0.1"), slave.serverPort());
    QVERIFY2(connectedSpy.wait(3000), "连不上自己开的从站");

    // ★ 这一条【直接发出去】(槽是空的)⇒ 它是【在途】的,不是排队的
    QSignalSpy writeSpy(&master, &ModbusMaster::writeFinished);
    QModbusDataUnit writeUnit(QModbusDataUnit::HoldingRegisters, 2, QList<quint16>{0});
    QVERIFY(master.sendWriteRequest(writeUnit, 1, 300) != nullptr);

    // 确认它真的发出去了(在途)
    QVERIFY2(QTest::qWaitFor([&]{ return !slave.writes.isEmpty(); }, 5000),
             "前置条件不成立:写没发出去");

    // ★ 断线
    //   (writeFinished 是在 disconnect() 里【同步】发出来的,所以判据用【计数】,
    //    不用 wait() —— 理由跟 ⑥ 那处一样。)
    const int before = writeSpy.count();
    master.disconnect();

    QVERIFY2(writeSpy.count() == before + 1,
             "★ 断线时【在途】的写没有收到 writeFinished —— "
             "pending_ 被整个 clear 掉了,那条写在 WriteScheduler 那边就【永远等不到 reply】:"
             "pending_[reqId] 永远留着、inflightPerPlc_ 永远降不下来 ⇒ "
             "那台 PLC 从此再也不写任何东西(而且零日志)。");
    QCOMPARE(writeSpy.at(before).at(0).toUInt(), 300u);
    QCOMPARE(writeSpy.at(before).at(1).toBool(), false);
}

// ---------------------------------------------------------------------------
// ⑧ 断线时,【在途】的读也必须收到 readFinished(false)
//
//   ⑦ 的读侧版本 —— 但它比 ⑦ 更严重,后果也更常见:
//
//   PollScheduler 清 inFlight_ 的【唯一】入口是 onDriverReadFinished,
//   而那个入口只会被 readFinished 触发。
//   ⇒ 断线时在途的那批读如果【不】收到 readFinished(false),
//     那批变量就【永远留在 inFlight_ 里】⇒ 那台 PLC 从此再也不发任何读请求。
//
//   ★ 这跟 2026-10-21 查到的"16.5 小时零轮询流量"是同一个形状:
//     那次是【协议级失败】没发 readFinished(BUGFIX-C1),
//     这次是【连接断开】这条路没发。
//     ——同一个症状,两条不同的路。修了一条,另一条照旧漏。
//
//   【怎么测才有意义】
//     判据用 readFinished 信号 —— PollScheduler 真正等的东西。
//     而且要断【在途】的读(从站收下不回),断线才会碰得到那条路径。
// ---------------------------------------------------------------------------
void tst_ModbusWriteQueue::inFlightReadGetsFailureOnDisconnect() {
    RecordingSlave slave;
    QVERIFY(slave.listen(QHostAddress::LocalHost, 0));
    slave.replyDelayMs = 5000;   // 收下但不回 —— 读一直卡在途

    ModbusMaster master;
    QSignalSpy connectedSpy(&master, &ModbusMaster::connected);
    master.connectTo(QStringLiteral("127.0.0.1"), slave.serverPort());
    QVERIFY2(connectedSpy.wait(3000), "连不上自己开的从站");

    // ★ 发一个读,它就卡在途了
    QSignalSpy readSpy(&master, &ModbusMaster::readFinished);
    QModbusDataUnit readUnit(QModbusDataUnit::HoldingRegisters, 0, 3);
    QVERIFY(master.sendReadRequest(readUnit, 1, 500) != nullptr);

    // 确认它真的发出去了(在途)
    QVERIFY2(QTest::qWaitFor([&]{ return slave.readCount >= 1; }, 5000),
             "前置条件不成立:读没发出去");

    // ★ 断线
    const int before = readSpy.count();
    master.disconnect();

    QVERIFY2(readSpy.count() == before + 1,
             "★ 断线时【在途】的读没有收到 readFinished —— "
             "PollScheduler 清 inFlight_ 的唯一入口就是 onDriverReadFinished,"
             "而它只会被 readFinished 触发 ⇒ 那批变量永远卡在在途态 ⇒ "
             "那台 PLC 从此再也不发任何读请求(界面上的值冻住,而且零日志)。");
    QCOMPARE(readSpy.at(before).at(0).toUInt(), 500u);
    QCOMPARE(readSpy.at(before).at(2).toBool(), false);
}

// ---------------------------------------------------------------------------
// ⑨ 重连时,【在途】的写也必须收到 writeFinished(false)
//
//   ★ 这条是【负向对照逼出来的】——
//     负向对照里 W7(拆掉重连那处的失败通知)【没有】让任何一条用例变红,
//     说明测试【漏了】重连时在途请求这一路。
//     漏了不等于那条路是对的 —— 只等于【没测到】。
//     ★ 本项目反复吃亏的就是这个:"没测到"被当成"没问题"。
//
//   【为什么重连这条跟断线那条不是一回事】
//     断线(⑦⑧):连接没了,在途请求自然作废。
//     重连(⑨):【旧连接还在】(TcpClient::connectTo 里先 abort 再连),
//       而 ModbusMaster::connectTo 是在【连新的之前】把 pending_ 清掉的。
//       ⇒ 那笔在途的写要发给【旧连接】,清掉它就必须通知调用方。
//
//   【现场后果】跟 ⑦ 一模一样:
//     WriteScheduler 的 pending_[reqId] 永远留着、inflightPerPlc_ 永远降不下来
//     ⇒ 那台 PLC 从此再也不写任何东西,而且零日志。
// ---------------------------------------------------------------------------
void tst_ModbusWriteQueue::inFlightWriteGetsFailureOnReconnect() {
    RecordingSlave slave;
    QVERIFY(slave.listen(QHostAddress::LocalHost, 0));
    slave.replyDelayMs = 5000;   // 收下但不回 —— 写一直卡在途

    ModbusMaster master;
    QSignalSpy connectedSpy(&master, &ModbusMaster::connected);
    master.connectTo(QStringLiteral("127.0.0.1"), slave.serverPort());
    QVERIFY2(connectedSpy.wait(3000), "连不上自己开的从站");

    // ★ 这一条【直接发出去】(槽是空的)⇒ 它是【在途】的
    QSignalSpy writeSpy(&master, &ModbusMaster::writeFinished);
    QModbusDataUnit writeUnit(QModbusDataUnit::HoldingRegisters, 2, QList<quint16>{0});
    QVERIFY(master.sendWriteRequest(writeUnit, 1, 600) != nullptr);

    // 确认它真的发出去了(在途)
    QVERIFY2(QTest::qWaitFor([&]{ return !slave.writes.isEmpty(); }, 5000),
             "前置条件不成立:写没发出去");

    // ★ 重连到【另一台】从站
    const int before = writeSpy.count();
    RecordingSlave other;
    QVERIFY(other.listen(QHostAddress::LocalHost, 0));
    master.connectTo(QStringLiteral("127.0.0.1"), other.serverPort());

    QVERIFY2(writeSpy.count() == before + 1,
             "★ 重连时【在途】的写没有收到 writeFinished —— "
             "pending_ 被清掉了,但没通知调用方 ⇒ WriteScheduler 那边 "
             "pending_[reqId] 永远留着、inflightPerPlc_ 永远降不下来 ⇒ "
             "那台 PLC 从此再也不写任何东西(而且零日志)。");
    QCOMPARE(writeSpy.at(before).at(0).toUInt(), 600u);
    QCOMPARE(writeSpy.at(before).at(1).toBool(), false);
}

// ---------------------------------------------------------------------------
// ⑩ BUGFIX-E3(2026-10-27):写请求收到【异常响应】时,必须收到 writeFinished(false)
//
//   【原来为什么没有 —— 这就是本条要钉的洞】
//     写请求收到 Modbus 异常响应(FC | 0x80)时,走的是
//     ModbusMaster::onTcpReadyRead 里的异常响应分支:
//         finalizeErrorReply(&req, msg);
//         pending_.erase(it);
//     而 finalizeErrorReply **只给【读】发 readFinished**
//     (它里面有一句 `if (!req->isWrite) emit readFinished(...)`)
//     ⇒ 【写】走这条路时,一个 writeFinished 都不发。
//
//   【后果 —— 比"这一次写失败"严重得多】
//     WriteScheduler 等的是 writeFinished 信号:
//       · pending_[reqId] 永远留着(那条变量永远在途);
//       · inflightPerPlc_[plc] 永远降不下来 —— 而 WriteScheduler
//         每台 PLC 同时只发一个写 ⇒ 那台 PLC 从此【再也不写任何东西】;
//     而且【零日志】—— finalizeErrorReply 会打一条通讯错误,
//     但那说的是"协议报错了",没人看得出"这台 PLC 从此哑了"。
//     ⇒ 形状跟 2026-10-21 的"变量卡在在途态"、BUGFIX-E2 的
//       "断线时在途请求没人通知"【一模一样】:某个容器永远清不掉。
//
//   【★ 为什么"从站明确回答了不行"也算有回音 ★】
//     前面五条失败路径都是"这条写【没能】正常完成",
//     唯独这条是"从站【回答了】 —— 回答的是'不行'"。
//     回答了也算有回音,调用方必须收到通知,否则它只能一直等。
//
//   【怎么测才有意义】
//     判据用 writeFinished 信号 —— 那是 WriteScheduler 真正等的东西。
//     ★ 而且必须同时断言"【只发一次】" ——
//       如果修成"两处都发",调用方会收到两次失败通知,
//       WriteScheduler 的 pending_ 会被清两次(第二次是脏的),
//       那种 bug 比不发更隐蔽。
// ---------------------------------------------------------------------------
void tst_ModbusWriteQueue::exceptionResponseGetsFailure() {
    RecordingSlave slave;
    QVERIFY(slave.listen(QHostAddress::LocalHost, 0));
    slave.replyDelayMs = 0;
    // ★ 从站回异常:0x02 = Illegal Data Address(地址越界)
    slave.exceptionExCode = 0x02;

    ModbusMaster master;
    QSignalSpy connectedSpy(&master, &ModbusMaster::connected);
    master.connectTo(QStringLiteral("127.0.0.1"), slave.serverPort());
    QVERIFY2(connectedSpy.wait(3000), "连不上自己开的从站");

    QSignalSpy writeSpy(&master, &ModbusMaster::writeFinished);

    // ★ 这一条【直接发出去】(槽是空的)⇒ 它是【在途】的
    QModbusDataUnit writeUnit(QModbusDataUnit::HoldingRegisters, 2, QList<quint16>{0});
    QVERIFY(master.sendWriteRequest(writeUnit, 1, 700) != nullptr);

    // ★ 等从站回异常
    QVERIFY2(QTest::qWaitFor([&]{ return writeSpy.count() > 0; }, 2000),
             "★ 写请求收到【Modbus 异常响应】时,调用方没有收到 writeFinished —— "
             "ModbusMaster::finalizeErrorReply 只给【读】发 readFinished,写走这条路时零通知。"
             "⇒ WriteScheduler 的 pending_[reqId] 永远留着、inflightPerPlc_ 永远降不下来 "
             "⇒ 那台 PLC 从此再也不写任何东西(而且零日志)。");

    // ★★★ 这一条【必须有】,不然这条用例是【假绿】 ★★★
    //   上限给 2000 ms(而不是 5000 ms),而 ModbusMaster 的写超时是 3000 ms。
    //   ⇒ 如果异常响应路径【不发】 writeFinished,超时路径会在 3000 ms 时补一个 ——
    //     而 2000 ms 的等待已经先失败了 ⇒ 用例红。
    //   ⇒ 换句话说:这条断言把"【超时】发出来的"和"【异常响应】发出来的"区分开了。
    //
    //   ★ 第一版这个用例【是假绿的】,而且假得很有意思:
    //     从站的异常帧 LEN 写成了 0x03 0x03(= 771)而不是 0x00 0x03(= 3),
    //     主站按 frameSize = 6 + len 等 777 字节,而从站只发了 9 字节
    //     ⇒ 主站【永远没解析到这一帧】
    //     ⇒ writeFinished 是【3000 ms 超时路径】发出来的(日志里 elapsedMs= 3057)
    //     ⇒ 用例绿了,但绿的原因跟它要测的东西【完全无关】。
    //   ⇒ 教训跟 2026-10-21 那次一模一样:
    //     "判据坏掉的样子,和程序坏掉的样子,一模一样。"
    //   ⇒ 所以这里既改了 LEN,又加了【超时窗口】的断言 ——
    //     两条缺一不可:只改 LEN 的话,以后谁再改坏帧格式,又会变成超时绿。

    // ★ 必须【只发一次】—— 发两次比不发更隐蔽
    QTest::qWait(300);
    QCOMPARE(writeSpy.count(), 1);
    QCOMPARE(writeSpy.at(0).at(0).toUInt(), 700u);   // requestId 要原样带回
    QCOMPARE(writeSpy.at(0).at(1).toBool(), false);  // 失败

    // ★ 从站【确实收到过】这一帧 —— 不然这条用例可能在"什么都没发生"的情况下绿。
    //   (比方说帧格式写错、主站根本没发出去 —— 那时 writeFinished 也不会来,
    //    但【超时】那条路径会来 ⇒ 还是能骗过用例。)
    QCOMPARE(slave.receivedFc.size(), 1);
    QCOMPARE(int(slave.receivedFc.at(0)), 0x06);   // FC6 = 写单寄存器
}

QTEST_MAIN(tst_ModbusWriteQueue)
#include "tst_ModbusWriteQueue.moc"