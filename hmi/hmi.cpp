#include "hmi.h"
#include "Variable.h"             // 阶段 3.2:Variable 数据模型
#include "VariableManager.h"      // 阶段 3.2:Variable 容器
#include "PollScheduler.h"        // 阶段 3.3:定时轮询调度器
#include "drivers/IPollDriver.h"   // ★ C.1(2026-09-17):协议抽象层
#include "drivers/IPlcDriverProvider.h" // ★ 2b(2026-10-19):WriteScheduler 按 plcId 问它要 driver
#include "plc/PlcConnectionManager.h"  // ★ 2b:工程里 N 台 PLC 的连接持有者 + provider 实现
#include "drivers/ModbusDriver.h"  // ★ C.1:Modbus 协议 Driver
#include "drivers/DriverFactory.h" // ★ C.1:driver 工厂
#include "drivers/ProtocolGuard.h" // ★ C.4(2026-09-18):跨协议 UX 锁纯逻辑(可单测)
#include "drivers/ProtocolAvailabilityUi.h" // ★ 2026-10-10:协议下拉框"未实现"标注
#include "history/HistoryRecorder.h" // ★ D.1(2026-09-18):SQLite 历史库
#include "history/AlarmRecorder.h"  // ★ D.3(2026-09-20):报警持久化(alarm_history 表)
#include "history/AlarmHistoryView.h" // ★ F4(2026-10-08):报警历史查询视图(填 D.3 占位 tab)
#include "alarm/AlarmRowPalette.h"   // ★ 2026-10-14:报警表配色的唯一权威(与运行时画布共用)
#include "widgets/TrendWidget.h"     // ★ D.2(2026-09-20):历史曲线控件(QCustomPlot)
#include "widgets/LabelControl.h"    // ★ E.1a(2026-09-20):6 显示控件
#include "widgets/NumberDisplay.h"
#include "widgets/TextDisplay.h"
#include "widgets/BarControl.h"
#include "widgets/GaugeControl.h"
#include "widgets/LedControl.h"
// ★ E.1b-2(2026-09-21):3 个数据控件(Trend/Table/AlarmList)— 演示画布用到
#include "widgets/TrendControl.h"
#include "widgets/TableControl.h"
#include "widgets/AlarmListControl.h"
#include "export/CsvExporter.h"      // ★ D.4(2026-09-20):CSV 导出(QTextStream)

#include <QGraphicsView>            // ★ E.1a demo 画布
#include <QGraphicsScene>
#include <QGraphicsSimpleTextItem>
#include "ThresholdAlarmChecker.h" // 阶段 5.3-收尾(2026-08-07):合并 High/Low + 最小持续时间(DRY)
#include "WriteFailChecker.h"     // 阶段 4.3.x:写失败转报警(2026-08-05 Bug #2 修复)
#include "ActionManager.h"        // 阶段 5.6:报警联动动作(关阀/停机)
#include "alarm/AlarmConfigDialog.h"  // ★ F4-a(2026-10-08):报警配置对话框(报警配置按钮)
#include "alarm/AlarmSoundDialog.h"   // ★ F5(2026-10-08):报警声音设置对话框
#include "plc/PlcConfigDialog.h"      // ★ 2b 阶段6:N 台 PLC 的配置界面
#include "plc/VariableConfigDialog.h" // ★ 2026-10-19:变量配置界面(变量 plcId 的 UI 入口)
#include "AlarmSoundPlayer.h"         // ★ F5(2026-10-08):报警声音/蜂鸣(报警声音按钮)
#include <QIntValidator>
#include <QTimer>                 // counter / resetTimer_ / alarmBlinkTimer_ 共用
#include <QMessageBox>
#include <QMessageBox>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QTableWidget>     // 阶段 4.x 写审计日志
#include <QTableWidgetItem> // 阶段 4.x 写审计日志
#include <QCloseEvent>
#include "logging/Logging.h"
#include "auth/UIPermission.h"  // ★ B4(2026-09-17):画面级权限(按钮 enabled 跟 Permission 挂钩)
#include "auth/CurrentUser.h"   // ★ B4 修复(2026-09-17):updateButtons 需要查 role
#include "auth/Permission.h"    // ★ B4 修复(2026-09-17):updateButtons 需要查 WriteVariable
#include "auth/AuditLog.h"      // ★ C.4.2(2026-09-18):protocol.change 审计事件
#include "ui/MenuBuilder.h"
#include "comm/CommTraceModel.h"     // ★ 2026-10-20:通讯帧监视(调试区第三页)     // ★ 2026-10-16:菜单栏搭建(抽出来才能被 tst_MenuBar 测到真实那份)
#include <QColor>            // 阶段 5.4:严重级报警闪烁(红/橙背景色)
#include <QFile>             // ★ 阶段 6.3 DEBUG:写日志文件(load_debug.log)
#include <QFileDialog>       // ★ 阶段 6.4:UI 打开工程 → 选 .json 文件
#include <QStandardPaths>    // ★ D.4.1:CSV 导出默认路径(桌面)
#include <QMenu>             // 阶段 5.5:alarmTable 右键菜单
#include <QMenuBar>          // ★ 2026-10-16:菜单栏(hmi.ui:400 那个一直是空的)
#include <QKeySequence>      // ★ 2026-10-16:菜单项快捷键(Ctrl+O / Ctrl+S)
#include <QHeaderView>       // ★ 2026-10-10:两张表的列宽策略(报警名/Variable 吃余量)
#include <QToolButton>       // ★ P1-c(2026-10-20):调试面板的展开/折叠开关

// ★ 2026-10-10:两张表里"宽度最不确定"的那一列的下标。
//   写成常量而不是裸 1 —— 报警名的宽度【取决于用户自己起什么名】,
//   是唯一有理由吃余量的列;其余列一律按内容收紧。
//   跟 AlarmHistoryView.cpp / AlarmConfigDialog.cpp 里的 kColName 是同一个理由。
namespace {
constexpr int kColAlarmName = 1;   // 报警表:时间/报警名/等级/值/状态
constexpr int kColVarName   = 1;   // 审计表:时间/Variable/新值/结果/地址

// ★★★ 2026-10-20:状态徽章配色 —— 状态色原来散在两个函数里各写一遍字符串。
//
//   【为什么要抽出来】
//   深色主题下,原来那些值一个都不能用:
//     "lightgray" + 黑字 → 深底上一个浅灰块,晃眼且字看不清
//     "yellow"   + 黑字 → 刺眼,车间强光下更难读
//   换主题就得换色,而色值散在 updateStateLabel / updateConnLabel 两处、
//   九个 switch 分支里,改一遍漏一个都不会报任何错。
//   抽成一处之后,"状态一共有哪几档、每档什么色"变成一张能读完的表。
//
//   【★ 为什么不用 qss 里的颜色】qss 管不到它们 ——
//   状态色是按【运行期状态】变的,必须逐次 setStyleSheet;
//   而一旦在控件上写了 inline 样式表,qss 对这个控件就【完全失效】。
//   所以状态色只能在这里写,不能"顺便交给主题文件管"。
//
//   【配色约束 —— 不是"看着顺眼"挑的】
//     kStatusOk    #3fb950 绿  运行/已连接:一切正常
//     kStatusIdle  #7d8b9c 中灰 已停止:深底上用【灰】而不是浅灰,
//                            浅灰块在深色界面里比绿色还抢眼
//     kStatusBusy  #d29922 琥珀 连接中/复位中/重连中:在动,但还没定
//     kStatusBad   #f85149 红  故障/连接失败:需要人立刻处理
//   绿-琥珀-红 是工业界面的通行语义,操作员不用学就知道轻重。
//
//   【★ 四档字色统一成 #0d1117(近黑),不是随手挑的】
//     初版红/灰两档用了白字,WCAG 一算只有 3.35:1 和 4.31:1,低于 4.5 ——
//     现场强光下"故障"两个字会看不清,而故障恰恰是【最该看清的一档】。
//     改成"亮底 + 近黑字"之后四档分别是 7.45 / 5.45 / 7.50 / 5.65,
//     全部达标(数字由 tst_Theme::statusBadgeColorsAreLegibleOnDarkBackground 实跑得出)。
//     附带好处:操作员扫一眼时,四档徽章的【字是一样黑的】,
//     差异只在色相 —— 这正是"同一类东西"的视觉语言。
//     ★ 灰档因此从 #6e7b8b 提到 #7d8b9c:原来的暗灰配深字只有 4.39:1,差一点点。
//       而 #6e7b8b 在 qss 里仍作禁用态文字色 —— 那是对深底的【前景色】,
//       要求相反(要暗才好和正文区分),两处不必是同一个值。
//
//   【★ 色觉障碍】约 8% 男性有红绿色弱 —— 红和绿对ta们可能都偏黄。
//     所以本表【每一个状态都带文字】("状态:运行中"/"状态:故障"),
//     不存在"只有颜色、没有文字"的情况。
//     颜色是第二通道,文字才是第一通道。这个顺序不能反。
enum StatusTone { kStatusIdle, kStatusBusy, kStatusOk, kStatusBad };

QString statusBadgeStyle(StatusTone tone, bool large = false) {
    QString bg;
    switch (tone) {
    case kStatusOk:   bg = QStringLiteral("#3fb950"); break;
    case kStatusBusy: bg = QStringLiteral("#d29922"); break;
    case kStatusBad:  bg = QStringLiteral("#f85149"); break;
    case kStatusIdle: bg = QStringLiteral("#7d8b9c"); break;
    }
    // 字色不再是逐档挑选的 —— 见上面那段"四档字色统一"的说明。
    const QString fg = QStringLiteral("#0d1117");
    return QStringLiteral("background-color: %1; color: %2; "
                          "font-weight: bold; border-radius: 4px; padding: 4px 10px; %3")
        .arg(bg, fg,
             large ? QStringLiteral("font-size: 18px;") : QStringLiteral("font-size: 13px;"));
}
} // namespace

// ★ 阶段 5(2026-10-19):LegacySinglePlcProvider 已删除 —— 它是阶段 4 的临时
//   适配器("不管问什么 plcId 都回 plc1 那一个 driver"),存在的唯一理由是
//   让阶段 4 的 diff 可审。现在 PlcConnectionManager 自己就是 provider,
//   而且它【按 plcId 精确查表】,查不到回 nullptr ——
//   留着那个适配器反而会把"没配 plc2"悄悄变成"写到 plc1",
//   也就是 2b 要消灭的那个 bug 的原型。
//   少一个类、少一层转发,现场行为还从"永远单 PLC"变成"真的多 PLC"。


// 2026-07-13 阶段 2 第 6 练:parseValueList 前向声明(定义在文件底部;默认值在定义里)
QVector<quint16> parseValueList(const QString& text, QString* errOut);

// 阶段 5.4:AlarmLevel → 字符串(给 alarmTable 等级列用;内联避免头文件污染)2026-08-04
static QString alarmLevelName(AlarmLevel l) {
    switch (l) {
        case AlarmLevel::Info:     return "信息";
        case AlarmLevel::Warning:  return "警告";
        case AlarmLevel::Critical: return "严重";
        case AlarmLevel::Emergency: return "危急";
    }
    return "?";
}

