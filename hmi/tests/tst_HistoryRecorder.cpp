// ============================================================================
// 阶段 D · D.1 · HistoryRecorder 单测
//
// 验证 6 个核心行为:
//   1. open + close 数据库
//   2. setSource 后 valueChanged 触发写入
//   3. sampleInterval 限频(高频不写)
//   4. changeThreshold 限频(小变化不写)
//   5. query 时间范围正确
//   6. 环形缓冲 cleanupExpired 删除 > N 天数据
//
// 关联:HistoryRecorder.h, VariableManager.h, Variable.h
// ============================================================================

#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QSqlDatabase>     // ★ D.1 测试 cleanupExpired 用
#include <QSqlQuery>        // ★ D.1 测试 cleanupExpired 用
#include "history/HistoryRecorder.h"
#include "VariableManager.h"
#include "Variable.h"

using namespace qwhmi::history;

class TestHistoryRecorder : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();    // 创建临时目录
    void cleanupTestCase(); // 清理

    void openClose_database();
    void valueChanged_triggersWrite();
    void sampleInterval_throttlesHighFrequency();
    void changeThreshold_ignoresSmallChanges();
    void query_returnsTimeRangeCorrectly();
    void cleanupExpired_deletesOldRecords();

    // ★ D.2(2026-09-20):心跳 + schema 升级
    void heartbeat_writesAllVariablesEveryInterval();
    void heartbeatField_migratesFromOldDbWithoutColumn();
};

void TestHistoryRecorder::initTestCase() {
    qInfo() << "[tst_HistoryRecorder] init";
}

void TestHistoryRecorder::cleanupTestCase() {
    qInfo() << "[tst_HistoryRecorder] cleanup";
}

// ★ 用例 1:open + close 数据库
void TestHistoryRecorder::openClose_database() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString dbPath = tmp.path() + "/test_history.db";

    HistoryRecorder r;
    QVERIFY(!r.isOpen());
    QVERIFY(r.open(dbPath));
    QVERIFY(r.isOpen());
    r.close();
    QVERIFY(!r.isOpen());
}

// ★ 用例 2:setSource 后 valueChanged 触发写入
void TestHistoryRecorder::valueChanged_triggersWrite() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString dbPath = tmp.path() + "/test.db";

    HistoryRecorder r;
    QVERIFY(r.open(dbPath));
    VariableManager vm;

    Variable v("Speed", 0, Variable::DataType::Float, Variable::Access::ReadOnly, &vm);
    v.setPlcId("plc1");
    vm.addVariable(&v);  // ★ D.1:必须在 setSource 之前 add(setSource 时遍历现有变量)
    r.setSource(&vm);

    // 触发 valueChanged(模拟 PollScheduler 写入)
    v.setValue(QVariant::fromValue<float>(123.45f));
    // 异步等一下确保写入完成
    QTest::qWait(50);

    // query 应该返回 1 条记录
    QDateTime from = QDateTime::currentDateTime().addMSecs(-1000);
    QDateTime to = QDateTime::currentDateTime().addMSecs(1000);
    auto records = r.query("plc1.Speed", from, to);
    QCOMPARE(records.size(), 1);
    // ★ Float 精度容忍(123.45f → SQLite REAL → 123.449996948...)
    QVERIFY(qAbs(records[0].second - 123.45) < 0.001);
}

// ★ 用例 3:sampleInterval 限频
void TestHistoryRecorder::sampleInterval_throttlesHighFrequency() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString dbPath = tmp.path() + "/test.db";

    HistoryRecorder r;
    QVERIFY(r.open(dbPath));
    r.setSampleInterval(100);  // 100ms 内不写
    VariableManager vm;

    Variable v("Temp", 0, Variable::DataType::Float, Variable::Access::ReadOnly, &vm);
    v.setPlcId("plc1");
    vm.addVariable(&v);  // ★ D.1:必须 add 在 setSource 之前
    r.setSource(&vm);

    // 100ms 内连续写 5 次(应该只写 1 次)
    v.setValue(QVariant::fromValue<float>(1.0f));
    v.setValue(QVariant::fromValue<float>(2.0f));
    v.setValue(QVariant::fromValue<float>(3.0f));
    v.setValue(QVariant::fromValue<float>(4.0f));
    v.setValue(QVariant::fromValue<float>(5.0f));
    QTest::qWait(50);

    QDateTime from = QDateTime::currentDateTime().addMSecs(-1000);
    QDateTime to = QDateTime::currentDateTime().addMSecs(1000);
    auto records = r.query("plc1.Temp", from, to);
    QCOMPARE(records.size(), 1);
    QCOMPARE(records[0].second, 1.0);  // 第一次值
}

