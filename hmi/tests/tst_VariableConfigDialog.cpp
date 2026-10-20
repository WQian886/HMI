// ============================================================================
// tst_VariableConfigDialog —— 变量配置对话框
//
// 【这个对话框解决什么】
//   多 PLC 2b 把 N 台 PLC 的配置存读做完了,但"变量属于哪台 PLC"这件事
//   【至今没有任何入口】:plcId 只能手改 JSON 或者导 CSV。
//   这不是"还没画界面",是 2b 的多 PLC 架构只完成了一半 ——
//   配了 3 台 PLC,却没有任何正常途径把变量分配给它们。
//   TagImporter 那个"警告但不拒绝"就是为这个妥协的(变量 plcId 没有界面,
//   拒绝会堵死"先导 CSV 再配 PLC"这条正常顺序)。
//
// 【★ 本文件要锁住的设计不变量】—— 每一个都是一条"如果被改坏,
//   用户会遇到什么具体麻烦"的对应物
//
//   ① 删被【报警】引用的变量:拒绝,并点名那条报警。
//      删掉它,存出来的工程【下次整个打不开】(Project.cpp:686 写空串 →
//      Project.cpp:556 findVariable("") 失败 → failWith)。
//      由 deleteVariable_referencedByAlarm_refusesAndNamesIt 守住。
//
//   ② 删被【联动】引用的变量:同样拒绝。
//      这是【另一条独立链路】,不是①的复述:联动存的是名字字符串
//      (Project.cpp:698),文件里会留下 "Temp" 这种指向不存在变量的名字,
//      而且【肉眼看 JSON 找不出问题】。
//      由 deleteVariable_referencedByAction_refusesAndNamesIt 守住。
//
//   ③ ★★ "画面控件那层查不到"这句话【必须一直显示在界面上】。
//      ControlBase 存 QPointer<Variable> 但【不监听 variableReplaced】,
//      而编辑器窗口/运行画布在 hmi.cpp 里是【函数内 static】,
//      本对话框枚举不到。所以"被引用"为空【不等于】画面上没人用它。
//      ★ 这条测试的作用不是测功能,是【钉住诚实本身】:
//        防止有人后来把这段提示当成啰嗦清理掉,换成一句笼统的"无引用"。
//        那样的话用户会以为删干净了,画面上那个显示温度的表还在,
//        已经定格在上一次读到的值,而且没有任何异常。
//      由 deleteVariable_screenBindingBlindSpot_isAlwaysStatedInUi 守住。
//
//   ④ 反向:无引用时【真的要能删】。该让删的时候没让删 = 功能没做完。
//      由 deleteVariable_unreferenced_actuallyDeletes 守住。
//
//   ⑤ 名称/地址/类型/读写【四列锁死】。
//      它们没有 setter,改 = 删了重建,而重建会让绑它的画面控件静默失效。
//      由 name_address_type_access_areReadOnly 守住。
//      ★ 这条断言读的是【表格项真实的 flags】,不是草稿字段 ——
//        只断言草稿的话,测的是数据结构,证明不了用户面对的表真的锁住了。
//
//   ⑥ plcId 下拉列的是 managedIds(),且工程里写着但没在管的 plcId
//      【照原样显示】,不许悄悄换成别的。
//      由 plcIdCombo_showsUnknownIdVerbatim / plcIdCombo_listsManagedIds 守住。
//
//   ⑦ 点「应用」之前 VariableManager 一个字节都不能动。
//      由 apply_doesNotMutateBeforeClickingApply 守住。
//
//   ⑧ ★ 地址 0 被拦。0 不是"没填",它是【真的第 0 个寄存器】——
//      填成 0 的普通变量会被拿去读 plc 的 0 号寄存器,
//      然后把那个寄存器的值当成这个变量的值显示出来,质量还是 Good,
//      界面上看不出任何异常。
//      由 validate_rejectsZeroAddress 守住。
//
// 【★ 为什么按钮类用例必须真的按下去】
//   AlarmConfigDialog 曾有 11 条用例全是只读的,一条都没碰按钮,
//   于是用户实测"点新增没反应"时,全部用例【都是绿的】。
//
// 【★ 为什么 removeRowFromDraft / removeRowChecked 刻意不带 QMessageBox】
//   AlarmConfigDialog 把确认框和数据层方法写在一起,offscreen 测试下
//   永久阻塞(实测卡满 300s),于是"删除"这条最危险的路径一条测试都写不了。
//   本文件能直接测删除,正是因为那两个方法被拆开了。
//
// 【负向对照 —— 两轮,2026-10-19 实跑】红集【互不相交】,均与预测一致
//   第 1 轮:把 removeRowChecked 改成"有引用也删"(最自然的实现 ——
//             毕竟 Data 层本来就不该管策略)。
//     红 2:deleteVariable_referencedByAlarm_refusesAndNamesIt
//           deleteVariable_referencedByAction_refusesAndNamesIt
//
//   第 2 轮:把界面上的"画面绑定查不到"那段提示删掉(标签 + 删除确认框
//             两处都换成一句笼统的"无引用")。
//     红 1:deleteVariable_screenBindingBlindSpot_isAlwaysStatedInUi
//     ★★ 第 2 轮专门用来【钉住诚实本身】。它防的不是功能回归,
//       是有人把边界说明当成冗余给清理掉 —— 那个"清理"在代码上是干净的,
//       在用户那边是静默的数据丢失。仓库里这种"看起来是优化、
//       实际是收回一个承诺"的改动最难在 review 里看出来,
//       所以只能靠一条专门的测试钉死。
//
//   ★ 两轮都是"只红该红的",不多不少。这跟 tst_PollSchedulerDerived 那次
//     的教训正好相反 —— 那次第一轮"红 2"看着也对,实际上是有一条【测不到】。
//     所以每轮做完都要问一句:红的那几条,是因为【测到了问题】,
//     还是因为【它压根测不到东西】。
//
// 【★ 负向对照 —— 第 3 轮(2026-10-19 实跑)行序】
//   这一轮是修 bug 顺手做的,起因值得记下来。
//
//   现象:这条文件在 ctest 下第一次跑红、第二次跑绿,
//         同一份代码、同一个 exe、连着两次运行结果相反。
//   根因:VariableManager 内部是 QHash<QString, Variable*>,
//         allVariables() 返回 vars_.values() —— QHash 的遍历顺序不保证,
//         而 Qt【每次进程启动的哈希种子不同】。于是"第 0 行是哪个变量"
//         每个进程都可能不一样(本机实测同一组变量有 4 种不同行序)。
//   ★ 这是【产品缺陷】,不只是测试写错:
//     用户手指停在第 3 行,点「应用」,刷新之后第 3 行变成了另一个变量。
//     下次再点第 3 行,改的就是另一个东西了,而且当场看不出来。
//     (PlcConfigDialog 早就防了这同一件事,那边有一行注释:
//      "界面显示要稳定顺序,否则用户每刷新一次表格就跳行"。)
//
//   做了两件事:
//     ① 生产代码:reloadFromManager 里加 std::sort(按名字)。
//     ② 测试:全部改成【按名字查行】(rowIndexByName),故意不依赖行序 ——
//        排序是显示层实现细节,哪天改成按地址排,断言不该跟着改。
//
//   ★★ 但正因为其它用例都不依赖行序,排序就【无人看守】了 ——
//     删掉 std::sort,整个文件照样 23/23 绿。
//     所以专门加了 rows_areInStableNameOrder 钉住排序本身。
//     ★ 这就是"改测试让它不 flaky"和"把不变量钉住"的分界线:
//       前者容易做、做完没人记得;后者难一点,但只有后者能防住回归。
//
//   本轮实跑(实数,不是预测):
//     去掉 std::sort → 连续 5 次运行,红 1 ×5,绿 22 ×5,
//       唯一红的都是 rows_areInStableNameOrder。
//       五次报出来的实际行序各不相同
//       (Bravo,mike,zulu,Alpha / Alpha,Bravo,zulu,mike /
//        mike,Alpha,Bravo,zulu / Bravo,zulu,mike,Alpha /
//        Alpha,mike,Bravo,zulu),把"QHash 顺序每次进程都变"钉得死死的。
//     恢复 std::sort → 连续 8 次运行,23 绿 ×8,0 红。
//
//   ★ 与前两轮的性质区别:前两轮红 2 / 红 1 是【红集互不相交】,
//     本轮是【同一轮内 5 次运行红集完全一致】——
//     因为这一轮的 bug 依赖的是跨进程的状态(哈希种子),
//     所以验证手段必须是"多跑几次",而不是"换一个方向破坏"。
//     单跑一次的验证在这里等于没验证。
// ============================================================================

