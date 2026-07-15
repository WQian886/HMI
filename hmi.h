#pragma once

#include <QtWidgets/QMainWindow>
#include <QTimer>  // 阶段 0 第 1 练:加 QTimer 头
#include <QPair>   // 阶段 2-1 第 2 步:连接建立后待发的读请求
#include "TcpClient.h"  // 阶段 1 第 1 练:TCP 客户端封装
#include "ModbusMaster.h" // 阶段 2 第 3 练:手写 ModbusMaster 接入 UI
#include "ui_hmi.h"

class hmi : public QMainWindow
{
    Q_OBJECT

public:
    hmi(QWidget *parent = nullptr);
    ~hmi();

private slots:
    // 阶段 0 第 2 练:改名 + 改用显式 connect(避开 on_xxx_xxx 自动连接约定)
	void onStartClicked();  // 点击了"连接"按钮 槽函数
	void onStopClicked();   // 点击了"断开"按钮槽函数
	void onResetClicked();// 点击了"重置计数"按钮槽函数
    void onResetFinished();   // 阶段 1 第 2 练:复位 3 秒定时器到,自动转 Stopped

    // 阶段 0 第 1 练:手写 connect 的 QTimer 槽函数
	void onTimerTick();// 定时器 tick 了

    // 阶段 1 第 1 练:TCP 相关槽
	void onConnectClicked();// 点击了"连接"按钮槽函数
	void onDisconnectClicked();// 点击了"断开"按钮槽函数
	void onSendClicked();// 点击了"发送"按钮槽函数
	void onTcpConnected();// TCP 连接成功槽函数
    void onTcpDisconnected();   // TCP 断开连接槽函数
	void onTcpReadyRead(const QByteArray& data);// TCP 收到数据了槽函数
	void onTcpError(const QString& msg);// TCP 错误槽函数
	void appendRecv(const QByteArray& data, const QString& prefix);// 辅助函数:把收到的数据追加到文本框,加个前缀区分收/发

private:
    // 阶段 0 第 2 练:状态机雏形(业务)
    // [BUG #10] 原代码(已注释):enum class State { Stopped, Running, Reset };
    //           原因:Reset 跟 Qt 内部信号/方法名太像,容易混淆
    // [FIX #10] 改 Resetting(正在复位),语义更清晰
	enum class State { Stopped, Running, Resetting };// 业务状态机
    State state = State::Stopped;   // 业务三键的"事实"

    // 阶段 1 第 2 练:网络状态(跟业务 state 平级,共同决定 UI)
	enum class ConnState { Disconnected, Connecting, Connected, Failed, Reconnecting };// 连接状态机
    ConnState connState_ = ConnState::Disconnected;   // 网络的"事实"

    // 阶段 1 第 2 练:重连进度(给 UI 显示"重连中(2/5),3 秒后重试")
    int reconnectAttempt_ = 0;   // 当前第几次
    int reconnectMax_     = 5;   // 最大次数
    int reconnectDelayMs_ = 0;   // 距离下次重连的 ms

    // 阶段 2-1 第 2 步:Modbus 状态机(跟 connState_ 平级,共同决定 UI)
    enum class ModbusState { Disconnected, Connecting, Connected, Failed };
    ModbusState modbusState_ = ModbusState::Disconnected;

    // 阶段 2-4 重设计:连接建立后待发的请求(消除 500ms hack)
    struct PendingModbusRequest {
        bool isRead;
        QModbusDataUnit::RegisterType type;
        int addr;
        QVector<quint16> values;  // 写时用
        int count;                // 读时用
    };
    PendingModbusRequest pendingModbus_;
    bool hasPendingModbus_ = false;

    // 阶段 2-4 重设计:最近一次结果(由 updateUi() 推到 resultLabel)
    QString lastResultText_ = "--";
    QDateTime lastResultTime_;

    void updateUi();   // state + connState_ + modbusState_ → UI 同步函数(唯一的 UI 渲染入口)

    // 2026-07-07 重设计:Modbus 通用请求入口
    void executeModbus(bool isRead, QModbusDataUnit::RegisterType type, int slaveId, int addr);
    void executePendingModbus();

	Ui::hmiClass ui;// UI 设计器生成的界面类成员
    QTimer *timer;  // 阶段 0 第 1 练:QTimer 成员变量(1 秒 counter)
    QTimer *resetTimer_ = nullptr;  // 阶段 1 第 2 练:复位定时器(3 秒单次,到点转 Stopped)
    int counter = 0;  // 阶段 0 第 1 练:用成员变量存计数(修法 B,数据归程序)
    TcpClient* tcp_ = nullptr;  // 阶段 1 第 1 练:TCP 客户端(生命周期由 this 管)
    ModbusMaster* modbus_ = nullptr;  // 阶段 2 第 3 练:Modbus 客户端(手写版)
};
