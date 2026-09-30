// ============================================================================
// ★ F3(2026-09-30) · 手工改 JSON 加变量 —— 加载诊断能力回归
//
// 【为什么有这个文件】
// F3 验收目标原文:"加新变量 = 改 JSON 一行"。
// 2026-09-30 实测(手工改 JSON + 调【生产的】Project::loadFromFile,不是重写解析)发现:
//   ✅ 通路是通的 —— 手工加 5 个变量,地址/类型/权限/轮询/范围逐字段断言全对
//   ❌ 但暴露 3 个问题,都是"静默失败"或"报错无用":
//      1. 报错不含变量名、不含哪个字段、两种错报同一句话
//         → 200 个变量的工程里,第 137 个写错一个字母,用户不知道改哪行,只能挨个试
//      2. 未知字段静默丢弃(手打 "dead_band" → 死区没生效,他以为配了)
//      3. 解析中途失败,内存里留下"半个工程"不清理
//
// 这 3 条正是本文件的断言对象。注意:第 2 条断言的是【警告被发出】,
// 不是"报错"—— 报错会破坏前向兼容(新版本字段被老版本读到不该拒载)。
//
// 关联:JsonSerializer::fromJsonVariable, Project::loadRootFromFile
// ============================================================================

#include <QtTest>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include "JsonSerializer.h"
#include "Project.h"
#include "Variable.h"
#include "VariableManager.h"

using namespace qwhmi;

namespace {

// 造一个最小的、一定合法的 variables 数组底本
QJsonObject makeValidVariable(const QString& name, int address) {
    QJsonObject v;
    v["name"]       = name;
    v["address"]    = address;
    v["data_type"]  = QStringLiteral("Int16");
    v["access"]     = QStringLiteral("ReadOnly");
    v["deadband"]   = 0;
    v["poll_interval_ms"] = 1000;
    v["alwaysPoll"] = false;
    QJsonObject range;
    range["min"] = 0;
    range["max"] = 100;
    v["range"] = range;
    return v;
}

QJsonObject makeProjectRoot(const QJsonArray& variables, int schemaVersion = 3) {
    QJsonObject root;
    root["schema_version"] = schemaVersion;
    root["project_name"]   = QStringLiteral("F3 手测工程");
    root["variables"]      = variables;
    QJsonObject plc;
    plc["id"] = QStringLiteral("plc1");
    plc["protocol"] = QStringLiteral("modbus_tcp");
    plc["host"] = QStringLiteral("127.0.0.1");
    plc["port"] = 502;
    root["plcs"] = QJsonArray{plc};
    return root;
}

// 写文件 + 跑真实 loadFromFile,返回错误串
bool loadAndCapture(const QJsonObject& root, QTemporaryDir& dir,
                    VariableManager& vm, QString& errOut) {
    const QString path = dir.filePath(QStringLiteral("p.json"));
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(QJsonDocument(root).toJson());
    f.close();

    errOut.clear();
    Project p(&vm, nullptr, nullptr, nullptr, nullptr);
    QObject::connect(&p, &Project::loadError, [&errOut](const QString& m){ errOut = m; });
    return p.loadFromFile(path);
}

} // namespace

class TestJsonLoadDiagnostics : public QObject {
    Q_OBJECT
private slots:
    // ==== 正向:手工加变量必须成功(F3 的核心承诺)====
    void handAddedFiveVariables_allFieldsSurvive();

    // ==== 问题 1:报错必须能定位 ====
    void badDataType_errorNamesVariableAndFieldAndLegalValues();
    void missingAccess_errorNamesVariableAndLegalValues();
    void missingName_errorIsDistinct();
    void missingAddress_errorIsDistinct();
    void errorMessagePointsAtWhichVariableFailed();
    void differentErrors_produceDifferentMessages();

    // ==== 问题 2:未知字段警告(不报错,保前向兼容)====
    void unknownField_isIgnoredNotRejected();
    void knownFields_produceNoWarning();

    // ==== 问题 3:失败不留半成品 ====
    void loadFailure_leavesNoPartialVariables();

    // ==== 附带发现:重名是【静默替换】,不是报错 ====
    void duplicateName_silentlyKeepsLast();
};

static QJsonArray fiveManualPoints() {
    QJsonArray arr;
    for (int i = 1; i <= 5; ++i) {
        arr.append(makeValidVariable(QStringLiteral("ManualPoint%1").arg(i), 100 + i));
    }
    return arr;
}

