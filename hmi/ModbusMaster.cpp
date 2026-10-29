#include "ModbusMaster.h"
#include "logging/Logging.h"
#include <QtEndian>
#include <QDebug>

// ============================================================
// 通讯帧监视(2026-10-20)的摘要生成
// ============================================================
//
// 【为什么摘要在这里生成,而不是让界面去猜】
//   界面拿到的是原始字节。要显示"FC03 读保持寄存器 addr=0x0000 count=2",
//   就得有人懂 Modbus 的功能码 —— 而界面层不该懂协议。
//   所以协议相关的翻译留在协议这一层(这里),界面只负责显示字符串。
//
// 【为什么用匿名 namespace】
//   这些是纯内部helper,不需要外部链接,也不想污染 qwhmi::comm 命名空间。
namespace {

QString fcHex(quint8 fc) {
    return QStringLiteral("%1").arg(fc, 2, 16, QChar('0')).toUpper();
}

// 功能码 → 人话。
// ★ 异常响应(fc & 0x80)必须单列 —— 它跟原功能码是【两件事】:
//   0x83 是"对 0x03 的异常响应",不是"功能码 0x83"。
//   把 0x80 位掩掉再查表,界面就会把异常显示成正常响应,
//   那正好是现场最需要看清的那种帧。
QString fcName(quint8 fc) {
    if (fc & 0x80) {
        return QStringLiteral("异常响应(原功能码 %1)").arg(fcHex(fc & 0x7F));
    }
    switch (fc) {
        case 0x01: return QStringLiteral("读线圈");
        case 0x02: return QStringLiteral("读离散输入");
        case 0x03: return QStringLiteral("读保持寄存器");
        case 0x04: return QStringLiteral("读输入寄存器");
        case 0x05: return QStringLiteral("写单线圈");
        case 0x06: return QStringLiteral("写单寄存器");
        case 0x0F: return QStringLiteral("写多线圈");
        case 0x10: return QStringLiteral("写多寄存器");
        default:   return QStringLiteral("未知功能码 %1").arg(fcHex(fc));
    }
}

// 数据区域 → 人话(QModbusDataUnit::RegisterType)
QString areaName(QModbusDataUnit::RegisterType t) {
    switch (t) {
        case QModbusDataUnit::DiscreteInputs:  return QStringLiteral("离散输入");
        case QModbusDataUnit::Coils:           return QStringLiteral("线圈");
        case QModbusDataUnit::InputRegisters:  return QStringLiteral("输入寄存器");
        case QModbusDataUnit::HoldingRegisters:return QStringLiteral("保持寄存器");
        case QModbusDataUnit::Invalid:         break;
    }
    return QStringLiteral("未知区域");
}

} // namespace

// ★ 2026-10-27(SOAK-W):进程内实例号。
//   【为什么要它】harness 要把【多台 PLC】的写侧统计分开看 ——
//     多 PLC 工程里每台 PLC 一个 ModbusMaster,它们的统计行会混在同一个日志文件里。
//   【什么时候会变】每次 new ModbusMaster 就 +1。
//     ★ 只有【改 PLC 配置】才会重建 driver(PlcConnectionManager::applyConfig 里
//       sameConnection() 判断"没变就什么都不做");
//       TCP 层面的重连【复用同一个 ModbusMaster 对象】——
//       所以一次长跑里实例号通常一直是同一个。
int ModbusMaster::s_instanceCounter_ = 0;

// ============================================================
// 阶段 2 第 4 练 — 重设计版
// 作者:Wqian + 子衿
// 时间:2026-07-07
// ============================================================

ModbusMaster::ModbusMaster(QObject* parent) : QObject(parent) {
    tcp_ = new TcpClient(this);

    connect(tcp_, &TcpClient::connected, this, &ModbusMaster::connected);
    connect(tcp_, &TcpClient::disconnected, this, [this]() {
        // ★ 2026-10-20:断线时若有在途请求,记一条"作废"帧。
        //   【为什么必须在【清 pending_ 之前】记】
        //     数量得从 pending_ 里数,清完了就不知道刚才有几帧在途。
        //     ★ 第一版把这个判断写在 pending_.clear() 后面 —— 那样它永远是真的 false,
        //       这条帧永远不出现,而代码读起来完全正常。
        //       (跟本仓库"两处数字打架"那类坑同一个形状:写错了不报错,只是永远不发生。)
        if (!pending_.isEmpty()) {
            emit frameTraced(qwhmi::comm::FrameDir::Error, QByteArray(),
                             QStringLiteral("连接断开,%1 个在途请求作废").arg(pending_.size()),
                             -1);
        }
        // ★ 2026-10-26(BUGFIX-E2):在途请求【必须】各收到一个失败通知。
        //   【原来为什么没有】下面这个 handler 只是把 pending_ 整个 clear 掉了,
        //     而 clear 掉的那些请求【没有】任何失败通知:
        //       · reply 上确实 setError 了 —— 但那只是给调用方的 QModbusReply 句柄,
        //         而 PollScheduler / WriteScheduler 等的是【信号】,不是那个句柄;
        //       · 于是那批读变量永远留在 PollScheduler 的 inFlight_ 里
        //         ⇒ 那台 PLC 从此【再也不发任何读请求】;
        //         那笔写在 WriteScheduler 的 pending_[reqId] 也永远留着,
        //         inflightPerPlc_ 永远降不下来
        //         ⇒ 那台 PLC 从此【再也不写任何东西】。
        //     而且【零日志】—— 现场表现:界面上的值冻在那儿不动了。
        //   ⇒ 这跟 2026-10-21 查到的"16.5 小时零轮询流量"是同一个形状的故障,
        //     只不过那次是协议级失败(BUGFIX-C1),这次是【连接断开】这条路。
        failPendingRequests(QStringLiteral("TCP 断开,在途请求作废"));
        recvBuf_.clear();
        // ★ 2026-10-26(BUGFIX-E1):断线时,排队里的写【也】要作废。
        //   它们要发给旧连接 —— 连接没了,发出去没有意义;
        //   而且不留着的话它们永远等不到 writeFinished,
        //   WriteScheduler 那边 pending_ 就永远清不掉(那台 PLC 从此再也不写)。
        failQueuedWrites(QStringLiteral("TCP 断开,排队的写作废"));
        notifyIdleIfEmpty();       // 2026-07-09:TCP 断 → pending_ 已空 → 通知 UI
        emit disconnected();
    });
    connect(tcp_, &TcpClient::errorOccurred, this,
        [this](QString msg) {
            qCDebug(hmiComm) << "[ModbusMaster] TCP 错误(转发):" << msg;
            emit errorOccurred(msg);
        });
    connect(tcp_, &TcpClient::readyRead, this, &ModbusMaster::onTcpReadyRead);

    timeoutTimer_ = new QTimer(this);
    // ★ IMPROVE-A8(2026-07-29):50ms → 100ms,跟 PollScheduler/WriteScheduler 对齐(都是 100ms tick)
    //   100ms 精度下超时检测仍 30 tick 才超 3s,完全够
    // ★ 2026-10-14:【这一行是全项目唯一的真实定义处】。
    //   槽声明 ModbusMaster.h:71 原写"50ms 周期扫"一直没人改,跟这里对不上 ——
    //   两处数字打架最容易让人按旧注释去"修"这里。已把 .h 那处改成 100ms。
    timeoutTimer_->setInterval(100);
    connect(timeoutTimer_, &QTimer::timeout, this, &ModbusMaster::onTimeoutScan);
    timeoutTimer_->start();

    // ★ 2026-10-27(SOAK-W):写侧统计行(60 秒一条,INFO 级)。
    //   【为什么在构造时就起,而不是等第一次 connect】
    //     "一台 PLC 从来没连上过"恰恰是最该被看见的状态 ——
    //     等 connect 才开始打,那种情况就【永远没有统计行】,
    //     而 harness 会把它读成"这轮没有写 ⇒ 验不到写侧通路"。
    //     (看不见证据 ≠ 没问题。)
    writeStats_.instance = ++s_instanceCounter_;
    writeStatsTimer_.setInterval(60000);
    connect(&writeStatsTimer_, &QTimer::timeout, this, &ModbusMaster::onWriteStatsTick);
    writeStatsTimer_.start();
}

void ModbusMaster::connectTo(const QString& host, quint16 port) {
    recvBuf_.clear();
    // ★ 2026-10-26(BUGFIX-E2):在途请求【必须】各收到一个失败通知(理由见构造
    //   函数里那个 disconnected handler 的长注释)。原来这里只是把 pending_
    //   整个 clear 掉,没有通知 —— 于是 PollScheduler 的 inFlight_ 和
    //   WriteScheduler 的 pending_ 都永远清不掉。
    failPendingRequests(QStringLiteral("新连接前清空旧请求"));
    // ★ 2026-10-26(BUGFIX-E1):重连时,排队里的写【也】要作废(理由同上:
    //   它们要发给旧连接,而且留着就永远等不到 writeFinished)。
    failQueuedWrites(QStringLiteral("新连接前清空旧请求,排队的写作废"));
    notifyIdleIfEmpty();           // 2026-07-09:新连接前清空旧请求 → 通知 UI
    tcp_->connectTo(host, port);
}

bool ModbusMaster::isConnected() const {
    return tcp_->isConnected();
}

