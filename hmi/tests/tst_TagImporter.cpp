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
    // ★ 2b 阶段 6:变量指向不存在的 PLC 时【警告但不拒绝】
    void importFromCsv_unknownPlcId_warnsButStillImports();
    void importFromCsv_knownPlcId_isSilent();
    void importFromCsv_sameUnknownPlcId_reportedOnce();
    void importFromCsv_withoutKnownIds_doesNotCheck();
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

// ★ 用例 6(2b 阶段 6):变量指向一台【不存在】的 PLC 时,警告但不拒绝。
//
//   【为什么必须警告】变量 plc_id 至今【没有 UI 入口】—— 整个变量编辑界面
//   都不存在,用户只能靠 CSV 或手改 JSON 写 plc_id;而 plcs[] 现在能配了。
//   两边对不上是眼下最容易犯也最难查的一类错:导入全部"成功"、界面一片绿,
//   但那些变量永远不通讯。而 plc_id 那一格在界面上根本不显示,
//   用户没有任何地方能看到"plc3 不存在",那份坏配置就永远查不到了。
//
//   【为什么不拒绝】"先导 CSV、再配 PLC"是完全合理的顺序。
//   拒绝会把这个正常流程堵死 —— 用户导了 200 个变量、plc_id 先随便写上,
//   等会儿去 PLC 配置里配上就行了。
void TestTagImporter::importFromCsv_unknownPlcId_warnsButStillImports() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString csvPath = tmp.path() + "/tags.csv";
    writeCsv(csvPath,
             "name,plcId,type,access,address\n"
             "Speed,plc1,Int16,R,100\n"
             "Press,plc9,Int16,R,200\n");   // plc9 不存在

    VariableManager mgr;
    const auto r = TagImporter::importFromCsv(csvPath, &mgr,
                                              QStringList{"plc1", "plc2"});

    // ★ 变量【照常导入】—— 拒绝会堵死"先导 CSV 再配 PLC"
    QCOMPARE(r.imported, 2);
    QVERIFY(mgr.contains("Speed"));
    QVERIFY(mgr.contains("Press"));
    QCOMPARE(mgr.findVariable("Press")->plcId(), QStringLiteral("plc9"));

    // 但必须说出来,而且要说清【是哪一行、哪个变量、哪个 plcId】
    QString joined = r.errors.join('\n');
    QVERIFY2(joined.contains(QStringLiteral("plc9")), qPrintable(joined));
    QVERIFY2(joined.contains(QStringLiteral("Press")), qPrintable(joined));
}

// ★ 与上面成对:指向【存在】的 PLC 不该有任何噪音。
void TestTagImporter::importFromCsv_knownPlcId_isSilent() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString csvPath = tmp.path() + "/tags.csv";
    writeCsv(csvPath,
             "name,plcId,type,access,address\n"
             "Speed,plc1,Int16,R,100\n"
             "Press,plc2,Int16,R,200\n");

    VariableManager mgr;
    const auto r = TagImporter::importFromCsv(csvPath, &mgr,
                                              QStringList{"plc1", "plc2"});
    QCOMPARE(r.imported, 2);
    QVERIFY2(r.errors.isEmpty(),
             qPrintable(QStringLiteral("全都指向已存在的 PLC,不该有错误:"
                                       "%1").arg(r.errors.join('\n'))));
}

// ★ 同一个不存在的 plcId 被几十个变量共用时【只说一次】。
//   否则 200 个变量全指向 plc9,错误列表会被同一条刷爆,
//   真正需要看的那几条反而被挤掉。
void TestTagImporter::importFromCsv_sameUnknownPlcId_reportedOnce() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString csvPath = tmp.path() + "/tags.csv";
    writeCsv(csvPath,
             "name,plcId,type,access,address\n"
             "A,plc9,Int16,R,100\n"
             "B,plc9,Int16,R,101\n"
             "C,plc9,Int16,R,102\n"
             "D,plc8,Int16,R,103\n");

    VariableManager mgr;
    const auto r = TagImporter::importFromCsv(csvPath, &mgr,
                                              QStringList{"plc1"});
    QCOMPARE(r.imported, 4);

    int plc9Count = 0, plc8Count = 0;
    for (const QString& e : r.errors) {
        if (e.contains(QStringLiteral("plc9"))) ++plc9Count;
        if (e.contains(QStringLiteral("plc8"))) ++plc8Count;
    }
    QCOMPARE(plc9Count, 1);   // 三个变量指向 plc9,只报一次
    QCOMPARE(plc8Count, 1);   // plc8 也要报
}

// ★ 不传 knownPlcIds(默认)时【不做任何检查】——
//   那是"调用方不知道有哪些 PLC"的场合,不该凭空报一堆错。
void TestTagImporter::importFromCsv_withoutKnownIds_doesNotCheck() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString csvPath = tmp.path() + "/tags.csv";
    writeCsv(csvPath,
             "name,plcId,type,access,address\n"
             "Speed,plc9,Int16,R,100\n");

    VariableManager mgr;
    const auto r = TagImporter::importFromCsv(csvPath, &mgr);   // 不传第三个参数
    QCOMPARE(r.imported, 1);
    QVERIFY2(r.errors.isEmpty(),
             qPrintable(QStringLiteral("没传 knownPlcIds 就不该检查:%1")
                 .arg(r.errors.join('\n'))));
}

QTEST_GUILESS_MAIN(TestTagImporter)
#include "tst_TagImporter.moc"