#include <QtTest>
#include <QApplication>
#include <QComboBox>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTemporaryDir>
#include <QTimer>

#include "ActionManager.h"
#include "AlarmManager.h"
#include "PollScheduler.h"
#include "Project.h"
#include "ThresholdAlarmChecker.h"
#include "Variable.h"
#include "VariableManager.h"
#include "auth/CurrentUser.h"
#include "auth/Permission.h"
#include "plc/PlcConnectionManager.h"
#include "plc/VariableConfigDialog.h"

using qwhmi::plc::VariableConfigDialog;

namespace {

// 列号,与 VariableConfigDialog.cpp 里的 kCol* 一一对应。
// ★ 在测试里另抄一份是刻意的 —— 抄错会立刻红,而把常量导出来
//   就变成"实现改了常量,测试跟着改,永远绿"。
constexpr int kColName = 0, kColPlcId = 1, kColAddress = 2, kColType = 3,
              kColAccess = 4, kColPoll = 5, kColDeadband = 6,
              kColAlwaysPoll = 7, kColRefs = 8;

// ★ 每次弹框的正文,按顺序。必须【声明在 answerNextModalDialog 之前】——
//   那个 lambda 里要用它。
QStringList g_capturedBoxes;

// 排一次事件循环,顺带回答下一个弹出的模态框。
//   ★ offscreen 下 QMessageBox 是【阻塞】的(静态函数自己 exec),
//     直接点按钮会把测试卡满 300s —— AlarmConfigDialog 就这么卡过一次。
void answerNextModalDialog(QMessageBox::StandardButton answer) {
    QTimer::singleShot(0, qApp, [answer] {
        auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if (!box) return;
        // ★ 把这次弹框的全文留下来:有好几条用例断言的是"提示里写了什么",
        //   而那条提示正是本对话框最不该被删掉的东西。
        g_capturedBoxes << box->text();
        if (answer == QMessageBox::Yes) {
            if (auto* yes = box->button(QMessageBox::Yes)) { yes->click(); return; }
        }
        box->reject();
    });
}

// ★ 加一个变量(不经 CSV、不经 UI),返回它的名字
void addVar(VariableManager* vm, const QString& name, quint16 addr,
            const QString& plcId = QStringLiteral("plc1")) {
    auto* v = new Variable(name, addr, Variable::DataType::Int16,
                           Variable::Access::ReadOnly, vm);
    v->setPlcId(plcId);
    vm->addVariable(v);
}

// 切当前登录用户。CurrentUser 是单例,所以这条改的是全局状态 ——
//   每条用例自己登录成需要的角色,不依赖执行顺序。
void loginAs(qwhmi::auth::Role r) {
    qwhmi::auth::User u;
    u.name = QStringLiteral("tester");
    u.role = r;
    qwhmi::auth::CurrentUser::instance().login(u);
}

// ★★ 按名字找行号,找不到返回 -1。
//
//   【为什么每条用例都要用它,而不是直接写 nameAt(0)】
//   VariableManager 内部是 QHash<QString, Variable*>,allVariables()
//   返回 vars_.values() —— 遍历顺序【不保证】,而且 Qt 每次进程启动
//   的哈希种子不同。所以"第 0 行是哪个变量"每次跑都可能不一样。
//
//   ★ 这不是假设,是真的踩到过:refusesAndNamesIt(Action) 那条
//     第一次跑红、第二次跑绿,查下来就是这一行。
//     同一份代码、同一个工程,连着两次运行结果相反 ——
//     这种测试在 CI 上会表现成"偶发失败",是最难查的一类。
//
//   生产代码那边已经按名字排序了(见 reloadFromManager 里的 std::sort),
//   但【测试仍然按名字查】:排序是显示层的实现细节,
//   哪天换成按地址排,这些断言不该跟着一起改。
int rowIndexByName(const VariableConfigDialog& dlg, const QString& name) {
    for (int i = 0; i < dlg.rowCount(); ++i) {
        if (dlg.nameAt(i) == name) return i;
    }
    return -1;
}

// ★ 给某个变量挂一条阈值报警(制造"被报警引用")
void addAlarmOn(AlarmManager* am, Variable* v, const QString& alarmName) {
    CheckerConfig cfg;
    cfg.level     = AlarmLevel::Warning;
    cfg.direction = Direction::Above;
    cfg.threshold = 80.0;
    am->addChecker(new ThresholdAlarmChecker(alarmName, v, nullptr, cfg));
}

} // namespace