// ★ C.1(2026-09-17):供 ModbusDriver::disconnect() 调,内部 tcp_->close() 触发 TcpClient::disconnected
void ModbusMaster::disconnect() {
    if (tcp_) tcp_->close();
}

// ============================================================
// type → fc 映射(单点真理)
// ============================================================
// 2026-07-07 新增:把所有功能码集中在这里映射,以后加新 fc 只改这两函数
quint8 ModbusMaster::typeToReadFc(QModbusDataUnit::RegisterType type) {
    switch (type) {
        case QModbusDataUnit::HoldingRegisters:  return 0x03;  // 读保持
        case QModbusDataUnit::InputRegisters:    return 0x04;  // 读输入
        case QModbusDataUnit::Coils:             return 0x01;  // 读线圈
        case QModbusDataUnit::DiscreteInputs:    return 0x02;  // 读离散输入
        default:                                 return 0x00;  // 不支持
    }
}

quint8 ModbusMaster::typeToWriteFc(QModbusDataUnit::RegisterType type, bool single) {
    // 2026-07-07 新增:InputRegisters 和 DiscreteInputs 是只读的,不能写
    switch (type) {
        case QModbusDataUnit::HoldingRegisters:
            return single ? 0x06 : 0x10;  // 写单保持 / 写多保持
        case QModbusDataUnit::Coils:
            return single ? 0x05 : 0x0F;  // 写单线圈 / 写多线圈
        default:
            return 0x00;  // 不支持
    }
}

// ============================================================
// 大端写入 —— 改善清单 #13(2026-10-14)
// ============================================================
//
// 【为什么有这个小函数】改之前全文件有 10 处
// `body.append(static_cast<char>((x >> 8) & 0xFF)); body.append(...(x & 0xFF));`
// ——同一件事抄了 10 遍,而"字节序反了"正好是这段代码最容易犯又最难看的错。
// 收成一个具名函数之后:要错只能错一次,而且 grep appendBE16 就能找全。
//
// 【为什么是 qToBigEndian】它在 Qt 里是【无条件】大端(不像 qFromLittleEndian
// 依赖宿主字节序),语义就是"协议要的那个顺序"。
//
// 【为什么不直接 qToBigEndian 到 QByteArray】qToBigEndian 的签名要 uchar*,
// 指向一个已分配好的 2 字节缓冲区,不能往可能尚未分配内存的 QByteArray 上写。
// 所以先落到栈上 2 字节,再 append 进去。
static inline void appendBE16(QByteArray& out, quint16 v) {
    uchar be[2];
    qToBigEndian<quint16>(v, be);
    out.append(reinterpret_cast<const char*>(be), 2);
}

// ============================================================
// PDU body 组装(★ 2026-10-14 从 sendReadRequest / sendWriteRequest 提取)
// ============================================================
//
// 【为什么要提取成静态函数】原来这两段内联在发送函数里,而发送函数要真连上
// PLC 才走得到 —— 于是本机没有 PLC 时,PDU 的每一个字节都【从未被验证过】。
// 协议字节序错了是从站"一直超时"或 Illegal Data Value 的经典原因,
// 而这类问题在真机联调前根本发现不了。提成静态纯函数后离线可断言完整 body。
//
// 【byteCount 的取法】0x0F 多线圈是 ceil(N/8) 向上取整(9 个线圈 = 2 字节,
// 不是 1 字节 —— 截断会把第 9 个线圈整条丢掉)。0x10 多寄存器是 N*2。
// 两者不能混用,见下面那句 2026-07-13 的教训注释。

// 读请求 body = addr(2) + count(2)
QByteArray ModbusMaster::buildReadBody(quint16 addr, quint16 count) {
    QByteArray body;
    body.reserve(4);
    appendBE16(body, addr);
    appendBE16(body, count);
    return body;
}

// 写请求 body(格式随 fc 变,见头文件注释)
QByteArray ModbusMaster::buildWriteBody(quint8 fc, quint16 addr, const QList<quint16>& values) {
    QByteArray body;
    const int n = values.size();

    // 写单(0x05 线圈 / 0x06 保持寄存器):body = addr(2) + value(2)
    // 只有 1 个值时才走这条 —— 由调用方按 values.size()==1 判定 fc。
    if (n == 1) {
        body.reserve(4);
        appendBE16(body, addr);
        appendBE16(body, values.first());
        return body;
    }

    body.reserve(5 + n * 2);
    appendBE16(body, addr);
    appendBE16(body, quint16(n));

    if (fc == 0x0F) {
        // 0x0F 多线圈 → bit-pack
        // 协议:byteCount = ceil(N/8),每字节 8 个线圈(LSB first),余下 bit 补 0
        // 2026-07-13 阶段 2 第 7 练:之前误用 0x10 格式发 0x0F,ModbusSlave 报
        // Illegal Data Value —— 这条注释别删,它记的是一次真实联调事故。
        const int byteCount = (n + 7) / 8;  // 向上取整
        body.append(static_cast<char>(byteCount));
        QByteArray coils(byteCount, '\x00');  // 默认 0(余下 bit 也是 0)
        for (int i = 0; i < n; ++i) {
            // 只取最低位:输 0/1/0xFF00/0x0000 都接受,统一按 BOOL 解释
            if (values.at(i) & 0x01) {
                coils[i / 8] = coils[i / 8] | static_cast<char>(0x01 << (i % 8));
            }
        }
        body.append(coils);
    } else {
        // 0x10 多寄存器:每值 2 字节大端
        body.append(static_cast<char>(n * 2));
        for (quint16 v : values) {
            appendBE16(body, v);
        }
    }
    return body;
}

// ============================================================
// 帧组装 helper
// ============================================================
// 2026-07-07 新增:所有功能码走同一个 frame builder
//
// ★ 2026-10-14(改善清单 #13):手写移位换成 qToBigEndian。
//   【改动前先把字节钉成基线】—— tst_ModbusFrame.cpp。
//   在此之前 ModbusMaster 组出来的字节【零测试覆盖】,而"零行为变化"
//   这句话没有任何东西能证明。协议字节序错了是从站不响应的经典原因,
//   现场表现是"一直超时",而且本机没有 PLC 时根本验不了。
//   负向对照(改坏 → 必须变红 → 还原)已跑:
//     · TID 改成小端   → 2 条用例红
//     · Length 把自己算进去 → 4 条用例红
//
//   【为什么 qToBigEndian 更该用】
//   手写 `static_cast<char>((x >> 8) & 0xFF)` 要人心里记着"先高后低",
//   而"高低字节反了"正好就是上面那条负向对照 —— 一个肉眼很难在
//   几十行里发现的错误。qToBigEndian 把这件事交给一个具名函数,
//   而且它在 Qt 里是【无条件】大端,不像 qFromLittleEndian 那样依赖宿主字节序。
QByteArray ModbusMaster::buildFrame(quint16 tid, int serverAddress, quint8 fc, const QByteArray& pduBody) {
    // MBAP 头 7 字节:Transaction ID(2) + Protocol(2=0) + Length(2) + Unit ID(1)
    // Length = 后续字节数(UID 1 + PDU fc 1 + body),【不含 Length 自己那 2 字节】
    QByteArray frame;
    frame.reserve(7 + pduBody.size() + 1);

    appendBE16(frame, tid);                                    // Transaction ID
    appendBE16(frame, 0x0000);                                 // Protocol(恒 0)
    appendBE16(frame, quint16(1 + 1 + pduBody.size()));        // Length = UID + FC + body
    frame.append(static_cast<char>(serverAddress & 0xFF));      // Unit ID(1 字节)
    frame.append(static_cast<char>(fc));                        // Function Code
    frame.append(pduBody);                                      // PDU body(addr + data)

    return frame;
}

