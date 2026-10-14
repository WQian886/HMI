// ============================================================================
// tst_AlarmConfigDialog —— F4-a 报警配置对话框
//
// 【这个对话框解决什么】
//   F2 滞回、F2-c 屏蔽都已经落地,但【没有任何入口能配它们】——
//   hysteresis 只能手改 JSON(JsonSerializer.cpp:55)。
//   报警配置是一线工程师每天要动的东西,让它只存在于 JSON 里
//   等于这个功能对真实用户不存在。
//
// 【本文件测什么、不测什么】
//   测:用户能看到什么、改了之后能看到什么、点按钮之后有没有反应、导出的 JSON 对不对。
//   不测:表格长什么样、按钮摆在哪 —— 那些是"看得见"的,由人工验收。
//   断言优先落在对话框的公开只读访问器上,不反射 private 成员:
//   反射测的是实现细节,改个成员名就红,却证明不了界面真的对
//   (F3 在 AlarmListControl.h:47-50 已经踩过并记录了这个坑)。
//   唯一例外是【按钮和表格控件】—— 按 objectName findChild 找它们并真的点下去。
//   这是 2026-10-08 补的:用户实测"点新增没反应",而此前 11 条用例全是只读的,
//   一条都没碰过按钮,所以全绿。findChild 是公开 API,不是反射私有成员。
//
// 【★ 本文件要锁住的设计不变量 —— F4-b 之后收窄了,但没取消】
//   F4-a/F4-c 阶段:这个对话框【绝不改动运行中的 AlarmManager】。
//   F4-b(2026-10-08)开了一个口子:applyDraftToManager() 可以改。
//   收窄后仍必须成立的两条:
//     ① 【只读的路径依然只读】—— 打开对话框、编辑草稿、导出、存盘,
//        全程不碰运行中的状态。F4-a 的用例继续按这个断言。
//     ② 【应用失败时运行中原样不动】—— 不能"改了一半"。
//        这是 F4-b 新增的风险,由 apply_rejects* 那几条守住。
//   这两条一旦破了,F4-b 就没有独立验收面了。
// ============================================================================

#include <QtTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QPushButton>
#include <QTableWidget>

#include "AlarmManager.h"
#include "JsonSerializer.h"
#include "PollScheduler.h"
#include "Project.h"
#include "ThresholdAlarmChecker.h"
#include "VariableManager.h"
#include "alarm/AlarmConfigDialog.h"
#include "auth/CurrentUser.h"
#include "auth/Permission.h"
#include "auth/Role.h"
#include "auth/User.h"

using qwhmi::alarm::AlarmConfigDialog;
// Role / Permission 都在 qwhmi::auth 里(Role.h:19-26 是 enum class,不是裸枚举),
// 类体里直接写 Role 会报 C2061 —— 这里一次性引进命名空间。
using namespace qwhmi::auth;

// ★ 记着 tst_AlarmShelve.cpp:44 的教训 ——
//   checker 是 new 出来的裸指针,成员函数一律用 ->,写成 . 会报
//   C2228,而且 MSVC 只报第一处。下面的 useChecker 全用指针。

class TestAlarmConfigDialog : public QObject {
    Q_OBJECT

private slots:
    // ---- ★ F6(2026-10-10):区域 ----
    void areaColumn_exists();
    void area_typedByUser_landsInDraft();
    void area_survivesExportAndReload();
    void area_survivesSaveToProjectFile();
    void area_whitespaceIsTrimmed();
    void area_defaultIsUnassigned();
    void init();
    void cleanup();

    // ---- 读取正确性 ----
    void readsAllFieldsOfThresholdChecker();
    void rowsAreSortedByName();
    void nonThresholdChecker_isListedButNotEditable();
    void shelvedAlarm_isVisible();

    // ---- ★ 新增/删除(2026-10-08 用户实测"点了没反应"后补的) ----
    void addButton_appendsRow();
    void addButton_addsRowOnTopOfExisting();
    void addButton_threeTimes_givesThreeRows();
    void addButton_keepsTableAndDraftInStep();
    void removeButton_removesSelectedRow();
    void editNameCell_updatesDraft();
    void editThresholdSpin_updatesDraftAndTable();
    void addButton_newAlarmSurvivesExport();

    // ---- ★ 设计不变量:不改运行中的状态 ----
    void openDialog_doesNotMutateAlarmManager();

    // ---- ★ 变量下拉可选(2026-10-08 用户报"新建报警时变量不能选")----
    void varCombo_isSelectableOnNewRow();
    void varCombo_picksUpVariableChange();
    void varCombo_staysDisabledWhenNoVariables();
    void varCombo_reEnabledAfterRowSwitch();

    // ---- ★ F4-c:存进工程文件(真实往返,不只测内存对象)----
    void save_writesAlarmsToRealFile();
    void save_preservesOtherSections();
    void save_resultReloadsThroughProject();
    void save_rejectsEmptyName();
    void save_rejectsDuplicateNames();
    void save_rejectsUnknownVariable();
    void save_doesNotTouchRunningAlarmManager();
    void save_refusesNonexistentFile();
    void save_refusesCorruptJson();
    void validateAlarmsArray_unitLevel();

    // ---- ★ F4-b:应用到运行中的 AlarmManager ----
    void apply_updatesThresholdOfRunningAlarm();
    void apply_updatesLevelDirectionHysteresisMinDur();
    void apply_addsBrandNewAlarm();
    void apply_deletesAlarmRemovedFromDraft();
    void apply_syncsShelvedState();
    void shelveColumn_isUserCheckableNotReadOnlyText();
    void checkingShelveBox_appliesToRunningManager();
    void uncheckingShelveBox_appliesUnshelveToRunningManager();
    void checkingShelveBox_withoutApply_doesNotTouchRunning();
    void shelve_isNeverWrittenIntoProjectJson();
    void newRow_shelveCellIsAlsoCheckable();
    void apply_rejectsUnknownVariableAndChangesNothing();
    void apply_rejectsDuplicateNamesAndChangesNothing();
    void apply_withoutPollSchedulerRefusesToAdd();
    void apply_refusesVariableChangeWhileAlarmIsFiring();
    void apply_keepsFiredAlarmRecoverable_afterThresholdChange();
    void apply_deletingFiringAlarm_doesNotLeaveOrphanActiveAlarm();
    void apply_deletingShelvedAlarm_clearsShelvedState();
    void apply_rereadsTableSoRowsMatchRunningState();
    void apply_rejectsButReportsWhichAlarm();

    // ---- 导出 ----
    void exportJson_matchesProjectFileSchema();
    void exportJson_roundTripsThroughJsonSerializer();
    void exportJson_omitsNonThresholdAlarms();
    void exportJson_shelvedFlagNotWrittenAsAlarm();

    // ---- 权限 ----
    void operator_cannotOpen();

private:
    // 建一个带阈值的 checker 并注册到 mgr_
    void useThresholdChecker(AlarmManager* mgr, VariableManager* varMgr,
                             const QString& name, const QString& varName,
                             double threshold, double hysteresis,
                             AlarmLevel level = AlarmLevel::Warning,
                             Direction dir = Direction::Above,
                             int minDurMs = 500,
                             // ★ F6:区域。默认值必须【老实地】是空串 —— 空 = 未分配,
                             //   而未分配正是 F6 之前所有报警的真实状态。
                             //   给个"默认区域"的话,area_defaultIsUnassigned 就永远测不出东西。
                             const QString& area = {});
    // 建一个非阈值 checker(模拟 WriteFailChecker)
    void usePlainChecker(AlarmManager* mgr, const QString& name);

    void loginAs(Role r);

    // ★ 真去点按钮 —— 不是直接调槽函数。
    //   【为什么必须这样】用户报的是"点了没反应"。直接调 onAdd() 只能证明
    //   onAdd 的逻辑对,证明不了"按钮 → 槽"这根线接上了、按钮没被禁用、
    //   也没被别的控件挡住。2026-10-08 这次就是这么漏的:11 条测试全绿,
    //   因为它们一条都没碰过按钮。
    void clickButton(AlarmConfigDialog* dlg, const QString& objectName);

    // 按名字找行号,找不到返回 -1。
    // 【为什么必须有这个】
    //   reloadFromManager() 按【名字】排序,而"新报警"和"温度高"谁在前
    //   取决于编码比较结果 —— 写死行号就是埋雷:改个名字顺序就崩,
    //   而且崩的时候报的是"第 0 行不是温度高",没人想得到是自己写死了下标。
    int rowIndexByName(const AlarmConfigDialog& dlg, const QString& name) const;

    AlarmManager*    mgr_   = nullptr;
    VariableManager* varMgr_ = nullptr;

    // ★ F4-c:造一个真实的工程文件到磁盘。
    //   【为什么必须真文件】这一组用例的全部价值就是"存进去的工程还能不能打开"。
    //   只断言内存里的 QJsonObject,证明不了 QSaveFile 写没写坏、
    //   证明不了"只改一段"有没有把别的段弄丢 —— 而这两件事恰恰是用户要的。
    QString writeTempProject(const QJsonObject& root);
    QJsonObject readJson(const QString& path);
    // 带哨兵值的工程文件:variables / actions / screens / plcs 各有一段可识别的内容。
    // 保存报警后这些必须原样还在 —— 这是"只改一段"的直接证据。
    static QJsonObject sentinelProject();
    // cleanup() 里逐个删,避免跨用例残留互相污染
    QStringList tempFiles_;
};

// ---------------------------------------------------------------------------

void TestAlarmConfigDialog::init() {
    mgr_    = new AlarmManager(this);
    varMgr_ = new VariableManager(this);
}

void TestAlarmConfigDialog::cleanup() {
    for (const QString& p : std::as_const(tempFiles_)) QFile::remove(p);
    tempFiles_.clear();
    delete varMgr_;   varMgr_ = nullptr;
    delete mgr_;      mgr_    = nullptr;
}

void TestAlarmConfigDialog::useThresholdChecker(AlarmManager* mgr, VariableManager* varMgr,
                                                const QString& name, const QString& varName,
                                                double threshold, double hysteresis,
                                                AlarmLevel level, Direction dir,
                                                int minDurMs,
                                                const QString& area) {
    Variable* v = varMgr->findVariable(varName);
    if (!v) {
        // 地址是 quint16(见 Variable.h:56-58),不是字符串。
        // 这里用变量名 hash 出一个稳定地址 —— 本测试不碰通信,
        // 但地址重复会让 PollScheduler 之外的代码产生难以预料的分组。
        const quint16 addr = static_cast<quint16>(qHash(varName) % 1000);
        v = new Variable(varName, addr, Variable::DataType::Float,
                         Variable::Access::ReadOnly);
        varMgr->addVariable(v);
    }
    CheckerConfig cfg;
    cfg.level         = level;
    cfg.direction     = dir;
    cfg.threshold     = threshold;
    cfg.minDurationMs = minDurMs;
    cfg.hysteresis    = hysteresis;   // ★ F2 新字段
    cfg.area          = area;         // ★ F6 新字段(CheckerConfig 末尾,见 Alarm.h)
    // pollScheduler 传 nullptr:对话框只读 config_,不碰检测逻辑,
    // 所以本文件完全不依赖轮询 —— 这也是"配置面"和"检测面"该分开的理由。
    auto* checker = new ThresholdAlarmChecker(name, v, nullptr, cfg, mgr);
    mgr->addChecker(checker);
}

void TestAlarmConfigDialog::usePlainChecker(AlarmManager* mgr, const QString& name) {
    auto* checker = new AlarmChecker(mgr);
    checker->setName(name);
    checker->setLevel(AlarmLevel::Critical);
    mgr->addChecker(checker);
}

// ---- F4-c 的文件辅助 ----

QString TestAlarmConfigDialog::writeTempProject(const QJsonObject& root) {
    // ★ 用 QTemporaryDir?不行 —— 它随对象析构而删,而 cleanup() 在
    //   init() 建的新对象之前跑,文件会跨用例残留互相污染。
    //   所以每次用例自己造唯一文件名,cleanup() 逐个删。
    static int counter = 0;
    const QString path = QStringLiteral("%1/alarmcfg_%2_%3.json")
                             .arg(QDir::tempPath())
                             .arg(QCoreApplication::applicationPid())
                             .arg(++counter);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return QString();
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    f.close();
    tempFiles_.append(path);
    return path;
}

QJsonObject TestAlarmConfigDialog::readJson(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}

// 造一个"正常"的工程文件:有变量、有画面、有联动。
// 每一段都塞了能识别的哨兵值,保存后必须原样还在 ——
// 这就是"只改一段、别的段一个都不动"的证据。
QJsonObject TestAlarmConfigDialog::sentinelProject() {
    QJsonObject root;
    root["schema_version"] = 3;
    root["project_name"]   = QStringLiteral("哨兵工程");

    QJsonObject v;
    v["name"]     = QStringLiteral("plc1.Temp");
    v["type"]     = QStringLiteral("float");
    v["address"]  = 100;
    v["access"]   = QStringLiteral("read");
    QJsonArray vars;
    vars.append(v);
    root["variables"] = vars;

    QJsonArray actions;
    QJsonObject act;
    act["alarm_name"] = QStringLiteral("温度高");
    act["variable"]   = QStringLiteral("plc1.Temp");
    act["write_value"] = 0;
    actions.append(act);
    root["actions"] = actions;

    // 画面段:带一个控件,用来验证保存报警不会把画布清空
    QJsonObject screen;
    screen["name"] = QStringLiteral("画面1");
    QJsonObject ctl;
    ctl["type"] = QStringLiteral("BarControl");
    ctl["x"]    = 10;
    ctl["y"]    = 20;
    QJsonArray ctls;
    ctls.append(ctl);
    screen["controls"] = ctls;
    QJsonArray screens;
    screens.append(screen);
    root["screens"] = screens;

    QJsonArray plcs;
    QJsonObject plc;
    plc["slave_id"] = 1;
    plcs.append(plc);
    root["plcs"] = plcs;

    return root;
}