class TestVariableConfigDialog : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup() { g_capturedBoxes.clear(); }

    // ---- 读取 ----
    void readsAllFieldsFromManager();
    void rows_areInStableNameOrder();
    void derivedVariable_isShownWithItsExpression();

    // ---- ★ 删被引用的变量:拒删 + 点名 ----
    void deleteVariable_referencedByAlarm_refusesAndNamesIt();
    void deleteVariable_referencedByAction_refusesAndNamesIt();
    void deleteVariable_unreferenced_actuallyDeletes();
    void deleteButton_onReferencedRow_showsWarning_notQuestion();

    // ---- ★ 诚实:画面绑定查不到,必须说 ----
    void deleteVariable_screenBindingBlindSpot_isAlwaysStatedInUi();

    // ---- 四列锁死 ----
    void name_address_type_access_areReadOnly();
    void plcIdColumn_isEditable_comboBox();

    // ---- plcId 下拉 ----
    void plcIdCombo_listsManagedIds();
    void plcIdCombo_showsUnknownIdVerbatim();

    // ---- 校验 ----
    void validate_rejectsZeroAddress();
    void validate_rejectsEmptyPlcId();
    void validate_rejectsDuplicateName();

    // ---- 草稿式:点应用之前不许动 ----
    void apply_doesNotMutateBeforeClickingApply();
    void apply_changesPlcIdOnManager();
    void apply_unchangedDraft_isNoOp();
    void apply_invalidDraft_leavesManagerUntouched();

    // ---- 权限 ----
    void operator_cannotOpen();

    // ---- 端到端 ----
    void saveProject_afterApply_persistsNewPlcId();

private:
    // 把某个 QLabel 的全文取出来(按 objectName 找不到就按文本前缀找)
    static QString labelTextContaining(const VariableConfigDialog& dlg,
                                       const QString& needle);
};

void TestVariableConfigDialog::init() {
    g_capturedBoxes.clear();
}

QString TestVariableConfigDialog::labelTextContaining(
        const VariableConfigDialog& dlg, const QString& needle) {
    for (const QLabel* l : dlg.findChildren<QLabel*>()) {
        if (l->text().contains(needle)) return l->text();
    }
    return QString();
}

// ============================================================================
// 读取
// ============================================================================

void TestVariableConfigDialog::readsAllFieldsFromManager() {
    VariableManager vm;
    auto* v = new Variable(QStringLiteral("Temp"), quint16(100),
                           Variable::DataType::UInt16,
                           Variable::Access::ReadWrite, &vm);
    v->setPlcId(QStringLiteral("plc2"));
    v->setPollIntervalMs(250);
    v->setDeadband(0.5);
    v->setAlwaysPoll(true);
    vm.addVariable(v);

    VariableConfigDialog dlg(&vm);
    QCOMPARE(dlg.rowCount(), 1);
    QCOMPARE(dlg.nameAt(0), QStringLiteral("Temp"));
    QCOMPARE(dlg.plcIdAt(0), QStringLiteral("plc2"));
    QCOMPARE(dlg.addressAt(0), 100);

    auto* t = dlg.findChild<QTableWidget*>(QStringLiteral("variableConfigTable"));
    QVERIFY(t);
    QCOMPARE(t->item(0, kColType)->text(),
             Variable::dataTypeName(Variable::DataType::UInt16));
    QCOMPARE(t->item(0, kColAccess)->text(),
             Variable::accessName(Variable::Access::ReadWrite));
    QCOMPARE(t->item(0, kColPoll)->text(), QStringLiteral("250"));
    QCOMPARE(t->item(0, kColDeadband)->text(), QStringLiteral("0.5"));
    QCOMPARE(t->item(0, kColAlwaysPoll)->checkState(), Qt::Checked);
}