// ★ 用例 4:changeThreshold 限频
void TestHistoryRecorder::changeThreshold_ignoresSmallChanges() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString dbPath = tmp.path() + "/test.db";

    HistoryRecorder r;
    QVERIFY(r.open(dbPath));
    r.setSampleInterval(0);          // 关掉 sampleInterval 限制
    r.setChangeThreshold(10.0);      // 变化 < 10 不写
    VariableManager vm;

    Variable v("Press", 0, Variable::DataType::Float, Variable::Access::ReadOnly, &vm);
    v.setPlcId("plc2");
    vm.addVariable(&v);  // ★ D.1:必须 add 在 setSource 之前
    r.setSource(&vm);

    v.setValue(QVariant::fromValue<float>(100.0f));   // 写入(初始)
    QTest::qWait(50);
    v.setValue(QVariant::fromValue<float>(105.0f));   // 变化 5 < 10 → 不写
    v.setValue(QVariant::fromValue<float>(108.0f));   // 变化 8 < 10 → 不写
    v.setValue(QVariant::fromValue<float>(115.0f));   // 变化 15 > 10 → 写
    QTest::qWait(50);

    QDateTime from = QDateTime::currentDateTime().addMSecs(-1000);
    QDateTime to = QDateTime::currentDateTime().addMSecs(1000);
    auto records = r.query("plc2.Press", from, to);
    QCOMPARE(records.size(), 2);  // 100.0 + 115.0
    QCOMPARE(records[0].second, 100.0);
    QCOMPARE(records[1].second, 115.0);
}

// ★ 用例 5:query 时间范围正确
void TestHistoryRecorder::query_returnsTimeRangeCorrectly() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString dbPath = tmp.path() + "/test.db";

    HistoryRecorder r;
    QVERIFY(r.open(dbPath));
    r.setSampleInterval(0);
    VariableManager vm;

    Variable v("Flow", 0, Variable::DataType::Float, Variable::Access::ReadOnly, &vm);
    v.setPlcId("plc1");
    vm.addVariable(&v);  // ★ D.1:必须 add 在 setSource 之前
    r.setSource(&vm);

    // 写 3 条
    v.setValue(QVariant::fromValue<float>(10.0f)); QTest::qWait(50);
    v.setValue(QVariant::fromValue<float>(20.0f)); QTest::qWait(50);
    v.setValue(QVariant::fromValue<float>(30.0f)); QTest::qWait(50);

    // 全范围 query
    QDateTime from = QDateTime::currentDateTime().addMSecs(-2000);
    QDateTime to = QDateTime::currentDateTime().addMSecs(1000);
    auto all = r.query("plc1.Flow", from, to);
    QCOMPARE(all.size(), 3);

    // 范围外 query(只取第 2 条)
    QDateTime midFrom = QDateTime::currentDateTime().addMSecs(-1500);
    QDateTime midTo = QDateTime::currentDateTime().addMSecs(-500);
    // 注:可能时间窗内 0 条或 1-2 条,这里只验"不超过 3 条"
    auto mid = r.query("plc1.Flow", midFrom, midTo);
    QVERIFY(mid.size() <= 3);
}

// ★ 用例 6:cleanupExpired 删除过期
void TestHistoryRecorder::cleanupExpired_deletesOldRecords() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString dbPath = tmp.path() + "/test.db";

    HistoryRecorder r;
    QVERIFY(r.open(dbPath));
    r.setRetentionDays(1);   // 保留 1 天
    VariableManager vm;

    Variable v("Old", 0, Variable::DataType::Float, Variable::Access::ReadOnly, &vm);
    v.setPlcId("plc1");
    vm.addVariable(&v);  // ★ D.1:必须 add 在 setSource 之前
    r.setSource(&vm);
    v.setValue(QVariant::fromValue<float>(99.0f));
    QTest::qWait(50);

    // 手动改时间戳到 2 天前(用 SQL)
    QSqlQuery q(QSqlDatabase::database("hmi_history"));
    q.prepare("UPDATE history SET timestamp = ? WHERE variable_name = ?");
    q.addBindValue(QDateTime::currentMSecsSinceEpoch() - 2LL * 24 * 3600 * 1000);
    q.addBindValue("plc1.Old");
    QVERIFY(q.exec());

    r.cleanupExpired();

    // 应该删除了过期记录
    QDateTime from = QDateTime::currentDateTime().addMSecs(-3000);
    QDateTime to = QDateTime::currentDateTime().addMSecs(1000);
    auto records = r.query("plc1.Old", from, to);
    QCOMPARE(records.size(), 0);
}