// ============================================================
// 读请求
// ============================================================
// 2026-07-07 重设计:跟 Qt 官方 sendReadRequest 签名一致
QModbusReply* ModbusMaster::sendReadRequest(const QModbusDataUnit& readUnit, int serverAddress, quint32 requestId) {
    if (!isConnected()) {
        emit errorOccurred("Modbus 未连接");
        return nullptr;
    }
    // 2026-07-09:并发请求限流 —— 上一帧未完成时拒绝新请求
    // 避免从站串行响应不过来丢帧(现象:连续点击 → 部分超时)
    //
    // ★★ 2026-10-23:这一句【不再 emit errorOccurred】,改走 qCDebug ★★
    //
    //   【为什么】限流是【内部调度细节】,不是错误。Modbus 协议串行,
    //   而 PollScheduler::onTick 一个 tick 会把地址排序后拆成多批发出去 ——
    //   所以只要工程里存在地址不连续(跳过几个寄存器)的变量,
    //   【每一次轮询都会走到这一句】。一次都不撞,才说明这个 driver
    //   从来没被并发用过。
    //
    //   而 errorOccurred 在 hmi.cpp:377 被接到
    //   "改状态成 ModbusState::Failed + 弹一个模态 QMessageBox"。
    //   实测后果(2026-10-23,两个实例并行跑 70 秒,同一个二进制):
    //     · 9 个不连续地址变量 → 第一个 tick 就弹一个「Modbus 错误」框
    //     · 主线程卡在那个框上 → 轮询【永久停摆】
    //     · 日志里一条告警都没有(老代码走 qCDebug,INFO 级别看不见)
    //     · 现象:进程活着、CPU 低、内存平、统计行照常每 60 秒打印,
    //             计数永远停在 发出=1 成功=1 —— 全是"正常"的假象
    //     · 对照组(16 个连续地址 → 合批成 1 帧)不发弹窗,70 秒 318 次请求
    //
    //   【功能上零损失】PollScheduler::onTick 的 BUGFIX-B1 已经在 100 ms
    //   后自动重发(sendReadRequest 返回 false → 回滚 inFlight_ +
    //   nextPollAtMs = now+100)。本来就没丢帧,只是延后一轮。
    //
    //   【所以】errorOccurred 从此只留给真正的通讯故障:
    //   未连接 / 参数非法 / 超时 / 回包异常。节流不属于其中任何一种。
    if (!pending_.isEmpty()) {
        qCDebug(hmiComm) << "[ModbusMaster] 上一帧未完成,跳过本次请求(pending="
                         << pending_.size() << ")";
        return nullptr;
    }
    if (readUnit.valueCount() <= 0 || readUnit.valueCount() > 125) {
        emit errorOccurred(QString("读请求 count 非法:%1(范围 1-125)").arg(readUnit.valueCount()));
        return nullptr;
    }
    if (serverAddress < 0 || serverAddress > 0xFF) {
        emit errorOccurred(QString("serverAddress 非法:%1").arg(serverAddress));
        return nullptr;
    }

	quint8 fc = typeToReadFc(readUnit.registerType());// 2026-07-07 新增:功能码映射
	if (fc == 0x00) {// 2026-07-07 新增:不支持的类型
        emit errorOccurred(QString("不支持的 RegisterType"));
        return nullptr;
    }

    // 组 PDU body:addr(2) + count(2)  —— ★ 2026-10-14 提取成 buildReadBody()
    //   【为什么提取】这段要真连上 PLC 才走得到,离线测不到;
    //   而 PDU 字节序错了是从站报 Illegal Data Value 的经典原因。
    int addr = readUnit.startAddress();
    QByteArray body = buildReadBody(quint16(addr), quint16(readUnit.valueCount()));

    // ★ BUGFIX-A1(2026-07-30):改回 quint16,跟 buildFrame 形参对齐(防 0xFFFF+ 截断)
    quint16 tid = ++nextTid_;
    if (tid == 0) tid = ++nextTid_;  // 0 = MBAP 保留,跳过
    QByteArray frame = buildFrame(tid, serverAddress, fc, body);

    qint64 sent = tcp_->send(frame);
    if (sent != frame.size()) {
        emit errorOccurred(QString("发送失败:发了 %1 / %2 字节").arg(sent).arg(frame.size()));
        return nullptr;
    }

    // 创建 reply + 装上下文
    auto* reply = new PendingReply(readUnit.registerType(), serverAddress, this);
    Pending p;
    p.serverAddress = serverAddress;
    p.type          = readUnit.registerType();
    p.reply         = reply;
    p.sentAt        = QDateTime::currentDateTime();
    p.timeoutMs     = 3000;
    p.isWrite       = false;
    p.requestId     = requestId;  // 阶段 3.3:PollScheduler 用来反查
    pending_.insert(tid, p);

    // ★ 2026-10-20:通讯帧监视 —— 记录这一帧【发出】。
    //   【为什么放在 pending_.insert 之后】
    //     这样"发出去的这一帧"和"登记在册的这个请求"是同一件事 ——
    //     后面 onTcpReadyRead 靠 TID 找回它算往返耗时,两边必须指同一个 TID。
    //   【为什么耗时给 -1】发出的时候还不知道要等多久。
    //     填 0 会被 avgElapsedMs() 当成一次 0ms 的合法样本,把平均值拉到假的 0。
    {
        const QString addrHex = QStringLiteral("%1").arg(addr, 4, 16, QChar('0')).toUpper();
        emit frameTraced(qwhmi::comm::FrameDir::Tx, frame,
                         QStringLiteral("FC%1 %2 addr=0x%3 count=%4")
                             .arg(fcHex(fc), areaName(readUnit.registerType()),
                                  addrHex, QString::number(readUnit.valueCount())),
                         -1);
    }

    qCDebug(hmiComm) << "[ModbusMaster] 已发读 FC=0x" << QString::number(fc, 16).toUpper()
             << "TID=" << tid << "slave=" << serverAddress
             << "addr=" << addr << "count=" << readUnit.valueCount()
             << "reqId=" << requestId
             << "frame=" << frame.toHex(' ').toUpper();

    return reply;
}

// ============================================================
// 写请求
// ============================================================
// 2026-07-07 重设计:跟 Qt 官方 sendWriteRequest 签名一致
// 阶段 4.3.0:加 requestId(配合 writeFinished 信号给 WriteScheduler 反查)
//
// ★ 2026-10-27(SOAK-W):这个公开入口现在只是【一层 wrapper】——
//   真正的逻辑在 sendWriteRequestImpl() 里。
//
//   【为什么拆】
//     负向对照脚本(scripts/negctl_write_starve.py)会把
//     "槽被占就排队"那一整段换成【裸 return nullptr;】。
//     如果计数写在被换掉的那段里,变异体会把计数【一起删掉】——
//     那道闸当场瞎掉,而且它还绿着("没看见"被当成了"没问题")。
//   ⇒ 计数放在【这一层】,替换不到 ⇒ 闸永远有牙。
//     同 tst_PollStatsFormat 的理由:要钉的是【两边对得上】,
//     不是"某一边自己看着对"。
//
//   【这一层做三件事】
//     ① 计数:尝试 / 接受 / 拒收(以及拒收的【分类】)
//     ② 按 impl 留下的原因把拒收分到五个桶里
//     ③ 把"没有已知原因"的拒收归到【缺陷】桶 —— 那是新增拒收路径
//        却忘了记账的信号,必须看得见。
QModbusReply* ModbusMaster::sendWriteRequest(const QModbusDataUnit& writeUnit, int serverAddress, quint32 requestId) {
    ++writeStats_.attempts;

    // ★ 原因必须在 impl 进去之前清干净。
    //   否则这一轮如果走的是"没写原因的拒收路径",
    //   会捡到【上一轮】留下的旧原因 ⇒ 缺陷被当成环境放过。
    //   (这个坑跟"两处数字打架"是同一个形状:写错了不报错,只是永远不发生。)
    lastRejectReason_ = RejectReason::None;

    QModbusReply* reply = sendWriteRequestImpl(writeUnit, serverAddress, requestId);

    if (reply != nullptr) {
        ++writeStats_.accepted;
        return reply;
    }

    // ★ 拒收。按【原因】分类 —— 分类在这里做,是因为【这里知道原因】。
    //   harness 那边只能刮日志猜(而那条路已经被实测推翻了)。
    ++writeStats_.rejected;
    switch (lastRejectReason_) {
        case RejectReason::NotConnected:
            ++writeStats_.rejNotConnected; break;   // 环境:断线
        case RejectReason::Invalid:
            ++writeStats_.rejInvalid; break;        // 配置:地址/类型非法
        case RejectReason::QueueFull:
            ++writeStats_.rejQueueFull; break;      // 缺陷:队列撑爆
        case RejectReason::SendFailed:
            ++writeStats_.rejSendFailed; break;     // 环境:发送失败
        case RejectReason::None:
            // ★ 来源不明 ⇒ 【缺陷】。
            //   每一条合法的拒收路径都必须在 impl 里写明原因;
            //   走到这里说明有一条路径【忘了】——
            //   那可能是一条新的静默的洞,不能放过。
            ++writeStats_.rejDefect; break;
    }
    return nullptr;
}