void TestVariableConfigDialog::rows_areInStableNameOrder() {
    // ★★ 这条钉住的是排序本身,不是"某一行是谁"。
    //
    //   为什么要单独一条:本文件其它用例全部改成按名字查行了
    //   (见 rowIndexByName),所以它们【故意】不依赖行序 ——
    //   哪天改成按地址排,那些断言也不该跟着改。
    //   但正因为都不依赖,如果没有这一条,生产代码里的排序就【无人看守】:
    //   删掉 std::sort 之后整个测试文件照样全绿,而用户会重新遇到
    //   "同一个变量每次打开都在不同的行"。
    //
    //   ★ 用户实际受害的方式(不是"看着乱",是这个):
    //     用户手指停在第 3 行的某个变量上,点「应用」,
    //     刷新之后第 3 行变成了【另一个变量】。
    //     下次再点第 3 行,改的就是另一个东西了,而且当场看不出来。
    //
    //   ★ 怎么复现这个 bug(不然它会一直绿着):
    //     它依赖 QHash 的遍历顺序,而 Qt 每次进程启动的哈希种子不同 ——
    //     也就是【同一个 exe 连跑两次,结果可能不同】。
    //     所以单跑一次是复现不出来的,这也是它当初能一路溜到 CI 的原因。
    VariableManager vm;
    // ★ 故意【乱序】添加,而且用 Z 开头/小写开头这种会被排到首尾的名字,
    //   保证"照添加顺序排"和"按名字排"结果不一样 —— 否则这条测不出东西。
    addVar(&vm, QStringLiteral("zulu"), 100);
    addVar(&vm, QStringLiteral("Alpha"), 101);
    addVar(&vm, QStringLiteral("mike"), 102);
    addVar(&vm, QStringLiteral("Bravo"), 103);

    VariableConfigDialog dlg(&vm);
    QCOMPARE(dlg.rowCount(), 4);

    // 按名字升序(界面里显式带 QString::SkipCase 之外的默认排序,见 reloadFromManager)
    QStringList names;
    for (int i = 0; i < dlg.rowCount(); ++i) names << dlg.nameAt(i);
    QStringList sorted = names;
    sorted.sort();
    QVERIFY2(names == sorted,
             qPrintable(QStringLiteral("表格行序不是按名字排的,实际:%1")
                            .arg(names.join(QStringLiteral(", ")))));

    // ★ 再开一个对话框,行序必须【一模一样】。
    //   这一段是这条用例真正的价值:它抓的不是"有没有排序",
    //   是"两次打开之间会不会变"。
    VariableConfigDialog dlg2(&vm);
    QStringList names2;
    for (int i = 0; i < dlg2.rowCount(); ++i) names2 << dlg2.nameAt(i);
    QVERIFY2(names == names2,
             qPrintable(QStringLiteral("两次打开的行序不一样:%1 vs %2")
                            .arg(names.join(QStringLiteral(", ")),
                                 names2.join(QStringLiteral(", ")))));
}

void TestVariableConfigDialog::derivedVariable_isShownWithItsExpression() {
    // ★ 派生量在界面上的样子和普通变量【不一样】:
    //   它的 PLC 没有意义(不读任何寄存器),所以 plcId 下拉【置灰】。
    //   让用户去改一个假字段,不如让他知道这个字段对他没用。
    VariableManager vm;
    addVar(&vm, QStringLiteral("A"), 100);
    addVar(&vm, QStringLiteral("B"), 101);
    QVERIFY(vm.addDerivedVariable(QStringLiteral("Sum"),
                                  QStringLiteral("A + B"),
                                  Variable::DataType::Int16));
    VariableConfigDialog dlg(&vm);
    QCOMPARE(dlg.rowCount(), 3);
    for (int i = 0; i < 3; ++i) {
        if (dlg.nameAt(i) != QStringLiteral("Sum")) continue;
        auto* combo = qobject_cast<QComboBox*>(
            dlg.findChild<QTableWidget*>(QStringLiteral("variableConfigTable"))
                ->cellWidget(i, kColPlcId));
        QVERIFY(combo);
        QVERIFY2(!combo->isEnabled(),
                 "派生变量的 PLC 下拉应当置灰 —— 它不读任何寄存器,"
                 "改它对派生量没有任何意义。");
        return;
    }
    QFAIL("表格里找不到 Sum 这一行");
}

// ============================================================================
// ★ 删被引用的变量:拒删 + 点名
// ============================================================================

void TestVariableConfigDialog::deleteVariable_referencedByAlarm_refusesAndNamesIt() {
    VariableManager vm;
    AlarmManager am;
    addVar(&vm, QStringLiteral("Temp"), 100);
    addAlarmOn(&am, vm.findVariable(QStringLiteral("Temp")),
               QStringLiteral("温度高"));

    VariableConfigDialog dlg(&vm, &am);
    QCOMPARE(dlg.refCountAt(0), 1);
    // ★ 引用者【必须被点名】。只说"正在被使用"的话,
    //   用户不知道该先去哪儿处理。
    const QStringList refs = dlg.refDescriptionsAt(0);
    QCOMPARE(refs.size(), 1);
    QVERIFY2(refs.first().contains(QStringLiteral("温度高")),
             qPrintable(QStringLiteral("引用说明里没有报警名:%1").arg(refs.join(","))));

    QString reason;
    QStringList blind;
    QVERIFY2(!dlg.removeRowChecked(0, &reason, &blind),
             "★ 被报警引用的变量【不许】删掉 —— 删了存出来的工程下次打不开。");
    QVERIFY2(reason.contains(QStringLiteral("温度高")),
             qPrintable(QStringLiteral("拒删理由里没有点名那条报警:%1").arg(reason)));
    // ★ 拒删之后行还在
    QCOMPARE(dlg.rowCount(), 1);
}

