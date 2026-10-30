// ============================================================================
// G-23(2026-10-30)· tst_DerivedVariable —— 派生变量:存得住、算得出、别乱写
//
// 【这个缺陷的两半】
//   ① 存不住(P1-1)
//     Variable 早就有 expression_ 字段(阶段 D.6 加的),但这个字段
//     【从来没进过 JSON】。于是:
//       派生量存盘 → expression 没了 → 读回来 isDerived() == false
//       → 它变成一个 address = 0 的【普通变量】
//       → PollScheduler 会去读 plc1 的 0 号寄存器
//       → 回包把真寄存器内容盖到派生量上。
//     派生量从此"显示的是别人的值",而且【不报错、不告警】—— 静默失败。
//     ★ 而且 dependencies() 也一起没了 —— 只恢复 expression 不恢复依赖,
//       派生量照样永远是那个初值。
//
//   ② 算不出(P1-5)
//     VariableManager 的注释写着"依赖的 variable setValue 时自动触发重算",
//     但代码里【一行 connect 都没有】。于是:
//       PLC 轮询回包改了源变量 → 派生量纹丝不动
//       操作员在画面上改了源变量 → 派生量纹丝不动
//       动作联动写了源变量     → 派生量纹丝不动
//     界面上派生量显示的是【上一次算出来的旧值】——
//     跟"这个量真的就是这个数"长得一模一样。
//
// 【派生量为什么不该是寄存器】
//   派生量的 address 是 0 —— 那【是真的 plc1 的 0 号寄存器】。
//   所以"派生量被当成普通变量"这件事有两个方向:
//     读:去读 0 号寄存器,把现场数据顶掉计算结果(2026-10-19 已加过滤)
//     写:被界面改一下就 markDirty → WriteScheduler 把它发到 plc1 的 0 号寄存器
//   G-23 把写这一侧也堵上了(Variable::setValue 拒收派生量)。
//
// 【关联】
//   JsonSerializer::toJson / fromJsonVariable(expression 存取 + 依赖恢复)
//   VariableManager::addVariable / onVariableValueChanged / recomputeOne
//   Variable::setValue(派生量拒收外部写)
//   Project::loadRootFromFile(派生量依赖缺失 → 加载失败)
//   tst_PollSchedulerDerived(派生量不许进轮询 —— 那边有 driver mock)
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
#include "AlarmManager.h"
#include "ActionManager.h"
#include "PollScheduler.h"

namespace {

// ★ 一个源变量(普通变量,address 100)
Variable* makeSource(VariableManager* vm, const QString& name, float value) {
    auto* v = new Variable(name, quint16(100), Variable::DataType::Float,
                           Variable::Access::ReadOnly, vm);
    v->setValue(QVariant::fromValue<float>(value));
    vm->addVariable(v);
    return v;
}

// ★ 手写一个最小可加载的工程文件(形状照抄 tst_JsonLoadDiagnostics 的底本)。
//   为什么手写而不是 saveToFile 生成:本文件要测的正是
//   "文件里【手写着】一个 expression 会怎样" —— 而 saveToFile 生成的
//   文件里的 expression 是本程序自己写出去的,测不到"用户手写"的路径。
QJsonObject makeProjectRoot(const QJsonArray& variables) {
    QJsonObject root;
    root["schema_version"] = 3;
    root["project_name"]   = QStringLiteral("G-23 派生量工程");
    root["variables"]      = variables;
    QJsonObject plc;
    plc["id"]       = QStringLiteral("plc1");
    plc["protocol"] = QStringLiteral("modbus_tcp");
    plc["host"]     = QStringLiteral("127.0.0.1");
    plc["port"]     = 502;
    root["plcs"]    = QJsonArray{plc};
    return root;
}

// 一个普通变量 + 一个派生变量(expression 字段手写)
QJsonArray variablesWithDerived(const QString& expr) {
    QJsonObject src;
    src["name"]      = QStringLiteral("Speed");
    src["address"]   = 100;
    src["data_type"] = QStringLiteral("Float");
    src["access"]    = QStringLiteral("ReadOnly");
    src["deadband"]  = 0;
    src["poll_interval_ms"] = 1000;
    src["alwaysPoll"] = false;

    QJsonObject derived;
    derived["name"]      = QStringLiteral("Sum");
    derived["address"]   = 0;
    derived["data_type"] = QStringLiteral("Float");
    derived["access"]    = QStringLiteral("ReadOnly");
    derived["deadband"]  = 0;
    derived["poll_interval_ms"] = 1000;
    derived["alwaysPoll"] = false;
    derived["expression"] = expr;

    return QJsonArray{src, derived};
}

// 写文件 + 跑真实 loadFromFile,把错误串带出来
bool loadAndCapture(const QJsonObject& root, QTemporaryDir& dir,
                    VariableManager& vm, QString& errOut) {
    const QString path = dir.filePath(QStringLiteral("derived.json"));
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(QJsonDocument(root).toJson());
    f.close();

    errOut.clear();
    Project p(&vm, nullptr, nullptr, nullptr, nullptr);
    QObject::connect(&p, &Project::loadError, [&errOut](const QString& m) { errOut = m; });
    return p.loadFromFile(path);
}

} // namespace

