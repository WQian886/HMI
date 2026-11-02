// ============================================================================
// G-24(2026-10-30) · 表达式解析安全闸 —— 回归测试
//
//  【这条缺陷是什么】
//    体检 P1-6:表达式解析器是递归下降的,evalNode / collectVars / freeNode
//    也全是递归的,全文【没有深度计数、没有长度上限】。
//    ⇒ 一行 5000 层括号的文本 = 5000 层栈帧 ⇒ 栈溢出 ⇒ 进程直接死。
//    而表达式是可以由【人】给的:CSV 导入的 expression 列、变量配置窗口的
//    表达式框、手改的工程文件。
//
//  【还有第二条路径,体检时差点漏掉】
//    parseAddSub / parseTerm 是【循环】,所以 "1+1+1+…" 这种表达式解析期
//    不长栈 —— 但它拼出来的是一棵【左倾二叉树】,深度 = 运算符个数。
//    于是 evalNode(求值)和 freeNode(释放)照样递归爆栈。
//    ⇒ 光加"括号深度闸"堵不住这条路,必须同时管住【AST 节点数】。
//
//  【这几条用例分别钉住哪一段】
//    1. nestingJustUnderLimit_stillWorks
//       闸不能误伤合法表达式(30 层嵌套必须还能算)。
//    2. tooDeepNesting_isRejected
//       100 层嵌套必须【被拒】,而不是崩。
//    3. absurdlyLongExpression_isRejected
//       6000 字符必须被长度闸拒掉。
//    3-b. absurdlyDeepNesting_doesNotCrash
//       ★★ 原崩溃场景本身:5000 层括号。
//         修复前是【进程栈溢出直接死】,现在必须返回一个错误。
//    4. longChain_isRejectedByNodeCount
//       ★ "1+1+…" 这种左倾树 —— 字符数没超,是节点数闸在管。
//    5. longChainJustUnderLimit_stillWorks
//       100 项的链必须还能算(证明闸的位置对,不是一刀切)。
//    6. freeNode_survivesVeryDeepChain
//       ★ 直接手工搭一条 10 万层的链,调 freeNode ——
//         递归版在这里必爆栈,迭代版没事。这条把"释放期"单独钉住。
//    7. divisionByExactZero_isRejected
//    8. divisionByTinyNonZero_isNotZeroDivision
//       ★ 除数 1e-13 不是零。以前 qFuzzyIsNull(阈值 1e-12)把它判成
//         "division by zero" —— 报错信息指错了地方。
//    9. overflowResult_isRejected
//       ★ 1e308*10 得 Inf。以前【没人拦】,Inf 直接进变量、还能继续传播。
//    10. nanResult_isRejectedWithError
//       ★ Inf-Inf 得 NaN。以前 err 是空的 ⇒ 上游 VariableManager 看到 NaN
//         就 continue ⇒ 【连一句 warning 都不打】。
//    11. finiteResult_reportsNoError
//       正常表达式不许被新闸误伤(错误信息必须为空)。
//    12. collectDependencies_tooLong_fails
//    13. addDerivedVariable_withDeepExpression_fails
//       ★ 端到端:从"建派生量"这条路进去也必须被拒,而不是崩。
//
//  【负向对照】
//    把 ExpressionParser.cpp 里的三道闸逐个去掉,这几条必须变红 ——
//    其中 2/4/6 号去掉闸之后是【进程直接崩】(栈溢出),
//    不是"断言失败",是整个 ctest 进程死掉。
//
//  关联:src/tag/ExpressionParser.h / .cpp、VariableManager.h
// ============================================================================

#include <QtTest>

#include "tag/ExpressionParser.h"
#include "VariableManager.h"
#include "Variable.h"

using namespace qwhmi::tag;

