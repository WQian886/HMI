//#include "hmi.h"
//#include <QDebug>
//// QModbusReply 和 QModbusDataUnit 用于读取回复并解析寄存器值
//#include <QtSerialBus/QModbusReply>
//#include <QtSerialBus/QModbusDataUnit>
//
//hmi::hmi(QWidget* parent)
//    : QMainWindow(parent)
//{
//    // ===== 初始化 UI（由 Qt Designer 自动生成） =====
//    ui.setupUi(this);
//
//    // ===== 创建 MODBUS TCP 客户端 =====
//    modbusClient = new QModbusTcpClient(this);
//
//    // ===== 连接信号与槽 =====
//    // stateChanged：连接状态变化时触发（连上、断开后都会发）
//    connect(modbusClient, &QModbusTcpClient::stateChanged,
//        this, &hmi::onStateChanged);
//
//    // errorOccurred：通讯出错时触发
//    connect(modbusClient, &QModbusTcpClient::errorOccurred,
//        this, &hmi::onError);
//
//    // ===== 设置 PLC 连接参数 =====
//    // PLC 的 IP 地址，根据实际修改
//    modbusClient->setConnectionParameter(
//        QModbusDevice::NetworkAddressParameter, "192.168.1.100");
//
//    // MODBUS TCP 端口，默认 502
//    modbusClient->setConnectionParameter(
//        QModbusDevice::NetworkPortParameter, 502);
//
//    // ===== 启动连接 =====
//    modbusClient->connectDevice();
//}
//
//hmi::~hmi()
//{
//    // 关闭窗口时自动断开连接
//    if (modbusClient) {
//        modbusClient->disconnectDevice();
//    }
//}
//
//// ===== 连接状态变化回调 =====
//// state = ConnectedState 表示连接成功
//// state = UnconnectedState 表示断开了
//void hmi::onStateChanged(QModbusDevice::State state)
//{
//    if (state == QModbusDevice::ConnectedState) {
//        qDebug() << "MODBUS 连接成功！";
//
//        // ===== 连接成功后读取寄存器 =====
//        if (!modbusClient) {
//            return;
//        }
//
//        // 创建读取请求：读保持寄存器（功能码 03）
//        // 第一个参数：寄存器类型（保持寄存器）
//        // 第二个参数：起始地址（从 0 开始）
//        // 第三个参数：读取数量
//        QModbusDataUnit readUnit(QModbusDataUnit::HoldingRegisters, 0, 1);
//
//        // 发送读取请求，第二个参数 1 是 MODBUS 从机地址（单元ID）
//        QModbusReply *reply = modbusClient->sendReadRequest(readUnit, 1);
//        if (reply) {
//            // 请求完成（收到回复或超时）时触发
//            connect(reply, &QModbusReply::finished, this, [this, reply]() {
//                // 应该检查 reply->error() 而不是 reply->result()
//                if (reply->error() == QModbusDevice::NoError) {
//                    // 从 reply->result() 获取 QModbusDataUnit 并读取寄存器值
//                    QModbusDataUnit unit = reply->result();
//                    if (unit.valueCount() > 0) {
//                        quint16 value = unit.value(0);
//                        qDebug() << "寄存器 0 值:" << value;
//                    }
//                }
//                else {
//                    // 读取失败
//                    qDebug() << "读取错误:" << reply->errorString();
//                }
//                // reply 用完后必须 delete，防止内存泄漏
//                reply->deleteLater();
//            });
//        }
//        else {
//            qDebug() << "发送请求失败:" << modbusClient->errorString();
//        }
//
//    }
//    else if (state == QModbusDevice::UnconnectedState) {
//        qDebug() << "MODBUS 连接断开";
//    }
//}
//
//// ===== 通讯错误回调 =====
//void hmi::onError(QModbusDevice::Error error)
//{
//    // NoError 表示没有错误，直接忽略
//    if (error == QModbusDevice::NoError) {
//        return;
//    }
//    qDebug() << "MODBUS 错误代码:" << error
//        << "错误信息:" << modbusClient->errorString();
//}

#include "hmi.h"
#include <QDebug>

hmi::hmi(QWidget* parent)
    : QMainWindow(parent)
{
    ui.setupUi(this);

    modbusClient = new QModbusTcpClient(this);

    // stateChanged：连接状态变化时触发
    connect(modbusClient, &QModbusTcpClient::stateChanged,
        this, &hmi::onStateChanged);

    // 设置 PLC IP 和端口
    modbusClient->setConnectionParameter(
        QModbusDevice::NetworkAddressParameter, "127.0.0.1");
    modbusClient->setConnectionParameter(
        QModbusDevice::NetworkPortParameter, 502);

    modbusClient->connectDevice();
}

hmi::~hmi()
{
    if (modbusClient) {
        modbusClient->disconnectDevice();
    }
}

void hmi::onStateChanged(QModbusDevice::State state)
{
	// 连接成功后发送读取请求
    if (state == QModbusDevice::ConnectedState) {
        qDebug() << "MODBUS 连接成功！";

        if (!modbusClient) {
            return;
        }
		// 创建读取请求：读保持寄存器（功能码 03）
        QModbusDataUnit readUnit(QModbusDataUnit::HoldingRegisters, 0, 1);
		// 发送读取请求，第二个参数 1 是 MODBUS 从机地址（单元ID）
        if (auto* reply = modbusClient->sendReadRequest(readUnit, 1)) {
           
            connect(reply, &QModbusReply::finished, this, [this, reply]() {
                if (reply->error() == QModbusDevice::NoError) {
                    quint16 value = reply->result().value(0);
                    qDebug() << "寄存器 0 值:" << value;
                }
                else {
                    qDebug() << "读取错误:" << reply->errorString();
                }
                reply->deleteLater();
                });
        }
        else {
            qDebug() << "发送请求失败";
        }

    }
    else if (state == QModbusDevice::UnconnectedState) {
        qDebug() << "MODBUS 连接断开";
    }
}