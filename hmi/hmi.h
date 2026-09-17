#pragma once

#include <QtWidgets/QMainWindow>
#include <QTimer>  // 阶段 0 第 1 练:加 QTimer 头
//#include <QPair>   // 阶段 2-1 第 2 步:连接建立后待发的读请求
#include "TcpClient.h"  // 阶段 1 第 1 练:TCP 客户端封装
#include "ModbusMaster.h" // 阶段 2 第 3 练:手写 ModbusMaster 接入 UI
namespace qwhmi::drivers { class IPollDriver; }  // ★ C.1(2026-09-17):协议抽象层,modbus_ 改为 IPollDriver*
#include "VariableManager.h"  // 阶段 3.2:把 VariableManager 接入主工程
#include "PollScheduler.h"  // 阶段 3.3:定时轮询调度器
#include "WriteScheduler.h" // 阶段 4.3.0:写调度器(Variable.dirty_ → Modbus 写 PLC)
#include "Binding.h"        // 阶段 4.1:变量↔UI 绑定框架(替 onVariableChanged)
#include "AlarmManager.h"    // 阶段 5.2:报警管理器(集中管理所有 Checker + Alarm 生命周期)
#include "ActionManager.h"   // 阶段 5.6:报警联动动作(关阀/停机)
#include "Project.h"          // ★ 阶段 6.2:JSON 配置存读
#include <QCloseEvent>
#include "ui_hmi.h"

class hmi : public QMainWindow
{
    Q_OBJECT

public:
    hmi(QWidget *parent = nullptr);
    ~hmi();


protected:
    // ★ DIAG-2026-09-09:窗口关闭原因日志(spontaneous=true 表示外部/Qt,=false 表示用户代码)
    void closeEvent(QCloseEvent* e) override;
private slots:
    // 阶段 0 第 2 练:改名 + 改用显式 connect(避开 on_xxx_xxx 自动连接约定)
	void onStartClicked();  // 点击了"连接"按钮 槽函数
	void onStopClicked();   // 点击了"断开"按钮槽函数
	void onResetClicked();// 点击了"重置计数"按钮槽函数
    void onResetFinished();   // 阶段 1 第 2 练:复位 3 秒定时器到,自动转 Stopped

    // 阶段 5.4:报警 UI 槽函数(替换 5.3 的 lambda)
    void onAlarmTriggered(const Alarm& a);
    void onAlarmRecovered(qint64 id);
    void onAlarmBlinkTick();

    // 阶段 5.5:右键菜单 + 确认 + 全清
    void onAlarmContextMenu(const QPoint& pos);
    void onAlarmAcknowledge();
    void onAllAlarmsCleared();

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

	// ★ 阶段 5.6 收尾(2026-08-07):集中 ActionManager mapping 配置(为阶段 6 JSON 铺路)
	void initActionManager(Variable* speed);

	// ★ 阶段 6.2(2026-08-07):UI 保存按钮 → 调 project_->saveToFile
	void onSaveClicked();

	// ★ 阶段 6.4(2026-08-08):UI 打开工程按钮 → 弹 QFileDialog 选 .json → loadFromFile(6.3 已实现)
	void onOpenClicked();



private:
    // 阶段 0x 第 2 练:状态机雏形(业务)
    // [BUG #10] 原代码(已注释):enum class State { Stopped, Running, Reset };
    //           原因:Reset 跟 Qt 内部信号/方法名太像,容易混淆
    // [FIX #10] 改 Resetting(正在复位),语义更清晰
    // ★ IMPROVE-5.1(2026-08-03 Wqian 拍板):加 Fault 态,严重报警触发,只能 ResetButton 清
	enum class State { Stopped, Running, Resetting, Fault };// 业务状态机
    State state = State::Stopped;   // 业务三键的"事实"

	// ★ IMPROVE-5.1(2026-08-03 Wqian 拍板):状态迁移触发源(单一迁移入口 tryTransition 的入参)
	//   【背景】之前 3 个槽函数直接 state = State::xxx; updateUi(); —— 没有"非法迁移"概念
	//   【改法】所有状态变化必须走 tryTransition(Trigger t),查迁移表决定新 state
	//   【取舍】新增 1 个 enum + 1 个方法 + 1 个静态表,但杜绝了"任何态都能转任何态"的混乱
	//   【参照】Wqian 2026-08-03 review #3(阶段 5.1 拍板)
	enum class Trigger { StartButton, StopButton, ResetButton, ResetTimerFinished, AlarmCritical, AlarmCleared };

	// ★ IMPROVE-5.1(2026-08-03 Wqian 拍板):单一状态迁移入口
	//   返回 true=成功迁移(已 updateUi),false=非法迁移(原 state 不变)
	bool tryTransition(Trigger t);

