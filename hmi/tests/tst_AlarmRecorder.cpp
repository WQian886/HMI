// ============================================================================
// 阶段 D · D.3 · AlarmRecorder 单测
//
// 验证 5 个核心行为:
//   1. open + close 报警历史表
//   2. alarmTriggered → 写 1 行 action=0
//   3. alarmAcknowledged → 写 1 行 action=1
//   4. alarmRecovered → 写 1 行 action=2
//   5. query 时间范围 + level 过滤
//
// 关联:AlarmRecorder.h, AlarmManager.h, Alarm.h
// ============================================================================

#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include "history/AlarmRecorder.h"
#include "../Alarm.h"
#include "../AlarmManager.h"

using namespace qwhmi::history;

class TestChecker : public AlarmChecker {
    Q_OBJECT
public:
    explicit TestChecker(const QString& n, AlarmLevel lvl, QObject* parent = nullptr)
        : AlarmChecker(parent) { setName(n); setLevel(lvl); }
    // ★ 暴露 trigger 给测试用 — 真实 checker 自己 emit
    void fireTrigger(const Alarm& a) { emit triggered(a); }
    void fireRecover(qint64 id) { emit recovered(id); }
};

class TestAlarmRecorder : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();

    void openClose_database();
    void recordTriggered_writesActionZero();
    void recordAcknowledged_writesActionOne();
    void recordRecovered_writesActionTwo();
    void query_filtersByTimeRangeAndLevel();

    // ★ F1(2026-09-30)新增:上面 4 条绿了两个月,但它们只断言了 action 和 id ——
    //   恰好是恢复路径上唯一没坏的两样。补这条把"恢复行的内容"钉死。
    void recoveredRow_actuallyCarriesNameAndLevel();
    void recoveredRow_timeStampIsRealNotEpochZero();

    // ★ F1:archived_ 存档本身的行为
    void recovered_alarmGoesToArchiveWithClearedState();
    void findAlarm_findsRecoveredButNotUnknown();
    void archive_isBounded_dropsOldest();
    void clearAll_alsoClearsArchive();

    // ★ F1:改信号签名引入的回归风险 —— 老的只接 id 的槽还接得上吗
    void legacyIdOnlySlot_stillReceivesRecovery();

    // ★ F2(2026-10-10):老库升级
    void legacyDb_missingColumns_getsThemAdded();
    void legacyDb_existingRowsSurviveUpgrade();
    void freshDb_isNotTouchedByUpgrade();
    void upgrade_isIdempotent_reopenTwice();

private:
    // ★ 公共 fixture:DB + Recorder + Manager + 1 个 TestChecker
    struct Fixture {
        QTemporaryDir tmp;
        QString dbPath;
        AlarmRecorder* ar = nullptr;
        AlarmManager*   mgr = nullptr;
        TestChecker*    checker = nullptr;
        Alarm           sampleAlarm;

        explicit Fixture() {
            QVERIFY(tmp.isValid());
            dbPath = tmp.path() + "/test_alarm.db";

            ar = new AlarmRecorder;
            QVERIFY(ar->open(dbPath));

            mgr = new AlarmManager;
            ar->setSource(mgr);

            checker = new TestChecker("温度高", AlarmLevel::Critical, mgr);
            QVERIFY(mgr->addChecker(checker));

            sampleAlarm.id_           = QDateTime::currentMSecsSinceEpoch();
            sampleAlarm.name          = "温度高";
            sampleAlarm.variableName  = "plc1.Temperature";
            sampleAlarm.level         = AlarmLevel::Critical;
            sampleAlarm.state         = AlarmState::Active;
            sampleAlarm.triggeredAt   = QDateTime::currentDateTime();
            sampleAlarm.triggerValue  = "85.5";
            sampleAlarm.threshold     = ">= 80.0";
        }
        ~Fixture() {
            delete ar;
            delete mgr;  // 析构 checker
        }
    };
};

void TestAlarmRecorder::initTestCase() {
    qInfo() << "[tst_AlarmRecorder] init";
}

void TestAlarmRecorder::cleanupTestCase() {
    qInfo() << "[tst_AlarmRecorder] cleanup";
}

