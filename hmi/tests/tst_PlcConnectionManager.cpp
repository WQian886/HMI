// ============================================================================
// tst_PlcConnectionManager —— 多 PLC 2b(2026-10-19):plcs[] → 运行期真的在线的 N 台
//
// 【2b 要解决的问题】
//   2a 修完了配置层(plc_id 进 JSON、plcs[] 存得住),但运行期仍然只连第一台。
//   User 在工程文件里配 3 台 → 打开工程 → 界面上"看起来支持了" →
//   现场只有 1 号在通讯,另外 2 号安静地不刷新,界面上没有任何异常。
//   这跟 2a 那个 bug 是同一类病:**界面在撒谎**。
//
//   更糟的是写路径 —— WriteScheduler 全代码零 plcId,给 plc2 的变量赋的值,
//   字节从 plc1 的 socket 发出去。现场表现:操作员点了 2 号机的按钮,
//   1 号机的数值变了。**没有任何报错,没有任何日志。**
//
// 【本类钉住的是哪一段】
//   只钉"plcs[] 变成 N 个 driver、且改配置时的增删改对不对"。
//   ★ 不在这里测通讯本身通不通 —— 那是阶段 5 的 3 个真 QTcpServer 的活。
//     本文件【全程不建任何监听端口】:connectTo 是异步的,对着一堆没人监听的
//     端口连,结果只是 connection refused,不影响被测逻辑。
//     这是刻意的隔离:这里要绿是【逻辑】绿,不是【网络】绿。
//
// 【为什么"指针互不相同"值得单独一条用例】
//   DriverFactory::create 每次都 new 一个新 driver。这条用例把这件事钉死,
//   因为一旦有人图省事改成"复用一个 driver 实例、按 plcId 临时改 host",
//   现象是【三台 PLC 共用一个 socket,最后一次连接的地址赢】——
//   前两台的读请求会安静地从第三台发出去,一条日志都没有。
//   这类 bug 只有钉住"指针不同"才抓得到,盯 added/removed 列表是抓不到的
//   (那种实现 added 里照样有三条)。
//
// 【负向对照 —— 两轮实跑结果,2026-10-19】
//
//   轮次一:同Connection() 恒返回 false(永远是"变了")
//     → 红 7:applyConfig_twice_secondCallTouchesNothing
//             applyConfig_changeHost_reconnectsOnlyThatPlc(reconnected=plc1,plc2,plc3)
//             applyConfig_changeOnlyTimeout_doesNotReconnect
//             applyConfig_changeOnlyRetriesAndPoll_doesNotReconnect
//             protocolSpellingVariants_doNotTriggerReconnect
//             removalSignal_precedesReplacementAddedSignal
//             configApplied_notEmittedWhenNothingChanged
//       绿 18。红的【全是"本不该重连却重连了"那类】——
//             现场表现:打开工程就把三台 PLC 全断线重连一遍。
//
//   轮次二:同Connection() 恒返回 true(永远是"没变")
//     → 红 3:applyConfig_changeHost_reconnectsOnlyThatPlc(reconnected= 空)
//             applyConfig_changePortOrSlaveId_reconnects
//             removalSignal_precedesReplacementAddedSignal
//       绿 22。红的【全是"该重连却没重连"那类】——
//             现场表现:用户把 IP 改了,程序说"已应用",现场连的还是老地址。
//
// ★ 两轮抓到的用例【完全不重叠】(只有 removalSignal 那条两边都红,
//   因为它靠的是"重连"这个动作本身发生)。这就是必须做两轮的理由:
//   只做轮次一,一个"永远判没变"的实现能全绿;只做轮次二,一个"永远判变了"
//   的实现也能全绿。两边都堵住,判据才真的立住了。
//
// 关联:PlcConnectionManager, IPlcDriverProvider, DriverFactory
// ============================================================================

#include <QtTest>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>

#include "plc/PlcConnectionManager.h"
#include "drivers/DriverFactory.h"
#include "drivers/IPollDriver.h"