class TestDerivedVariable : public QObject {
    Q_OBJECT
private slots:
    // ===== P1-1:存得住 =====
    void expression_isWrittenToJson();
    void normalVariable_alsoWritesExpressionField();
    void saveLoad_keepsDerivedFlagAndExpression();
    void saveLoad_restoresDependencies();
    void saveLoad_normalVariableStaysNormal();

    // ===== P1-1:配错了要说得出来 =====
    void missingDependency_failsTheLoadAndNamesBoth();
    void invalidExpression_failsTheLoad();

    // ===== P1-5:算得出 =====
    void dependencyChange_recomputesDerivedValue();
    void chainedDerived_recomputesThroughTheChain();
    void dependencyWithNoValueYet_doesNotComputeGarbage();
    void cyclicDerived_terminatesInsteadOfHanging();

    // ===== 派生量不许冒充寄存器 =====
    void derivedVariable_rejectsExternalWrites();
    void normalVariable_stillAcceptsWrites();
};

// ============================================================================
// P1-1:expression 存得住
// ============================================================================

void TestDerivedVariable::expression_isWrittenToJson() {
    VariableManager vm;
    makeSource(&vm, QStringLiteral("Speed"), 10.0f);
    QVERIFY(vm.addDerivedVariable(QStringLiteral("Sum"),
                                  QStringLiteral("Speed * 2"),
                                  Variable::DataType::Float));

    Variable* sum = vm.findVariable(QStringLiteral("Sum"));
    QVERIFY(sum);

    const QJsonObject obj = JsonSerializer::toJson(*sum);
    QVERIFY2(obj.contains(QStringLiteral("expression")),
             "★ toJson 必须写 expression —— 不写的话存盘后派生量就变回普通变量");
    QCOMPARE(obj.value(QStringLiteral("expression")).toString(),
             QStringLiteral("Speed * 2"));
}

void TestDerivedVariable::normalVariable_alsoWritesExpressionField() {
    // 普通变量写空串 —— 跟 plc_id 一个做法:写出来才看得见。
    //   读回来还是普通变量(空 = 不是派生量),老工程没这个键也照样读得动。
    Variable v(QStringLiteral("Speed"), quint16(100), Variable::DataType::Float,
               Variable::Access::ReadOnly);
    const QJsonObject obj = JsonSerializer::toJson(v);
    QVERIFY(obj.contains(QStringLiteral("expression")));
    QVERIFY(obj.value(QStringLiteral("expression")).toString().isEmpty());
    QVERIFY(!v.isDerived());
}

void TestDerivedVariable::saveLoad_keepsDerivedFlagAndExpression() {
    // ★★ 本文件的主用例:存盘 → 打开,派生量还是派生量。
    //   改之前(expression 从来不存):读回来 isDerived() == false,
    //   它变成一个 address = 0 的普通变量 → 去读 plc1 的 0 号寄存器。
    VariableManager vm;
    AlarmManager am;
    ActionManager acm;
    PollScheduler ps(&vm, nullptr);
    Project p(&vm, &am, &acm, &ps, nullptr);

    makeSource(&vm, QStringLiteral("Speed"), 10.0f);
    QVERIFY(vm.addDerivedVariable(QStringLiteral("Sum"),
                                  QStringLiteral("Speed * 2"),
                                  Variable::DataType::Float));

    QTemporaryDir dir;
    QVERIFY2(dir.isValid(), qPrintable(dir.errorString()));
    const QString path = dir.filePath(QStringLiteral("derived.hmi.json"));
    QVERIFY2(p.saveToFile(path), "存盘失败 —— 后面测的就不是派生量的问题了");

    // 换一个 manager 读回来(模拟"关掉工程再打开")
    VariableManager vm2;
    AlarmManager am2;
    ActionManager acm2;
    PollScheduler ps2(&vm2, nullptr);
    Project p2(&vm2, &am2, &acm2, &ps2, nullptr);

    QString err;
    QObject::connect(&p2, &Project::loadError, [&err](const QString& m) { err = m; });
    QVERIFY2(p2.loadFromFile(path), qPrintable(QStringLiteral("加载失败:%1").arg(err)));

    Variable* sum = vm2.findVariable(QStringLiteral("Sum"));
    QVERIFY2(sum, "读回来连 Sum 都没了");
    QVERIFY2(sum->isDerived(),
             "★★ 派生量存盘再打开,必须还是派生量。\n"
             "  它现在 isDerived() == false ⇒ 被当成普通变量 ⇒ "
             "轮询会去读 plc1 的 0 号寄存器,把现场数据顶掉计算结果。");
    QCOMPARE(sum->expression(), QStringLiteral("Speed * 2"));
}

