#pragma once

#include <QtWidgets/QMainWindow>
#include <QTimer>  // 阶段 0 第 1 练:加 QTimer 头
//#include <QPair>   // 阶段 2-1 第 2 步:连接建立后待发的读请求
#include "TcpClient.h"  // 阶段 1 第 1 练:TCP 客户端封装
#include "ModbusMaster.h" // 阶段 2 第 3 练:手写 ModbusMaster 接入 UI
namespace qwhmi::drivers { class IPollDriver; }  // ★ C.1(2026-09-17):协议抽象层,modbus_ 改为 IPollDriver*
// ★ 2b(2026-10-19):PlcConnectionManager 在【全局命名空间】,不在 qwhmi::plc 下
//   (它在 src/plc/ 目录下,但阶段 2 定它时就只前向声明了 IPollDriver,
//    类本身没套命名空间)。前向声明必须跟真实定义所在的命名空间一致 ——
//   写错的话编译器报的是"未定义类型",而不是"找不到成员",查起来绕。
class PlcConnectionManager;
namespace qwhmi::history { class HistoryRecorder; }  // ★ D.1(2026-09-18):SQLite 历史库
namespace qwhmi::history { class AlarmRecorder; }   // ★ D.3(2026-09-20):报警持久化
namespace qwhmi::widgets { class TrendWidget; }  // ★ D.2(2026-09-20):历史曲线控件
#include "VariableManager.h"  // 阶段 3.2:把 VariableManager 接入主工程
#include "PollScheduler.h"  // 阶段 3.3:定时轮询调度器
#include "WriteScheduler.h" // 阶段 4.3.0:写调度器(Variable.dirty_ → Modbus 写 PLC)
#include "Binding.h"        // 阶段 4.1:变量↔UI 绑定框架(替 onVariableChanged)
#include "AlarmManager.h"    // 阶段 5.2:报警管理器(集中管理所有 Checker + Alarm 生命周期)
#include "ActionManager.h"   // 阶段 5.6:报警联动动作(关阀/停机)
#include "Project.h"          // ★ 阶段 6.2:JSON 配置存读
#include <QCloseEvent>
#include "ui_hmi.h"
// ★ E.4a(2026-09-21):编辑器按钮入口
#include "editor/EditorMainWindow.h"
#include "editor/RuntimeCanvas.h"   // ★ E.5(2026-09-29):运行时画布