namespace {

// 一台合法 PLC 的最小配置(与 Project::defaultPlcFromTcp 的字段名一致)
QJsonObject makePlc(const QString& id, const QString& host, int port) {
    QJsonObject p;
    p["id"]       = id;
    p["protocol"] = QStringLiteral("modbus_tcp");
    p["host"]     = host;
    p["port"]     = port;
    return p;
}

QJsonArray threePlcs() {
    QJsonArray a;
    a.append(makePlc(QStringLiteral("plc1"), QStringLiteral("127.0.0.1"), 1502));
    a.append(makePlc(QStringLiteral("plc2"), QStringLiteral("127.0.0.1"), 1503));
    a.append(makePlc(QStringLiteral("plc3"), QStringLiteral("127.0.0.1"), 1504));
    return a;
}

} // namespace

class tst_PlcConnectionManager : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    // ---------- 建 ----------

    // ★★★ 三台 PLC → 三个【互不相同】的 driver 指针
    void threePlcs_getThreeDistinctDrivers();

    // 各自连自己的地址 —— 断言 driver 数量,但更要看 driverCount 是不是 3
    void threePlcs_driverCountIsThree();

    // driverFor 查不到时必须回 nullptr,【不许】退化成"取第一个"
    //   那个退化正是"给 plc2 的变量写进 plc1 的 socket"的直接成因
    void driverFor_unknownPlc_returnsNull_notFirstDriver();

    // ---------- 幂等 ----------

    // ★★★ 同一份配置连调两次,第二次什么都【不】做 —— 连 connectTo 都不发
    void applyConfig_twice_secondCallTouchesNothing();

    // 第一次 applyConfig 必须真的发 3 次 connectTo。
    //   跟上面那条成对:只看"第二次是 0"不够,也要钉住"第一次是 3",
    //   否则一个"永远不连"的实现能同时满足两条断言。
    void applyConfig_firstCall_connectsEachPlcOnce();

    // ---------- 增删改 ----------

    // ★★★ 改一台的 host → 只重连那台,其余两台【指针不变】
    void applyConfig_changeHost_reconnectsOnlyThatPlc();

    // 只改 timeout_ms → 【不】重连(TcpClient 的超时在 connectTo 时定死,运行期改不动)
    void applyConfig_changeOnlyTimeout_doesNotReconnect();

    // 同理,max_retries / poll_interval_ms 也不该触发重连
    void applyConfig_changeOnlyRetriesAndPoll_doesNotReconnect();

    // 改 port → 重连;改 slave_id → 重连(它进了 Modbus 的帧里)
    void applyConfig_changePortOrSlaveId_reconnects();

    // 配置里删掉一台 → 那台下线
    void applyConfig_omitsOnePlc_removesIt();

    // 配置清空 → 全部下线
    void applyConfig_emptyArray_removesEverything();

    // ---------- 拒收 ----------

    // 未知协议 → 进 rejected,其余两台照常上线(一台填错不挡整个工程)
    void unknownProtocol_rejectedButOthersStillComeUp();

    // 重复 id → 后者被拒,前者不受影响
    void duplicateId_secondEntryRejectedFirstSurvives();

    // 缺 id / host 空 / port 越界 → 跳过,但不影响别的
    void malformedEntries_skippedOthersUnaffected();

    // ---------- slaveId ----------

    void slaveId_defaultsToOneWhenAbsent();
    void slaveId_readFromConfig();
    void slaveId_outOfRange_fallsBackToOne();
    // 未知 plcId 回 1,【绝不】回 0(0 是 Modbus 广播地址)
    void slaveIdFor_unknownPlc_isOneNotZero();

    // ---------- 协议归一 ----------

    // "modbus" / "modbus_tcp" / "Modbus TCP" 指同一台 —— 只改写法不该触发重连
    void protocolSpellingVariants_doNotTriggerReconnect();

    // ---------- 信号 ----------

    // driverAdded 必须在 driverFor 已经能查到它【之后】才发,
    //   否则接信号的槽(阶段 5 里是 PollScheduler::addDriver)拿到的是一个查不到的 id
    void driverAdded_emittedAfterDriverQueryable();

    // 顺序:先 driverAboutToBeRemoved(下游才摘得掉连接),再 driverAdded
    void removalSignal_precedesReplacementAddedSignal();

    // 幂等重入时 configApplied 【不】发(界面会整表闪)
    void configApplied_notEmittedWhenNothingChanged();

    // ---------- 原始配置保真 ----------

    // ★ lastConfig 必须是【原文】,不能是"按已知字段重建"的
    void lastConfig_preservesUnknownUserFields();