hmi::hmi(QWidget *parent)
    : QMainWindow(parent)
{
    ui.setupUi(this);

    // 输入校验:IP 正则 + 端口范围
    QRegularExpression ipRx(
        R"(^(?:(?:25[0-5]|2[0-4]\d|1\d\d|[1-9]?\d)\.){3}(?:25[0-5]|2[0-4]\d|1\d\d|[1-9]?\d)$)");// IP 地址正则
    ui.ipEdit->setValidator(new QRegularExpressionValidator(ipRx, this));// IP 输入框只允许输入合法的 IP 地址格式
	ui.portEdit->setValidator(new QIntValidator(1, 65535, this)); // 端口输入框只允许输入 1-65535 的整数

    // 阶段 0 第 1 练:QTimer 创建 + 手写 connect + 启动
	timer = new QTimer(this);       // 创建一个 QTimer 对象,父对象是 this (hmi 窗口)
	connect(timer, &QTimer::timeout, this, &hmi::onTimerTick);  // 手写 connect:当 timer 超时时调用 hmi::onTimerTick
    // [BUG #1] 原代码(已注释):timer->start(1000);  // 1 秒
    // [FIX #1] 启动挪到 updateUi() 的 Running 分支,跟 state 挂钩

    // 阶段 1 第 2 练:复位定时器(单次 3 秒,到点自动转 Stopped)
	resetTimer_ = new QTimer(this);  // 创建一个复位定时器,单次触发
	resetTimer_->setSingleShot(true); // 设置为单次触发,到点自动停止
	resetTimer_->setInterval(kResetIntervalMs);     // ★ IMPROVE-B4
	connect(resetTimer_, &QTimer::timeout, this, &hmi::onResetFinished);// 手写 connect:当 resetTimer_ 超时时调用 hmi::onResetFinished

    // 阶段 0 第 2 练:三按钮手写 connect(改用显式 connect,不再依赖 connectSlotsByName)
	connect(ui.startButton, &QPushButton::clicked, this, &hmi::onStartClicked);// 当 startButton 被点击时调用 hmi::onStartClicked
	connect(ui.stopButton, &QPushButton::clicked, this, &hmi::onStopClicked);// 当 stopButton 被点击时调用 hmi::onStopClicked
	connect(ui.resetButton, &QPushButton::clicked, this, &hmi::onResetClicked);//   当 resetButton 被点击时调用 hmi::onResetClicked

    // ★★ 2026-10-20(P1-b):按钮行里那些"功能在菜单里已经有一份"的按钮,
    //   全部撤掉 —— 演示画布 / 编辑器 / 运行时画面 / 报警配置 / 报警声音,
    //   以及 .ui 里的 保存工程 / 打开工程 / 导出历史CSV / 导出报警CSV。
    //
    //   【为什么撤】同一个动作有两个入口,后果有三样:
    //     ① 权限要绑两遍,而且必须永远保持一致 —— P0 之前正是这个状态:
    //        按钮绑了、菜单没绑,一个 Viewer 绕到菜单里就把权限整个绕过去了。
    //     ② 主画面第一眼全是按钮(原来那一行 13 个 80x32 的小方块),
    //        现场真正要按的那三个(启动/停止/复位)被淹没在里面。
    //     ③ 改功能要记得改两处,漏一处就分叉,而分叉极难被发现。
    //   菜单那一份是【同一个槽函数】,不是另写一遍 —— 功能一处没少。
    //
    //   【每个功能的入口现在在哪儿】(一个都没丢)
    //     演示画布 / 编辑器 / 运行时画面 → 画面菜单(V)
    //     报警配置 / 报警声音           → 报警菜单(A)
    //     保存工程 / 打开工程           → 工程菜单(P),带 Ctrl+S / Ctrl+O
    //     导出历史CSV / 导出报警CSV     → 数据菜单(D)
    //
    //   【★ 为什么这里【不】再 connect 一遍】
    //     槽函数一个都没删,只是不再从按钮触发。菜单项在 buildMenus() 里连。
    //     刻意不写"按钮隐藏但保留 connect"这种折中 —— 留着连着但看不见的
    //     控件,权限绑定还挂在上面,测试还在数它,腐烂的时候没人知道。
    //     直接不建,漏删的引用就变成【编译错误】。

    // ★★ 2026-10-16:菜单栏 —— 把散在按钮行里的功能收进菜单
    //
    //   【为什么现在才做】hmi.ui 里一直有个空的 QMenuBar(hmi.ui:400),
    //   26 个功能全挤在按钮行里。加 PLC 要改 JSON、报警要弹对话框、
    //   编辑器/运行时画面要按按钮 —— 现场工程师找不到东西在哪。
    //   这不是"界面不好看",是【功能没法被发现】。
    //
    //   【设计原则:菜单项只做"入口",不重造功能】
    //     每一项都 connect 到【已经存在的槽函数】,没有一行新逻辑。
    //     理由:菜单和按钮如果各写一份,改一处忘一处,两边行为就会分叉 ——
    //     而"按钮能用菜单点不了"这种 bug 极难被发现。
    //     按钮保留(向后兼容 + 已有人习惯),菜单是【同样功能的第二个入口】。
    //
    //   【为什么用代码建菜单,不改 .ui】
    //     .ui 是 Designer 的产物,手改 XML 加 QMenu/QAction 极易写坏
    //     (元素顺序、property 格式都对不上就静默不生效)。
    //     而这里的菜单项会随功能增减频繁改动,放代码里 diff 清晰、可 review。
    //     hmi.ui 里那个空 QMenuBar 保留不动 —— Qt 需要它在,
    //     menuBar() 拿到的就是它,菜单项加在它下面。
    buildMenus();

    // ★★ 2026-10-20(P1-c):调试面板折叠(理由见 hmi.h buildDebugPanel 声明处)
    //   必须在 buildMenus() 之后、updateUi() 之前 —— updateUi() 会碰
    //   sendButton/modbusSendButton 的 enabled,那时候它们已经被折进容器里了。
    //   顺序反了不会崩,但"折"这件事会在第一次刷新时跟 enabled 计算搅在一起,
    //   出问题时很难看出是哪一步。
    buildDebugPanel();
    // ★ 2026-10-20:通讯帧监视(调试区第三页「通讯监视」)。
    //   放在 buildDebugPanel() 之后:那一段会把调试区高度按【当前页】定住,
    //   本页的表格高度是 .ui 里写死的 160,两者互不影响。
    //   放在任何 driver 建出来之前:driverAdded 那一段要往这个模型里塞帧,
    //   模型还没建就塞 = 空指针。
    buildCommTracePanel();
    // 初始化 UI(让 statusLabel 跟初始 state 一致)
    updateUi();

    // 阶段 1 第 1 练:TCP 客户端 + 按钮手写 connect
    tcp_ = new TcpClient(this);
	connect(tcp_, &TcpClient::connected, this, &hmi::onTcpConnected);// 当 TCP 连接成功时调用 hmi::onTcpConnected
	connect(tcp_, &TcpClient::disconnected, this, &hmi::onTcpDisconnected);// 当 TCP 连接断开时调用 hmi::onTcpDisconnected
	connect(tcp_, &TcpClient::readyRead, this, &hmi::onTcpReadyRead);// 当 TCP 有数据可读时调用 hmi::onTcpReadyRead
	connect(tcp_, &TcpClient::errorOccurred, this, &hmi::onTcpError);// 当 TCP 发生错误时调用 hmi::onTcpError
    // 阶段 1 第 2 练:任何时候发起连接 → UI 切 Connecting
	connect(tcp_, &TcpClient::connectionAttempted, this, [this]() { // 当 TCP 发起连接时切换到 Connecting 状态
        connState_ = ConnState::Connecting;
        updateUi();
    });
    // 阶段 1 第 2 练:重连进度信号
	connect(tcp_, &TcpClient::reconnectScheduled, this,  // 当 TCP 计划重连时更新重连进度
		[this](int attempt, int max, int delayMs) {// lambda 捕获 this,接收 attempt 当前尝试次数, max 最大尝试次数, delayMs 距离下一次尝试的毫秒数    
			reconnectAttempt_ = attempt;//  更新当前尝试次数
			reconnectMax_ = max;//  更新最大尝试次数
			reconnectDelayMs_ = delayMs;//  更新距离下一次尝试的毫秒数
			connState_ = ConnState::Reconnecting;//  切换到 Reconnecting 状态    
            updateUi();
        });
    // 阶段 1 第 2 练:重连到上限 → UI 进 Failed
	connect(tcp_, &TcpClient::reconnectAbandoned, this, [this]() {// 当 TCP 放弃重连时切换到 Failed 状态
		connState_ = ConnState::Failed; // 切换到 Failed 状态
        updateUi();
        qCDebug(hmiComm) << "重连放弃(5 次重试已用完),需手动重连";
    });

    // TCP 控件按钮(注意:这些按钮的槽函数名都不是 on_<obj>_<signal> 格式,
    // 不会触发自动连接,只能靠上面这堆 connect 把信号接上)
	connect(ui.connectButton, &QPushButton::clicked, this, &hmi::onConnectClicked); // 当 connectButton 被点击时调用 hmi::onConnectClicked
	connect(ui.disconnectButton, &QPushButton::clicked, this, &hmi::onDisconnectClicked);// 当 disconnectButton 被点击时调用 hmi::onDisconnectClicked
	connect(ui.sendButton, &QPushButton::clicked, this, &hmi::onSendClicked);//      当 sendButton 被点击时调用 hmi::onSendClicked

    // ★ 阶段 C · C.4(2026-09-18):协议选择下拉框信号(教训 1.5 跨协议 UX 锁)
    connect(ui.protocolComboBox, &QComboBox::currentTextChanged,
            this, &hmi::onProtocolComboChanged);

    // ★★ 2026-10-10:把没实现的协议置灰(Wqian 2026-09-29 拍板的那条处置)
    //
    //   【原来的问题】S7 / EtherNet/IP 在下拉框里是【正常可选项】,
    //     没灰、没标注。选下去只会弹"协议不兼容,不能切换" ——
    //     界面从头到尾没告诉过用户"这两个还没实现"。
    //     而 S7Driver/EipDriver 至今是 qWarning + emit errorOccurred 的骨架,
    //     底层依赖(snap7 / libplctag)连头文件都没有,3rdparty 下只有 README。
    //
    //   【为什么改成查表而不是直接删掉这两项】
    //     v0.3.0 真接上 S7/EIP 时,如果是删的,还得回头改 UI + 改测试。
    //     置灰 = isUsable() 里加一行就自动亮起来(见 DriverFactory.cpp 注释)。
    //
    //   【为什么这段在 ProtocolAvailabilityUi.cpp 而不是就地写】
    //     就地写就只能让测试复制一份副本去跑,测的不是这一份。
    //     抽出来之后下面 tst_ProtocolComboGrey 调的是同一个函数。
    qwhmi::drivers::applyProtocolAvailabilityStyling(ui.protocolComboBox);

    // 启动时提示一次(现状.md 279-281 定的处置:置灰 + 启动 qWarning)
    for (int i = 0; i < ui.protocolComboBox->count(); ++i) {
        const QString shown = ui.protocolComboBox->itemText(i);
        if (qwhmi::drivers::DriverFactory::isUsable(shown)) {
            continue;
        }
        qWarning() << "[协议可用性]" << shown << "已在下拉框置灰:"
                   << ui.protocolComboBox->itemData(i, Qt::ToolTipRole).toString();
    }

    // ==================== 阶段 2 第 4 练:重设计 — Qt 统一接口 ====================
    // ★★★ 2b(2026-10-19):这里原来是 DriverFactory::create("modbus", this)
    //   建出【唯一一台】driver,modbus_ / PollScheduler / WriteScheduler 全指着它。
    //   工程里配几台都只连一台 —— 那就是 2b 要消灭的"半截多 PLC"。
    //
    //   现在 driver 由 PlcConnectionManager 按配置建,每台一条 socket。
    //   【modbus_ 保留但降级】:它变成一个【非拥有】指针,指向 plc1 的 driver,
    //   只服务于手工调试面板(面板要用 ModbusMaster::inner() 这种协议特有接口)。
    //   面板不扩成 N 路 —— 开发工具、零测试覆盖,扩它只会增加接错线的面。
    plcMgr_ = new PlcConnectionManager(this);

    // 2026-07-07 重设计:连接 / 错误 信号(同阶段 2-3 风格)
    // ★ 2b:driver 是运行期一台一台建出来的,不能在构造函数里 connect 一个
    //   固定对象(那时一个都还不存在)。改成在 driverAdded 时挂上去。
    connect(plcMgr_, &PlcConnectionManager::driverAdded, this,
        [this](const QString& plcId, qwhmi::drivers::IPollDriver* d) {
            // ★ 先把读/写两侧接上 —— 每一台都要,不分 plc1 还是 plc2。
            //   ★ 判空:driverAdded 理论上在 pollScheduler_ / writeScheduler_
            //     建好之后才发(plcMgr_ 那时一个 driver 都还没有),
            //     但这两个指针是成员,写代码的人改构造顺序时不会想到要回来补判空。
            //     判空比"注释里写清楚它不会空"可靠 —— 后者已经骗过人一次了。
            if (pollScheduler_) pollScheduler_->addDriver(plcId, d);
            if (writeScheduler_) writeScheduler_->hookDriver(plcId, d);

            // ★ 只有 plc1 的信号驱动界面状态和调试面板 —— 界面只有一套状态显示,
            //   3 台 PLC 各连各的,弹窗策略也没法定。一个"plc2 断了"的弹窗
            //   在有 3 台 PLC 的现场会变成骚扰。状态如实进日志,界面只反映 plc1。
            if (plcId != QStringLiteral("plc1")) {
                qCDebug(hmiComm) << "[hmi] PLC" << plcId << "已上线(界面状态栏只跟随 plc1)";
                return;
            }
            modbus_ = d;
            auto* modbusDriver = qobject_cast<qwhmi::drivers::ModbusDriver*>(d);

            connect(d, &qwhmi::drivers::IPollDriver::connected, this, [this](){
                modbusState_ = ModbusState::Connected;
                // ★ BUGFIX(2026-07-28):重连时清 PollScheduler 残留 in-flight -1,避免 Variable 永远不读
                if (pollScheduler_) pollScheduler_->resetAll();
                // 连接刚建好,如果之前有"待发"请求 → 现在发(消除 500ms hack)
                if (hasPendingModbus_) {
                    executePendingModbus();
                }
                updateUi();
            });
            connect(d, &qwhmi::drivers::IPollDriver::disconnected, this, [this](){
                // 2026-07-09:记住旧状态,在跳转 Disconnected 前判断 —— Connecting → 要弹窗告诉用户连不上
                const bool wasConnecting = (modbusState_ == ModbusState::Connecting);
                const bool hadPending     = hasPendingModbus_;
                modbusState_ = ModbusState::Disconnected;
                hasPendingModbus_ = false;
                updateUi();
                if (wasConnecting) {
                    // 主动发起的连接失败 —— 常见原因:ModbusSlave 仿真器没起 / IP 端口不对
                    QMessageBox::warning(this, "Modbus 连不上",
                        "TCP 连不上 ModbusSlave(默认 127.0.0.1:5000)。\n\n"
                        "可能原因:\n"
                        "• ModbusSlave 仿真器/PLC 没起\n"
                        "• IP / 端口填错\n"
                        "• 防火墙挡住\n\n"
                        "调好后点 [发送] 重试。");
                } else if (hadPending) {
                    QMessageBox::information(this, "Modbus 断了",
                        "连接意外中断,刚才排队的请求被丢弃。\n请重新点 [发送]。");
                }
            });
            connect(d, &qwhmi::drivers::IPollDriver::errorOccurred, this,
                [this](QString msg){
                    qCDebug(hmiComm) << "Modbus 错误(伴随状态):" << msg;
                    modbusState_ = ModbusState::Failed;
                    updateUi();
                    // 2026-07-09:协议/网络错误也弹窗 —— 之前只 qDebug 太隐了
                    QMessageBox::warning(this, "Modbus 错误", msg);
                });
            // 2026-07-09:UI 防抖 —— pending_ 变空时重新启用发送按钮
            // ★ C.1:idle 是 ModbusMaster 特有,需要从 ModbusDriver.inner() 取
            if (modbusDriver) {
                // ★ 2026-10-20:通讯帧监视 —— 把每一帧塞进第三页的模型。
                //   【为什么只跟 plc1】调试面板其余部分(状态、Modbus 那一行)
                //     也只跟 plc1,保持一致。跟 plc2/plc3 混在一起的话,
                //     两台机器的帧会交错,同一个 TID 看上去像同一台机的 ——
                //     而"是哪台机"恰恰是现场最先要分清的事。
                //   【枚举在信号里为什么不注册 metatype】
                //     这里用的是 lambda(成员函数指针式的重载),编译期就把
                //     FrameDir 的类型定死了,走的是直接连接,不需要 metatype。
                //     ★ 但如果以后把这个连接挪到别的线程,就必须补
                //       qRegisterMetaType<qwhmi::comm::FrameDir>() ——
                //       少补的话连接会【静默不生效】,帧一条都不进来,
                //       而且没有任何报错(跨线程队列连接找不到元类型就断开)。
                connect(modbusDriver->inner(), &ModbusMaster::frameTraced, this,
                    [this](qwhmi::comm::FrameDir dir, const QByteArray& raw,
                           const QString& summary, qint64 elapsedMs) {
                        // 判空:模型是在构造函数里建的,理论上这会儿一定在;
                        //   但这一行是"运行期才跑"的代码,写代码的人不会想到
                        //   要回来补判空 —— 判空比注释可靠(同 driverAdded 里
                        //   pollScheduler_ / writeScheduler_ 那两处)。
                        if (!commTrace_) return;
                        qwhmi::comm::FrameRecord rec;
                        rec.dir       = dir;
                        rec.raw       = raw;
                        rec.summary   = summary;
                        rec.elapsedMs = elapsedMs;
                        // appendFrame 在暂停时返回 false(这一帧没进来)。
                        //   这里【不弹框】:暂停是用户自己按的,
                        //   每来一帧弹一次框会烦死人。
                        commTrace_->appendFrame(rec);
                    });
                connect(modbusDriver->inner(), &ModbusMaster::idle, this, [this](){
                    ui.modbusSendButton->setEnabled(true);
                    statusBar()->showMessage("Modbus 空闲,可发下一帧", kStatusBarShortMs);  // ★ IMPROVE-B4
                });
            }
            updateUi();
        });

    // ★ 2b:plc1 被移除/改配置时,modbus_ 立刻置空。
    //   不置空的话它会指着一个【已被 deleteLater】的 driver ——
    //   调试面板下一次点击就是 UAF,现场表现是随机崩,而且崩在按钮槽里,难查。
    connect(plcMgr_, &PlcConnectionManager::driverAboutToBeRemoved, this,
        [this](const QString& plcId) {
            // ★ 摘两侧必须在 driver 被 deleteLater 之前 ——
            //   阶段 1/4 给 PollScheduler / WriteScheduler 的 unhook/removeDriver
            //   做的都是"先 disconnect 再摘表",前提是 driver 还活着。
            //   顺序反了的话,那两个对象里会留下一条指向已析构对象的连接,
            //   表现是下一次重连时随机崩,而且崩在事件循环里,现场极难查。
            if (pollScheduler_) pollScheduler_->removeDriver(plcId);
            if (writeScheduler_) writeScheduler_->unhookDriver(plcId);

            if (plcId != QStringLiteral("plc1")) return;
            modbus_ = nullptr;
            modbusState_ = ModbusState::Disconnected;
            // 待发队列里的请求属于"发给 plc1 的",plc1 都没了还留着没有意义
            hasPendingModbus_ = false;
            updateUi();
        });

    // 2026-07-07 重设计:通用「发送」按钮 — 靠 fcCombo 区分读/写
    connect(ui.modbusSendButton, &QPushButton::clicked, this, [this](){
        // ★ 2b:plc1 现在不一定存在 —— 工程里可能只配了 plc2/plc3,
        //   或者 plc1 刚被配置界面删了。modbus_ 是非拥有指针,这时候是 nullptr。
        //   原来是 DriverFactory 建出来的一个永远非空的指针,这一层判空以前不需要。
        if (!modbus_) {
            statusBar()->showMessage("plc1 没有 driver —— 调试面板只对 plc1 有效,"
                                     "请在 PLC 配置里确认 plc1 存在", kStatusBarLongMs);
            return;
        }
        QString host = ui.ipEdit->text().trimmed();
        bool ok;
        quint16 port = ui.portEdit->text().toUShort(&ok);
        if (host.isEmpty() || !ok) {
            statusBar()->showMessage("IP 或端口不能为空", kStatusBarMediumMs);  // ★ IMPROVE-B4
            return;
        }
        int slaveId = ui.modbusSlaveEdit->text().toInt(&ok);
        int addr    = ui.modbusAddrEdit->text().toInt(&ok);
        if (!ok || slaveId < 0 || slaveId > 255) {
            statusBar()->showMessage("slaveId 必须 0-255", kStatusBarMediumMs);  // ★ IMPROVE-B4
            return;
        }
        int typeIdx = ui.modbusTypeCombo->currentIndex();
        bool isRead = ui.modbusRwCombo->currentIndex() == 0;  // 0=读,1=写
        // 2026-07-09:UI 下拉框索引 ≠ Qt 枚举值(Qt 0 是 Invalid)
        // 这里做显式映射,以后加类型只改这张表
        static const QModbusDataUnit::RegisterType kTypeMap[] = {
            QModbusDataUnit::HoldingRegisters,   // UI 0 = 保持寄存器
            QModbusDataUnit::InputRegisters,     // UI 1 = 输入寄存器
            QModbusDataUnit::Coils,              // UI 2 = 线圈
            QModbusDataUnit::DiscreteInputs,     // UI 3 = 离散输入
        };
        QModbusDataUnit::RegisterType type = kTypeMap[typeIdx];

        if (!modbus_->isConnected()) {
            // 未连接:先连,等 connected 再发
            modbusState_ = ModbusState::Connecting;
            hasPendingModbus_ = true;
            pendingModbus_.isRead = isRead;
            pendingModbus_.type = type;
            pendingModbus_.addr = addr;
            pendingModbus_.count = 1;
            pendingModbus_.values.clear();
            if (!isRead) {
                // 2026-07-13 阶段 2 第 6 练:写多值(逗号分隔)
                QString err;
                QVector<quint16> vals = parseValueList(ui.modbusDataEdit->text(), &err);
                if (vals.isEmpty()) {
                    statusBar()->showMessage(err.isEmpty() ? "写值不能为空" : err, kStatusBarMediumMs);  // ★ IMPROVE-B4
                    return;
                }
                pendingModbus_.values = vals;  // 整个 list(单值就是 size=1)
            } else {
                pendingModbus_.count = ui.modbusDataEdit->text().toInt(&ok);
                if (!ok || pendingModbus_.count < 1 || pendingModbus_.count > 125) {
                    statusBar()->showMessage("读数量必须 1-125", kStatusBarMediumMs);  // ★ IMPROVE-B4
                    return;
                }
            }
            updateUi();
            // ★ 2b:连的是【plc1 现有的那条连接】,host/port 只是 UI 上那两个框。
            //   真正生效的是工程配置里的 host/port —— 这两个框现在只是"看着像"
            //   在配连接,实际改不动任何东西。
            //   【为什么不改掉这两个框】面板要连成 N 路才能自洽,那是另一个里程碑;
            //   而把框留着、只让它们作用于 plc1,是 2b 范围内最小且不骗人的做法
            //   (至少行为是确定的:发出去的是 plc1)。
            modbus_->connectTo(host, port);
            // 2026-07-09:延长到 5s,失败的话用户盯着屏幕等结果
            statusBar()->showMessage("正在连接 Modbus...(失败会弹窗提示)", kStatusBarLongMs);  // ★ IMPROVE-B4
            return;
        }

        // 已连接:直接发
        executeModbus(isRead, type, slaveId, addr);
    });

    // ==================== 阶段 3.2:VariableManager + demo 变量(订阅发布 UI 验证) ====================
    varMgr_ = new VariableManager(this);

    // 3 个 demo:模拟工业现场常见数据点(压力 Float / 温度 Int16 / 转速 UInt16)
    auto* pressure = new Variable("Pressure", 0x0000,
                                  Variable::DataType::Int16,
                                  Variable::Access::ReadOnly, varMgr_);
    auto* temperature = new Variable("Temperature", 0x0001,
                                     Variable::DataType::Int16,
                                     Variable::Access::ReadOnly, varMgr_);
    auto* speed = new Variable("Speed", 0x0002,
                               Variable::DataType::UInt16,
                               Variable::Access::ReadWrite, varMgr_);
    varMgr_->addVariable(pressure);
    varMgr_->addVariable(temperature);
    varMgr_->addVariable(speed);

    // 阶段 3.x 死区验证对比:speed 设 5 单位死区,其他默认 0
    // 这样改 ModbusSlave 0x0002 (±4 内) 标签不变,(±6 以上) 标签更新 — 一眼看到效果
    pressure->setDeadband(0.0);     // 无死区,每次变化都更新
    temperature->setDeadband(0.0);  // 无死区
    // 阶段 4.2:测试期间临时关掉死区(5 单位吞掉小波动不便 F5 验证);练完决定是否恢复
    speed->setDeadband(0.0);

    // 阶段 4.x 写范围检查(2026-07-28):UI 输入超范围拒绝,PLC 值不超(从 modbus 读)
    pressure->setRange(-1000.0, 1000.0);  // bar,允许负压
    temperature->setRange(-100.0, 200.0); // ℃(工业传感器常见)
    speed->setRange(0, 65535);            // UInt16 全范围

    // 阶段 4.1:Binding 框架 — 替 onVariableChanged 手工 if/else,加新变量只需写一行
    Binding::bind(ui.pressureLabel,    pressure,     "压力(bar): %1");
    Binding::bind(ui.temperatureLabel, temperature,  "温度(℃): %1");
    Binding::bind(ui.speedLabel,       speed,        "转速(rpm): %1");

    // 阶段 4.2:UI → 变量 反向 + 威纶通输入确认(回车写 / Esc 取消)
    Binding::bindEdit(ui.speedWriteEdit, speed);

    // 阶段 3.3:启动定时轮询(替代 demoTimer 的 random 数据,真 modbus 读 PLC)
    //
    // ★★★ 2b(2026-10-19):第二个参数【传 nullptr】。
    //   改之前是 modbus_ —— 构造函数把它塞进 drivers_["plc1"],
    //   于是不管工程里配几台,读通路永远只有一个 driver。
    //   现在 driver 全部由 plcMgr_ 在 applyConfig 时一台一台建、建完 emit driverAdded,
    //   PollScheduler 在 driverAdded 里 addDriver(阶段 1 已经把 addDriver 修成幂等可重入)。
    //   传 nullptr 是合法的:PollScheduler 对空 drivers_ 不做任何假设,
    //   真正开始轮询要等第一台 driver 到位。
    pollScheduler_ = new PollScheduler(varMgr_, nullptr, this);
    // ★ 2b:读侧的 slaveId 也统一从 provider 取(原来 PollScheduler 里是写死的 1)。
    pollScheduler_->setSlaveIdProvider(plcMgr_);
    pollScheduler_->start();
    // ★ 阶段 6.x.1(2026-08-11 Wqian 拍板)分级轮询 —— 单窗口场景初始化
    //   单窗口场景:所有 var 都在 UI 中,所以全部激活
    //   未来多屏幕(主窗口 / 趋势页 / 报警页)可ef调 addActiveVariable/removeActiveVariable
    pollScheduler_->setAllActive();

    // 阶段 4.3.0:启动写调度(与 PollScheduler 对称,处理 Variable.dirty_ → Modbus 写 PLC)
    //
    // ★★★ 2b(2026-10-19):第二个参数从阶段 4 的 LegacySinglePlcProvider 换成 plcMgr_。
    //   LegacySinglePlcProvider 已删除(见本文件上方说明):它的语义是
    //   "不管问什么 plcId 都回 plc1 那一个 driver",而 plcMgr_ 是【按 plcId 精确查表】,
    //   查不到回 nullptr。留着那个适配器会把"没配 plc2"悄悄降级成"写到 plc1",
    //   也就是 2b 要消灭的那个 bug 的原型。
    //
    //   【不再在这里 hookDriver】driver 的 writeFinished 连接在下面 driverAdded
    //   里统一挂 —— 两处挂意味着将来有一处会被漏掉,而漏掉的表现是
    //   "写发得出去、reply 收不回来",变量永远卡在 dirty,不报任何错。
    writeScheduler_ = new WriteScheduler(varMgr_, plcMgr_, this);
    writeScheduler_->start();

    // 阶段 5.2:报警管理器(空架子,5.3 才注册 Checker)
    alarmMgr_ = new AlarmManager(this);

    // 阶段 5.3-收尾(2026-08-07):ThresholdAlarmChecker 合并 High/Low(DRY)+ 最小持续时间
    //   High:温度 > 50℃ 持续 500ms → Critical
    //   Low:  温度 < 10℃ 持续 500ms → Critical
    CheckerConfig highCfg{AlarmLevel::Critical, Direction::Above, 50.0, 500};
    auto* tempHighChecker = new ThresholdAlarmChecker(
        "温度高", temperature, pollScheduler_, highCfg);
    alarmMgr_->addChecker(tempHighChecker);

    CheckerConfig lowCfg{AlarmLevel::Critical, Direction::Below, 10.0, 500};
    auto* tempLowChecker = new ThresholdAlarmChecker(
        "温度低", temperature, pollScheduler_, lowCfg);
    alarmMgr_->addChecker(tempLowChecker);

    // 阶段 4.3.x(2026-08-05 Bug #2 修复):写失败转 Critical 报警
    //   WriteScheduler 重试到上限 → writeFailed → WriteFailChecker → 报警
    //   写失败是持续故障,操作员手动 acknowledge 走 5.5 流程(Active → Acknowledged)
    //   不接 recovered —— 不会自动恢复
    auto* writeFailChecker = new WriteFailChecker(alarmMgr_);
    alarmMgr_->addChecker(writeFailChecker);
    connect(writeScheduler_, &WriteScheduler::writeFailed,
            writeFailChecker, &WriteFailChecker::onWriteFailed);

    // 阶段 5.4(2026-08-04):报警列表 UI 配置(5 列:时间/报警名/等级/值/状态)
    ui.alarmTable->setColumnCount(5);
    ui.alarmTable->setHorizontalHeaderLabels({"时间", "报警名", "等级", "值", "状态"});
    // ★ 拉伸【报警名】那一列,不是最后一列(2026-10-10)。
    //   原来用的是 setStretchLastSection(true) —— "把多余的宽度全给最后一列"。
    //   而最后一列是"状态",内容就"未确认"三个字,大多数时候更短。
    //   实测(876px 视口,不是推理):时间 100 / 报警名 100 / 等级 100 / 值 100 /
    //   状态 476 —— 状态空掉 440px,而报警名只有 100px,
    //   真实报警名(如"3#锅炉给水温度高高报警")需要 135px,★直接被截断★。
    //   这与 F5-a 在 AlarmConfigDialog / AlarmHistoryView 上修掉的是同一形态的缺陷,
    //   当时漏了这两张表。做法照搬那两处:按内容收紧,余量给宽度最不确定的一列。
    ui.alarmTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    ui.alarmTable->horizontalHeader()->setSectionResizeMode(kColAlarmName, QHeaderView::Stretch);
    ui.alarmTable->horizontalHeader()->setStretchLastSection(false);

    // 阶段 5.4:接 AlarmManager 信号(槽函数实现见文件末尾)
    connect(alarmMgr_, &AlarmManager::alarmTriggered, this, &hmi::onAlarmTriggered);
    connect(alarmMgr_, &AlarmManager::alarmRecovered, this, &hmi::onAlarmRecovered);

    // 阶段 5.5(2026-08-04):报警确认(右键菜单)
    ui.alarmTable->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui.alarmTable, &QWidget::customContextMenuRequested,
            this, &hmi::onAlarmContextMenu);

    alarmContextMenu_ = new QMenu(this);
    alarmAckAction_ = alarmContextMenu_->addAction("确认", this, &hmi::onAlarmAcknowledge);
    // ★ 2026-10-14:补上"全部确认" —— AlarmManager::acknowledgeAll()
    //   从 5.5 就写好了、返回真实条数(F3 改的),但全项目没有一个入口调它。
    alarmAckAllAction_ = alarmContextMenu_->addAction("全部确认", this,
                                                       &hmi::onAlarmAcknowledgeAll);

    // 阶段 5.5 BUGFIX:报警已确认 → 表格行"未确认"→"已确认"(停闪),用 ID 匹配避免同名报警错配
    connect(alarmMgr_, &AlarmManager::alarmAcknowledged, this,
        [this](qint64 id){
            for (int row = 0; row < ui.alarmTable->rowCount(); ++row) {
                auto* item = ui.alarmTable->item(row, 1);
                if (item && item->data(Qt::UserRole).toLongLong() == id) {
                    // ★ 2026-10-14:走 setAlarmRowState + repaintAlarmRow。
                    //   原来只改文字 —— 状态 UserRole 没跟着变,配色按"未确认"算,
                    //   于是"确认"这一下文字变了、颜色没变,而闪烁定时器照旧在跑。
                    //   操作员看到的是:点了确认,状态列变了,那一行却还在闪。
                    setAlarmRowState(row, AlarmState::Acknowledged);
                    repaintAlarmRow(row);
                    updateAlarmBlinkTimer();
                    break;
                }
            }
        });

    // 阶段 5.5:全部清除 → 清空 alarmTable + 停闪烁
    connect(alarmMgr_, &AlarmManager::allCleared, this, &hmi::onAllAlarmsCleared);

    // 阶段 5.4:严重级闪烁定时器(500ms 切红/橙)
    alarmBlinkTimer_ = new QTimer(this);
    alarmBlinkTimer_->setInterval(500);
    connect(alarmBlinkTimer_, &QTimer::timeout, this, &hmi::onAlarmBlinkTick);

    // ★ F5(2026-10-08):报警声音/蜂鸣。
    //   【为什么放在闪烁定时器旁边】两者都是"报警发生了"的【表现层】响应,
    //   而 AlarmManager 只负责"发生了"这个事实。
    //   【为什么这里什么都不配】默认设置就该是现场能直接用的那一套
    //   (Info 不响、其余三档响、5 秒重复提醒到确认为止)。
    //   想改的走「报警声音…」菜单 —— 见 onAlarmSoundSettings。
    alarmSound_ = new AlarmSoundPlayer(alarmMgr_, this);

    // 阶段 5.6(2026-08-04):报警联动动作 — 温度高/低时速度写 0(模拟停机)
    //   ★ 5.6 收尾(2026-08-07):mapping 配置抽到 initActionManager(),为阶段 6 JSON 铺路
    actionMgr_ = new ActionManager(this);
    actionMgr_->setVariableManager(varMgr_);  // ★ E #29(2026-09-15):按名查指针需要 varMgr
    initActionManager(speed);   // 阶段 5.6 收尾:集中 mapping 配置(speed 是构造函数局部变量)
    connect(alarmMgr_, &AlarmManager::alarmTriggered,
            actionMgr_, &ActionManager::onAlarmTriggered);

    // ★ 阶段 6.3 onVariableReplaced connect 已删除(E #29):改为按名查,无需监听 variableReplaced 广播

    // ★ 阶段 6.2(2026-08-07):Project 类(saveToFile 入口)—— 等所有管理器都建好后才能 new
    project_ = new Project(varMgr_, alarmMgr_, actionMgr_, pollScheduler_, tcp_, this);

    // ★★★ 2b(2026-10-19):把"建连接"这个副作用的接收端交给 Project。
    //   没有这一句,Project 读到 plcs[] 就只是读到了 —— 工程里配 3 台,
    //   运行期一条连接都不会建,而日志里【不会】有任何提示
    //   (2a 期间那句"多台同时在线是 2b 的工作"已经在阶段 3 删掉了,
    //    因为它做完后那句话就是骗人的)。
    //
    //   【★ 捕获 this,不能捕获 plcMgr_ 裸指针】
    //     捕获 this 有它的代价(见 hmi.h 里的构造顺序说明),但捕获裸指针更糟:
    //     project_ 在析构逆序里比 plcMgr_ 后死还是先死,取决于构造顺序,
    //     一旦顺序调了,lambda 里那个指针就是野的 —— 而它只在"打开工程"时
    //     才被调用,现场表现是"平时好好的,某次打开工程崩"。
    //     捕获 this:崩之前那一下 this 一定还活着。
    project_->setPlcApplier([this](const QJsonArray& plcs) {
        if (!plcMgr_) {
            qCWarning(hmiComm) << "[hmi] ★ 工程里有 PLC 配置,但 plcMgr_ 不存在 ——"
                               << "不会有任何连接被建立。";
            return;
        }
        const auto r = plcMgr_->applyConfig(plcs);
        qCInfo(hmiComm) << "[hmi] PLC 配置已应用:"
                        << "新增" << r.added.size()
                        << "移除" << r.removed.size()
                        << "重连" << r.reconnected.size()
                        << "未变" << r.unchanged.size()
                        << "拒绝" << r.rejected.size();
        for (const QString& id : r.rejected) {
            // ★ 拒绝的要【点名】。原来 PlcConnectionManager 会打,
            //   但那是 qCDebug 级别,默认日志配置下看不见 ——
            //   "我配了 3 台,怎么只连上 2 台"这种问题在现场无从查起。
            qCWarning(hmiComm) << "[hmi] ★ PLC" << id << "被拒绝(协议不支持或地址非法),未连接。";
        }
        updateUi();
    });

    // ★ 阶段 D · D.1(2026-09-18):SQLite 历史库记录器
    //   监听所有 Variable 的 valueChanged → 写入 %APPDATA%\QianWei\HMI\history.db
    historyRecorder_ = new qwhmi::history::HistoryRecorder(this);
    if (historyRecorder_->open(qwhmi::history::HistoryRecorder::defaultDbPath())) {
        historyRecorder_->setSource(varMgr_);
        qInfo() << "[hmi] HistoryRecorder 启动 OK,db="
                << qwhmi::history::HistoryRecorder::defaultDbPath();
    } else {
        qWarning() << "[hmi] HistoryRecorder 启动失败,历史功能禁用";
    }

    // ★ 阶段 D · D.2(2026-09-20):历史曲线控件
    //   new TrendWidget → 接 HistoryRecorder + VariableManager → 替换"曲线"tab 占位 QLabel
    //   D.2 起步:默认绑 "plc1.Speed"(跟主窗口 demo 变量对齐),用户切工程时可改
    trendWidget_ = new qwhmi::widgets::TrendWidget(this);
    trendWidget_->setHistoryRecorder(historyRecorder_);
    trendWidget_->setVariableManager(varMgr_);
    trendWidget_->setVariable("plc1", "Speed");

    {
        // ★ 找到 hmi.ui 里"曲线"tab 的占位 QLabel(trendChartPlaceholder)
        //   layout->replaceWidget 后,旧 Label 自动 delete
        QLabel* placeholder = ui.bottomTabs->findChild<QLabel*>("trendChartPlaceholder");
        QVBoxLayout* trendLayout = qobject_cast<QVBoxLayout*>(
            ui.bottomTabs->widget(1)->layout());
        if (placeholder && trendLayout) {
            trendLayout->replaceWidget(placeholder, trendWidget_);
            placeholder->deleteLater();
        } else {
            qWarning() << "[hmi] 找不到 trendChartPlaceholder / 曲线 tab layout,D.2 曲线未接入";
        }
    }

    // ★ 阶段 D · D.3(2026-09-20):报警持久化(同 history.db + alarm_history 表)
    //   new AlarmRecorder → open(同 DB) + setSource(alarmMgr_) → 4 个信号自动落表
    alarmRecorder_ = new qwhmi::history::AlarmRecorder(this);
    if (alarmRecorder_->open(qwhmi::history::HistoryRecorder::defaultDbPath())) {
        alarmRecorder_->setSource(alarmMgr_);
        qInfo() << "[hmi] AlarmRecorder 启动 OK,DB=" << qwhmi::history::HistoryRecorder::defaultDbPath();
    } else {
        qWarning() << "[hmi] AlarmRecorder 启动失败,报警历史功能禁用";
    }

    // ★ F4(2026-10-08):把 D.3 留的"报警历史"占位 Label 换成真的查询界面。
    //   【为什么用 replaceWidget 而不是改 .ui】
    //   改 .ui 要重新跑 UIC,而这个项目里 UIC 已经咬过人
    //   (见上方"报警配置"按钮的说明:不用改 .ui 就不会撞 AUTOUIC)。
    //   曲线 tab 当初留的注释也是这个套路 —— 照着做,不发明新办法。
    //
    //   【为什么放在这里而不是 .ui 定义时】
    //   这个视图要 AlarmRecorder*,而 recorder 在 D.3 才建出来;
    //   .ui 里放控件就得用 ui->setupUi(this) 之后再填。
    if (auto* placeholder = ui.tabAlarmHistory->findChild<QLabel*>(
                QStringLiteral("alarmHistoryPlaceholder"))) {
        auto* view = new qwhmi::history::AlarmHistoryView(alarmRecorder_, ui.tabAlarmHistory);
        ui.tabAlarmHistoryLayout->replaceWidget(placeholder, view);
        // 占位 Label 要显式删掉 —— replaceWidget 只是"换位置",
        // 不删的话它还会留在布局里,用户看到上面那行灰字。
        delete placeholder;
    }

    // ★ 阶段 6.3(2026-08-07):loadFromFile 完成后重绑 UI(Binding 持的是旧 Variable*)
    connect(project_, &Project::loaded, this, [this](const QString&){
        // ★ 6.5 收尾(2026-08-08 BugFix #1):先断开旧 binding,避免 connection 累积泄漏
        Binding::unbind(ui.pressureLabel);
        Binding::unbind(ui.temperatureLabel);
        Binding::unbind(ui.speedLabel);
        Binding::unbindEdit(ui.speedWriteEdit);

        // 重绑 3 个 label(Variable* 重建后,旧绑定死了)
        Binding::bind(ui.pressureLabel,    varMgr_->findVariable("Pressure"),    "压力(bar): %1");
        Binding::bind(ui.temperatureLabel, varMgr_->findVariable("Temperature"), "温度(℃): %1");
        Binding::bind(ui.speedLabel,       varMgr_->findVariable("Speed"),       "转速(rpm): %1");
        // ★ 阶段 6.3(2026-08-07):bindEdit 也持旧 Variable*,load 后必须重绑(否则 UI 改值写到死指针)
        Binding::bindEdit(ui.speedWriteEdit, varMgr_->findVariable("Speed"));

        // ★ 6.5 收尾(2026-08-08 BugFix #2):auditTable 切工程时清空,避免老数据残留
        ui.auditTable->setRowCount(0);

        // ★★ 2b(2026-10-19):原来这里有一整段"loadFromFile 后从 tcp_ 拿 host/port
        //   主动 connectTo 同步 modbus"。现在【整段删掉】。
        //   原因:连接已经由 Project::setPlcApplier → plcMgr_->applyConfig
        //   在 loadFromFile 【内部】建立了 —— 那个 applier 是在 loaded 之前触发的,
        //   所以等到 loaded 再同步,是在用一条已经建好的连接去覆盖它。
        //   而且 tcp_ 的 host/port 和工程里的 plc[0] 是两个来源,
        //   它们不一致时"谁最后 connectTo 谁赢",界面上看不出区别。
        //   现在只有一个来源:工程配置。
        updateUi();
    });

    // ★ 阶段 6.3 FIX(2026-08-07):把 connect 提前到 project_ 实例化之后(loadFromFile 之前)
    //   原来 connect 在 DEBUG block 之后,loadFromFile emit loadError 时还没连 → lambda 不触发
    connect(project_, &Project::loadError, this, [this](const QString& reason){
        QMessageBox::warning(this, "加载失败", reason);
    });

    // ★ 阶段 6.3(2026-08-07):启动默认读 default.json(开箱即用,失败静默回退)
    //   ★ 6.4 收尾(2026-08-08):用 applicationDirPath() 拼路径,挪工程位置也能找到
    //   【背景】exe 在 x64/Debug/,default.json 在工程根——往上找 3 级,适配 MSBuild / 发布 / Qrc 各种布局
    //   【取舍】找不到就静默回退到代码默认(hmi 构造函数手动 new 的 3 个 var + 2 checker),不弹错
    //   【参照】Wqian 2026-08-08 review 6.4 修复 #1
    QStringList defaultPathCandidates;
    QDir appDir(QCoreApplication::applicationDirPath());
    defaultPathCandidates << appDir.absoluteFilePath("default.json");
    for (int i = 0; i < 3; ++i) {
        if (appDir.cdUp()) defaultPathCandidates << appDir.absoluteFilePath("default.json");
    }
    QString defaultPath;
    for (const QString& p : defaultPathCandidates) {
        if (QFile::exists(p)) { defaultPath = p; break; }
    }
    if (!defaultPath.isEmpty()) {
        if (project_->loadFromFile(defaultPath)) {
            qCDebug(hmiApp) << "[hmi] default.json 加载成功;path=" << defaultPath;
        } else {
            qCDebug(hmiApp) << "[hmi] default.json 加载失败,用代码默认配置;path=" << defaultPath;
        }
    } else {
        qCDebug(hmiApp) << "[hmi] default.json 找不到,候选路径:" << defaultPathCandidates;
    }

    // ★★ P1-b(2026-10-20):保存/打开工程的入口从【按钮】搬到了【工程菜单】。
    //   槽函数 onSaveClicked / onOpenClicked 一个字没改,只是不再从按钮触发 ——
    //   MenuBuilder.cpp 的"打开工程/保存工程"两项 connect 到这两个槽(带 Ctrl+O/Ctrl+S)。
    //   这里原来的两行 connect 随之撤掉:留着就是第二个入口,又回到"两处要保持一致"的老问题。

    // 阶段 4.x 写审计日志(2026-07-28):配置 auditTable + 接 WriteScheduler 信号
    ui.auditTable->setColumnCount(4);
    ui.auditTable->setHorizontalHeaderLabels({"时间", "Variable", "新值", "结果/地址"});
    // ★ 拉伸【Variable】那一列,不是最后一列(2026-10-10)。理由同上:
    //   最后一列是"结果/地址",就几个字;而 Variable 名是用户自己配的,
    //   长度最不确定。原来 stretchLastSection(true) 让短列吃走全部余量。
    ui.auditTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    ui.auditTable->horizontalHeader()->setSectionResizeMode(kColVarName, QHeaderView::Stretch);
    ui.auditTable->horizontalHeader()->setStretchLastSection(false);
    connect(writeScheduler_, &WriteScheduler::auditAppended, this,
        [this](const WriteAuditEntry& e){
            int row = ui.auditTable->rowCount();
            ui.auditTable->insertRow(row);
            ui.auditTable->setItem(row, 0, new QTableWidgetItem(e.timestamp.toString("HH:mm:ss")));
            ui.auditTable->setItem(row, 1, new QTableWidgetItem(e.variableName));
            ui.auditTable->setItem(row, 2, new QTableWidgetItem(e.newValue));
            ui.auditTable->setItem(row, 3, new QTableWidgetItem(
                QString("%1 %2").arg(e.resultText, e.address)));
            ui.auditTable->scrollToBottom();
        });

    // ★ B4(2026-09-17):画面级权限 - 按钮 enabled 跟 CurrentUser.role 挂钩
    //   B3 已在 Binding.cpp:148 / AlarmManager.cpp:35 拦截动作发生瞬间(兜底),
    //   这里负责"动作发生前 UI 就灰",提升 UX、减少无效点击 + 拒日志噪音。
    //   connectButton/disconnectButton 不绑:任何角色都要能连 PLC 看数据
    //   speedWriteEdit 是 QLineEdit 不是按钮,B3 在回车时拦
    //   托盘"用户管理"在 SystemTray.cpp 里 bindActionEnable(ManageUsers)
    qwhmi::auth::bindButtonEnable(ui.startButton, qwhmi::auth::Permission::WriteVariable);
    qwhmi::auth::bindButtonEnable(ui.stopButton,  qwhmi::auth::Permission::WriteVariable);
    qwhmi::auth::bindButtonEnable(ui.resetButton, qwhmi::auth::Permission::WriteVariable);

    // ★★ P1-b(2026-10-20):这条绑定还在,但它【不再只靠按钮了】。
    //
    //   【背景】P0(2026-10-20)给代码后加的 5 个按钮补了 bindButtonEnable,
    //   当时绑的是按钮。P1-b 把这些按钮全撤了 —— 入口搬到了菜单栏。
    //   于是"编辑器的唯一门"从按钮变成了菜单项:
    //     MenuBuilder.cpp 里"画面 → 编辑器"绑 EditProject,
    //     那一条现在是 EditorMainWindow(内部零权限检查,直接
    //     project_->saveToFile())的【唯一一道门】。
    //
    //   【为什么两道门必须一致 —— 不一致比没有门更危险】
    //     界面上灰着的按钮让人以为"这东西我不能碰",而菜单里却能点开。
    //     所以 P1-a 把菜单 17 项逐项表态了(见 MenuBuilder.cpp 的权限表),
    //     而且 addItem 的 perm 参数【没有默认值】—— 加一项不表态就编译不过。
    //     这正是 P0 事故的形状:每加一个入口只写 tooltip,没人顺手补绑定,
    //     漏一次不报错、测试也绿,权限在一个个加功能的过程中被悄悄漏光。
    //
    //   【P0 补的那几条绑定为什么现在一条都不剩】
    //     editorBtn / alarmCfgBtn / alarmSoundBtn 三个按钮对象本身没了,
    //     runtimeBtn / demoBtn 同样。对应的权限现在由【菜单项】承担,
    //     判定依据一个字没改(查过各对话框内部自查的是哪个权限):
    //       编辑器      → EditProject  它能存盘改写工程文件(唯一门)
    //       报警配置    → EditProject  AlarmConfigDialog.cpp:1353 自查同一权限
    //       报警声音    → EditProject  AlarmSoundDialog.cpp:281 同上
    //       运行时画面  → 【不绑】     只读,任何角色都要能看现场画面
    //       演示画布    → 【不绑】     只读的演示控件
    //     漏绑是 bug,刻意不绑是决定 —— 都写在 MenuBuilder.cpp 的权限表里。
}


