#include "TcpClient.h"
#include <QDebug>
#include <QtMath>  // qMin

TcpClient::TcpClient(QObject* parent) : QObject(parent) {
    socket_ = new QTcpSocket(this);
    connect(socket_, &QTcpSocket::connected,    this, &TcpClient::onSocketConnected);
    connect(socket_, &QTcpSocket::disconnected, this, &TcpClient::onSocketDisconnected);
    connect(socket_, &QTcpSocket::readyRead,    this, &TcpClient::onSocketReadyRead);
    connect(socket_, &QTcpSocket::errorOccurred,this, &TcpClient::onSocketError);

    // ★ 改善清单 #10 —— 【查过了,不能挪,清单这条是错的】
    //   清单原话是"setSocketOption 每次都设,挪到构造函数"。
    //   实测(2026-10-10):在【未连接】的 QTcpSocket 上设 LowDelayOption 是 no-op ——
    //   连上之后再读 socketOption(LowDelayOption) 拿到的仍然是 0,Qt 根本没保留这个请求。
    //   也就是说按清单挪过去 = 悄悄把"关 Nagle"关掉了,每帧小 Modbus 请求又慢 ~40ms。
    //   tst_TcpClient::lowDelayOption_mustBeSetAfterConnect 把这个事实钉住了,
    //   免得下一轮又有人照清单把它"优化"回去。
    //   设在 onSocketConnected 里是唯一实测有效的位置,保持原样。

    // 阶段 1 第 2 练:重连定时器(单次)
    reconnectTimer_ = new QTimer(this);
    reconnectTimer_->setSingleShot(true);
    connect(reconnectTimer_, &QTimer::timeout, this, &TcpClient::onReconnectTimer);

    // 阶段 1 第 2 练:心跳定时器(周期)
    heartbeatTimer_ = new QTimer(this);
    connect(heartbeatTimer_, &QTimer::timeout, this, &TcpClient::onHeartbeatTimer);

    // 阶段 1 第 3 练:连接超时定时器(单次 3s,close()/scheduleReconnect() 会 stop 掉,避免竞态)
    connectTimeoutTimer_ = new QTimer(this);
    connectTimeoutTimer_->setSingleShot(true);
    connect(connectTimeoutTimer_, &QTimer::timeout, this, [this](){
        if (socket_->state() == QAbstractSocket::ConnectingState ||
            socket_->state() == QAbstractSocket::HostLookupState) {
            qDebug() << "[TcpClient] 连接超时,强制 abort (state=" << socket_->state() << ")";
            socket_->abort();
            emit errorOccurred("连接超时(3 秒未连上)");
            // [FIX #13] Fallback: Qt 在 ConnectingState 下 abort() 触发的 disconnected/errorOccurred
            //   信号有时不能同步到达(尤其目标是局域网外/被防火墙丢包时),导致
            //   onSocketDisconnected 永远不被调 → scheduleReconnect 不跑 → UI 永远卡在"连接中"。
            //   等 50ms 看 reconnectTimer_ 有没有被启动(说明信号链通了),没通就手动推一把。
            QTimer::singleShot(50, this, [this](){
                if (!reconnectTimer_->isActive() && !userInitiated_) {
                    qDebug() << "[TcpClient] 超时 fallback:信号链断了,手动调 scheduleReconnect";
                    scheduleReconnect();
                }
            });
        }
    });
}

TcpClient::~TcpClient() = default;

void TcpClient::connectTo(const QString& host, quint16 port) {
    // ★ 改善清单 #9(2026-09-15):host 空校验
    //   【背景】空 host 调 connectToHost 会进入 ConnectingState 卡 3 秒(connect timeout),
    //          期间 UI 显示"正在连接..."但其实啥也没连,误导操作员
    //   【改法】直接 emit errorOccurred + return,不进 ConnectingState
    //   【参照】docs/改善清单.md #9
    if (host.isEmpty()) {
        qWarning() << "[TcpClient] connectTo: host 为空,拒绝";
        emit errorOccurred(QStringLiteral("host 为空"));
        return;
    }
    // 已有进行中?先 abort 干净
    if (socket_->state() != QAbstractSocket::UnconnectedState) {
        socket_->abort();
    }

    // 取消之前的重连计划 + 重置计数 + 标志位(用户主动连 → 允许后续重连)
    reconnectTimer_->stop();
    retryCount_     = 0;
    userInitiated_  = false;
    // ★ 改善清单 #2:上一轮"放弃"作废 —— 用户既然又主动连了,就得能重新退避。
    //   不清的话这台机器永远停在 Failed,用户点一百次连接也没用。
    reconnectAbandoned_ = false;

    // 存目标地址(重连要用)
    lastHost_ = host;
    lastPort_ = port;

    socket_->connectToHost(host, port);
    emit connectionAttempted();   // 通知 UI:正在连接了

    // 3 秒连接超时(用成员定时器,close() 会 stop,避免竞态)
    connectTimeoutTimer_->start(TcpClient::kConnectTimeoutMs);  // ★ IMPROVE-B4ext
}