// ★ 用例 1:open + close
void TestAlarmRecorder::openClose_database() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString dbPath = tmp.path() + "/test.db";

    AlarmRecorder ar;
    QVERIFY(!ar.isOpen());
    QVERIFY(ar.open(dbPath));
    QVERIFY(ar.isOpen());
    ar.close();
    QVERIFY(!ar.isOpen());
}

// ★ 用例 2:alarmTriggered → 写 1 行 action=0
void TestAlarmRecorder::recordTriggered_writesActionZero() {
    Fixture f;
    f.checker->fireTrigger(f.sampleAlarm);
    QTest::qWait(50);   // 让信号跨连接落库

    QCOMPARE(f.ar->totalCount(), 1);

    const auto from = f.sampleAlarm.triggeredAt.addMSecs(-1000).toMSecsSinceEpoch();
    const auto to   = f.sampleAlarm.triggeredAt.addMSecs(1000).toMSecsSinceEpoch();
    const auto records = f.ar->query(from, to);
    QCOMPARE(records.size(), 1);
    QCOMPARE(records[0].action, 0);                 // 0=triggered
    QCOMPARE(records[0].alarmId, f.sampleAlarm.id_);
    QCOMPARE(records[0].name, QString("温度高"));
    QCOMPARE(records[0].level, static_cast<int>(AlarmLevel::Critical));
    QCOMPARE(records[0].triggerValue, QString("85.5"));
}

// ★ 用例 3:alarmAcknowledged 路径 → 写 1 行 action=1
//   Qt6 信号不能从类外调,但 QMetaObject::invokeMethod 可绕开 private 调 slot
//   (生产路径:AlarmManager::acknowledge() → 权限检查 → emit alarmAcknowledged →
//      AlarmRecorder::onAlarmAcknowledged 槽)
void TestAlarmRecorder::recordAcknowledged_writesActionOne() {
    Fixture f;
    f.checker->fireTrigger(f.sampleAlarm);
    QTest::qWait(50);

    // ★ QMetaObject::invokeMethod 调 private slot(按名字)
    const bool invoked = QMetaObject::invokeMethod(f.ar, "onAlarmAcknowledged",
                                                   Qt::DirectConnection,
                                                   Q_ARG(qint64, f.sampleAlarm.id_));
    qInfo() << "[tst_AlarmRecorder] invokeMethod returned:" << invoked;
    QTest::qWait(50);

    QCOMPARE(f.ar->totalCount(), 2);   // triggered + acknowledged

    const auto from = QDateTime::currentDateTime().addMSecs(-2000).toMSecsSinceEpoch();
    const auto to   = QDateTime::currentDateTime().addMSecs(2000).toMSecsSinceEpoch();
    const auto records = f.ar->query(from, to);
    qInfo() << "[tst_AlarmRecorder] ack query returned" << records.size() << "rows";
    for (const auto& r : records) {
        qInfo() << "  alarmId=" << r.alarmId << "action=" << r.action << "level=" << r.level;
    }
    bool foundAck = false;
    for (const auto& r : records) {
        if (r.action == 1 && r.alarmId == f.sampleAlarm.id_) {
            foundAck = true;
            break;
        }
    }
    QVERIFY(foundAck);
}

// ★ 用例 4:alarmRecovered → 写 1 行 action=2
//   用 TestChecker fireRecover → AlarmChecker::recovered → AlarmManager::onCheckerRecovered
//   → 进 archived_ + emit alarmRecovered(id, recovered) → AlarmRecorder::onAlarmRecovered
//
//   ⚠ 这条用例本身【太弱】—— 只断言 action==2 && id 匹配,恰好是这条路径上
//     唯一没坏的两样,所以它绿了两个月而恢复行的内容一直是空的。
//     真正的内容断言在下面 recoveredRow_actuallyCarriesNameAndLevel()。
//     保留弱断言是因为它是链路连通性的探针(信号断了它会红),
//     但别再把它当成"恢复路径没问题"的证据。
void TestAlarmRecorder::recordRecovered_writesActionTwo() {
    Fixture f;
    f.checker->fireTrigger(f.sampleAlarm);
    QTest::qWait(50);

    f.checker->fireRecover(f.sampleAlarm.id_);
    QTest::qWait(50);

    QCOMPARE(f.ar->totalCount(), 2);   // triggered + recovered

    const auto from = QDateTime::currentDateTime().addMSecs(-2000).toMSecsSinceEpoch();
    const auto to   = QDateTime::currentDateTime().addMSecs(2000).toMSecsSinceEpoch();
    const auto records = f.ar->query(from, to);
    int recoverCount = 0;
    for (const auto& r : records) {
        if (r.action == 2 && r.alarmId == f.sampleAlarm.id_) {
            ++recoverCount;
        }
    }
    QCOMPARE(recoverCount, 1);
}