class TestExpressionParserLimits : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();

    // —— 闸的位置 ——
    void nestingJustUnderLimit_stillWorks();
    void longChainJustUnderLimit_stillWorks();
    void finiteResult_reportsNoError();

    // —— 三道闸 ——
    void tooDeepNesting_isRejected();
    void absurdlyLongExpression_isRejected();
    void absurdlyDeepNesting_doesNotCrash();
    void longChain_isRejectedByNodeCount();
    void collectDependencies_tooLong_fails();

    // —— 释放期 ——
    void freeNode_survivesVeryDeepChain();

    // —— 求值 ——
    void divisionByExactZero_isRejected();
    void divisionByTinyNonZero_isNotZeroDivision();
    void overflowResult_isRejected();
    void nanResult_isRejectedWithError();

    // —— 端到端 ——
    void addDerivedVariable_withDeepExpression_fails();
};

void TestExpressionParserLimits::initTestCase() {
    qInfo() << "[tst_ExpressionParserLimits] init";
}
void TestExpressionParserLimits::cleanupTestCase() {
    qInfo() << "[tst_ExpressionParserLimits] cleanup";
}

// ---------------------------------------------------------------------------
// helper:求值并把错误信息带出来
// ---------------------------------------------------------------------------
struct EvalResult {
    double value = 0.0;
    QString error;
};

static EvalResult evalWithError(const QString& expr) {
    EvalResult r;
    r.value = ExpressionParser::evaluate(
        expr,
        [](const QString&) -> const QVariant { return QVariant(); },
        &r.error);
    return r;
}

// ★ 10^160 的字面量 —— 【不能写 "1e160"】。
//   解析器只认纯数字(没有指数写法),写 "1e160" 会被解析成
//   数字 1 + 尾巴 "e160" ⇒ "trailing chars at pos 1",
//   测的就不是溢出而是另一件事了。
//   10^160 本身是有限数,但两个一乘就溢出到 Inf —— 正好用来测溢出。
static QString bigLiteral() {
    return QString("1") + QString(160, '0');
}

// ---------------------------------------------------------------------------
// 1. 闸不能误伤:30 层嵌套必须还能算
// ---------------------------------------------------------------------------
void TestExpressionParserLimits::nestingJustUnderLimit_stillWorks() {
    const QString expr = QString(30, '(') + "1" + QString(30, ')');
    QVERIFY2(expr.size() < ExpressionParser::kMaxExprLength,
             "这条用例自己就得满足长度闸 —— 否则它测的不是深度闸");

    const EvalResult r = evalWithError(expr);
    QVERIFY2(r.error.isEmpty(),
             qPrintable(QString("30 层嵌套不该报错,却报了:%1").arg(r.error)));
    QCOMPARE(r.value, 1.0);
}

// ---------------------------------------------------------------------------
// 5. 闸不能误伤:100 项的加法链必须还能算
// ---------------------------------------------------------------------------
void TestExpressionParserLimits::longChainJustUnderLimit_stillWorks() {
    QString expr;
    for (int i = 0; i < 99; ++i) expr += "1+";
    expr += "1";
    QVERIFY(expr.size() < ExpressionParser::kMaxExprLength);

    const EvalResult r = evalWithError(expr);
    QVERIFY2(r.error.isEmpty(),
             qPrintable(QString("100 项的链不该报错,却报了:%1").arg(r.error)));
    QCOMPARE(r.value, 100.0);
}

// ---------------------------------------------------------------------------
// 11. 正常表达式不许被新闸误伤
// ---------------------------------------------------------------------------
void TestExpressionParserLimits::finiteResult_reportsNoError() {
    const EvalResult r = evalWithError("2 + 3 * 4 - 1");
    QVERIFY2(r.error.isEmpty(),
             qPrintable(QString("正常表达式不该有错误信息,却有了:%1").arg(r.error)));
    QCOMPARE(r.value, 13.0);
}