// ★ 2026-10-27(SOAK-W):sendWriteRequest 的真身。
//   【逻辑跟拆出来之前【一模一样】,只多了"在拒收之前写明原因"】
//   —— 每一条 return nullptr 之前多一行 lastRejectReason_ = ...;
//   其余一行没动。
//
//   【★ 进来第一件事必须把原因清成 None ★】
//     (公开 wrapper 里也清了一次 —— 两处都清是【有意的冗余】:
//      wrapper 清是为了"impl 早退"这种路径也不会捡到旧值;
//      这里清是为了"有人直接调 impl"也不会捡到旧值。)
//     少了它,变异体的裸 return nullptr 会被误分类成"有理由的拒收",
//     缺陷就被当成环境放过了 —— 而那道闸正是为了抓这个。
QModbusReply* ModbusMaster::sendWriteRequestImpl(const QModbusDataUnit& writeUnit, int serverAddress, quint32 requestId) {
    lastRejectReason_ = RejectReason::None;

    if (!isConnected()) {
        lastRejectReason_ = RejectReason::NotConnected;
        emit errorOccurred("Modbus 未连接");
        return nullptr;
    }
    // 2026-07-09:并发请求限流 —— 同上,写请求也排队(一次只能有 1 个未完成)
    // ★ 2026-10-23:同样降级成 qCDebug,理由见上面 sendReadRequest 那处的长注释。
    //   写路径更不能当错误:WriteScheduler 的写请求可能和 PollScheduler 的读请求
    //   抢同一个 driver,一次撞车就会弹框 —— 而撞车在多 PLC 工程里是【常态】,
    //   不是故障。同一个 signal 在读侧弹过一次框,写侧不能漏。
    //
    // ★★ 2026-10-26(BUGFIX-E1):这一段的语义【变了】—— 写不再被拒,而是【排队】。
    //
    //   【改之前的真实行为(实测出来的,不是推演)】
    //     槽被读占着 → 直接 return nullptr → WriteScheduler 那边
    //     "driver 拒收了写请求" → dirty 保留,下一帧重试 → 下一帧又被拒 …
    //     实测(sweeps/sweep_m85):604 次写尝试【全部】被拒,0 帧到达从站;
    //     同一时刻 547 次读全部成功。写被拒到读回包到达,平均只隔 2.18 ms。
    //     而同样配置的另外六轮(sweep_m70/m99/m88/m92/m95/m97)写【一次都没被拒】。
    //     ⇒ 决定成败的是两个 100ms 节拍的【启动相位差】,而它一辈子不变。
    //     ⇒ 这是【开机掷一次硬币】:掷到坏的那一面,24 小时一个字节都写不出去。
    //
    //   【为什么"拒收"在这里是错的,而不只是"不够好"】
    //     拒收对读路径是对的:读下一秒就会重发,丢一帧没有代价。
    //     但写路径【有代价】:值是"停机"这种一次性动作,
    //     而读是"刷新显示"这种可以随时再来的连续动作。
    //     把"可以再来的"和"不能错过的"放进同一个槽去抢,
    //     结果就是后者【系统性地】被前者压死 —— 而且压不压得死取决于运气。
    //
    //   【改法】
    //     槽被占着 → 写【入队】,返回 true(已接受,必定会有 writeFinished)。
    //     槽一空 → flushQueuedWrites() 把队里的写发出去。
    //     ⇒ 写从"跟读抢"变成"等读让出来",相位不再是决定因素。
    //
    // ★★ 2026-10-26:【校验必须排在排队【前面】—— 这是本节第一版写错的地方】
    //   第一版把"槽被占就排队"这个分支放在了参数校验【前面】,
    //   于是【不合法的写请求也会被排队】:
    //     · values 为空  → 排进队 → flush 时按"多寄存器写"(0x10)组装,
    //       byteCount=0 ⇒ 发出一个畸形的帧给 PLC;
    //     · serverAddress 越界 → 排进队 → flush 时不校验 ⇒ 帧的 unit id 是乱的;
    //     · 不支持的 RegisterType → 排进队 → flush 时才发现,白跑一趟。
    //   ⇒ 校验在前、排队在后:不合法的请求【当场拒收】(跟改之前一样),
    //     只有【合法的】写才会进队列。
    if (serverAddress < 0 || serverAddress > 0xFF) {
        lastRejectReason_ = RejectReason::Invalid;
        emit errorOccurred(QString("serverAddress 非法:%1").arg(serverAddress));
        return nullptr;
    }
    if (writeUnit.startAddress() < 0 || writeUnit.startAddress() > 0xFFFF) {
        lastRejectReason_ = RejectReason::Invalid;
        emit errorOccurred(QString("addr 非法:%1").arg(writeUnit.startAddress()));
        return nullptr;
    }
    if (writeUnit.values().isEmpty()) {
        lastRejectReason_ = RejectReason::Invalid;
        emit errorOccurred("写请求 values 不能为空");
        return nullptr;
    }

    // 单写 vs 多写:Coils/Holding 时按 value 数量决定
    // ★ 2026-10-26:fc 在【排队之前】就算出来 —— 排队分支里不再重算。
    const int addr = writeUnit.startAddress();
    const auto& values = writeUnit.values();
    const bool single = (values.size() == 1);
    const quint8 fc = typeToWriteFc(writeUnit.registerType(), single);
    if (fc == 0x00) {
        lastRejectReason_ = RejectReason::Invalid;
        emit errorOccurred(QString("不支持的写 RegisterType"));
        return nullptr;
    }

    if (!pending_.isEmpty()) {
        if (queuedWrites_.size() >= kMaxQueuedWrites) {
            // ★ 队列满了 —— 这才真的拒收。
            //   正常情况下队列里最多 1 条(WriteScheduler 每台 PLC 同时只发一个写),
            //   走到这里说明有别的东西在往里灌请求。
            //   宁可拒收也不能让队列无限涨(那会吃内存,而且掩盖真正的问题)。
            qCDebug(hmiComm) << "[ModbusMaster] 写队列已满(" << queuedWrites_.size()
                             << "),拒收本次写请求";
            // ★ 队列满 ⇒ 缺陷(不是环境问题)。
            //   正常情况下队列里最多 1 条(WriteScheduler 每台 PLC 同时只发一个写),
            //   走到这里说明有别的东西在往里灌请求 ⇒ 这本身就是要查的事。
            lastRejectReason_ = RejectReason::QueueFull;
            return nullptr;
        }
        QueuedWrite qw;
        qw.type          = writeUnit.registerType();
        qw.serverAddress = serverAddress;
        qw.addr          = writeUnit.startAddress();
        // ★ 值在【入队这一刻】就定下来 —— 不是"发出去那一刻"。
        //   排队可能跨过好几拍(读一直占着槽的话)。
        //   如果等到发出去才取 v->value(),那一串等待期间本地值变了,
        //   发出去的就是【另一个值】—— 跟 BUGFIX-D1 修的是同一类错,
        //   只不过那次错在"reply 时刻",这次错在"发送时刻"。
        qw.values        = writeUnit.values();
        qw.requestId     = requestId;
        queuedWrites_.append(qw);
        // ★ 2026-10-27(SOAK-W):入队计数。
        //   这一行【在队列分支里】—— 而队列分支正是负向对照会替换掉的那一段。
        //   ⇒ 变异之后这一行会跟着消失 ⇒ 计数【不再增长】。
        //   ★ 而这恰恰是【要】的:闸报的是"排队数没涨、拒收数涨了",
        //     正是 BUGFIX-E1 那个洞的形状。
        //   ⇒ 但"入队"这件事【本身】也该有个不受变异影响的记录 ——
        //     所以下面那句"接受"是由【公开 wrapper】记的(wrapper 在 impl 外面,
        //     变异体替换不到)。两条数一起看,才说得清发生了什么。
        ++writeStats_.queued;
        qCDebug(hmiComm) << "[ModbusMaster] 槽被占着,写请求【排队】(队列长度="
                         << queuedWrites_.size() << ",requestId=" << requestId << ")";
        // 返回 true:这个写【已被接受】,必定会有一个 writeFinished 回来。
        //   ★ 这里必须返回 true —— 返回 nullptr 的语义是"拒收,不会有 writeFinished",
        //     而排队等来的那个 writeFinished【真的会来】(flush 之后)。
        //     撒谎的话,WriteScheduler 会永远等一个不存在的 reply
        //     (pending_ 泄漏 → 那台 PLC 再也不写任何东西)。
        //   但 QModbusReply 句柄还是得给一个 —— 它只是给调用方一个"受理凭证",
        //   真正的完成通知走 writeFinished 信号(跟读路径一样)。
        auto* queuedReply = new PendingReply(writeUnit.registerType(), serverAddress, this);
        queuedWrites_.last().reply = queuedReply;
        return queuedReply;
    }
    // ★ 2026-10-14:body 组装提取成 buildWriteBody(),理由同 buildReadBody。
    //   原本这段 40 行内联在这里,离线一条都测不到。
    QByteArray body = buildWriteBody(fc, quint16(addr), values);

    // ★ BUGFIX-A1(2026-07-30):改回 quint16,跟 buildFrame 形参对齐(防 0xFFFF+ 截断)
    quint16 tid = ++nextTid_;
    if (tid == 0) tid = ++nextTid_;
    QByteArray frame = buildFrame(tid, serverAddress, fc, body);

    qint64 sent = tcp_->send(frame);
    if (sent != frame.size()) {
        lastRejectReason_ = RejectReason::SendFailed;
        emit errorOccurred(QString("发送失败:发了 %1 / %2 字节").arg(sent).arg(frame.size()));
        return nullptr;
    }

    auto* reply = new PendingReply(writeUnit.registerType(), serverAddress, this);
    Pending p;
    p.serverAddress = serverAddress;
    p.type          = writeUnit.registerType();
    p.reply         = reply;
    p.sentAt        = QDateTime::currentDateTime();
    p.timeoutMs     = 3000;
    p.isWrite       = true;
    p.requestId     = requestId;  // 阶段 4.3.0:WriteScheduler 用来反查 dirty Variable
    // ★★ 2026-10-21:把【请求的起始地址】存下来 —— 回包要拿它比对 echo。
    //   写响应(0x05/0x06/0x0F/0x10)会回显这个地址,回显的对不上就是异常。
    p.startAddress  = quint16(addr);
    pending_.insert(tid, p);

    // ★ 2026-10-27(SOAK-W):"已发出"计数 —— 这一帧真的上了总线。
    //   它是恒等式①的左端:已发出 == 成功 + 失败 + 在途写。
    //   差值不为零 ⇒ 有【发出去却永远没回来】的写 ⇒ 泄漏。
    ++writeStats_.sent;

    // ★ 2026-10-20:通讯帧监视 —— 记录这一帧【发出】(理由同读请求那处)
    {
        const QString addrHex = QStringLiteral("%1").arg(addr, 4, 16, QChar('0')).toUpper();
        emit frameTraced(qwhmi::comm::FrameDir::Tx, frame,
                         QStringLiteral("FC%1 %2 addr=0x%3 count=%4")
                             .arg(fcHex(fc), areaName(writeUnit.registerType()),
                                  addrHex, QString::number(values.size())),
                         -1);
    }

    qCDebug(hmiComm) << "[ModbusMaster] 已发写 FC=0x" << QString::number(fc, 16).toUpper()
             << "TID=" << tid << "slave=" << serverAddress
             << "addr=" << addr << "values.size=" << values.size()
             << "frame=" << frame.toHex(' ').toUpper();

    return reply;
}