// ★ 用例 5:query 时间范围 + level 过滤
void TestAlarmRecorder::query_filtersByTimeRangeAndLevel() {
    Fixture f;

    // 写 2 个不同 level 的报警
    Alarm infoAlarm = f.sampleAlarm;
    infoAlarm.id_ = QDateTime::currentMSecsSinceEpoch() + 1;
    infoAlarm.name = "心跳";
    infoAlarm.level = AlarmLevel::Info;
    f.checker->fireTrigger(f.sampleAlarm);  // Critical
    f.checker->fireTrigger(infoAlarm);       // Info
    QTest::qWait(50);

    QCOMPARE(f.ar->totalCount(), 2);

    const auto from = QDateTime::currentDateTime().addMSecs(-2000).toMSecsSinceEpoch();
    const auto to   = QDateTime::currentDateTime().addMSecs(2000).toMSecsSinceEpoch();

    // ★ 不带 level 过滤 → 2 条
    const auto all = f.ar->query(from, to);
    QCOMPARE(all.size(), 2);

    // ★ minLevel=2 (Critical) → 只 1 条
    const auto onlyCritical = f.ar->query(from, to, /*minLevel=*/2);
    QCOMPARE(onlyCritical.size(), 1);
    QCOMPARE(onlyCritical[0].level, static_cast<int>(AlarmLevel::Critical));
}

// ============================================================================
// ★ F1(2026-09-30)新增用例 —— 这两条是把"报警历史"这个卖点钉死的
//
// 【背景】阶段 D.3 收口时给 F6 打的 ✅ 是"4 档 + 列表 + 闪烁 + 触发/确认/恢复三态",
//   其中"历史"靠 AlarmRecorder 落 SQLite。但 2026-09-30 开 F 阶段时读代码发现:
//
//   AlarmManager::onCheckerRecovered (AlarmManager.cpp:107-113) 的顺序是
//       1. it->recoveredAt = now()      ← 写时间戳
//       2. activeAlarms_.erase(it)      ← 删掉整条记录(对象销毁)
//       3. emit alarmRecovered(id)      ← 只传 id,内容不传
//
//   而 AlarmRecorder::onAlarmRecovered (AlarmRecorder.cpp:185-199) 靠
//       遍历 mgr_->activeAlarms() 找完整数据
//   —— 刚 erase 掉的那条,遍历时【必然找不到】,于是走到末尾兜底:
//       insertAction(id, {}, 0, action=2, now())
//
//   结果:历史表里【每一条"恢复"行】的
//       name / level / variable_name / trigger_value / threshold 全是空的。
//       查出来是一行只有 id 和时间的孤儿。
//
// 【为什么旧测试没发现】recordRecovered_writesActionTwo 只断言了
//   `r.action == 2 && r.alarmId == id` —— 恰好是这条路径上唯一没坏的��样。
//   断言挑在了不会红的地方,所以它绿了两个月。
//
// 【测法】不 mock 任何东西,走生产链路:
//   TestChecker::fireTrigger → AlarmChecker::triggered → AlarmManager::onCheckerTriggered
//   → emit alarmTriggered → AlarmRecorder 落一行(这行是好的)
//   TestChecker::fireRecover → recovered → AlarmManager::onCheckerRecovered
//   → erase + emit alarmRecovered → AlarmRecorder 落一行(这行是空的)
// ============================================================================