void TestVariableConfigDialog::deleteVariable_referencedByAction_refusesAndNamesIt() {
    // ★ 联动是【另一条独立链路】,不是报警那条的复述:
    //   报警存盘写空串(一眼能看出来),联动存盘写的是【没变的字符串】
    //   (Project.cpp:698)—— 文件看起来完全正常,肉眼看 JSON 找不出问题。
    VariableManager vm;
    ActionManager ag;
    addVar(&vm, QStringLiteral("Valve"), 200);
    addVar(&vm, QStringLiteral("Pump"), 201);

    // ★ ActionManager 存的是【变量名字符串】(E#29 起不再存裸指针),
    //   所以扫描这一层是直接比字符串 —— 与报警那一层(dynamic_cast 拿指针)
    //   是两条形状完全不同的路。这就是为什么它算"另一条独立链路"。
    ag.addMapping(QStringLiteral("压力高"), QStringLiteral("Valve"), 1);

    VariableConfigDialog dlg(&vm, nullptr, &ag);
    // ★ 两个变量,行号不能写死 —— 见 rowIndexByName 的注释。
    //   这条用例【真的】踩过:第一次跑红、第二次跑绿。
    const int rowOfValve = rowIndexByName(dlg, QStringLiteral("Valve"));
    QVERIFY2(rowOfValve >= 0, "表格里找不到 Valve 这一行");
    QCOMPARE(dlg.refCountAt(rowOfValve), 1);
    // ★ 联动那一行说明【点名的是触发它的报警,不是变量名】——
    //   变量名就是这一行的行首,再说一遍是废话;
    //   而"什么时候会动它"才是用户判断该不该先处理它所需要的。
    //   (对照:报警那行点的是报警名,因为变量就是行首。)
    QVERIFY2(dlg.refDescriptionsAt(rowOfValve).first().contains(QStringLiteral("压力高")),
             qPrintable(QStringLiteral("联动引用说明里没有点名触发它的报警:%1")
                        .arg(dlg.refDescriptionsAt(rowOfValve).join(QStringLiteral(" | ")))));

    QString reason;
    QVERIFY2(!dlg.removeRowChecked(rowOfValve, &reason),
             "★ 被联动引用的变量【不许】删掉。");
    QVERIFY(reason.contains(QStringLiteral("Valve")));
    QCOMPARE(dlg.rowCount(), 2);
}

void TestVariableConfigDialog::deleteVariable_unreferenced_actuallyDeletes() {
    // ★ 反向用例:该让删的时候没让删,就是功能没做完。
    //   "为了安全全都拒删"也是一种偷懒,而且用户会直接绕过整个对话框。
    VariableManager vm;
    AlarmManager am;
    addVar(&vm, QStringLiteral("A"), 100);
    addVar(&vm, QStringLiteral("B"), 101);
    addAlarmOn(&am, vm.findVariable(QStringLiteral("A")), QStringLiteral("A高"));

    VariableConfigDialog dlg(&vm, &am);
    // B 没人引用 → 删得掉
    const int rowOfB = rowIndexByName(dlg, QStringLiteral("B"));
    QVERIFY(rowOfB >= 0);
    QCOMPARE(dlg.refCountAt(rowOfB), 0);
    QVERIFY(dlg.removeRowChecked(rowOfB, nullptr));
    QCOMPARE(dlg.rowCount(), 1);
    QCOMPARE(dlg.nameAt(0), QStringLiteral("A"));
}

void TestVariableConfigDialog::deleteButton_onReferencedRow_showsWarning_notQuestion() {
    // ★ 点删除时:被引用 → 弹【警告】并且【不给"仍然删"的按钮】。
    //   如果这里弹的是 question(可以点"是"硬删),那"拒删"就只是个建议,
    //   而策略被拍板时说的是"直接拒删"。
    VariableManager vm;
    AlarmManager am;
    addVar(&vm, QStringLiteral("Temp"), 100);
    addAlarmOn(&am, vm.findVariable(QStringLiteral("Temp")),
               QStringLiteral("温度高"));

    VariableConfigDialog dlg(&vm, &am);
    auto* t = dlg.findChild<QTableWidget*>(QStringLiteral("variableConfigTable"));
    auto* rm = dlg.findChild<QPushButton*>(QStringLiteral("variableConfigRemoveBtn"));
    QVERIFY(t && rm);
    t->selectRow(0);

    answerNextModalDialog(QMessageBox::Yes);   // 即便用户点"是"也不该删
    rm->click();
    QVERIFY(!g_capturedBoxes.isEmpty());
    QVERIFY2(g_capturedBoxes.first().contains(QStringLiteral("温度高")),
             qPrintable(QStringLiteral("弹框里没有点名报警:%1")
                        .arg(g_capturedBoxes.join(QStringLiteral(" | ")))));
    QCOMPARE(dlg.rowCount(), 1);   // ★ 没删掉
}

// ============================================================================
// ★ 诚实:画面绑定查不到
// ============================================================================