// ★ QMenu 一直是靠 QtWidgets 传递包含进来的,这里把两个都显式前向声明 ——
//   2026-10-14 新增 QAction 成员时才发现:不能假设"QMenu 可见则 QAction 也可见"。
class QAction;
class QMenu;

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
    // ★ 2026-10-14:报警表配色的唯一入口。
    //   主画面过去自己一套配色(而且静止态根本没有四档色),
    //   与运行时画布的 AlarmListControl 是两个权威,已经漂开了。
    //   现在两边都调 src/alarm/AlarmRowPalette。
    void repaintAlarmRow(int row);

    // ★★★ 2026-10-20:下面这 10 个槽原来【声明在 private: 段里】,
    //   而 private: 不是 private slots: —— moc 不会为普通私有函数生成元方法。
    //   于是:
    //     · 主画面上的按钮没事 —— hmi.cpp 用的是【成员函数指针】connect
    //       (connect(btn, &QPushButton::clicked, this, &hmi::onEditorClicked)),
    //       编译期就解析了,根本不需要 moc。
    //     · 菜单栏【点了没反应】—— MenuBuilder 用的是【字符串】connect
    //       (SLOT(onEditorClicked())),它靠 moc 生成的元方法表查找;
    //       找不到就【静默返回 false】,Qt 只往 stderr 打一行 "No such slot",
    //       编译期一个字都不报。
    //   ★ 同一个槽、两种 connect 写法,只有一种能工作 ——
    //     这就是"按钮好好的、菜单却是哑的"的原因。
    //   ★ 顺带解释了为什么 tst_MenuBar 一直是绿的:它测的是 buildApplicationMenu
    //     和一个【假接收者】,假接收者由测试自己声明在 public slots: 里,
    //     所以那条路永远是通的,发现不了真 hmi 类的槽其实没被 moc 收录。
    //
    //   【为什么不干脆把菜单也改成成员函数指针 connect】
    //     做不到 —— MenuBuilder 是独立编译单元,它不认识 hmi 类,
    //     而 hmi 依赖 uic 生成的 ui_*.h,测试链的 hmi_core 里根本没有 ui_*.h,
    //     连编译都过不了。抽 MenuBuilder 的全部目的就是能被测试单独调用。
    //     所以只能让 moc 这条路通。
    //
    //   【以后加槽的规矩】凡是要给菜单用的,必须声明在【带 slots 关键字的段】里。
    //   只接按钮、不进菜单的,放哪都行。

    // ★ E.1a(2026-09-20):演示画布按钮触发(6 控件实时数据 demo)
    void onDemoCanvasClicked();
    // ★ E.4a:弹/隐藏 EditorMainWindow(调色板 + 画布 + 属性面板 + JSON 存读)
    void onEditorClicked();
    // ★ E.5(2026-09-29):弹/隐藏 RuntimeCanvas(把编辑器存的工程文件显示出来)
    //   这是"编辑器画的画面怎么在主画面里显示"的答案。
    void onRuntimeCanvasClicked();
    // ★ F4-a(2026-10-08):弹 AlarmConfigDialog(报警阈值/等级/滞回/屏蔽)
    //   F2 滞回和 F2-c 屏蔽配完一直没地方点,只能手改 JSON ——
    //   这个按钮是它们对真实用户存在的唯一途径。
    void onAlarmConfigClicked();
    void onAlarmSoundSettings();   // ★ F5:报警声音设置对话框
    // ★ 2b 阶段6(2026-10-19):弹 PlcConfigDialog(N 台 PLC 的地址/协议/从站号)
    //   2a/2b 把多 PLC 的存读和运行期连接都做完了,但 plcs[] 只能手改 JSON ——
    //   host 写错要停机改文件、slave_id 填 0(广播地址)没有任何人拦。
    //   改现场设备的配置却只存在于 JSON 里,等于这个功能对真实用户不存在。
    void onPlcConfigClicked();
    // ★ 2026-10-19:弹 VariableConfigDialog(变量归属哪台 PLC / 轮询 / 死区 / 常轮询)
    //   变量至今没有 UI 入口,plcId 只能手改 JSON 或导 CSV ——
    //   多 PLC 配了 3 台,却没有任何正常途径把变量分配给它们。
    void onVariableConfigClicked();
    void onExportVariablesClicked();
    void onExportAlarmsClicked();
    // 帮助 → "当前支持的功能":把程序的能力边界直接写在界面上。
    //   【为什么不是个装饰性的"关于"】用户在菜单里点了个没反应的项,
    //   最该立刻知道的是"这功能到底有没有"。藏起来只会被反复试。
    void onShowCapabilities();
    // ★ 改某一行的状态列(文字 + UserRole + 重新上色)。三样必须一起改,
    //   只改文字就会出现"文字说已确认、颜色说未确认"——那一行既灰着又在闪。
    void setAlarmRowState(int row, AlarmState st);
    // ★ 有该闪的行才亮表,一个都没有就停,【并且把相位复位】。
    void updateAlarmBlinkTimer();

    // 阶段 5.5:右键菜单 + 确认 + 全清
    void onAlarmContextMenu(const QPoint& pos);
    void onAlarmAcknowledge();
    // ★ 2026-10-14:一次确认全部未确认报警。
    //   AlarmManager::acknowledgeAll() 早就写好了、也返回真实条数(F3 改的),
    //   但全项目【没有任何入口调它】—— 建好了没接线。
    //   而现场真出状况时往往是同时来七八条,操作员只能一条条双击/右键。
    void onAlarmAcknowledgeAll();
    void onAllAlarmsCleared();
    // ★ 2026-10-14:当前未确认条数(按 AlarmManager 的 state,不按表格行数)
    int  alarmUnacknowledgedCount() const;

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

    // ★ 阶段 C · C.4(2026-09-18):协议选择下拉框槽函数
    //   跨协议切工程 → 弹错误对话框 + AuditLog(教训 1.5)
    void onProtocolComboChanged(const QString& protocolName);



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

    // ★ D.4.1(2026-09-20):D.4 CSV 导出 UI 集成
    //   QFileDialog 选路径 → 调 qwhmi::export_::writeVariablesCsv / writeAlarmsCsv
    //   失败用 QMessageBox 提示(便于操作员看到)
    //   【这两个槽的声明已移到 private slots: 段】—— 它们原先在这里(普通 private:),
    //   moc 不收录,菜单栏点"导出变量表/导出报警表"全是哑的。
    //   实现体没动,仍是 QFileDialog 选路径 → 调 writeVariablesCsv/writeAlarmsCsv。

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
    QTimer *alarmBlinkTimer_ = nullptr;  // 阶段 5.4:严重级报警闪烁定时器(500ms 切亮/暗两相)
    // ★ 2026-10-14:闪烁相位。
    //   原来写在 onAlarmBlinkTick 里是个【函数内 static 变量】——
    //   全体 hmi 实例共用一个,而且定时器停下来时【不复位】。
    //   于是报警全清之后再来第一条,第一帧可能还停在亮相的浅色上,
    //   用户看到的是"新报警颜色不对,过 500ms 才对"。
    //   AlarmListControl 在 F3-b 已经把这个 bug 修掉了,主画面还留着。
    bool alarmBlinkOn_ = false;
    QMenu* alarmContextMenu_ = nullptr;  // 阶段 5.5:alarmTable 右键菜单(含"确认"动作)
    // ★ 2026-10-14:两个动作各自持有,才能分别按当前行/全局状态置灰。
    //   原来用 actions().first() 取"确认"—— 加了第二个动作之后那个下标就指错人了,
    //   而症状是"菜单项该灰的没灰、不该灰的灰着",不崩也不报错。
    QAction* alarmAckAction_     = nullptr;
    QAction* alarmAckAllAction_  = nullptr;
    int counter = 0;  // 阶段 0 第 1 练:用成员变量存计数(修法 B,数据归程序)
    TcpClient* tcp_ = nullptr;  // 阶段 1 第 1 练:TCP 客户端(生命周期由 this 管)
    // ★ C.1(2026-09-17):从 ModbusMaster* 改为 IPollDriver*
    //   实参:DriverFactory::create("modbus") → ModbusDriver(IPollDriver*)
    //   executeModbus 内 cast 回 ModbusDriver* 拿 inner() 用 Modbus-特定接口(QModbusDataUnit 等)
    //   未来 C.4 协议 UI 切到 S7 时,executeModbus 也要改或被更通用的函数替代
    // ★ 2b(2026-10-19):【不再】自己 new 一个 driver 握着。
    //   原来这里是 DriverFactory::create("modbus", this) 建出来的唯一一台,
    //   PollScheduler 和 WriteScheduler 都指着它 —— 于是工程里配几台都只连一台。
    //   现在 driver 的所有权 100% 归 plcMgr_,这里只留一个【非拥有】指针
    //   指向 plcMgr_->driverFor("plc1"),专供手工调试面板用
    //   (面板要 ModbusMaster::inner() 那种协议特有接口,不改造成 N 路 ——
    //    开发工具,零测试覆盖,扩 N 路只会增加接错线的面)。
    //   ★ 它可能是 nullptr:plc1 还没配 / 被删了 / 正在重连。
    //     每个用到它的地方都必须判空,不能"反正有值"地解引用。
    qwhmi::drivers::IPollDriver* modbus_ = nullptr;

    // ★ 2b:工程里 N 台 PLC 的连接持有者,同时是 PollScheduler/WriteScheduler 的 provider。
    //   【构造顺序 —— 改的人请先读这一段】
    //     plcMgr_ 必须【早于】pollScheduler_ / writeScheduler_ / project_ 构造。
    //     理由不是"谁先 new 谁后 new",而是两条独立的:
    //     ① 析构:QObject 子对象按【构造顺序的逆序】析构,plcMgr_ 最后死,
    //        那些 driver 要活到所有用户方都拆完之后才 deleteLater(阶段 5 用 deleteLater,
    //        不 delete,所以实际更宽松,但顺序别去赌它)。
    //     ② [this] 捕获:下面那些 lambda 全靠 this 找 plcMgr_,而它们是
    //        project_->setPlcApplier(...) 装的 —— lambda 在 project_ 加载文件时
    //        才被调用,那时 plcMgr_ 必须已经存在。晚构造一步 = 打开工程直接崩。
    PlcConnectionManager* plcMgr_ = nullptr;

    VariableManager* varMgr_ = nullptr;  // 阶段 3.2:变量管理器(管理所有 Variable 生命周期)
    PollScheduler* pollScheduler_ = nullptr;  // 阶段 3.3:定时轮询调度器(替代 demoTimer,真 modbus 读)
    WriteScheduler* writeScheduler_ = nullptr; // 阶段 4.3.0:写调度器(Variable.dirty_ → modbus 写 PLC,与 PollScheduler 对称)
    AlarmManager* alarmMgr_ = nullptr;  // 阶段 5.2:报警管理器(5.3 才注册 Checker 接 Variable)
    // ★ F5(2026-10-08):报警声音/蜂鸣。独立于 AlarmManager —— 它管的是
    //   "要不要响",而 AlarmManager 只管"发生了"。混在一起就没法按班次配了。
    class AlarmSoundPlayer* alarmSound_ = nullptr;
    ActionManager* actionMgr_ = nullptr;  // 阶段 5.6:报警联动动作(关阀/停机)
    Project*       project_    = nullptr;  // ★ 阶段 6.2:JSON 配置存读入口

    // ★ 阶段 D · D.1(2026-09-18):SQLite 历史库记录器
    qwhmi::history::HistoryRecorder* historyRecorder_ = nullptr;

    // ★ 阶段 D · D.2(2026-09-20):历史曲线控件(QCustomPlot)— 接 HistoryRecorder + VariableManager
    qwhmi::widgets::TrendWidget* trendWidget_ = nullptr;

    // ★ 阶段 D · D.3(2026-09-20):报警持久化(同 history.db 的 alarm_history 表 + 接 AlarmManager)
    qwhmi::history::AlarmRecorder* alarmRecorder_ = nullptr;

    // ★★ 2026-10-19:这三个原来是【函数内 static】,提成成员是为了让
    //   变量配置对话框能枚举到"画面上有哪些控件绑着哪个变量"。
    //   函数内 static 的话这个类没有任何成员指得过去,那层引用就永远查不到,
    //   只能靠一句免责说明顶着。改的是【可见性】不是【生命周期】——
    //   三者本来就是进程内单例、都靠 WA_DeleteOnClose=false 不随关窗口销毁。
    //   【为什么不顺手把 demoWindow_ 也一起改】它也是同一类问题,一起改了;
    //   留着它反而不一致 —— "另外两个改了它没改"比"三个都还是 static"更难读。
    qwhmi::editor::EditorMainWindow* editorWindow_ = nullptr;
    qwhmi::editor::RuntimeCanvas*     canvas_       = nullptr;
    QWidget*                          demoWindow_   = nullptr;

    // ★★ 2026-10-16:菜单栏
    //   buildMenus() 在构造函数里调一次,全部 connect 到上面那些【已有槽】,
    //   没有一行新逻辑 —— 菜单和按钮必须指向同一个实现,否则两边行为会分叉。
    void buildMenus();

    // ★★ 2026-10-20(P1-e):调试区的展开/收起开关。
    //
    //   【P1-e 改了什么 —— 这里曾经有一个真 bug】
    //     P1-c 的做法是:新建 QWidget + QVBoxLayout,用 centralLayout->takeAt()
    //     把 tcpSendLayout / modbusLayout 两个 QLayoutItem 摘出来,addItem() 过去,
    //     再 insertWidget() 把新面板塞回去。
    //     ★ 摘出来的那两个布局【不会自动重新激活】—— 它们保留了原来在
    //       centralLayout 里的几何,运行时照旧画在老位置上。
    //       实测:面板一展开,Modbus 那一行就压在【启动/停止/复位】那一行上面,
    //       而面板自己的槽位是一块全黑空白。
    //       这不是"看着不齐",是两层控件画在同一处。
    //     所以 P1-e 把两页直接写进 .ui(debugTabs),这里不再搬任何布局。
    //
    //   【这个函数现在只做一件事】
    //     在 debugTabs 前面插一个开关按钮,点它显示/隐藏 debugTabs。
    //     插一个【新建的】控件进布局是正常操作 —— 危险的是"摘走已有的 item",
    //     那才会留下陈旧几何。两者的区别就是这次 bug 的全部成因。
    //
    //   【为什么这两行值得折】它们是【协议调试工具】(手打 hex 帧、手填从站号
    //     地址数量),现场操作员一天也不碰一次;而它们常年占着主画面一整行。
    //     折起来之后主画面只剩"连上 → 看数据 → 启停",这才是操作员要的那三步。
    //     默认折叠 —— 调试工具默认就该是收着的,不是默认摊在生产界面上。
    //
    //   【连接那一行为什么不折】protocolComboBox / ipEdit / portEdit /
    //     connectButton / disconnectButton 是【主操作路径】,不是调试工具。
    //     把它一起折起来 = 让人为了看现场先展开面板,那是把调试工具的地位
    //     反过来了。所以分界线画在"主操作"和"协议细节"之间,不在"上面"和"下面"之间。
    void buildDebugPanel();

    // ★ 阶段 C · C.4(2026-09-18):当前工程协议(modbus/s7/eip),初始 modbus
    //   跨协议切工程时此值 vs protocolComboBox.currentText() 比对(教训 1.5)
    QString currentProtocol_ = QStringLiteral("modbus");

    // ★ 把下拉框撤回"当前协议"那一项。
    //   【为什么需要它】原来内联在 onProtocolComboChanged 里,写成
    //   findText(current, Qt::MatchFixedString),而 current 存的是协议 ID
    //   ("modbus"),下拉框里的显示文字是 "Modbus" ——
    //   MatchFixedString 是【大小写敏感】的,findText 返回 -1,
    //   撤回那几行在 if(idx >= 0) 里,于是【静默不执行】。
    //   结果:用户选了别的协议被拒绝后,下拉框停在错的那一项上,
    //   再点一下连接,连的是下拉框显示的那个协议 —— 而 currentProtocol_ 还是旧的。
    //   这个 bug 一直没显形,是因为 create() 硬编码 "modbus",
    //   弹窗虽然错了,实际连的始终是 Modbus,看不出不一致。
    //   v0.3.0 真接上 S7/EIP 就会立刻变成"界面说 A、实际连 B"。
    void revertProtocolCombo();
};
