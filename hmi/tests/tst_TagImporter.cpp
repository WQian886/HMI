// ============================================================================
// 阶段 D · D.7 · TagImporter 单测(CSV-based)
//
// 验证:
//   1. importFromCsv_basicImportsVariables
//   2. importFromCsv_withAlias_andPlcId
//   3. importFromCsv_withDerivedVariables
//   4. importFromCsv_invalidTypeSkipped
//   5. importFromCsv_emptyPathErrors
//
// 关联:TagImporter.h, VariableManager.h
// ============================================================================

#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include "tag/TagImporter.h"
#include "VariableManager.h"
#include "Variable.h"

using namespace qwhmi::tag;

// 写 CSV 文件 helper
static void writeCsv(const QString& path, const QString& csv) {
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    f.write(csv.toUtf8());
}

class TestTagImporter : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();

    void importFromCsv_basicImportsVariables();
    void importFromCsv_withAlias_andPlcId();
    void importFromCsv_withDerivedVariables();
    void importFromCsv_invalidTypeSkipped();
    void importFromCsv_emptyPathErrors();
};

void TestTagImporter::initTestCase() { qInfo() << "[tst_TagImporter] init"; }
void TestTagImporter::cleanupTestCase() { qInfo() << "[tst_TagImporter] cleanup"; }

// ★ 用例 1:基本导入(尾部省略列也要 OK)
void TestTagImporter::importFromCsv_basicImportsVariables() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString csvPath = tmp.path() + "/tags.csv";
    writeCsv(csvPath,
        "name,plcId,type,access,address,alias,expression\n"
        "Speed,plc1,Float,R,100,,\n"
        "Temp,plc1,Float,RW,101,,\n"
        "Counter,plc1,UInt16,R,200\n");   // ← 末尾省略 expression 列(应容错)

    VariableManager mgr;
    const TagImporter::Result r = TagImporter::importFromCsv(csvPath, &mgr);
    QCOMPARE(r.imported, 3);
    QCOMPARE(r.skipped, 0);
    // ★ 第 3 行触发 warning(被忽略),不是 error → 总 errors 不该有 import 失败的行
    // 允许 warning 计入 errors,这里我们只关心 imported/skipped
    QCOMPARE(r.total, 3);

    QVERIFY(mgr.contains("Speed"));
    QVERIFY(mgr.contains("Temp"));
    QVERIFY(mgr.contains("Counter"));
    QCOMPARE(mgr.findVariable("Speed")->address(), quint16(100));
    QCOMPARE(mgr.findVariable("Temp")->access(), Variable::Access::ReadWrite);
}

// ★ 用例 2:别名 + plcId
void TestTagImporter::importFromCsv_withAlias_andPlcId() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString csvPath = tmp.path() + "/tags.csv";
    writeCsv(csvPath,
        "name,plcId,type,access,address,alias,expression\n"
        "Speed,plc1,Float,R,100,转速|RPM,\n");

    VariableManager mgr;
    const auto r = TagImporter::importFromCsv(csvPath, &mgr);
    QCOMPARE(r.errors.size(), 0);
    QCOMPARE(r.imported, 1);
    auto* v = mgr.findVariable("Speed");
    QVERIFY(v != nullptr);
    QCOMPARE(v->plcId(), QString("plc1"));
    QCOMPARE(v->aliases().size(), 2);
    QVERIFY(v->aliases().contains("转速"));
    QVERIFY(v->aliases().contains("RPM"));
    QCOMPARE(mgr.findByAlias("RPM"), v);
}

// ★ 用例 3:派生变量
void TestTagImporter::importFromCsv_withDerivedVariables() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString csvPath = tmp.path() + "/tags.csv";
    writeCsv(csvPath,
        "name,plcId,type,access,address,alias,expression\n"
        "Speed,plc1,Float,R,100,,\n"
        "Temp,plc1,Float,R,101,,\n"
        "Sum,,Float,R,,,Speed + Temp\n");

    VariableManager mgr;
    const auto r = TagImporter::importFromCsv(csvPath, &mgr);
    QCOMPARE(r.errors.size(), 0);
    QCOMPARE(r.imported, 3);

    auto* sum = mgr.findVariable("Sum");
    QVERIFY(sum != nullptr);
    QVERIFY(sum->isDerived());
    QCOMPARE(sum->dependencies().size(), 2);
}

// ★ 用例 4:无效 type → 跳过
void TestTagImporter::importFromCsv_invalidTypeSkipped() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString csvPath = tmp.path() + "/tags.csv";
    writeCsv(csvPath,
        "name,plcId,type,access,address,alias,expression\n"
        "Good,plc1,Float,R,100,,\n"
        "Bad,plc1,BogusType,R,101,,\n");

    VariableManager mgr;
    const auto r = TagImporter::importFromCsv(csvPath, &mgr);
    QCOMPARE(r.imported, 1);
    QCOMPARE(r.skipped, 1);
    QVERIFY(!r.errors.isEmpty());
    QVERIFY(mgr.contains("Good"));
    QVERIFY(!mgr.contains("Bad"));
}

// ★ 用例 5:无效路径
void TestTagImporter::importFromCsv_emptyPathErrors() {
    VariableManager mgr;
    const auto r = TagImporter::importFromCsv("/nonexistent/file.csv", &mgr);
    QVERIFY(!r.errors.isEmpty());
    QCOMPARE(r.imported, 0);
}

QTEST_GUILESS_MAIN(TestTagImporter)
#include "tst_TagImporter.moc"