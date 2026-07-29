#pragma once
#include <QObject>
#include <QTcpSocket>
#include <QAbstractSocket>
#include <QByteArray>
#include <QTimer>
#include <QTime>

// 阶段 1 第 1 练:TCP 客户端封装
// 职责:打开/关闭 TCP 连接、收发字节、转发底层信号给上层
// 上层(hmi)只管 UI,所有 IO 走这个类
class TcpClient : public QObject {
    Q_OBJECT
public:
    explicit TcpClient(QObject* parent = nullptr);
    ~TcpClient() override;

    // ★ IMPROVE-B4ext(2026-07-29):magic number 提常量,改超时/重试集中一处
    //   【背景】原代码 6 个 magic number 散在 .h + .cpp(3000 连接超时 / 1000 重连基数 / 30000 重连上限 / 5 重试次数 / 5000 心跳 / 15000 inactivity)
    //   【改法】提 6 个 static constexpr,按用途命名
    //   【取舍】常量放 class 内 public static(从构造函数内部能直接初始化引用)— Qt 习惯
    static constexpr int kConnectTimeoutMs    = 3000;   // TCP 连接超时 3 秒
    static constexpr int kReconnectBaseMs     = 1000;   // 指数退避基数(1<<retryCount * 此值)
    static constexpr int kReconnectMaxMs      = 30000;  // 重连间隔上限 30 秒
    static constexpr int kMaxRetries          = 5;      // 最大重试次数(超 limit 进 Failed)
    static constexpr int kHeartbeatIntervalMs = 5000;   // heartbeatTimer 5s 醒一次(注:现在只做 inactivity check)
    static constexpr int kInactivityTimeoutMs = 15000;  // 15s 无活动 → 判掉线(给 3 次心跳机会)

    void connectTo(const QString& host, quint16 port); // 连接服务器(异步)
    // [BUG #9] 原代码:void disconnect();                                 // 断开连接(优雅版)
    //           原因:遮蔽了 QObject::disconnect 静态函数
    //           (后续想在 TcpClient 内 disconnect 信号会调到错的)
    // [FIX #9] 改名:close() —— 避免遮蔽 QObject 静态方法
    void close();                                      // 断开连接(优雅版)
    bool isConnected() const;                          // 是否连接中
    qint64 send(const QByteArray& data);               // 发送数据

signals:
    void connected();                                  // 连接成功
    void disconnected();                               // 连接断开
    void readyRead(const QByteArray& data);            // 累积缓冲发出去
    void errorOccurred(const QString& msg);            // 发生错误

    // 阶段 1 第 2 练:重连进度信号(给 UI 显示"重连中(2/5)")
    void reconnectScheduled(int attempt, int maxRetries, int delayMs);
    // 阶段 1 第 2 练:每次发起连接(用户主动 / 重连)都触发 → UI 切 Connecting
    void connectionAttempted();
    // 阶段 1 第 2 练:重连到上限(放弃),UI 应该进 Failed
    void reconnectAbandoned();

private slots:
	void onSocketConnected();// TCP 连接成功
	void onSocketDisconnected();// TCP 连接断开
	void onSocketReadyRead();// TCP 收到数据了
    void onSocketError(QAbstractSocket::SocketError err);

    // 阶段 1 第 2 练:重连 + 心跳
    void onReconnectTimer();
    void onHeartbeatTimer();

private:
    // 阶段 1 第 1 练:底层 socket + 累积缓冲
    QTcpSocket* socket_ = nullptr;
    // [BUG #5] 原代码(已注释):QByteArray  buffer_;(实际不再 append,只当占位等未来分帧)
    // [FIX #5] 阶段 0/1 不做协议分帧,字段保留但不累加 append; 阶段 2/3 接 Modbus 时再启用
    QByteArray  buffer_;   // 阶段 2/3 启用

    // 阶段 1 第 2 练:重连相关
    QTimer* reconnectTimer_ = nullptr;     // 退避定时器
    int     retryCount_      = 0;          // 当前重试次数
    // ★ IMPROVE-B4ext(2026-07-29):5 → kMaxRetries(常量替 magic number,改重试上限只改一处)
    int     maxRetries_      = TcpClient::kMaxRetries;  // 最大重试次数
    QString lastHost_;                     // 上次连接目标
    quint16 lastPort_       = 0;
    bool    userInitiated_  = false;       // 区分"用户主动"vs"被动断开"

    // 阶段 1 第 2 练:心跳相关
    QTimer* heartbeatTimer_      = nullptr;   // 心跳定时器
    QTimer* connectTimeoutTimer_ = nullptr;   // 连接超时定时器(3s,可在 close()/scheduleReconnect() 里停掉)
    QTime   lastReceivedTime_;                // 上次收到数据时间
    // ★ IMPROVE-B4ext(2026-07-29):5000 → kHeartbeatIntervalMs(注:现在只做 inactivity check,不发包)
    int     heartbeatIntervalMs_ = TcpClient::kHeartbeatIntervalMs;  // 5s 检一次超时
    // ★ IMPROVE-B4ext(2026-07-29):15000 → kInactivityTimeoutMs
    int     inactivityTimeoutMs_ = TcpClient::kInactivityTimeoutMs;  // 15s 没数据 → 判掉线

    // 内部方法
	void scheduleReconnect();// 计算退避时间 + 启动重连定时器
	void startHeartbeat();// 启动心跳定时器
	void stopHeartbeat();// 停止心跳定时器
};