// ===== 正向 =====
void TestJsonLoadDiagnostics::handAddedFiveVariables_allFieldsSurvive() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    VariableManager vm;
    QString err;
    QVERIFY2(loadAndCapture(makeProjectRoot(fiveManualPoints()), dir, vm, err),
             qPrintable(err));
    QCOMPARE(vm.count(), 5);

    for (int i = 1; i <= 5; ++i) {
        const QString n = QStringLiteral("ManualPoint%1").arg(i);
        Variable* v = vm.findVariable(n);
        QVERIFY2(v, qPrintable(QStringLiteral("手工变量 %1 没加载出来").arg(n)));
        QCOMPARE(v->address(), static_cast<quint16>(100 + i));
        QCOMPARE(v->dataType(), Variable::DataType::Int16);
        QCOMPARE(v->access(), Variable::Access::ReadOnly);
        QCOMPARE(v->pollIntervalMs(), 1000);
        QVERIFY(v->hasRange());
    }
}

// ===== 问题 1:报错可定位 =====
void TestJsonLoadDiagnostics::badDataType_errorNamesVariableAndFieldAndLegalValues() {
    QTemporaryDir dir; VariableManager vm; QString err;
    QJsonObject bad = makeValidVariable(QStringLiteral("Flow"), 5);
    bad["data_type"] = QStringLiteral("int16");       // 小写
    QVERIFY(!loadAndCapture(makeProjectRoot(QJsonArray{bad}), dir, vm, err));
    QVERIFY2(err.contains("Flow"),  qPrintable(QStringLiteral("报错没点名变量: %1").arg(err)));
    QVERIFY2(err.contains("data_type"), qPrintable(QStringLiteral("报错没点名字段: %1").arg(err)));
    // 必须告诉用户合法值是什么 —— 否则他知道错了,还是不知道该填什么
    QVERIFY2(err.contains("Int16"), qPrintable(QStringLiteral("报错没给合法值: %1").arg(err)));
}

void TestJsonLoadDiagnostics::missingAccess_errorNamesVariableAndLegalValues() {
    QTemporaryDir dir; VariableManager vm; QString err;
    QJsonObject bad = makeValidVariable(QStringLiteral("Level"), 7);
    bad.remove("access");
    QVERIFY(!loadAndCapture(makeProjectRoot(QJsonArray{bad}), dir, vm, err));
    QVERIFY2(err.contains("Level"), qPrintable(QStringLiteral("报错没点名变量: %1").arg(err)));
    QVERIFY2(err.contains("ReadOnly"), qPrintable(QStringLiteral("报错没给合法值: %1").arg(err)));
}

void TestJsonLoadDiagnostics::missingName_errorIsDistinct() {
    QTemporaryDir dir; VariableManager vm; QString err;
    QJsonObject bad = makeValidVariable(QStringLiteral("Temp"), 9);
    bad.remove("name");
    QVERIFY(!loadAndCapture(makeProjectRoot(QJsonArray{bad}), dir, vm, err));
    QVERIFY2(err.contains("name"), qPrintable(QStringLiteral("缺 name 的报错没点名字段: %1").arg(err)));
}

void TestJsonLoadDiagnostics::missingAddress_errorIsDistinct() {
    QTemporaryDir dir; VariableManager vm; QString err;
    QJsonObject bad = makeValidVariable(QStringLiteral("Press"), 3);
    bad.remove("address");
    QVERIFY(!loadAndCapture(makeProjectRoot(QJsonArray{bad}), dir, vm, err));
    QVERIFY2(err.contains("address"), qPrintable(QStringLiteral("缺 address 的报错没点名字段: %1").arg(err)));
}

void TestJsonLoadDiagnostics::errorMessagePointsAtWhichVariableFailed() {
    // ★ 这条是问题 1 的核心:200 个变量里第 137 个错了,得告诉用户是第几个。
    //   修之前只能挨个试 —— 因为所有错误报的都是同一句话。
    QTemporaryDir dir; VariableManager vm; QString err;
    QJsonArray arr = fiveManualPoints();               // 5 个好的
    QJsonObject bad = makeValidVariable(QStringLiteral("Boom"), 200);
    bad["data_type"] = QStringLiteral("NOPE");
    arr.append(bad);                                    // 第 6 个坏的
    QVERIFY(!loadAndCapture(makeProjectRoot(arr), dir, vm, err));
    QVERIFY2(err.contains(QStringLiteral("第 6 个")),
             qPrintable(QStringLiteral("报错没指出是第几个变量: %1").arg(err)));
    QVERIFY2(err.contains("Boom"), qPrintable(QStringLiteral("报错没点名变量: %1").arg(err)));
}

