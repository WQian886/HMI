// ============================================================================
// ★ 2026-10-10 · 改善清单 #2 —— TcpClient 重连退避状态机
//
// 【为什么单独开一个测试文件】
//   TcpClient 从阶段 1 起就是核心类,但【从来没有一个测试文件】——
//   HMI_CORE_SOURCES 里带着 TcpClient.cpp(别的测试间接用),
//   重连/退避/放弃这套逻辑一行都没被测过。
//   #2 说的"重入防护不严 → retryCount_ 飘"就是这么漏下来的。
//
// 【测什么:不是"有没有重连",而是"放弃之后会不会自己复活"】
//   Qt 在连接失败时的信号顺序是【确定的】:
//       errorOccurred 先,disconnected 紧跟着后。
//   两个都连到 scheduleReconnect()。防护是靠 `reconnectTimer_->isActive()`
//   —— 正常情况下第二条被挡住,没问题。
//   但"重试到上限放弃"那条分支【故意没有启动定时器】,于是防护失效。
//
// 【怎么测:走真实信号入口,不 invoke 私有普通函数】
//   onSocketError / onSocketDisconnected 都是 private slots,
//   QMetaObject::invokeMethod 按名字能调到 —— 测的是【真实信号路径】,
//   不是把 scheduleReconnect 单独拎出来验一遍。
//
// 【为什么测试里要手动 stop 定时器】
//   isActive() 防护在真实运行时管的是"同一个事件循环瞬间的重复调用"。
//   测试里要连续走 5 轮退避(真实等待要 1+2+4+8+16=31 秒),
//   所以每次调用前把定时器停掉,等价于"上一次重连已经过去并结束了"。
//   ★ 这不是绕过被测逻辑:下面 errorThenDisconnected... 那条用例专门证明
//     "不 stop 的时候防护确实是生效的"。
// ============================================================================

#include <QtTest>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include "TcpClient.h"

class TestTcpClient : public QObject {
    Q_OBJECT
private slots:
    // 防护本身:error + disconnected 紧挨着来,只该排【一次】重连
    void errorThenDisconnected_whileTimerRunning_schedulesOnlyOnce();

    // ★ 真 bug:重试耗尽放弃之后,紧跟着的 disconnected 会把整个循环重新排起来
    void abandoned_thenDisconnected_doesNotRestartRetryCycle();
    void abandoned_emitsAbandonedOnlyOnce();

    // 放弃之后用户手动重连,状态必须能清干净(不能永久卡死)
    void abandoned_clearedWhenUserConnectsAgain();

    // ★ 改善清单 #10:查过了 —— 清单这条是错的,不能挪
    void lowDelayOption_mustBeSetAfterConnect();

private:
    // 把 TcpClient 的三个 QTimer 全停掉(它们都是 this 的子对象)
    static void stopAllTimers(QObject* client);
    // 走真实信号路径
    static void fireSocketError(QObject* client);
    static void fireSocketDisconnected(QObject* client);
    // 连敲 maxRetries 次错误,直到放弃
    static void driveToAbandoned(QObject* client);
};

void TestTcpClient::stopAllTimers(QObject* client) {
    const QList<QTimer*> timers = client->findChildren<QTimer*>();
    for (QTimer* t : timers) t->stop();
}

void TestTcpClient::fireSocketError(QObject* client) {
    // 错误码传什么无所谓:TcpClient 里 Q_UNUSED(err),只用 errorString() 打日志
    QVERIFY(QMetaObject::invokeMethod(client, "onSocketError",
                                      Q_ARG(QAbstractSocket::SocketError,
                                            QAbstractSocket::UnknownSocketError)));
}

void TestTcpClient::fireSocketDisconnected(QObject* client) {
    QVERIFY(QMetaObject::invokeMethod(client, "onSocketDisconnected"));
}

void TestTcpClient::driveToAbandoned(QObject* client) {
    for (int i = 0; i < TcpClient::kMaxRetries; ++i) {
        stopAllTimers(client);
        fireSocketError(client);       // 每次都真的走一遍退避计算
    }
    // 第 kMaxRetries 次之后 retryCount_ == kMaxRetries,再敲一次就该放弃
    stopAllTimers(client);
    fireSocketError(client);
}

// ---------------------------------------------------------------------------