void TestAlarmRecorder::recoveredRow_actuallyCarriesNameAndLevel() {
    Fixture f;
    f.checker->fireTrigger(f.sampleAlarm);
    QTest::qWait(50);
    f.checker->fireRecover(f.sampleAlarm.id_);
    QTest::qWait(50);

    const auto from = QDateTime::currentDateTime().addMSecs(-5000).toMSecsSinceEpoch();
    const auto to   = QDateTime::currentDateTime().addMSecs(5000).toMSecsSinceEpoch();
    const auto records = f.ar->query(from, to);

    // 找那条恢复行
    const AlarmRecorder::Record* rec = nullptr;
    for (const auto& r : records) {
        if (r.action == 2 && r.alarmId == f.sampleAlarm.id_) { rec = &r; break; }
    }
    QVERIFY2(rec, "没找到恢复行 —— 信号链路本身就断了,先查 connect");

    // ★ 这 4 条正是现在会红的地方
    QVERIFY2(!rec->name.isEmpty(),
             qPrintable(QStringLiteral("恢复行的 name 是空的 —— 现场排除法:AlarmManager 先 erase "
                                       "再 emit,AlarmRecorder 靠遍历 activeAlarms() 取数据,"
                                       "必然取不到。历史查询出来是一行只有 id 的孤儿。")));
    QCOMPARE(rec->level, static_cast<int>(AlarmLevel::Critical));
    QVERIFY2(!rec->variableName.isEmpty(), "恢复行的 variable_name 是空的");
    QVERIFY2(!rec->triggerValue.isEmpty(), "恢复行的 trigger_value 是空的");

    // 同一 id 的触发行必须是好的 —— 反证:不是"整个测试环境没数据",
    // 而是"恢复这一段单独坏了"。没有这条,上面的红可能是 fixture 没配好。
    bool triggeredRowOk = false;
    for (const auto& r : records) {
        if (r.action == 0 && r.alarmId == f.sampleAlarm.id_) {
            triggeredRowOk = (r.name == QString("温度高"));
        }
    }
    QVERIFY2(triggeredRowOk, "连触发行都是空的 —— 那是 fixture 或整条链路的问题,不是恢复路径");
}

void TestAlarmRecorder::recoveredRow_timeStampIsRealNotEpochZero() {
    // 时间戳走的是另一条路:AlarmRecorder 兜底用 currentDateTime(),
    // 所以这行【时间是对的】。但正因为它对,光看"时间窗查得到"会以为整行都对 ——
    // 这正是旧测试漏掉它的原因。单独钉一条,防以后有人把兜底也改坏。
    Fixture f;
    f.checker->fireTrigger(f.sampleAlarm);
    QTest::qWait(50);
    const qint64 before = QDateTime::currentMSecsSinceEpoch();
    f.checker->fireRecover(f.sampleAlarm.id_);
    QTest::qWait(50);

    const auto records = f.ar->query(before - 5000, QDateTime::currentMSecsSinceEpoch() + 5000);
    for (const auto& r : records) {
        if (r.action == 2 && r.alarmId == f.sampleAlarm.id_) {
            const qint64 atMs = r.at.toMSecsSinceEpoch();
            QVERIFY2(r.at.isValid() && atMs >= before - 1000,
                     qPrintable(QStringLiteral("恢复行时间戳无效或异常: valid=%1 at=%2")
                                    .arg(r.at.isValid()).arg(atMs)));
            return;
        }
    }
    QVERIFY2(false, "没找到恢复行");
}

// ============================================================================
// ★ F1(2026-09-30):archived_ 存档的行为
//
// 【为什么要存档】原来恢复时 erase 掉整条记录,acknowledgedAt / state 当场蒸发。
//   现场最常见的一种情况恰恰是"没来得及确认,值自己回来了" ——
//   出了事要查"当时到底确认了没有",查不到就只能靠猜。
//   注释里写"完整版应该保留 archived_ 存档,但 5.4 UI 暂时不需要" ——
//   "暂时不需要"这个判断,代价由 6.x 接 AlarmRecorder 的人承担了。
// ============================================================================