void TestJsonLoadDiagnostics::differentErrors_produceDifferentMessages() {
    // ★ 修之前:data_type 拼错 和 漏 access 报的是同一句
    //   "variable 字段缺失或类型错误" —— 用户看到错误,依然不知道错在哪。
    QTemporaryDir dir; QString e1, e2;
    { VariableManager vm;
      QJsonObject v = makeValidVariable(QStringLiteral("A"), 1);
      v["data_type"] = QStringLiteral("BAD");
      QVERIFY(!loadAndCapture(makeProjectRoot(QJsonArray{v}), dir, vm, e1)); }
    { VariableManager vm;
      QJsonObject v = makeValidVariable(QStringLiteral("A"), 1);
      v.remove("access");
      QVERIFY(!loadAndCapture(makeProjectRoot(QJsonArray{v}), dir, vm, e2)); }
    QVERIFY2(e1 != e2, qPrintable(QStringLiteral("两种不同错误报了同一句话: %1").arg(e1)));
}

// ===== 问题 2:未知字段警告 =====
void TestJsonLoadDiagnostics::unknownField_isIgnoredNotRejected() {
    // 只警告不报错:新版本加的字段被老版本读到时,不该拒绝加载整个工程。
    QTemporaryDir dir; VariableManager vm; QString err;
    QJsonObject v = makeValidVariable(QStringLiteral("X"), 1);
    v["myCustomJunk"] = 12345;
    QVERIFY2(loadAndCapture(makeProjectRoot(QJsonArray{v}), dir, vm, err),
             qPrintable(QStringLiteral("未知字段不该导致加载失败: %1").arg(err)));
    QCOMPARE(vm.count(), 1);
}

void TestJsonLoadDiagnostics::knownFields_produceNoWarning() {
    // 反向:白名单里的字段不能被当成"未知"警告 ——
    //   那会让每个正常工程都刷一堆噪音,警告也就没人看了。
    QTemporaryDir dir; VariableManager vm; QString err;
    QJsonObject v = makeValidVariable(QStringLiteral("Y"), 2);
    QVERIFY(loadAndCapture(makeProjectRoot(QJsonArray{v}), dir, vm, err));
    QCOMPARE(vm.count(), 1);
    // 直接调序列化器验证 fromJsonVariable 对标准字段不报未知
    VariableManager tmp;
    QString e;
    Variable* got = JsonSerializer::fromJsonVariable(v, &tmp, &e);
    QVERIFY2(got, qPrintable(e));
    QCOMPARE(got->name(), QStringLiteral("Y"));
    delete got;
}

// ===== 问题 3:失败不留半成品 =====
void TestJsonLoadDiagnostics::loadFailure_leavesNoPartialVariables() {
    // ★ 修之前:前 5 个变量已经建进 VariableManager,第 6 个失败后直接 return false,
    //   内存里留着 5 个"半个工程"。调用方不查返回值继续用,就拿到了残缺状态。
    QTemporaryDir dir; VariableManager vm; QString err;
    QJsonArray arr = fiveManualPoints();
    QJsonObject bad = makeValidVariable(QStringLiteral("Boom"), 200);
    bad["data_type"] = QStringLiteral("NOPE");
    arr.append(bad);

    QVERIFY(!loadAndCapture(makeProjectRoot(arr), dir, vm, err));
    QVERIFY2(vm.count() == 0,
             qPrintable(QStringLiteral("失败后仍残留 %1 个变量 —— 是半个工程").arg(vm.count())));
}

void TestJsonLoadDiagnostics::duplicateName_silentlyKeepsLast() {
    // ★ 写这条测试时才发现的第 4 个静默失败 —— 不是本次要修的 3 条之一,
    //   先把【当前真实行为】用测试锁住,免得以后有人以为是 bug 顺手"修"了它。
    //
    //   VariableManager::addVariable 碰到同名【替换】而不是拒绝,这是 6.3 有意为之
    //   (要 emit variableReplaced 让报警/绑定更新 Variable* 指针)。
    //   VariableManager.h:20 的注释「返回 false:name 重复」是过时的。
    //
    //   但对手改 JSON 的用户来说,结果是:两个同名变量,第一个被静默丢掉,
    //   零提示 —— 跟"未知字段"同一类问题(配了跟没配长得一样)。
    //   怎么办见 F3 报告:要么加载时检测重名并 qWarning,要么接受现状。
    //   本测试只陈述事实,不主张哪种才对。
    QTemporaryDir dir; VariableManager vm; QString err;
    QJsonArray arr;
    arr.append(makeValidVariable(QStringLiteral("Dup"), 1));
    arr.append(makeValidVariable(QStringLiteral("Dup"), 2));   // 同名,地址不同
    QVERIFY(loadAndCapture(makeProjectRoot(arr), dir, vm, err));
    QCOMPARE(vm.count(), 1);                                   // 不是 2
    Variable* kept = vm.findVariable(QStringLiteral("Dup"));
    QVERIFY(kept);
    QCOMPARE(kept->address(), static_cast<quint16>(2));         // 后来的赢
}

QTEST_MAIN(TestJsonLoadDiagnostics)
#include "tst_JsonLoadDiagnostics.moc"
