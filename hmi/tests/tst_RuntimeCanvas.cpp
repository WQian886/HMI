// ============================================================================
// 阶段 E · E.5(2026-09-29)· RuntimeCanvas 运行时画布测试
//
// 【这个测试文件为什么重要】
//   E 阶段标"完工"时,编辑器能画能存,但主画面从来不显示 —— 整条链断在
//   "存出去的 JSON 没人读"这一环,而且【一行报错都没有】。
//   本文件测的就是这条链,重点不是测画布,是测:
//     ① JSON 里的控件能真的建出来(不是"文件读成功了"就算过)
//     ② variableName 字符串能真的变成 Variable* 绑定(不是"存进去了"就算过)
//     ③ 绑定失败的能被【看见】,而不是静悄悄什么都不发生
//   ②③ 是重点。以前这两件事【完全没人测】,所以它们【完全没发生】。
// ============================================================================

#include <QtTest>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QGraphicsScene>

#include "editor/RuntimeCanvas.h"
#include "editor/EditorMainWindow.h"
#include "widgets/base/ControlBase.h"
#include "widgets/NumberDisplay.h"
#include "widgets/LabelControl.h"
#include "VariableManager.h"
#include "Variable.h"
#include "Project.h"
#include "auth/CurrentUser.h"
#include "auth/User.h"
#include "auth/Role.h"

using namespace qwhmi::editor;
using namespace qwhmi::widgets;

class TestRuntimeCanvas : public QObject {
    Q_OBJECT
private slots:
    // ===== 1. 注册表预热 =====
    void registry_hasAll22Types();

    // ===== 2. 纯 JSON → 控件 =====
    void load_restoresControlsFromJson();
    void load_missingFile_setsLastError();

    // ===== 3. ★ 绑定:JSON 字符串 → Variable* =====
    void bindByName_bindsExistingVariable();
    void bindByName_acceptsShortName();
    void bindByName_unknownName_returnsFalse();
    void load_controlsTrackBindingCounts();
    void load_unboundNamesReportedNotSilent();

    // ===== 4. ★ 全链路:编辑器画 → 存 → 运行时画布显示 =====
    void fullChain_editorSave_thenRuntimeLoad();
    void fullChain_boundControlFollowsLiveVariable();
};

// =============================================================================
// 1. 注册表
// =============================================================================

// ★ 这个断言是 E.5 抽出 ensureControlTypesRegistered 的直接理由:
//   以前预热列表只在 EditorMainWindow 构造函数里,谁 new 编辑器谁触发。
//   RuntimeCanvas 若不开编辑器就自己加载,注册表是空的,create() 全返 null,
//   画面空白且【零报错】。抽成静态方法后两边共用一份。
void TestRuntimeCanvas::registry_hasAll22Types() {
    EditorMainWindow::ensureControlTypesRegistered();
    // 幂等性:调两次结果一致(测试之间会反复 new 画布,不能互相干扰)
    const int n1 = ControlBase::registeredTypeNames().size();
    EditorMainWindow::ensureControlTypesRegistered();
    QCOMPARE(ControlBase::registeredTypeNames().size(), n1);
    // ★ 数字跟 tst_EditorMainWindow 一样卡 22 —— 加控件记得同步改两处
    QCOMPARE(n1, 22);
}

// =============================================================================
// 2. 纯 JSON → 控件
// =============================================================================

static QString writeTempProject(const QJsonObject& root, const QString& name) {
    const QString path = QDir::temp().filePath(name);
    QFile f(path);
    f.open(QIODevice::WriteOnly);
    f.write(QJsonDocument(root).toJson());
    f.close();
    return path;
}

static QJsonObject makeRoot(const QJsonArray& controls) {
    QJsonObject root;
    root["schema_version"] = 3;
    QJsonArray screens;
    QJsonObject screen0;
    screen0["name"] = "主屏";
    screen0["controls"] = controls;
    screens.append(screen0);
    root["screens"] = screens;
    return root;
}