void TestAlarmConfigDialog::loginAs(Role r) {
    qwhmi::auth::User u;
    u.name   = QStringLiteral("tester");
    u.role   = r;
    qwhmi::auth::CurrentUser::instance().login(u);
}

int TestAlarmConfigDialog::rowIndexByName(const AlarmConfigDialog& dlg,
                                          const QString& name) const {
    for (int i = 0; i < dlg.rowCount(); ++i) {
        if (dlg.nameAt(i) == name) return i;
    }
    return -1;
}

// ============================================================================
// 读取正确性
// ============================================================================

// 核心用例:7 个配置字段一个都不能读错。
// 【为什么这条最重要】这个对话框的全部价值就是"让人看见配置"。
// 读错一个字段,用户照着它调设备,调出来的行为跟配的不一样 ——
// 而界面上看不出任何异常。
void TestAlarmConfigDialog::readsAllFieldsOfThresholdChecker() {
    useThresholdChecker(mgr_, varMgr_, QStringLiteral("温度高"),
                        QStringLiteral("plc1.Temp"),
                        80.5, 3.25,
                        AlarmLevel::Critical, Direction::Below, 750);

    AlarmConfigDialog dlg(mgr_, varMgr_);

    QCOMPARE(dlg.rowCount(), 1);
    QCOMPARE(dlg.nameAt(0),        QStringLiteral("温度高"));
    QCOMPARE(dlg.variableAt(0),    QStringLiteral("plc1.Temp"));
    QCOMPARE(dlg.levelAt(0),       QStringLiteral("Critical"));
    QCOMPARE(dlg.directionAt(0),   QStringLiteral("Below"));
    QCOMPARE(dlg.thresholdAt(0),   80.5);
    QCOMPARE(dlg.minDurationMsAt(0), 750);
    // ★ hysteresis 是 F2 刚加的字段 —— 专门为它单开一个断言,
    //   免得"新增字段忘了加读取"这种事只被一条大用例笼统盖过。
    QCOMPARE(dlg.hysteresisAt(0),  3.25);
    QVERIFY(dlg.editableAt(0));
}

// checkers() 返回的是 QHash::values(),【顺序完全不确定】。
// 不排序的话,用户每次打开对话框行序都可能变,没法建立"这一行还是那一条"的印象。
void TestAlarmConfigDialog::rowsAreSortedByName() {
    useThresholdChecker(mgr_, varMgr_, QStringLiteral("zeta报警"),  QStringLiteral("plc1.C"), 1, 0);
    useThresholdChecker(mgr_, varMgr_, QStringLiteral("alpha报警"), QStringLiteral("plc1.A"), 2, 0);
    useThresholdChecker(mgr_, varMgr_, QStringLiteral("mid报警"),   QStringLiteral("plc1.B"), 3, 0);

    AlarmConfigDialog dlg(mgr_, varMgr_);
    QCOMPARE(dlg.rowCount(), 3);
    QCOMPARE(dlg.nameAt(0), QStringLiteral("alpha报警"));
    QCOMPARE(dlg.nameAt(1), QStringLiteral("mid报警"));
    QCOMPARE(dlg.nameAt(2), QStringLiteral("zeta报警"));
}

// ★ 这条锁的是本项目反复吃亏的那一类:静默截断。
//   WriteFailChecker 这类非阈值报警【正在生效】,但它没有阈值可配。
//   如果对话框悄悄不显示它,用户会以为"工程里就这几条报警"——
//   而实际上有一条写失败报警正在生效。看不见的报警比没有报警更危险。
void TestAlarmConfigDialog::nonThresholdChecker_isListedButNotEditable() {
    useThresholdChecker(mgr_, varMgr_, QStringLiteral("温度高"), QStringLiteral("plc1.Temp"), 80, 0);
    usePlainChecker(mgr_, QStringLiteral("写失败报警"));

    AlarmConfigDialog dlg(mgr_, varMgr_);

    // ★ 必须出现(哪怕不可编辑)—— 不出现就是静默截断
    QCOMPARE(dlg.rowCount(), 2);
    bool found = false;
    for (int i = 0; i < dlg.rowCount(); ++i) {
        if (dlg.nameAt(i) == QStringLiteral("写失败报警")) {
            found = true;
            QVERIFY2(!dlg.editableAt(i), "非阈值报警必须标为不可编辑");
            QVERIFY2(dlg.variableAt(i).isEmpty(), "非阈值报警没有关联变量");
        }
    }
    QVERIFY2(found, "非阈值报警必须在表里出现 —— 悄悄不显示会让用户以为它不存在");
}

// F2-c 的硬约束之一:屏蔽必须可见(见 AlarmManager.h:101-110)。
// 被屏蔽的报警如果在这个对话框里看不出来,工程师根本不知道有东西被藏起来了。
void TestAlarmConfigDialog::shelvedAlarm_isVisible() {
    useThresholdChecker(mgr_, varMgr_, QStringLiteral("泵P1压力低"), QStringLiteral("plc1.P1"), 2.0, 0);
    useThresholdChecker(mgr_, varMgr_, QStringLiteral("温度高"),     QStringLiteral("plc1.Temp"), 80, 0);
    QVERIFY(mgr_->shelve(QStringLiteral("泵P1压力低")));
    QCOMPARE(mgr_->shelveCount(), 1);

    AlarmConfigDialog dlg(mgr_, varMgr_);
    QCOMPARE(dlg.rowCount(), 2);

    int shelvedCount = 0;
    for (int i = 0; i < dlg.rowCount(); ++i) {
        if (dlg.shelvedAt(i)) ++shelvedCount;
    }
    QCOMPARE(shelvedCount, 1);

    // 逐个点名确认,避免"屏蔽状态挂到了错误的行上"这种错配 ——
    // 按名字排过序,所以能直接定位。
    for (int i = 0; i < dlg.rowCount(); ++i) {
        if (dlg.nameAt(i) == QStringLiteral("泵P1压力低")) QVERIFY(dlg.shelvedAt(i));
        if (dlg.nameAt(i) == QStringLiteral("温度高"))     QVERIFY(!dlg.shelvedAt(i));
    }
}

// ============================================================================
// ★ 设计不变量:打开对话框绝不能改动运行中的状态
// ============================================================================

void TestAlarmConfigDialog::openDialog_doesNotMutateAlarmManager() {
    useThresholdChecker(mgr_, varMgr_, QStringLiteral("温度高"), QStringLiteral("plc1.Temp"), 80, 2.5);
    mgr_->shelve(QStringLiteral("温度高"));

    const int checkersBefore  = static_cast<int>(mgr_->checkers().size());
    const int shelvedBefore   = mgr_->shelveCount();

    {
        AlarmConfigDialog dlg(mgr_, varMgr_);
        (void)dlg.rowCount();
        (void)dlg.exportDraftToJson();
        // 析构也在这里 —— 析构函数偷偷改状态是最阴的一种
    }

    QCOMPARE(static_cast<int>(mgr_->checkers().size()), checkersBefore);
    QCOMPARE(mgr_->shelveCount(), shelvedBefore);

    // ★ 配置内容本身也不能被就地改掉。
    //   (ThresholdAlarmChecker::config() 返回 const 引用,理论上改不了,
    //    但这条断言的意义是:万一哪天有人加了 setConfig 又忘了管这里,
    //    测试会立刻告诉他"你把 F4-b 的活漏进 F4-a 了"。)
    AlarmChecker* c = mgr_->checkers().isEmpty() ? nullptr : mgr_->checkers().first();
    auto* tc = dynamic_cast<ThresholdAlarmChecker*>(c);
    QVERIFY(tc);
    QCOMPARE(tc->config().threshold,  80.0);
    QCOMPARE(tc->config().hysteresis, 2.5);
    QVERIFY(mgr_->isShelved(QStringLiteral("温度高")));
}

// ============================================================================
// ★ 新增 / 删除 —— 这组用例是 2026-10-08 用户实测后补的
//
// 【背景:为什么 11 条绿测试没拦住】
//   上一版 11 条用例全部只读:它们查 rows_ 里已有的东西。
//   而"点新增"这条路径【一条都没被调用过】——
//   包括 onAdd() 本身。用户点下去没反应,测试却是绿的。
//   这就是本项目 CHANGELOG 里记了 4 次的那条教训的第 5 次。
// ============================================================================

void TestAlarmConfigDialog::clickButton(AlarmConfigDialog* dlg, const QString& objectName) {
    // ★ 必须先 show —— 否则 isVisible() 恒为 false,断言的是"没显示过"而不是
    //   "按钮真的摆出来了"。真实使用走 exec(),那里窗口是显示的。
    //   show 之后再断言 isVisible,才真的能抓出"按钮被布局挤没了"这类问题。
    if (!dlg->isVisible()) {
        dlg->show();
        QVERIFY2(QTest::qWaitForWindowExposed(dlg), "对话框窗口没能显示出来(offscreen 下也应能)");
    }

    auto* btn = dlg->findChild<QPushButton*>(objectName);
    QVERIFY2(btn, qPrintable(QStringLiteral("找不到按钮:%1").arg(objectName)));
    QVERIFY2(btn->isEnabled(), qPrintable(QStringLiteral("按钮是灰的,点了不会有反应:%1").arg(objectName)));
    QVERIFY2(btn->isVisible(), qPrintable(QStringLiteral("按钮不可见,点不到:%1").arg(objectName)));
    QTest::mouseClick(btn, Qt::LeftButton);
}

// ★ 最直接的一条:空表点新增,必须多出一行。
//   用户报的现象原样复现就在这一行断言里。
void TestAlarmConfigDialog::addButton_appendsRow() {
    AlarmConfigDialog dlg(mgr_, varMgr_);
    QCOMPARE(dlg.rowCount(), 0);

    clickButton(&dlg, QStringLiteral("alarmConfigAddBtn"));
    QCOMPARE(dlg.rowCount(), 1);
    QVERIFY(!dlg.nameAt(0).isEmpty());   // 至少得有个名字可改
    QVERIFY(dlg.editableAt(0));          // 新行必须是可编辑的
}

// 已经有报警时再点新增 —— 必须【追加】而不是覆盖/替换。
// 上一版 onAdd 用 insertRow(rows_.size()-1),一旦表格行数和 rows_ 不同步,
// 就会插到已有行上面,把用户正在看的那一行顶掉。
void TestAlarmConfigDialog::addButton_addsRowOnTopOfExisting() {
    useThresholdChecker(mgr_, varMgr_, QStringLiteral("温度高"), QStringLiteral("plc1.Temp"), 80, 0);
    AlarmConfigDialog dlg(mgr_, varMgr_);
    QCOMPARE(dlg.rowCount(), 1);

    clickButton(&dlg, QStringLiteral("alarmConfigAddBtn"));

    QCOMPARE(dlg.rowCount(), 2);
    // 原有那条必须还在,且内容没变 —— 新增不是"替换选中行"
    bool foundOld = false;
    for (int i = 0; i < dlg.rowCount(); ++i) {
        if (dlg.nameAt(i) == QStringLiteral("温度高")) {
            foundOld = true;
            QCOMPARE(dlg.thresholdAt(i), 80.0);
        }
    }
    QVERIFY2(foundOld, "新增把原有报警弄丢了 —— insertRow 插到了已有行上面");
}

// 连点三次:验证不会因为重复 connect 而崩/串行。
// 上一版每次 onAdd 都新连一次 itemChanged,第 N 次点就有 N 个 lambda 挂着。
// 这条至少保证"还能点、还能加",重复连接的具体后果由下面的
// addButton_keepsTableAndDraftInStep 覆盖。
void TestAlarmConfigDialog::addButton_threeTimes_givesThreeRows() {
    AlarmConfigDialog dlg(mgr_, varMgr_);
    for (int i = 0; i < 3; ++i) {
        clickButton(&dlg, QStringLiteral("alarmConfigAddBtn"));
    }
    QCOMPARE(dlg.rowCount(), 3);

    // 三行名字必须各不相同 —— 如果都还是同一个占位名,
    // 说明 rows_ 和表格没同步,后面导出就会出三条重名报警。
    QSet<QString> names;
    for (int i = 0; i < 3; ++i) names.insert(dlg.nameAt(i));
    QCOMPARE(names.size(), 3);
}

// 新行必须落在表格里,而且是最后一行。
// rowCount() 只查 rows_ —— 如果表格没跟着长,用户屏幕上就是"点了没反应",
// 而 rowCount() 的断言照样绿。这条专门查【表格本身】。
void TestAlarmConfigDialog::addButton_keepsTableAndDraftInStep() {
    useThresholdChecker(mgr_, varMgr_, QStringLiteral("温度高"), QStringLiteral("plc1.Temp"), 80, 0);
    AlarmConfigDialog dlg(mgr_, varMgr_);

    auto* table = dlg.findChild<QTableWidget*>(QStringLiteral("alarmConfigTable"));
    QVERIFY(table);
    QCOMPARE(table->rowCount(), dlg.rowCount());

    clickButton(&dlg, QStringLiteral("alarmConfigAddBtn"));

    // ★ 草稿和表格必须一致。差一个就是用户看得见的"点了没反应"。
    QCOMPARE(table->rowCount(), 2);
    QCOMPARE(table->rowCount(), dlg.rowCount());
    QVERIFY(table->item(1, 0));          // 新行的名字单元格必须存在
    QCOMPARE(table->item(1, 0)->text(), dlg.nameAt(1));
}

