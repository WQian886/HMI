#include "ModbusMaster.h"
#include "logging/Logging.h"
#include <QtEndian>
#include <QDebug>

// ============================================================
// 阶段 2 第 4 练 — 重设计版
// 作者:Wqian + 子衿
// 时间:2026-07-07
// ============================================================

ModbusMaster::ModbusMaster(QObject* parent) : QObject(parent) {
    tcp_ = new TcpClient(this);

    connect(tcp_, &TcpClient::connected, this, &ModbusMaster::connected);
    connect(tcp_, &TcpClient::disconnected, this, [this]() {
        // 断开时清空 pending(防止半截请求挂着)
        const auto keys = pending_.keys();
        for (quint16 tid : keys) {
            Pending& p = pending_[tid];
            if (p.reply) {
                p.reply->setError(QModbusDevice::ConnectionError, "TCP 断开,请求作废");
                p.reply->setFinished(true);
                p.reply->deleteLater();
            }
        }
        pending_.clear();
        recvBuf_.clear();
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
}

void ModbusMaster::connectTo(const QString& host, quint16 port) {
    recvBuf_.clear();
    const auto keys = pending_.keys();
    for (quint16 tid : keys) {
        Pending& p = pending_[tid];
        if (p.reply) {
            p.reply->setError(QModbusDevice::ConnectionError, "新连接前清空旧请求");
            p.reply->setFinished(true);
            p.reply->deleteLater();
        }
    }
    pending_.clear();
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
    if (!pending_.isEmpty()) {
        emit errorOccurred(QString("上一帧未完成,稍后再发(pending=%1)")
                            .arg(pending_.size()));
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
QModbusReply* ModbusMaster::sendWriteRequest(const QModbusDataUnit& writeUnit, int serverAddress, quint32 requestId) {
    if (!isConnected()) {
        emit errorOccurred("Modbus 未连接");
        return nullptr;
    }
    // 2026-07-09:并发请求限流 —— 同上,写请求也排队(一次只能有 1 个未完成)
    if (!pending_.isEmpty()) {
        emit errorOccurred(QString("上一帧未完成,稍后再发(pending=%1)")
                            .arg(pending_.size()));
        return nullptr;
    }
    if (serverAddress < 0 || serverAddress > 0xFF) {
        emit errorOccurred(QString("serverAddress 非法:%1").arg(serverAddress));
        return nullptr;
    }
    if (writeUnit.startAddress() < 0 || writeUnit.startAddress() > 0xFFFF) {
        emit errorOccurred(QString("addr 非法:%1").arg(writeUnit.startAddress()));
        return nullptr;
    }
    if (writeUnit.values().isEmpty()) {
        emit errorOccurred("写请求 values 不能为空");
        return nullptr;
    }

    // 单写 vs 多写:Coils/Holding 时按 value 数量决定
    quint8 fc;
    int addr = writeUnit.startAddress();
    const auto& values = writeUnit.values();

    bool single = (values.size() == 1);
    fc = typeToWriteFc(writeUnit.registerType(), single);
    if (fc == 0x00) {
        emit errorOccurred(QString("不支持的写 RegisterType"));
        return nullptr;
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
    pending_.insert(tid, p);

    qCDebug(hmiComm) << "[ModbusMaster] 已发写 FC=0x" << QString::number(fc, 16).toUpper()
             << "TID=" << tid << "slave=" << serverAddress
             << "addr=" << addr << "values.size=" << values.size()
             << "frame=" << frame.toHex(' ').toUpper();

    return reply;
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
            recvBuf_.remove(0, frameSize);
            continue;
        }
        Pending req = it.value();

        if (uid != quint8(req.serverAddress)) {
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
            finalizeErrorReply(&req, msg);  // ★ IMPROVE-B5(2026-07-29):5 处重复 helper 替
            pending_.erase(it);
            notifyIdleIfEmpty();   // 2026-07-09:异常响应后可能 pending_ 变空
            recvBuf_.remove(0, frameSize);
            continue;
        }

        // 正常响应 — 按 fc 分发
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
    emit errorOccurred(msg);
    if (req->reply) {
        req->reply->setError(QModbusDevice::ProtocolError, msg);
        req->reply->setFinished(true);
        req->reply->deleteLater();
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
        emit writeFinished(req->requestId, false);
        return;
    }

    quint16 echoAddr = (quint16(quint8(frame[8])) << 8) | quint8(frame[9]);
    if (echoAddr != quint16(req->reply ? /*addr 通过 reply 拿*/0 : 0)) {
        // echo addr 不匹配 — 仅警告(某些从站实现宽松),不报错
        // ★ 2026-10-16:上面那个三元两分支【都是 0】,占位从没填上,
        //   所以这里判的其实是 "echoAddr != 0",不是注释说的"与请求地址比对"。
        //   已知问题,本轮只做日志归类未改逻辑(改它属于协议行为变更,要单独验证)。
        qCDebug(hmiComm) << "[ModbusMaster] 写响应 echo addr 与请求不一致";
    }

    qCDebug(hmiComm) << "[ModbusMaster] 写响应成功 FC=0x" << QString::number(fc, 16).toUpper()
             << "echo addr=" << echoAddr;

    if (req->reply) {
        req->reply->setFinished(true);  // 写响应只标记完成,没有 result 数据
        req->reply->deleteLater();
    }
    // 阶段 4.3.0:成功路径 emit writeFinished,WriteScheduler 据此 clearDirty
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
            emit errorOccurred(QString("Modbus 请求超时 TID=%1").arg(it.key()));
            if (req.reply) {
                req.reply->setError(QModbusDevice::TimeoutError, "请求超时");
                req.reply->setFinished(true);
                req.reply->deleteLater();
            }
            // 阶段 4.3.0:超时也 emit writeFinished(success=false),WriteScheduler 保留 dirty
            emit writeFinished(req.requestId, false);
            it = pending_.erase(it);
            notifyIdleIfEmpty();   // 2026-07-09:超时清理也可能让 pending_ 变空
        } else {
            ++it;
        }
    }
}