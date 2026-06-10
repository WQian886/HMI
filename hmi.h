//#ifndef HMI_H
//#define HMI_H
//
//#include <QtWidgets/QMainWindow>
//#include "ui_hmi.h"
//
//// MODBUS TCP 客户端
//#include <QtSerialBus/QModbusTcpClient>
//// 用于判断连接状态（确保使用 QtSerialBus 的头文件）
//#include <QtSerialBus/QModbusDevice>
//
//class hmi : public QMainWindow
//{
//    Q_OBJECT
//
//public:
//    explicit hmi(QWidget* parent = nullptr);
//    ~hmi();
//
//private:
//    // UI 界面（由 Qt Designer 自动生成）
//    Ui::hmiClass ui;
//
//    // MODBUS TCP 客户端指针
//    QModbusTcpClient* modbusClient = nullptr;
//
//private slots:
//    // 连接状态变化时调用（PLC 连上或断开）
//    void onStateChanged(QModbusDevice::State state);
//
//    // 通讯错误时调用
//    void onError(QModbusDevice::Error error);
//};
//
//#endif // HMI_H

#ifndef HMI_H
#define HMI_H

#include <QtWidgets/QMainWindow>
#include "ui_hmi.h"
#include <QtSerialBus/QModbusTcpClient>
#include <QModbusDevice>

class hmi : public QMainWindow
{
    Q_OBJECT

public:
    explicit hmi(QWidget* parent = nullptr);
    ~hmi();

private:
    Ui::hmiClass ui;
	QModbusTcpClient* modbusClient = nullptr;// MODBUS TCP 客户端指针

private slots:
    void onStateChanged(QModbusDevice::State state);
};

#endif