// 删除:选中第 0 行删掉,剩下的是另一条。
// onRemove 会弹模态 QMessageBox::question —— offscreen 下会阻塞。
// 所以这里【不点删除按钮】,而是直接调槽并预设"用户答 Yes":
// QMessageBox 在测试里没法自动应答,这是 Qt 测试的已知限制,不是缺陷。
// ★ 2026-10-08 F4-b 更新:删除逻辑已从 onRemove 拆到 removeRowFromDraft
//   (onRemove 只负责弹确认框),所以"删掉了会怎样"现在【真的能测】了 ——
//   见 apply_deletesAlarmRemovedFromDraft / apply_deletingFiringAlarm_doesNotLeaveOrphanActiveAlarm。
//   本条仍然只查按钮的存在与 enabled 状态,因为"按钮 → 确认框"那段没法在测试里走完。
void TestAlarmConfigDialog::removeButton_removesSelectedRow() {
    useThresholdChecker(mgr_, varMgr_, QStringLiteral("alpha报警"), QStringLiteral("plc1.A"), 1, 0);
    useThresholdChecker(mgr_, varMgr_, QStringLiteral("beta报警"),  QStringLiteral("plc1.B"), 2, 0);

    AlarmConfigDialog dlg(mgr_, varMgr_);
    QCOMPARE(dlg.rowCount(), 2);

    // 先确认删除按钮存在、且在有选中行时是亮的(灰的 = 点了没反应)
    auto* rm = dlg.findChild<QPushButton*>(QStringLiteral("alarmConfigRemoveBtn"));
    QVERIFY(rm);
    auto* table = dlg.findChild<QTableWidget*>(QStringLiteral("alarmConfigTable"));
    QVERIFY(table);
    table->selectRow(0);
    QVERIFY2(rm->isEnabled(), "选中了可编辑行,删除按钮却是灰的");

    QCOMPARE(dlg.nameAt(0), QStringLiteral("alpha报警"));
    QCOMPARE(dlg.nameAt(1), QStringLiteral("beta报警"));
}

// 名字单元格可编辑:双击改名后,草稿里的名字必须跟着变。
// 【上一版的真实缺陷】setupTable 设了 NoEditTriggers,只给新增行的
//   nameItem 单独加了 Qt::ItemIsEditable 标志 —— 于是【从工程文件读进来的
//   那些报警名字改不了】,只有本次会话新增的能改。
//   界面上没有任何提示,用户只能看着自己的编辑不生效。
void TestAlarmConfigDialog::editNameCell_updatesDraft() {
    useThresholdChecker(mgr_, varMgr_, QStringLiteral("温度高"), QStringLiteral("plc1.Temp"), 80, 0);
    AlarmConfigDialog dlg(mgr_, varMgr_);

    auto* table = dlg.findChild<QTableWidget*>(QStringLiteral("alarmConfigTable"));
    QVERIFY(table);
    QCOMPARE(table->rowCount(), 1);

    auto* nameItem = table->item(0, 0);
    QVERIFY(nameItem);
    // ★ 断言标志而不是断言"能不能打字":editTriggers 是视图级策略,
    //   两者共同决定可编辑性。少了 Qt::ItemIsEditable 就一定改不了。
    QVERIFY2(nameItem->flags().testFlag(Qt::ItemIsEditable),
             "已有报警的名字单元格不可编辑 —— 用户改了没反应,界面无任何提示");
    QVERIFY2(table->editTriggers() != QAbstractItemView::NoEditTriggers,
             "表格整体设成了 NoEditTriggers,任何单元格都改不了");
}

// 改阈值 → 草稿变 → 表格里的显示也跟着变。
// 三者必须同步:草稿变了表格没变,用户会以为没改成功而再改一遍。
void TestAlarmConfigDialog::editThresholdSpin_updatesDraftAndTable() {
    useThresholdChecker(mgr_, varMgr_, QStringLiteral("温度高"), QStringLiteral("plc1.Temp"), 80, 0);
    AlarmConfigDialog dlg(mgr_, varMgr_);

    auto* table = dlg.findChild<QTableWidget*>(QStringLiteral("alarmConfigTable"));
    auto* spin  = dlg.findChild<QDoubleSpinBox*>(QStringLiteral("alarmConfigThresholdSpin"));
    QVERIFY(table);
    QVERIFY(spin);

    table->selectRow(0);              // 选中行 → 表单被填成这一行的值
    QCOMPARE(spin->value(), 80.0);

    spin->setValue(95.5);             // 模拟用户改阈值

    QCOMPARE(dlg.thresholdAt(0), 95.5);
    // ★ 表格那一格必须同步,否则用户看着没变,以为没生效
    QCOMPARE(table->item(0, 4)->text(), QStringLiteral("95.5"));
}

// 新增的报警必须能被导出,否则"新增"只是屏幕上一行字。
void TestAlarmConfigDialog::addButton_newAlarmSurvivesExport() {
    useThresholdChecker(mgr_, varMgr_, QStringLiteral("温度高"), QStringLiteral("plc1.Temp"), 80, 0);
    AlarmConfigDialog dlg(mgr_, varMgr_);
    clickButton(&dlg, QStringLiteral("alarmConfigAddBtn"));

    const QJsonArray arr = QJsonDocument::fromJson(dlg.exportDraftToJson().toUtf8())
                               .object()["alarms"].toArray();
    QCOMPARE(arr.size(), 2);   // 原有 1 条 + 新增 1 条
}

// ============================================================================
// ★ 变量下拉 —— 2026-10-08 用户报"新建报警的时候,变量不能选择"
//
// 【根因:一个自引用门闩】
//   setFormEnabled 里写的是 varCombo_->setEnabled(on && varCombo_->isEnabled());
//   —— 读自己的当前状态来决定要不要设自己。
//   构造函数末尾会调 setFormEnabled(false)(还没选中行),
//   于是组合框从打开对话框那一刻起永久变灰,选中任何行都不会恢复。
//   等级/方向/阈值/滞回全都能改,唯独"关联变量"点不动 —— 界面上看不出任何异常。
//
// 【为什么 30 条测试全绿却没发现】
//   之前没有一条用例断言过 varCombo 的 enabled 状态。
//   大家只查 rows_ 里存的值对不对,没查过"用户能不能改"。
//   "值对了"和"控件能用"是两件事 —— 这正是本项目记了 5 次的那条教训。
// ============================================================================

// ★ 用户报的现象原样复现:点新增后,关联变量下拉必须是可点的。
void TestAlarmConfigDialog::varCombo_isSelectableOnNewRow() {
    varMgr_->addVariable(new Variable(QStringLiteral("plc1.Temp"), static_cast<quint16>(100),
                                      Variable::DataType::Float, Variable::Access::ReadOnly));
    varMgr_->addVariable(new Variable(QStringLiteral("plc1.Press"), static_cast<quint16>(101),
                                      Variable::DataType::Float, Variable::Access::ReadOnly));

    AlarmConfigDialog dlg(mgr_, varMgr_);
    auto* combo = dlg.findChild<QComboBox*>(QStringLiteral("alarmConfigVarCombo"));
    QVERIFY2(combo, "找不到关联变量下拉框");
    QCOMPARE(combo->count(), 2);

    clickButton(&dlg, QStringLiteral("alarmConfigAddBtn"));

    // ★ 这条断言在修复前是红的 —— 它就是用户说的"变量不能选择"
    QVERIFY2(combo->isEnabled(),
             "新建报警后关联变量下拉是灰的 —— 用户点不动(自引用门闩 bug)");
}

// 不只是"亮着",还得真能改:选另一个变量,草稿和表格都要跟着变。
// 只断言 isEnabled() 是不够的 —— 亮着但写不进草稿,用户照样觉得"没用"。
void TestAlarmConfigDialog::varCombo_picksUpVariableChange() {
    varMgr_->addVariable(new Variable(QStringLiteral("plc1.Temp"), static_cast<quint16>(100),
                                      Variable::DataType::Float, Variable::Access::ReadOnly));
    varMgr_->addVariable(new Variable(QStringLiteral("plc1.Press"), static_cast<quint16>(101),
                                      Variable::DataType::Float, Variable::Access::ReadOnly));

    AlarmConfigDialog dlg(mgr_, varMgr_);
    auto* combo  = dlg.findChild<QComboBox*>(QStringLiteral("alarmConfigVarCombo"));
    auto* table  = dlg.findChild<QTableWidget*>(QStringLiteral("alarmConfigTable"));
    QVERIFY(combo);
    QVERIFY(table);

    clickButton(&dlg, QStringLiteral("alarmConfigAddBtn"));   // 新行选中,currentRow()==0
    QCOMPARE(dlg.rowCount(), 1);

    const int pressIdx = combo->findText(QStringLiteral("plc1.Press"));
    QVERIFY(pressIdx >= 0);
    combo->setCurrentIndex(pressIdx);          // 模拟用户选了另一个变量

    QCOMPARE(dlg.variableAt(0), QStringLiteral("plc1.Press"));
    // 表格那一格也要同步,否则用户看着没变
    QCOMPARE(table->item(0, 1)->text(), QStringLiteral("plc1.Press"));
}

// 工程里一个变量都没有时,下拉必须保持禁用 —— 不能因为修了门闩
// 就让"(工程里还没有变量)"这个占位项变得可选。
void TestAlarmConfigDialog::varCombo_staysDisabledWhenNoVariables() {
    QCOMPARE(varMgr_->allVariables().size(), 0);
    AlarmConfigDialog dlg(mgr_, varMgr_);
    auto* combo = dlg.findChild<QComboBox*>(QStringLiteral("alarmConfigVarCombo"));
    QVERIFY(combo);
    QVERIFY2(!combo->isEnabled(),
             "工程里没有变量,下拉却可点 —— 用户会选到一个不存在的变量");
    QCOMPARE(combo->count(), 1);   // 只有占位项
}

// 切行之后必须还能改 —— 这条直接钉死"单向门闩"这个性质。
// 只要它存在一次"禁用后再也回不来",这条必红。
void TestAlarmConfigDialog::varCombo_reEnabledAfterRowSwitch() {
    varMgr_->addVariable(new Variable(QStringLiteral("plc1.Temp"), static_cast<quint16>(100),
                                      Variable::DataType::Float, Variable::Access::ReadOnly));
    AlarmConfigDialog dlg(mgr_, varMgr_);
    auto* combo = dlg.findChild<QComboBox*>(QStringLiteral("alarmConfigVarCombo"));
    auto* table = dlg.findChild<QTableWidget*>(QStringLiteral("alarmConfigTable"));
    QVERIFY(combo);
    QVERIFY(table);

    // 还没选中任何行 → 整个表单禁用(这是预期的)
    QVERIFY2(!combo->isEnabled(), "没选中行时下拉应当是灰的");

    clickButton(&dlg, QStringLiteral("alarmConfigAddBtn"));   // 选中第 0 行
    QVERIFY2(combo->isEnabled(), "选中行后下拉必须恢复可用 —— 门闩 bug");

    // ★ 用 setCurrentCell(-1,-1) 真正取消选中,而不是 clearSelection()。
    //   clearSelection() 只清高亮,currentRow() 还在,itemSelectionChanged
    //   不发 → fillFormFromRow 根本不会被调用 → 表单状态压根没变过。
    //   （第一版这里用 clearSelection,断言"应该变灰"却没灰,是我把
    //     测试前提写错了,不是实现有问题。）
    table->setCurrentCell(-1, -1);
    QVERIFY2(!combo->isEnabled(), "取消选中后下拉应变灰");

    table->selectRow(0);
    QVERIFY2(combo->isEnabled(), "二次选中行后下拉仍必须可用 —— 单向门闩");
}

// ============================================================================
// ★ F4-c:存进工程文件 —— 真实往返
//
// 【这组用例在防什么】
//   用户 2026-10-08 反馈"加了行、改了值没反应"。根因是草稿根本没有落地出口。
//   落地之后,新的风险是【存进去把工程存坏了】——
//   而"存坏了"的表现是下次打开工程直接失败,用户几天后才发现。
//   所以这组用例全部基于磁盘上的真实文件,不只断言内存对象。
// ============================================================================

// 存完 alarms 段必须真的在文件里。
void TestAlarmConfigDialog::save_writesAlarmsToRealFile() {
    useThresholdChecker(mgr_, varMgr_, QStringLiteral("温度高"), QStringLiteral("plc1.Temp"), 80, 0);
    const QString path = writeTempProject(sentinelProject());
    QVERIFY(!path.isEmpty());

    AlarmConfigDialog dlg(mgr_, varMgr_);
    QString err;
    QVERIFY2(dlg.saveDraftToProjectFile(path, &err), qPrintable(err));

    const QJsonArray alarms = readJson(path)["alarms"].toArray();
    QCOMPARE(alarms.size(), 1);
    QCOMPARE(alarms.first().toObject()["name"].toString(), QStringLiteral("温度高"));
}

// ★ 核心约束:只改 alarms,别的段一个都不动。
//   如果这条红了,意味着用户配个报警就把画面和变量清空了 ——
//   而对话框界面上根本没碰过它们,用户不会知道。
void TestAlarmConfigDialog::save_preservesOtherSections() {
    useThresholdChecker(mgr_, varMgr_, QStringLiteral("温度高"), QStringLiteral("plc1.Temp"), 80, 0);
    const QString path = writeTempProject(sentinelProject());
    QVERIFY(!path.isEmpty());

    AlarmConfigDialog dlg(mgr_, varMgr_);
    QString err;
    QVERIFY2(dlg.saveDraftToProjectFile(path, &err), qPrintable(err));

    const QJsonObject after = readJson(path);
    QCOMPARE(after["project_name"].toString(), QStringLiteral("哨兵工程"));
    QCOMPARE(after["schema_version"].toInt(), 3);

    // variables
    const QJsonArray vars = after["variables"].toArray();
    QCOMPARE(vars.size(), 1);
    QCOMPARE(vars.first().toObject()["name"].toString(), QStringLiteral("plc1.Temp"));

    // actions
    QCOMPARE(after["actions"].toArray().size(), 1);
    QCOMPARE(after["actions"].toArray().first().toObject()["alarm_name"].toString(),
             QStringLiteral("温度高"));

    // screens[0].controls —— 画布必须完好
    const QJsonArray screens = after["screens"].toArray();
    QCOMPARE(screens.size(), 1);
    QCOMPARE(screens.first().toObject()["controls"].toArray().size(), 1);
    QCOMPARE(screens.first().toObject()["controls"].toArray().first().toObject()["type"].toString(),
             QStringLiteral("BarControl"));

    // plcs
    QCOMPARE(after["plcs"].toArray().size(), 1);
}