private:
    PlcConnectionManager* m_mgr = nullptr;
};

// ---------------------------------------------------------------------------

void tst_PlcConnectionManager::init() {
    m_mgr = new PlcConnectionManager(this);
}

void tst_PlcConnectionManager::cleanup() {
    // ★ 必删,不能只靠 parent。
    //   每个 driver 里有一个 TcpClient,带着指数退避重连定时器(最长 30s 一跳)。
    //   不销毁的话这些定时器会横跨后续用例继续发 socket —— 测试就开始依赖
    //   上一个用例留下的连接失败时序了,那是最难查的一类偶发红。
    delete m_mgr;
    m_mgr = nullptr;
}

// ---------------------------------------------------------------------------
// 建
// ---------------------------------------------------------------------------

void tst_PlcConnectionManager::threePlcs_getThreeDistinctDrivers() {
    m_mgr->applyConfig(threePlcs());

    auto* d1 = m_mgr->driverFor(QStringLiteral("plc1"));
    auto* d2 = m_mgr->driverFor(QStringLiteral("plc2"));
    auto* d3 = m_mgr->driverFor(QStringLiteral("plc3"));

    QVERIFY2(d1 && d2 && d3,
             "三台都该拿到 driver。某一个为 null 说明 applyConfig 漏建或建失败,"
             "去看 hmi.plc 分类的日志。");
    QVERIFY2(d1 != d2 && d2 != d3 && d1 != d3,
             "★ 三台必须各有【自己的】driver 实例。若三者指针相同,说明有人把"
             "DriverFactory::create 换成了复用单例 —— 现场表现是三台 PLC 共用一个 "
             "socket,最后一次连接的地址赢,前两台的请求从第三台发出去,而且一条日志都没有。");
}

void tst_PlcConnectionManager::threePlcs_driverCountIsThree() {
    m_mgr->applyConfig(threePlcs());
    QCOMPARE(m_mgr->driverCount(), 3);
    QCOMPARE(m_mgr->managedIds(), QStringList({QStringLiteral("plc1"),
                                               QStringLiteral("plc2"),
                                               QStringLiteral("plc3")}));
}

void tst_PlcConnectionManager::driverFor_unknownPlc_returnsNull_notFirstDriver() {
    m_mgr->applyConfig(threePlcs());

    // ★ 这是 2b 最想钉死的一条:
    //   "查不到就退化成取第一个 driver" 是静默写错设备的直接成因。
    //   WriteScheduler(阶段 4)拿到 null 就该跳过这台,而不是接着找别的。
    QVERIFY2(m_mgr->driverFor(QStringLiteral("plc99")) == nullptr,
             "查不到的 plcId 必须回 nullptr。回第一个 driver 的话,"
             "给 plc2 变量写的值会从 plc1 的 socket 发出去 —— 现场零日志。");

    // 空 id 也一样
    QVERIFY(m_mgr->driverFor(QString()) == nullptr);
}

// ---------------------------------------------------------------------------
// 幂等
// ---------------------------------------------------------------------------