// ============================================================
// ★ 2026-10-26(BUGFIX-E1):把排队的写发出去
// ============================================================
//
// 【什么时候被调】notifyIdleIfEmpty() —— 也就是【槽变空】的那一刻:
//   读回包到达 / 超时清理 / 连接变化。三条路径都经过那个 helper。
//
// 【为什么只发一个】Modbus 协议串行,一次只能有一个未决请求。
//   发一个之后槽又被占了,剩下的等下一个空槽。
//
// 【为什么先进先出】写有顺序:先写"目标转速"再写"启停标志",
//   交换顺序就是另一种语义。队列必须保序。
//
// 【发送失败怎么办】跟直接发送一样的处理:
//   发不出去(连接断了/参数非法)→ failQueuedWrites 把【整队】作废。
//   ★ 不能只作废这一条然后留着后面的 —— 那样前面失败的语义就丢了,
//     而且调用方那边会等到一个永远不来的 writeFinished。
void ModbusMaster::flushQueuedWrites() {
    if (queuedWrites_.isEmpty()) return;
    if (!pending_.isEmpty()) return;      // 槽还占着,等下一个空槽
    if (!isConnected()) {
        failQueuedWrites(QStringLiteral("连接已断开,排队的写作废"));
        return;
    }

    QueuedWrite qw = queuedWrites_.takeFirst();

    // ★ 用【排队时存下的】值,不是现在重新取的。
    //   排队可能跨过好几拍,本地值在这期间可能变了 ——
    //   发出去的那个值必须是【入队那一刻】定下来的那个。
    //
    // ★ 为什么用 setValues() 而不是构造函数的 values 重载:
    //   QModbusDataUnit(RegisterType,int,const QList<quint16>&) 这个重载
    //   在 MSVC 下会被解析成 (RegisterType,int,int)(见 handleReadResponse 里的老注释),
    //   QVector→int 编不过;而 (RegisterType,int) 两参重载【压根不存在】。
    //   所以走"默认构造 + 三个 setter" —— 稳,而且显式。
    QModbusDataUnit unit;
    unit.setRegisterType(qw.type);
    unit.setStartAddress(static_cast<quint16>(qw.addr));
    unit.setValues(qw.values);

    const int addr = qw.addr;
    const auto& values = qw.values;
    const int serverAddress = qw.serverAddress;

    bool single = (values.size() == 1);
    quint8 fc = typeToWriteFc(qw.type, single);
    if (fc == 0x00) {
        // ★ 这一条已经【出队】了,所以 failQueuedWrites 不会管到它 ——
        //   得自己给一个 writeFinished(false),否则调用方永远等一个不来的 reply
        //   (pending_ 泄漏 → 那台 PLC 从此再也不写任何东西)。
        failOneQueuedWrite(qw, QStringLiteral("不支持的写 RegisterType"));
        failQueuedWrites(QStringLiteral("不支持的写 RegisterType,后续的写也作废"));
        return;
    }

    QByteArray body = buildWriteBody(fc, quint16(addr), values);

    quint16 tid = ++nextTid_;
    if (tid == 0) tid = ++nextTid_;
    QByteArray frame = buildFrame(tid, serverAddress, fc, body);

    qint64 sent = tcp_->send(frame);
    if (sent != frame.size()) {
        // 发出去了但没发全 —— 半个帧在总线上是【更糟】的情况,
        // 但按现有语义这跟"发送失败"是一回事:调用方要收到 writeFinished(false)。
        // ★ 这一帧的字节已经有一半在线上了,后面的写即使发出去,
        //   从站那边看到的也是一个破损的帧序列 ⇒ 整队作废。
        failOneQueuedWrite(qw, QStringLiteral("发送失败:发了 %1 / %2 字节")
                                 .arg(sent).arg(frame.size()));
        failQueuedWrites(QStringLiteral("发送失败:发了 %1 / %2 字节,后续的写也作废")
                             .arg(sent).arg(frame.size()));
        return;
    }

    // 把排队时建的 reply 挂到 pending_ 上 —— 现在它才真的有一个在途请求。
    Pending p;
    p.serverAddress = serverAddress;
    p.type          = qw.type;
    p.reply         = qw.reply;
    p.sentAt        = QDateTime::currentDateTime();
    p.timeoutMs     = 3000;
    p.isWrite       = true;
    p.requestId     = qw.requestId;
    p.startAddress  = quint16(addr);
    pending_.insert(tid, p);

    // ★ 2026-10-27(SOAK-W):排队写【真的发出去了】+ "已发出"。
    //   两条恒等式②(排队 == 排队发出 + 排队作废 + 在队)的右端就靠这个。
    ++writeStats_.queuedSent;
    ++writeStats_.sent;

    {
        const QString addrHex = QStringLiteral("%1").arg(addr, 4, 16, QChar('0')).toUpper();
        emit frameTraced(qwhmi::comm::FrameDir::Tx, frame,
                         QStringLiteral("FC%1 %2 addr=0x%3 count=%4")
                             .arg(fcHex(fc), areaName(qw.type),
                                  addrHex, QString::number(values.size())),
                         -1);
    }

    qCDebug(hmiComm) << "[ModbusMaster] 排队写已发出 FC=0x" << QString::number(fc, 16).toUpper()
             << "TID=" << tid << "slave=" << serverAddress
             << "addr=" << addr << "values.size=" << values.size()
             << "requestId=" << qw.requestId
             << "frame=" << frame.toHex(' ').toUpper();
}

// ★ 2026-10-26(BUGFIX-E2):在途请求【全部作废】,并且【每人一个失败通知】
//
// 【它在防什么】
//   连接断开 / 要重连时,pending_ 里那些【已经发出去了】的请求不会再有回包。
//   如果只是把它们从 pending_ 里删掉(原来就是 pending_.clear()),
//   调用方就【永远等一个不来的 reply】:
//     · 读 → PollScheduler 的 inFlight_ 里那批变量永远出不来
//            ⇒ 那台 PLC 从此再也不发任何读请求(界面上的值冻住);
//     · 写 → WriteScheduler 的 pending_[reqId] 永远留着,
//            inflightPerPlc_ 永远降不下来
//            ⇒ 那台 PLC 从此再也不写任何东西。
//   而这两种都是【零日志】的 —— 现场只看得出"值不动了",看不出原因。
//
// 【为什么不能只靠 reply->setError】
//   reply 句柄上的错误是给"直接用 QModbusReply 的人"看的。
//   而 PollScheduler / WriteScheduler 等的是【信号】——
//   readFinished / writeFinished。它们不监听 reply 的 finished。
//   ⇒ 通知必须【发信号】,不能只 setError。
//
// 【为什么按 isWrite 分流】
//   跟 onTimeoutScan 里那处一样的道理:同一个请求不能同时惊动
//   读调度器和写调度器。写就发 writeFinished,读就发 readFinished。
void ModbusMaster::failPendingRequests(const QString& reason) {
    if (pending_.isEmpty()) return;
    const auto keys = pending_.keys();
    for (quint16 tid : keys) {
        Pending& p = pending_[tid];
        if (p.reply) {
            p.reply->setError(QModbusDevice::ConnectionError, reason);
            p.reply->setFinished(true);
            p.reply->deleteLater();
        }
        // ★ 关键:发信号,让调度器把在途状态清掉。
        if (p.isWrite) {
            // ★ 2026-10-27(SOAK-W):写失败计数。
            //   这一笔写【已经上了总线】(sent++ 已经发生过),
            //   现在被断线作废了 ⇒ 它必须落到"失败"这一侧,
            //   否则恒等式①(已发出 == 成功 + 失败 + 在途写)会残差不为零
            //   ⇒ harness 会把【一次正常的断线】报成"写泄漏"。
            //   ★ 那是误报 —— 断线是环境事件,不是缺陷。
            ++writeStats_.fail;
            emit writeFinished(p.requestId, false);
        } else {
            emit readFinished(p.requestId, QModbusDataUnit(), false);
        }
    }
    pending_.clear();
    logCommError(QStringLiteral("%1(%2 个在途请求)").arg(reason).arg(keys.size()));
}

// ★ 2026-10-26(BUGFIX-E1):【单独一条】排队写失败时的收尾
//
// 【为什么需要它】flushQueuedWrites 里那条写是【已经出队】的
//   (takeFirst 拿走了),所以 failQueuedWrites(整队作废)【管不到它】。
//   而它如果【不】拿到 writeFinished,调用方就永远等一个不来的 reply。
//   ⇒ 这一条必须自己收尾。
void ModbusMaster::failOneQueuedWrite(const QueuedWrite& qw, const QString& reason) {
    if (qw.reply) {
        qw.reply->setError(QModbusDevice::ConnectionError, reason);
        qw.reply->setFinished(true);
        qw.reply->deleteLater();
    }
    // ★ 2026-10-27(SOAK-W):排队写作废计数。
    //   ★ 这一条【单列】是关键:断线时排队写被作废是【正常的环境事件】,
    //     不是"排队写丢了"。原判据把这两件事混成一件 ⇒ 必然误报。
    ++writeStats_.queuedVoided;
    emit writeFinished(qw.requestId, false);
}