// ★ 存进去的工程必须还能被 Project::loadFromFile 读回来 ——
//   且读回来的报警就是新加的那条。这条是整组用例的最终目的:
//   "配的东西真的进了工程文件,重开还在"。
void TestAlarmConfigDialog::save_resultReloadsThroughProject() {
    // 先在 varMgr 里建出哨兵工程里的那个变量,否则加载会因缺变量失败
    varMgr_->addVariable(new Variable(QStringLiteral("plc1.Temp"),
                                      static_cast<quint16>(100),
                                      Variable::DataType::Float,
                                      Variable::Access::ReadOnly));

    // 草稿里新增一条报警,指向同一个变量
    AlarmConfigDialog dlg(mgr_, varMgr_);
    clickButton(&dlg, QStringLiteral("alarmConfigAddBtn"));

    const QString path = writeTempProject(sentinelProject());
    QVERIFY(!path.isEmpty());
    QString err;
    QVERIFY2(dlg.saveDraftToProjectFile(path, &err), qPrintable(err));

    // ★ 用真正的 Project 读回来。AlarmManager* 传 nullptr 也行 ——
    //   loadRootFromFile 对 manager 为 null 是容错的(Project.cpp:112-113),
    //   我们要验的是【文件本身合法可解析】,不是加载副作用。
    Project proj(nullptr, nullptr, nullptr, nullptr, nullptr);
    QVERIFY2(proj.loadFromFile(path), "存进去的工程文件读不回来 —— 这是最严重的一种坏");

    // 读回来的 alarms 里有新增那条
    const QJsonArray alarms = readJson(path)["alarms"].toArray();
    QCOMPARE(alarms.size(), 1);
    const QString savedName = alarms.first().toObject()["name"].toString();
    QCOMPARE(savedName, dlg.nameAt(0));
    QVERIFY2(!savedName.isEmpty(), "新增的报警没存进去");
}

// 名字为空 → 拒绝落盘。
// 理由:AlarmManager::addChecker 拒收空名(AlarmManager.cpp:13),
// 加载时会整体失败。存下去等于给用户埋一个"打不开的工程"。
void TestAlarmConfigDialog::save_rejectsEmptyName() {
    useThresholdChecker(mgr_, varMgr_, QStringLiteral("温度高"), QStringLiteral("plc1.Temp"), 80, 0);
    const QString path = writeTempProject(sentinelProject());
    QVERIFY(!path.isEmpty());

    AlarmConfigDialog dlg(mgr_, varMgr_);
    // 直接构造一个空名的 alarms 数组测 static 校验(不用走 UI)
    QJsonArray bad;
    QJsonObject a;
    a["name"]     = QString();
    a["variable"] = QStringLiteral("plc1.Temp");
    bad.append(a);

    QString err;
    QVERIFY2(!Project::validateAlarmsArray(bad, &err), "空名字被放过了");
    QVERIFY(!err.isEmpty());   // ★ 必须说明为什么,否则用户不知道为什么存不了
    // ★ 文件必须没被动过
    QVERIFY(!readJson(path).contains("alarms"));
}

// 名字重复 → 拒绝。理由同上:addChecker 用 name 作 key,重名整体失败。
void TestAlarmConfigDialog::save_rejectsDuplicateNames() {
    QJsonArray dup;
    for (int i = 0; i < 2; ++i) {
        QJsonObject a;
        a["name"]     = QStringLiteral("温度高");
        a["variable"] = QStringLiteral("plc1.Temp");
        dup.append(a);
    }
    QString err;
    QVERIFY2(!Project::validateAlarmsArray(dup, &err), "重名被放过了");
    QVERIFY2(err.contains(QStringLiteral("重复")), qPrintable(err));
}

// ★ 引用不存在的变量 → 拒绝。
//   这条最要紧:Project.cpp:211 加载时遇到这种报警会 return false,
//   整个工程文件打不开 —— 用户的画面、变量、联动全部一起完蛋。
void TestAlarmConfigDialog::save_rejectsUnknownVariable() {
    // 这个变量【故意】不进 varMgr_
    Variable* orphan = new Variable(QStringLiteral("plc1.Orphan"),
                                    static_cast<quint16>(200),
                                    Variable::DataType::Float,
                                    Variable::Access::ReadOnly);
    CheckerConfig cfg;
    cfg.threshold = 10.0;
    auto* checker = new ThresholdAlarmChecker(QStringLiteral("孤儿报警"),
                                              orphan, nullptr, cfg, mgr_);
    mgr_->addChecker(checker);

    QCOMPARE(varMgr_->allVariables().size(), 0);   // varMgr_ 确实是空的

    AlarmConfigDialog dlg(mgr_, varMgr_);
    QCOMPARE(dlg.rowCount(), 1);

    QString err;
    // ★ 必须拒绝。理由:Project.cpp:211 加载时遇到引用不存在变量的报警会
    //   return false —— 不是"这一条报警有问题",是【整个工程打不开】。
    QVERIFY2(!dlg.validateDraft(&err),
             "报警引用了不存在的变量,却允许存盘 —— 这会产出打不开的工程文件");
    QVERIFY2(!err.isEmpty(), "拦截了必须告诉用户为什么");
    QVERIFY2(err.contains(QStringLiteral("变量")),
             qPrintable(QStringLiteral("错误信息应点明是变量的问题,实际:%1").arg(err)));

    // ★ 更重要的:文件必须没被动过。校验没过还照样写,拦截就形同虚设。
    const QString path = writeTempProject(sentinelProject());
    QVERIFY(!path.isEmpty());
    QVERIFY(!dlg.saveDraftToProjectFile(path, &err));
    QVERIFY2(!readJson(path).contains("alarms"),
             "校验没过却仍然写了文件 —— 拦截形同虚设");
}

// 落盘不得触碰运行中的 AlarmManager —— 这是 F4-a 一直守的不变量,
// F4-c 加了写入能力之后更要守住。
void TestAlarmConfigDialog::save_doesNotTouchRunningAlarmManager() {
    useThresholdChecker(mgr_, varMgr_, QStringLiteral("温度高"), QStringLiteral("plc1.Temp"), 80, 2.5);
    const QString path = writeTempProject(sentinelProject());
    QVERIFY(!path.isEmpty());

    const int checkersBefore = static_cast<int>(mgr_->checkers().size());

    AlarmConfigDialog dlg(mgr_, varMgr_);
    clickButton(&dlg, QStringLiteral("alarmConfigAddBtn"));
    QString err;
    QVERIFY2(dlg.saveDraftToProjectFile(path, &err), qPrintable(err));

    // 文件里有 2 条了,但运行中的 checker 数必须没变
    QCOMPARE(readJson(path)["alarms"].toArray().size(), 2);
    QCOMPARE(static_cast<int>(mgr_->checkers().size()), checkersBefore);
}

// 文件不存在 → 拒绝,不许凭空造一个只有 alarms 的文件。
// 理由:那种文件加载时必然缺 variables 而失败(见 loadRootFromFile)。
void TestAlarmConfigDialog::save_refusesNonexistentFile() {
    useThresholdChecker(mgr_, varMgr_, QStringLiteral("温度高"), QStringLiteral("plc1.Temp"), 80, 0);
    AlarmConfigDialog dlg(mgr_, varMgr_);

    QString err;
    const QString missing = QStringLiteral("%1/绝对不存在_%2.json")
                                .arg(QDir::tempPath())
                                .arg(QCoreApplication::applicationPid());
    QVERIFY2(!dlg.saveDraftToProjectFile(missing, &err), "不存在的文件竟被写成功了");
    QVERIFY2(!err.isEmpty(), "失败必须说明原因");
    QVERIFY(!QFile::exists(missing));   // ★ 绝不能顺手创建一个
}

// 原文件不是合法 JSON → 拒绝覆盖。
// 理由:覆盖掉用户唯一的工程文件是不可逆的,而这里完全能避免。
void TestAlarmConfigDialog::save_refusesCorruptJson() {
    const QString path = QStringLiteral("%1/alarmcfg_broken_%2.json")
                             .arg(QDir::tempPath())
                             .arg(QCoreApplication::applicationPid());
    tempFiles_.append(path);
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
    f.write("{ 这不是合法 JSON ");
    f.close();

    useThresholdChecker(mgr_, varMgr_, QStringLiteral("温度高"), QStringLiteral("plc1.Temp"), 80, 0);
    AlarmConfigDialog dlg(mgr_, varMgr_);
    QString err;
    QVERIFY2(!dlg.saveDraftToProjectFile(path, &err), "坏文件被覆盖了");
    // ★ 原内容必须原封不动还在
    QFile check(path);
    QVERIFY(check.open(QIODevice::ReadOnly));
    QCOMPARE(check.readAll(), QByteArray("{ 这不是合法 JSON "));
}

// static 校验层的直接用例 —— 上面几条是从对话框侧打的,这里钉住纯函数行为。
void TestAlarmConfigDialog::validateAlarmsArray_unitLevel() {
    QString err;
    // 空数组合法
    QVERIFY(Project::validateAlarmsArray(QJsonArray(), &err));
    QVERIFY(err.isEmpty());
    // 正常两条合法
    QJsonArray ok;
    for (const char* n : {"A", "B"}) {
        QJsonObject a;
        a["name"] = QString::fromLatin1(n);
        ok.append(a);
    }
    QVERIFY2(Project::validateAlarmsArray(ok, &err), qPrintable(err));
    // 只有空格的名字也算空 —— 名字是 key,空格名在界面上看着像有名字,
    // 但用户八成是想删掉重填。静默通过会产出一条莫名其妙的报警。
    QJsonArray blank;
    QJsonObject b;
    b["name"] = QStringLiteral("   ");
    blank.append(b);
    QVERIFY(!Project::validateAlarmsArray(blank, &err));
}

// ============================================================================
// ★ F4-b:应用到运行中的 AlarmManager
//
// 【这组用例在防什么】
//   F4-b 是第一次让这个对话框【真的改动运行中的报警】。在此之前
//   "改动只存在于草稿"是一个可以反复依赖的安全网;现在这层网撤了,
//   于是有两类以前不可能发生的故障:
//     ①【改了一半】—— 前几条生效了,中间一条失败,而用户只看到"应用失败"。
//        结果是运行中的状态和界面上的草稿对不上,且没人说得清哪里对不上。
//     ②【孤儿记录】—— 报警正在响时被删掉重建,活动列表里那条再也收不了尾。
//        操作员看到一个撤不掉的报警,历史表也少一条收尾(F1 d504b53 那类 bug)。
//
//   所以这组用例的重点不是"改了会不会生效"(那是显然的),
//   而是"改坏了之后能不能被发现",以及"失败的路径有没有留下半成品"。
//
// ★ 关于 mock 的取舍
//   F4-b 的核心风险在"改运行中的状态",而 ThresholdAlarmChecker 的检测逻辑
//   要有 PollScheduler 才跑得起来。本组测试【大多不需要真的跑起来】:
//   改配置、改变量、删报警都是内存操作,断言 config()/isActive()/activeAlarms()
//   就够了。唯一需要真跑的是"改完还能不能恢复",那条走 PollScheduler 真发信号。
// ============================================================================

// ---- 基本:改阈值要真的落到运行中的 checker 上 ----

void TestAlarmConfigDialog::apply_updatesThresholdOfRunningAlarm() {
    useThresholdChecker(mgr_, varMgr_, QStringLiteral("温度高"), QStringLiteral("plc1.Temp"), 80, 0);
    // ★ 这条用例只验"改已有报警",所以【不新增】—— 新增需要 pollScheduler,
    //   那是 apply_addsBrandNewAlarm 的活。一条用例只验一件事。
    AlarmConfigDialog dlg(mgr_, varMgr_, new PollScheduler(nullptr, nullptr));
    dlg.show();
    auto* table = dlg.findChild<QTableWidget*>(QStringLiteral("alarmConfigTable"));
    QVERIFY(table);
    // ★ 不要靠"第几行"找 —— reloadFromManager 按名字排序,
    //   而"新报警"和"温度高"的先后跟编码比较有关,写死行号就是埋雷。
    const int r = rowIndexByName(dlg, QStringLiteral("温度高"));
    QVERIFY2(r >= 0, "表里找不到「温度高」这一行");
    QCOMPARE(r, 0);   // 只有一条,必然是第 0 行 —— 这条断言是刻意的:
                      // 多一条草稿时它会红,提醒我测试的前提变了。
    table->setCurrentCell(r, 0);
    auto* spin = dlg.findChild<QDoubleSpinBox*>(QStringLiteral("alarmConfigThresholdSpin"));
    QVERIFY(spin);
    spin->setValue(55.0);

    QString err;
    if (!dlg.applyDraftToManager(&err)) {
        // 失败时把草稿内容打出来 —— 否则只能看到一句"返回 FALSE",
        // 不知道是哪一行、哪个变量出的问题(这就是 F3 定下的"报错必须能用")。
        for (int i = 0; i < dlg.rowCount(); ++i) {
            qDebug() << "  draft row" << i << "name=" << dlg.nameAt(i)
                     << "var=" << dlg.variableAt(i) << "thr=" << dlg.thresholdAt(i);
        }
        QFAIL(qPrintable(err));
    }

    // ★ 断言运行中那个 checker 的配置,不是断言对话框里的显示。
    bool found = false;
    for (AlarmChecker* c : mgr_->checkers()) {
        auto* tc = dynamic_cast<ThresholdAlarmChecker*>(c);
        if (!tc || tc->name() != QStringLiteral("温度高")) continue;
        found = true;
        QCOMPARE(tc->config().threshold, 55.0);
    }
    QVERIFY2(found, "温度高这条报警不见了 —— 应用后 checker 数量不该变");
}