// ---------------------------------------------------------------------------
// 2. 100 层嵌套必须被深度闸拒掉(而不是崩)
// ---------------------------------------------------------------------------
void TestExpressionParserLimits::tooDeepNesting_isRejected() {
    const QString expr = QString(100, '(') + "1" + QString(100, ')');

    const EvalResult r = evalWithError(expr);
    QVERIFY2(qIsNaN(r.value),
             "被拒的表达式不许返回一个数 —— 拒了就得是 NaN");
    QVERIFY2(r.error.contains("too deep"),
             qPrintable(QString("错误信息必须点名'太深',实际是:%1").arg(r.error)));
    QVERIFY2(r.error.contains(QString::number(ExpressionParser::kMaxParseDepth)),
             qPrintable(QString("错误信息必须带上实际上限 %1,实际是:%2")
                            .arg(ExpressionParser::kMaxParseDepth)
                            .arg(r.error)));
}

// ---------------------------------------------------------------------------
// 3. 6000 字符必须被长度闸拒掉
// ---------------------------------------------------------------------------
void TestExpressionParserLimits::absurdlyLongExpression_isRejected() {
    QString expr;
    for (int i = 0; i < 3000; ++i) expr += "1+";
    expr += "1";
    QVERIFY(expr.size() > ExpressionParser::kMaxExprLength);

    const EvalResult r = evalWithError(expr);
    QVERIFY(qIsNaN(r.value));
    QVERIFY2(r.error.contains("too long"),
             qPrintable(QString("错误信息必须点名'太长',实际是:%1").arg(r.error)));
}

// ---------------------------------------------------------------------------
// 3-b. ★★ 原崩溃场景本身:5000 层括号
//
//    这一行是体检 P1-6 的原文场景:
//      表达式 = "(" * 5000 + "1" + ")" * 5000
//      ⇒ 修复前:解析期 5000 层栈帧 ⇒ 栈溢出 ⇒ 进程直接死
//        (不是异常、不是断言失败,是【整个 ctest 进程没了】)
//
//    【这条为什么值得单独写】
//      上面那条 tooDeepNesting 用的是 100 层 —— 它证明"深度闸会拒绝"。
//      这一条证明的是【原来会死的那个输入,现在会返回一个错误】。
//      两件事不一样:前者是策略,后者是【崩溃没了】。
//
//    【被哪道闸拦住的不重要】
//      5000 层括号 = 10001 字符 ⇒ 长度闸先拦。
//      但两道闸是【互相兜底】的:哪天有人把长度上限从 1024 放宽到 100000,
//      深度闸(64)就是唯一还站着的那道 —— 见负向对照 "depthcrash"。
// ---------------------------------------------------------------------------
void TestExpressionParserLimits::absurdlyDeepNesting_doesNotCrash() {
    const QString expr = QString(5000, '(') + "1" + QString(5000, ')');

    // ★ 走到这一行本身就是在验"没崩"。
    //   崩了的话,整个进程死在这儿,后面所有用例都不会跑,
    //   ctest 看到的是 "Test Failed" + 一句 "进程退出码 -1073741571"。
    const EvalResult r = evalWithError(expr);

    QVERIFY2(qIsNaN(r.value),
             "5000 层括号必须被拒 —— 拒了就得是 NaN,不许返回一个数");
    QVERIFY2(!r.error.isEmpty(),
             "被拒的表达式必须说清楚为什么 —— 静悄悄地不算是拒绝");
}

// ---------------------------------------------------------------------------
// 4. ★ "1+1+…" 左倾树:字符数没超,是节点数闸在管
//
//    300 项 ⇒ 601 字符(远小于长度闸)⇒ 只能被节点数闸拦住。
//    这条专门堵"体检时差点漏掉的那条路径"。
// ---------------------------------------------------------------------------
void TestExpressionParserLimits::longChain_isRejectedByNodeCount() {
    QString expr;
    for (int i = 0; i < 299; ++i) expr += "1+";
    expr += "1";
    QVERIFY2(expr.size() < ExpressionParser::kMaxExprLength,
             "这条用例必须【不】触发长度闸 —— 它测的是节点数闸");

    const EvalResult r = evalWithError(expr);
    QVERIFY(qIsNaN(r.value));
    QVERIFY2(r.error.contains("too complex"),
             qPrintable(QString("错误信息必须点名'太复杂',实际是:%1").arg(r.error)));
}