void tst_PlcConnectionManager::applyConfig_twice_secondCallTouchesNothing() {
    const QJsonArray cfg = threePlcs();
    m_mgr->applyConfig(cfg);

    auto* d1before = m_mgr->driverFor(QStringLiteral("plc1"));
    auto* d2before = m_mgr->driverFor(QStringLiteral("plc2"));
    auto* d3before = m_mgr->driverFor(QStringLiteral("plc3"));
    const int attemptsBefore = m_mgr->connectAttemptCount();

    const auto r = m_mgr->applyConfig(cfg);

    QVERIFY2(r.added.isEmpty(), "第二次 applyConfig 不该有新增 —— 同一份配置,一台都没变。");
    QVERIFY2(r.removed.isEmpty(), "第二次不该有删除 —— 三台都还在配置里。");
    QVERIFY2(r.reconnected.isEmpty(), "第二次不该有重连 —— 四元组一个都没变。");
    QCOMPARE(r.unchanged.size(), 3);

    // ★ 光看三个空列表【不够】:一个"偷偷重连了但列表没填"的实现照样能过。
    //   connectAttemptCount 是纯观测计数器,直接量 connectTo 被调了几次。
    QCOMPARE(m_mgr->connectAttemptCount(), attemptsBefore);

    // 指针也要一模一样(重连 = 拆了重建 = 换指针)
    QCOMPARE(m_mgr->driverFor(QStringLiteral("plc1")), d1before);
    QCOMPARE(m_mgr->driverFor(QStringLiteral("plc2")), d2before);
    QCOMPARE(m_mgr->driverFor(QStringLiteral("plc3")), d3before);
}

void tst_PlcConnectionManager::applyConfig_firstCall_connectsEachPlcOnce() {
    m_mgr->applyConfig(threePlcs());
    // 跟上一条成对:上一条钉住"第二次是 0",这条钉住"第一次是 3"。
    // 只钉上一条的话,一个从不连接的实现在两条断言下都会绿。
    QCOMPARE(m_mgr->connectAttemptCount(), 3);
}

// ---------------------------------------------------------------------------
// 增删改
// ---------------------------------------------------------------------------

void tst_PlcConnectionManager::applyConfig_changeHost_reconnectsOnlyThatPlc() {
    m_mgr->applyConfig(threePlcs());
    auto* d1 = m_mgr->driverFor(QStringLiteral("plc1"));
    auto* d2 = m_mgr->driverFor(QStringLiteral("plc2"));
    auto* d3 = m_mgr->driverFor(QStringLiteral("plc3"));

    // ★ 只把 plc2 的 host 改掉
    QJsonArray cfg = threePlcs();
    QJsonObject p2 = cfg.at(1).toObject();
    p2["host"] = QStringLiteral("127.0.0.2");
    cfg.replace(1, p2);

    const auto r = m_mgr->applyConfig(cfg);

    QVERIFY2(r.reconnected.size() == 1 && r.reconnected.first() == QStringLiteral("plc2"),
             qPrintable(QStringLiteral("只该重连 plc2。实际 reconnected=%1")
                            .arg(r.reconnected.join(','))));
    QVERIFY(r.added.isEmpty());
    QVERIFY(r.removed.isEmpty());
    QCOMPARE(r.unchanged.size(), 2);

    // ★ 关键:另外两台的【指针必须不变】。
    //   只断言"reconnected 只有 plc2"是不够的 —— 一个"全部拆了重建、
    //   但只把变化了的记进 reconnected"的实现也能过,而现场会看到
    //   三台 PLC 在改一台地址时全部断线重连。
    QCOMPARE(m_mgr->driverFor(QStringLiteral("plc1")), d1);
    QCOMPARE(m_mgr->driverFor(QStringLiteral("plc3")), d3);

    // 被改的那台是新实例(拆了重建)
    QVERIFY(m_mgr->driverFor(QStringLiteral("plc2")) != d2);
    QCOMPARE(m_mgr->driverCount(), 3);
}