// ★ DIAG-2026-09-09:窗口关闭原因诊断
//   spontaneous=true → 外部事件(WM_CLOSE / Taskmgr / 父进程退出)
//   spontaneous=false → 用户代码调 close()
//   看 isVisible → 关之前窗口是显示的还是隐藏的
void hmi::closeEvent(QCloseEvent* e) {
    qCInfo(hmiApp) << "[hmi-close] reason spontaneous=" << e->spontaneous()
                   << "isVisible=" << isVisible()
                   << "isMinimized=" << isMinimized()
                   << "windowState=" << (int)windowState();
    QMainWindow::closeEvent(e);
}
hmi::~hmi()
{}

// ============================================================
// 2026-07-13 阶段 2 第 6 练:parseValueList helper
// 解析 "1,2,3" / "0xFF,0x00" 这种逗号分隔 uint16 列表
// 失败:errOut 写错误信息(用于 statusBar),返回值是空 list
// ============================================================
QVector<quint16> parseValueList(const QString& text, QString* errOut = nullptr) {
    QVector<quint16> result;
    const QStringList parts = text.split(',', Qt::SkipEmptyParts);
    for (const QString& p : parts) {
        QString t = p.trimmed();
        if (t.isEmpty()) continue;
        bool ok = false;
        quint16 v = 0;
        if (t.startsWith("0x", Qt::CaseInsensitive)) {
            v = t.toUShort(&ok, 16);  // 0x 前缀走十六进制
        } else {
            v = t.toUShort(&ok, 10);  // 默认十进制
        }
        if (!ok) {
            if (errOut) *errOut = QString("非法值: '%1'(需 0-65535 或 0x 前缀)").arg(t);
            return {};
        }
        result.append(v);
    }
    if (result.isEmpty() && errOut) *errOut = "值列表为空";
    return result;
}

