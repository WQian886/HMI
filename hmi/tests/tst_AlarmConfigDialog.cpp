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
// 【★ 本文件要锁住的一个设计不变量】
//   这个对话框【绝不改动运行中的 AlarmManager】。
//   所以每条测试在断言完"对话框显示的对不对"之后,
//   都额外断言 AlarmManager 侧【原封不动】——
//   这条不变量一旦破了,F4-b 就没法独立验收了。
// ============================================================================

#include <QtTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QPushButton>
#include <QTableWidget>

#include "AlarmManager.h"
#include "JsonSerializer.h"
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
                             int minDurMs = 500);
    // 建一个非阈值 checker(模拟 WriteFailChecker)
    void usePlainChecker(AlarmManager* mgr, const QString& name);

    void loginAs(Role r);

    // ★ 真去点按钮 —— 不是直接调槽函数。
    //   【为什么必须这样】用户报的是"点了没反应"。直接调 onAdd() 只能证明
    //   onAdd 的逻辑对,证明不了"按钮 → 槽"这根线接上了、按钮没被禁用、
    //   也没被别的控件挡住。2026-10-08 这次就是这么漏的:11 条测试全绿,
    //   因为它们一条都没碰过按钮。
    void clickButton(AlarmConfigDialog* dlg, const QString& objectName);

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
                                                int minDurMs) {
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
// 删除按钮本身的连线由 clickButton 的"按钮存在且 enabled"部分覆盖。
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

QTEST_MAIN(TestAlarmConfigDialog)
#include "tst_AlarmConfigDialog.moc"