void TestRuntimeCanvas::load_restoresControlsFromJson() {
    QJsonArray ctrls;
    QJsonObject led;
    led["type"] = "LED";
    led["position"] = QJsonObject{{"x", 30.0}, {"y", 40.0}};
    led["size"]     = QJsonObject{{"width", 50.0}, {"height", 60.0}};
    ctrls.append(led);

    const QString path = writeTempProject(makeRoot(ctrls), "tst_rt_led.hmi.json");
    RuntimeCanvas canvas(nullptr);
    QCOMPARE(canvas.load(path), 1);
    QCOMPARE(canvas.controls().size(), 1);
    // ★ 断言"建出来的对象真在 scene 里",不只是列表里有 —— 列表有但没 addItem
    //   的情况画面上就是空的,以前没人分开查过这两件事
    QCOMPARE(canvas.scene()->items().size(), 1);
    QFile::remove(path);
}

void TestRuntimeCanvas::load_missingFile_setsLastError() {
    RuntimeCanvas canvas(nullptr);
    QCOMPARE(canvas.load("Z:/绝对不存在的路径/nope.hmi.json"), 0);
    // ★ 失败必须留痕:空画布 + 空 error = 用户完全无从判断出了什么事
    QVERIFY(!canvas.lastError().isEmpty());
}

// =============================================================================
// 3. ★ 绑定:JSON 字符串 → Variable*
// =============================================================================

void TestRuntimeCanvas::bindByName_bindsExistingVariable() {
    VariableManager vm;
    auto* v = new Variable("Speed", 40001, Variable::DataType::Float,
                            Variable::Access::ReadWrite);
    v->setPlcId("plc1");
    QVERIFY(vm.addVariable(v));

    auto* nd = new NumberDisplay;
    nd->setVariableName("plc1.Speed");
    QVERIFY(nd->bindByName(&vm));
    QCOMPARE(nd->boundVariable(), v);
    delete nd;
}

void TestRuntimeCanvas::bindByName_acceptsShortName() {
    // 为什么收短名:VariableManager 里按 name 存的是 "Speed",
    //   而属性面板显示的却是 "plc1.Speed"。用户在 JSON 里两种都可能写。
    VariableManager vm;
    auto* v = new Variable("Temp", 40002, Variable::DataType::Float,
                            Variable::Access::ReadOnly);
    QVERIFY(vm.addVariable(v));

    auto* nd = new NumberDisplay;
    nd->setVariableName("Temp");          // 只有短名
    QVERIFY(nd->bindByName(&vm));
    QCOMPARE(nd->boundVariable(), v);
    // ★ 绑上以后 variableName 要保持用户填的原样,不能被改写成另一种
    //   —— 属性面板显示的名字跟用户填的不一致,用户会以为自己改的东西丢了
    QCOMPARE(nd->variableName(), QString("Temp"));
    delete nd;
}

void TestRuntimeCanvas::bindByName_unknownName_returnsFalse() {
    VariableManager vm;
    auto* v = new Variable("Speed", 40001, Variable::DataType::Float,
                            Variable::Access::ReadOnly);
    QVERIFY(vm.addVariable(v));

    auto* nd = new NumberDisplay;
    nd->setVariableName("Speeed");         // ★ 故意拼错
    // ★ 返回 false 而不是假装成功 —— 这是"不撒谎"的底线
    QVERIFY(!nd->bindByName(&vm));
    QCOMPARE(nd->boundVariable(), nullptr);
    delete nd;
}

void TestRuntimeCanvas::load_controlsTrackBindingCounts() {
    VariableManager vm;
    auto* v = new Variable("Speed", 40001, Variable::DataType::Float,
                            Variable::Access::ReadWrite);
    QVERIFY(vm.addVariable(v));

    QJsonArray ctrls;
    QJsonObject ok;
    // ★ type 必须是【注册表短名】,不是类名。NumberDisplay 类注册成 "NumberDisplay",
    //   写 "Number" 会走 loadControlsFromJson 的 qWarning 分支静默跳过 —— 工程文件
    //   里少一个控件,画面上凭空少一块,且不报任何错(见 ControlBase.cpp 各控件构造)。
    //   少写一个 "Number" 后缀的代价:加载数从 2 变 1,第一眼看不出是名字错了。
    ok["type"] = "NumberDisplay";
    ok["variableName"] = "plc1.Speed";
    ctrls.append(ok);
    QJsonObject noName;                    // 没填变量名 = 不该算失败
    noName["type"] = "Label";
    ctrls.append(noName);

    const QString path = writeTempProject(makeRoot(ctrls), "tst_rt_bind.hmi.json");
    RuntimeCanvas canvas(&vm);
    QCOMPARE(canvas.load(path), 2);
    // ★ 关键断言:一个填了名的绑上了,一个没填的不进分母
    QCOMPARE(canvas.expectedBindCount(), 1);
    QCOMPARE(canvas.boundCount(), 1);
    QVERIFY(canvas.unboundNames().isEmpty());
    QFile::remove(path);
}