// 2026-07-07 重设计:执行待发请求(连接刚建好后)
void hmi::executePendingModbus() {
    executeModbus(pendingModbus_.isRead, pendingModbus_.type,
                  ui.modbusSlaveEdit->text().toInt(),
                  pendingModbus_.addr);
    hasPendingModbus_ = false;
}

// 2026-07-07 重设计:统一的读/写入口
void hmi::executeModbus(bool isRead, QModbusDataUnit::RegisterType type, int slaveId, int addr) {
    QModbusReply* reply = nullptr;

    // ★ C.1(2026-09-17):executeModbus 是 Modbus-特定调试 UI,需要 ModbusMaster 接口
    //   从 modbus_(IPollDriver*) cast 回 ModbusDriver*,再拿 inner() 用 ModbusMaster 接口
    // ★ 2b:modbus_ 现在可能为 nullptr(plc1 没配 / 被删 / 正在重连)。
    //   调本函数的两处都判过了,但这个函数自己也判一次 ——
    //   executePendingModbus() 是由 connected 信号异步调的,
    //   从"点击发送"到"connected 回来"之间 plc1 完全可能被配置界面删掉。
    auto* modbusDriver = qobject_cast<qwhmi::drivers::ModbusDriver*>(modbus_);
    if (!modbusDriver) {
        qCWarning(hmiApp) << "[hmi] executeModbus:modbus_ 不是 ModbusDriver(C.1 协议切换后此函数无效)";
        return;
    }
    ModbusMaster* modbus = modbusDriver->inner();

    if (isRead) {
        int count = pendingModbus_.isRead == false ? 1 : pendingModbus_.count;
        if (hasPendingModbus_ && pendingModbus_.isRead) count = pendingModbus_.count;
        // 读
        QModbusDataUnit unit(type, addr, static_cast<quint16>(count));
        reply = modbus->sendReadRequest(unit, slaveId);
    } else {
        // 2026-07-13 阶段 2 第 6 练:写多值(逗号分隔)
        QVector<quint16> vals;
        if (hasPendingModbus_ && !pendingModbus_.isRead) {
            vals = pendingModbus_.values;  // 来自点击时的排队
        } else {
            QString err;
            vals = parseValueList(ui.modbusDataEdit->text(), &err);
            if (vals.isEmpty()) {
                statusBar()->showMessage(err.isEmpty() ? "写值不能为空" : err, kStatusBarMediumMs);  // ★ IMPROVE-B4
                return;
            }
        }
        QModbusDataUnit unit(type, addr, vals);
        reply = modbus->sendWriteRequest(unit, slaveId);
    }

    if (!reply) return;
    // 2026-07-09:UI 防抖 —— 请求已发,按钮变灰,等 idle 信号再亮
    ui.modbusSendButton->setEnabled(false);
    statusBar()->showMessage("Modbus 请求中...", kStatusBarShortMs);  // ★ IMPROVE-B4

    // 2026-07-07 重设计:跟 Qt 官方写法一致 — reply->finished 拿数据
    connect(reply, &QModbusReply::finished, this,
        [this, reply, isRead, type, slaveId, addr]() {
            if (reply->error() == QModbusDevice::NoError) {
                if (isRead) {
                    QModbusDataUnit unit = reply->result();
                    QStringList parts;
                    for (int i = 0; i < unit.valueCount(); ++i) {
                        parts << QString::number(unit.value(i));
                    }
                    lastResultText_ = QString("[%1] 读  从机 %2 FC type=%3 addr=%4 = %5")
                        .arg(QDateTime::currentDateTime().toString("HH:mm:ss"))
                        .arg(slaveId)
                        .arg(static_cast<int>(type))
                        .arg(addr)
                        .arg(parts.join(","));
                } else {
                    lastResultText_ = QString("[%1] 写成功  从机 %2 FC type=%3 addr=%4")
                        .arg(QDateTime::currentDateTime().toString("HH:mm:ss"))
                        .arg(slaveId)
                        .arg(static_cast<int>(type))
                        .arg(addr);
                }
            } else {
                lastResultText_ = QString("[%1] 错误: %2")
                    .arg(QDateTime::currentDateTime().toString("HH:mm:ss"))
                    .arg(reply->errorString());
            }
            lastResultTime_ = QDateTime::currentDateTime();
            updateUi();
            reply->deleteLater();
        });
}

// ==================== 阶段 5.1(2026-08-03 Wqian 拍板):状态迁移表 + tryTransition ====================
//   【背景】之前 3 个槽函数直接 state = State::xxx —— 任何态都能转任何态
//   【改法】所有合法迁移写在这里,槽函数走 tryTransition(Trigger) 查表
//   【取舍】多一处声明,但杜绝"非法迁移"和"重复状态设置"等混乱
//   【参照】Wqian 2026-08-03 review #3(阶段 5.1 拍板)
//   注:AlarmCritical / AlarmCleared 5.3 报警系统完成后再真实触发
const QHash<QPair<hmi::State, hmi::Trigger>, hmi::State> hmi::kTransitionTable = {
    // Stopped 初始 / 主动停止
    {{State::Stopped,    Trigger::StartButton},        State::Running},
    {{State::Stopped,    Trigger::StopButton},         State::Stopped},   // 重复按 stop,合法但无变化
    {{State::Stopped,    Trigger::ResetButton},        State::Resetting}, // 启动 3s reset 定时器
    {{State::Stopped,    Trigger::AlarmCritical},      State::Fault},
    // Running
    {{State::Running,    Trigger::StopButton},         State::Stopped},
    {{State::Running,    Trigger::ResetButton},        State::Resetting}, // 启动 3s reset 定时器
    {{State::Running,    Trigger::AlarmCritical},      State::Fault},
    // Resetting 中按按钮无变化(等定时器),只能被报警打断
    {{State::Resetting,  Trigger::ResetButton},        State::Resetting},
    {{State::Resetting,  Trigger::ResetTimerFinished}, State::Stopped},   // 3s 定时器到点
    {{State::Resetting,  Trigger::AlarmCritical},      State::Fault},
    // Fault 只能复位清,且严重故障直接转 Stopped(不等定时器,避免长时间卡 Fault)
    {{State::Fault,      Trigger::ResetButton},        State::Stopped},
};

// ★ IMPROVE-5.1(2026-08-03 Wqian 拍板):单一状态迁移入口
//   返回 true=成功迁移(已 updateUi),false=非法迁移(原 state 不变)
bool hmi::tryTransition(Trigger t) {
    auto key = qMakePair(state, t);
    auto it = kTransitionTable.find(key);
    if (it == kTransitionTable.end()) {
        // 非法迁移:不输出日志(避免噪音,5.3 报警触发时可能高频调用)
        return false;
    }
    State newState = it.value();
    state = newState;
    updateUi();
    return true;
}

// ==================== 阶段 0 第 2 练:业务三键槽 ====================
// 阶段 5.1 改造:槽函数只发"意图",实际状态变化走 tryTransition 查迁移表
// ★ BUGFIX-5.5(2026-08-04 Wqian 反馈):报警值还在时禁止启动
//   【背景】Wqian 反馈:条件还在超限时,启动马上又会触发报警,这是浪费 + 不安全
//   【改法】StartButton 点击先检查 alarmMgr_->count(),>0 拒绝 + 弹窗
//   【取舍】弹窗让用户知道原因;静默失败会让用户困惑
void hmi::onStartClicked() {
    if (alarmMgr_->count() > 0) {
        QMessageBox::warning(this, "无法启动",
            QString("还有 %1 条报警未恢复。\n请等条件消失后再启动。").arg(alarmMgr_->count()));
        return;
    }
    tryTransition(Trigger::StartButton);
}
void hmi::onStopClicked()   { tryTransition(Trigger::StopButton); }