void TestAlarmRecorder::recovered_alarmGoesToArchiveWithClearedState() {
    Fixture f;
    f.checker->fireTrigger(f.sampleAlarm);
    QTest::qWait(50);
    f.checker->fireRecover(f.sampleAlarm.id_);
    QTest::qWait(50);

    // 恢复后不该还在 active 里
    QCOMPARE(f.mgr->count(), 0);
    QVERIFY(!f.mgr->contains(f.sampleAlarm.id_));

    // 应该在存档里
    // ★ 注意不能写 `const Alarm& a = f.mgr->archivedAlarms().first();`
    //   —— archivedAlarms() 按值返回 QList 临时对象,.first() 返的是
    //   指向临时内部元素的引用,而【生命周期延长不适用于返回引用的成员函数】
    //   (只适用于 prvalue)。那条写法是悬垂引用,读到的是已释放内存,
    //   id_ 恰好对不上,排查起来会以为是存档逻辑错了。
    const QList<Alarm> arch = f.mgr->archivedAlarms();
    QCOMPARE(arch.size(), 1);
    const Alarm& a = arch.first();
    QCOMPARE(a.id_, f.sampleAlarm.id_);
    QCOMPARE(a.name, QString("温度高"));
    QCOMPARE(a.level, AlarmLevel::Critical);
    // ★ 三个时间戳:触发有、恢复有
    QVERIFY(a.triggeredAt.isValid());
    QVERIFY(a.recoveredAt.isValid());
    // ★ state=Cleared —— 这个枚举值定义在 Alarm.h:24 一直没人写,是死代码
    QVERIFY2(a.state == AlarmState::Cleared,
             "恢复的记录 state 该是 Cleared,否则这个枚举值仍然是死代码");
}

void TestAlarmRecorder::findAlarm_findsRecoveredButNotUnknown() {
    Fixture f;
    f.checker->fireTrigger(f.sampleAlarm);
    QTest::qWait(50);

    // active 时能找到
    QVERIFY(f.mgr->findAlarm(f.sampleAlarm.id_) != nullptr);

    f.checker->fireRecover(f.sampleAlarm.id_);
    QTest::qWait(50);

    // ★ 恢复后(activeAlarms_ 里已经没有)仍然能找到 —— 这正是存档的意义
    const Alarm* found = f.mgr->findAlarm(f.sampleAlarm.id_);
    QVERIFY2(found, "恢复后 findAlarm 找不到 —— 存档没起作用");
    QCOMPARE(found->name, QString("温度高"));

    // 不存在的 id 返回 nullptr,不能崩也不能瞎指
    QVERIFY(f.mgr->findAlarm(123456789LL) == nullptr);
}

void TestAlarmRecorder::archive_isBounded_dropsOldest() {
    // 7×24 不关机的机器,没人会来点"清存档" —— 无界增长的结果是
    // 跑几个月吃光内存,而报警数据本该在 SQLite 里。
    Fixture f;
    const int total = AlarmManager::kMaxArchived + 20;

    for (int i = 0; i < total; ++i) {
        Alarm a = f.sampleAlarm;
        a.id_ = f.sampleAlarm.id_ + i;      // 每个 id 不同,避免被"重复触发"分支吞掉
        a.name = QStringLiteral("A%1").arg(i);
        f.checker->fireTrigger(a);
        f.checker->fireRecover(a.id_);
    }
    QTest::qWait(100);

    // ★ 关键性质:【有界】,不随恢复次数无限涨
    QCOMPARE(f.mgr->archivedAlarms().size(), AlarmManager::kMaxArchived);

    // 丢的是【最旧的】,留的是最近的 —— 事故追溯要看最近的
    const auto arch = f.mgr->archivedAlarms();
    const int newest = total - 1;
    QVERIFY2(arch.last().name == QStringLiteral("A%1").arg(newest),
             qPrintable(QStringLiteral("存档最后一条应是最近的 A%1,实际是 %2")
                            .arg(newest).arg(arch.last().name)));
    // 最旧的 A0 已被丢掉
    QVERIFY2(arch.first().name != QStringLiteral("A0"),
             "最旧的 A0 还在 —— 说明丢的不是最旧的,或者根本没丢");
}

