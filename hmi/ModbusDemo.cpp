#include "ModbusDemo.h"
#include <QDebug>
#include <QVariant>

ModbusDemo::ModbusDemo(QObject* parent) : QObject(parent) {
    // 创建 Modbus TCP 主站(库内部包了 QModbusTcpClient + 自动组帧/拆帧)
    client_ = new QModbusTcpClient(this);

    // 库自己发的网络状态信号
	connect(client_, &QModbusClient::stateChanged, this, [this](QModbusClient::State s) {// 网络状态变化时,更新 connState_ 和 UI
        qDebug() << "[ModbusDemo] 状态变化:" << s;//
        // Unconnected / Connecting / Connected / Closing
        if (s == QModbusClient::ConnectedState)    emit connected();
        else if (s == QModbusClient::UnconnectedState) emit disconnected();
    });

    // 库自己的错误信号
    connect(client_, &QModbusClient::errorOccurred, this,
        [this](QModbusClient::Error err){
            if (err != QModbusClient::NoError) {
                emit errorOccurred(QString("Modbus 错误:%1").arg(err));
            }
        });
}

void ModbusDemo::connectTo(const QString& host, quint16 port) {
    // 库的配置方式:参数键值对
    // Qt 6 的 API 签名:setConnectionParameter(ConnectionParameter, const QVariant&)
    // QString → QVariant 有隐式转换,但为保险显式包装(Qt 文档示例就这么写)
	client_->setConnectionParameter(QModbusClient::NetworkAddressParameter, QVariant(host)); // 设置主站要连接的从站 IP
	client_->setConnectionParameter(QModbusClient::NetworkPortParameter, QVariant(port));//  设置主站要连接的从站端口
	client_->connectDevice();// 发起连接(异步,成功后会触发 stateChanged 信号)
}

bool ModbusDemo::isConnected() const { // 是否已连接
	return client_->state() == QModbusClient::ConnectedState; // QModbusClient::State 枚举值
}

QModbusReply* ModbusDemo::readHolding(int slaveId, int addr, int count) {
    if (!isConnected()) {
        emit errorOccurred("Modbus 未连接");
        return nullptr;
    }

    // 数据单元(寄存器范围 + 类型)
    QModbusDataUnit unit(QModbusDataUnit::HoldingRegisters,
                         static_cast<quint16>(addr),
                         static_cast<quint16>(count));

    // 发请求,slaveId 走第 2 个参数
    auto* reply = client_->sendReadRequest(unit, slaveId);
    if (!reply) {
        emit errorOccurred("sendReadRequest 失败(可能 slaveId 非法)");
        return nullptr;
    }

    // reply 完成时 → 解析
    connect(reply, &QModbusReply::finished, this, &ModbusDemo::onReplyFinished);

    // ★ 调试用:看原始字节流(MBAP 头 + PDU)
    qDebug() << "[ModbusDemo] 发请求:slaveId=" << slaveId
             << "addr=" << addr << "count=" << count;

    return reply;
}

void ModbusDemo::onReplyFinished() {
    auto* reply = qobject_cast<QModbusReply*>(sender());
    if (!reply) return;

    // ★ 关键:看库收到的原始字节(PDU,不含 MBAP 头)
    // [FIX] Qt 6 的 QModbusResponse(继承自 QModbusPdu)没有 toByteArray(),
    //       也没有 encode()。要拿 PDU 字节,用 data() + dataSize()。
    // [NOTE] 完整 ADU(MBAP 头 + PDU)这里拿不到,要从 TCP 层抓包。
    const QModbusResponse resp = reply->rawResult();
    QByteArray pdu(reinterpret_cast<const char*>(resp.data().data()),
                   resp.data().size());
    qDebug() << "[ModbusDemo] 收到响应(PDU 字节):" << pdu.toHex(' ');
    qDebug() << "[ModbusDemo] 响应 serverAddr=" << reply->serverAddress();

    if (reply->error() != QModbusDevice::NoError) {
        // 异常响应 / 超时 / CRC 错误都会进这
        QString err = QString("Modbus 响应错误:%1,errorString=%2")
                          .arg(reply->error())
                          .arg(reply->errorString());
        emit errorOccurred(err);
        reply->deleteLater();
        return;
    }

    // 成功:提取寄存器值
    const QModbusDataUnit unit = reply->result();
    QVector<quint16> values;
    for (int i = 0; i < unit.valueCount(); ++i) {
        values.append(unit.value(i));
    }

    // 注意:库把 slaveId 放在 serverAddress,起始地址藏在 result 里
    emit holdingReadResult(reply->serverAddress(),
                           unit.startAddress(),
                           values);
    reply->deleteLater();
}