// ★★ 2026-10-16:菜单栏搭建(见构造函数里的长注释,那里写了"为什么"和"为什么用代码建")
//
//   【分组依据】按现场工程师的心智模型分,不按代码模块分:
//     工程 —— 打开/保存,是每天开头结尾做的事
//     运行 —— 启停/连接,现场最常用,所以放第一个
//     画面 —— 编辑器/运行时画面,改画面时才用
//     报警 —— 配置和声音设置
//     数据 —— 导出 CSV,交接班/报表用
//     帮助 —— 放"程序现在做不到什么",比藏起来强
void hmi::buildMenus() {
    // ★ 菜单本身在 src/ui/MenuBuilder.cpp 里搭,不在这里。
    //   原因见那个文件头:菜单写在这儿的花,测试只能复制一份跑,测的是副本;
    //   而"点'打开工程'弹出'保存工程'对话框"这种错连,副本测试抓不到。
    //   hmi.cpp 和测试调的是同一个 buildApplicationMenu,测试绿了才算数。
    const QStringList failed = qwhmi::ui::buildApplicationMenu(menuBar(), this);

    // ★★ 失败必须【在这里】喊出来,不能吞掉。
    //   tst_MenuBar 用的是假接收者,它只能证明"接到了一个叫这个名字的槽";
    //   而 receiver 换成 hmi 之后,hmi 到底有没有这个槽,只有这份代码知道。
    //   字符串 connect 拼错槽名【不崩、不报、只是点了没反应】——
    //   那是现场最难查的一类故障:用户看到的就是"这个菜单是灰的/没用的"。
    //   所以每次启动都检查一遍,坏了就喊,而不是等用户去发现。
    for (const QString& f : failed) {
        qCWarning(hmiApp) << "[菜单]" << f
                           << "——该菜单项点了不会有任何反应(槽名和 hmi 里的对不上)";
    }
}

// ★★ 2026-10-20(P1-e):调试区的展开/收起开关。
//   设计理由(以及 P1-c 那个 takeAt 陈旧几何的真 bug)见 hmi.h 里 buildDebugPanel()
//   的声明处,这里只讲实现。
void hmi::buildDebugPanel() {
    // ---- 找 debugTabs 在 centralLayout 里的位置 ----
    //   ★ 这里找的是【一个控件】,不是摘布局。
    //     P1-c 用 takeAt() 把两个 QLayoutItem 摘给新面板 —— 摘出来的布局不会
    //     重新激活,保留着旧几何,面板一展开 Modbus 那行就压在启动/停止/复位
    //     那一行上面(P1-e 修掉的正是这个)。现在两页在 .ui 里由 uic 生成,
    //     这里只需要知道"开关该插在谁前面"。
    const auto widgetIndexOf = [this](QWidget* want) -> int {
        for (int i = 0; i < ui.centralLayout->count(); ++i) {
            if (ui.centralLayout->itemAt(i)->widget() == want) return i;
        }
        return -1;
    };
    const int iTabs = widgetIndexOf(ui.debugTabs);
    if (iTabs < 0) {
        // 找不到就不折 —— 调试区保持常驻可见,而不是少一个功能。
        //   刻意不 qWarning:这不是错误,是 .ui 改了布局顺序的正常后果。
        qCDebug(hmiApp) << "[调试面板] hmi.ui 里没找到 debugTabs,"
                          "调试区保持常驻(功能没丢)";
        return;
    }

    QToolButton* toggle = new QToolButton(this);
    toggle->setObjectName(QStringLiteral("debugToggleButton"));
    toggle->setText(QStringLiteral("调试区(手动发送 / Modbus)"));
    toggle->setCheckable(true);
    toggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    toggle->setArrowType(Qt::RightArrow);
    toggle->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    // 插在 debugTabs 【前面】—— 插反了开关会落在调试区下面,
    // 点它时调试区已经在开关上面,视觉上就成了"点一下开关,东西出现在开关上方"。
    ui.centralLayout->insertWidget(iTabs, toggle);

    // ---- 默认值 = 折叠,而且这是有意选的 ----
    //   调试工具默认摊在生产界面上,等于每个人每次开机都要先把它关掉。
    //   ★ 但箭头/文字必须跟着 checked 一起变,否则用户点开之后不知道现在是什么状态。
    QWidget* tabs = ui.debugTabs;
    const auto syncToggle = [toggle, tabs](bool expanded) {
        tabs->setVisible(expanded);
        toggle->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
        toggle->setText(expanded ? QStringLiteral("调试区(点击收起)")
                                 : QStringLiteral("调试区(手动发送 / Modbus)"));
    };
    connect(toggle, &QToolButton::toggled, this, syncToggle);
    // ★ syncToggle 要手动调一次而不是靠 toggled 发射:
    //   setChecked(false) 在"本来就是 false"时【不发射】toggled,
    //   于是调试区会停在 setVisible 的默认值(true)—— 折叠功能看着像没做。
    syncToggle(false);

    // ★★ 2026-10-20:调试区高度跟着【当前页】走。
    //
    //   【为什么需要】第三页(通讯监视)要塞一张表(160px),前两页各只有一行 40px。
    //     而 QStackedLayout 的 sizeHint 取的是【各页 sizeHint 的最大值】——
    //     也就是说,不显式定住的话,页签控件自己就会按最高的那页(≈240px)算,
    //     前两页下面各空 160px 黑。那不是"留白",是"面板坏了"的观感。
    //   【为什么不在 .ui 里写死】页高取决于 qss 里的页签条高度和按钮高度,
    //     写死就会和主题脱节 —— 改了 qss 之后这里对不上,表现是面板多一截空白。
    //     运行时算 = 跟着主题走。
    //   【取舍】切页时面板会伸缩一次。
    //     另一个选择是"永远按最高页算",那样面板不动,但前两页一直空着。
    //     伸缩一次,比一直空着强。
    const auto syncTabHeight = [this](int idx) {
        if (idx < 0) return;
        QWidget* page = ui.debugTabs->widget(idx);
        if (!page || !page->layout()) return;
        const int pageH = page->layout()->totalSizeHint().height();
        const int barH  = ui.debugTabs->tabBar()->sizeHint().height();
        const int h     = barH + pageH + 4;   // +4:QTabWidget 上下边框(各 2px)
        // 76 = .ui 里 debugTabs 原来的 minimumSize 高度(页签条 + 一行 40 + 边框)
        ui.debugTabs->setFixedHeight(h > 76 ? h : 76);
    };
    connect(ui.debugTabs, &QTabWidget::currentChanged, this, syncTabHeight);
    syncTabHeight(ui.debugTabs->currentIndex());
}

// ★★ 2026-10-20:通讯帧监视(调试区第三页「通讯监视」)。
//
//   【这一页是干什么的】把 ModbusMaster 真正收发的那一帧一帧摊开:
//     时间 / 方向 / 摘要 / 往返耗时 / 原始字节。
//   【为什么值得单独一页】协议出问题时,现场能拿到的只有
//     「Modbus 错误:…」一个弹窗,几秒之后就没了。
//     而「是哪一帧错的」「错之前那几帧正常吗」「往返多久」,
//     才是真正能定位问题的信息 —— 而它们在别的地方从来没留过。
//     轮询日志只记成功的那一帧,错的那一帧不进日志。
//   【数据从哪来】ModbusMaster::frameTraced。
//     只有那一层同时握有【请求帧字节】【响应帧字节】【请求发出的时刻】,
//     所以「往返耗时」只有它算得出来 ——
//     TcpClient 那层只有裸字节,连"什么是一帧"都不知道。
//   【数据层的规矩】CommTraceModel 纯 QtCore,一行界面代码都没有,
//     所以 tst_CommTraceModel 能在无界面环境里跑 ——
//     「行数超限之后最旧的那一行真的被淘汰了吗」这种断言才写得出来。
//   【还没做的(诚实起见写在这)】行着色没做。
//     方向靠"发送/接收/错误"三个字认,错误帧靠"错误"两个字认。
//     要上色就得跟 qss 统一(不能再像以前那样两处各写一套配色),
//     所以它得等一个"配色唯一权威"的地方,而不是随手在模型里塞两个色值。
void hmi::buildCommTracePanel() {
    commTrace_ = new qwhmi::comm::CommTraceModel(this);
    ui.commTraceView->setModel(commTrace_);

    // 行号藏起来:帧列表是滚动淘汰的,行号会一直变 ——
    //   用户看到"第 37 行"会以为那是个稳定的编号,其实下一帧就变成 36 了。
    //   时间列才是稳定的定位依据。
    ui.commTraceView->verticalHeader()->setVisible(false);
    ui.commTraceView->horizontalHeader()->setStretchLastSection(false);

    // 列宽:时间/方向/耗时按内容收紧,摘要吃余量,原始字节给固定宽度。
    //   ★ 原始字节不参与 stretch —— 一帧 Modbus TCP 有十几到二十几个字节,
    //     拉太宽会把摘要挤没,而摘要才是"这一帧在干什么"。
    QHeaderView* hh = ui.commTraceView->horizontalHeader();
    using C = qwhmi::comm::CommTraceModel;
    hh->setSectionResizeMode(C::ColTime,    QHeaderView::ResizeToContents);
    hh->setSectionResizeMode(C::ColDir,     QHeaderView::ResizeToContents);
    hh->setSectionResizeMode(C::ColSummary, QHeaderView::Stretch);
    hh->setSectionResizeMode(C::ColElapsed, QHeaderView::ResizeToContents);
    hh->setSectionResizeMode(C::ColRaw,     QHeaderView::Interactive);
    hh->resizeSection(C::ColRaw, 320);

    // 统计标签:跟着模型走。
    //   statsChanged 在【每一次】appendFrame 后都发 —— 轮询 100ms 一帧,
    //   也就是每秒十几次 setText。这不贵(一个 QLabel 一次 setText),
    //   但它意味着"暂停"必须真的冻结统计,否则界面会显示"暂停中"而数字还在涨。
    connect(commTrace_, &qwhmi::comm::CommTraceModel::statsChanged, this, [this](){
        ui.commTraceStatsLabel->setText(commTrace_->statsText());
    });
    ui.commTraceStatsLabel->setText(commTrace_->statsText());

    // 暂停/继续:按钮是 checkable 的,勾上 = 暂停,文字跟着变。
    //   ★ 文字是"接下来点我会做什么"(暂停→继续),不是"我现在是什么状态"
    //     (那样就该是"已暂停")。这两种约定都有产品用,但同一屏里
    //     只能有一种 —— 这里跟"清空"那个按钮保持一致:都是动作。
    connect(ui.commTracePauseButton, &QPushButton::toggled, this, [this](bool paused) {
        commTrace_->setCollecting(!paused);
        ui.commTracePauseButton->setText(paused ? QStringLiteral("继续") : QStringLiteral("暂停"));
    });

    // 清空:只清帧列表,统计保留(理由见 CommTraceModel::clear 的注释)
    connect(ui.commTraceClearButton, &QPushButton::clicked, this, [this]() {
        commTrace_->clear();
    });
}

void hmi::onShowCapabilities() {
    QMessageBox::information(this, tr("当前支持的功能"),
        tr("<h3>✅ 现在就能用</h3>"
           "<ul>"
           "<li>连接一台 Modbus TCP 的 PLC（填 IP + 端口）</li>"
           "<li>读变量：温度 / 压力 / 转速等，绑到画面控件上</li>"
           "<li>写变量：手动改一个可写值发回 PLC</li>"
           "<li>报警：上下限、超时、确认、声音、导出</li>"
           "<li>画面编辑：22 种控件，拖拽排版、撤销重做、复制粘贴</li>"
           "<li>数据导出：变量表、报警表存成 CSV（交接班/报表用）</li>"
           "<li>多 PLC：可以配好存进工程文件，<b>但目前只连第 1 台</b></li>"
           "</ul>"
           "<h3>⏳ 还没做（菜单里不出现，免得你反复试）</h3>"
           "<ul>"
           "<li>多台 PLC 同时在线 —— 在做</li>"
           "<li>界面上直接加/删 PLC —— 在做</li>"
           "<li>西门子 S7 / AB EtherNet-IP —— 下拉框里是<b>灰的</b>，真接了才能选</li>"
           "<li>新建工程（现在只能打开/保存已有工程）</li>"
           "</ul>"));
}

// ★ BUGFIX-5.5(2026-08-04 Wqian 反馈):报警值还在时禁止复位 + 复位只确认不删记录
//   【背景】Wqian 反馈两点:
//     1) 复位不应该把报警记录清除掉(事故追溯需要)
//     2) 报警值还在(超限)时,既不能复位也不能启动
//   【改法】count > 0 拒绝复位 + 弹窗;复位成功只调 acknowledgeAll()(保留记录)
//   【取舍】acknowledgeAll 标记确认但不删;clearAll 留作工程师手动清
//   【参照】Wqian 2026-08-04 反馈
void hmi::onResetClicked() {
    if (alarmMgr_->count() > 0) {
        QMessageBox::warning(this, "无法复位",
            QString("还有 %1 条报警未恢复。\n请等条件消失后再复位。").arg(alarmMgr_->count()));
        return;
    }
    // 复位:Stopped/Running → Resetting(等 3s 定时器);Fault → Stopped(立即,不等定时器)
    if (tryTransition(Trigger::ResetButton) && state == State::Resetting) {
        resetTimer_->start();  // 启动 3 秒单次定时器,到点 onResetFinished 自动转 Stopped
    }
    // 阶段 5.5:复位联动 — 把所有 active 报警标记为"已确认"(保留记录,不删)
    //
    // ★ F3(2026-09-30):接住 acknowledgeAll 的返回值。
    //   【顺手发现的一个事实,先记在这,没擅自改】
    //   上面 count() > 0 就 return 了,所以能走到这里时 count() 必然 == 0,
    //   而 acknowledgeAll 遍历的正是 activeAlarms_ —— 【它永远确认不到任何东西】。
    //   也就是说这行从 2026-08-04 加进来至今一直是空转,复位并不会确认任何报警。
    //   这算不算 bug 取决于当初的本意:
    //     · 如果本意是"复位时把残留报警标记已确认" → 上面的 count 守卫挡掉了,逻辑矛盾
    //     · 如果本意是"有报警就不许复位" → 那这行就是多余的,应该删
    //   改法涉及复位语义(属于 Wqian 的决定),本次只把返回值接住,
    //   并在这里 qWarning 出来 —— 免得下一个读代码的人以为它在干活。
    const int acked = alarmMgr_->acknowledgeAll();
    if (acked == 0 && alarmMgr_->count() == 0) {
        // 走到这里说明本来就一条都没有,属于正常情况,不打扰操作员。
        // 留这条分支是为了:将来有人挪动上面的 count 守卫时,这里会立刻暴露出来。
        qCDebug(hmiAlarm) << "[复位] 当前没有 active 报警,acknowledgeAll 无事可做(预期行为)";
    }
}

void hmi::onResetFinished() {
    // 阶段 5.1 改造:3 秒定时器到,从 Resetting 触发转 Stopped(走 tryTransition)
    // 注:Fault → Stopped 不经过 Resetting,所以不会触发 onResetFinished
    tryTransition(Trigger::ResetTimerFinished);
}

// ★ D.4.1(2026-09-20):D.4 CSV 导出 UI 集成
//   默认文件名带时间戳,避免覆盖前次导出
//   from/to 用最近 1 天(后续可加日期选择对话框;先做 MVP)
// ★ E.4a(2026-09-21):编辑器入口 — 单例模式 show/hide 切窗
//   点"编辑器"按钮弹 EditorMainWindow(画布 + 调色板 + 属性面板 + JSON 存读)
//   再点同一按钮关窗口(类比演示画布的 static QWidget* 单例模式)
void hmi::onEditorClicked() {
    // ★ E.4a:EditorMainWindow 在 qwhmi::editor 命名空间,这里用全限定
    //
    // ★★ 2026-10-19:原来是【函数内 static】,现在是成员 editorWindow_。
    //   为什么提上来:变量配置对话框要枚举"画面上有哪些控件绑着哪个变量",
    //   而编辑器窗口的控件列表在 EditorMainWindow::controls() 里 ——
    //   函数内 static 的话,这个类【没有任何成员指得过去】,对话框拿不到。
    //   那就等于"查不到画面引用"只能靠一句免责说明,而不是真的查一遍。
    //   (函数内 static 和成员的生命周期一样长:都是进程内单例、
    //    都靠 WA_DeleteOnClose=false 不随关窗口销毁。改的是【可见性】,
    //    不是【生命周期】。)
    if (editorWindow_ && editorWindow_->isVisible()) {
        editorWindow_->hide();
        return;
    }
    if (!editorWindow_) {
        // ★ E.5(2026-09-29):传主画面的 project_ + varMgr_。
        //   原来传的是 EditorMainWindow(nullptr) —— 编辑器自己 new 了一个
        //   4 个 manager 全空的 Project,后果是编辑器和主画面各有一份工程数据,
        //   变量名都填了也绑不上主画面的 Variable。E.5 改成同一个数据源。
        editorWindow_ = new qwhmi::editor::EditorMainWindow(project_, varMgr_);  // 独立顶层窗口
        editorWindow_->setAttribute(Qt::WA_DeleteOnClose, false);   // 进程内单例,不随关掉 delete
    }
    editorWindow_->show();
    editorWindow_->raise();
    editorWindow_->activateWindow();
}