void TestAlarmRecorder::clearAll_alsoClearsArchive() {
    // Project::loadFromFile 开头调 clearAll()。换工程时留着上个工程的存档,
    // findAlarm() 会串号 —— 同一个 id 在两个工程里都出现过的话,
    // 上一个工程的记录会冒充这个工程的。
    Fixture f;
    f.checker->fireTrigger(f.sampleAlarm);
    QTest::qWait(50);
    f.checker->fireRecover(f.sampleAlarm.id_);
    QTest::qWait(50);
    QCOMPARE(f.mgr->archivedAlarms().size(), 1);

    f.mgr->clearAll();
    QVERIFY2(f.mgr->archivedAlarms().isEmpty(),
             "clearAll 没清存档 —— 换工程时 findAlarm 会串到上个工程的数据");
}

// ============================================================================
// ★ F1:改信号签名带来的回归风险
//
// alarmRecovered 从 (qint64) 变成 (qint64, const Alarm&)。全仓有两个消费者
// 是老签名,只接 id:
//   - hmi::onAlarmRecovered(qint64)          hmi.cpp:1350
//   - AlarmListControl::onAlarmRecovered(qint64)  AlarmListControl.h:49
// 两者都只按 id 更新一行文字,不读 Alarm 内容。
//
// 【为什么专门写测试而不是"读一眼就知道"】
//   Qt 允许"参数多的信号 → 参数少的槽",所以理论上没事。但这条规则
//   没有任何编译期检查兜底:哪天有人给这两个槽加了个用 Alarm 的逻辑,
//   或者信号顺序改错,都是静默失配 —— 症状是"UI 不更新了",
//   跟代码看着没问题完全不搭边,极难查。
//   这正是本项目反复吃的那类亏(静默失败),所以钉一条。
// ============================================================================
namespace {
class LegacyIdOnlySlot : public QObject {
    Q_OBJECT
public:
    int recoveredCount = 0;
    qint64 lastId = 0;
public slots:
    // ★ 故意用老签名:少一个参数
    void onAlarmRecovered(qint64 id) { ++recoveredCount; lastId = id; }
};
} // namespace

void TestAlarmRecorder::legacyIdOnlySlot_stillReceivesRecovery() {
    Fixture f;
    LegacyIdOnlySlot legacy;
    QObject::connect(f.mgr, &AlarmManager::alarmRecovered,
                     &legacy, &LegacyIdOnlySlot::onAlarmRecovered);

    f.checker->fireTrigger(f.sampleAlarm);
    QTest::qWait(50);
    f.checker->fireRecover(f.sampleAlarm.id_);
    QTest::qWait(50);

    QVERIFY2(legacy.recoveredCount == 1,
             "只接 id 的老槽没收到恢复信号 —— 信号多带参数后失配了。"
             "hmi.cpp:1350 和 AlarmListControl.h:49 都是这个签名,"
             "它们失配的话 UI 就不更新了,而且不会有任何编译错误。");
    QCOMPARE(legacy.lastId, f.sampleAlarm.id_);
}

// ============================================================================
// ★ F2(2026-10-10):老库升级
//
// 【要防的事】AlarmRecorder::ensureSchema() 原来只有
//     CREATE TABLE IF NOT EXISTS alarm_history (...)
// 而 IF NOT EXISTS 的语义是"表在就啥也不做"。于是老库一旦少了列,
// 它就一直少着 —— 直到用户现场第一次写报警,INSERT 才抛 "no such column: user",
// 报警历史当场哑掉。这是典型的【测试全绿、装到现场才炸】:
// 任何新装的环境都建的是完整表,老库升级这条路径压根没人走过。
//
// 【怎么测才有意义】光断言"升级后列齐了"是不够的 ——
// 不加迁移时代码照样能建出完整表(new DB 走 CREATE 就齐了),
// 那条断言在加迁移之前【也是绿的】,测了个寂寞。
// 所以每条用例都从【手搓一个残缺的老库】开始:
// 形状照 D.3 提交时的样子,故意少两列,然后让生产代码去打开它。
// 不加迁移 → PRAGMA 查不到 user 列 → 断言红。
// ============================================================================

