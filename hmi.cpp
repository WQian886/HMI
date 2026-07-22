#include "hmi.h"
#include "Variable.h"             // 阶段 3.2:Variable 数据模型
#include "VariableManager.h"      // 阶段 3.2:Variable 容器
#include "PollScheduler.h"        // 阶段 3.3:定时轮询调度器
#include <QIntValidator>
#include <QMessageBox>
#include <QRegularExpression>
#include <QRegularExpressionValidator>

// 2026-07-13 阶段 2 第 6 练:parseValueList 前向声明(定义在文件底部;默认值在定义里)
QVector<quint16> parseValueList(const QString& text, QString* errOut);

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
	resetTimer_->setInterval(3000);     // 设置触发间隔为 3 秒
	connect(resetTimer_, &QTimer::timeout, this, &hmi::onResetFinished);// 手写 connect:当 resetTimer_ 超时时调用 hmi::onResetFinished

    // 阶段 0 第 2 练:三按钮手写 connect(改用显式 connect,不再依赖 connectSlotsByName)
	connect(ui.startButton, &QPushButton::clicked, this, &hmi::onStartClicked);// 当 startButton 被点击时调用 hmi::onStartClicked
	connect(ui.stopButton, &QPushButton::clicked, this, &hmi::onStopClicked);// 当 stopButton 被点击时调用 hmi::onStopClicked
	connect(ui.resetButton, &QPushButton::clicked, this, &hmi::onResetClicked);//   当 resetButton 被点击时调用 hmi::onResetClicked

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

    // ==================== 阶段 2 第 4 练:重设计 — Qt 统一接口 ====================
    modbus_ = new ModbusMaster(this);

    // 2026-07-07 重设计:连接 / 错误 信号(同阶段 2-3 风格)
    connect(modbus_, &ModbusMaster::connected, this, [this](){
        modbusState_ = ModbusState::Connected;
        // 连接刚建好,如果之前有"待发"请求 → 现在发(消除 500ms hack)
        if (hasPendingModbus_) {
            executePendingModbus();
        }
        updateUi();
    });
    connect(modbus_, &ModbusMaster::disconnected, this, [this](){
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
    connect(modbus_, &ModbusMaster::errorOccurred, this,
        [this](QString msg){
            qDebug() << "Modbus 错误(伴随状态):" << msg;
            modbusState_ = ModbusState::Failed;
            updateUi();
            // 2026-07-09:协议/网络错误也弹窗 —— 之前只 qDebug 太隐了
            QMessageBox::warning(this, "Modbus 错误", msg);
        });
    // 2026-07-09:UI 防抖 —— pending_ 变空时重新启用发送按钮
    // (正常响应 / 异常响应 / 超时 / 断开 都会 emit idle)
    connect(modbus_, &ModbusMaster::idle, this, [this](){
        ui.modbusSendBtn->setEnabled(true);
        statusBar()->showMessage("Modbus 空闲,可发下一帧", 1500);
    });

    // 2026-07-07 重设计:通用「发送」按钮 — 靠 fcCombo 区分读/写
    connect(ui.modbusSendBtn, &QPushButton::clicked, this, [this](){
        QString host = ui.ipEdit->text().trimmed();
        bool ok;
        quint16 port = ui.portEdit->text().toUShort(&ok);
        if (host.isEmpty() || !ok) {
            statusBar()->showMessage("IP 或端口不能为空", 3000);
            return;
        }
        int slaveId = ui.modbusSlaveEdit->text().toInt(&ok);
        int addr    = ui.modbusAddrEdit->text().toInt(&ok);
        if (!ok || slaveId < 0 || slaveId > 255) {
            statusBar()->showMessage("slaveId 必须 0-255", 3000);
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
                    statusBar()->showMessage(err.isEmpty() ? "写值不能为空" : err, 3000);
                    return;
                }
                pendingModbus_.values = vals;  // 整个 list(单值就是 size=1)
            } else {
                pendingModbus_.count = ui.modbusDataEdit->text().toInt(&ok);
                if (!ok || pendingModbus_.count < 1 || pendingModbus_.count > 125) {
                    statusBar()->showMessage("读数量必须 1-125", 3000);
                    return;
                }
            }
            updateUi();
            modbus_->connectTo(host, port);
            // 2026-07-09:延长到 5s,失败的话用户盯着屏幕等结果
            statusBar()->showMessage("正在连接 Modbus...(失败会弹窗提示)", 5000);
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
    speed->setDeadband(5.0);        // ±5 内吞掉(Float 也可,这里 Int16 转 double 后 5 单位合理)

    // 阶段 4.1:Binding 框架 — 替 onVariableChanged 手工 if/else,加新变量只需写一行
    Binding::bind(ui.pressureLabel,    pressure,     "压力(bar): %1");
    Binding::bind(ui.temperatureLabel, temperature,  "温度(℃): %1");
    Binding::bind(ui.speedLabel,       speed,        "转速(rpm): %1");

    // 阶段 3.3:启动定时轮询(替代 demoTimer 的 random 数据,真 modbus 读 PLC)
    pollScheduler_ = new PollScheduler(varMgr_, modbus_, this);
    pollScheduler_->start();
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
    bool ok;
    QModbusReply* reply = nullptr;

    if (isRead) {
        int count = pendingModbus_.isRead == false ? 1 : pendingModbus_.count;
        if (hasPendingModbus_ && pendingModbus_.isRead) count = pendingModbus_.count;
        // 读
        QModbusDataUnit unit(type, addr, static_cast<quint16>(count));
        reply = modbus_->sendReadRequest(unit, slaveId);
    } else {
        // 2026-07-13 阶段 2 第 6 练:写多值(逗号分隔)
        QVector<quint16> vals;
        if (hasPendingModbus_ && !pendingModbus_.isRead) {
            vals = pendingModbus_.values;  // 来自点击时的排队
        } else {
            QString err;
            vals = parseValueList(ui.modbusDataEdit->text(), &err);
            if (vals.isEmpty()) {
                statusBar()->showMessage(err.isEmpty() ? "写值不能为空" : err, 3000);
                return;
            }
        }
        QModbusDataUnit unit(type, addr, vals);
        reply = modbus_->sendWriteRequest(unit, slaveId);
    }

    if (!reply) return;
    // 2026-07-09:UI 防抖 —— 请求已发,按钮变灰,等 idle 信号再亮
    ui.modbusSendBtn->setEnabled(false);
    statusBar()->showMessage("Modbus 请求中...", 1500);

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

// ==================== 阶段 0 第 2 练:业务三键槽 ====================
// 只改 state(事实),然后调 updateUi() 推 UI —— 不直接动 UI
void hmi::onStartClicked()  { state = State::Running; updateUi(); }
void hmi::onStopClicked()   { state = State::Stopped; updateUi(); }
void hmi::onResetClicked() {
    state = State::Resetting;
    updateUi();
    resetTimer_->start();  // 启动 3 秒单次定时器,到点 onResetFinished 自动转 Stopped
}

void hmi::onResetFinished() {
    // 复位 3 秒到,自动转 Stopped(用户可以再按 [启动] / [复位])
    state = State::Stopped;
    updateUi();
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
		statusBar()->showMessage("IP 或端口不能为空", 3000);// 3000 毫秒后自动清除提示
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
		statusBar()->showMessage("发送框为空", 3000);// 3000 毫秒后自动清除提示
		return;// [FIX #6] 发送前校验:空
    }
	QString hex = text.remove(' ');// 去掉空格
	QByteArray bytes = QByteArray::fromHex(hex.toLatin1());// 从十六进制字符串转换为字节数组
	if (bytes.isEmpty()) {// 发送内容必须是合法的十六进制
		statusBar()->showMessage("发送内容不是合法 hex", 3000); // 3000 毫秒后自动清除提示
        return;
    }
	if (!tcp_->isConnected()) {// 只能在连接状态发送
		statusBar()->showMessage("未连接,无法发送", 3000);// 3000 毫秒后自动清除提示
        return;
    }
	qint64 n = tcp_->send(bytes);// 发送数据,返回实际发送的字节数
    if (n <= 0) {
		statusBar()->showMessage(QString("发送失败,write 返回 %1").arg(n), 3000);// 3000 毫秒后自动清除提示
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
	QString hex = data.toHex(' ').toUpper();// 转成十六进制字符串,每字节用空格分隔,大写
	ui.recvText->append(prefix + " " + hex);//  在 recvText 上追加显示,加个前缀区分发送/接收
}

// 阶段 4.1:onVariableChanged 已删除,改用 Binding 框架做 UI 绑定
//   (hmi.cpp 构造函数里 3 行 Binding::bind 替代此处 30 行 if/else)

// 阶段 3.3:onDemoTick 已删除,数据来源改由 PollScheduler 周期性 modbus 读 PLC

// ==================== updateUi():唯一的 UI 渲染入口 ====================
// 由 state(业务)+ connState_(网络)共同决定所有 UI 元素的文字/颜色/enabled

void hmi::updateUi() {
    // ===== 1. 业务侧 stateLabel(只跟 state 有关)=====
    switch (state) {
    case State::Running:
        // [FIX #1] 进入 Running 才启动 timer
        timer->start(1000);
        ui.stateLabel->setText("状态:运行中");
        ui.stateLabel->setStyleSheet(
            "background-color: green; color: white; "
            "font-size: 18px; font-weight: bold;");
        // [FIX #2] 复位时同步显示 0
        ui.numberLineEdit->setText(QString::number(counter));
        break;
    case State::Stopped:
        // [FIX #1] 离开 Running 立即停 timer
        timer->stop();
        ui.stateLabel->setText("状态:已停止");
        ui.stateLabel->setStyleSheet(
            "background-color: lightgray; color: black; "
            "font-size: 18px; font-weight: bold;");
        // [FIX #2] 复位时同步显示 0
        ui.numberLineEdit->setText(QString::number(counter));
        break;
    case State::Resetting:
        // [FIX #1] Reset 状态停 timer
        timer->stop();
        ui.stateLabel->setText("状态:复位中");
        ui.stateLabel->setStyleSheet(
            "background-color: yellow; color: black; "
            "font-size: 18px; font-weight: bold;");
        counter = 0; // 复位时把 counter 也重置(保留原逻辑)
        // [FIX #2] 复位时同步显示 0
        ui.numberLineEdit->setText(QString::number(counter));
        break;
    }

    // ===== 2. 网络侧 connLabel(只跟 connState_ 有关)=====
    switch (connState_) {
    case ConnState::Disconnected:
        ui.connLabel->setText("状态:已断开");
        ui.connLabel->setStyleSheet("background-color: lightgray; color: black; font-weight: bold;");
        // [FIX #2] 复位时同步显示 0
        ui.numberLineEdit->setText(QString::number(counter));
        break;
    case ConnState::Connecting:
        ui.connLabel->setText("状态:连接中...");
        ui.connLabel->setStyleSheet("background-color: yellow; color: black; font-weight: bold;");
        // [FIX #2] 复位时同步显示 0
        ui.numberLineEdit->setText(QString::number(counter));
        break;
    case ConnState::Connected:
        ui.connLabel->setText("状态:已连接");
        ui.connLabel->setStyleSheet("background-color: green; color: white; font-weight: bold;");
        // [FIX #2] 复位时同步显示 0
        ui.numberLineEdit->setText(QString::number(counter));
        break;
    case ConnState::Failed:
        // 不再是"错误:xxx",统一走 ConnState 渲染
        ui.connLabel->setText("状态:连接失败(超时或网络错误)");
        ui.connLabel->setStyleSheet("background-color: red; color: white; font-weight: bold;");
        // [FIX #2] 复位时同步显示 0
        ui.numberLineEdit->setText(QString::number(counter));
        break;
    case ConnState::Reconnecting:
        // 阶段 1 第 2 练:重连进度显示
        ui.connLabel->setText(QString("状态:重连中(%1/%2),%3 秒后重试")
            .arg(reconnectAttempt_).arg(reconnectMax_).arg(reconnectDelayMs_ / 1000));
        ui.connLabel->setStyleSheet("background-color: orange; color: white; font-weight: bold;");
        // [FIX #2] 复位时同步显示 0
        ui.numberLineEdit->setText(QString::number(counter));
        break;
    }

    // ===== 3. 按钮 enabled(由 state + connState_ 共同决定)=====
	bool tcpReady = (connState_ == ConnState::Connected); // 只有连接成功才能发送
	bool tcpIdle = (connState_ == ConnState::Disconnected || connState_ == ConnState::Failed);// 只有断开或连接失败才能点击连接

    // 业务三键:必须 TCP 连上才能用(决策 1-B)
	ui.startButton->setEnabled(tcpReady && state == State::Stopped);// 只能在已连接且当前状态是 Stopped 时才能点击 Start
	ui.stopButton->setEnabled(tcpReady && state == State::Running);// 只能在已连接且当前状态是 Running 时才能点击 Stop
	ui.resetButton->setEnabled(tcpReady && state != State::Resetting);//    只能在已连接且当前状态不是 Resetting 时才能点击 Reset

    // 网络按钮
    // 阶段 1 第 2 练:Reconnecting 时 connectBtn 灰掉(已经在重连了),disconnectBtn 仍可点(用户取消)
	bool isReconnecting = (connState_ == ConnState::Reconnecting);// 是否正在重连
    ui.connectBtn->setEnabled(tcpIdle);        // 断开/失败时可重连
    ui.disconnectBtn->setEnabled(!tcpIdle || isReconnecting);    // 连接中/已连/重连中 都可断

    // 发送按钮:必须已连(顺手收进 updateUi,断网时灰掉避免发不出去)
	ui.sendBtn->setEnabled(tcpReady);// 只有连接成功才能发送

    // ===== 4. Modbus 侧 modbusResultLabel =====
    // 2026-07-08 收敛:统一用 lastResultText_,废弃拆读写两个 label 的旧设计
    // (hmi.ui 里也只有 modbusResultLabel,没有 modbusWriteResultLabel / readHoldBtn)
    ui.modbusResultLabel->setText(lastResultText_);
}