void TestDerivedVariable::saveLoad_restoresDependencies() {
    // ★ dependencies 也必须恢复。
    //   只恢复 expression 不恢复 dependencies,派生量照样不会更新 ——
    //   因为自动重算靠 dependencies 知道"谁变了要重算我"。
    VariableManager vm;
    AlarmManager am;
    ActionManager acm;
    PollScheduler ps(&vm, nullptr);
    Project p(&vm, &am, &acm, &ps, nullptr);

    makeSource(&vm, QStringLiteral("Speed"), 10.0f);
    makeSource(&vm, QStringLiteral("Temp"), 5.0f);
    QVERIFY(vm.addDerivedVariable(QStringLiteral("Sum"),
                                  QStringLiteral("Speed + Temp"),
                                  Variable::DataType::Float));

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("derived.hmi.json"));
    QVERIFY(p.saveToFile(path));

    VariableManager vm2;
    AlarmManager am2;
    ActionManager acm2;
    PollScheduler ps2(&vm2, nullptr);
    Project p2(&vm2, &am2, &acm2, &ps2, nullptr);
    QVERIFY(p2.loadFromFile(path));

    Variable* sum = vm2.findVariable(QStringLiteral("Sum"));
    QVERIFY(sum);
    QVERIFY2(sum->dependencies().contains(QStringLiteral("Speed")),
             "★ dependencies 没恢复 —— 自动重算就不知道该盯谁");
    QVERIFY2(sum->dependencies().contains(QStringLiteral("Temp")),
             "★ dependencies 没恢复 —— 自动重算就不知道该盯谁");
}

void TestDerivedVariable::saveLoad_normalVariableStaysNormal() {
    // "不误伤"的那一半:普通变量存盘再打开,还是普通变量。
    //   要是这次修复把普通变量也标成派生量,那所有普通变量都会退出轮询 ——
    //   现场会变成"一个数都不刷新",而画面上没有任何提示。
    VariableManager vm;
    AlarmManager am;
    ActionManager acm;
    PollScheduler ps(&vm, nullptr);
    Project p(&vm, &am, &acm, &ps, nullptr);

    makeSource(&vm, QStringLiteral("Speed"), 10.0f);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("derived.hmi.json"));
    QVERIFY(p.saveToFile(path));

    VariableManager vm2;
    AlarmManager am2;
    ActionManager acm2;
    PollScheduler ps2(&vm2, nullptr);
    Project p2(&vm2, &am2, &acm2, &ps2, nullptr);
    QVERIFY(p2.loadFromFile(path));

    Variable* speed = vm2.findVariable(QStringLiteral("Speed"));
    QVERIFY(speed);
    QVERIFY2(!speed->isDerived(), "普通变量不许被标成派生量 —— 那会让它退出轮询");
    QVERIFY(speed->expression().isEmpty());
}

// ============================================================================
// 配错了要说得出来
// ============================================================================

void TestDerivedVariable::missingDependency_failsTheLoadAndNamesBoth() {
    // 派生量引用了一个【不存在的】变量。
    //   为什么必须让加载失败:派生量算不出来时,界面上显示的是旧值或者空 ——
    //   跟"这个量真的就是这个数"长得一模一样。报警/动作引用不存在的变量
    //   走的都是同一条路(直接失败),派生量没理由更宽松。
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    VariableManager vm;
    QString err;
    const bool ok = loadAndCapture(makeProjectRoot(variablesWithDerived(QStringLiteral("NoSuchVar * 2"))),
                                    dir, vm, err);

    QVERIFY2(!ok, "★ 派生量依赖不存在的变量,加载【必须】失败");
    QVERIFY2(err.contains(QStringLiteral("Sum")),
             qPrintable(QStringLiteral("报错里必须点名是哪个派生量,实际:%1").arg(err)));
    QVERIFY2(err.contains(QStringLiteral("NoSuchVar")),
             qPrintable(QStringLiteral("报错里必须点名缺的是哪个依赖,实际:%1").arg(err)));
}