void TestTcpClient::errorThenDisconnected_whileTimerRunning_schedulesOnlyOnce() {
    // 基线:isActive() 防护是有效的。
    // 不 stop 定时器,让 error 和 disconnected 挤在同一个瞬间到达
    // —— 这就是 Qt 连接失败时的真实顺序。
    TcpClient c;
    QSignalSpy scheduled(&c, &TcpClient::reconnectScheduled);
    QSignalSpy abandoned(&c, &TcpClient::reconnectAbandoned);

    fireSocketError(&c);
    QCOMPARE(scheduled.count(), 1);
    QCOMPARE(scheduled.last().at(0).toInt(), 1);      // 第 1 次
    QCOMPARE(scheduled.last().at(2).toInt(), 1000);   // 退避 1 秒

    // ★ 定时器还活着 —— 此刻来的第二条调用必须被挡住
    fireSocketDisconnected(&c);

    QCOMPARE(scheduled.count(), 1);   // 没有多排一次
    QCOMPARE(abandoned.count(), 0);
}

void TestTcpClient::abandoned_thenDisconnected_doesNotRestartRetryCycle() {
    // ★ 真 bug(改善清单 #2 "retryCount_ 飘"的实体)
    //
    // 重试耗尽 → scheduleReconnect 走放弃分支 → 通知 UI 进 Failed。
    // 紧接着 Qt 的 disconnected 到达 → 又调一次 scheduleReconnect。
    //
    // 放弃分支把 retryCount_ 清成了 0,而【没有启动定时器】,
    // 所以 isActive() 防护完全失效:第二条调用看到 retryCount_=0 < 5,
    // 于是又排了第 1 次重连,UI 从 Failed 又跳回"重连中(1/5)"。
    // 而没有任何人按过"重连"按钮。
    TcpClient c;
    QSignalSpy scheduled(&c, &TcpClient::reconnectScheduled);
    QSignalSpy abandoned(&c, &TcpClient::reconnectAbandoned);

    driveToAbandoned(&c);

    // 前 5 次正常排退避,第 6 次放弃
    QCOMPARE(scheduled.count(), TcpClient::kMaxRetries);
    QCOMPARE(abandoned.count(), 1);

    // ★ 现在模拟紧跟着的 disconnected(TCP 连接失败时 Qt 必定会发)
    fireSocketDisconnected(&c);

    // 放弃之后【不该再排任何重连】。
    // 修复前这里 scheduled.count() == 6 —— 客户端自己复活了,
    // 而且是无限循环:每轮放弃后都重启,永远停在"重连中",
    // 界面上"连接失败"的提示一闪而过 —— 恰好是操作员最该看见的那句话。
    QCOMPARE(scheduled.count(), TcpClient::kMaxRetries);
    QCOMPARE(abandoned.count(), 1);
}

void TestTcpClient::abandoned_emitsAbandonedOnlyOnce() {
    // 配套:放弃之后再来 error / disconnected,也不能重复通知 UI。
    // 重复发 reconnectAbandoned 本身不致命,但它说明"放弃"这个状态
    // 根本不存在 —— 而它必须存在,否则上面那个 bug 就还在。
    TcpClient c;
    QSignalSpy abandoned(&c, &TcpClient::reconnectAbandoned);
    QSignalSpy scheduled(&c, &TcpClient::reconnectScheduled);

    driveToAbandoned(&c);
    QCOMPARE(abandoned.count(), 1);

    stopAllTimers(&c);
    fireSocketDisconnected(&c);
    stopAllTimers(&c);
    fireSocketError(&c);
    stopAllTimers(&c);
    fireSocketDisconnected(&c);

    QCOMPARE(abandoned.count(), 1);
    QCOMPARE(scheduled.count(), TcpClient::kMaxRetries);
}

void TestTcpClient::abandoned_clearedWhenUserConnectsAgain() {
    // ★ 修 #2 最容易踩的坑:把"放弃"做成永久状态。
    //   放弃之后用户手动点【连接】,退避必须能从 1 重新开始,
    //   否则这台机器就再也连不上了 —— 那比原 bug 更严重。
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));

    TcpClient c;
    QSignalSpy scheduled(&c, &TcpClient::reconnectScheduled);
    QSignalSpy abandoned(&c, &TcpClient::reconnectAbandoned);

    driveToAbandoned(&c);
    QCOMPARE(abandoned.count(), 1);

    // 用户手动重连(目标是本机真的在听的 server)
    c.connectTo(QStringLiteral("127.0.0.1"), server.serverPort());
    QTRY_VERIFY_WITH_TIMEOUT(c.isConnected(), 3000);

    // 连上之后再来一次错误 → 必须重新排第 1 次,而不是直接又"放弃"
    stopAllTimers(&c);
    fireSocketError(&c);

    QCOMPARE(scheduled.count(), TcpClient::kMaxRetries + 1);
    QCOMPARE(scheduled.last().at(0).toInt(), 1);   // ★ 计数真的归零重来了
    QCOMPARE(abandoned.count(), 1);                // 没有再放弃

    c.close();
}