void TcpClient::close() {
    // 标记用户主动断开 → 后续 disconnected 信号不会触发重连
    userInitiated_ = true;
    reconnectTimer_->stop();   // 取消重连计划
    connectTimeoutTimer_->stop();   // [FIX #12] 取消挂起的连接超时,避免 abort 后定时器又触发一次

    // [BUG #12] 原代码(已注释):socket_->disconnectFromHost();
    //   原因 1:ConnectingState 下 disconnectFromHost 在 Windows 上不可靠,
    //          不会真正取消正在进行的连接。
    //          → 用户点 [断开] 后 3 秒内没反应,UI 卡住
    //   原因 2:UnconnectedState(重连阶段)下 disconnectFromHost 是 no-op,
    //          不会发 disconnected 信号。
    //          → hmi 端 connState_ 永远卡在 Reconnecting
    // [FIX #12] 按 socket_->state() 分三路处理:
    //   ConnectingState  → abort()              (强制取消,会触发 disconnected)
    //   ConnectedState   → disconnectFromHost() (正常优雅断开)
    //   UnconnectedState → emit disconnected()  (不会自动发,手动补)
    auto s = socket_->state();
    if (s == QAbstractSocket::ConnectingState) {
        socket_->abort();
    } else if (s == QAbstractSocket::ConnectedState) {
        socket_->disconnectFromHost();
    } else {
        emit disconnected();
    }

    stopHeartbeat();
}
bool TcpClient::isConnected() const {
    return socket_->state() == QAbstractSocket::ConnectedState;
}

qint64 TcpClient::send(const QByteArray& data) {
    return socket_->write(data);
}

void TcpClient::onSocketConnected() {
    retryCount_      = 0;
    userInitiated_   = false;
    reconnectAbandoned_ = false;   // ★ #2:连上了,"放弃"状态作废
    lastReceivedTime_ = QTime::currentTime();   // 初始化"上次收到"时间
    // 2026-07-09:关 Nagle —— Modbus 帧小(10-20 字节),默认 Nagle 会让它等 ~40ms 合并/ACK
    // 关掉后小包立即发,响应从 ~40ms 降到 < 1ms(本机回环)
    //
    // ★ 必须在【连上之后】设(改善清单 #10 查证结论)。
    //   在未连接的 socket 上设是 no-op,Qt 不保留 —— 见构造函数里的长注释。
    socket_->setSocketOption(QAbstractSocket::LowDelayOption, 1);
    startHeartbeat();

    emit connected();
}

void TcpClient::onSocketDisconnected() {
    stopHeartbeat();

    // 主动断开 → 不重连
    if (userInitiated_) {
        emit disconnected();
        return;
    }

    // ★ Bug 修复(2026-06-18 Wqian 反馈):被动断开不 emit disconnected
    // 原因:onSocketError 已经先调了 scheduleReconnect(决定 UI 橙/红)
    //      如果这里再 emit disconnected → hmi 端 onTcpDisconnected 会把 UI 覆盖成 Disconnected(灰)
    // 修复:被动断开时,onSocketError 已经接管了所有事(状态由 scheduleReconnect 决定)
    //      主动调 scheduleReconnect(让重连机制接管)
    //
    // ★★ 2026-10-14 更正下面这行注释(原来写"靠 isActive 防重入",已过期):
    //   scheduleReconnect 里有【两道】闸,顺序是
    //     第 1 道  reconnectAbandoned_   ← 真正的第一道闸
    //     第 2 道  reconnectTimer_->isActive()
    //   2026-10-10 修 #2 之前只有第 2 道。修完之后【第 1 道才是关键】——
    //   "重试到上限放弃"那条分支故意不启动定时器,isActive() 必然为 false,
    //   单靠它挡不住紧跟其后的 disconnected。
    //   (完整推理见 scheduleReconnect 里 #2 的长注释)
    //   别照着旧注释以为"这里只受 isActive 保护" —— 那会漏掉放弃态的粘住语义。
    scheduleReconnect();
}