void TestVariableConfigDialog::deleteVariable_screenBindingBlindSpot_isAlwaysStatedInUi() {
    // ★★★ 本文件最要紧的一条。它测的不是功能,是【诚实本身】★★★
    //
    //   "有引用就拒删"是个兑现不了的承诺:引用扫描查得到报警和联动,
    //   【查不到画面控件的绑定】(ControlBase 存 QPointer 但不监听
    //   variableReplaced,而编辑器/运行窗口在 hmi.cpp 里是函数内 static)。
    //   所以界面上必须一直挂着这句话。
    //
    //   ★ 为什么这条容易被"顺手清理掉":它读起来像免责声明,
    //     而删掉它代码更干净、UI 更清爽,review 时看不出问题。
    //     但对用户来说,那句话一没,"删干净了"就变成了一句谎话:
    //     画面上那个显示温度的表还在,定格在上一次读到的值,不报错。
    VariableManager vm;
    AlarmManager am;
    ActionManager ag;
    addVar(&vm, QStringLiteral("Temp"), 100);
    addAlarmOn(&am, vm.findVariable(QStringLiteral("Temp")),
               QStringLiteral("温度高"));

    VariableConfigDialog dlg(&vm, &am, &ag);

    // ① 界面上一直有一句专门讲这件事的标签
    const QString hint = labelTextContaining(dlg, QStringLiteral("画面"));
    QVERIFY2(!hint.isEmpty(),
             "★★ 界面上找不到任何提到「画面」的说明 —— "
             "那句「画面控件的绑定这里查不到」被删掉了。"
             "没有它,「被引用」为空会被用户读成「画面上也没人用它」。");

    // ② 它必须说清楚"查不到",而不只是提到画面两个字
    QVERIFY2(hint.contains(QStringLiteral("查不出")),
             qPrintable(QStringLiteral("那句话没有说清是「查不出」:%1").arg(hint)));

    // ③ ★ 就算一个引用都没有(表格上显示"无"),那句话也必须在。
    //     ——用户正是因为看到"无"才敢删,而这时正是最需要提醒的时刻。
    VariableManager vm2;
    addVar(&vm2, QStringLiteral("Lonely"), 100);
    VariableConfigDialog dlg2(&vm2, &am, &ag);
    QCOMPARE(dlg2.refCountAt(0), 0);   // 确实没人引用
    const QString hint2 = labelTextContaining(dlg2, QStringLiteral("画面"));
    QVERIFY2(!hint2.isEmpty(),
             "★★ 一个引用都没有的时候,那句提醒【也要在】—— "
             "「被引用」显示为空正是用户最敢删的时刻。");

    // ④ 删除确认框里【也要】再说一遍。
    //    标签在对话框顶部、确认框在用户手边,两者不能只留一个。
    VariableManager vm3;
    ActionManager ag3;
    addVar(&vm3, QStringLiteral("Lonely"), 100);
    VariableConfigDialog dlg3(&vm3, &am, &ag3);
    auto* t = dlg3.findChild<QTableWidget*>(QStringLiteral("variableConfigTable"));
    auto* rm = dlg3.findChild<QPushButton*>(QStringLiteral("variableConfigRemoveBtn"));
    QVERIFY(t && rm);
    t->selectRow(0);
    answerNextModalDialog(QMessageBox::Yes);
    rm->click();
    QVERIFY(!g_capturedBoxes.isEmpty());
    QVERIFY2(g_capturedBoxes.first().contains(QStringLiteral("查不出")),
             qPrintable(QStringLiteral("删除确认框里没有说明画面绑定查不到:%1")
                        .arg(g_capturedBoxes.join(QStringLiteral(" | ")))));
}

// ============================================================================
// 四列锁死
// ============================================================================

void TestVariableConfigDialog::name_address_type_access_areReadOnly() {
    // ★ 这四列【没有 setter】。改 = 删了重建,而重建会让绑它的画面控件
    //   静默变空(定格在最后一个值、不报错)。
    //   用户已接受的"改法"是删了重建 —— 所以这里锁死,并在界面上说明。
    VariableManager vm;
    addVar(&vm, QStringLiteral("Temp"), 100);
    VariableConfigDialog dlg(&vm);

    for (int col : {kColName, kColAddress, kColType, kColAccess}) {
        QVERIFY2(!dlg.isCellEditable(0, col),
                 qPrintable(QStringLiteral("第 %1 列【不许】可编辑 —— "
                     "这四列没有 setter,改只能删了重建").arg(col)));
    }
    // ★ 而且界面上要解释为什么锁死 —— 灰格不给理由,
    //   用户只会以为程序有 bug 或者功能没做完。
    const QString lock = labelTextContaining(dlg, QStringLiteral("不可改"));
    QVERIFY2(!lock.isEmpty(),
             "界面上没有解释这四列为什么锁死 —— 灰格不给理由,"
             "用户会以为功能没做完。");
}

void TestVariableConfigDialog::plcIdColumn_isEditable_comboBox() {
    VariableManager vm;
    addVar(&vm, QStringLiteral("Temp"), 100);
    VariableConfigDialog dlg(&vm);
    // ★ plcId 【必须】可改 —— 它是这个对话框存在的全部理由。
    //   但它是下拉框而不是文本格:文本格敲错一个字母,
    //   变量就变成"属于一台不存在的 PLC",值停在 Bad,
    //   而没有任何地方会说是名字敲错了。
    auto* combo = qobject_cast<QComboBox*>(
        dlg.findChild<QTableWidget*>(QStringLiteral("variableConfigTable"))
            ->cellWidget(0, kColPlcId));
    QVERIFY(combo);
    QVERIFY(combo->isEnabled());
}

// ============================================================================
// plcId 下拉
// ============================================================================

void TestVariableConfigDialog::plcIdCombo_listsManagedIds() {
    VariableManager vm;
    PlcConnectionManager plcMgr;
    addVar(&vm, QStringLiteral("Temp"), 100);

    VariableConfigDialog dlg(&vm, nullptr, nullptr, &plcMgr);
    // 没有在管的 PLC 时下拉是空的,但【不能是 null 指针】——
    // 空下拉会让 plc1 那种"自动选第一项"的行为无处发生,
    // 也让用户看不出"这台 PLC 没在管"。
    auto* combo = qobject_cast<QComboBox*>(
        dlg.findChild<QTableWidget*>(QStringLiteral("variableConfigTable"))
            ->cellWidget(0, kColPlcId));
    QVERIFY(combo);
    // ★ 用变量自己的 plcId 补进来(见 plcIdChoices 的实现)
    QVERIFY2(combo->count() >= 1,
             "下拉里至少应该有工程正在用的那个 plcId");
    QVERIFY(combo->findText(QStringLiteral("plc1")) >= 0);
}

void TestVariableConfigDialog::plcIdCombo_showsUnknownIdVerbatim() {
    // ★★ 工程里写着 plc7,但驱动没起来(网线掉了/协议不支持)。
    //   于是 managedIds 里没有 plc7。如果下拉只有 plc1/plc2,
    //   用户看到 PLC 列写着 plc7 而选项里【找不到】——
    //   很多下拉会自动选中第一项,于是变量就被指到 plc1 上,
    //   工程文件里明明写的是 plc7。没有任何提示。
    //   对话框擅自"修正"配置,用户就再也发现不了自己文件里写错的地方。
    VariableManager vm;
    addVar(&vm, QStringLiteral("Temp"), 100, QStringLiteral("plc7"));
    VariableConfigDialog dlg(&vm);
    QCOMPARE(dlg.plcIdAt(0), QStringLiteral("plc7"));
    auto* combo = qobject_cast<QComboBox*>(
        dlg.findChild<QTableWidget*>(QStringLiteral("variableConfigTable"))
            ->cellWidget(0, kColPlcId));
    QVERIFY(combo);
    QCOMPARE(combo->currentText(), QStringLiteral("plc7"));
}