// ---------------------------------------------------------------------------
// 12. collectDependencies 走同一道闸
// ---------------------------------------------------------------------------
void TestExpressionParserLimits::collectDependencies_tooLong_fails() {
    QString expr;
    for (int i = 0; i < 2000; ++i) expr += "a+";
    expr += "b";

    QStringList deps;
    QString err;
    QVERIFY2(!ExpressionParser::collectDependencies(expr, &deps, &err),
             "超长的表达式不许收集依赖成功");
    QVERIFY(!err.isEmpty());
}

// ---------------------------------------------------------------------------
// 6. ★ 释放期:手工搭一条 10 万层的链,调 freeNode
//
//    Node 是公开结构,可以直接搭 —— 这条不需要解析器配合,
//    专门把"释放期递归"这件事单独钉住。
//    递归版 freeNode 在这里必爆栈(整个 ctest 进程死掉)。
// ---------------------------------------------------------------------------
void TestExpressionParserLimits::freeNode_survivesVeryDeepChain() {
    constexpr int kDepth = 100000;

    auto* head = new ExpressionParser::Node;
    head->kind = ExpressionParser::NodeKind::BinaryOp;
    head->op = '+';
    head->lhs = nullptr;
    head->rhs = nullptr;

    // ★ 链是【头尾相接】建的:每个新节点的 lhs 指向上一个。
    //   root 必须记【最后建的那个】—— 它才是这棵树的根。
    //   (踩过的坑:一开始把 root 记成了 head,于是 freeNode(head)
    //    只走到第一层就返回,后面 99999 个节点压根没被访问 ——
    //    递归版也不崩、还泄漏一整条链。这条用例当时是【假绿】的,
    //    是负向对照把它揪出来的。)
    ExpressionParser::Node* root  = head;
    ExpressionParser::Node* tail  = head;
    for (int i = 1; i < kDepth; ++i) {
        auto* n = new ExpressionParser::Node;
        n->kind = ExpressionParser::NodeKind::BinaryOp;
        n->op = '+';
        n->lhs = tail;
        n->rhs = nullptr;
        tail = n;
        root = n;
    }

    // 递归版走到这儿就是栈溢出 —— 这条用例的进程会直接死,
    // 不是断言失败。迭代版只是把 10 万个节点依次 delete 掉。
    ExpressionParser::freeNode(root);

    // 走到这儿说明没爆栈。再确认一下空指针调用也不炸。
    ExpressionParser::freeNode(nullptr);
}

// ---------------------------------------------------------------------------
// 7. 真除零必须被拒
// ---------------------------------------------------------------------------
void TestExpressionParserLimits::divisionByExactZero_isRejected() {
    const EvalResult r = evalWithError("1 / 0");
    QVERIFY(qIsNaN(r.value));
    QVERIFY2(r.error.contains("division by zero"),
             qPrintable(QString("错误信息必须说'除零',实际是:%1").arg(r.error)));
}

// ---------------------------------------------------------------------------
// 8. ★ 除数极小但非零:不是除零
//
//    以前 qFuzzyIsNull(阈值 1e-12)把 1e-13 判成零 ⇒ 报"division by zero",
//    而现场真正的除数是 1e-13 —— 报错信息指错了地方。
// ---------------------------------------------------------------------------
void TestExpressionParserLimits::divisionByTinyNonZero_isNotZeroDivision() {
    const EvalResult r = evalWithError("1 / 0.0000000000001");
    QVERIFY2(!r.error.contains("division by zero"),
             qPrintable(QString("1e-13 不是零,不该报除零,却报了:%1").arg(r.error)));
    QVERIFY2(r.error.isEmpty(),
             qPrintable(QString("这条表达式不该有错误信息,却有了:%1").arg(r.error)));
    QVERIFY2(qFuzzyCompare(r.value, 1e13),
             qPrintable(QString("1/1e-13 应约等于 1e13,实际是:%1").arg(r.value)));
}