void tst_PlcConnectionManager::applyConfig_changeOnlyTimeout_doesNotReconnect() {
    m_mgr->applyConfig(threePlcs());
    auto* d1 = m_mgr->driverFor(QStringLiteral("plc1"));

    QJsonArray cfg = threePlcs();
    QJsonObject p1 = cfg.at(0).toObject();
    QJsonObject modbus;
    modbus["timeout_ms"] = 5000;
    modbus["max_retries"] = 9;
    p1["modbus"] = modbus;
    cfg.replace(0, p1);

    const auto r = m_mgr->applyConfig(cfg);

    // ★ 这些参数在 connectTo 那一刻就定死了(TcpClient.h:22-27 全是 static constexpr,
    //   没有 setter),重连也用不上新值。所以只更新配置 + 日志说"下次生效",
    //   【不】假装重连 —— 那只会让现场 PLC 白抖一次。
    QVERIFY2(r.reconnected.isEmpty(),
             "只改 timeout/max_retries 不该重连:TcpClient 的超时参数运行期改不动,"
             "重连了也用不上新值,只会让现场那台白断一次线。");
    QVERIFY(r.unchanged.contains(QStringLiteral("plc1")));
    QCOMPARE(m_mgr->driverFor(QStringLiteral("plc1")), d1);
}

void tst_PlcConnectionManager::applyConfig_changeOnlyRetriesAndPoll_doesNotReconnect() {
    m_mgr->applyConfig(threePlcs());
    auto* d1 = m_mgr->driverFor(QStringLiteral("plc1"));

    QJsonArray cfg = threePlcs();
    QJsonObject p1 = cfg.at(0).toObject();
    QJsonObject modbus;
    modbus["poll_interval_ms"] = 500;
    modbus["max_retries"] = 3;
    p1["modbus"] = modbus;
    cfg.replace(0, p1);

    const auto r = m_mgr->applyConfig(cfg);
    QVERIFY2(r.reconnected.isEmpty(), "poll_interval_ms / max_retries 不参与重连判据。");
    QCOMPARE(m_mgr->driverFor(QStringLiteral("plc1")), d1);
}

void tst_PlcConnectionManager::applyConfig_changePortOrSlaveId_reconnects() {
    m_mgr->applyConfig(threePlcs());

    // 改 port
    QJsonArray cfgPort = threePlcs();
    QJsonObject pa = cfgPort.at(0).toObject();
    pa["port"] = 2502;
    cfgPort.replace(0, pa);
    auto r = m_mgr->applyConfig(cfgPort);
    QVERIFY2(r.reconnected.contains(QStringLiteral("plc1")), "port 变了必须重连 —— 它是连接目标。");

    // 改 slave_id
    QJsonArray cfgSlave = threePlcs();
    QJsonObject pb = cfgSlave.at(1).toObject();
    pb["slave_id"] = 7;
    cfgSlave.replace(1, pb);
    r = m_mgr->applyConfig(cfgSlave);
    QVERIFY2(r.reconnected.contains(QStringLiteral("plc2")),
             "slave_id 变了必须重连 —— 它进了 Modbus 的帧里(MBAP 的 unit id),"
             "不重连的话新值送不出去,用户改了配置却看到还是老从站号在应答。");
    QCOMPARE(m_mgr->slaveIdFor(QStringLiteral("plc2")), 7);
}

void tst_PlcConnectionManager::applyConfig_omitsOnePlc_removesIt() {
    m_mgr->applyConfig(threePlcs());

    QJsonArray cfg = threePlcs();
    cfg.removeAt(1);   // plc2 不在新配置里
    const auto r = m_mgr->applyConfig(cfg);

    QVERIFY2(r.removed.size() == 1 && r.removed.first() == QStringLiteral("plc2"),
             "配置里删掉的 PLC 必须下线。留在那儿的话,用户已经不管它了,"
             "程序还在往一台他没配的机器发请求。");
    QVERIFY(m_mgr->driverFor(QStringLiteral("plc2")) == nullptr);
    QCOMPARE(m_mgr->driverCount(), 2);
    QCOMPARE(m_mgr->managedIds(), QStringList({QStringLiteral("plc1"), QStringLiteral("plc3")}));
}