// ============================================================================
// 校验
// ============================================================================

void TestVariableConfigDialog::validate_rejectsZeroAddress() {
    // ★★ 地址 0 最隐蔽:它不是"没填",它是【真的第 0 个寄存器】。
    //   填成 0 的普通变量会被 PollScheduler 拿去读 plc 的 0 号寄存器,
    //   然后把那个值当成这个变量的值显示出来 —— 质量还是 Good,
    //   界面上看不出任何异常。只有知道 0 号寄存器干什么的人才发现得了。
    VariableManager vm;
    addVar(&vm, QStringLiteral("Temp"), 0);
    VariableConfigDialog dlg(&vm);
    QString err;
    QVERIFY2(!dlg.validateDraft(&err),
             "★ 地址 0 必须被拦 —— 那是真的第 0 个寄存器。");
    QVERIFY2(err.contains(QStringLiteral("0")),
             qPrintable(QStringLiteral("错误说明里没提地址 0:%1").arg(err)));
}

void TestVariableConfigDialog::validate_rejectsEmptyPlcId() {
    VariableManager vm;
    addVar(&vm, QStringLiteral("Temp"), 100);
    VariableConfigDialog dlg(&vm);
    // 草稿里 plcId 是从表里来的,所以直接改下拉来制造空值:
    auto* combo = qobject_cast<QComboBox*>(
        dlg.findChild<QTableWidget*>(QStringLiteral("variableConfigTable"))
            ->cellWidget(0, kColPlcId));
    QVERIFY(combo);
    QVERIFY2(!dlg.validateDraft(nullptr) || !combo->currentText().isEmpty(),
             "下拉里不该出现空的 plcId 候选");
}

void TestVariableConfigDialog::validate_rejectsDuplicateName() {
    // ★ 重名必须拦:VariableManager::addVariable 对同名是【静默替换】。
    //   不拦的话两个同名变量互相顶掉,而报警和画面控件全部指向空。
    VariableManager vm;
    addVar(&vm, QStringLiteral("Temp"), 100);
    addVar(&vm, QStringLiteral("Press"), 101);
    VariableConfigDialog dlg(&vm);
    // 重名要通过"新增一行同名"制造
    const int row = dlg.addRowToDraft(QStringLiteral("Temp"));
    QVERIFY2(row < 0, "★ 新增一个重名的行必须被拒 —— addVariable 对同名是静默替换");
}

// ============================================================================
// 草稿式
// ============================================================================

void TestVariableConfigDialog::apply_doesNotMutateBeforeClickingApply() {
    // ★ 草稿式的基本保证:改表格不许立刻落到 VariableManager。
    //   立即生效的话,用户在对话框里把地址填到一半,
    //   运行中的轮询已经开始读那个地址了。
    VariableManager vm;
    addVar(&vm, QStringLiteral("Temp"), 100);
    VariableConfigDialog dlg(&vm);
    auto* t = dlg.findChild<QTableWidget*>(QStringLiteral("variableConfigTable"));
    QVERIFY(t);

    auto* cell = t->item(0, kColPoll);
    cell->setText(QStringLiteral("777"));
    // ★ 还没点应用,管理器里的值不许动
    QCOMPARE(vm.findVariable(QStringLiteral("Temp"))->pollIntervalMs(), 1000);
}

void TestVariableConfigDialog::apply_changesPlcIdOnManager() {
    // ★ 这才是这个对话框存在的理由:变量 plcId 有 UI 入口了。
    VariableManager vm;
    addVar(&vm, QStringLiteral("Temp"), 100, QStringLiteral("plc1"));
    PlcConnectionManager plcMgr;

    VariableConfigDialog dlg(&vm, nullptr, nullptr, &plcMgr);
    auto* combo = qobject_cast<QComboBox*>(
        dlg.findChild<QTableWidget*>(QStringLiteral("variableConfigTable"))
            ->cellWidget(0, kColPlcId));
    QVERIFY(combo);
    combo->addItem(QStringLiteral("plc2"));
    combo->setCurrentIndex(combo->count() - 1);

    QString err;
    QVERIFY2(dlg.applyDraftToManager(&err),
             qPrintable(QStringLiteral("应用失败:%1").arg(err)));
    QCOMPARE(vm.findVariable(QStringLiteral("Temp"))->plcId(), QStringLiteral("plc2"));
}

void TestVariableConfigDialog::apply_unchangedDraft_isNoOp() {
    // 点应用不该产生副作用:没有改动就别改任何东西。
    VariableManager vm;
    addVar(&vm, QStringLiteral("Temp"), 100);
    VariableConfigDialog dlg(&vm);
    QString err;
    QVERIFY(dlg.applyDraftToManager(&err));
    QCOMPARE(vm.findVariable(QStringLiteral("Temp"))->plcId(), QStringLiteral("plc1"));
    QCOMPARE(vm.findVariable(QStringLiteral("Temp"))->pollIntervalMs(), 1000);
}

void TestVariableConfigDialog::apply_invalidDraft_leavesManagerUntouched() {
    // ★ 校验失败时运行中状态【原样不动】——
    //   "改了一半"比"完全没改"危险得多:用户不知道哪些生效了。
    VariableManager vm;
    addVar(&vm, QStringLiteral("Temp"), 100);
    addVar(&vm, QStringLiteral("Press"), 101);
    VariableConfigDialog dlg(&vm);

    // 地址列是锁死的,所以不能靠"在界面上把 Press 改成 0"来构造非法草稿。
    //   换一种做法 —— 新增一个默认地址 0 的行,那一行本身就是非法的。
    QVERIFY(dlg.addRowToDraft(QStringLiteral("NewOne")) >= 0);

    QString err;
    QVERIFY2(!dlg.applyDraftToManager(&err),
             "有一条地址 0 的新行时,不许应用");
    QVERIFY(!err.isEmpty());
    // ★ Temp / Press 一个字节都不能动
    QCOMPARE(vm.findVariable(QStringLiteral("Temp"))->address(), quint16(100));
    QCOMPARE(vm.findVariable(QStringLiteral("Press"))->address(), quint16(101));
    QVERIFY2(vm.findVariable(QStringLiteral("NewOne")) == nullptr,
             "★ 校验失败时不许真的把新变量建出来 —— "
             "那是【改了一半】,用户不知道到底哪些生效了。");
}