void TestDerivedVariable::invalidExpression_failsTheLoad() {
    // 表达式本身就写错了("Speed +" 后面缺东西)—— 这时候连依赖都收集不出来。
    //   必须失败:一个算不出来的派生量,和一个值恰好是 0 的派生量,在界面上一样。
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    VariableManager vm;
    QString err;
    const bool ok = loadAndCapture(makeProjectRoot(variablesWithDerived(QStringLiteral("Speed +"))),
                                    dir, vm, err);

    QVERIFY2(!ok, "★ 表达式不合法,加载【必须】失败");
    QVERIFY2(!err.isEmpty(), "★ 失败必须给出原因 —— 光失败不给原因等于让用户猜");
}

// ============================================================================
// P1-5:算得出
// ============================================================================

void TestDerivedVariable::dependencyChange_recomputesDerivedValue() {
    // ★★ 本缺陷的核心:改源变量,派生量【自己】就变了。
    //   改之前:VariableManager 全文没有一行 connect,派生量永远不更新。
    //   注意这里【没有】调 recomputeDerivedVariables() ——
    //   调了的话这条用例就测不到"自动"了。
    VariableManager vm;
    Variable* a = makeSource(&vm, QStringLiteral("a"), 5.0f);
    makeSource(&vm, QStringLiteral("b"), 2.0f);

    QVERIFY(vm.addDerivedVariable(QStringLiteral("mul"), QStringLiteral("a * b"),
                                  Variable::DataType::Float));
    Variable* mul = vm.findVariable(QStringLiteral("mul"));
    QVERIFY(mul);
    QCOMPARE(mul->value().toFloat(), 10.0f);

    a->setValue(QVariant::fromValue<float>(7.0f));
    QCOMPARE(mul->value().toFloat(), 14.0f);   // 7 * 2

    // 再改一次,确认不是"只更新一次"
    a->setValue(QVariant::fromValue<float>(3.0f));
    QCOMPARE(mul->value().toFloat(), 6.0f);
}

void TestDerivedVariable::chainedDerived_recomputesThroughTheChain() {
    // 派生量依赖派生量:d2 = d1 * 2,d1 = a + b。
    //   这条测的是"连锁能不能走完"—— 只重算一层的话,d2 会停在旧值上。
    VariableManager vm;
    Variable* a = makeSource(&vm, QStringLiteral("a"), 1.0f);
    makeSource(&vm, QStringLiteral("b"), 2.0f);

    QVERIFY(vm.addDerivedVariable(QStringLiteral("d1"), QStringLiteral("a + b"),
                                  Variable::DataType::Float));
    QVERIFY(vm.addDerivedVariable(QStringLiteral("d2"), QStringLiteral("d1 * 2"),
                                  Variable::DataType::Float));

    Variable* d1 = vm.findVariable(QStringLiteral("d1"));
    Variable* d2 = vm.findVariable(QStringLiteral("d2"));
    QVERIFY(d1 && d2);
    QCOMPARE(d1->value().toFloat(), 3.0f);
    QCOMPARE(d2->value().toFloat(), 6.0f);

    a->setValue(QVariant::fromValue<float>(10.0f));
    QCOMPARE(d1->value().toFloat(), 12.0f);
    QCOMPARE(d2->value().toFloat(), 24.0f);   // ★ 连锁必须走到底
}