// ---------------------------------------------------------------------------

void TestTcpClient::lowDelayOption_mustBeSetAfterConnect() {
    // ★ 改善清单 #10 —— 查完之后结论是:【清单这条是错的,别按它改】
    //
    // 清单原话:#10 setSocketOption 每次都设 → 移到构造。
    // 听起来是纯粹的清理(设一次就够,何必每次连接重设)。但实测:
    //   把那行从 onSocketConnected 挪到构造函数之后,连上再读 socketOption
    //   拿到的是【0】—— Nagle 根本没被关掉,每个小 Modbus 帧又慢 ~40ms,
    //   而代码上【看不出任何变化】。
    //
    // 【这条测试为什么用【一个跟 TcpClient 无关的裸 socket】当探针】
    //   一开始想在 TcpClient 上验"连上之后读出来是 0",但那条断言是废的:
    //   onSocketConnected 里本来就在设 1,连上之后不管预连接那次有没有生效,
    //   读出来都是 1 —— 【分不清是哪一次设的】。
    //   分不清来源的断言不算断言,那次跑出来 Actual=1 / Expected=0
    //   就是它自己先红给我看的。
    //   所以这里拿全新 QTcpSocket 单独验"预连接设 LowDelayOption 到底算不算数";
    //   TcpClient 自己那份另用断言盯(它只能验结果对不对,验不了来源)。
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));
    const quint16 port = server.serverPort();

    // ① 探针:连【之前】设 LowDelayOption —— 清单 #10 建议的做法
    {
        QTcpSocket probe;
        probe.setSocketOption(QAbstractSocket::LowDelayOption, 1);
        probe.connectToHost(QHostAddress::LocalHost, port);
        QTRY_VERIFY_WITH_TIMEOUT(probe.state() == QAbstractSocket::ConnectedState, 3000);
        // ★ 没被兑现 —— Qt 对还没连上的 socket 直接丢弃了这个请求。
        QCOMPARE(probe.socketOption(QAbstractSocket::LowDelayOption).toInt(), 0);
        probe.abort();
    }

    // ② 探针:连【上之后】再设 —— 只有这样才真的生效
    {
        QTcpSocket probe;
        probe.connectToHost(QHostAddress::LocalHost, port);
        QTRY_VERIFY_WITH_TIMEOUT(probe.state() == QAbstractSocket::ConnectedState, 3000);
        probe.setSocketOption(QAbstractSocket::LowDelayOption, 1);
        QCOMPARE(probe.socketOption(QAbstractSocket::LowDelayOption).toInt(), 1);
        probe.abort();
    }

    // ③ TcpClient 自己:首次连接和重连之后都必须是 1
    //   TcpClient 在 onSocketConnected 里设,所以每次连接都覆盖到了。
    //   这条只能验结果,验不了来源 —— 来源由上面两个探针钉住。
    TcpClient c;
    // socket_ 是 private,但它是 TcpClient 的 QObject 子对象,找得到
    auto* sock = c.findChild<QTcpSocket*>();
    QVERIFY(sock != nullptr);

    c.connectTo(QStringLiteral("127.0.0.1"), port);
    QTRY_VERIFY_WITH_TIMEOUT(c.isConnected(), 3000);
    QCOMPARE(sock->socketOption(QAbstractSocket::LowDelayOption).toInt(), 1);

    c.close();
    QTRY_VERIFY_WITH_TIMEOUT(!c.isConnected(), 3000);
    c.connectTo(QStringLiteral("127.0.0.1"), port);
    QTRY_VERIFY_WITH_TIMEOUT(c.isConnected(), 3000);
    QCOMPARE(sock->socketOption(QAbstractSocket::LowDelayOption).toInt(), 1);

    c.close();
}

QTEST_MAIN(TestTcpClient)
#include "tst_TcpClient.moc"