void TcpClient::onSocketReadyRead() {
    // 累积 → 更新时间 → 发出去 → 清空
    // [BUG #5] 原代码(已注释):buffer_.append(socket_->readAll()) (间接继承,buffer_ 每次 append 不清)
    // [FIX #5] 阶段 0/1 不做协议分帧,直接 readAll() 一次 emit 一段; 阶段 2/3 接 Modbus 时再启用 buffer_ 按帧切分
    QByteArray data = socket_->readAll();
    lastReceivedTime_ = QTime::currentTime();   // ★ 心跳超时判定靠这个
    emit readyRead(data);
    // buffer_ 字段保留,不再 clear,等在 stage 2/3 启用
}

void TcpClient::onSocketError(QAbstractSocket::SocketError err) {
    Q_UNUSED(err);
    QString msg = socket_->errorString();

    // 主动断开 → 不重连
    if (userInitiated_) {
        // ★ 主动断开,不 emit errorOccurred(由 disconnected 信号决定 UI 状态 → 灰)
        qDebug() << "主动断开(忽略 errorOccurred):" << msg;
        return;
    }

    // ★ Bug 修复(2026-06-18 Wqian 反馈):onSocketError 调 scheduleReconnect 后,不要 emit errorOccurred
    // 原因:scheduleReconnect 内部已经 emit reconnectScheduled → UI 进 Reconnecting(橙)
    //      如果再 emit errorOccurred → UI 立刻覆盖成 Failed(红),UI 会"闪红"
    // 修复:让 scheduleReconnect 决定 UI 状态(它会在重试到上限时才 emit errorOccurred)
    scheduleReconnect();

    // 错误详情走 qDebug(不进 UI)
    qDebug() << "TCP 错误(进入重连):" << msg;
}

// ==================== 阶段 1 第 2 练:重连 ====================

void TcpClient::scheduleReconnect() {
    // ★ 改善清单 #2(2026-10-10)修:"放弃"必须是一个【粘住的状态】,而不是一次性的动作。
    //
    // 【原来错在哪 —— 这是个真 bug,不是"可能"]
    //   Qt 连接失败时的信号顺序是【确定】的:errorOccurred 先,disconnected 紧跟其后,
    //   两个槽都调 scheduleReconnect。正常情况下靠下面 isActive() 挡住第二条。
    //   但"重试到上限"那条分支【故意没有启动定时器】,于是 isActive() 必然为 false,
    //   而那条分支又顺手把 retryCount_ 清成了 0 ——
    //   紧接着到达的 disconnected 看到 retryCount_=0 < maxRetries_,
    //   就【又排了第 1 次重连】,UI 从"连接失败"又跳回"重连中(1/5)"。
    //
    //   而且这不是偶发:每一轮放弃后都会重启,所以客户端永远停在"重连中",
    //   界面上"连接失败"的提示一闪而过 —— 恰好是操作员最需要看见的那句话。
    //   没有任何人按过重连按钮,是它自己活过来的。
    //
    // 【改法】
    //   加 reconnectAbandoned_ 标志,置位后本函数直接返回 —— 不再排新的,也不再重复通知。
    //   标志由 connectTo()(用户手动重连)和 onSocketConnected()(连上了)清掉,
    //   所以不会把客户端永久焊死在放弃状态 —— 那是比原 bug 更严重的坑,
    //   tst_TcpClient::abandoned_clearedWhenUserConnectsAgain 专门盯这一条。
    if (reconnectAbandoned_) {
        return;
    }

    // 防御重入:onSocketError + onSocketDisconnected 都可能调,避免重复启动定时器
    if (reconnectTimer_->isActive()) {
        return;   // 已有重连计划,不再累加 retryCount_、不再覆盖定时器
    }

    if (retryCount_ >= maxRetries_) {
        // 重试到上限,放弃(通知 UI 进入 Failed 状态)
        // ★ 这里【不再清 retryCount_】:清掉就等于把"我已经放弃了"这个信息抹了,
        //   而紧接着的 disconnected 正是靠这个计数判断要不要继续的。
        //   计数本来就由 connectTo() / onSocketConnected() 清,不必多此一举。
        reconnectAbandoned_ = true;
        emit reconnectAbandoned();   // 改:不再 emit errorOccurred(避免 hmi onTcpError 误设 Failed)
        qDebug() << "[TcpClient] 重连放弃(已用完" << retryCount_ << "次),等用户手动重连";
        return;
    }

    // 指数退避:1秒、2秒、4秒、8秒、16秒、30秒封顶
    // 第 1 次:retryCount_=0,位移 1<<0=1,延迟 1000ms
    // 第 2 次:retryCount_=1,位移 1<<1=2,延迟 2000ms
    // 依此类推
    // 第 6 次:retryCount_=5,位移 1<<5=32,qMin(30000,32000)=30000ms,达到封顶
    // ★ IMPROVE-B4ext(2026-07-29):30000 → kReconnectMaxMs / 1000 → kReconnectBaseMs
    int delayMs = qMin(TcpClient::kReconnectMaxMs, TcpClient::kReconnectBaseMs * (1 << retryCount_));
    retryCount_++;

    // 通知 UI(进入 Reconnecting 状态,显示"重连中(2/5)")
    emit reconnectScheduled(retryCount_, maxRetries_, delayMs);

    // 启动退避定时器
    reconnectTimer_->start(delayMs);
}