namespace {

// 手搓一个"老库":D.3 时期的表,故意少 user / threshold 两列。
// 刻意不用 AlarmRecorder::open() 建 —— 那建出来的是【新】表,正是要模拟的东西的反面。
bool createLegacyDb(const QString& path, QString* err) {
    if (QSqlDatabase::contains("legacy_alarm")) {
        QSqlDatabase::removeDatabase("legacy_alarm");
    }
    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", "legacy_alarm");
    db.setDatabaseName(path);
    if (!db.open()) { *err = db.lastError().text(); return false; }
    QSqlQuery q(db);
    if (!q.exec(R"(
        CREATE TABLE IF NOT EXISTS alarm_history (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            alarm_id INTEGER NOT NULL,
            name TEXT,
            level INTEGER,
            action INTEGER NOT NULL,
            action_at INTEGER NOT NULL,
            variable_name TEXT,
            trigger_value TEXT
        )
    )")) { *err = q.lastError().text(); db.close(); return false; }
    db.close();
    QSqlDatabase::removeDatabase("legacy_alarm");
    return true;
}

// 查表里有哪些列(PRAGMA table_info 的第 1 列是列名)
QStringList columnsOf(const QString& path, const QString& table) {
    QStringList cols;
    const QString conn = "cols_" + QString::number(reinterpret_cast<quintptr>(&cols));
    if (QSqlDatabase::contains(conn)) QSqlDatabase::removeDatabase(conn);
    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", conn);
    db.setDatabaseName(path);
    if (!db.open()) { QSqlDatabase::removeDatabase(conn); return cols; }
    QSqlQuery q(db);
    if (q.exec(QStringLiteral("PRAGMA table_info(%1)").arg(table))) {
        while (q.next()) cols << q.value(1).toString();
    }
    db.close();
    QSqlDatabase::removeDatabase(conn);
    return cols;
}

int rowCountOf(const QString& path) {
    const QString conn = "cnt_" + QString::number(static_cast<int>(path.size()));
    if (QSqlDatabase::contains(conn)) QSqlDatabase::removeDatabase(conn);
    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", conn);
    db.setDatabaseName(path);
    int n = -1;
    if (db.open()) {
        QSqlQuery q(db);
        if (q.exec("SELECT COUNT(*) FROM alarm_history") && q.next()) {
            n = q.value(0).toInt();
        }
        db.close();
    }
    QSqlDatabase::removeDatabase(conn);
    return n;
}

} // namespace

// ★ 老库少两列 → 打开后自动补上
void TestAlarmRecorder::legacyDb_missingColumns_getsThemAdded() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString path = tmp.path() + "/legacy.db";

    QString err;
    QVERIFY2(createLegacyDb(path, &err),
             qPrintable(QStringLiteral("造老库失败:%1").arg(err)));
    // 先确认老库确实是残缺的 —— 不然下面的绿可能只是"本来就有"
    const QStringList before = columnsOf(path, "alarm_history");
    QVERIFY2(!before.contains(QStringLiteral("user")) &&
             !before.contains(QStringLiteral("threshold")),
             qPrintable(QStringLiteral("手搓的老库本该缺 user/threshold,实际列:%1")
                            .arg(before.join(","))));

    {
        AlarmRecorder ar;
        QVERIFY2(ar.open(path), "打开老库失败");
    }

    const QStringList after = columnsOf(path, "alarm_history");
    for (const QString& want : { "user", "threshold" }) {
        QVERIFY2(after.contains(want),
                 qPrintable(QStringLiteral("升级后仍然缺列 %1,实际列:%2")
                                .arg(want).arg(after.join(","))));
    }
    // 反向:不该凭空多出列
    QCOMPARE(after.size(), before.size() + 2);
}