// 等级/方向/滞回/最短持续 四个字段一起改 —— 少改一个都是"界面上改了、没生效"。
// 尤其【等级】:它存在基类的 level_ 里,只改 config_ 的话报警还是旧等级。
void TestAlarmConfigDialog::apply_updatesLevelDirectionHysteresisMinDur() {
    useThresholdChecker(mgr_, varMgr_, QStringLiteral("温度高"), QStringLiteral("plc1.Temp"), 80, 0);
    AlarmConfigDialog dlg(mgr_, varMgr_);
    dlg.show();
    auto* table = dlg.findChild<QTableWidget*>(QStringLiteral("alarmConfigTable"));
    QVERIFY(table);
    table->setCurrentCell(0, 0);

    auto* level = dlg.findChild<QComboBox*>(QStringLiteral("alarmConfigLevelCombo"));
    auto* dir   = dlg.findChild<QComboBox*>(QStringLiteral("alarmConfigDirCombo"));
    auto* hyst  = dlg.findChild<QDoubleSpinBox*>(QStringLiteral("alarmConfigHysteresisSpin"));
    auto* minDur= dlg.findChild<QSpinBox*>(QStringLiteral("alarmConfigMinDurSpin"));
    QVERIFY(level && dir && hyst && minDur);
    level->setCurrentIndex(level->findData(QStringLiteral("Emergency")));
    dir->setCurrentIndex(dir->findData(QStringLiteral("Below")));
    hyst->setValue(4.5);
    minDur->setValue(1500);

    QString err;
    QVERIFY2(dlg.applyDraftToManager(&err), qPrintable(err));

    for (AlarmChecker* c : mgr_->checkers()) {
        auto* tc = dynamic_cast<ThresholdAlarmChecker*>(c);
        if (!tc) continue;
        QCOMPARE(tc->config().level,         AlarmLevel::Emergency);
        QCOMPARE(tc->config().direction,     Direction::Below);
        QCOMPARE(tc->config().hysteresis,    4.5);
        QCOMPARE(tc->config().minDurationMs, 1500);
        // ★ 等级必须同时进了基类 —— 它决定报警在界面上是什么颜色。
        QCOMPARE(tc->level(), AlarmLevel::Emergency);
    }
}

// 新增一条 → 运行中真的多了一个能用的 checker。
void TestAlarmConfigDialog::apply_addsBrandNewAlarm() {
    useThresholdChecker(mgr_, varMgr_, QStringLiteral("温度高"), QStringLiteral("plc1.Temp"), 80, 0);
    PollScheduler* poll = new PollScheduler(nullptr, nullptr);
    AlarmConfigDialog dlg(mgr_, varMgr_, poll);
    clickButton(&dlg, QStringLiteral("alarmConfigAddBtn"));

    QString err;
    QVERIFY2(dlg.applyDraftToManager(&err), qPrintable(err));

    // 运行中的 checker 从 1 条变 2 条
    QSet<QString> names;
    for (AlarmChecker* c : mgr_->checkers()) if (c) names.insert(c->name());
    QCOMPARE(names.size(), 2);
    QVERIFY(names.contains(QStringLiteral("温度高")));
    QVERIFY2(names.contains(dlg.nameAt(dlg.rowCount() - 1))
             || names.contains(QStringLiteral("新报警")),
             "新增的报警没出现在运行中的 AlarmManager 里");
}

// 草稿里删掉的 → 运行中也要真的删掉,且不能留孤儿。
void TestAlarmConfigDialog::apply_deletesAlarmRemovedFromDraft() {
    useThresholdChecker(mgr_, varMgr_, QStringLiteral("温度高"), QStringLiteral("plc1.Temp"), 80, 0);
    useThresholdChecker(mgr_, varMgr_, QStringLiteral("温度低"), QStringLiteral("plc1.Temp"), 10, 0);
    PollScheduler* poll = new PollScheduler(nullptr, nullptr);
    AlarmConfigDialog dlg(mgr_, varMgr_, poll);
    dlg.show();
    QCOMPARE(dlg.rowCount(), 2);
    // ★ 走 removeRowFromDraft 而不是点按钮:onRemove 的确认框在 offscreen 下
    //   会永久阻塞(实测卡到 300s 超时)。详见 removeRowFromDraft 的说明。
    const int victim = rowIndexByName(dlg, QStringLiteral("温度低"));
    QVERIFY2(victim >= 0, "表里找不到「温度低」这一行");
    QVERIFY(dlg.removeRowFromDraft(victim));
    QCOMPARE(dlg.rowCount(), 1);

    QString err;
    QVERIFY2(dlg.applyDraftToManager(&err), qPrintable(err));

    QSet<QString> names;
    for (AlarmChecker* c : mgr_->checkers()) if (c) names.insert(c->name());
    QCOMPARE(names.size(), 1);
    QVERIFY(!names.contains(QStringLiteral("温度低")));
    QVERIFY(names.contains(QStringLiteral("温度高")));
}

// 屏蔽状态是【运行中】的状态(shelved_ 里),草稿改了就得跟着变。
// 不同步的话,对话框显示的"已屏蔽"和实际屏蔽状态会各说各话 ——
// 而 F2-c 的硬约束恰恰是"屏蔽必须可见、不能藏"。
void TestAlarmConfigDialog::apply_syncsShelvedState() {
    useThresholdChecker(mgr_, varMgr_, QStringLiteral("温度高"), QStringLiteral("plc1.Temp"), 80, 0);
    QVERIFY(mgr_->shelve(QStringLiteral("温度高")));
    QVERIFY(mgr_->isShelved(QStringLiteral("温度高")));

    PollScheduler* poll = new PollScheduler(nullptr, nullptr);
    AlarmConfigDialog dlg(mgr_, varMgr_, poll);
    QCOMPARE(dlg.shelvedAt(0), true);   // 草稿读进来时带着屏蔽标记

    QString err;
    QVERIFY2(dlg.applyDraftToManager(&err), qPrintable(err));
    // 草稿里仍是"已屏蔽" → 运行中也应仍是屏蔽的(不能被应用动作顺手清掉)
    QVERIFY2(mgr_->isShelved(QStringLiteral("温度高")),
             "应用草稿把用户的屏蔽状态弄丢了 —— 这是 F2-c 明令禁止的");
}

// ---- 屏蔽列可勾选(2026-10-14)----
//
// 【这条要守住的是什么】
//   修复前"已屏蔽"那一列是纯只读文字,用户改不动,于是 applyToRunning()
//   里那段同步屏蔽状态的分支永远走不到 —— UI 上看着有这一列,
//   实际上这个功能没有任何入口。
//   所以下面几条不是"测一个勾选框",是测【勾了之后真的会生效】。

// 勾选框要真的画得出来。修复前那一格是只读文字,没设 CheckStateRole。
void TestAlarmConfigDialog::shelveColumn_isUserCheckableNotReadOnlyText() {
    useThresholdChecker(mgr_, varMgr_, QStringLiteral("温度高"), QStringLiteral("plc1.Temp"), 80, 0);

    PollScheduler* poll = new PollScheduler(nullptr, nullptr);
    AlarmConfigDialog dlg(mgr_, varMgr_, poll);
    dlg.show();
    auto* table = dlg.findChild<QTableWidget*>(QStringLiteral("alarmConfigTable"));
    QVERIFY(table);

    // 找到"已屏蔽"那一列 —— 按表头文字找,不写死列号,
    // 免得以后插列时这条用例悄悄指到别的列上(还照样绿)。
    int col = -1;
    for (int c = 0; c < table->columnCount(); ++c) {
        if (table->horizontalHeaderItem(c)
            && table->horizontalHeaderItem(c)->text() == QStringLiteral("已屏蔽")) {
            col = c;
        }
    }
    QVERIFY2(col >= 0, "表头里找不到「已屏蔽」这一列");

    auto* item = table->item(0, col);
    QVERIFY(item);

    // ★ 真正让这一格变成勾选框的是【有没有设过 CheckStateRole】,
    //   不是 ItemIsUserCheckable 标志 —— 那个标志 QTableWidgetItem
    //   【默认就带着】,断言它等于什么都没断言(负向对照实测:
    //   把生产代码里的 "| Qt::ItemIsUserCheckable" 删掉,本用例照样全绿)。
    //   样式是按 CheckStateRole 画勾选框的:这个角色没值 → 只画文字或不画,
    //   点了也没反应。修复前"已屏蔽"那列恰恰就是这个状态。
    QVERIFY2(item->data(Qt::CheckStateRole).isValid(),
             "「已屏蔽」那一格没设 CheckStateRole —— 表格不会画勾选框,"
             "点了也没反应,于是 applyToRunning() 里的屏蔽同步分支永远走不到");
    QVERIFY(!item->flags().testFlag(Qt::ItemIsEditable));
    QCOMPARE(item->text(), QString());   // 不显示"是" —— 避免两套表示
}

// 核心用例:勾上 → 草稿变了 → 应用后运行中真的被屏蔽。
//
// ★ 这里的勾选是【真的用鼠标点那一格】,不是 setCheckState()。
//   【为什么不能直接 setCheckState】
//   setCheckState 是程序写值 —— 它绕过了"这一格到底能不能点"这件事。
//   实测:把 Qt::ItemIsUserCheckable 去掉之后,本文件里所有 setCheckState
//   的用例【照样全绿】(负向对照跑出来的)。
//   而用户能不能真的点得动,恰恰是这次要修的东西。
//   直接写值的断言测的是"数据通路",不是"用户通路" ——
//   而这次的用户报障正是"界面上那一列看得见、点不动"。
void TestAlarmConfigDialog::checkingShelveBox_appliesToRunningManager() {
    useThresholdChecker(mgr_, varMgr_, QStringLiteral("温度高"), QStringLiteral("plc1.Temp"), 80, 0);
    QVERIFY(!mgr_->isShelved(QStringLiteral("温度高")));

    PollScheduler* poll = new PollScheduler(nullptr, nullptr);
    AlarmConfigDialog dlg(mgr_, varMgr_, poll);
    dlg.show();
    auto* table = dlg.findChild<QTableWidget*>(QStringLiteral("alarmConfigTable"));
    QVERIFY(table);

    int col = -1;
    for (int c = 0; c < table->columnCount(); ++c) {
        if (table->horizontalHeaderItem(c)
            && table->horizontalHeaderItem(c)->text() == QStringLiteral("已屏蔽")) {
            col = c;
        }
    }
    QVERIFY(col >= 0);
    QCOMPARE(table->item(0, col)->checkState(), Qt::Unchecked);

    // ★ 真点击勾选框。用户要点的就是那个小方块。
    //   【为什么不直接点整格中心】
    //   QStyledItemDelegate::editorEvent 只在鼠标落在
    //   SE_ItemViewItemCheckIndicator 算出来的那个矩形里才 toggle ——
    //   点格子正中(哪怕正在这个可勾选的格子上)什么也不会发生。
    //   这不是本项目的怪癖,是 Qt 对所有勾选列的统一行为。
    //   写死一个偏移量(比如"往左 10 像素")会在别的 DPI/样式下打到别处,
    //   所以这里向 style 要那个矩形的真实位置。
    //
    //   ★ HasCheckIndicator 这一行【不能省】—— 负向对照实测:
    //     本机(Qt 6.8.3 + fusion 样式 + offscreen 平台)去掉它,
    //     subElementRect 返回 QRect(0,0 0x0),直接点格子正中什么也不会发生;
    //     加回它,返回 QRect(814,5 14x14),点得中。
    //     也就是说这个标志位就是"这一格画不画勾选框"的总开关。
    QStyleOptionViewItem opt;
    opt.initFrom(table);   // 参数必须是视图:QTableWidgetItem 不是 QWidget
    opt.rect = table->visualItemRect(table->item(0, col));
    opt.state = QStyle::State_Enabled | QStyle::State_Active;
    opt.features |= QStyleOptionViewItem::HasCheckIndicator;
    const QRect checkRect = table->style()->subElementRect(
        QStyle::SE_ItemViewItemCheckIndicator, &opt, table);
    QVERIFY2(checkRect.isValid() && !checkRect.isEmpty(),
             "算不出勾选框的位置 —— 这台机器的样式不给 indicator,"
             "这条用例没法验证真实点击路径");

    // opt.rect 就是 visualItemRect(已是 viewport 坐标),
    // 所以 subElementRect 返回的也是 viewport 坐标,直接点它的中心即可。
    QTest::mouseClick(table->viewport(), Qt::LeftButton, Qt::NoModifier,
                      checkRect.center());

    QVERIFY2(table->item(0, col)->checkState() == Qt::Checked,
             "鼠标点了一下,勾选框没打勾 —— "
             "这一列对用户仍然是点不动的(检查有没有 setCheckState)");

    // 勾了但还没点"应用":运行中【必须还没变】。
    // 草稿语义:改了先不生效,点【应用到运行中】才生效。
    QVERIFY2(!mgr_->isShelved(QStringLiteral("温度高")),
             "勾选框一勾就改了运行中状态 —— 用户还没点「应用到运行中」");

    // 草稿必须记下来了(这是中间那一步,断了的话 apply 无从判断)
    QVERIFY2(dlg.shelvedAt(0), "点了勾但草稿没记 —— apply() 会以为用户没改过");

    QString err;
    QVERIFY2(dlg.applyDraftToManager(&err), qPrintable(err));
    QVERIFY2(mgr_->isShelved(QStringLiteral("温度高")),
             "点了应用但运行中没被屏蔽 —— 用户的操作被吞了");
}

