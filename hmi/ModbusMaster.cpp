#include "ModbusMaster.h"
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
            qDebug() << "[ModbusMaster] TCP 错误(转发):" << msg;
            emit errorOccurred(msg);
        });
    connect(tcp_, &TcpClient::readyRead, this, &ModbusMaster::onTcpReadyRead);

    timeoutTimer_ = new QTimer(this);
    // ★ IMPROVE-A8(2026-07-29):50ms → 100ms,跟 PollScheduler/WriteScheduler 对齐(都是 100ms tick)
    //   100ms 精度下超时检测仍 30 tick 才超 3s,完全够
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
// 帧组装 helper
// ============================================================
// 2026-07-07 新增:所有功能码走同一个 frame builder
QByteArray ModbusMaster::buildFrame(quint16 tid, int serverAddress, quint8 fc, const QByteArray& pduBody) {
    // MBAP 头 7 字节:Transaction ID(2) + Protocol(2=0) + Length(2) + Unit ID(1)
    // Length = 后续字节数(UID 1 + PDU fc 1 + body)
    QByteArray frame;
    frame.reserve(7 + pduBody.size() + 1);

    // TID
    frame.append(static_cast<char>((tid >> 8) & 0xFF));
    frame.append(static_cast<char>(tid & 0xFF));
    // Protocol
    frame.append('\x00');
    frame.append('\x00');
    // Length
    quint16 len = 1 + 1 + pduBody.size();  // UID + FC + body
    frame.append(static_cast<char>((len >> 8) & 0xFF));
    frame.append(static_cast<char>(len & 0xFF));
    // Unit ID
    frame.append(static_cast<char>(serverAddress & 0xFF));
    // FC
    frame.append(static_cast<char>(fc));
    // body (addr + data)
    frame.append(pduBody);

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

    // 组 PDU body:addr(2) + count(2)
    QByteArray body;
    int addr = readUnit.startAddress();
    body.append(static_cast<char>((addr >> 8) & 0xFF));
    body.append(static_cast<char>(addr & 0xFF));
    int count = readUnit.valueCount();
    body.append(static_cast<char>((count >> 8) & 0xFF));
    body.append(static_cast<char>(count & 0xFF));

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

    qDebug() << "[ModbusMaster] 已发读 FC=0x" << QString::number(fc, 16).toUpper()
             << "TID=" << tid << "slave=" << serverAddress
             << "addr=" << addr << "count=" << count
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
    QByteArray body;
    int addr = writeUnit.startAddress();
    const auto& values = writeUnit.values();

    bool single = (values.size() == 1);
    fc = typeToWriteFc(writeUnit.registerType(), single);
    if (fc == 0x00) {
        emit errorOccurred(QString("不支持的写 RegisterType"));
        return nullptr;
    }

    if (single) {
        // 写单:body = addr(2) + value(2)
        body.append(static_cast<char>((addr >> 8) & 0xFF));
        body.append(static_cast<char>(addr & 0xFF));
        quint16 v = values.first();
        body.append(static_cast<char>((v >> 8) & 0xFF));
        body.append(static_cast<char>(v & 0xFF));
    } else {
        // 写多:按 fc 分开处理
        // 0x0F 多线圈:body = addr(2) + count(2) + byteCount(1) + bit-packed bytes(LSB first)
        // 0x10 多寄存器:body = addr(2) + count(2) + byteCount(1) + values(N*2 字节,大端)
        // 2026-07-13 阶段 2 第 7 练:之前误用 0x10 格式发 0x0F,ModbusSlave 报 Illegal Data Value
        body.append(static_cast<char>((addr >> 8) & 0xFF));
        body.append(static_cast<char>(addr & 0xFF));
        body.append(static_cast<char>((values.size() >> 8) & 0xFF));
        body.append(static_cast<char>(values.size() & 0xFF));

        if (fc == 0x0F) {
            // 2026-07-13 阶段 2 第 7 练:0x0F 写多线圈 → bit-pack
            // 协议:byteCount = ceil(N/8),每字节 8 个线圈(LSB first),余下 bit 补 0
            int n = values.size();
            int byteCount = (n + 7) / 8;  // 向上取整
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
            body.append(static_cast<char>(values.size() * 2));
            for (quint16 v : values) {
                body.append(static_cast<char>((v >> 8) & 0xFF));
                body.append(static_cast<char>(v & 0xFF));
            }
        }
    }

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

    qDebug() << "[ModbusMaster] 已发写 FC=0x" << QString::number(fc, 16).toUpper()
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
    qDebug() << "[ModbusMaster] 收到读响应 FC=0x" << QString::number(fc, 16).toUpper()
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

    qDebug() << "[ModbusMaster] 读响应成功 FC=0x" << QString::number(fc, 16).toUpper()
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
    qDebug() << "[ModbusMaster] 收到写响应 FC=0x" << QString::number(fc, 16).toUpper()
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
        qDebug() << "[ModbusMaster] 写响应 echo addr 与请求不一致";
    }

    qDebug() << "[ModbusMaster] 写响应成功 FC=0x" << QString::number(fc, 16).toUpper()
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
            qDebug() << "[ModbusMaster] 请求超时 TID=" << it.key()
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