void tst_PlcConnectionManager::applyConfig_emptyArray_removesEverything() {
    m_mgr->applyConfig(threePlcs());
    const auto r = m_mgr->applyConfig(QJsonArray());

    QCOMPARE(r.removed.size(), 3);
    QCOMPARE(m_mgr->driverCount(), 0);
    QVERIFY(m_mgr->managedIds().isEmpty());
}

// ---------------------------------------------------------------------------
// 拒收
// ---------------------------------------------------------------------------

void tst_PlcConnectionManager::unknownProtocol_rejectedButOthersStillComeUp() {
    QJsonArray cfg = threePlcs();
    QJsonObject bad = makePlc(QStringLiteral("plcX"), QStringLiteral("127.0.0.1"), 1599);
    bad["protocol"] = QStringLiteral("profinet");     // 将来版本才支持的协议
    cfg.append(bad);

    const auto r = m_mgr->applyConfig(cfg);

    QVERIFY2(r.rejected.contains(QStringLiteral("plcX")),
             "工厂造不出来的协议要进 rejected,而不是悄悄消失。");
    // ★ 一台填错不挡整个工程 —— 与 Project::validatePlcEntry 的既有取舍一致。
    QCOMPARE(m_mgr->driverCount(), 3);
    QVERIFY(m_mgr->driverFor(QStringLiteral("plc1")) != nullptr);
    QVERIFY(m_mgr->driverFor(QStringLiteral("plcX")) == nullptr);
}

void tst_PlcConnectionManager::duplicateId_secondEntryRejectedFirstSurvives() {
    QJsonArray cfg = threePlcs();
    // 同 id 再来一台,地址不同
    cfg.append(makePlc(QStringLiteral("plc2"), QStringLiteral("10.0.0.9"), 9602));

    const auto r = m_mgr->applyConfig(cfg);

    QVERIFY2(r.rejected.contains(QStringLiteral("plc2")), "重复 id 的后者要被拒。");
    QCOMPARE(r.added.size(), 3);
    QCOMPARE(m_mgr->driverCount(), 3);
    // ★ 前者活下来,不是后者顶替它 —— 顶替会让正在通讯的那台静默换地址,
    //   这是最坏的一种错法(工程文件是手写的,重复 id 多半是复制粘贴忘了改)。
    QCOMPARE(m_mgr->slaveIdFor(QStringLiteral("plc2")), 1);
}

void tst_PlcConnectionManager::malformedEntries_skippedOthersUnaffected() {
    QJsonArray cfg;

    QJsonObject noId = makePlc(QString(), QStringLiteral("127.0.0.1"), 1502);
    cfg.append(noId);

    QJsonObject noHost = makePlc(QStringLiteral("plcB"), QString(), 1503);
    cfg.append(noHost);

    QJsonObject badPort = makePlc(QStringLiteral("plcC"), QStringLiteral("127.0.0.1"), 70000);
    cfg.append(badPort);

    cfg.append(makePlc(QStringLiteral("plcGood"), QStringLiteral("127.0.0.1"), 1505));

    const auto r = m_mgr->applyConfig(cfg);

    QCOMPARE(r.added.size(), 1);
    QCOMPARE(r.added.first(), QStringLiteral("plcGood"));
    QCOMPARE(m_mgr->driverCount(), 1);
    // 坏的三项不该出现在 rejected 里(连 id 都没有 / 用户还没配过这台)
    // —— rejected 是给界面看的"你配的这台没上",混进这些会误导用户
    QVERIFY(!r.rejected.contains(QStringLiteral("plcB")));
    QVERIFY(!r.rejected.contains(QStringLiteral("plcC")));
}

// ---------------------------------------------------------------------------
// slaveId
// ---------------------------------------------------------------------------

void tst_PlcConnectionManager::slaveId_defaultsToOneWhenAbsent() {
    QJsonArray cfg = threePlcs();   // makePlc 不写 slave_id
    m_mgr->applyConfig(cfg);
    QCOMPARE(m_mgr->slaveIdFor(QStringLiteral("plc1")), 1);
    QCOMPARE(m_mgr->slaveIdFor(QStringLiteral("plc2")), 1);
}