void TestDerivedVariable::dependencyWithNoValueYet_doesNotComputeGarbage() {
    // 依赖还没取到值(PLC 还没轮询到)时,派生量【不许】算出一个错的值。
    //   以前这里是"直接算 → 算出 NaN → 报一句 'variable not found'"。
    //   现在:跳过,等源变量有值了再算。
    VariableManager vm;
    auto* a = new Variable(QStringLiteral("a"), quint16(100), Variable::DataType::Float,
                           Variable::Access::ReadOnly, &vm);
    QVERIFY(vm.addVariable(a));      // ★ 注意:这个变量【还没有值】
    auto* b = new Variable(QStringLiteral("b"), quint16(101), Variable::DataType::Float,
                           Variable::Access::ReadOnly, &vm);
    QVERIFY(vm.addVariable(b));

    QVERIFY(vm.addDerivedVariable(QStringLiteral("sum"), QStringLiteral("a + b"),
                                  Variable::DataType::Float));
    Variable* sum = vm.findVariable(QStringLiteral("sum"));
    QVERIFY(sum);
    QVERIFY2(!sum->value().isValid(),
             "★ 依赖还没值时,派生量不许先给一个数(0 也算 —— 0 看起来跟真值一样)");

    // 源变量到齐了 → 派生量自动算出来
    a->setValue(QVariant::fromValue<float>(3.0f));
    QVERIFY2(!sum->value().isValid(), "★ 还差一个依赖,仍然不该算");
    b->setValue(QVariant::fromValue<float>(4.0f));
    QCOMPARE(sum->value().toFloat(), 7.0f);
}

void TestDerivedVariable::cyclicDerived_terminatesInsteadOfHanging() {
    // 用户能配出环:d1 = d2 + 1,d2 = d1 + 1。
    //   addDerivedVariable 建不了环(建 d1 时 d2 还不存在),
    //   所以这里手工建 —— 而"手工建"正是外部能走到的路径。
    //   要求:不死循环、不爆栈。这条要是红的,现场表现是界面卡死。
    VariableManager vm;
    auto* d1 = new Variable(QStringLiteral("d1"), quint16(0), Variable::DataType::Float,
                            Variable::Access::ReadOnly, &vm);
    d1->setExpression(QStringLiteral("d2 + 1"));
    d1->setDependencies(QStringList{QStringLiteral("d2")});
    QVERIFY(vm.addVariable(d1));

    auto* d2 = new Variable(QStringLiteral("d2"), quint16(0), Variable::DataType::Float,
                            Variable::Access::ReadOnly, &vm);
    d2->setExpression(QStringLiteral("d1 + 1"));
    d2->setDependencies(QStringList{QStringLiteral("d1")});
    QVERIFY(vm.addVariable(d2));

    // ★ 派生量不许被外部写(setValue 拒收),所以用它的专属入口播种一个初值。
    d1->recomputeAsDerived(1.0);

    // 没死循环 = 这行能跑完。值是不是"对"不重要(环本身没有唯一定义)。
    QVERIFY2(d1->value().isValid() && d2->value().isValid(),
             "环上的两个量都该有个有限的值 —— 说明重算收敛了,不是死循环");
}

// ============================================================================
// 派生量不许冒充寄存器
// ============================================================================

void TestDerivedVariable::derivedVariable_rejectsExternalWrites() {
    // ★ 写这一侧的门。
    //   派生量的 address 是 0 —— 那是【真的 plc1 的 0 号寄存器】。
    //   以前界面改一个绑在派生量上的控件:值被改了 → markDirty
    //   → WriteScheduler 把它当【真变量】发出去 → 写到 plc1 的 0 号寄存器。
    //   那是往真实设备的一个真实地址写了一个算出来的数。
    VariableManager vm;
    makeSource(&vm, QStringLiteral("a"), 5.0f);
    QVERIFY(vm.addDerivedVariable(QStringLiteral("mul"), QStringLiteral("a * 2"),
                                  Variable::DataType::Float));
    Variable* mul = vm.findVariable(QStringLiteral("mul"));
    QVERIFY(mul);

    const QVariant before = mul->value();
    const auto result = mul->setValue(QVariant::fromValue<float>(999.0f));

    QVERIFY2(result == Variable::SetValueResult::Rejected,
             "★ 派生量必须拒收外部写 —— 它的值只由表达式决定");
    QVERIFY2(mul->value() == before, "★ 拒收之后值不许变");
    QVERIFY2(!mul->isDirty(), "★ 拒收之后不许标 dirty —— 否则 WriteScheduler 会把它发到 plc1 的 0 号寄存器");
}

void TestDerivedVariable::normalVariable_stillAcceptsWrites() {
    // "不误伤"的那一半:普通变量的写路径一点都不能受影响。
    Variable v(QStringLiteral("Speed"), quint16(100), Variable::DataType::Float,
               Variable::Access::ReadWrite);
    const auto result = v.setValue(QVariant::fromValue<float>(12.0f));
    QCOMPARE(result, Variable::SetValueResult::Accepted);
    QCOMPARE(v.value().toFloat(), 12.0f);
}

QTEST_MAIN(TestDerivedVariable)
#include "tst_DerivedVariable.moc"
