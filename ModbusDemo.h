#pragma once
#include <QObject>
#include <QModbusTcpClient>   // Qt 自带的 Modbus TCP 主站
#include <QModbusReply>
#include <QModbusDataUnit>

// 阶段 2-1 第 1 步:用 Qt 自带 QModbusClient 跑通 modbus,理解库做了什么
// 下一阶段自己手写 ModbusMaster 替代这个类
class ModbusDemo : public QObject {
    Q_OBJECT
public:
    explicit ModbusDemo(QObject* parent = nullptr);

    // 连接到 Modbus Slave(TCP)
    void connectTo(const QString& host, quint16 port);

    // 读保持寄存器(功能码 0x03)
    // slaveId:从机号(1-247),addr:起始地址,count:寄存器数
    // 返回 reply 指针(可观察它的 rawResponse() 看到原始字节流)
    QModbusReply* readHolding(int slaveId, int addr, int count);

    bool isConnected() const;

signals:
    // 读保持寄存器的结果(values 跟请求一一对应)
    void holdingReadResult(int slaveId, int addr, QVector<quint16> values);

    // 出错时发(异常响应 / 超时 / 网络错误)
    void errorOccurred(QString msg);

    // 网络状态变化(连接/断开)
    void connected();
    void disconnected();

private slots:
    // Modbus reply 完成(成功或失败都进这里)
    void onReplyFinished();

private:
    QModbusTcpClient* client_ = nullptr;
};