// ★ 2026-10-26(BUGFIX-E1):排队里的写【全部作废】
//
// 【为什么要给调用方一个交代】调用方(WriteScheduler)等的是 writeFinished。
//   队列里的写如果【不】发通知就消失,WriteScheduler 那边 pending_ 就永远清不掉
//   —— 那台 PLC 从此再也不写任何东西,而且零日志。
//   ⇒ 作废时【必须】给每个发 writeFinished(false)。
//
// 【为什么失败就是失败,不能"先放着以后再说"】
//   队列里的写要发给【旧连接】。连接都没了,发出去也没有意义。
//   而留着它们 = 它们永远等不到 writeFinished = 上面那个故障。
void ModbusMaster::failQueuedWrites(const QString& reason) {
    if (queuedWrites_.isEmpty()) return;
    const QList<QueuedWrite> doomed = queuedWrites_;
    queuedWrites_.clear();
    for (const QueuedWrite& qw : doomed) {
        if (qw.reply) {
            qw.reply->setError(QModbusDevice::ConnectionError, reason);
            qw.reply->setFinished(true);
            qw.reply->deleteLater();
        }
        // ★ 失败必须让调用方知道 —— 否则它永远等一个不会来的 reply。
        // ★ 2026-10-27(SOAK-W):排队写作废计数(理由同 failOneQueuedWrite)。
        ++writeStats_.queuedVoided;
        emit writeFinished(qw.requestId, false);
    }
    logCommError(QStringLiteral("排队的写作废(%1 条):%2").arg(doomed.size()).arg(reason));
}

// ============================================================
// onTcpReadyRead:拆 MBAP + 按 fc 分发拆帧
// ============================================================
// 2026-07-07 重设计:5 个功能码拆帧逻辑统一在一个 switch 里
void ModbusMaster::onTcpReadyRead(const QByteArray& data) {
    recvBuf_.append(data);

    while (true) {
        if (recvBuf_.size() < 7) break;

        const quint8* p = reinterpret_cast<const quint8*>(recvBuf_.constData());
        // ★ BUGFIX-A1(2026-07-30):改回 quint16,跟 pending_ key 对齐(MBAP TID 标准 2 字节)
        quint16 tid   = (quint16(quint8(p[0])) << 8) | quint16(quint8(p[1]));
        quint16 proto = (quint16(p[2]) << 8) | quint16(p[3]);
        quint16 len   = (quint16(p[4]) << 8) | quint16(p[5]);
        quint8  uid   = p[6];

        if (proto != 0x0000) {
            emit errorOccurred(QString("MBAP Protocol 字段非 0:0x%1").arg(proto, 4, 16, QChar('0')));
            recvBuf_.clear();
            return;
        }

        int frameSize = 6 + len;
        if (recvBuf_.size() < frameSize) break;

        QByteArray frame = recvBuf_.left(frameSize);
        quint8 fc = static_cast<quint8>(frame[7]);

        auto it = pending_.find(tid);
        if (it == pending_.end()) {
            // 未知 TID,丢弃
            // ★ 2026-10-20:这一帧【不能默默丢】。
            //   未知 TID 意味着对端发了一帧我们没在等的 —— 可能是上一轮的残留响应,
            //   也可能是对端串了会话。两种都是现场要查的事,
            //   而"悄悄丢掉"的表现是"偶尔多发一帧,PLC 也不报错",极难定位。
            emit frameTraced(qwhmi::comm::FrameDir::Error, frame,
                             QStringLiteral("未知 TID=%1(收到的帧已丢弃)").arg(tid), -1);
            recvBuf_.remove(0, frameSize);
            continue;
        }
        Pending req = it.value();

        // ★ 2026-10-20:往返耗时 = 收到响应 - 发出请求。
        //   req.sentAt 是发出那一刻 —— 这是全项目唯一能算出往返耗时的地方
        //   (TcpClient 那层没有"请求"的概念)。
        const qint64 elapsedMs = req.sentAt.msecsTo(QDateTime::currentDateTime());

        if (uid != quint8(req.serverAddress)) {
            // 从站地址对不上:同样是"看不见就查不了"的那类帧
            emit frameTraced(qwhmi::comm::FrameDir::Error, frame,
                             QStringLiteral("从站地址不匹配:帧 UID=%1 期望 %2")
                                 .arg(uid).arg(req.serverAddress),
                             elapsedMs);
            recvBuf_.remove(0, frameSize);
            continue;
        }

        // 异常响应(FC | 0x80)
        if ((fc & 0x80) != 0) {
            quint8 exCode = (frame.size() > 8) ? static_cast<quint8>(frame[8]) : 0;
            const char* exMsg = "未知异常";
            switch (exCode) {
                case 0x01: exMsg = "Illegal Function(不支持的功能码)"; break;
                case 0x02: exMsg = "Illegal Data Address(地址越界)"; break;
                case 0x03: exMsg = "Illegal Data Value(值越界)"; break;
                case 0x04: exMsg = "Slave Device Failure(从机故障)"; break;
                case 0x05: exMsg = "Acknowledge(从机忙)"; break;
                case 0x06: exMsg = "Slave Device Busy(从机忙)"; break;
                case 0x08: exMsg = "Memory Parity Error(存储奇偶错)"; break;
                case 0x0A: exMsg = "Gateway Path Unavailable(网关路径不可用)"; break;
                case 0x0B: exMsg = "Gateway Target Failed(网关目标无响应)"; break;
            }
            QString msg = QString("Modbus 异常响应:FC=0x%1 ExCode=0x%2(%3)")
                            .arg(fc, 2, 16, QChar('0'))
                            .arg(exCode, 2, 16, QChar('0'))
                            .arg(exMsg);
            // ★ 2026-10-20:异常响应【必须】在监视器里显示成"错误"帧。
            //   它是现场最需要看清的一类帧(地址越界/值越界/从机故障),
            //   而原来它只走 errorOccurred 弹一次窗,之后就再也找不到了。
            emit frameTraced(qwhmi::comm::FrameDir::Error, frame, msg, elapsedMs);
            finalizeErrorReply(&req, msg);  // ★ IMPROVE-B5(2026-07-29):5 处重复 helper 替

            // ★★ BUGFIX-E3(2026-10-27):写请求收到【异常响应】时,必须发 writeFinished ★★
            //
            //   【原来为什么没有】
            //     finalizeErrorReply 只给【读】发 readFinished(:878 的 `if (!req->isWrite)`),
            //     写请求走这条路时【一个通知都不发】。
            //   【后果 —— 比"这一次写失败"严重得多】
            //     WriteScheduler 等的是 writeFinished 信号:
            //       · pending_[reqId] 永远留着 ⇒ 内存里那条变量永远在途;
            //       · inflightPerPlc_[plc] 永远降不下来 ⇒ 那台 PLC
            //         从此【再也不写任何东西】(WriteScheduler 每台 PLC 同时只发一个写)。
            //     而且【零日志】—— finalizeErrorReply 会打一条通讯错误,
            //     但那说的是"协议报错了",没人看得出"这台 PLC 从此哑了"。
            //     ⇒ 形状跟 2026-10-21 的"变量卡在在途态"、BUGFIX-E2 的
            //       "断线时在途请求没人通知"【一模一样】:都是某个容器永远清不掉。
            //
            //   【会不会重复发】
            //     不会。写侧全部的 writeFinished 出口是这六个,互斥:
            //       :692 failPendingRequests / :713 failOneQueuedWrite /
            //       :737 failQueuedWrites / :980 handleWriteResponse 短帧 /
            //       :1032 handleWriteResponse 成功 / :1076 onTimeoutScan
            //     而 finalizeErrorReply 对写【明确不发】。
            //     每个 requestId 只在 pending_ / queuedWrites_ 其中一个容器里,
            //     且被【同一条路径】移除 ⇒ 不重复、不漏。
            //
            //   【为什么写侧现在只剩这一条漏网】
            //     前面五个出口都是"这条写【没能】正常完成",
            //     唯独这条是"从站【明确地】回答了 —— 回答的是'不行'"。
            //     回答了也算有回音,调用方必须收到通知。
            if (req.isWrite) {
                // ★ 2026-10-27(SOAK-W):写失败计数(帧上了总线,从站回了异常)。
                ++writeStats_.fail;
                emit writeFinished(req.requestId, false);
            }

            pending_.erase(it);
            notifyIdleIfEmpty();   // 2026-07-09:异常响应后可能 pending_ 变空
            recvBuf_.remove(0, frameSize);
            continue;
        }

        // 正常响应 — 按 fc 分发
        // ★ 2026-10-20:记录这一帧【收到】。
        //   【为什么在分发【之前】记】
        //     分发过程可能因为"帧太短""数据不足"判成错误 ——
        //     那种情况后面走 finalizeErrorReply,会再发一条 errorOccurred。
        //     但那一帧的字节本身还是收到了,监视器里要能看到它。
        emit frameTraced(qwhmi::comm::FrameDir::Rx, frame,
                         QStringLiteral("FC%1 %2 响应(%3 字节)")
                             .arg(fcHex(fc), fcName(fc), QString::number(frame.size())),
                         elapsedMs);

        if (req.isWrite) {
            handleWriteResponse(fc, &req, frame);
        } else {
            handleReadResponse(fc, &req, frame);
        }

        pending_.erase(it);
        notifyIdleIfEmpty();       // 2026-07-09:正常响应 → pending_ 变空时通知 UI
        recvBuf_.remove(0, frameSize);
    }
}