// ★ E.5(2026-09-29):运行时画面 —— 显示编辑器存的工程文件
//   【这条链补上之后】编辑器画控件 → 存 .hmi.json → 这里打开 → 主画面显示,
//   而且变量是活的(varMgr_ 传的是主画面正在轮询的那张表)。
//   以前这条链的终点不存在:编辑器只写不读,主画面只有 9 个 C++ 硬编码的 demo 控件。
void hmi::onRuntimeCanvasClicked() {
    // ★★ 2026-10-19:canvas_ 由函数内 static 提成成员,理由见 onEditorClicked。
    if (canvas_ && canvas_->isVisible()) {
        canvas_->hide();
        return;
    }
    if (!canvas_) {
        // ★ 传 varMgr_ 而不是自建:绑定要连的是主画面正在跑的那些 Variable
        // ★ F3(2026-09-30):同时传 alarmMgr_ —— 少了它,运行时画面上摆的
        //   AlarmListControl 拿不到报警数据源,表格永远空着,而且不报错。
        canvas_ = new qwhmi::editor::RuntimeCanvas(varMgr_, alarmMgr_);
        canvas_->setAttribute(Qt::WA_DeleteOnClose, false);
        canvas_->resize(1000, 700);
    }

    // 弹文件选择 —— 第一版不做"记住上次路径",先把链路打通
    const QString path = QFileDialog::getOpenFileName(
        canvas_, QStringLiteral("选择要显示的工程文件"), QString(),
        QStringLiteral("HMI 工程 (*.hmi.json *.json);;所有文件 (*)"));
    if (path.isEmpty()) {
        // 取消 = 只是不换画面,窗口照常显示(空画布),不该报错退出
        canvas_->show();
        canvas_->raise();
        return;
    }

    const int n = canvas_->load(path);

    // ★ 把"静默失败"变成"看得见的提示" —— 这是 E.5 的核心价值。
    //   以前没有这一层:控件建不出来、变量绑不上,统统无声无息,
    //   用户看到的是一片空白画布,只能猜"是不是我哪里做错了"。
    if (canvas_->lastError().isEmpty() && n == 0) {
        QMessageBox::information(canvas_, QStringLiteral("运行时画面"),
                                 QStringLiteral("工程文件里一个控件也没有。\n"
                                                "请先在编辑器里放几个控件并保存。"));
    } else if (!canvas_->lastError().isEmpty()) {
        QMessageBox::warning(canvas_, QStringLiteral("加载失败"), canvas_->lastError());
    }

    if (!canvas_->unboundNames().isEmpty()) {
        // ★ 变量名打错是【用户操作失误】,不是程序错误 ——
        //   必须让他看见具体是哪几个名字没对上,而不是只说一句"部分控件未绑定"
        QMessageBox::warning(canvas_, QStringLiteral("部分控件没绑上变量"),
                             QStringLiteral("画面加载了 %1 个控件,其中 %2 个填了变量名\n"
                                            "但在当前变量表里找不到:\n\n%3\n\n"
                                            "变量表来自正在运行的工程(打开工程时会变)。\n"
                                            "如果变量刚在编辑器里加过,先在主窗口【打开工程】再回来看。")
                                 .arg(n)
                                 .arg(canvas_->unboundNames().size())
                                 .arg(canvas_->unboundNames().join(QStringLiteral("\n"))));
    }

    canvas_->show();
    canvas_->raise();
    canvas_->activateWindow();
}

// ★ F4-a(2026-10-08):报警配置按钮 — 弹 AlarmConfigDialog
//   【为什么每次点击都 new,不做 static 单例】
//   编辑器 / 运行时画面用 static 单例是因为它们是【窗口】,要能反复
//   show/hide 保持状态。而对话框的草稿在 exec() 每次都会
//   reloadFromManager() 重读(见 AlarmConfigDialog::exec),
//   复用实例反而要操心"上次的草稿还在不在、选中行还对不对"。
//   局部变量 + 栈上析构是最省心的写法。
//   【为什么传 alarmMgr_ 和 varMgr_ 而不是让对话框自己找】
//   主画面的 alarmMgr_/varMgr_ 是正在跑的那套。传错了就会出现
//   "配置里配的报警和实际报的报警不是一回事"——
//   而这种错在界面上完全看不出来。
//
// ★ F4-b(2026-10-08)加了第三个参数 pollScheduler_
//   「应用到运行中」新建报警时要 new ThresholdAlarmChecker,
//   而它靠订阅 PollScheduler::rawRead 才有实时值。
//   传 nullptr 会让新增报警建不出来(对话框会明确拒绝并说明原因),
//   不会建出一条"看着配好了、实际永远不响"的报警。
void hmi::onAlarmConfigClicked() {
    qwhmi::alarm::AlarmConfigDialog dlg(alarmMgr_, varMgr_, pollScheduler_, this);
    dlg.exec();
}

// ★ F5(2026-10-08):报警声音设置
//   【为什么每次点击都 new】同 onAlarmConfigClicked —— 对话框是栈上局部变量,
//   状态存在 AlarmSoundPlayer 里,对话框自己不留草稿。
void hmi::onAlarmSoundSettings() {
    qwhmi::alarm::AlarmSoundDialog dlg(alarmSound_, this);
    dlg.exec();
}

// ★ 2b 阶段6(2026-10-19):PLC 配置对话框
//   【为什么每次点击都 new】同 onAlarmConfigClicked —— 对话框是栈上局部变量,
//   草稿存在它自己的 rows_ 里,关掉就没了。"保存到工程文件"和
//   "应用到运行中"是两个分开的按钮,草稿的生命周期短于任何一种生效方式。
//
//   【为什么传 varMgr_】只为算"引用变量数"那一列 ——
//   删一台被 N 个变量引用的 PLC 时,必须先告诉用户会连累哪几个变量。
//   传 nullptr 那一列显示 0,界面照常工作(只是少了个提示),不会崩。
void hmi::onPlcConfigClicked() {
    qwhmi::plc::PlcConfigDialog dlg(plcMgr_, varMgr_, this);
    dlg.exec();
}

// ★ 2026-10-19:变量配置 —— 让变量 plcId 有 UI 入口(多 PLC 2b 的收尾缺口)
//
//   【为什么必须存在】
//   2a/2b 把 N 台 PLC 的配置存读和运行期连接都做完了,但"变量属于哪台 PLC"
//   至今没有任何入口:plcId 只能手改 JSON 或导 CSV。
//   这不是"还没画界面",是 2b 的多 PLC 架构只完成了一半 ——
//   配了 3 台 PLC,却没有任何正常途径把变量分配给它们。
//
//   【三个管理器都传进去,一个都不省】
//   varMgr_   :数据源
//   alarmMgr_ / actionMgr_ :查引用。传 nullptr 的后果不是"少个提示",
//     是【查不到却看起来像没有】—— 对话框会把 blindSpots 记上并显示,
//     但既然这里拿得到,就没理由让用户去面对一句免责说明。
//   plcMgr_   :plcId 下拉的候选来源。
void hmi::onVariableConfigClicked() {
    qwhmi::plc::VariableConfigDialog dlg(varMgr_, alarmMgr_, actionMgr_, plcMgr_, this);
    dlg.exec();
}
// ★ E.1a 演示画布(2026-09-20):弹独立 QGraphicsView 窗口 + 6 控件 + 绑现有 Variable
//   临时 demo,后续 E.4 WYSIWYG 完整编辑器替换
//   点击"演示画布"按钮 → 切换显示/隐藏 demoWindow_
// ★ E.1b-2(2026-09-21):加 Trend + Table + AlarmList 3 个数据控件(6 → 9)
void hmi::onDemoCanvasClicked() {
    // ★★ 2026-10-19:demoWindow_ 由函数内 static 提成成员,理由见 onEditorClicked。
    if (demoWindow_ && demoWindow_->isVisible()) {
        demoWindow_->hide();
        return;
    }
    if (!demoWindow_) {
        demoWindow_ = new QWidget(nullptr, Qt::Window);   // 独立顶层窗口
        demoWindow_->setWindowTitle("E.1b 演示画布 — 9 控件实时数据");
        demoWindow_->resize(900, 800);

        auto* view = new QGraphicsView(demoWindow_);
        auto* scene = new QGraphicsScene(view);
        scene->setSceneRect(0, 0, 880, 780);
        view->setScene(scene);
        view->setRenderHint(QPainter::Antialiasing);
        view->setBackgroundBrush(QColor(245, 245, 245));

        // ★ 9 控件:3 显示 3 演示 3 数据(放画布上)
        using namespace qwhmi::widgets;
        // 1. LabelControl 静态文字
        auto* lbl = new LabelControl;
        lbl->setPosition({20, 20});
        lbl->setVariableName("plc1.demo");
        lbl->setValue(QStringLiteral("演示画布 — E.1b"));
        scene->addItem(lbl);

        // 2. NumberDisplay 绑 plc1.Speed
        auto* num = new NumberDisplay;
        num->setPosition({20, 80});
        num->setUnit(" rpm");
        if (Variable* v = varMgr_->findVariable("Speed")) num->bind(v);
        scene->addItem(num);

        // 3. TextDisplay 绑 plc1.Temperature
        auto* txt = new TextDisplay;
        txt->setPosition({20, 140});
        txt->setPrefix("温度:");
        txt->setSuffix(" ℃");
        if (Variable* v = varMgr_->findVariable("Temperature")) txt->bind(v);
        scene->addItem(txt);

        // 4. BarControl 绑 plc1.Pressure (0-10 bar)
        auto* bar = new BarControl;
        bar->setPosition({220, 80});
        bar->setSize({300, 30});
        bar->setMinimum(0); bar->setMaximum(10);
        bar->setFillColor(QColor(40, 180, 40));
        if (Variable* v = varMgr_->findVariable("Pressure")) bar->bind(v);
        scene->addItem(bar);

        // 5. GaugeControl 绑 plc1.Speed (0-3000 rpm)
        auto* gauge = new GaugeControl;
        gauge->setPosition({550, 30});
        gauge->setSize({180, 180});
        gauge->setMinimum(0); gauge->setMaximum(3000);
        if (Variable* v = varMgr_->findVariable("Speed")) gauge->bind(v);
        scene->addItem(gauge);

        // 6. LedControl 绑 plc1.Temperature(>50 亮)
        auto* led = new LedControl;
        led->setPosition({780, 30});
        led->setSize({40, 40});
        if (Variable* v = varMgr_->findVariable("Temperature")) {
            led->bind(v);
            led->setOnColor(QColor(40, 200, 40));
            led->setOffColor(QColor(80, 80, 80));
        }
        scene->addItem(led);

        // 7. TrendControl(全宽,840x100)— 当前是静态 sin 波演示
        //   后续 E.4 接 HistoryRecorder 后改成实时数据
        auto* trend = new TrendControl;
        trend->setPosition({20, 250});
        trend->setSize({840, 100});
        trend->refresh("plc1.Speed");   // ★ refresh 内部会画演示数据 + 记 variableName
        scene->addItem(trend);

        // 8. TableControl(左半,420x140)— 演示填几行假数据(不接 VariableManager)
        auto* table = new TableControl;
        table->setPosition({20, 380});
        table->setSize({420, 140});
        table->setHeaders({QStringLiteral("时间"), QStringLiteral("变量"), QStringLiteral("值")});
        table->setCell(0, 0, "12:00:00");
        table->setCell(0, 1, "Speed");
        table->setCell(0, 2, "1234");
        table->setCell(1, 0, "12:00:01");
        table->setCell(1, 1, "Pressure");
        table->setCell(1, 2, "5.6");
        table->setCell(2, 0, "12:00:02");
        table->setCell(2, 1, "Temperature");
        table->setCell(2, 2, "42");
        scene->addItem(table);

        // 9. AlarmListControl(右半,420x140)— 接 alarmMgr_ 自动刷活动报警
        auto* alarmList = new AlarmListControl;
        alarmList->setPosition({460, 380});
        alarmList->setSize({420, 140});
        if (alarmMgr_) alarmList->bindAlarmManager(alarmMgr_);
        scene->addItem(alarmList);

        // 标注文字(纯 QGraphicsSimpleTextItem 即可)
        auto addLabel = [&](const QString& text, qreal x, qreal y) {
            auto* item = scene->addSimpleText(text);
            item->setPos(x, y);
        };
        addLabel(QStringLiteral("LabelControl"),           150,   5);
        addLabel(QStringLiteral("NumberDisplay"),          150,  65);
        addLabel(QStringLiteral("TextDisplay"),            190, 125);
        addLabel(QStringLiteral("BarControl (Pressure 0-10 bar)"), 540, 65);
        addLabel(QStringLiteral("GaugeControl (Speed 0-3000 rpm)"), 770, 215);
        addLabel(QStringLiteral("LED (>50 亮)"),            830,  75);
        addLabel(QStringLiteral("TrendControl (sin 波演示)"), 20, 235);
        addLabel(QStringLiteral("TableControl (3 列演示数据)"), 20, 365);
        addLabel(QStringLiteral("AlarmListControl (绑 alarmMgr_)"), 460, 365);
    }
    demoWindow_->show();
    demoWindow_->raise();
    demoWindow_->activateWindow();
}