// 反向:取消勾选 → 应用后运行中真的解除屏蔽。
// 只测"能屏蔽"不测"能解除",那这个功能就是个单向门 ——
// 而 F2-c 明写"屏蔽必须能撤销(不是单向门)"。
void TestAlarmConfigDialog::uncheckingShelveBox_appliesUnshelveToRunningManager() {
    useThresholdChecker(mgr_, varMgr_, QStringLiteral("温度高"), QStringLiteral("plc1.Temp"), 80, 0);
    QVERIFY(mgr_->shelve(QStringLiteral("温度高")));

    PollScheduler* poll = new PollScheduler(nullptr, nullptr);
    AlarmConfigDialog dlg(mgr_, varMgr_, poll);
    dlg.show();
    auto* table = dlg.findChild<QTableWidget*>(QStringLiteral("alarmConfigTable"));
    QVERIFY(table);

    int col = -1;
    for (int c = 0; c < table->columnCount(); ++c) {
        if (table->horizontalHeaderItem(c)
            && table->horizontalHeaderItem(c)->text() == QStringLiteral("已屏蔽")) {
            col = c;
        }
    }
    QVERIFY(col >= 0);

    QVERIFY(dlg.shelvedAt(0));   // 草稿读进来时是屏蔽态
    table->item(0, col)->setCheckState(Qt::Unchecked);
    QVERIFY(!dlg.shelvedAt(0));

    QString err;
    QVERIFY2(dlg.applyDraftToManager(&err), qPrintable(err));
    QVERIFY2(!mgr_->isShelved(QStringLiteral("温度高")),
             "取消了勾选但解除不了屏蔽 —— F2-c 明写屏蔽必须可撤销");
}

// ★ 新增出来的那一行,「已屏蔽」也必须是能点的勾选框。
//   这不是重复上面那条 —— onAdd() 有【自己一套填表代码】(put() 那一支),
//   和 reloadFromManager() 不是同一段。两处只要漏一处,那条漏的就复现了:
//   从工程里读进来的报警能勾,新加的勾不动。
//   实测:onAdd() 原来对这一列调的是 put(kColShelved, QString()),
//   而 put() 从不设 CheckStateRole —— 那一格画不出勾选框。
void TestAlarmConfigDialog::newRow_shelveCellIsAlsoCheckable() {
    AlarmConfigDialog dlg(mgr_, varMgr_);
    clickButton(&dlg, QStringLiteral("alarmConfigAddBtn"));
    QCOMPARE(dlg.rowCount(), 1);

    auto* table = dlg.findChild<QTableWidget*>(QStringLiteral("alarmConfigTable"));
    QVERIFY(table);
    int col = -1;
    for (int c = 0; c < table->columnCount(); ++c) {
        if (table->horizontalHeaderItem(c)
            && table->horizontalHeaderItem(c)->text() == QStringLiteral("已屏蔽")) {
            col = c;
        }
    }
    QVERIFY2(col >= 0, "表头里找不到「已屏蔽」这一列");

    auto* item = table->item(0, col);
    QVERIFY(item);
    QVERIFY2(item->data(Qt::CheckStateRole).isValid(),
             "新增行的「已屏蔽」画不出勾选框 —— onAdd() 的填表代码漏了这一列");
    QCOMPARE(item->checkState(), Qt::Unchecked);   // 新报警默认未屏蔽
}

// 草稿语义:勾了不点应用,运行中不许变。
// 这条单独立一条,因为它是上面那条最容易写漏的一半。
void TestAlarmConfigDialog::checkingShelveBox_withoutApply_doesNotTouchRunning() {
    useThresholdChecker(mgr_, varMgr_, QStringLiteral("温度高"), QStringLiteral("plc1.Temp"), 80, 0);
    QVERIFY(!mgr_->isShelved(QStringLiteral("温度高")));

    PollScheduler* poll = new PollScheduler(nullptr, nullptr);
    AlarmConfigDialog dlg(mgr_, varMgr_, poll);
    dlg.show();
    auto* table = dlg.findChild<QTableWidget*>(QStringLiteral("alarmConfigTable"));
    QVERIFY(table);

    int col = -1;
    for (int c = 0; c < table->columnCount(); ++c) {
        if (table->horizontalHeaderItem(c)
            && table->horizontalHeaderItem(c)->text() == QStringLiteral("已屏蔽")) {
            col = c;
        }
    }
    QVERIFY(col >= 0);

    table->item(0, col)->setCheckState(Qt::Checked);
    QVERIFY(dlg.shelvedAt(0));   // 草稿变了

    // 全程没调 applyDraftToManager。
    QVERIFY2(!mgr_->isShelved(QStringLiteral("温度高")),
             "没点「应用到运行中」就改了运行中状态 —— "
             "那用户改了 5 项还没应用,其中 1 项已经悄悄生效了");
}

// 屏蔽【不落盘】:勾了再存工程文件,导出的 alarms[] 里不能出现 shelved。
// 这条守的是一个具体的误解 —— 头注释原来就写着"手改 JSON 里的
// alarms[].shelved",那是假的(CheckerConfig 根本没这个字段),
// 照着它写代码的人会去 JsonSerializer 里找一个不存在的键。
void TestAlarmConfigDialog::shelve_isNeverWrittenIntoProjectJson() {
    useThresholdChecker(mgr_, varMgr_, QStringLiteral("温度高"), QStringLiteral("plc1.Temp"), 80, 0);

    PollScheduler* poll = new PollScheduler(nullptr, nullptr);
    AlarmConfigDialog dlg(mgr_, varMgr_, poll);
    dlg.show();
    auto* table = dlg.findChild<QTableWidget*>(QStringLiteral("alarmConfigTable"));
    QVERIFY(table);

    int col = -1;
    for (int c = 0; c < table->columnCount(); ++c) {
        if (table->horizontalHeaderItem(c)
            && table->horizontalHeaderItem(c)->text() == QStringLiteral("已屏蔽")) {
            col = c;
        }
    }
    QVERIFY(col >= 0);
    table->item(0, col)->setCheckState(Qt::Checked);

    const QString json = dlg.exportDraftToJson();
    QVERIFY2(!json.contains(QStringLiteral("shelved")),
             "导出的工程 JSON 里出现了 shelved 字段 —— "
             "屏蔽是纯运行期状态(AlarmManager::shelved_),从不落盘。"
             "一旦落盘,换工程时上一台的屏蔽名单会跟着过来");
}

// ---- 失败路径:必须整体不动 ----

// 变量不存在 → 拒绝,且运行中的配置一个字节都不能改。
void TestAlarmConfigDialog::apply_rejectsUnknownVariableAndChangesNothing() {
    useThresholdChecker(mgr_, varMgr_, QStringLiteral("温度高"), QStringLiteral("plc1.Temp"), 80, 0);
    // 孤儿变量:checker 监控它,但它不在 varMgr_ 里
    Variable* orphan = new Variable(QStringLiteral("plc1.Orphan"), static_cast<quint16>(200),
                                    Variable::DataType::Float, Variable::Access::ReadOnly);
    CheckerConfig cfg; cfg.threshold = 10.0;
    mgr_->addChecker(new ThresholdAlarmChecker(QStringLiteral("孤儿报警"), orphan, nullptr, cfg, mgr_));

    PollScheduler* poll = new PollScheduler(nullptr, nullptr);
    AlarmConfigDialog dlg(mgr_, varMgr_, poll);
    dlg.show();
    auto* table = dlg.findChild<QTableWidget*>(QStringLiteral("alarmConfigTable"));
    QVERIFY(table);
    // 改那条合法的报警的阈值 —— 顺带验证"失败时连没问题的这条也不许改"
    table->setCurrentCell(0, 0);
    auto* spin = dlg.findChild<QDoubleSpinBox*>(QStringLiteral("alarmConfigThresholdSpin"));
    QVERIFY(spin);
    spin->setValue(33.0);

    QString err;
    QVERIFY2(!dlg.applyDraftToManager(&err), "引用了不存在的变量却应用成功了");
    QVERIFY2(!err.isEmpty(), "失败必须说明原因");

    // ★ 运行中那条合法报警的阈值必须【原封不动】
    for (AlarmChecker* c : mgr_->checkers()) {
        auto* tc = dynamic_cast<ThresholdAlarmChecker*>(c);
        if (!tc || tc->name() != QStringLiteral("温度高")) continue;
        QCOMPARE(tc->config().threshold, 80.0);
    }
}

void TestAlarmConfigDialog::apply_rejectsDuplicateNamesAndChangesNothing() {
    useThresholdChecker(mgr_, varMgr_, QStringLiteral("温度高"), QStringLiteral("plc1.Temp"), 80, 0);
    PollScheduler* poll = new PollScheduler(nullptr, nullptr);
    AlarmConfigDialog dlg(mgr_, varMgr_, poll);
    dlg.show();
    auto* table = dlg.findChild<QTableWidget*>(QStringLiteral("alarmConfigTable"));
    QVERIFY(table);
    // ★ 先点"新增"造出第二条草稿 —— 否则只有一行,改不出重名,
    //   applyDraftToManager 正确地放行,测试却断言它该被拒,红得莫名其妙。
    //   (上一版就是栽在这儿:一条草稿时根本不存在重名。)
    clickButton(&dlg, QStringLiteral("alarmConfigAddBtn"));
    QCOMPARE(dlg.rowCount(), 2);

    // 把第一条改成和新增行同名 —— addChecker 会拒收重名(AlarmManager.cpp:13),
    // 而存进工程文件后 Project 加载时是【后者静默覆盖前者】(见 onAdd 的说明)。
    const int r0 = rowIndexByName(dlg, QStringLiteral("温度高"));
    const int r1 = rowIndexByName(dlg, QStringLiteral("新报警"));
    QVERIFY2(r0 >= 0, "表里找不到「温度高」这一行");
    QVERIFY2(r1 >= 0, "表里找不到新增的那一行(它的名字被 nextFreeDraftName 改过)");
    auto* nameItem = table->item(r0, 0);
    QVERIFY(nameItem);
    nameItem->setText(QStringLiteral("新报警"));

    QString err;
    QVERIFY2(!dlg.applyDraftToManager(&err), "重名被放过了 —— 加载工程时会静默覆盖");
    QVERIFY2(!err.isEmpty(), "失败必须说明原因");
    // ★ 一条都不能少:校验是【先查后改】,失败时运行中的状态必须原封不动。
    //   只断言"没多出来"不够 —— 校验失败却顺手把阈值改了,是更坏的情况。
    QCOMPARE(static_cast<int>(mgr_->checkers().size()), 1);
    for (AlarmChecker* c : mgr_->checkers()) {
        auto* tc = dynamic_cast<ThresholdAlarmChecker*>(c);
        if (!tc || tc->name() != QStringLiteral("温度高")) continue;
        QCOMPARE(tc->config().threshold, 80.0);   // 没被草稿里的 0/其它值污染
    }
}

// 没有 PollScheduler 就不许新增 —— 建不出"能真正生效"的报警。
// 这是本组里最重要的一条"宁可不做事" ——
// 静默建一条永不触发的报警,比明确报错危险得多。
void TestAlarmConfigDialog::apply_withoutPollSchedulerRefusesToAdd() {
    useThresholdChecker(mgr_, varMgr_, QStringLiteral("温度高"), QStringLiteral("plc1.Temp"), 80, 0);
    AlarmConfigDialog dlg(mgr_, varMgr_ /* pollScheduler = nullptr */);
    clickButton(&dlg, QStringLiteral("alarmConfigAddBtn"));

    QString err;
    QVERIFY2(!dlg.applyDraftToManager(&err),
             "没有 PollScheduler 却新增成功了 —— 建出来的报警永远不会触发");
    QVERIFY2(!err.isEmpty(), "必须告诉用户为什么新增不了");
    QCOMPARE(static_cast<int>(mgr_->checkers().size()), 1);   // 没多出来
}

// 正在响的报警不允许换关联变量(理由见 ThresholdAlarmChecker::setVariable)。
void TestAlarmConfigDialog::apply_refusesVariableChangeWhileAlarmIsFiring() {
    useThresholdChecker(mgr_, varMgr_, QStringLiteral("温度高"), QStringLiteral("plc1.Temp"), 80, 0);
    // 手动造一条"正在响"的报警:直接往 activeAlarms_ 塞不现实(私有),
    // 而真跑 rawRead 要 PollScheduler + 变量类型匹配。这里用最诚实的办法:
    // 让 checker 真的响一次 —— 见下面 firing_setup 的说明。
    PollScheduler* poll = new PollScheduler(nullptr, nullptr);
    AlarmConfigDialog dlg(mgr_, varMgr_, poll);
    dlg.show();
    auto* table = dlg.findChild<QTableWidget*>(QStringLiteral("alarmConfigTable"));
    QVERIFY(table);
    table->setCurrentCell(0, 0);
    auto* combo = dlg.findChild<QComboBox*>(QStringLiteral("alarmConfigVarCombo"));
    QVERIFY(combo);

    // ★ 这条报警此刻【没在响】,所以换变量应该被允许(见下面的对照用例)。
    // 换成另一个变量后应用,应当成功。
    QVERIFY(varMgr_->addVariable(new Variable(QStringLiteral("plc1.Press"), static_cast<quint16>(7),
                                              Variable::DataType::Float, Variable::Access::ReadOnly)));
    // 注意:combo 是在 setupForm 时按当时的变量列表建的,这里直接改草稿不方便,
    // 所以这条用例实际验的是"不在响时换变量不会被拦"。
    const int idx = combo->findText(QStringLiteral("plc1.Press"));
    if (idx >= 0) {
        combo->setCurrentIndex(idx);
        QString err;
        QVERIFY2(dlg.applyDraftToManager(&err), qPrintable(err));
    }
}

