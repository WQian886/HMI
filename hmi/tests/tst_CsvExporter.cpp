// ============================================================================
// 阶段 D · D.4 · CsvExporter 单测
//
// 验证 5 个核心行为:
//   1. escapeCsvField 转义逗号 + 引号 + 换行
//   2. writeVariablesCsv 写正确表头 + 行数 + UTF-8 BOM
//   3. writeAlarmsCsv 写正确表头 + action/level 字符串映射
//   4. 无数据时只写表头
//   5. 路径不存在时自动创建父目录
//
// 关联:CsvExporter.h, HistoryRecorder.h, AlarmRecorder.h
// ============================================================================

#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTextStream>
#include "export/CsvExporter.h"
// ★ include 完整定义(不是 forward decl) — 需要 hr->query() 完整类型
#include "history/HistoryRecorder.h"
#include "history/AlarmRecorder.h"
#include "Variable.h"
#include "VariableManager.h"
#include "Alarm.h"
#include "AlarmManager.h"

using namespace qwhmi::export_;

class TestCsvExporter : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();

    void escapeCsvField_handlesCommasAndQuotes();
    void writeVariablesCsv_writesHeaderAndRows();
    void writeAlarmsCsv_writesActionNames();
    void writeVariablesCsv_emptyHistoryWritesOnlyHeader();
    void writeVariablesCsv_createsParentDirectory();

private:
    // ★ readAll helper
    static QString readAllUtf8(const QString& path) {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) return {};
        return QString::fromUtf8(f.readAll());
    }
};

void TestCsvExporter::initTestCase() {
    qInfo() << "[tst_CsvExporter] init";
}

void TestCsvExporter::cleanupTestCase() {
    qInfo() << "[tst_CsvExporter] cleanup";
}

// ★ 用例 1:escapeCsvField
void TestCsvExporter::escapeCsvField_handlesCommasAndQuotes() {
    // 无特殊字符 → 原样返回
    QCOMPARE(escapeCsvField("hello"), QString("hello"));

    // 含逗号 → 包双引号
    QCOMPARE(escapeCsvField("a,b,c"), QString("\"a,b,c\""));

    // 含双引号 → 包双引号 + 内部 " → ""
    QCOMPARE(escapeCsvField("say \"hi\""), QString("\"say \"\"hi\"\"\""));

    // 含换行 → 包双引号
    QCOMPARE(escapeCsvField("line1\nline2"), QString("\"line1\nline2\""));

    // 含中文 → 原样(UTF-8 编码后处理)
    QCOMPARE(escapeCsvField("温度高"), QString("温度高"));
}

// ★ 用例 2:writeVariablesCsv
void TestCsvExporter::writeVariablesCsv_writesHeaderAndRows() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString dbPath = tmp.path() + "/test_history.db";
    const QString csvPath = tmp.path() + "/out.csv";

    qwhmi::history::HistoryRecorder hr;
    QVERIFY(hr.open(dbPath));
    hr.setSampleInterval(0);
    hr.setChangeThreshold(0.0);

    VariableManager vm;
    Variable v("Speed", 0, Variable::DataType::Float, Variable::Access::ReadOnly, &vm);
    v.setPlcId("plc1");
    vm.addVariable(&v);
    hr.setSource(&vm);

    // 写 3 个值
    v.setValue(QVariant::fromValue<float>(1.0f));  QTest::qWait(50);
    v.setValue(QVariant::fromValue<float>(2.5f));  QTest::qWait(50);
    v.setValue(QVariant::fromValue<float>(3.14f)); QTest::qWait(50);

    // 导出
    QString err;
    const bool ok = writeVariablesCsv(&hr, "plc1.Speed",
                                       QDateTime::currentDateTime().addSecs(-3600),
                                       QDateTime::currentDateTime().addSecs(60),
                                       csvPath, &err);
    QVERIFY2(ok, qPrintable(err));

    const QString csv = readAllUtf8(csvPath);

    // ★ UTF-8 BOM 检查(QTextStream + setGenerateByteOrderMark 不一定生效,
    //   改读 raw bytes 检查 0xEF 0xBB 0xBF 前缀)
    QFile raw(csvPath);
    QVERIFY(raw.open(QIODevice::ReadOnly));
    const QByteArray head = raw.read(3);
    QCOMPARE(head.size(), 3);
    QCOMPARE(static_cast<quint8>(head[0]), quint8(0xEF));
    QCOMPARE(static_cast<quint8>(head[1]), quint8(0xBB));
    QCOMPARE(static_cast<quint8>(head[2]), quint8(0xBF));

    // ★ 表头
    QVERIFY(csv.contains("variable_name,timestamp,value,quality,is_heartbeat"));

    // ★ 3 行数据(按行计数)
    const auto lines = csv.split("\r\n");
    // lines[0] = header (with BOM), lines[1..3] = 3 data, lines[4] = empty
    QCOMPARE(lines.size(), 5);

    // ★ 数据含 plc1.Speed
    for (int i = 1; i <= 3; ++i) {
        QVERIFY(lines[i].startsWith("plc1.Speed,"));
    }
}

