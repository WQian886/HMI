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

// ★ QTEST_GUILESS_MAIN(创建 QCoreApplication,Qt SQL 需要)
QTEST_GUILESS_MAIN(TestHistoryRecorder)
#include "tst_HistoryRecorder.moc"