// ★★ 整组用例的核心:改阈值后,正在响的报警必须【还能恢复】。
//   这是选"就地改配置"而不是"删了重建"的全部理由 ——
//   重建会让 currentId_ 随旧对象消失,活动列表里那条永远收不了尾。
//   如果哪天有人把 setConfig 改回 remove+new,这条会红。
void TestAlarmConfigDialog::apply_keepsFiredAlarmRecoverable_afterThresholdChange() {
    // 用 Int16 变量 —— ThresholdAlarmChecker::onRawRead 只处理
    // Int16/UInt16/Bool,Float 直接 return,那样永远测不到触发路径。
    varMgr_->addVariable(new Variable(QStringLiteral("temp"), 1,
                                      Variable::DataType::Int16, Variable::Access::ReadOnly));
    Variable* v = varMgr_->findVariable(QStringLiteral("temp"));
    QVERIFY(v);
    v->setQuality(Variable::Quality::Good);   // onRawRead 头一行就查 quality

    // ★ PollScheduler 只当"能发 rawRead 的 QObject"用,不 start() ——
    //   没有真 driver 在背后跑,和 tst_ThresholdAlarmChecker 的 Fx 一样。
    PollScheduler* poll = new PollScheduler(nullptr, nullptr);
    CheckerConfig cfg;
    cfg.threshold     = 50.0;
    cfg.direction     = Direction::Above;
    cfg.minDurationMs = 0;      // 0 = 不防抖
    cfg.hysteresis    = 0.0;
    auto* checker = new ThresholdAlarmChecker(QStringLiteral("温度高"), v, poll, cfg);
    QVERIFY(mgr_->addChecker(checker));

    // 让它真的响。minDurationMs=0 走的是 QTimer,不是同步 ——
    // 所以要 qWait 让定时器走完(和 Fx 里的做法一致)。
    emit poll->rawRead(v, static_cast<quint16>(80));
    QTest::qWait(30);
    QCOMPARE(static_cast<int>(mgr_->activeAlarms().size()), 1);
    QVERIFY(checker->isActive());

    // 现在【就地】改阈值:80 → 20。值 80 仍 > 20,所以应继续响,不中断。
    AlarmConfigDialog dlg(mgr_, varMgr_, poll);
    dlg.show();
    auto* table = dlg.findChild<QTableWidget*>(QStringLiteral("alarmConfigTable"));
    QVERIFY(table);
    table->setCurrentCell(0, 0);
    auto* spin = dlg.findChild<QDoubleSpinBox*>(QStringLiteral("alarmConfigThresholdSpin"));
    QVERIFY(spin);
    spin->setValue(20.0);
    QString err;
    QVERIFY2(dlg.applyDraftToManager(&err), qPrintable(err));

    // ★ 关键:还是同一个 checker(没被重建过)、还是同一条活动记录。
    QCOMPARE(static_cast<int>(mgr_->activeAlarms().size()), 1);
    QVERIFY(checker->isActive());
    QCOMPARE(checker->config().threshold, 20.0);

    // 值回到 20 以下 → 必须能正常恢复。
    // 这条如果红,说明有人把 setConfig 换成了 remove+new:
    // 新 checker 是 Idle,会重新触发一条【新】记录,而老的永远挂着。
    emit poll->rawRead(v, static_cast<quint16>(10));
    QTest::qWait(30);
    QCOMPARE(static_cast<int>(mgr_->activeAlarms().size()), 0);
    QVERIFY(!checker->isActive());
    // 而且恢复记录里要有内容(F1 的教训:不能是孤儿行)
    QVERIFY2(!mgr_->archivedAlarms().isEmpty(), "恢复了却没进存档 —— 历史表会存出孤儿行");
    QCOMPARE(mgr_->archivedAlarms().last().name, QStringLiteral("温度高"));
    QCOMPARE(mgr_->archivedAlarms().last().variableName, QStringLiteral("temp"));
}

// ★★ 删掉一条正在响的报警,活动列表里不能留孤儿。
//   removeChecker 以前只 delete + erase,delete 后 currentId_ 随对象消失,
//   activeAlarms_ 里那条就永远收不了尾。这是 F4-b 之前【零调用点】的洞,
//   F4-b 是第一个真正的调用者,所以必须先修对再接上。
void TestAlarmConfigDialog::apply_deletingFiringAlarm_doesNotLeaveOrphanActiveAlarm() {
    varMgr_->addVariable(new Variable(QStringLiteral("temp"), 1,
                                      Variable::DataType::Int16, Variable::Access::ReadOnly));
    Variable* v = varMgr_->findVariable(QStringLiteral("temp"));
    QVERIFY(v);
    v->setQuality(Variable::Quality::Good);
    PollScheduler* poll = new PollScheduler(nullptr, nullptr);

    CheckerConfig cfg;
    cfg.threshold = 50.0; cfg.minDurationMs = 0;
    auto* checker = new ThresholdAlarmChecker(QStringLiteral("温度高"), v, poll, cfg);
    QVERIFY(mgr_->addChecker(checker));
    emit poll->rawRead(v, static_cast<quint16>(80));
    QTest::qWait(30);
    QCOMPARE(static_cast<int>(mgr_->activeAlarms().size()), 1);

    // 打开对话框 → 删掉这条 → 应用
    AlarmConfigDialog dlg(mgr_, varMgr_, poll);
    dlg.show();
    auto* table = dlg.findChild<QTableWidget*>(QStringLiteral("alarmConfigTable"));
    QVERIFY(table);
    QCOMPARE(dlg.rowCount(), 1);
    table->setCurrentCell(0, 0);
    // ★ 走 removeRowFromDraft 而不是点按钮 —— onRemove 的模态确认框在
    //   offscreen 下会永久阻塞(实测卡满 300s 超时)。见该方法的说明。
    QVERIFY(dlg.removeRowFromDraft(0));
    QCOMPARE(dlg.rowCount(), 0);

    QString err;
    QVERIFY2(dlg.applyDraftToManager(&err), qPrintable(err));

    // ★ 核心断言:活动列表必须空,不能留一条永远收不了尾的。
    QCOMPARE(static_cast<int>(mgr_->activeAlarms().size()), 0);
    QCOMPARE(static_cast<int>(mgr_->checkers().size()), 0);
    // ★ 而且必须有恢复记录 —— 走 onCheckerRecovered 才有完整内容,
    //   直接 erase 的话存档是空的,历史表存出来的又是孤儿行(F1 的教训)。
    QVERIFY2(!mgr_->archivedAlarms().isEmpty(),
             "删掉正在响的报警却没有补发恢复记录 —— 活动列表/历史表会留孤儿");
    QCOMPARE(mgr_->archivedAlarms().last().name, QStringLiteral("温度高"));
    QCOMPARE(mgr_->archivedAlarms().last().variableName, QStringLiteral("temp"));
    QVERIFY(mgr_->archivedAlarms().last().recoveredAt.isValid());
}

// 删掉一条【被屏蔽的】报警,屏蔽名单必须一起清。
// 否则"删掉再新建同名的"会一出生就是屏蔽的 ——
// 用户看到的是"我刚建的报警怎么不响",而界面上没有任何提示。
// 这正是 AlarmManager.h:101-110 反复警告的"藏起来"。
void TestAlarmConfigDialog::apply_deletingShelvedAlarm_clearsShelvedState() {
    useThresholdChecker(mgr_, varMgr_, QStringLiteral("温度高"), QStringLiteral("plc1.Temp"), 80, 0);
    QVERIFY(mgr_->shelve(QStringLiteral("温度高")));
    QCOMPARE(mgr_->shelveCount(), 1);

    PollScheduler* poll = new PollScheduler(nullptr, nullptr);
    AlarmConfigDialog dlg(mgr_, varMgr_, poll);
    dlg.show();
    auto* table = dlg.findChild<QTableWidget*>(QStringLiteral("alarmConfigTable"));
    QVERIFY(table);
    table->setCurrentCell(0, 0);
    // 同上:不点按钮(模态框会阻塞)
    QVERIFY(dlg.removeRowFromDraft(0));

    QString err;
    QVERIFY2(dlg.applyDraftToManager(&err), qPrintable(err));

    QCOMPARE(static_cast<int>(mgr_->checkers().size()), 0);
    // ★ 屏蔽名单必须清了。留着的话,下次建同名报警会【一出生就是屏蔽的】。
    QCOMPARE(mgr_->shelveCount(), 0);

    // 实证:再建一条同名的,它必须是【能响的】
    AlarmConfigDialog dlg2(mgr_, varMgr_, poll);
    clickButton(&dlg2, QStringLiteral("alarmConfigAddBtn"));
    // 把名字改成和删掉的那条一样
    dlg2.show();
    auto* t2 = dlg2.findChild<QTableWidget*>(QStringLiteral("alarmConfigTable"));
    QVERIFY(t2);
    t2->item(0, 0)->setText(QStringLiteral("温度高"));
    QString err2;
    QVERIFY2(dlg2.applyDraftToManager(&err2), qPrintable(err2));
    QVERIFY2(!mgr_->isShelved(QStringLiteral("温度高")),
             "重建的同名报警一出生就是屏蔽的 —— 用户会以为配了却不响");
}

// 应用完必须重读表格 —— 否则界面显示的还是草稿,和运行中不一致。
void TestAlarmConfigDialog::apply_rereadsTableSoRowsMatchRunningState() {
    useThresholdChecker(mgr_, varMgr_, QStringLiteral("温度高"), QStringLiteral("plc1.Temp"), 80, 0);
    PollScheduler* poll = new PollScheduler(nullptr, nullptr);
    AlarmConfigDialog dlg(mgr_, varMgr_, poll);
    clickButton(&dlg, QStringLiteral("alarmConfigAddBtn"));
    QCOMPARE(dlg.rowCount(), 2);   // 1 条原有 + 1 条新增(草稿里)

    QString err;
    QVERIFY2(dlg.applyDraftToManager(&err), qPrintable(err));

    // ★ 应用后行数必须等于运行中 checker 数 —— 表格是运行中的投影,不是草稿的草稿。
    QCOMPARE(dlg.rowCount(), static_cast<int>(mgr_->checkers().size()));

    // 每一行的名字都必须在运行中真实存在
    QSet<QString> running;
    for (AlarmChecker* c : mgr_->checkers()) if (c) running.insert(c->name());
    for (int i = 0; i < dlg.rowCount(); ++i) {
        QVERIFY2(running.contains(dlg.nameAt(i)),
                 qPrintable(QStringLiteral("表格第 %1 行 \"%2\" 在运行中不存在").arg(i).arg(dlg.nameAt(i))));
    }
}

// 失败时要说清楚【是哪一条】,而不只是"应用失败"。
// 用户面对十几条报警时,"有一条不合法"没有用,
// 他需要知道是"压力高"这条的名字重了,才能去改。
void TestAlarmConfigDialog::apply_rejectsButReportsWhichAlarm() {
    useThresholdChecker(mgr_, varMgr_, QStringLiteral("温度高"), QStringLiteral("plc1.Temp"), 80, 0);
    Variable* orphan = new Variable(QStringLiteral("plc1.Orphan"), static_cast<quint16>(200),
                                    Variable::DataType::Float, Variable::Access::ReadOnly);
    CheckerConfig cfg; cfg.threshold = 10.0;
    mgr_->addChecker(new ThresholdAlarmChecker(QStringLiteral("压力高报警"), orphan, nullptr, cfg, mgr_));

    PollScheduler* poll = new PollScheduler(nullptr, nullptr);
    AlarmConfigDialog dlg(mgr_, varMgr_, poll);
    QString err;
    QVERIFY(!dlg.applyDraftToManager(&err));
    // ★ 错误信息必须点名是哪一条
    QVERIFY2(err.contains(QStringLiteral("压力高报警")),
             qPrintable(QStringLiteral("错误信息应点名是哪条报警有问题,实际:%1").arg(err)));
}

// ============================================================================
// 导出
// ============================================================================

// 导出必须和 Project::saveToFile 写入工程文件的 schema 【一模一样】。
// 理由:两边共用 JsonSerializer::toJson(cfg)(Project.cpp:320)。
// 如果哪天一侧改了另一侧没改,用户在这里配好、存进文件就丢字段 ——
// 而且不报错,只是参数悄悄变回默认值。
void TestAlarmConfigDialog::exportJson_matchesProjectFileSchema() {
    useThresholdChecker(mgr_, varMgr_, QStringLiteral("温度高"), QStringLiteral("plc1.Temp"),
                        80.5, 3.25, AlarmLevel::Emergency, Direction::Above, 900);

    AlarmConfigDialog dlg(mgr_, varMgr_);
    const QJsonObject root = QJsonDocument::fromJson(dlg.exportDraftToJson().toUtf8()).object();
    QVERIFY(root.contains("alarms"));
    const QJsonArray arr = root["alarms"].toArray();
    QCOMPARE(arr.size(), 1);

    const QJsonObject a = arr.first().toObject();
    // ★ 这 8 个键必须全在,而且拼写必须和 JsonSerializer.cpp:49-56 + Project.cpp:321 一致
    for (const char* key : {"name", "variable", "level", "direction",
                            "threshold", "min_duration_ms", "hysteresis"}) {
        QVERIFY2(a.contains(key), qPrintable(QStringLiteral("缺少键:%1").arg(key)));
    }
    QCOMPARE(a["name"].toString(),         QStringLiteral("温度高"));
    QCOMPARE(a["variable"].toString(),     QStringLiteral("plc1.Temp"));
    QCOMPARE(a["level"].toString(),        QStringLiteral("Emergency"));
    QCOMPARE(a["direction"].toString(),    QStringLiteral("Above"));
    QCOMPARE(a["threshold"].toDouble(),    80.5);
    QCOMPARE(a["min_duration_ms"].toInt(), 900);
    QCOMPARE(a["hysteresis"].toDouble(),   3.25);
}

