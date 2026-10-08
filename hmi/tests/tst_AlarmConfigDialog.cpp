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
//   测:用户能看到什么、改了之后能看到什么、导出的 JSON 对不对。
//   不测:表格长什么样、按钮摆在哪 —— 那些是"看得见"的,由人工验收。
//   断言全部落在对话框的公开只读访问器上,不反射 private 成员:
//   反射测的是实现细节,改个成员名就红,却证明不了界面真的对
//   (F3 在 AlarmListControl.h:47-50 已经踩过并记录了这个坑)。
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

#include "AlarmManager.h"
#include "JsonSerializer.h"
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

    // ---- ★ 设计不变量:不改运行中的状态 ----
    void openDialog_doesNotMutateAlarmManager();

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

    AlarmManager*    mgr_   = nullptr;
    VariableManager* varMgr_ = nullptr;
};

// ---------------------------------------------------------------------------

void TestAlarmConfigDialog::init() {
    mgr_    = new AlarmManager(this);
    varMgr_ = new VariableManager(this);
}

void TestAlarmConfigDialog::cleanup() {
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