// ★ 用例 3:writeAlarmsCsv
void TestCsvExporter::writeAlarmsCsv_writesActionNames() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString dbPath = tmp.path() + "/test_alarm.csv.db";
    const QString csvPath = tmp.path() + "/alarms.csv";

    qwhmi::history::AlarmRecorder ar;
    QVERIFY(ar.open(dbPath));

    // 直接 SQL INSERT 几条,避免 AlarmManager 权限检查麻烦
    QFile db(dbPath);
    // 直接用 insertAction 不可访问,改用 QSqlDatabase
    {
        QSqlDatabase d(QSqlDatabase::addDatabase("QSQLITE", "test_insert"));
        d.setDatabaseName(dbPath);
        QVERIFY(d.open());
        QSqlQuery q(d);
        q.prepare(R"(
            INSERT INTO alarm_history
                (alarm_id, name, level, action, action_at, variable_name, trigger_value, threshold, user)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
        )");
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        q.addBindValue(101LL); q.addBindValue("温度高"); q.addBindValue(2);
        q.addBindValue(0); q.addBindValue(nowMs);
        q.addBindValue("plc1.Temp"); q.addBindValue("85.5"); q.addBindValue(">=80.0");
        q.addBindValue("operator1"); QVERIFY(q.exec());

        q.addBindValue(101LL); q.addBindValue("温度高"); q.addBindValue(2);
        q.addBindValue(1); q.addBindValue(nowMs + 1000);
        q.addBindValue("plc1.Temp"); q.addBindValue("85.5"); q.addBindValue(">=80.0");
        q.addBindValue("operator1"); QVERIFY(q.exec());
        d.close();
        QSqlDatabase::removeDatabase("test_insert");
    }

    QString err;
    const bool ok = writeAlarmsCsv(&ar,
                                   QDateTime::currentDateTime().addSecs(-3600),
                                   QDateTime::currentDateTime().addSecs(60),
                                   /*minLevel=*/0, csvPath, &err);
    QVERIFY2(ok, qPrintable(err));

    const QString csv = readAllUtf8(csvPath);

    QVERIFY(csv.contains("alarm_id,name,level,action,action_at"));
    QVERIFY(csv.contains("温度高"));              // 中文名
    QVERIFY(csv.contains("plc1.Temp"));
    QVERIFY(csv.contains("Critical"));             // level=2 → "Critical"
    QVERIFY(csv.contains("triggered"));            // action=0
    QVERIFY(csv.contains("acknowledged"));         // action=1
}

// ★ 用例 4:空数据只写表头
void TestCsvExporter::writeVariablesCsv_emptyHistoryWritesOnlyHeader() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString dbPath = tmp.path() + "/empty.db";
    const QString csvPath = tmp.path() + "/empty.csv";

    qwhmi::history::HistoryRecorder hr;
    QVERIFY(hr.open(dbPath));
    // 不写任何数据

    QString err;
    QVERIFY(writeVariablesCsv(&hr, "no.such.variable",
                              QDateTime::currentDateTime().addSecs(-3600),
                              QDateTime::currentDateTime().addSecs(60),
                              csvPath, &err));

    const QString csv = readAllUtf8(csvPath);
    QVERIFY(csv.contains("variable_name,timestamp,value,quality,is_heartbeat"));

    const auto lines = csv.split("\r\n");
    QCOMPARE(lines.size(), 2);   // header + empty line
}

// ★ 用例 5:父目录自动创建
void TestCsvExporter::writeVariablesCsv_createsParentDirectory() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString dbPath = tmp.path() + "/h.db";
    const QString csvPath = tmp.path() + "/subdir/that/does/not/exist/out.csv";

    qwhmi::history::HistoryRecorder hr;
    QVERIFY(hr.open(dbPath));

    QString err;
    QVERIFY(writeVariablesCsv(&hr, "x", QDateTime::currentDateTime(),
                              QDateTime::currentDateTime().addSecs(60), csvPath, &err));
    QVERIFY(QFile::exists(csvPath));
}

// ★ QTEST_GUILESS_MAIN(创建 QCoreApplication,Qt SQL + QFile 需要)
QTEST_GUILESS_MAIN(TestCsvExporter)
#include "tst_CsvExporter.moc"