#include "hmi.h"
#include "Variable.h"             // 阶段 3.2:Variable 数据模型
#include "VariableManager.h"      // 阶段 3.2:Variable 容器
#include "PollScheduler.h"        // 阶段 3.3:定时轮询调度器
#include "drivers/IPollDriver.h"   // ★ C.1(2026-09-17):协议抽象层
#include "drivers/ModbusDriver.h"  // ★ C.1:Modbus 协议 Driver
#include "drivers/DriverFactory.h" // ★ C.1:driver 工厂
#include "drivers/ProtocolGuard.h" // ★ C.4(2026-09-18):跨协议 UX 锁纯逻辑(可单测)
#include "history/HistoryRecorder.h" // ★ D.1(2026-09-18):SQLite 历史库
#include "history/AlarmRecorder.h"  // ★ D.3(2026-09-20):报警持久化(alarm_history 表)
#include "widgets/TrendWidget.h"     // ★ D.2(2026-09-20):历史曲线控件(QCustomPlot)
#include "widgets/LabelControl.h"    // ★ E.1a(2026-09-20):6 显示控件
#include "widgets/NumberDisplay.h"
#include "widgets/TextDisplay.h"
#include "widgets/BarControl.h"
#include "widgets/GaugeControl.h"
#include "widgets/LedControl.h"
#include "export/CsvExporter.h"      // ★ D.4(2026-09-20):CSV 导出(QTextStream)