// ★ IMPROVE-B5(2026-07-29):错误收尾 helper,5 处重复提一处
void ModbusMaster::finalizeErrorReply(Pending* req, const QString& msg) {
    // ★ 2026-10-21(BUGFIX-C1):错误【先记日志】再发信号。
    //   原来协议层的失败只有 hmi.cpp 里一句 qCDebug(hmiComm) ——
    //   默认 INFO 级别下整条路径一声不吭,现场表现是"程序活着,PLC 不通,日志里什么都没有"。
    //   2026-10-21 查"16.5 小时零轮询流量"的根因,查到的就是这条静默路径。
    logCommError(msg);
    emit errorOccurred(msg);

    // ★ 2026-10-21(BUGFIX-C1):读失败【必须】补一个 readFinished(false)。
    //
    //   【原来为什么没有】
    //     成功路径发 readFinished(..., true)(ModbusMaster.cpp:639),
    //     失败路径一个都不发 —— 异常响应(FC|0x80)、读响应帧太短、数据不足,
    //     三条失败路径全都只走到这个 helper。
    //     而 PollScheduler 清 inFlight_ 的【唯一】地方是 onDriverReadFinished。
    //     于是:一次失败 → 那批变量永远留在在途态 → 再也不会被发出去。
    //     现场表现:一次偶发异常之后,整台 PLC 再也没有任何读请求,
    //     而日志里只有一句 Debug 级的 "上一帧未完成,稍后再发"。
    //
    //   【为什么这里不会重复发】
    //     readFinished 全文件只发两次:成功一次(成功路径)、这里一次(失败路径),互斥。
    //   【对写请求不适用】
    //     写路径的失败由调用方发 writeFinished(false)(handleWriteResponse),
    //     这里只管读 —— 不然同一个请求会同时惊动读调度器和写调度器。
    if (!req->isWrite) {
        emit readFinished(req->requestId, QModbusDataUnit(), false);
    }

    if (req->reply) {
        req->reply->setError(QModbusDevice::ProtocolError, msg);
        req->reply->setFinished(true);
        req->reply->deleteLater();
    }
}

// ★ 2026-10-21(BUGFIX-C1):通讯错误限速日志
//   规则:第 1 次打 WARNING(现场第一眼就要看到完整原因),
//        之后 10 秒内的重复降级到 Debug(不落默认级别),
//        每满 10 秒补一条 WARNING 汇总(累计次数 + 最近一次原因)。
//   为什么必须有"第 1 次"这条:限速再狠,也不能把第一次失败吞掉 ——
//   那一次才是现场的根因线索,后面 86 万次都只是它的回声。
void ModbusMaster::logCommError(const QString& msg) {
    ++commErrorCount_;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (commErrorCount_ == 1 || now - lastCommErrorLogMs_ >= kCommErrorSummaryIntervalMs) {
        lastCommErrorLogMs_ = now;
        qCWarning(hmiComm) << "[ModbusMaster] 通讯错误(累计" << commErrorCount_ << "次):" << msg;
    } else {
        qCDebug(hmiComm) << "[ModbusMaster] 通讯错误(已限速,累计" << commErrorCount_ << "次):" << msg;
    }
}

// 2026-07-07 新增:读响应拆帧(0x01/0x02/0x03/0x04)
void ModbusMaster::handleReadResponse(quint8 fc, Pending* req, const QByteArray& frame) {
    // 2026-07-09:测速 —— sentAt 是发送时刻,这里算收到响应的耗时
    qCDebug(hmiComm) << "[ModbusMaster] 收到读响应 FC=0x" << QString::number(fc, 16).toUpper()
             << "elapsedMs=" << req->sentAt.msecsTo(QDateTime::currentDateTime());
    if (frame.size() < 9) {
        finalizeErrorReply(req, QString("读响应帧太短:%1 字节").arg(frame.size()));  // ★ IMPROVE-B5
        return;
    }

    quint8 byteCount = static_cast<quint8>(frame[8]);
    if (frame.size() < 9 + byteCount) {
        finalizeErrorReply(req, QString("读响应数据不足:声明 %1 字节,实际 %2 字节")  // ★ IMPROVE-B5
                                  .arg(byteCount).arg(frame.size() - 9));
        return;
    }

    QModbusDataUnit unit;  // 2026-07-08:默认构造,后面按 fc 分支 setRegisterType + setStartAddress + setValues


    // 计算单元 count(0x03/0x04 是 byteCount/2,0x01/0x02 是 byteCount*8)
    int unitCount = 0;
    if (fc == 0x03 || fc == 0x04) {
        unitCount = byteCount / 2;
        QVector<quint16> values;
        values.reserve(unitCount);
        for (int i = 0; i < unitCount; ++i) {
            int off = 9 + i * 2;
            values.append((quint16(quint8(frame[off])) << 8) | quint8(frame[off + 1]));
        }
        // 2026-07-08:Qt 6.8 的 QModbusDataUnit(RegisterType,int,const QList<quint16>&) 重载
        // 在 MSVC 下被解析成 (RegisterType,int,int) 重载,QVector→int 编不过
        // 改用 setValues() 显式拆开,稳
        unit.setRegisterType(req->type);
        unit.setStartAddress(0);
        unit.setValues(values);
    } else {
        // 0x01 / 0x02 — Qt 用 QVector<quint16> 表示线圈(0/1)
        unitCount = byteCount * 8;
        QVector<quint16> values;
        values.reserve(unitCount);
        for (int i = 0; i < byteCount; ++i) {
            quint8 b = static_cast<quint8>(frame[9 + i]);
            for (int j = 0; j < 8; ++j) {
                values.append((b >> j) & 0x01);
            }
        }
        // 2026-07-08:同上,改 setValues() 方式
        unit.setRegisterType(req->type);
        unit.setStartAddress(0);
        unit.setValues(values);
    }

    qCDebug(hmiComm) << "[ModbusMaster] 读响应成功 FC=0x" << QString::number(fc, 16).toUpper()
             << "TID 关联 → slave=" << req->serverAddress
             << "count=" << unitCount;

    if (req->reply) {
        req->reply->setResult(unit);
        req->reply->setFinished(true);
        req->reply->deleteLater();
    }
    // 阶段 3.3:reply 成功后通知 PollScheduler(带 requestId 反查)
    emit readFinished(req->requestId, unit, true);
}

// 2026-07-07 新增:写响应拆帧(0x05/0x06/0x0F/0x10 都是 echo)
void ModbusMaster::handleWriteResponse(quint8 fc, Pending* req, const QByteArray& frame) {
    // 2026-07-09:测速 —— 同上
    qCDebug(hmiComm) << "[ModbusMaster] 收到写响应 FC=0x" << QString::number(fc, 16).toUpper()
             << "elapsedMs=" << req->sentAt.msecsTo(QDateTime::currentDateTime());
    if (frame.size() < 12) {
        finalizeErrorReply(req, QString("写响应帧太短:%1 字节").arg(frame.size()));  // ★ IMPROVE-B5
        // 阶段 4.3.0:失败也 emit writeFinished,WriteScheduler 保留 dirty 等下一帧重试
        // ★ 2026-10-27(SOAK-W):写失败计数(这一帧已经上了总线)。
        ++writeStats_.fail;
        emit writeFinished(req->requestId, false);
        return;
    }

    // ★★★ 2026-10-21:回包 echo 地址【真的跟请求比对】。
    //
    //   原来这一行是:
    //       if (echoAddr != quint16(req->reply ? /*addr 通过 reply 拿*/0 : 0)) {
    //   三元的两个分支【都是 0】,占位从来没填上 ——
    //   所以它实际判的是 "echoAddr != 0",**不是**注释说的"与请求地址比对"。
    //   两个方向都错:
    //     · 请求地址非 0、回包 echo 也非 0(= 一切正常)→ 判成"不一致",
    //       于是每一次正常的写都打一条"不一致"(内容是假的);
    //     · 请求地址非 0、回包 echo 恰是 0(= 真的不一致)→ 判成"一致",
    //       真的不一致反而【一声不吭】。
    //   而这行是 qCDebug —— 默认 INFO 级别下一行都不落,现场看不见。
    //
    //   ★ 正确做法:请求地址在【发出去的时候】就存进 Pending(见 Pending::startAddress),
    //     回包时拿它比。QModbusReply 上没有这个信息,回包时是"拿"不到的。
    const quint16 echoAddr = (quint16(quint8(frame[8])) << 8) | quint8(frame[9]);
    if (echoAddr != req->startAddress) {
        // 【只报警,不判失败】—— 沿用原来的取舍:有些从站实现宽松,
        //   把写判成失败会让【本来能用的现场】直接瘫掉。
        //   但必须让现场【看得见】:原来这里是 qCDebug,默认级别下一行都不落,
        //   而"回包地址不是我们要写的那个点"恰恰是必须一眼看到的那类事。
        //   logCommError 是本文件既有的限速告警出口(第 1 次 WARNING,
        //   之后 10 秒内降级到 Debug,每 10 秒补一条汇总),
        //   顺带解决了"从站一直回错地址 → 日志被刷爆"的问题。
        //
        //   ★ 为什么不再多发一条 frameTraced(Error):
        //     这一帧在进到这里【之前】已经作为 Rx 帧记过一次了
        //     (onTcpReadyRead 里分发前的那个 emit)。
        //     再记一遍就是同一帧出现两行,而且统计里的"接收字节数"会多算一遍。
        //     回包的内容监视器里已经看得见(hex 里就有那个地址),
        //     这里补的是"它跟请求对不上"这句话。
        const QString msg = QString("写响应 echo 地址与请求不一致:回包 0x%1,请求 0x%2")
                                .arg(echoAddr, 4, 16, QChar('0'))
                                .arg(req->startAddress, 4, 16, QChar('0'));
        logCommError(msg);
    }

    // ★ 把【请求地址】也打出来 —— 只打 echo 地址的话,
    //   看日志的人无从判断"对不对得上",得回头翻请求那一行。
    qCDebug(hmiComm) << "[ModbusMaster] 写响应成功 FC=0x" << QString::number(fc, 16).toUpper()
             << "echo addr=" << echoAddr
             << "请求 addr=" << req->startAddress;

    if (req->reply) {
        req->reply->setFinished(true);  // 写响应只标记完成,没有 result 数据
        req->reply->deleteLater();
    }
    // 阶段 4.3.0:成功路径 emit writeFinished,WriteScheduler 据此 clearDirty
    // ★ 2026-10-27(SOAK-W):写成功计数。
    ++writeStats_.ok;
    emit writeFinished(req->requestId, true);
}