	// ★ IMPROVE-5.1(2026-08-03 Wqian 拍板):状态迁移表(QHash<{from, trigger}> → newState)
	//   5.1 阶段暂未接入报警系统,AlarmCritical 入口等 5.3 报警系统完成后才真实触发
	static const QHash<QPair<State, Trigger>, State> kTransitionTable;

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

    // ★ IMPROVE-B3(2026-07-29):updateUi 拆 5 个 helper,每个负责一个 UI 区域
    //   【背景】原 updateUi 110+ 行,4 块大区(stateLabel / connLabel / 按钮 / modbusLabel)+ 6 处 numberLineEdit 重复
    //   【改法】拆 helper,updateUi() 变 5 行调用
    //   【取舍】helper 间共享 state/connState_ 读取(都是只读,顺序无关)— 安全
    //   【参照】Wqian 2026-07-29 review #2(hmi.cpp #2 updateUi 巨函数)
    void updateStateLabel();       // state → stateLabel + timer 启停 + counter 复位
    void updateConnLabel();        // connState_ → connLabel + 重连进度
    void updateNumberLineEdit();   // counter → numberLineEdit(替原 6 处重复)
    void updateButtons();          // state + connState_ → 业务三键 + 网络按钮 enabled
    void updateModbusLabel();      // lastResultText_ → modbusResultLabel

    // ★ IMPROVE-B4(2026-07-29):magic number 提常量,改超时/提示时长集中一处
    //   【背景】原代码 statusBar(...) 调 13 处,timeout 参数 1500/3000/5000 散落;timer interval 1000/3000 散落
    //          改任何一处超时都要全文搜数字,易漏
    //   【改法】提 5 个 static constexpr,按场景分档
    //   【取舍】常量放 class 内 private static(类作用域,不污染全局)— Qt 习惯
    static constexpr int kStatusBarShortMs  = 1500;  // 短提示:请求中 / 空闲(在 1.5s 内自动消失)
    static constexpr int kStatusBarMediumMs = 3000;  // 中等提示:校验失败 / 输入错误(给操作员看到)
    static constexpr int kStatusBarLongMs   = 5000;  // 长提示:连接等待(失败会弹窗,长时间看得见)
    static constexpr int kCounterTickMs     = 1000;  // counter QTimer 周期(阶段 0 第 1 练)
    static constexpr int kResetIntervalMs   = 3000;  // 复位 QTimer 间隔(到点转 Stopped)

    // 2026-07-07 重设计:Modbus 通用请求入口
	void executeModbus(bool isRead, QModbusDataUnit::RegisterType type, int slaveId, int addr);// 统一入口:Modbus 读/写请求,由 ModbusMaster 调用
	void executePendingModbus();// 阶段 2-4 重设计:连接建立后待发的请求(消除 500ms hack)

	Ui::hmiClass ui;// UI 设计器生成的界面类成员
    QTimer *timer;  // 阶段 0 第 1 练:QTimer 成员变量(1 秒 counter)
    QTimer *resetTimer_ = nullptr;  // 阶段 1 第 2 练:复位定时器(3 秒单次,到点转 Stopped)
    QTimer *alarmBlinkTimer_ = nullptr;  // 阶段 5.4:严重级报警闪烁定时器(500ms 切红/橙)
    QMenu* alarmContextMenu_ = nullptr;  // 阶段 5.5:alarmTable 右键菜单(含"确认"动作)
    int counter = 0;  // 阶段 0 第 1 练:用成员变量存计数(修法 B,数据归程序)
    TcpClient* tcp_ = nullptr;  // 阶段 1 第 1 练:TCP 客户端(生命周期由 this 管)
    // ★ C.1(2026-09-17):从 ModbusMaster* 改为 IPollDriver*
    //   实参:DriverFactory::create("modbus") → ModbusDriver(IPollDriver*)
    //   executeModbus 内 cast 回 ModbusDriver* 拿 inner() 用 Modbus-特定接口(QModbusDataUnit 等)
    //   未来 C.4 协议 UI 切到 S7 时,executeModbus 也要改或被更通用的函数替代
    qwhmi::drivers::IPollDriver* modbus_ = nullptr;
    VariableManager* varMgr_ = nullptr;  // 阶段 3.2:变量管理器(管理所有 Variable 生命周期)
    PollScheduler* pollScheduler_ = nullptr;  // 阶段 3.3:定时轮询调度器(替代 demoTimer,真 modbus 读)
    WriteScheduler* writeScheduler_ = nullptr; // 阶段 4.3.0:写调度器(Variable.dirty_ → modbus 写 PLC,与 PollScheduler 对称)
    AlarmManager* alarmMgr_ = nullptr;  // 阶段 5.2:报警管理器(5.3 才注册 Checker 接 Variable)
    ActionManager* actionMgr_ = nullptr;  // 阶段 5.6:报警联动动作(关阀/停机)
    Project*       project_    = nullptr;  // ★ 阶段 6.2:JSON 配置存读入口
};