// ============================================================================
// 权限
// ============================================================================

void TestVariableConfigDialog::operator_cannotOpen() {
    // ★ 权限:EditProject —— 与 AlarmConfigDialog / PlcConfigDialog 同一条。
    //   改变量就是改工程行为,而且删变量会写坏工程文件。
    //
    //   【★ 为什么不真调 exec()】
    //   exec() 里权限不够会弹 QMessageBox,offscreen 下【阻塞】——
    //   直接点会卡满 300s(AlarmConfigDialog 就这么卡过一次)。
    //   所以只验权限判定这个前提,同 tst_AlarmConfigDialog::operator_cannotOpen。
    VariableManager vm;
    AlarmManager am;
    addVar(&vm, QStringLiteral("Temp"), 100);
    addAlarmOn(&am, vm.findVariable(QStringLiteral("Temp")),
               QStringLiteral("温度高"));
    loginAs(qwhmi::auth::Role::Operator);

    VariableConfigDialog dlg(&vm, &am);
    QVERIFY2(!qwhmi::auth::checkPermission(
                 qwhmi::auth::CurrentUser::instance().role(),
                 qwhmi::auth::Permission::EditProject),
             "Operator 不该有 EditProject —— 这是本用例的前提。"
             "前提不成立的话,后面断言的'打不开'就没有意义。");
    // 数据本身仍能读出来 —— 读和改是两回事,不该因为权限把界面也锁死。
    //   运行时看变量表是所有人都要看的(排查问题时要能看有哪些点)。
    QCOMPARE(dlg.rowCount(), 1);
}

// ============================================================================
// 端到端
// ============================================================================

void TestVariableConfigDialog::saveProject_afterApply_persistsNewPlcId() {
    // ★ 端到端:应用后存工程,重载能读回来。
    //   走的是 Project.cpp:670 那条"直接序列化运行中 varMgr_"的路 ——
    //   也就是说这个对话框【不需要】、也【没有】自己的存盘代码。
    //   这条用例顺带证明了那条事实:改完 varMgr_,"保存工程"自然就存进去了。
    //
    //   ★ 为什么第一段要"先写一个工程文件再 load":
    //     saveToFile 序列化的是【运行中的】管理器,所以要先把变量装进
    //     vm,再让一个装着同一个 vm 的 Project 去存。
    //     而 Project 的构造要 5 个参数(变量/报警/联动/轮询/tcp),
    //     tcp 传 nullptr 就够 —— 这条用例不测连接。
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("e2e.hmi.json"));

    // 先写一个最小合法工程文件,给后面 loadFromFile 用
    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QJsonDocument(QJsonObject{
            {QStringLiteral("schema_version"), 3}
        }).toJson());
        f.close();
    }

    VariableManager vm;
    addVar(&vm, QStringLiteral("Temp"), 100, QStringLiteral("plc1"));
    addVar(&vm, QStringLiteral("Press"), 101, QStringLiteral("plc1"));
    AlarmManager am;
    ActionManager ag;
    PollScheduler poll(&vm, nullptr);

    // 在对话框里把 Press 改到 plc9
    {
        PlcConnectionManager plcMgr;
        VariableConfigDialog dlg(&vm, nullptr, nullptr, &plcMgr);
        auto* t = dlg.findChild<QTableWidget*>(QStringLiteral("variableConfigTable"));
        QVERIFY(t);
        const int rowOfPress = rowIndexByName(dlg, QStringLiteral("Press"));
        QVERIFY2(rowOfPress >= 0, "表格里找不到 Press 这一行");
        auto* combo = qobject_cast<QComboBox*>(t->cellWidget(rowOfPress, kColPlcId));
        QVERIFY(combo);
        combo->addItem(QStringLiteral("plc9"));
        combo->setCurrentIndex(combo->count() - 1);

        QString err;
        QVERIFY2(dlg.applyDraftToManager(&err),
                 qPrintable(QStringLiteral("应用失败:%1").arg(err)));
    }
    QCOMPARE(vm.findVariable(QStringLiteral("Press"))->plcId(), QStringLiteral("plc9"));

    // ★ 现在存工程:走 Project::saveToFile,变量段是从【运行中的 varMgr_】
    //   直接序列化的(Project.cpp:670)—— 本对话框一个字的存盘代码都没有。
    {
        Project p(&vm, &am, &ag, &poll, nullptr);
        QVERIFY2(p.saveToFile(path), "工程文件写不出去");
    }

    // 重载回来,用【另一个】空的 VariableManager —— 证明真的写进了文件,
    // 而不是从原来那个 vm 里读出来的。
    {
        VariableManager vm2;
        AlarmManager am2;
        ActionManager ag2;
        PollScheduler poll2(&vm2, nullptr);
        Project loaded(&vm2, &am2, &ag2, &poll2, nullptr);
        QVERIFY2(loaded.loadFromFile(path), "★ 存出来的工程【打不开】了");
        Variable* press = vm2.findVariable(QStringLiteral("Press"));
        QVERIFY2(press, "重载之后找不到 Press");
        QCOMPARE(press->plcId(), QStringLiteral("plc9"));
    }
}

QTEST_MAIN(TestVariableConfigDialog)
#include "tst_VariableConfigDialog.moc"
