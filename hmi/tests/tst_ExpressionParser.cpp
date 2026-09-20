// ============================================================================
// 阶段 D · D.6 · ExpressionParser + Derived Variable 单测
//
// 验证:
//   ExpressionParser:基本 4 则运算 + 变量引用 + 嵌套括号 + 解析失败
//   VariableManager::addDerivedVariable + auto-recompute on dep change
//
// 关联:ExpressionParser.h, VariableManager.h
// ============================================================================

#include <QtTest>
#include <QSignalSpy>
#include "tag/ExpressionParser.h"
#include "VariableManager.h"
#include "Variable.h"

using namespace qwhmi::tag;

// ★ 通用 resolve helper:传 QString name → QVariant value
static QVariant resolveFromMap(const QString& name,
                                const QHash<QString, QVariant>& m) {
    return m.value(name);
}

class TestExpressionParser : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();

    void evaluate_basicArithmetic();
    void evaluate_variableReference();
    void evaluate_parentheses();
    void evaluate_unaryMinus();
    void collectDependencies_returnsVariableNames();

    // ★ D.6:VariableManager 派生
    void addDerivedVariable_initialValueComputed();
    void addDerivedVariable_recomputesWhenDepChanges();
};

void TestExpressionParser::initTestCase() { qInfo() << "[tst_ExpressionParser] init"; }
void TestExpressionParser::cleanupTestCase() { qInfo() << "[tst_ExpressionParser] cleanup"; }

// 解析 + 求值 helper
static double evalStr(const QString& expr, QHash<QString, QVariant> vars = {}) {
    QString err;
    const double v = ExpressionParser::evaluate(
        expr,
        [&vars](const QString& name) -> const QVariant {
            return vars.value(name);
        },
        &err);
    if (!err.isEmpty()) qWarning() << "eval err:" << err;
    return v;
}

// ★ 用例 1:基本四则
void TestExpressionParser::evaluate_basicArithmetic() {
    QCOMPARE(evalStr("1 + 2"), 3.0);
    QCOMPARE(evalStr("10 - 3"), 7.0);
    QCOMPARE(evalStr("4 * 5"), 20.0);
    QCOMPARE(evalStr("20 / 4"), 5.0);
    // 优先级
    QCOMPARE(evalStr("2 + 3 * 4"), 14.0);
    QCOMPARE(evalStr("(2 + 3) * 4"), 20.0);
}

// ★ 用例 2:变量引用
void TestExpressionParser::evaluate_variableReference() {
    QHash<QString, QVariant> vars;
    vars["a"] = 10.0;
    vars["b"] = 3.0;
    QCOMPARE(evalStr("a + b", vars), 13.0);
    QCOMPARE(evalStr("a * 2 + b", vars), 23.0);
    // plc1.Speed 这种含点号的变量名
    vars["plc1.Speed"] = 50.0;
    QCOMPARE(evalStr("plc1.Speed * 2", vars), 100.0);
}

// ★ 用例 3:嵌套括号
void TestExpressionParser::evaluate_parentheses() {
    QCOMPARE(evalStr("((1 + 2) * (3 + 4))"), 21.0);
    QCOMPARE(evalStr("(((1)))"), 1.0);
}

// ★ 用例 4:一元负号
void TestExpressionParser::evaluate_unaryMinus() {
    QCOMPARE(evalStr("-5 + 3"), -2.0);
    QCOMPARE(evalStr("-(2 + 3)"), -5.0);
    QCOMPARE(evalStr("--3"), 3.0);  // 双重负号 = 正
}

// ★ 用例 5:依赖收集
void TestExpressionParser::collectDependencies_returnsVariableNames() {
    QStringList deps;
    QString err;
    QVERIFY(ExpressionParser::collectDependencies("a + b * 2", &deps, &err));
    QCOMPARE(deps.size(), 2);
    QVERIFY(deps.contains("a"));
    QVERIFY(deps.contains("b"));

    QVERIFY(ExpressionParser::collectDependencies("plc1.Speed * 3 + plc2.Temp", &deps, &err));
    QCOMPARE(deps.size(), 2);
    QVERIFY(deps.contains("plc1.Speed"));
    QVERIFY(deps.contains("plc2.Temp"));
}

// ★ 用例 6:addDerivedVariable 初始值
void TestExpressionParser::addDerivedVariable_initialValueComputed() {
    VariableManager mgr;
    auto* a = new Variable("a", 0, Variable::DataType::Float, Variable::Access::ReadOnly, &mgr);
    a->setValue(QVariant::fromValue<float>(10.0f));
    auto* b = new Variable("b", 1, Variable::DataType::Float, Variable::Access::ReadOnly, &mgr);
    b->setValue(QVariant::fromValue<float>(3.0f));
    QVERIFY(mgr.addVariable(a));
    QVERIFY(mgr.addVariable(b));

    QVERIFY(mgr.addDerivedVariable("sum", "a + b"));
    auto* sum = mgr.findVariable("sum");
    QVERIFY(sum != nullptr);
    QCOMPARE(sum->value().toFloat(), 13.0f);
}

// ★ 用例 7:依赖变化时自动重算
void TestExpressionParser::addDerivedVariable_recomputesWhenDepChanges() {
    VariableManager mgr;
    auto* a = new Variable("a", 0, Variable::DataType::Float, Variable::Access::ReadOnly, &mgr);
    a->setValue(QVariant::fromValue<float>(5.0f));
    auto* b = new Variable("b", 1, Variable::DataType::Float, Variable::Access::ReadOnly, &mgr);
    b->setValue(QVariant::fromValue<float>(2.0f));
    QVERIFY(mgr.addVariable(a));
    QVERIFY(mgr.addVariable(b));
    QVERIFY(mgr.addDerivedVariable("mul", "a * b"));

    auto* mul = mgr.findVariable("mul");
    QCOMPARE(mul->value().toFloat(), 10.0f);

    // ★ a 变化后手动重算(实际生产 PollScheduler 触发)
    a->setValue(QVariant::fromValue<float>(7.0f));
    mgr.recomputeDerivedVariables();
    QCOMPARE(mul->value().toFloat(), 14.0f);   // 7 * 2
}

QTEST_GUILESS_MAIN(TestExpressionParser)
#include "tst_ExpressionParser.moc"