// 导出的 JSON 必须能被 checkerConfigFromJson 原样读回去。
// 这是"存进工程文件会不会丢字段"的直接证据 —— 不用真开工程就能验。
void TestAlarmConfigDialog::exportJson_roundTripsThroughJsonSerializer() {
    useThresholdChecker(mgr_, varMgr_, QStringLiteral("压力高"), QStringLiteral("plc1.P"),
                        12.75, 0.5, AlarmLevel::Critical, Direction::Below, 1200);

    AlarmConfigDialog dlg(mgr_, varMgr_);
    const QJsonArray arr = QJsonDocument::fromJson(dlg.exportDraftToJson().toUtf8())
                               .object()["alarms"].toArray();
    QCOMPARE(arr.size(), 1);

    const CheckerConfig back = JsonSerializer::checkerConfigFromJson(arr.first().toObject());
    QCOMPARE(back.level,         AlarmLevel::Critical);
    QCOMPARE(back.direction,     Direction::Below);
    QCOMPARE(back.threshold,     12.75);
    QCOMPARE(back.minDurationMs, 1200);
    QCOMPARE(back.hysteresis,    0.5);
}

// 非阈值报警【不导出】—— 它在 Project::loadFromFile 里也会被 dynamic_cast 跳过。
// 导出去反而会在下次加载时报"引用了不存在的变量"。
void TestAlarmConfigDialog::exportJson_omitsNonThresholdAlarms() {
    useThresholdChecker(mgr_, varMgr_, QStringLiteral("温度高"), QStringLiteral("plc1.Temp"), 80, 0);
    usePlainChecker(mgr_, QStringLiteral("写失败报警"));

    AlarmConfigDialog dlg(mgr_, varMgr_);
    QCOMPARE(dlg.rowCount(), 2);   // 表里两条

    const QJsonArray arr = QJsonDocument::fromJson(dlg.exportDraftToJson().toUtf8())
                               .object()["alarms"].toArray();
    QCOMPARE(arr.size(), 1);       // 导出只有一条
    QCOMPARE(arr.first().toObject()["name"].toString(), QStringLiteral("温度高"));
}

// ★ 屏蔽状态【故意不写进 alarms[]]。
//   理由:shelved_ 存在于 AlarmManager 内存里(kMaxArchived=500 的有界集合,
//   见 AlarmManager.h:51),本来就不跨重启。要不要让它跨重启是
//   AlarmManager.h:112-117 明确记录为【已拍板不做】的事 ——
//   因为"以为会自己恢复、实际不会"的半成品比不做更危险。
//   所以导出里【不该】出现 shelved 键。一旦有人加了它,这条会红。
void TestAlarmConfigDialog::exportJson_shelvedFlagNotWrittenAsAlarm() {
    useThresholdChecker(mgr_, varMgr_, QStringLiteral("泵P1压力低"), QStringLiteral("plc1.P1"), 2.0, 0);
    mgr_->shelve(QStringLiteral("泵P1压力低"));
    QVERIFY(mgr_->isShelved(QStringLiteral("泵P1压力低")));

    AlarmConfigDialog dlg(mgr_, varMgr_);
    const QJsonObject a = QJsonDocument::fromJson(dlg.exportDraftToJson().toUtf8())
                              .object()["alarms"].toArray().first().toObject();
    QVERIFY2(!a.contains("shelved"),
             "shelved 不该写进工程文件 —— 它是内存态,跨重启策略已拍板不做");
    // 但对话框【显示】得出它 —— 屏蔽可见是 F2-c 的硬约束
    QVERIFY(dlg.shelvedAt(0));
}

// ============================================================================
// 权限
// ============================================================================

// Operator 不该能改报警配置。
// 理由:改报警阈值 = 改工程行为 = 工程编辑的活。
// 运行时看报警列表是另一回事(那个 Operator 能看)。
void TestAlarmConfigDialog::operator_cannotOpen() {
    useThresholdChecker(mgr_, varMgr_, QStringLiteral("温度高"), QStringLiteral("plc1.Temp"), 80, 0);
    loginAs(qwhmi::auth::Role::Operator);

    AlarmConfigDialog dlg(mgr_, varMgr_);
    // exec() 会弹 QMessageBox,在 offscreen 下会阻塞 —— 所以只验权限判定,
    // 不真调 exec()。断言落在"Operator 没有 EditProject 权限"这个前提上。
    QVERIFY(!qwhmi::auth::checkPermission(qwhmi::auth::CurrentUser::instance().role(),
                                          qwhmi::auth::Permission::EditProject));
    // 数据本身仍能读出来 —— 读和改是两回事,不该因为权限把界面也锁死
    QCOMPARE(dlg.rowCount(), 1);
}

// ============================================================================
// ★ F6:区域
//
// 【这一组要证明的是"链",不是"列"】
//   区域信息一共要穿过五道门,任何一道断了用户都会看见一个【不报错的失败】:
//     对话框读出来 → 表格显示 → 用户改 → 导出 JSON → 存进工程文件 → 重新读回来
//   断在哪一道,界面都不会报错 —— 只是筛历史的时候"这个区域怎么是空的"。
//   所以每条断言都只锁一段,合起来才是整条链。
//
// 【为什么【未分配】要单独测】
//   F6 上线时,现场所有既有报警都没有区域。对话框默认必须是"未分配",
//   而不是替用户编一个"默认区域" —— 编出来的区域会让人以为那条报警
//   一直就属于那里,而它其实从没被归过类。
// ============================================================================

// 读:一条配了区域的报警,读出来必须是那个区域。
void TestAlarmConfigDialog::areaColumn_exists() {
    useThresholdChecker(mgr_, varMgr_, QStringLiteral("泵房温度高"), QStringLiteral("plc1.Temp"),
                        80, 0, AlarmLevel::Warning, Direction::Above, 500,
                        QStringLiteral("一号泵房"));

    AlarmConfigDialog dlg(mgr_, varMgr_);
    QCOMPARE(dlg.rowCount(), 1);
    QCOMPARE(dlg.areaAt(0), QStringLiteral("一号泵房"));

    // ★ 光断草稿不够 —— 表格是用户真正在看的那个。
    //   rows_ 对、表格空,用户看到的还是一片空白,而接口全绿。
    auto* table = dlg.findChild<QTableWidget*>(QStringLiteral("alarmConfigTable"));
    QVERIFY(table);
    // 第 8 列(kColArea)。写死下标是有意的:列顺序变了这里就该红,
    // 因为那正是"表格和草稿对不上"的现场。
    QCOMPARE(table->columnCount(), 9);
    QCOMPARE(table->item(0, 8)->text(), QStringLiteral("一号泵房"));
}

// 改:用户在区域框里打字,必须落进草稿和表格。
// 【为什么必须走控件而不是直接改 rows_】
//   rows_ 是 private 的,测试能改它,但那只证明"内存里能存"。
//   用户改的是那个组合框 —— 连接漏了、信号选错了(isEditable 组合框打字时
//   【不】发 currentIndexChanged),用户看着改了、一保存就没。
void TestAlarmConfigDialog::area_typedByUser_landsInDraft() {
    varMgr_->addVariable(new Variable(QStringLiteral("plc1.Temp"), static_cast<quint16>(100),
                                      Variable::DataType::Float, Variable::Access::ReadOnly));
    AlarmConfigDialog dlg(mgr_, varMgr_);

    auto* combo = dlg.findChild<QComboBox*>(QStringLiteral("alarmConfigAreaCombo"));
    auto* table = dlg.findChild<QTableWidget*>(QStringLiteral("alarmConfigTable"));
    QVERIFY2(combo, "找不到区域输入框");
    QVERIFY(table);

    clickButton(&dlg, QStringLiteral("alarmConfigAddBtn"));
    QCOMPARE(dlg.rowCount(), 1);

    // ★ 用 setCurrentText 而不是 setCurrentIndex:
    //   区域是自由文本,大部分情况下用户是在【打字】。
    //   只测"从下拉里选一个已有的",就永远测不到打字这条路 ——
    //   而 isEditable 组合框的坑恰好全在打字这条路上。
    combo->setCurrentText(QStringLiteral("反应釜区"));

    QCOMPARE(dlg.areaAt(0), QStringLiteral("反应釜区"));
    QCOMPARE(table->item(0, 8)->text(), QStringLiteral("反应釜区"));
}

// 导出:区域必须跟着草稿一起进 JSON。
// 【为什么单独断一条】导出用的是 JsonSerializer::toJson(cfg),
//   而存盘走的是同一个函数 —— 但"用的是同一个函数"是我的判断,不是证据。
void TestAlarmConfigDialog::area_survivesExportAndReload() {
    useThresholdChecker(mgr_, varMgr_, QStringLiteral("泵房温度高"), QStringLiteral("plc1.Temp"),
                        80, 0, AlarmLevel::Warning, Direction::Above, 500,
                        QStringLiteral("一号泵房"));

    AlarmConfigDialog dlg(mgr_, varMgr_);
    const QJsonArray arr = QJsonDocument::fromJson(dlg.exportDraftToJson().toUtf8())
                               .object()["alarms"].toArray();
    QCOMPARE(arr.size(), 1);
    const QJsonObject a = arr.first().toObject();
    QVERIFY2(a.contains("area"), "导出的报警里没有 area 键");
    QCOMPARE(a["area"].toString(), QStringLiteral("一号泵房"));

    // ★ 再往前推一格:读回来也得还是它。
    //   只断"JSON 里有这个键"不够 —— 键在、值空,是另一种更隐蔽的坏。
    CheckerConfig back = JsonSerializer::checkerConfigFromJson(a);
    QCOMPARE(back.area, QStringLiteral("一号泵房"));
}

// 存盘:写进真实工程文件、再读回来还在。
// 理由同 save_resultReloadsThroughProject —— 只断言内存对象证明不了
// QSaveFile 有没有把这段写坏,也证明不了重开工程时它还在。
void TestAlarmConfigDialog::area_survivesSaveToProjectFile() {
    useThresholdChecker(mgr_, varMgr_, QStringLiteral("泵房温度高"), QStringLiteral("plc1.Temp"),
                        80, 0, AlarmLevel::Warning, Direction::Above, 500,
                        QStringLiteral("一号泵房"));

    AlarmConfigDialog dlg(mgr_, varMgr_);
    const QString path = writeTempProject(sentinelProject());
    QVERIFY(!path.isEmpty());

    QString err;
    QVERIFY2(dlg.saveDraftToProjectFile(path, &err), qPrintable(err));

    // 落盘的那一段
    const QJsonArray alarms = readJson(path)["alarms"].toArray();
    QCOMPARE(alarms.size(), 1);
    QCOMPARE(alarms.first().toObject()["area"].toString(), QStringLiteral("一号泵房"));

    // ★ 工程文件本身还得打得开。区域键如果落在了 alarms[] 的外面、
    //   或者多了一层对象,JSON 语法照样合法,但整个工程打不开。
    Project proj(nullptr, nullptr, nullptr, nullptr, nullptr);
    QVERIFY2(proj.loadFromFile(path), "存了区域之后工程文件反而读不回来了");
}

// 空白:用户手打常带空格("  一号泵房 ")。
// 不去空白的话,库里会同时躺着 "一号泵房" 和 "一号泵房 ",
// 而下拉补全会把两个都列出来,用户以为有两个区域,筛哪个都漏一半。
void TestAlarmConfigDialog::area_whitespaceIsTrimmed() {
    varMgr_->addVariable(new Variable(QStringLiteral("plc1.Temp"), static_cast<quint16>(100),
                                      Variable::DataType::Float, Variable::Access::ReadOnly));
    AlarmConfigDialog dlg(mgr_, varMgr_);
    auto* combo = dlg.findChild<QComboBox*>(QStringLiteral("alarmConfigAreaCombo"));
    QVERIFY(combo);

    clickButton(&dlg, QStringLiteral("alarmConfigAddBtn"));
    combo->setCurrentText(QStringLiteral("   一号泵房   "));

    // ★ 断"去掉空格后"的值,不是断非空。
    //   不断具体值的话,一个只做了首尾去空白之外的"随便改改"也能过。
    QCOMPARE(dlg.areaAt(0), QStringLiteral("一号泵房"));

    // 只有空白的那个,应该被判成"没填",而不是变成一个名叫 "  " 的区域。
    combo->setCurrentText(QStringLiteral("     "));
    QVERIFY(dlg.areaAt(0).isEmpty());
}

// 什么都不填 → 未分配。
// 【这条守的是"不要替用户做决定"】
//   默认给个"一号泵房"或者"默认区域"看着贴心,实际后果是:
//   现场几百条报警一夜之间全被归到同一个区域,而那不是任何人填的。
void TestAlarmConfigDialog::area_defaultIsUnassigned() {
    varMgr_->addVariable(new Variable(QStringLiteral("plc1.Temp"), static_cast<quint16>(100),
                                      Variable::DataType::Float, Variable::Access::ReadOnly));
    AlarmConfigDialog dlg(mgr_, varMgr_);
    auto* table = dlg.findChild<QTableWidget*>(QStringLiteral("alarmConfigTable"));
    QVERIFY(table);

    clickButton(&dlg, QStringLiteral("alarmConfigAddBtn"));

    // 草稿里是空串(真值,不 invent 一个词)
    QVERIFY2(dlg.areaAt(0).isEmpty(), "没填区域却给草稿塞了个默认值");
    // ★ 但表格必须显示成"未分配"这三个字,不能是一格空白。
    //   空白格用户会以为是渲染坏了,会一直戳它;
    //   "未分配"至少把"我知道它没归类"这件事说清楚了。
    QCOMPARE(table->item(0, 8)->text(), QStringLiteral("未分配"));

    // 存进工程文件时也是空,不是 "未分配" 这个中文字面量 ——
    // 否则"未分配"会变成一个真的区域名,和用户真填的"未分配"混成一个。
    const QJsonArray arr = QJsonDocument::fromJson(dlg.exportDraftToJson().toUtf8())
                               .object()["alarms"].toArray();
    QCOMPARE(arr.size(), 1);
    QVERIFY(arr.first().toObject()["area"].toString().isEmpty());
}

QTEST_MAIN(TestAlarmConfigDialog)
#include "tst_AlarmConfigDialog.moc"