void TestRuntimeCanvas::load_unboundNamesReportedNotSilent() {
    // ★★ 本文件最重要的一条:变量名打错时,必须【报得出来】
    //   以前没这条链路,现象是"画面上数字永远是 0",用户根本猜不到是变量名错了。
    //   静默失败是工业 HMI 最难查的一类 bug —— 设备没坏,程序没崩,就是不对。
    VariableManager vm;
    auto* v = new Variable("Speed", 40001, Variable::DataType::Float,
                            Variable::Access::ReadWrite);
    QVERIFY(vm.addVariable(v));

    QJsonArray ctrls;
    QJsonObject bad;
    bad["type"] = "NumberDisplay";
    bad["variableName"] = "Speeed";        // 拼错
    ctrls.append(bad);

    const QString path = writeTempProject(makeRoot(ctrls), "tst_rt_unbound.hmi.json");
    RuntimeCanvas canvas(&vm);
    QCOMPARE(canvas.load(path), 1);        // ★ 控件【建出来了】—— 画面不是空的
    QCOMPARE(canvas.boundCount(), 0);
    QCOMPARE(canvas.expectedBindCount(), 1);
    // ★ 但名字必须出现在清单里,UI 拿它弹窗告诉用户
    QCOMPARE(canvas.unboundNames(), QStringList{QStringLiteral("Speeed")});
    QFile::remove(path);
}

// =============================================================================
// 4. ★ 全链路:编辑器画 → 存 → 运行时画布显示
// =============================================================================

// ★ 这才是用户真正关心的:"我在编辑器里画的,主画面能看见吗?"
//   走真实的两端:EditorMainWindow::onFileSave 的真实存盘路径
//   (Project::saveToFile)→ RuntimeCanvas::load 的真实读盘路径。
//   不用手工拼 JSON —— 手工拼只能证明"格式对",证不了"这条路真的通"。
void TestRuntimeCanvas::fullChain_editorSave_thenRuntimeLoad() {
    VariableManager vm;
    // 变量表里有 Speed(模拟主画面正在跑的那张表)
    auto* v = new Variable("Speed", 40001, Variable::DataType::Float,
                            Variable::Access::ReadWrite);
    v->setPlcId("plc1");
    QVERIFY(vm.addVariable(v));

    // --- 编辑器侧:真画 2 个控件,绑 1 个变量 ---
    EditorMainWindow editor(nullptr, &vm);
    auto* nd = new NumberDisplay;
    nd->setPosition({100, 120});
    nd->setSize({200, 60});
    nd->setVariableName("plc1.Speed");
    editor.addControl(nd);

    auto* lbl = new LabelControl;
    lbl->setPosition({10, 20});
    lbl->setSize({80, 30});
    editor.addControl(lbl);
    QCOMPARE(editor.controls().size(), 2);

    // ★ 编辑器侧也要能绑上 —— 这是 E.5 给 addControl 加 bindByName 的意义
    QCOMPARE(nd->boundVariable(), v);

    const QString path = QDir::temp().filePath("tst_rt_fullchain.hmi.json");
    Project saver(nullptr, nullptr, nullptr, nullptr, nullptr);
    QVERIFY(saver.saveToFile(path, editor.controls()));

    // --- 运行时侧:同一份文件,不同的 VariableManager 实例 ---
    //   刻意用另一个 vm,证明绑定是靠【名字】查出来的,不是靠碰巧同一个指针
    VariableManager vm2;
    auto* v2 = new Variable("Speed", 40001, Variable::DataType::Float,
                             Variable::Access::ReadWrite);
    v2->setPlcId("plc1");
    QVERIFY(vm2.addVariable(v2));

    RuntimeCanvas canvas(&vm2);
    QCOMPARE(canvas.load(path), 2);            // ★ 两个控件都出来了
    QCOMPARE(canvas.scene()->items().size(), 2);
    QCOMPARE(canvas.boundCount(), 1);         // ★ 填了变量的那个绑上了
    QCOMPARE(canvas.expectedBindCount(), 1);
    QVERIFY(canvas.unboundNames().isEmpty());  // 没填名的不算失败

    // 几何也要对 —— 位置错了用户一眼就能看出来,存读这一环最容易漏的就是它
    NumberDisplay* loadedNd = nullptr;
    for (auto* c : canvas.controls()) {
        if (auto* n = qobject_cast<NumberDisplay*>(c)) loadedNd = n;
    }
    QVERIFY(loadedNd != nullptr);
    QCOMPARE(loadedNd->position(), QPointF(100, 120));
    QCOMPARE(loadedNd->size(), QSizeF(200, 60));

    QFile::remove(path);
}