void TcpClient::onReconnectTimer() {
    if (lastHost_.isEmpty()) return;

    socket_->connectToHost(lastHost_, lastPort_);
    emit connectionAttempted();   // 通知 UI:重连开始了

    // 每次重连也加 3s 超时(成员定时器)
    connectTimeoutTimer_->start(TcpClient::kConnectTimeoutMs);  // ★ IMPROVE-B4ext
}

// ==================== 阶段 1 第 2 练:心跳 ====================

void TcpClient::startHeartbeat() {
    lastReceivedTime_ = QTime::currentTime();
    heartbeatTimer_->start(heartbeatIntervalMs_);   // 5s
}

void TcpClient::stopHeartbeat() {
    heartbeatTimer_->stop();
}

// ★ IMPROVE-B2(2026-07-29):heartbeatTimer 改造
//   【背景】原代码 timer 每 5s 醒一次,只 qDebug 打一行 "[TcpClient] 心跳定时器到..."
//          — Modbus 协议不能主动发 1 字节 0x00(会污染协议帧),timer 空转刷屏
//   【现状】timer 唯一作用:检查 inactivity timeout(15s 无活动 → 判掉线)
//          实际是「被动超时检测」,不是真「心跳」
//   【取舍】以后要做应用层心跳:每 N 秒主动发一次合规 Modbus 读请求(由 PollScheduler 承担)
//          现在 timer 只剩 inactivity check,够用
//   【参照】ModbusMaster::onTimeoutScan(2026-07-09)+ IMPL: https://doc.qt.io/qt-6/qabstractsocket.html
void TcpClient::onHeartbeatTimer() {
    // 重连期间 socket 不在 ConnectedState,timer 醒直接 return(每 5s 一次检查,无副作用)
    if (socket_->state() != QAbstractSocket::ConnectedState) {
        return;
    }

    // 检查"无活动超时" — 15s 内没收到任何数据 → 判掉线,触发重连
    qint64 elapsed = lastReceivedTime_.msecsTo(QTime::currentTime());
    if (lastReceivedTime_.isValid() && elapsed > inactivityTimeoutMs_) {
        qDebug() << "[TcpClient] 心跳超时:" << elapsed << "ms 无活动,主动断开重连";
        socket_->abort();
        // ★ 不 emit errorOccurred:abort 触发 onSocketError → scheduleReconnect
        // hmi 端 onTcpError 不应再设 Failed(由 reconnectScheduled 决定 UI = 橙)
        return;
    }
    // 其余路径:timer 醒 + state Connected + 未超时 → 什么都不做(等下 5s 再检)
}