void tst_PlcConnectionManager::slaveId_readFromConfig() {
    QJsonArray cfg;
    cfg.append(makePlc(QStringLiteral("plcA"), QStringLiteral("127.0.0.1"), 1502));
    QJsonObject a = cfg.at(0).toObject();
    a["slave_id"] = 5;
    cfg.replace(0, a);
    cfg.append(makePlc(QStringLiteral("plcB"), QStringLiteral("127.0.0.1"), 1503));

    m_mgr->applyConfig(cfg);
    QCOMPARE(m_mgr->slaveIdFor(QStringLiteral("plcA")), 5);
    QCOMPARE(m_mgr->slaveIdFor(QStringLiteral("plcB")), 1);
}

void tst_PlcConnectionManager::slaveId_outOfRange_fallsBackToOne() {
    QJsonArray cfg;
    cfg.append(makePlc(QStringLiteral("plcZero"), QStringLiteral("127.0.0.1"), 1502));
    QJsonObject z = cfg.at(0).toObject();
    z["slave_id"] = 0;                       // 0 是 Modbus 广播地址!
    cfg.replace(0, z);
    cfg.append(makePlc(QStringLiteral("plcBig"), QStringLiteral("127.0.0.1"), 1503));
    QJsonObject b = cfg.at(1).toObject();
    b["slave_id"] = 300;
    cfg.replace(1, b);

    m_mgr->applyConfig(cfg);

    QVERIFY2(m_mgr->slaveIdFor(QStringLiteral("plcZero")) == 1,
             "slave_id=0 是广播地址,发出去会收到一堆没人认领的响应 —— "
             "必须退回 1。原样传下去的话现场只看到超时,日志里一个线索都没有。");
    QVERIFY2(m_mgr->slaveIdFor(QStringLiteral("plcBig")) == 1,
             "越界的 unit id 会被从站当非法帧丢掉,表现是\"配了 300 却连不上\","
             "而配置看上去完全正常。");
}

void tst_PlcConnectionManager::slaveIdFor_unknownPlc_isOneNotZero() {
    QCOMPARE(m_mgr->slaveIdFor(QStringLiteral("根本没有这台")), 1);
}

// ---------------------------------------------------------------------------
// 协议归一
// ---------------------------------------------------------------------------

void tst_PlcConnectionManager::protocolSpellingVariants_doNotTriggerReconnect() {
    QJsonArray cfg;
    QJsonObject p = makePlc(QStringLiteral("plcA"), QStringLiteral("127.0.0.1"), 1502);
    p["protocol"] = QStringLiteral("modbus");
    cfg.append(p);
    m_mgr->applyConfig(cfg);
    auto* before = m_mgr->driverFor(QStringLiteral("plcA"));

    // ★ 只把协议写法从 "modbus" 改成 "modbus_tcp" —— 指的还是同一台
    QJsonArray cfg2;
    QJsonObject p2 = cfg.at(0).toObject();
    p2["protocol"] = QStringLiteral("modbus_tcp");
    cfg2.append(p2);

    const auto r = m_mgr->applyConfig(cfg2);
    QVERIFY2(r.reconnected.isEmpty(),
             "\"modbus\" 和 \"modbus_tcp\" 归一后是同一个协议,只改写法不该重连。"
             "不归一的话用户在对话框里把下拉框换个显示名就会把现场 PLC 抖一下。");
    QCOMPARE(m_mgr->driverFor(QStringLiteral("plcA")), before);
}

// ---------------------------------------------------------------------------
// 信号
// ---------------------------------------------------------------------------

