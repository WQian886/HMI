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
//   → erase + emit alarmRecovered → AlarmRecorder::onAlarmRecovered
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

// ★ QTEST_GUILESS_MAIN(创建 QCoreApplication,SQLite 需要)
QTEST_GUILESS_MAIN(TestAlarmRecorder)
#include "tst_AlarmRecorder.moc"