// ============================================================
// 超时清理
// ============================================================
// 2026-07-06:周期扫 pending_,超时未响应的请求 erase
void ModbusMaster::onTimeoutScan() {
    const QDateTime now = QDateTime::currentDateTime();
    for (auto it = pending_.begin(); it != pending_.end(); ) {
        const Pending& req = it.value();
        if (req.sentAt.msecsTo(now) > req.timeoutMs) {
            qCDebug(hmiComm) << "[ModbusMaster] 请求超时 TID=" << it.key()
                     << "slave=" << req.serverAddress
                     << "elapsedMs=" << req.sentAt.msecsTo(now);
            // ★ 2026-10-21(BUGFIX-C1):超时也要【在默认日志级别下看得见】。
            //   原来这行只有 qCDebug —— 从站不回包时,日志里连一句"超时"都没有,
            //   现场只能靠"界面上的值不跳"来猜。
            logCommError(QString("Modbus 请求超时 TID=%1 slave=%2 等了 %3 ms")
                             .arg(it.key()).arg(req.serverAddress)
                             .arg(req.sentAt.msecsTo(now)));
            emit errorOccurred(QString("Modbus 请求超时 TID=%1").arg(it.key()));
            // ★ 2026-10-20:超时也要在监视器里留一条。
            //   【为什么 raw 是空的】超时【没有】收到任何字节 ——
            //     这里要是编一个假的帧填进去,监视器上就会出现一条
            //     "看起来收到过东西"的记录,比不记更坏。
            emit frameTraced(qwhmi::comm::FrameDir::Error, QByteArray(),
                             QStringLiteral("请求超时 TID=%1(等了 %2 ms)")
                                 .arg(it.key()).arg(req.sentAt.msecsTo(now)),
                             req.sentAt.msecsTo(now));
            if (req.reply) {
                req.reply->setError(QModbusDevice::TimeoutError, "请求超时");
                req.reply->setFinished(true);
                req.reply->deleteLater();
            }
            // ★ 2026-10-21(BUGFIX-C1):失败【必须】通知调度器,否则变量永久卡在途。
            //   原来这里无条件 emit writeFinished(false) —— 对【读】请求来说,
            //   那个信号发给了 WriteScheduler,读调度器永远等不到回包,
            //   inFlight_ 永不清 → 那批变量再也不会被发出去(根因就在这里)。
            //   顺带修掉一个副作用:读请求的 requestId 跟写请求的 requestId
            //   是两套独立计数器,可能撞号;撞上就会把一笔正在进行的写的
            //   dirty 标志误清掉 —— 所以现在按 isWrite 分流,不再乱发。
            if (req.isWrite) {
                // 阶段 4.3.0:写超时 emit writeFinished(success=false),WriteScheduler 保留 dirty
                // ★ 2026-10-27(SOAK-W):写失败计数(这一帧已经上了总线,只是没等到回包)。
                ++writeStats_.fail;
            } else {
                emit readFinished(req.requestId, QModbusDataUnit(), false);
            }
            it = pending_.erase(it);
            notifyIdleIfEmpty();   // 2026-07-09:超时清理也可能让 pending_ 变空
        } else {
            ++it;
        }
    }
}

// ============================================================
// ★ 2026-10-27(SOAK-W):写侧统计行
// ============================================================
//
// 【为什么写侧要有一条像读侧那样的统计行】
//   读侧早就有(PollScheduler 的"轮询统计",INFO 级、60 秒一条、累计计数),
//   长跑 harness 靠它判断"业务还在跑"。
//   写侧【什么都没有】—— 写被拒收、被排队、作废,一个数都没有。
//   ⇒ 后果:BUGFIX-E1 修之前那轮("读 547 次全成功、写 604 次全被拒、
//     0 帧到达从站"),活体判据【照样绿】。写侧那侧压根没有数字可看。
//
// 【★ 为什么是 INFO 级、而不是 DEBUG ★】
//   读侧那条是 INFO,写侧这条也必须是 INFO ——
//   【默认日志级别就是 info】(Logging.cpp 的 defaultLevel),
//   INFO 的行在【什么都不配置的情况下】也落地。
//   ⇒ 长跑【不需要】改 hmi.json 去开日志级别。
//   ★ 而这正是必须去掉那段 hmi.json 的原因:
//     实测开 hmi.comm=debug 之后 893 行/分钟 ⇒ 24 小时 129 万行,
//     而 harness 的 Get-LogSince limit=50000 ⇒ 只看得到前 0.9 小时
//     ⇒ 连 SOAK-6 都被截断。写侧这条行是 INFO,每天只多 1440 行。
//
// 【★ 为什么整行拼成一个字符串 ★】
//   QDebug 在【相邻两项之间插一个空格】:
//     `qCInfo(cat) << "发出=" << count` 出来是 "发出= 271",不是 "发出=271"。
//   2026-10-21 就栽过一次:程序打的是"发出= 271",harness 的正则要"发出=271",
//   启动自检等满 120 秒一条都没匹配上 ⇒ 判成"没资格跑"。
//   ⇒ 整行先拼成 QString,再一次 qCInfo(... << utf8.constData())。
//     (PollScheduler.cpp 那条长注释是同一个教训。)
//
// 【格式契约 —— 改这里必须同时改 scripts/long_run.ps1 的 $reWriteStats】
//   "[ModbusMaster] 写侧统计:实例=1 尝试=26 接受=26 拒收=0 …"
//   tst_WriteStatsFormat 把这份契约钉住,谁改都能立刻看见。
QString ModbusMaster::formatWriteStatsLine(const WritePathStats& s) {
    return QStringLiteral(
        "[ModbusMaster] 写侧统计:实例=%1 尝试=%2 接受=%3 拒收=%4 "
        "拒收未连=%5 拒收非法=%6 拒收队列满=%7 拒收发送=%8 拒收缺陷=%9 "
        "排队=%10 排队发出=%11 排队作废=%12 已发出=%13 成功=%14 失败=%15 "
        "在队=%16 在途写=%17")
        .arg(s.instance)
        .arg(s.attempts)
        .arg(s.accepted)
        .arg(s.rejected)
        .arg(s.rejNotConnected)
        .arg(s.rejInvalid)
        .arg(s.rejQueueFull)
        .arg(s.rejSendFailed)
        .arg(s.rejDefect)
        .arg(s.queued)
        .arg(s.queuedSent)
        .arg(s.queuedVoided)
        .arg(s.sent)
        .arg(s.ok)
        .arg(s.fail)
        .arg(s.inQueue)
        .arg(s.inflightWrites);
}

// ★ 2026-10-27:统计快照 —— 累计计数照抄,两个仪表取【瞬时值】。
ModbusMaster::WritePathStats ModbusMaster::writePathStats() const {
    WritePathStats s = writeStats_;
    s.inQueue        = queuedWrites_.size();
    // 在途的写 = pending_ 里 isWrite 的条数。
    //   为什么单独数:pending_ 里还有读 —— 读占着槽正是"写被挡住"的原因,
    //   而恒等式①关心的是"发了出去没回来的【写】"。
    int inflightWrites = 0;
    for (auto it = pending_.constBegin(); it != pending_.constEnd(); ++it) {
        if (it.value().isWrite) ++inflightWrites;
    }
    s.inflightWrites = inflightWrites;
    return s;
}

// ★ 2026-10-27:每 60 秒打一条写侧统计。
void ModbusMaster::onWriteStatsTick() {
    const QString line = formatWriteStatsLine(writePathStats());
    const QByteArray utf8 = line.toUtf8();
    qCInfo(hmiWrite) << utf8.constData();
}