void tst_PlcConnectionManager::driverAdded_emittedAfterDriverQueryable() {
    // ★ 在槽里立刻查 —— 如果 emit 早于 insert,阶段 5 里
    //   PollScheduler::addDriver 会拿到一个还查不到的 id,而测试照样绿。
    bool queriedInSlot = false;
    bool foundInSlot   = false;
    connect(m_mgr, &PlcConnectionManager::driverAdded, this,
            [this, &queriedInSlot, &foundInSlot](const QString& id, qwhmi::drivers::IPollDriver* d) {
                queriedInSlot = true;
                foundInSlot = (m_mgr->driverFor(id) == d);
            });

    m_mgr->applyConfig(threePlcs());

    QVERIFY2(queriedInSlot, "driverAdded 一条都不该不发。");
    QVERIFY2(foundInSlot,
             "★ emit driverAdded 时 driverFor(id) 必须已经能返回它。"
             "早于入表 emit 的话,接信号的槽拿到的是一个查不到的 id。");
}

void tst_PlcConnectionManager::removalSignal_precedesReplacementAddedSignal() {
    m_mgr->applyConfig(threePlcs());

    QStringList order;
    connect(m_mgr, &PlcConnectionManager::driverAboutToBeRemoved, this,
            [&order](const QString& id) { order.append(QStringLiteral("-") + id); });
    connect(m_mgr, &PlcConnectionManager::driverAdded, this,
            [&order](const QString& id, qwhmi::drivers::IPollDriver*) {
                order.append(QStringLiteral("+") + id);
            });

    QJsonArray cfg = threePlcs();
    QJsonObject p2 = cfg.at(1).toObject();
    p2["port"] = 2503;
    cfg.replace(1, p2);
    m_mgr->applyConfig(cfg);

    // ★ 顺序反了会出事:PollScheduler 在 driverAboutToBeRemoved 里 removeDriver,
    //   如果那时它已经把新 driver 装上去了,removeDriver 会把新的一起摘掉。
    QCOMPARE(order, QStringList({QStringLiteral("-plc2"), QStringLiteral("+plc2")}));
}

void tst_PlcConnectionManager::configApplied_notEmittedWhenNothingChanged() {
    int fired = 0;
    connect(m_mgr, &PlcConnectionManager::configApplied, this, [&fired]() { ++fired; });

    const QJsonArray cfg = threePlcs();
    m_mgr->applyConfig(cfg);
    QCOMPARE(fired, 1);   // 第一次真动了,应该发

    m_mgr->applyConfig(cfg);
    // ★ 幂等重入不发:界面若每次都整表刷新,用户改一个字段点"应用"会看到整表闪一下
    QCOMPARE(fired, 1);
}

// ---------------------------------------------------------------------------
// 原始配置保真
// ---------------------------------------------------------------------------

void tst_PlcConnectionManager::lastConfig_preservesUnknownUserFields() {
    QJsonObject p = makePlc(QStringLiteral("plcA"), QStringLiteral("127.0.0.1"), 1502);
    p["slave_id"] = 3;
    QJsonObject vendor;                 // 代码完全不认识的字段
    vendor["vendor_specific"] = QStringLiteral("某品牌网关");
    vendor["rs485_bus"] = 2;
    p["gateway"] = vendor;
    p["我自己加的字段"] = QStringLiteral("别删我");

    QJsonArray cfg;
    cfg.append(p);
    m_mgr->applyConfig(cfg);

    // ★ lastConfig 必须是【原文】,不是按已知字段重建的。
    //   重建 == 宣告"代码没列的字段一律丢弃"。今天 modbus.timeout_ms 还没接进
    //   调度器,重建一次它就消失了 —— 用户版本控制里会冒出一片"我什么都没改"的 diff。
    //   理由与 Project::saveToFile 的路径 1/2 完全同源。
    QCOMPARE(m_mgr->lastConfig(), cfg);
    QCOMPARE(m_mgr->lastConfig().at(0).toObject().value(QStringLiteral("我自己加的字段")).toString(),
             QStringLiteral("别删我"));
}

QTEST_MAIN(tst_PlcConnectionManager)
#include "tst_PlcConnectionManager.moc"