// ★ "绑上了"不等于"能用" —— 还要证明变量一变,画面跟着变。
//   只测 boundVariable() != nullptr 是不够的:那种情况 connect 没接上照样通过,
//   症状是"绑定成功但数字永远不动",比没绑还难查(因为没报错)。
void TestRuntimeCanvas::fullChain_boundControlFollowsLiveVariable() {
    VariableManager vm;
    auto* v = new Variable("Speed", 40001, Variable::DataType::Float,
                            Variable::Access::ReadWrite);
    v->setPlcId("plc1");
    QVERIFY(vm.addVariable(v));

    QJsonArray ctrls;
    QJsonObject nd;
    nd["type"] = "NumberDisplay";
    nd["variableName"] = "plc1.Speed";
    ctrls.append(nd);

    const QString path = writeTempProject(makeRoot(ctrls), "tst_rt_follow.hmi.json");
    RuntimeCanvas canvas(&vm);
    QCOMPARE(canvas.load(path), 1);
    QCOMPARE(canvas.boundCount(), 1);

    auto* c = canvas.controls().first();
    // PLC 侧:变量变 → 控件的 value 跟着变(这才是"绑上了"的实际意义)
    //
    // ★★ 这里的 1234.5f 【必须带 f 后缀】,写成 1234.5 就是 double,
    //   Variable::setValue 会因为类型不匹配直接【拒绝赋值并返回失败】:
    //       Variable "Speed" type mismatch: expected "Float" , got double
    //   而拒绝是【静默】的 —— 变量保持 0,控件也就保持 0,全链路看着好好的,
    //   就是数据永远不动。这个坑写测试时踩过一次,记在这里免得再踩。
    //   真实 PLC(ModbusDriver)交上来的是 float,所以生产代码用 float 是对的。
    v->setValue(1234.5f);
    QCOMPARE(c->value().toDouble(), 1234.5);

    // ★ 再补一刀:从控件写回去,验证反向通道也通(E.1c 的 writeToPlc)
    //   【必须先登录】CurrentUser 的默认 role 是 Viewer,而 Viewer 没有
    //   WriteVariable 权限 —— writeToPlc 的闸 3 会直接拒。
    //   第一次写这个用例忘了登录,writeToPlc 返回 false,一度以为是绑定没成功。
    //   这里显式登成 Operator(有 WriteVariable),别依赖默认值。
    qwhmi::auth::User op;
    op.name = QStringLiteral("tst_operator");
    op.role = qwhmi::auth::Role::Operator;
    qwhmi::auth::CurrentUser::instance().login(op);

    QVERIFY(c->writeToPlc(999.0f));
    QCOMPARE(v->value().toDouble(), 999.0);

    // 收尾:CurrentUser 是单例,登出免得影响同进程后续用例
    qwhmi::auth::CurrentUser::instance().logout();

    QFile::remove(path);
}

QTEST_MAIN(TestRuntimeCanvas)
#include "tst_RuntimeCanvas.moc"