#include <QGraphicsView>            // ★ E.1a demo 画布
#include <QGraphicsScene>
#include <QGraphicsSimpleTextItem>
#include "ThresholdAlarmChecker.h" // 阶段 5.3-收尾(2026-08-07):合并 High/Low + 最小持续时间(DRY)
#include "WriteFailChecker.h"     // 阶段 4.3.x:写失败转报警(2026-08-05 Bug #2 修复)
#include "ActionManager.h"        // 阶段 5.6:报警联动动作(关阀/停机)
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
#include <QColor>            // 阶段 5.4:严重级报警闪烁(红/橙背景色)
#include <QFile>             // ★ 阶段 6.3 DEBUG:写日志文件(load_debug.log)
#include <QFileDialog>       // ★ 阶段 6.4:UI 打开工程 → 选 .json 文件
#include <QStandardPaths>    // ★ D.4.1:CSV 导出默认路径(桌面)
#include <QMenu>             // 阶段 5.5:alarmTable 右键菜单

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

    // ★ D.4.1(2026-09-20):D.4 CSV 导出按钮 — QFileDialog → CsvExporter
    connect(ui.exportVariablesBtn, &QPushButton::clicked, this, &hmi::onExportVariablesClicked);
    connect(ui.exportAlarmsBtn,    &QPushButton::clicked, this, &hmi::onExportAlarmsClicked);

    // ★ E.1a 演示画布(2026-09-20)— 1-2 h 临时 demo,挂 6 控件到主窗口
    //   hmi.ui 没改;程序化建按钮 + 绑 onClicked
    auto* demoBtn = new QPushButton(QStringLiteral("演示画布"), this);
    demoBtn->setMinimumSize(80, 32);
    ui.buttonRowLayout->addWidget(demoBtn);
    connect(demoBtn, &QPushButton::clicked, this, &hmi::onDemoCanvasClicked);

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
        qDebug() << "重连放弃(5 次重试已用完),需手动重连";
    });

    // TCP 控件按钮(注意:这些按钮的槽函数名都不是 on_<obj>_<signal> 格式,
    // 不会触发自动连接,只能靠上面这堆 connect 把信号接上)
	connect(ui.connectBtn, &QPushButton::clicked, this, &hmi::onConnectClicked); // 当 connectBtn 被点击时调用 hmi::onConnectClicked
	connect(ui.disconnectBtn, &QPushButton::clicked, this, &hmi::onDisconnectClicked);// 当 disconnectBtn 被点击时调用 hmi::onDisconnectClicked
	connect(ui.sendBtn, &QPushButton::clicked, this, &hmi::onSendClicked);//      当 sendBtn 被点击时调用 hmi::onSendClicked

    // ★ 阶段 C · C.4(2026-09-18):协议选择下拉框信号(教训 1.5 跨协议 UX 锁)
    connect(ui.protocolComboBox, &QComboBox::currentTextChanged,
            this, &hmi::onProtocolComboChanged);

    // ==================== 阶段 2 第 4 练:重设计 — Qt 统一接口 ====================
    // ★ C.1(2026-09-17):ModbusMaster 包装为 ModbusDriver(IPollDriver*)
    //   100% 行为兼容(后面 PollScheduler 直接换 IPollDriver* 不破现有逻辑)
    modbus_ = qwhmi::drivers::DriverFactory::create("modbus", this);
    auto* modbusDriver = qobject_cast<qwhmi::drivers::ModbusDriver*>(modbus_);

    // 2026-07-07 重设计:连接 / 错误 信号(同阶段 2-3 风格)
    // ★ C.1(2026-09-17):用 IPollDriver 通用信号(由 ModbusDriver 转发自 ModbusMaster)
    connect(modbus_, &qwhmi::drivers::IPollDriver::connected, this, [this](){
        modbusState_ = ModbusState::Connected;
        // ★ BUGFIX(2026-07-28):重连时清 PollScheduler 残留 in-flight -1,避免 Variable 永远不读
        if (pollScheduler_) pollScheduler_->resetAll();
        // 连接刚建好,如果之前有"待发"请求 → 现在发(消除 500ms hack)
        if (hasPendingModbus_) {
            executePendingModbus();
        }
        updateUi();
    });
    connect(modbus_, &qwhmi::drivers::IPollDriver::disconnected, this, [this](){
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
    connect(modbus_, &qwhmi::drivers::IPollDriver::errorOccurred, this,
        [this](QString msg){
            qDebug() << "Modbus 错误(伴随状态):" << msg;
            modbusState_ = ModbusState::Failed;
            updateUi();
            // 2026-07-09:协议/网络错误也弹窗 —— 之前只 qDebug 太隐了
            QMessageBox::warning(this, "Modbus 错误", msg);
        });
    // 2026-07-09:UI 防抖 —— pending_ 变空时重新启用发送按钮
    // ★ C.1:idle 是 ModbusMaster 特有,需要从 ModbusDriver.inner() 取
    connect(modbusDriver->inner(), &ModbusMaster::idle, this, [this](){
        ui.modbusSendBtn->setEnabled(true);
        statusBar()->showMessage("Modbus 空闲,可发下一帧", kStatusBarShortMs);  // ★ IMPROVE-B4
    });

    // 2026-07-07 重设计:通用「发送」按钮 — 靠 fcCombo 区分读/写
    connect(ui.modbusSendBtn, &QPushButton::clicked, this, [this](){
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
    pollScheduler_ = new PollScheduler(varMgr_, modbus_, this);   // ★ C.1:modbus_ 现在是 IPollDriver*
    pollScheduler_->start();
    // ★ 阶段 6.x.1(2026-08-11 Wqian 拍板)分级轮询 —— 单窗口场景初始化
    //   单窗口场景:所有 var 都在 UI 中,所以全部激活
    //   未来多屏幕(主窗口 / 趋势页 / 报警页)可ef调 addActiveVariable/removeActiveVariable
    pollScheduler_->setAllActive();

    // 阶段 4.3.0:启动写调度(与 PollScheduler 对称,处理 Variable.dirty_ → Modbus 写 PLC)
    writeScheduler_ = new WriteScheduler(varMgr_, modbusDriver->inner(), this);
    //                  ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
    //   ★ C.1:WriteScheduler 保留 ModbusMaster* 参数(写调度仍是 Modbus-specific),
    //   从 ModbusDriver.inner() 拿 ModbusMaster* 给它用
    //   未来 C.4 协议 UI 切到 S7 时,WriteScheduler 也得协议无关化(单独一个里程碑)
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
    ui.alarmTable->horizontalHeader()->setStretchLastSection(true);

    // 阶段 5.4:接 AlarmManager 信号(槽函数实现见文件末尾)
    connect(alarmMgr_, &AlarmManager::alarmTriggered, this, &hmi::onAlarmTriggered);
    connect(alarmMgr_, &AlarmManager::alarmRecovered, this, &hmi::onAlarmRecovered);

    // 阶段 5.5(2026-08-04):报警确认(右键菜单)
    ui.alarmTable->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui.alarmTable, &QWidget::customContextMenuRequested,
            this, &hmi::onAlarmContextMenu);

    alarmContextMenu_ = new QMenu(this);
    alarmContextMenu_->addAction("确认", this, &hmi::onAlarmAcknowledge);

    // 阶段 5.5 BUGFIX:报警已确认 → 表格行"未确认"→"已确认"(停闪),用 ID 匹配避免同名报警错配
    connect(alarmMgr_, &AlarmManager::alarmAcknowledged, this,
        [this](qint64 id){
            for (int row = 0; row < ui.alarmTable->rowCount(); ++row) {
                auto* item = ui.alarmTable->item(row, 1);
                if (item && item->data(Qt::UserRole).toLongLong() == id) {
                    auto* statusItem = ui.alarmTable->item(row, 4);
                    if (statusItem) statusItem->setText("已确认");
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

        // ★ 6.5 收尾(2026-08-08):loadFromFile 后同步 modbus 用新 host/port
        //   【背景】6.3 改了 host/port 字段,但没真正应用 — 切工程后 TCP 已切,modbus 还连着旧 IP
        //   【改法】loaded 信号后从 tcp_ 拿 host/port,主动 connectTo 同步
        //   【取舍】正在通讯时 loadFromFile 会短暂断连,但用户主动打开工程 = 期望切换,合理
        if (tcp_ && modbus_) {
            modbus_->connectTo(tcp_->host(), tcp_->port());
            qDebug() << "[hmi] loadFromFile 同步 modbus →" << tcp_->host() << ":" << tcp_->port();
        }
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
            qDebug() << "[hmi] default.json 加载成功;path=" << defaultPath;
        } else {
            qDebug() << "[hmi] default.json 加载失败,用代码默认配置;path=" << defaultPath;
        }
    } else {
        qDebug() << "[hmi] default.json 找不到,候选路径:" << defaultPathCandidates;
    }

    // ★ 阶段 6.2(2026-08-07):UI 保存按钮 → onSaveClicked(手动 connect,
    //   因为 onSaveClicked 不符合 on_<objectName>_<signal>() 自动连接命名)
    connect(ui.saveButton, &QPushButton::clicked, this, &hmi::onSaveClicked);

    // ★ 阶段 6.4(2026-08-08):UI 打开工程按钮 → onOpenClicked(对称 saveButton)
    connect(ui.openButton, &QPushButton::clicked, this, &hmi::onOpenClicked);



    // 阶段 4.x 写审计日志(2026-07-28):配置 auditTable + 接 WriteScheduler 信号
    ui.auditTable->setColumnCount(4);
    ui.auditTable->setHorizontalHeaderLabels({"时间", "Variable", "新值", "结果/地址"});
    ui.auditTable->horizontalHeader()->setStretchLastSection(true);
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
    //   connectBtn/disconnectBtn 不绑:任何角色都要能连 PLC 看数据
    //   speedWriteEdit 是 QLineEdit 不是按钮,B3 在回车时拦
    //   托盘"用户管理"在 SystemTray.cpp 里 bindActionEnable(ManageUsers)
    qwhmi::auth::bindButtonEnable(ui.startButton, qwhmi::auth::Permission::WriteVariable);
    qwhmi::auth::bindButtonEnable(ui.stopButton,  qwhmi::auth::Permission::WriteVariable);
    qwhmi::auth::bindButtonEnable(ui.resetButton, qwhmi::auth::Permission::WriteVariable);
    qwhmi::auth::bindButtonEnable(ui.saveButton,  qwhmi::auth::Permission::EditProject);
    qwhmi::auth::bindButtonEnable(ui.openButton,  qwhmi::auth::Permission::EditProject);
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
    ui.modbusSendBtn->setEnabled(false);
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
    alarmMgr_->acknowledgeAll();
}

void hmi::onResetFinished() {
    // 阶段 5.1 改造:3 秒定时器到,从 Resetting 触发转 Stopped(走 tryTransition)
    // 注:Fault → Stopped 不经过 Resetting,所以不会触发 onResetFinished
    tryTransition(Trigger::ResetTimerFinished);
}

// ★ D.4.1(2026-09-20):D.4 CSV 导出 UI 集成
//   默认文件名带时间戳,避免覆盖前次导出
//   from/to 用最近 1 天(后续可加日期选择对话框;先做 MVP)
// ★ E.1a 演示画布(2026-09-20):弹独立 QGraphicsView 窗口 + 6 控件 + 绑现有 Variable
//   临时 demo,后续 E.4 WYSIWYG 完整编辑器替换
//   点击"演示画布"按钮 → 切换显示/隐藏 demoWindow_
void hmi::onDemoCanvasClicked() {
    static QWidget* demoWindow_ = nullptr;       // 进程内单例
    if (demoWindow_ && demoWindow_->isVisible()) {
        demoWindow_->hide();
        return;
    }
    if (!demoWindow_) {
        demoWindow_ = new QWidget(nullptr, Qt::Window);   // 独立顶层窗口
        demoWindow_->setWindowTitle("E.1a 演示画布 — 6 控件实时数据");
        demoWindow_->resize(900, 600);

        auto* view = new QGraphicsView(demoWindow_);
        auto* scene = new QGraphicsScene(view);
        scene->setSceneRect(0, 0, 880, 580);
        view->setScene(scene);
        view->setRenderHint(QPainter::Antialiasing);
        view->setBackgroundBrush(QColor(245, 245, 245));

        // ★ 6 控件:3 显示 3 演示(放画布上)
        using namespace qwhmi::widgets;
        // 1. LabelControl 静态文字
        auto* lbl = new LabelControl;
        lbl->setPosition({20, 20});
        lbl->setVariableName("plc1.demo");
        lbl->setValue(QStringLiteral("演示画布 — E.1a"));
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
    qDebug() << "counter:" << counter;
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
    // 2026-07-21 同步连 modbus_(以前只连 TCP 客户端,导致 PollScheduler 一直报未连接)
    modbus_->connectTo(host, port);
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
    qDebug() << "TCP 错误(不独立决定 UI):" << msg;
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
        ui.stateLabel->setStyleSheet(
            "background-color: green; color: white; "
            "font-size: 18px; font-weight: bold;");
        break;
    case State::Stopped:
        timer->stop();
        ui.stateLabel->setText("状态:已停止");
        ui.stateLabel->setStyleSheet(
            "background-color: lightgray; color: black; "
            "font-size: 18px; font-weight: bold;");
        break;
    case State::Resetting:
        timer->stop();
        ui.stateLabel->setText("状态:复位中");
        ui.stateLabel->setStyleSheet(
            "background-color: yellow; color: black; "
            "font-size: 18px; font-weight: bold;");
        counter = 0; // 复位时把 counter 也重置(保留原逻辑)
        break;
    case State::Fault:
        // 阶段 5.1(2026-08-03 Wqian 拍板):Fault 态 UI(严重报警触发)
        //   timer 停(进入 Fault 不再跑 counter)
        //   红底白字(跟 ConnState::Failed 一致风格)
        //   注:5.3 接入报警系统后会加闪烁(QTimer 周期切背景色)
        timer->stop();
        ui.stateLabel->setText("状态:故障");
        ui.stateLabel->setStyleSheet(
            "background-color: red; color: white; "
            "font-size: 18px; font-weight: bold;");
        break;
    }
}

// 网络侧 connLabel(只跟 connState_ 有关)— 文字 + 重连进度
void hmi::updateConnLabel() {
    switch (connState_) {
    case ConnState::Disconnected:
        ui.connLabel->setText("状态:已断开");
        ui.connLabel->setStyleSheet("background-color: lightgray; color: black; font-weight: bold;");
        break;
    case ConnState::Connecting:
        ui.connLabel->setText("状态:连接中...");
        ui.connLabel->setStyleSheet("background-color: yellow; color: black; font-weight: bold;");
        break;
    case ConnState::Connected:
        ui.connLabel->setText("状态:已连接");
        ui.connLabel->setStyleSheet("background-color: green; color: white; font-weight: bold;");
        break;
    case ConnState::Failed:
        ui.connLabel->setText("状态:连接失败(超时或网络错误)");
        ui.connLabel->setStyleSheet("background-color: red; color: white; font-weight: bold;");
        break;
    case ConnState::Reconnecting:
        ui.connLabel->setText(QString("状态:重连中(%1/%2),%3 秒后重试")
            .arg(reconnectAttempt_).arg(reconnectMax_).arg(reconnectDelayMs_ / 1000));
        ui.connLabel->setStyleSheet("background-color: orange; color: white; font-weight: bold;");
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
    //   导致 Viewer 连了 PLC 后 startButton/stopButton/resetButton 变亮(saveButton/openButton
    //   不在 updateButtons 里所以 B4 生效)。修法:加 canWrite 闸门,跟 B4 形成 AND。
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

    // 网络按钮:Reconnecting 时 connectBtn 灰掉,disconnectBtn 仍可点(用户取消)
    //   不加 canWrite:Viewer 也要能连 PLC 看数据(B4 设计如此)
    ui.connectBtn->setEnabled(tcpIdle);
    ui.disconnectBtn->setEnabled(!tcpIdle || isReconnecting);

    // 发送按钮:必须已连(断网时灰掉避免发不出去)
    ui.sendBtn->setEnabled(tcpReady);
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
    // ★ 阶段 6.4 加强(2026-08-08):QFileDialog 选保存路径(跟 openButton 对称)
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

// ★ 阶段 6.4(2026-08-08):UI 打开工程按钮槽函数 → 弹 QFileDialog 选 .json → loadFromFile
//   【背景】6.4 把"打开工程"这条 UI 入口补上,跟 saveButton 对称
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
    qDebug() << "[Alarm 触发]" << a.name << "level=" << static_cast<int>(a.level)
             << "value=" << a.triggerValue << "threshold=" << a.threshold;
    tryTransition(Trigger::AlarmCritical);   // 推状态机进 Fault

    int row = ui.alarmTable->rowCount();
    ui.alarmTable->insertRow(row);
    ui.alarmTable->setItem(row, 0, new QTableWidgetItem(a.triggeredAt.toString("HH:mm:ss")));
    auto* nameItem = new QTableWidgetItem(a.name);
    nameItem->setData(Qt::UserRole, a.id_);  // ★ 存 ID 给 ack lambda 匹配(5.5 BUGFIX)
    ui.alarmTable->setItem(row, 1, nameItem);
    ui.alarmTable->setItem(row, 2, new QTableWidgetItem(alarmLevelName(a.level)));
    ui.alarmTable->setItem(row, 3, new QTableWidgetItem(a.triggerValue));
    ui.alarmTable->setItem(row, 4, new QTableWidgetItem("未确认"));
    ui.alarmTable->scrollToBottom();

    if (!alarmBlinkTimer_->isActive()) alarmBlinkTimer_->start();
}

// ★ BUGFIX-5.5(2026-08-04 Wqian 反馈):报警恢复 → 用 ID 匹配找原行
//   【背景】原按 name 匹配会错配同名报警,改成 ID 唯一匹配
//   【改法】Qt::UserRole 存 ID,scan alarmTable 按 ID 匹配,更新"状态"列文字 → "已恢复"
//   【取舍】没用 model,直接扫行;5.5 数据量小(< 100 条)够用
void hmi::onAlarmRecovered(qint64 id) {
    qDebug() << "[Alarm 恢复]" << id;
    // 恢复不直接转 Stopped — Fault 态只能 ResetButton 清(状态机规则)

    for (int row = 0; row < ui.alarmTable->rowCount(); ++row) {
        auto* item = ui.alarmTable->item(row, 1);
        if (item && item->data(Qt::UserRole).toLongLong() == id) {
            auto* statusItem = ui.alarmTable->item(row, 4);
            if (statusItem) statusItem->setText("已恢复");
            break;
        }
    }
    if (alarmMgr_->count() == 0) alarmBlinkTimer_->stop();
}

// ★ IMPROVE-5.5(2026-08-04):严重级报警闪烁 + 统一背景管理
//   【背景】5.5 BUGFIX(Wqian 反馈):"已恢复"/"已确认"的行偶尔还显示红色,残留闪烁背景
//   【改法】blink tick 是背景唯一权威:
//     - "未确认" + 严重/危急级 → 切红/橙
//     - 其他(已确认/已恢复/信息/警告)→ 清背景(默认)
//   【取舍】每 tick 都清非活动行的背景(浪费但轻量);保证任何代码路径改状态后,500ms 内清干净
//   【参照】Wqian 2026-08-04 反馈
void hmi::onAlarmBlinkTick() {
    static bool blinkOn = false;
    blinkOn = !blinkOn;
    const QColor activeBg = blinkOn ? QColor(255, 80, 80) : QColor(255, 200, 100);  // 红/橙
    for (int row = 0; row < ui.alarmTable->rowCount(); ++row) {
        auto* levelItem = ui.alarmTable->item(row, 2);
        auto* statusItem = ui.alarmTable->item(row, 4);
        if (!levelItem || !statusItem) continue;
        const QString status = statusItem->text();
        const QString levelText = levelItem->text();

        if (status == "未确认" && (levelText == "严重" || levelText == "危急")) {
            // 未确认 + 严重级 → 切红/橙背景
            for (int col = 0; col < ui.alarmTable->columnCount(); ++col) {
                auto* item = ui.alarmTable->item(row, col);
                if (item) item->setBackground(activeBg);
            }
        } else {
            // 其他(已确认/已恢复/信息/警告)→ 清背景,不留残色
            for (int col = 0; col < ui.alarmTable->columnCount(); ++col) {
                auto* item = ui.alarmTable->item(row, col);
                if (item) item->setData(Qt::BackgroundRole, QVariant());
            }
        }
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
    auto* statusItem = ui.alarmTable->item(row, 4);
    // ★ 5.5:只有"未确认"才能确认(灰掉菜单项)
    if (statusItem) {
        bool canAck = (statusItem->text() == "未确认");
        alarmContextMenu_->actions().first()->setEnabled(canAck);
    }
    // 选中当前行,菜单关闭后 onAlarmAcknowledge 用 currentRow() 拿
    ui.alarmTable->setCurrentCell(row, 0);
    alarmContextMenu_->exec(ui.alarmTable->viewport()->mapToGlobal(pos));
}

// ★ BUGFIX-5.5(2026-08-04 Wqian 反馈):右键菜单"确认"槽 → 用 ID 调 acknowledge
//   【背景】原按 name 调 acknowledge,但同名报警存在时多个 Alarm 共享 name,错配
//   【改法】从 Qt::UserRole 读 ID → alarmMgr_->acknowledge(id)
//   【取舍】每个 Alarm 唯一 ID(QDateTime::currentMSecsSinceEpoch()),保证不重不漏
//   【参照】5.5 BUGFIX:AlarmManager.activeAlarms_ 改用 ID 索引
void hmi::onAlarmAcknowledge() {
    int row = ui.alarmTable->currentRow();
    if (row < 0) return;
    auto* nameItem = ui.alarmTable->item(row, 1);
    if (!nameItem) return;
    qint64 id = nameItem->data(Qt::UserRole).toLongLong();
    alarmMgr_->acknowledge(id);
    qDebug() << "[Alarm 确认]" << nameItem->text() << "id=" << id;
}

// ★ IMPROVE-5.5(2026-08-04):全部清除 → 清空表格 + 停闪烁
//   【改法】alarmMgr clearAll → emit allCleared → 本槽清 alarmTable + 停 timer
//   【取舍】AlarmManager 5.2 已实现 clearAll,本槽只管 UI 同步
//   【参照】onAlarmTriggered 的反向操作
void hmi::onAllAlarmsCleared() {
    ui.alarmTable->setRowCount(0);
    alarmBlinkTimer_->stop();
    qDebug() << "[Alarm 全部清除]";
}

// ==================== ★ 阶段 C · C.4(2026-09-18):协议选择 UI ====================
//   教训 1.5:不允许跨协议切工程 — Modbus 地址 0x0002 在 S7 是 DBW2,语义完全不同
//   教训 2026-09-17 4.1:状态/迁移要给 1 个现实类比(让 Wqian 秒懂)
//   规划 C.4 第 316-348 行:协议选择 UX 规则锁定
void hmi::onProtocolComboChanged(const QString& protocolName) {
    const QString normalized = protocolName.trimmed().toLower();
    const QString current = currentProtocol_;

    // ★ C.4:用 ProtocolGuard 纯逻辑判断(可单测,不耦合 GUI)
    //   教训 1.5:跨协议切工程 → 弹错误 + audit 记录 rejected + 撤销下拉框
    if (!qwhmi::drivers::isCrossProtocolChange(current, normalized)) {
        qDebug() << "[C.4] 协议未变:" << protocolName;
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
    ui.protocolComboBox->blockSignals(true);
    const int idx = ui.protocolComboBox->findText(current, Qt::MatchFixedString);
    if (idx >= 0) {
        ui.protocolComboBox->setCurrentIndex(idx);
    }
    ui.protocolComboBox->blockSignals(false);
}