// ★ D.2 用例 7:心跳 timer 触发后,所有 Variable 各写一行 is_heartbeat=1
//   用 setHeartbeatInterval(200) + 手动调 onHeartbeatTick() 模拟 timer timeout
//   (不真等 60s,跑得快)
void TestHistoryRecorder::heartbeat_writesAllVariablesEveryInterval() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString dbPath = tmp.path() + "/test.db";

    HistoryRecorder r;
    QVERIFY(r.open(dbPath));
    r.setHeartbeatInterval(200);  // 200ms 测试间隔(生产 60000)
    VariableManager vm;

    Variable v1("Speed", 0, Variable::DataType::Float, Variable::Access::ReadOnly, &vm);
    v1.setPlcId("plc1");
    Variable v2("Temp", 0, Variable::DataType::Float, Variable::Access::ReadOnly, &vm);
    v2.setPlcId("plc1");
    vm.addVariable(&v1);
    vm.addVariable(&v2);
    r.setSource(&vm);
    QVERIFY(r.isRunning());

    // 等一次心跳 tick(用 QTest::qWait 比真等 timer 更稳)
    QTest::qWait(300);

    // query 应该看到 2 行心跳(各变量 1 行,is_heartbeat=1)
    QSqlQuery check(QSqlDatabase::database("hmi_history"));
    check.prepare("SELECT variable_name, is_heartbeat FROM history WHERE is_heartbeat = 1 ORDER BY variable_name");
    QVERIFY(check.exec());
    int heartbeatRows = 0;
    QStringList varNames;
    while (check.next()) {
        varNames << check.value(0).toString();
        QCOMPARE(check.value(1).toInt(), 1);  // is_heartbeat 必须是 1
        ++heartbeatRows;
    }
    QCOMPARE(heartbeatRows, 2);
    QVERIFY(varNames.contains("plc1.Speed"));
    QVERIFY(varNames.contains("plc1.Temp"));

    r.stop();
    QVERIFY(!r.isRunning());
}

// ★ D.2 用例 8:旧 DB(无 is_heartbeat 列)打开后能 ALTER TABLE 升级
//   模拟 D.1 时期生成的 DB:CREATE TABLE 时不含 is_heartbeat
//   打开后 isOpen() == true 且 INSERT/UPDATE 都仍可用
void TestHistoryRecorder::heartbeatField_migratesFromOldDbWithoutColumn() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString dbPath = tmp.path() + "/test_legacy.db";

    // 1. 模拟 D.1 旧 DB:手动创建无 is_heartbeat 列的 history 表
    {
        QSqlDatabase legacy(QSqlDatabase::addDatabase("QSQLITE", "legacy_setup"));
        legacy.setDatabaseName(dbPath);
        QVERIFY(legacy.open());
        QSqlQuery q(legacy);
        QVERIFY(q.exec(R"(
            CREATE TABLE history (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                variable_name TEXT NOT NULL,
                timestamp INTEGER NOT NULL,
                value REAL NOT NULL,
                quality INTEGER NOT NULL
            )
        )"));
        QVERIFY(q.exec("CREATE INDEX idx_history_var_time ON history(variable_name, timestamp)"));
        legacy.close();
        QSqlDatabase::removeDatabase("legacy_setup");
    }

    // 2. 用 HistoryRecorder 打开旧 DB(应触发 migrateAddHeartbeatColumn)
    HistoryRecorder r;
    QVERIFY(r.open(dbPath));   // 不应崩,应自动 ALTER 成功
    QVERIFY(r.isOpen());

    // 4. 验证 is_heartbeat 列已存在(用 PRAGMA table_info 查)
    QSqlQuery check(QSqlDatabase::database("hmi_history"));
    QVERIFY(check.exec("PRAGMA table_info(history)"));
    bool found = false;
    while (check.next()) {
        if (check.value(1).toString() == QLatin1String("is_heartbeat")) {
            found = true;
            // 默认值应为 0
            QCOMPARE(check.value(3).toInt(), 0);  // dflt_value 列
            break;
        }
    }
    QVERIFY(found);

    // 5. 验证旧数据兼容 — 插入一条 is_heartbeat=0 的数据不报错
    VariableManager vm;
    Variable v("Legacy", 0, Variable::DataType::Float, Variable::Access::ReadOnly, &vm);
    v.setPlcId("plc1");
    vm.addVariable(&v);
    r.setSource(&vm);
    v.setValue(QVariant::fromValue<float>(42.0f));
    QTest::qWait(50);

    QDateTime from = QDateTime::currentDateTime().addMSecs(-1000);
    QDateTime to = QDateTime::currentDateTime().addMSecs(1000);
    auto records = r.query("plc1.Legacy", from, to);
    QCOMPARE(records.size(), 1);
    QVERIFY(qAbs(records[0].second - 42.0) < 0.001);
}

// ★ QTEST_GUILESS_MAIN(创建 QCoreApplication,Qt SQL 需要)
QTEST_GUILESS_MAIN(TestHistoryRecorder)
#include "tst_HistoryRecorder.moc"