// ★ 升级不能动老数据 —— 报警历史是事故追溯的凭据,升级把它清了就等于删证据
void TestAlarmRecorder::legacyDb_existingRowsSurviveUpgrade() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString path = tmp.path() + "/legacy_rows.db";

    QString err;
    QVERIFY2(createLegacyDb(path, &err), qPrintable(err));

    // 往老库里塞 3 行(只写老库有的列)
    {
        const QString conn = "seed";
        QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", conn);
        db.setDatabaseName(path);
        QVERIFY(db.open());
        QSqlQuery q(db);
        q.prepare(R"(
            INSERT INTO alarm_history
                (alarm_id, name, level, action, action_at, variable_name, trigger_value)
            VALUES (?, ?, ?, ?, ?, ?, ?)
        )");
        const qint64 t0 = QDateTime::currentMSecsSinceEpoch();
        for (int i = 0; i < 3; ++i) {
            q.addBindValue(1000 + i);
            q.addBindValue(QStringLiteral("老报警%1").arg(i));
            q.addBindValue(2);
            q.addBindValue(0);
            q.addBindValue(t0 + i);
            q.addBindValue(QStringLiteral("plc1.T%1").arg(i));
            q.addBindValue(QStringLiteral("%1.5").arg(i));
            QVERIFY(q.exec());
        }
        db.close();
        QSqlDatabase::removeDatabase(conn);
    }
    QCOMPARE(rowCountOf(path), 3);

    { AlarmRecorder ar; QVERIFY(ar.open(path)); }

    QCOMPARE(rowCountOf(path), 3);

    // 内容也要在,不能只是行数对(行数对但内容被清空也是事故)
    // ★ 刻意不用 lambda 包 —— QVERIFY 失败时展开成 return;,
    //   有返回值的 lambda 里塞它编译不过(实测 C2562)。
    AlarmRecorder ar;
    QVERIFY(ar.open(path));
    const auto recs = ar.query(QDateTime::currentMSecsSinceEpoch() - 60000,
                               QDateTime::currentMSecsSinceEpoch() + 60000);
    ar.close();
    QCOMPARE(recs.size(), 3);
    QVERIFY2(recs[0].name == QStringLiteral("老报警0"),
             qPrintable(QStringLiteral("老数据内容变了,读到:%1").arg(recs[0].name)));
}

// ★ 新库不该被"升级"碰 —— 顺带钉住列清单本身
//   (建表定义由 kColumns 生成,这条能挡住"清单和实际表结构对不上")
void TestAlarmRecorder::freshDb_isNotTouchedByUpgrade() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString path = tmp.path() + "/fresh.db";

    { AlarmRecorder ar; QVERIFY(ar.open(path)); }
    const QStringList cols = columnsOf(path, "alarm_history");

    QCOMPARE(cols.size(), 10);
    for (const QString& want : { "id", "alarm_id", "name", "level", "action",
                                 "action_at", "variable_name", "trigger_value",
                                 "threshold", "user" }) {
        QVERIFY2(cols.contains(want),
                 qPrintable(QStringLiteral("新库缺列 %1,实际列:%2")
                                .arg(want).arg(cols.join(","))));
    }
}

// ★ 幂等:同一文件开两次,第二次不该再 ALTER(否则每次启动都改一次表)
void TestAlarmRecorder::upgrade_isIdempotent_reopenTwice() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString path = tmp.path() + "/twice.db";

    QString err;
    QVERIFY2(createLegacyDb(path, &err), qPrintable(err));
    const QStringList first = columnsOf(path, "alarm_history");

    { AlarmRecorder ar; QVERIFY(ar.open(path)); }
    const QStringList second = columnsOf(path, "alarm_history");

    { AlarmRecorder ar; QVERIFY(ar.open(path)); }
    const QStringList third = columnsOf(path, "alarm_history");

    QCOMPARE(second, third);
    // 补过之后不该再翻倍 —— 不幂等的话这里会是 first+4
    QCOMPARE(second.size(), first.size() + 2);
}

// ★ QTEST_GUILESS_MAIN(创建 QCoreApplication,SQLite 需要)
QTEST_GUILESS_MAIN(TestAlarmRecorder)
#include "tst_AlarmRecorder.moc"