// ---------------------------------------------------------------------------
// 9. ★ 溢出得 Inf:必须被拒
//
//    以前 Inf【没人拦】,直接进变量,还能继续传播给别的派生量。
// ---------------------------------------------------------------------------
void TestExpressionParserLimits::overflowResult_isRejected() {
    // 10^160 * 10^160 = 10^320 ⇒ 溢出成 Inf
    const QString expr = bigLiteral() + " * " + bigLiteral();
    QVERIFY2(expr.size() < ExpressionParser::kMaxExprLength,
             "这条用例自己就得满足长度闸 —— 否则它测的不是溢出");

    const EvalResult r = evalWithError(expr);
    QVERIFY2(qIsNaN(r.value),
             "溢出的结果不许返回 Inf —— 必须变成 NaN 并报错");
    QVERIFY2(r.error.contains("finite"),
             qPrintable(QString("错误信息必须说'非有限数',实际是:%1").arg(r.error)));
}

// ---------------------------------------------------------------------------
// 10. ★ Inf-Inf 得 NaN:必须【带上错误信息】
//
//    以前 err 是空的 ⇒ 上游 VariableManager 看到 NaN 就 continue,
//    连一句 warning 都不打 ⇒ 派生量静悄悄地停住。
// ---------------------------------------------------------------------------
void TestExpressionParserLimits::nanResult_isRejectedWithError() {
    // (10^160 * 10^160) - (10^160 * 10^160) ⇒ Inf - Inf ⇒ NaN
    const QString one = bigLiteral() + " * " + bigLiteral();
    const QString expr = "(" + one + ") - (" + one + ")";
    QVERIFY2(expr.size() < ExpressionParser::kMaxExprLength,
             "这条用例自己就得满足长度闸 —— 否则它测的不是 NaN");

    const EvalResult r = evalWithError(expr);
    QVERIFY(qIsNaN(r.value));
    QVERIFY2(!r.error.isEmpty(),
             "NaN 结果必须带错误信息 —— 空的错误信息会让上游【静悄悄地】跳过,"
             "派生量停在那儿不动,现场只看到数字不更新");
    QVERIFY2(r.error.contains("finite"),
             qPrintable(QString("错误信息必须说'非有限数',实际是:%1").arg(r.error)));
}

// ---------------------------------------------------------------------------
// 13. ★ 端到端:从"建派生量"这条路进去也必须被拒
// ---------------------------------------------------------------------------
void TestExpressionParserLimits::addDerivedVariable_withDeepExpression_fails() {
    VariableManager mgr;
    auto* a = new Variable("a", 0, Variable::DataType::Float,
                            Variable::Access::ReadOnly, &mgr);
    a->setValue(QVariant::fromValue<float>(1.0f));
    QVERIFY(mgr.addVariable(a));

    const QString deep = QString(100, '(') + "a" + QString(100, ')');
    QVERIFY2(!mgr.addDerivedVariable("bad", deep),
             "超深的表达式不许建成派生量 —— 建成了它就是一个随时会崩的雷");

    // ★ 派生量【压根没被建出来】—— 这才是"拒绝了"的正确形态。
    QVERIFY2(mgr.findVariable("bad") == nullptr,
             "被拒的表达式不许留下一个变量 —— "
             "留一个没表达式的派生量,重载工程后它就会变成 plc1 的 0 号寄存器"
             "(这正是 G-23 修的那类事故)");
}

QTEST_GUILESS_MAIN(TestExpressionParserLimits)
#include "tst_ExpressionParserLimits.moc"