void hmi::onExportVariablesClicked() {
    if (!historyRecorder_ || !historyRecorder_->isOpen()) {
        QMessageBox::warning(this, "导出失败", "HistoryRecorder 未启动,无法导出");
        return;
    }
    // ★ 默认变量 = TrendWidget 当前绑的变量(操作员直观)
    const QString varName = trendWidget_ ? trendWidget_->currentVariable() : QString("plc1.Speed");

    // ★ 默认文件名:hmi-variables-<var>-<timestamp>.csv
    const QString defaultName = QString("hmi-variables-%1-%2.csv")
                                    .arg(varName).arg(QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss"));

    // ★ QFileDialog 选路径(默认 Desktop)
    const QString desktop = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    const QString path = QFileDialog::getSaveFileName(
        this, "导出变量历史 CSV",
        desktop + "/" + defaultName,
        "CSV files (*.csv);;All files (*)");
    if (path.isEmpty()) return;   // 用户取消

    // ★ from = 当前时间 - 1 天(历史数据范围;后续 UI 可加日期选择)
    const QDateTime to = QDateTime::currentDateTime();
    const QDateTime from = to.addDays(-1);

    QString err;
    const bool ok = qwhmi::export_::writeVariablesCsv(historyRecorder_, varName,
                                                     from, to, path, &err);
    if (!ok) {
        QMessageBox::critical(this, "导出失败", err);
        return;
    }
    QMessageBox::information(this, "导出成功",
        QString("已导出 %1\n变量: %2\n时间窗: %3 → %4")
            .arg(path).arg(varName)
            .arg(from.toString("yyyy-MM-dd HH:mm:ss"))
            .arg(to.toString("yyyy-MM-dd HH:mm:ss")));
}

void hmi::onExportAlarmsClicked() {
    if (!alarmRecorder_ || !alarmRecorder_->totalCount()) {
        // ★ totalCount=0 不阻断,导出空 CSV(只有表头)— 操作员可能想看列名
        //   但若 alarmRecorder_ 本身没起,要阻断
        if (!alarmRecorder_) {
            QMessageBox::warning(this, "导出失败", "AlarmRecorder 未启动,无法导出");
            return;
        }
    }

    const QString defaultName = QString("hmi-alarms-%1.csv")
                                    .arg(QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss"));
    const QString desktop = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    const QString path = QFileDialog::getSaveFileName(
        this, "导出报警历史 CSV",
        desktop + "/" + defaultName,
        "CSV files (*.csv);;All files (*)");
    if (path.isEmpty()) return;

    // ★ from = 7 天前,to = now;minLevel=0 全收
    const QDateTime to = QDateTime::currentDateTime();
    const QDateTime from = to.addDays(-7);

    QString err;
    const bool ok = qwhmi::export_::writeAlarmsCsv(alarmRecorder_,
                                                   from, to, /*minLevel=*/0,
                                                   path, &err);
    if (!ok) {
        QMessageBox::critical(this, "导出失败", err);
        return;
    }
    QMessageBox::information(this, "导出成功",
        QString("已导出 %1\n时间窗: %2 → %3\n总报警数: %4")
            .arg(path)
            .arg(from.toString("yyyy-MM-dd HH:mm:ss"))
            .arg(to.toString("yyyy-MM-dd HH:mm:ss"))
            .arg(alarmRecorder_->totalCount()));
}

void hmi::onTimerTick() 
{
    // [BUG #1] 原代码(已注释):counter++; qDebug() << counter;
    // [FIX #1] state != Running 直接 return
    if (state != State::Running) return;
    counter++;
    qCDebug(hmiUi) << "counter:" << counter;
    // [FIX #2] 同步显示到 numberLineEdit(原计划新增的 UI 控件)
    ui.numberLineEdit->setText(QString::number(counter));
}

// ==================== 阶段 1 第 2 练:TCP 相关槽 ====================
// 全部走 connState_ + updateUi(),不再有直接 setText/setStyleSheet

void hmi::onConnectClicked() {// 用户点击连接按钮,发起 TCP 连接
	QString host = ui.ipEdit->text().trimmed(); // 获取用户输入的 IP 地址,去除首尾空白
    bool ok;
	quint16 port = ui.portEdit->text().toUShort(&ok);// 获取用户输入的端口号,转换为无符号短整数,并检查转换是否成功
	if (!ok || host.isEmpty()) {//      如果端口转换失败或 IP 地址为空,提示用户输入错误
        // UI 收敛后,这里不再 setText —— 用 statusBar 临时提示(3 秒自动消失)
		statusBar()->showMessage("IP 或端口不能为空", kStatusBarMediumMs);  // ★ IMPROVE-B4
        return;
    }
    // 先改 state、再 updateUi()、最后调底层 —— 点完按钮 UI 立刻有反应
    connState_ = ConnState::Connecting;
    updateUi();
    tcp_->connectTo(host, port);
    // ★ 2b:原来这里还有一行 modbus_->connectTo(host, port)。
    //   现在【删掉】—— 改地址这件事已经由工程配置接管:
    //   PlcConfigDialog 里改 host → applyConfig → 那台重连。
    //   在这里 connectTo 会把 plc1 指到用户随手敲的 IP 上,而工程配置里
    //   记的还是另一个 —— 于是界面显示"连上了",现场连的却不是配置的那台。
    //   ★ tcp_ 是【手工调试通道】,跟调度器无关,保留。
}

void hmi::onDisconnectClicked() {
    // 不主动改 connState_ —— 等底层 disconnected 信号回来再统一改
    // (避免"按钮按下就立刻显示断开,但底层还在 ClosingState"的撕裂)
    // [BUG #9] 原代码(已注释):tcp_->disconnect() (遮蔽 QObject::disconnect)
    // [FIX #9] 改 close()
    tcp_->close();
}

void hmi::onSendClicked() {// 发送框内容发给服务器
    // [BUG #6] 原代码(已注释):hex/fromHex/empty/send/appendRecv
    // [FIX #4] 加 prefix 区分发送/接收
    // [FIX #6] 顺序校验:空、非法 hex、未连接、send 返回值
    QString text = ui.sendEdit->text();
	if (text.trimmed().isEmpty()) {  // 发送内容不能为空
		statusBar()->showMessage("发送框为空", kStatusBarMediumMs);// ★ IMPROVE-B4
		return;// [FIX #6] 发送前校验:空
    }
	QString hex = text.remove(' ');// 去掉空格
	QByteArray bytes = QByteArray::fromHex(hex.toLatin1());// 从十六进制字符串转换为字节数组
	if (bytes.isEmpty()) {// 发送内容必须是合法的十六进制
		statusBar()->showMessage("发送内容不是合法 hex", kStatusBarMediumMs); // ★ IMPROVE-B4
        return;
    }
	if (!tcp_->isConnected()) {// 只能在连接状态发送
		statusBar()->showMessage("未连接,无法发送", kStatusBarMediumMs);// ★ IMPROVE-B4
        return;
    }
	qint64 n = tcp_->send(bytes);// 发送数据,返回实际发送的字节数
    if (n <= 0) {
		statusBar()->showMessage(QString("发送失败,write 返回 %1").arg(n), kStatusBarMediumMs);// ★ IMPROVE-B4
        return;
    }
	appendRecv(bytes, "→");// 发送成功,把发送的内容也显示到接收框(加个前缀区分)
}

// TCP 信号 → 改 connState_,然后 updateUi()
void hmi::onTcpConnected() {   // TCP 连接成功
    connState_ = ConnState::Connected;
    updateUi();
}

void hmi::onTcpDisconnected() {// TCP 连接断开
    connState_ = ConnState::Disconnected;
    updateUi();
}

void hmi::onTcpError(const QString& msg) { // TCP 发生错误
    Q_UNUSED(msg);
    // ★ 重构(2026-06-18):errorOccurred 是"伴随"信号,不独立决定 UI 状态
    // 状态由 connected/disconnected/reconnectScheduled/connectionAttempted/reconnectAbandoned 决定
    // 这里只 qDebug 记日志(便于调试)
    qCDebug(hmiComm) << "TCP 错误(不独立决定 UI):" << msg;
}

void hmi::onTcpReadyRead(const QByteArray& data) {// 收到数据了,显示在 recvText 上
    // [FIX #4] 接收用 ←
    appendRecv(data, "←");
}

void hmi::appendRecv(const QByteArray& data, const QString& prefix) {// 显示在 recvText 上(纯文本追加,不走 state)
	// ★ BUGFIX(2026-07-28):防止 recvText 无限增长 — 超过 200 行清空
	// 【背景】原代码无上限,长时间跑(7x24)+ 高频收发,recvText 会越来越大,UI 卡顿
	// 【改法】超过 200 行自动 clear,丢老日志保新日志
	// 【取舍】真要"永不丢",得接 #3 写审计升级(持久化到文件)或 #G73 历史数据存储
	// 【参照】改善计划.md #8
	if (ui.recvText->document()->lineCount() > 200) {
		ui.recvText->clear();
	}
	QString hex = data.toHex(' ').toUpper();// 转成十六进制字符串,每字节用空格分隔,大写
	ui.recvText->append(prefix + " " + hex);//  在 recvText 上追加显示,加个前缀区分发送/接收
}

// 阶段 4.1:onVariableChanged 已删除,改用 Binding 框架做 UI 绑定
//   (hmi.cpp 构造函数里 3 行 Binding::bind 替代此处 30 行 if/else)

// 阶段 3.3:onDemoTick 已删除,数据来源改由 PollScheduler 周期性 modbus 读 PLC

// ==================== updateUi():唯一的 UI 渲染入口 ====================
// 由 state(业务)+ connState_(网络)共同决定所有 UI 元素的文字/颜色/enabled
// ★ IMPROVE-B3(2026-07-29):拆 5 个 helper,updateUi() 变 5 行调用,每个 helper 单一职责

void hmi::updateUi() {
    updateStateLabel();        // state → stateLabel + timer 启停 + counter 复位
    updateConnLabel();         // connState_ → connLabel + 重连进度
    updateNumberLineEdit();    // counter → numberLineEdit(替原 6 处重复)
    updateButtons();           // state + connState_ → 业务三键 + 网络按钮 enabled
    updateModbusLabel();       // lastResultText_ → modbusResultLabel
}

// 业务侧 stateLabel(只跟 state 有关)— timer 启停 + counter 复位 + 文字颜色
void hmi::updateStateLabel() {
    switch (state) {
    case State::Running:
        timer->start(kCounterTickMs);  // ★ IMPROVE-B4
        ui.stateLabel->setText("状态:运行中");
        ui.stateLabel->setStyleSheet(statusBadgeStyle(kStatusOk, /*large=*/true));
        break;
    case State::Stopped:
        timer->stop();
        ui.stateLabel->setText("状态:已停止");
        ui.stateLabel->setStyleSheet(statusBadgeStyle(kStatusIdle, /*large=*/true));
        break;
    case State::Resetting:
        timer->stop();
        ui.stateLabel->setText("状态:复位中");
        ui.stateLabel->setStyleSheet(statusBadgeStyle(kStatusBusy, /*large=*/true));
        counter = 0; // 复位时把 counter 也重置(保留原逻辑)
        break;
    case State::Fault:
        // 阶段 5.1(2026-08-03 Wqian 拍板):Fault 态 UI(严重报警触发)
        //   timer 停(进入 Fault 不再跑 counter)
        //   红底白字(跟 ConnState::Failed 一致风格)
        //   注:5.3 接入报警系统后会加闪烁(QTimer 周期切背景色)
        timer->stop();
        ui.stateLabel->setText("状态:故障");
        ui.stateLabel->setStyleSheet(statusBadgeStyle(kStatusBad, /*large=*/true));
        break;
    }
}

// 网络侧 connLabel(只跟 connState_ 有关)— 文字 + 重连进度
void hmi::updateConnLabel() {
    switch (connState_) {
    case ConnState::Disconnected:
        ui.connLabel->setText("状态:已断开");
        ui.connLabel->setStyleSheet(statusBadgeStyle(kStatusIdle));
        break;
    case ConnState::Connecting:
        ui.connLabel->setText("状态:连接中...");
        ui.connLabel->setStyleSheet(statusBadgeStyle(kStatusBusy));
        break;
    case ConnState::Connected:
        ui.connLabel->setText("状态:已连接");
        ui.connLabel->setStyleSheet(statusBadgeStyle(kStatusOk));
        break;
    case ConnState::Failed:
        ui.connLabel->setText("状态:连接失败(超时或网络错误)");
        ui.connLabel->setStyleSheet(statusBadgeStyle(kStatusBad));
        break;
    case ConnState::Reconnecting:
        ui.connLabel->setText(QString("状态:重连中(%1/%2),%3 秒后重试")
            .arg(reconnectAttempt_).arg(reconnectMax_).arg(reconnectDelayMs_ / 1000));
        ui.connLabel->setStyleSheet(statusBadgeStyle(kStatusBusy));
        break;
    }
}

// counter → numberLineEdit ★ IMPROVE-B3:替原 6 处重复 setText
void hmi::updateNumberLineEdit() {
    ui.numberLineEdit->setText(QString::number(counter));
}

// 按钮 enabled(由 state + connState_ 共同决定)— 业务三键 + 网络按钮 + 发送
void hmi::updateButtons() {
    bool tcpReady = (connState_ == ConnState::Connected);
    bool tcpIdle = (connState_ == ConnState::Disconnected || connState_ == ConnState::Failed);
    bool isReconnecting = (connState_ == ConnState::Reconnecting);

    // ★ B4 修复(2026-09-17):updateButtons 之前会覆盖 bindButtonEnable 的 setEnabled,
    //   导致 Viewer 连了 PLC 后 startButton/stopButton/resetButton 变亮
    //   (当时的 saveButton/openButton 不在 updateButtons 里,所以 B4 生效)。
    //   修法:加 canWrite 闸门,跟 B4 形成 AND。
    //   切换用户时 B4 自动重算,updateButtons 也会被 onTcpConnected/disconnected/state 变化触发重跑,
    //   两者协同 → Viewer 永远灰,Admin 在 Stopped+已连 时亮。
    const bool canWrite = qwhmi::auth::checkPermission(
        qwhmi::auth::CurrentUser::instance().role(),
        qwhmi::auth::Permission::WriteVariable);

    // 业务三键:必须 TCP 连上 + 有 WriteVariable 权限才能用(决策 1-B + B4)
    // 阶段 5.1:Fault 态时,startButton/stopButton 因为 state != Stopped/Running 自动灰,
    //   resetButton 因为 state != Resetting 自动可用 —— 现有逻辑已正确,无需改
    ui.startButton->setEnabled(canWrite && tcpReady && state == State::Stopped);
    ui.stopButton->setEnabled(canWrite && tcpReady && state == State::Running);
    ui.resetButton->setEnabled(canWrite && tcpReady && state != State::Resetting);

    // 网络按钮:Reconnecting 时 connectButton 灰掉,disconnectButton 仍可点(用户取消)
    //   不加 canWrite:Viewer 也要能连 PLC 看数据(B4 设计如此)
    ui.connectButton->setEnabled(tcpIdle);
    ui.disconnectButton->setEnabled(!tcpIdle || isReconnecting);

    // 发送按钮:必须已连(断网时灰掉避免发不出去)
    ui.sendButton->setEnabled(tcpReady);
}

// Modbus 侧 modbusResultLabel(2026-07-08 收敛:统一用 lastResultText_)
void hmi::updateModbusLabel() {
    ui.modbusResultLabel->setText(lastResultText_);
}

// ==================== 阶段 5.4(2026-08-04):报警 UI 槽函数实现 ====================

// ★ IMPROVE-5.4(2026-08-04):报警触发 → qDebug + 推状态机 Fault + alarmTable 追加一行
//   【背景】5.3 只做数据 + qDebug,UI 上看不到。5.4 给最小可视化
//   【改法】槽函数内:qDebug + tryTransition(AlarmCritical) + insertRow + scrollToBottom + 启闪烁
//   【取舍】tryTransition 不动 — 保持单一迁移入口的设计

// ★ IMPROVE-5.6-收尾(2026-08-07):集中 ActionManager 配置
//   【背景】原来 addMapping 散在 hmi 构造里,5.6 收尾抽成 init 函数集中管理
//   【改法】把全部 addMapping 集中到这一处;阶段 6 上 JSON 时,init 函数体替换成 loadFromJson(file),hmi 构造一行不用动
//   【取舍】当前还是"配置写代码"(5.6 最小版),但接口位置留好
//   【参照】Wqian 2026-08-07 阶段 5.6 收尾拍板
void hmi::initActionManager(Variable* speed) {
    // ★ E #29(2026-09-15):传变量名而不是裸指针(Mapping 已改存 QString)
    actionMgr_->addMapping("温度高", speed->name(), 0);   // 温度 > 50 触发 → 速度 = 0(停机)
    actionMgr_->addMapping("温度低", speed->name(), 0);   // 温度 < 10 触发 → 速度 = 0(停机)
}

// ★ 阶段 6.2(2026-08-07):UI 保存按钮槽函数 → 调 project_->saveToFile
//   【背景】测试 saveToFile 真能产生正确 JSON
//   【改法】固定路径写到工程目录下的 test_save.json
//   【取舍】6.4 阶段会用 QFileDialog 选路径,这里先固定路径试跑
//   【参照】Wqian 2026-08-07 "在ui界面上做个保存按钮,先试一次"
void hmi::onSaveClicked() {
    if (!project_) {
        QMessageBox::warning(this, "保存失败", "project_ 指针为空");
        return;
    }
    // ★ 阶段 6.4 加强(2026-08-08):QFileDialog 选保存路径(跟"打开工程"对称)
    //   【背景】6.2 写死到 test_save.json,6.4 改成用户选路径(实际工业 HMI 用法)
    //   【取舍】getSaveFileName 跟 getOpenFileName 对称,过滤器一致
    //   【参照】Wqian 2026-08-08 "把'保存工程'也改成 QFileDialog 选路径"
    QString path = QFileDialog::getSaveFileName(
        this, "保存工程", QString(),
        "HMI 工程文件 (*.json);;所有文件 (*.*)");
    if (path.isEmpty()) return;     // 用户按"取消"
    if (project_->saveToFile(path)) {
        QMessageBox::information(this, "保存成功",
            "JSON 已写入:\n" + path +
            "\n\n提示:用记事本打开可看 4 类配置(通讯/变量/报警/联动)");
    } else {
        QMessageBox::warning(this, "保存失败", "写文件失败,看 qDebug");
    }
}

// ★ 阶段 6.4(2026-08-08):UI 打开工程槽函数 → 弹 QFileDialog 选 .json → loadFromFile
//   【背景】6.4 把"打开工程"这条 UI 入口补上,跟"保存工程"对称
//   【P1-b 之后】这个槽不再从按钮触发,只从 工程→打开工程 菜单项(Ctrl+O)触发;
//     槽本身一行没改 —— 改的只是"从哪儿能点到它"。
//   【流程】QFileDialog → 选文件 → project_->loadFromFile()(6.3 已实现,失败走 loadError 弹窗)
//   【取舍】启动默认读 default.json 已经在构造函数里(6.3 修 Bug #3 时写好的),此 slot 只供用户手动切工程
//   【参照】Wqian 2026-08-08 阶段 6.4 拍板
void hmi::onOpenClicked() {
    if (!project_) {
        QMessageBox::warning(this, "打开失败", "project_ 指针为空");
        return;
    }
    // QFileDialog:父窗口/标题/默认路径(空=上次目录)/过滤器
    QString path = QFileDialog::getOpenFileName(
        this, "打开工程", QString(),
        "HMI 工程文件 (*.json);;所有文件 (*.*)");
    if (path.isEmpty()) return;     // 用户按"取消"
    project_->loadFromFile(path);   // 6.3 已实现,失败由 loadError 信号弹窗
}

void hmi::onAlarmTriggered(const Alarm& a) {
    qCDebug(hmiAlarm) << "[Alarm 触发]" << a.name << "level=" << static_cast<int>(a.level)
             << "value=" << a.triggerValue << "threshold=" << a.threshold;
    tryTransition(Trigger::AlarmCritical);   // 推状态机进 Fault

    int row = ui.alarmTable->rowCount();
    ui.alarmTable->insertRow(row);
    ui.alarmTable->setItem(row, 0, new QTableWidgetItem(a.triggeredAt.toString("HH:mm:ss")));
    auto* nameItem = new QTableWidgetItem(a.name);
    nameItem->setData(Qt::UserRole, a.id_);  // ★ 存 ID 给 ack lambda 匹配(5.5 BUGFIX)
    ui.alarmTable->setItem(row, 1, nameItem);

    // ★ 2026-10-14:等级列带 enum,不再让配色去解析中文文字。
    //   原来闪烁判据是 levelItem->text()=="严重"||=="危急" —— 而运行时画布那边
    //   判的是 "Critical"/"Emergency"。两边一个认中文一个认英文,判据天生可能对不上,
    //   而"两套 UI 判据必须一致"恰恰是对齐 500ms 时强调的硬要求。
    auto* levelItem = new QTableWidgetItem(alarmLevelName(a.level));
    levelItem->setData(qwhmi::alarm::kLevelRole, static_cast<int>(a.level));
    ui.alarmTable->setItem(row, 2, levelItem);

    ui.alarmTable->setItem(row, 3, new QTableWidgetItem(a.triggerValue));

    // ★ 状态列同样带 enum,文字与配色都由它派生。
    setAlarmRowState(row, a.state);
    repaintAlarmRow(row);
    ui.alarmTable->scrollToBottom();

    updateAlarmBlinkTimer();
}

// ★ 2026-10-14:改某一行状态列 —— 文字、UserRole、配色三样一起。
//
//   【为什么必须三样一起】
//   只改文字不动 UserRole → 配色还按"未确认"算,那一行既灰着又在闪;
//   只改 UserRole 不动文字 → 屏幕写着"未确认"却不再闪,操作员无从判断。
//   两个都是"看起来能点,点了没反应"那一类静默失败。
void hmi::setAlarmRowState(int row, AlarmState st) {
    auto* statusItem = ui.alarmTable->item(row, 4);
    if (!statusItem) return;
    QString text;
    switch (st) {
        case AlarmState::Active:       text = QStringLiteral("未确认"); break;
        case AlarmState::Acknowledged: text = QStringLiteral("已确认"); break;
        case AlarmState::Cleared:      text = QStringLiteral("已恢复"); break;
    }
    statusItem->setText(text);
    statusItem->setData(qwhmi::alarm::kStateRole,
                        static_cast<int>(qwhmi::alarm::visualStateFor(st)));
}

// ★ 2026-10-14:主画面报警表的配色入口 —— 决策全在 AlarmRowPalette。
//   原来这个函数是【第二个】配色权威(第三个是 AlarmListControl 那套),
//   而且是最弱的一个:静止态非闪烁行直接 setData(BackgroundRole, QVariant())
//   清成白底,于是 Info / Warning 两档跟"这一行没有报警"长得一模一样。
void hmi::repaintAlarmRow(int row) {
    auto* levelItem  = ui.alarmTable->item(row, 2);
    auto* statusItem = ui.alarmTable->item(row, 4);
    if (!levelItem || !statusItem) return;

    const QVariant lv = levelItem->data(qwhmi::alarm::kLevelRole);
    const QVariant sv = statusItem->data(qwhmi::alarm::kStateRole);
    if (!lv.isValid() || !sv.isValid()) {
        // 兜底方向:偏向"更像未处理的报警"。理由与 AlarmListControl::levelAt 相同 ——
        // 漏报比多报危险得多(见 docs/现状.md F2:"压错了报警,比多报一条危险得多")。
        qCWarning(hmiAlarm) << "[hmi] 报警表第" << row
                            << "行缺 UserRole(等级/状态),写这一行的代码漏了 setData";
    }
    const auto level = lv.isValid() ? static_cast<AlarmLevel>(lv.toInt())
                                    : AlarmLevel::Warning;
    const auto state = sv.isValid()
        ? static_cast<qwhmi::alarm::RowVisualState>(sv.toInt())
        : qwhmi::alarm::RowVisualState::Unacknowledged;

    const qwhmi::alarm::RowColors c = qwhmi::alarm::colorsFor(level, state, alarmBlinkOn_);
    for (int col = 0; col < ui.alarmTable->columnCount(); ++col) {
        if (auto* it = ui.alarmTable->item(row, col)) {
            it->setBackground(c.bg);
            it->setForeground(c.fg);
        }
    }
}

// ★ 2026-10-14:该闪的行一个都没有就停表,并【把相位复位】。
//
//   原来只有 onAlarmRecovered 里一句 count()==0 就 stop(),而且不复位相位,
//   于是清空之后再来第一条,第一帧可能停在亮相的浅色上(见 hmi.h 里那个成员变量
//   的注释)。AlarmListControl 在 F3-b 修好了这件事,主画面一直没跟上。
void hmi::updateAlarmBlinkTimer() {
    bool anyShouldBlink = false;
    for (int row = 0; row < ui.alarmTable->rowCount(); ++row) {
        auto* levelItem  = ui.alarmTable->item(row, 2);
        auto* statusItem = ui.alarmTable->item(row, 4);
        if (!levelItem || !statusItem) continue;
        const auto level = static_cast<AlarmLevel>(levelItem->data(qwhmi::alarm::kLevelRole).toInt());
        const auto state = static_cast<qwhmi::alarm::RowVisualState>(
                               statusItem->data(qwhmi::alarm::kStateRole).toInt());
        if (qwhmi::alarm::shouldBlink(level, state)) { anyShouldBlink = true; break; }
    }

    if (anyShouldBlink) {
        if (!alarmBlinkTimer_->isActive()) alarmBlinkTimer_->start();
        return;
    }

    alarmBlinkTimer_->stop();
    if (alarmBlinkOn_) {          // 复位相位 + 把所有行刷回暗相
        alarmBlinkOn_ = false;
        for (int row = 0; row < ui.alarmTable->rowCount(); ++row)
            repaintAlarmRow(row);
    }
}

// ★ BUGFIX-5.5(2026-08-04 Wqian 反馈):报警恢复 → 用 ID 匹配找原行
//   【背景】原按 name 匹配会错配同名报警,改成 ID 唯一匹配
//   【改法】Qt::UserRole 存 ID,scan alarmTable 按 ID 匹配,更新"状态"列文字 → "已恢复"
//   【取舍】没用 model,直接扫行;5.5 数据量小(< 100 条)够用
void hmi::onAlarmRecovered(qint64 id) {
    qCDebug(hmiAlarm) << "[Alarm 恢复]" << id;
    // 恢复不直接转 Stopped — Fault 态只能 ResetButton 清(状态机规则)

    for (int row = 0; row < ui.alarmTable->rowCount(); ++row) {
        auto* item = ui.alarmTable->item(row, 1);
        if (item && item->data(Qt::UserRole).toLongLong() == id) {
            // ★ 2026-10-14:走 setAlarmRowState —— 文字和 UserRole 必须一起改。
            //   原来只 setText("已恢复"),配色那边读不到对应的状态,会按"未确认"画。
            setAlarmRowState(row, AlarmState::Cleared);
            repaintAlarmRow(row);
            break;
        }
    }
    // ★ 判据从 count()==0 换成"还有没有该闪的行"。
    //   count()==0 是 activeAlarms_ 空了 —— 但 acknowledged 却未恢复的报警
    //   仍然在 activeAlarms_ 里(5.5 有意为之:复位不该清历史),
    //   那些行是灰的、不该闪,而 count() 把它们算进来 → 定时器还在空转。
    updateAlarmBlinkTimer();
}

// ★ IMPROVE-5.5(2026-08-04):严重级报警闪烁 + 统一背景管理
//
// ★ 2026-10-14:本函数只剩"切相位 + 重画",判据与配色全部交给 AlarmRowPalette。
//   原来这里是【第二个】配色权威,而它是最弱的一个:
//     · 静止态非闪烁行被 setData(BackgroundRole, QVariant()) 清成白底
//       → Info / Warning 两档跟"这一行没有报警"长得一模一样
//       (而报警表里【每一行都是报警】,没有"没报警"这个参照物)
//     · 闪烁是"换成固定红/橙",危急/严重/警告共用一个红
//       → 闪一下就分不出档位,而分不出档位正是颜色存在的理由
//   两处问题都由 AlarmRowPalette 一并解决,详见那个文件的头注释。
void hmi::onAlarmBlinkTick() {
    alarmBlinkOn_ = !alarmBlinkOn_;
    // 只重画"该闪的那些行",不是全表 —— 闪烁本来就是个局部信号。
    // ★ 不闪的行不用动:它们的颜色由 colorsFor 决定,与相位无关。
    for (int row = 0; row < ui.alarmTable->rowCount(); ++row) {
        auto* levelItem  = ui.alarmTable->item(row, 2);
        auto* statusItem = ui.alarmTable->item(row, 4);
        if (!levelItem || !statusItem) continue;
        const auto level = static_cast<AlarmLevel>(levelItem->data(qwhmi::alarm::kLevelRole).toInt());
        const auto state = static_cast<qwhmi::alarm::RowVisualState>(
                               statusItem->data(qwhmi::alarm::kStateRole).toInt());
        if (qwhmi::alarm::shouldBlink(level, state)) repaintAlarmRow(row);
    }
}

// ★ IMPROVE-5.5(2026-08-04):alarmTable 右键菜单
//   【背景】用户需要确认报警,但不能每行都点确认按钮(太繁琐)
//   【改法】启用 alarmTable 的 Qt::CustomContextMenu,右键弹出"确认"菜单
//   【取舍】只支持"确认"一个动作;5.5+ 后续可加"抑制""详情"等
//   【参照】QTableWidget::setContextMenuPolicy Qt 官方
void hmi::onAlarmContextMenu(const QPoint& pos) {
    QTableWidgetItem* item = ui.alarmTable->itemAt(pos);
    if (!item) return;
    int row = item->row();
    // ★ 2026-10-14:判据从"状态列文字 == 未确认"改成读 UserRole 里的 enum。
    //   拿显示文字当 API 用,翻译一改就错位 —— 而且不会报错,只是安静地置错灰。
    auto* statusItem = ui.alarmTable->item(row, 4);
    const bool canAck = statusItem
        && static_cast<qwhmi::alarm::RowVisualState>(
               statusItem->data(qwhmi::alarm::kStateRole).toInt())
           == qwhmi::alarm::RowVisualState::Unacknowledged;
    if (alarmAckAction_) alarmAckAction_->setEnabled(canAck);

    // "全部确认"看的是全局有没有未确认的,与当前点在哪一行无关。
    if (alarmAckAllAction_) alarmAckAllAction_->setEnabled(alarmUnacknowledgedCount() > 0);

    // 选中当前行,菜单关闭后 onAlarmAcknowledge 用 currentRow() 拿
    ui.alarmTable->setCurrentCell(row, 0);
    alarmContextMenu_->exec(ui.alarmTable->viewport()->mapToGlobal(pos));
}

// ★ 2026-10-14:当前未确认条数 —— 按 AlarmManager 的 state 数,不按表格行数。
//
//   acknowledge 只改 state 不删记录(5.5 有意为之),所以表里既有 Active 的,
//   也有 Acknowledged 还没恢复的。行数 ≠ 未确认数 —— 按行数算的话,
//   操作员确认过的报警仍然被计入,"全部确认"的按钮会一直亮着。
int hmi::alarmUnacknowledgedCount() const {
    if (!alarmMgr_) return 0;
    int n = 0;
    const auto alarms = alarmMgr_->activeAlarms();
    for (const Alarm& a : alarms) {
        if (a.state == AlarmState::Active) ++n;
    }
    return n;
}

// ★ 2026-10-14:一次确认全部。
//
//   【为什么值得做】现场一次异常往往同时来七八条报警,
//   只有单条确认入口的话,操作员得双击/右键七八次 —— 而他此时最该做的是
//   去现场看一眼,不是点鼠标。
//
//   【返回值的用法】acknowledgeAll() 内部逐条调 acknowledge(),
//   权限不足时每条都返回 false → 这里拿到 0。分三种情况讲清楚:
//     ① 确认到了 N 条   → 报个数
//     ② 一条都没确认到 且没权限 → 明确告诉他是权限问题(该找管理员)
//     ③ 一条都没确认到 且有权限 → 本来就没有未确认的(按了也没必要)
void hmi::onAlarmAcknowledgeAll() {
    const int pending = alarmUnacknowledgedCount();
    if (pending == 0) {
        QMessageBox::information(this, "无需确认", "当前没有未确认的报警。");
        return;
    }

    const int acked = alarmMgr_->acknowledgeAll();
    if (acked > 0) {
        qCInfo(hmiAlarm) << "[Alarm] 全部确认:" << acked << "条(共" << pending << "条待确认)";
        // 成功就把"全部确认"自己灰掉 —— 否则菜单下次打开时它还亮着,
        // 而按下去只会得到一句"当前没有未确认的报警"。
        if (alarmAckAllAction_) alarmAckAllAction_->setEnabled(false);
        return;
    }

    // 一条都没确认成。两种可能,提示文案要分开。
    if (!qwhmi::auth::checkPermission(qwhmi::auth::CurrentUser::instance().role(),
                                     qwhmi::auth::Permission::AcknowledgeAlarm)) {
        QMessageBox::warning(this, "无法确认报警",
            QString("当前用户「%1」没有确认报警的权限。\n请让管理员确认后再试。")
                .arg(qwhmi::auth::CurrentUser::instance().name()));
        return;
    }
    // 有权限却一条没确认成 —— 说明 pending 刚被别处清掉了(比如另一路刚恢复)。
    // 这不是错误,不该弹窗打断操作员。
    qCInfo(hmiAlarm) << "[Alarm] 全部确认无事可做:待确认" << pending
                     << "条,但 acknowledgeAll 返回" << acked;
}

// ★ BUGFIX-5.5(2026-08-04 Wqian 反馈):右键菜单"确认"槽 → 用 ID 调 acknowledge
//   【背景】原按 name 调 acknowledge,但同名报警存在时多个 Alarm 共享 name,错配
//   【改法】从 Qt::UserRole 读 ID → alarmMgr_->acknowledge(id)
//   【取舍】每个 Alarm 唯一 ID(QDateTime::currentMSecsSinceEpoch()),保证不重不漏
//   【参照】5.5 BUGFIX:AlarmManager.activeAlarms_ 改用 ID 索引
//
// ★ F3(2026-09-30):接住 acknowledge 的返回值,失败时明确告诉操作员。
//   【原来错在哪】返回值是 void,权限不足时 AlarmManager 只 qCWarning 一行就返回,
//   而这里紧接着无条件 qDebug "[Alarm 确认]" —— 日志说确认了,实际没确认。
//   界面上的表现是:操作员点了"确认",什么也没变,却没有任何提示。
//   权限不足是一个需要【找管理员】才能解决的问题,静默失败等于让操作员干瞪眼。
void hmi::onAlarmAcknowledge() {
    int row = ui.alarmTable->currentRow();
    if (row < 0) return;
    auto* nameItem = ui.alarmTable->item(row, 1);
    if (!nameItem) return;
    qint64 id = nameItem->data(Qt::UserRole).toLongLong();
    if (alarmMgr_->acknowledge(id)) {
        qCDebug(hmiAlarm) << "[Alarm 确认]" << nameItem->text() << "id=" << id;
        return;
    }
    // 走到这里是失败。三种可能,提示文案要分开,别都写成"失败":
    //   ① 权限不足 → 该找管理员
    //   ② 已经确认过 → 不用管,不是错误
    //   ③ 已经恢复/被清掉 → 这行只是历史,不用管
    if (!qwhmi::auth::checkPermission(qwhmi::auth::CurrentUser::instance().role(),
                                     qwhmi::auth::Permission::AcknowledgeAlarm)) {
        QMessageBox::warning(this, "无法确认报警",
            QString("当前用户「%1」没有确认报警的权限。\n请让管理员确认后再试。")
                .arg(qwhmi::auth::CurrentUser::instance().name()));
        return;
    }
    // 有权限却没确认成 → 只可能是 ②③,都是"不用管",给个轻提示即可
    // ★ 必须判空:第 4 列理论上总有内容,但 acknowledge 失败也可能是因为
    //   这一行是 clearAll 之后残留的半截行。真崩在这里就是"给个提示把程序搞挂"。
    auto* statusItem = ui.alarmTable->item(row, 4);
    if (!statusItem) return;   // 拿不到状态列就什么都不说,总比崩了好
    QMessageBox::information(this, "无需确认",
        QString("这条报警已经是「%1」状态。").arg(statusItem->text()));
}

// ★ IMPROVE-5.5(2026-08-04):全部清除 → 清空表格 + 停闪烁
//   【改法】alarmMgr clearAll → emit allCleared → 本槽清 alarmTable + 停 timer
//   【取舍】AlarmManager 5.2 已实现 clearAll,本槽只管 UI 同步
//   【参照】onAlarmTriggered 的反向操作
void hmi::onAllAlarmsCleared() {
    ui.alarmTable->setRowCount(0);
    alarmBlinkTimer_->stop();
    // ★ 2026-10-14:相位必须复位。清表后如果相位还停在"亮相",
    //   下一条新报警的第一帧就是浅色,要等 500ms 才切回本档色 ——
    //   用户看到的是"新报警颜色不对,过一会儿才对"。
    alarmBlinkOn_ = false;
    qCDebug(hmiAlarm) << "[Alarm 全部清除]";
}

// ==================== ★ 阶段 C · C.4(2026-09-18):协议选择 UI ====================
//   教训 1.5:不允许跨协议切工程 — Modbus 地址 0x0002 在 S7 是 DBW2,语义完全不同
//   教训 2026-09-17 4.1:状态/迁移要给 1 个现实类比(让 Wqian 秒懂)
//   规划 C.4 第 316-348 行:协议选择 UX 规则锁定
// ★ 撤回下拉框到当前协议那一项(声明处的注释解释了为什么不能直接 findText)
void hmi::revertProtocolCombo() {
    ui.protocolComboBox->blockSignals(true);
    QStringList texts;
    texts.reserve(ui.protocolComboBox->count());
    for (int i = 0; i < ui.protocolComboBox->count(); ++i) {
        texts << ui.protocolComboBox->itemText(i);
    }
    const int idx = qwhmi::drivers::findProtocolItemIndex(texts, currentProtocol_);
    if (idx < 0) {
        // ★ 找不到必须说出来。原来这里是静默的 if(idx >= 0),
        //   下拉框停在错的那一项上,而 currentProtocol_ 还是旧的 ——
        //   界面和实际连的东西对不上,却没有任何日志。
        qWarning() << "[C.4] 撤回协议下拉框失败:找不到与"
                   << currentProtocol_ << "匹配的项目,"
                   << "下拉框现在停在:" << ui.protocolComboBox->currentText();
    } else {
        ui.protocolComboBox->setCurrentIndex(idx);
    }
    ui.protocolComboBox->blockSignals(false);
}

void hmi::onProtocolComboChanged(const QString& protocolName) {
    const QString normalized = protocolName.trimmed().toLower();
    const QString current = currentProtocol_;

    // ★ C.4:用 ProtocolGuard 纯逻辑判断(可单测,不耦合 GUI)
    //   教训 1.5:跨协议切工程 → 弹错误 + audit 记录 rejected + 撤销下拉框
    if (!qwhmi::drivers::isCrossProtocolChange(current, normalized)) {
        qCDebug(hmiComm) << "[C.4] 协议未变:" << protocolName;
        return;
    }

    // ★★ 2026-10-10:先分清"这个协议没实现"和"实现了但不能跨协议切"。
    //
    //   【为什么要分开】原来一律弹"协议不兼容,不能切换,请创建新工程"。
    //   用户选 S7 得到的答复是"你得换个工程" —— 而真相是"这东西还没写"。
    //   按前者去做的人会白建一个工程,然后发现还是连不上。
    //   这是同一个"界面在撒谎"病的第二种表现:措辞把用户往错误方向带。
    if (!qwhmi::drivers::DriverFactory::isUsable(protocolName)) {
        const QString reason = qwhmi::drivers::DriverFactory::unavailableReason(protocolName);
        QMessageBox::information(this, QStringLiteral("协议尚未实现"), reason);
        qwhmi::auth::AuditLog::instance().append(
            QStringLiteral("protocol.change"),
            QStringLiteral("system"),
            QStringLiteral("system"),
            current,
            QStringLiteral("%1→%2").arg(current, normalized),
            QStringLiteral("unimplemented"));
        // 同样要撤回下拉框 —— 用户没能切过去
        revertProtocolCombo();
        return;
    }

    // ★ 跨协议切换:弹错误对话框 + AuditLog 记录 rejected + 不更新 currentProtocol_
    QMessageBox::warning(
        this,
        QStringLiteral("协议不兼容"),
        QStringLiteral("当前工程使用 %1,不能切换到 %2。\n\n"
                       "%1 地址(如 0x0002)在 %2 中没有对应语义。\n\n"
                       "请创建新工程。")
            .arg(current.toUpper(), protocolName),
        QMessageBox::Ok);

    // ★ C.4.2:AuditLog 记录协议切换拒绝事件
    //   action=protocol.change detail=modbus→s7 result=rejected
    qwhmi::auth::AuditLog::instance().append(
        QStringLiteral("protocol.change"),
        QStringLiteral("system"),
        QStringLiteral("system"),
        current,
        QStringLiteral("%1→%2").arg(current, normalized),
        QStringLiteral("rejected"));

    qWarning() << "[C.4] 跨协议切工程被阻止:" << current << "→" << protocolName;

    // 撤销下拉框变更(用户没成功切)
    revertProtocolCombo();
}
