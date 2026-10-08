// ============================================================================
// 阶段 F · F4(2026-10-08):报警历史查询视图
//
// ★ 这组测试要证明的是"查得准",不是"画得出来"
//   界面能显示是一回事,显示的是不是用户要的那几条是另一回事 ——
//   而后者才是"能查事故"和"有个表格"的区别。
//
// ★ 最要紧的是 refresh_reQuery_narrowsTableThatShrinks
//   第二遍查之前没清表的话,表格里会留着上一遍的行。
//   界面上看着"查到了 5 条",其实有 3 条是残留 ——
//   用户据此下结论("昨晚只有 2 次报警"),而数据是错的。
// ============================================================================

#include <QtTest>
#include <QComboBox>
#include <QDateTimeEdit>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>

#include "Alarm.h"
#include "AlarmManager.h"
#include "history/AlarmHistoryView.h"
#include "history/AlarmRecorder.h"
#include "auth/CurrentUser.h"
#include "auth/Permission.h"

class TestAlarmHistoryView : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void opensWithEmptyDatabase_saysSoExplicitly();
    void showsRecords_whenQueryMatches();
    void refresh_reQuery_narrowsTableThatShrinks();

    // ---- 时间范围过滤 ----
    void timeFilter_excludesRecordsOutsideRange();
    void quickRangeButton_setsAndQueriesImmediately();

    // ---- 等级过滤 ----
    void levelFilter_minLevelExcludesLighterAlarms();
    void levelFilter_defaultExcludesInfoLevel();
    void levelCombo_isLabeledAsMinimumNotExactMatch();

    // ---- 不许有的东西 ----
    void table_isNotEditable();
    void noRecorder_disablesInsteadOfShowingEmptyTable();

private:
    // 造一条真实落库的报警:走 AlarmRecorder::setSource → AlarmManager 信号,
    // 和生产一模一样。用 fakeQSqlQuery 之类的手写 insert 测的是我自己的代码。
    qint64 fireAlarm(const QString& name, AlarmLevel level, const QDateTime& when);
    bool   openRecorder();

    QString dbPath_;
    AlarmManager*    mgr_  = nullptr;
    // ★ 必须写全限定名:AlarmRecorder 在 qwhmi::history 里,而这个类是全局的。
    //   在成员声明处直接写 `AlarmRecorder*` 报的是"语法错误:缺少';'",
    //   报错信息离病根(少了命名空间)远得离谱。
    qwhmi::history::AlarmRecorder* rec_ = nullptr;
    // 用内存库,不碰 %APPDATA% 里用户真实的 history.db ——
    // 测试写用户的生产数据是不可原谅的。
    QStringList createdFiles_;
};

void TestAlarmHistoryView::init() {
    qwhmi::auth::User u;
    u.name = QStringLiteral("tester");
    u.role = qwhmi::auth::Role::Admin;   // acknowledge 需要 AcknowledgeAlarm
    qwhmi::auth::CurrentUser::instance().login(u);

    // QTEMP 下的唯一文件名,测完删掉。
    dbPath_ = QDir(QDir::tempPath())
                 .filePath(QStringLiteral("hmi-alarmhist-test-%1.db")
                                .arg(QDateTime::currentMSecsSinceEpoch()));
    createdFiles_ << dbPath_ << dbPath_ + QStringLiteral("-wal") << dbPath_ + QStringLiteral("-shm");

    mgr_ = new AlarmManager(nullptr);
    rec_ = new qwhmi::history::AlarmRecorder(nullptr);
}

void TestAlarmHistoryView::cleanup() {
    if (rec_) { rec_->close(); delete rec_; rec_ = nullptr; }
    if (mgr_) { delete mgr_; mgr_ = nullptr; }
    for (const QString& f : createdFiles_) QFile::remove(f);
}

bool TestAlarmHistoryView::openRecorder() {
    if (!rec_->open(dbPath_)) return false;
    rec_->setSource(mgr_);
    return true;
}

qint64 TestAlarmHistoryView::fireAlarm(const QString& name, AlarmLevel level,
                                       const QDateTime& when) {
    static qint64 next = 5000;
    const qint64 id = next++;
    // 直接建 checker 触发的路径:AlarmRecorder::onAlarmTriggered 会取
    // alarm.triggeredAt 作为时间,所以只要把 triggeredAt 设成想要的时刻即可 ——
    // 这样能造出"三天前的报警"而不必真的等三天。
    Alarm a;
    a.id_         = id;
    a.name        = name;
    a.level       = level;
    a.state       = AlarmState::Active;
    a.triggeredAt = when;
    a.triggerValue = QStringLiteral("88.5");
    a.threshold   = QStringLiteral("> 80.0");
    // 绕开 checker 直接发信号:AlarmRecorder 订阅的是 AlarmManager 的信号,
    // 而这里要控制的是时间戳,不是检测逻辑。
    QMetaObject::invokeMethod(rec_, "onAlarmTriggered", Qt::DirectConnection,
                              Q_ARG(Alarm, a));
    return id;
}

// ============================================================================
// 打开时的状态
// ============================================================================

void TestAlarmHistoryView::opensWithEmptyDatabase_saysSoExplicitly() {
    QVERIFY(openRecorder());

    qwhmi::history::AlarmHistoryView view(rec_);

    QCOMPARE(view.rowCount(), 0);
    // ★ 0 条必须【说清楚】,不能是一张空表。
    //   空表有两种解释(真的没报警 / 查错了),用户分辨不了,
    //   于是只能反复点查询、反复怀疑,是白花时间。
    QVERIFY2(view.statusText().contains(QStringLiteral("没有记录")),
             "查不到记录时必须在状态栏说明,而不是留一张空表");
}

void TestAlarmHistoryView::showsRecords_whenQueryMatches() {
    QVERIFY(openRecorder());
    const QDateTime now = QDateTime::currentDateTime();
    fireAlarm(QStringLiteral("温度高"), AlarmLevel::Critical, now.addSecs(-60));
    fireAlarm(QStringLiteral("压力低"), AlarmLevel::Warning,  now.addSecs(-120));

    qwhmi::history::AlarmHistoryView view(rec_);

    QCOMPARE(view.rowCount(), 2);
    // ★ 逐条点名断言,而不是只断条数。
    //   "有 2 条"可能是【同一条显示了两遍】,条数对不代表内容对。
    QSet<QString> names;
    for (int i = 0; i < view.rowCount(); ++i) names.insert(view.nameAt(i));
    QVERIFY(names.contains(QStringLiteral("温度高")));
    QVERIFY(names.contains(QStringLiteral("压力低")));
}

void TestAlarmHistoryView::refresh_reQuery_narrowsTableThatShrinks() {
    QVERIFY(openRecorder());
    const QDateTime now = QDateTime::currentDateTime();
    // ★ 三条都放在"最近 24 小时"之内,别把正好卡在 24 小时边界的那条算进来。
    //   默认时间窗是构造那一刻算的 now.addDays(-1),而这些时间戳是基于更早的
    //   那个 now —— 正好 24 小时的那条会掉出窗口,
    //   于是测试红在一个和被测逻辑无关的地方。
    //   （这本身也是个提醒:凡是用"默认值"当前提的用例,都要留足余量。）
    fireAlarm(QStringLiteral("十分钟前"), AlarmLevel::Critical, now.addSecs(-600));
    fireAlarm(QStringLiteral("一小时前"), AlarmLevel::Critical, now.addSecs(-3600));
    fireAlarm(QStringLiteral("二十小时前"), AlarmLevel::Critical, now.addSecs(-20 * 3600));

    qwhmi::history::AlarmHistoryView view(rec_);
    QCOMPARE(view.rowCount(), 3);

    // 收窄到最近 15 分钟,只该剩下"十分钟前"那一条
    auto* from = view.findChild<QDateTimeEdit*>(QStringLiteral("alarmHistoryFromEdit"));
    auto* to   = view.findChild<QDateTimeEdit*>(QStringLiteral("alarmHistoryToEdit"));
    QVERIFY(from);
    QVERIFY(to);
    to->setDateTime(now);
    from->setDateTime(now.addSecs(-15 * 60));
    view.refresh();

    // ★ 这条曾经是红的。refresh() 开头忘了 setRowCount(0),
    //   第二遍查完表格里还留着上一遍的行 ——
    //   界面上看着"查到了 3 条",其实有 2 条是不该出现的。
    QCOMPARE(view.rowCount(), 1);
    QCOMPARE(view.nameAt(0), QStringLiteral("十分钟前"));
}

// ============================================================================
// 时间过滤
// ============================================================================

void TestAlarmHistoryView::timeFilter_excludesRecordsOutsideRange() {
    QVERIFY(openRecorder());
    const QDateTime now = QDateTime::currentDateTime();
    fireAlarm(QStringLiteral("区间内"), AlarmLevel::Critical, now.addSecs(-60));
    fireAlarm(QStringLiteral("区间外-太早"), AlarmLevel::Critical, now.addSecs(-86400 * 7));
    fireAlarm(QStringLiteral("区间外-太晚"), AlarmLevel::Critical, now.addSecs(3600));

    qwhmi::history::AlarmHistoryView view(rec_);
    // 默认就是"最近 24 小时",所以"太早/太晚"两条本该被挡掉。
    // 注意"太晚"那条:现在是过去时刻,加一小时是【未来】——
    // 边界处理写反的话它会混进来,而用户看到的是一条他还没发生的报警。
    QCOMPARE(view.rowCount(), 1);
    QCOMPARE(view.nameAt(0), QStringLiteral("区间内"));
}

void TestAlarmHistoryView::quickRangeButton_setsAndQueriesImmediately() {
    QVERIFY(openRecorder());
    const QDateTime now = QDateTime::currentDateTime();
    fireAlarm(QStringLiteral("半小时前"), AlarmLevel::Critical, now.addSecs(-1800));
    fireAlarm(QStringLiteral("五天前"),   AlarmLevel::Critical, now.addSecs(-86400 * 5));

    qwhmi::history::AlarmHistoryView view(rec_);

    // 找"最近1小时"那个快捷按钮(索引 0)
    auto* quick1h = view.findChild<QPushButton*>(QStringLiteral("alarmHistoryQuick0"));
    QVERIFY2(quick1h, "找不到「最近1小时」快捷按钮");
    quick1h->click();

    // ★ 点了快捷按钮必须【立刻查】,不逼用户再按"查询"。
    //   少一次点击少一次"我是不是没点到"的怀疑。
    QCOMPARE(view.rowCount(), 1);
    QCOMPARE(view.nameAt(0), QStringLiteral("半小时前"));
    QCOMPARE(view.from().secsTo(view.to()) / 3600, 1);   // 时间窗被设成 1 小时
}

// ============================================================================
// 等级过滤
// ============================================================================

void TestAlarmHistoryView::levelFilter_minLevelExcludesLighterAlarms() {
    QVERIFY(openRecorder());
    const QDateTime now = QDateTime::currentDateTime();
    fireAlarm(QStringLiteral("信息级"), AlarmLevel::Info,      now.addSecs(-60));
    fireAlarm(QStringLiteral("警告级"), AlarmLevel::Warning,   now.addSecs(-61));
    fireAlarm(QStringLiteral("严重级"), AlarmLevel::Critical,  now.addSecs(-62));
    fireAlarm(QStringLiteral("危急级"), AlarmLevel::Emergency, now.addSecs(-63));

    qwhmi::history::AlarmHistoryView view(rec_);

    // 选"严重及以上"(下标 2)
    auto* combo = view.findChild<QComboBox*>(QStringLiteral("alarmHistoryLevelCombo"));
    QVERIFY(combo);
    combo->setCurrentIndex(2);
    view.refresh();

    // ★ 下限语义:选"严重及以上"必须把危急一起带出来。
    //   只留严重的话,用户会以为危急报警没记录 —— 而那是最该被查到的。
    QCOMPARE(view.rowCount(), 2);
    // 顺序:最新的在上面。上面 Critical 是 -62 秒、Emergency 是 -63 秒,
    // 所以第一行是"严重"。这条断言顺带锁住了"最新在上"这个排序约定。
    QCOMPARE(view.levelTextAt(0), QStringLiteral("严重"));
    QCOMPARE(view.levelTextAt(1), QStringLiteral("危急"));

    // ★ 等级这一条曾经是红的,而且红得很冤:
    //   报错打印 Actual 是 "ä¸¥",肉眼看过去【就是"严重"两个字的乱码形状】,
    //   很容易以为是自己期望值写错了。
    //   真病根在实现里:显示用的字面量是 UTF-8,代码却用 fromLatin1 转,
    //   "严"的 3 个字节 E4 B8 A5 被当成 3 个 Latin-1 字符 —— 表格里两列全是乱码。
    //   顺带把"动作"列也断上,它是同一行代码里的同一个错。
    QCOMPARE(view.actionTextAt(0), QStringLiteral("触发"));
    // 名字那一列是从库里直接取的 QString,所以它一直是对的 ——
    // 正是这种"别的列好好的、就这两列是乱码",最容易让人以为是数据问题。
    QCOMPARE(view.nameAt(0), QStringLiteral("严重级"));
}

void TestAlarmHistoryView::levelFilter_defaultExcludesInfoLevel() {
    QVERIFY(openRecorder());
    const QDateTime now = QDateTime::currentDateTime();
    fireAlarm(QStringLiteral("信息级"), AlarmLevel::Info,     now.addSecs(-60));
    fireAlarm(QStringLiteral("警告级"), AlarmLevel::Warning,  now.addSecs(-61));

    qwhmi::history::AlarmHistoryView view(rec_);

    // 默认是"警告及以上"。信息级不响、历史量又大,默认挡掉是对的。
    QCOMPARE(view.rowCount(), 1);
    QCOMPARE(view.nameAt(0), QStringLiteral("警告级"));
}

void TestAlarmHistoryView::levelCombo_isLabeledAsMinimumNotExactMatch() {
    QVERIFY(openRecorder());
    qwhmi::history::AlarmHistoryView view(rec_);
    auto* combo = view.findChild<QComboBox*>(QStringLiteral("alarmHistoryLevelCombo"));
    QVERIFY(combo);

    // ★ 文案必须说"及以上"。
    //   AlarmRecorder::query 的第三个参数是 minLevel(下限),不是集合。
    //   界面上写成"等级 = 严重"的话,用户会以为危急的记录被弄丢了,
    //   而实际上只是没显示 —— 界面在撒谎,比缺功能更糟。
    const QStringList labels = {combo->itemText(0), combo->itemText(1),
                                combo->itemText(2), combo->itemText(3)};
    for (const QString& l : labels) {
        if (l.contains(QStringLiteral("严重")))
            QVERIFY2(l.contains(QStringLiteral("以上")),
                     "等级选项必须写成「及以上」,它是下限不是精确匹配");
    }
}

// ============================================================================
// 不许有的东西
// ============================================================================

void TestAlarmHistoryView::table_isNotEditable() {
    QVERIFY(openRecorder());
    qwhmi::history::AlarmHistoryView view(rec_);
    auto* table = view.findChild<QTableWidget*>(QStringLiteral("alarmHistoryTable"));
    QVERIFY(table);

    // ★ 历史是"给人查"的,不是"给人改"的。
    //   双击就能改的话,用户改完看着像成功了,实际上只是内存里的显示变了 ——
    //   关掉窗口就没了,而用户会以为历史被改过。
    QCOMPARE(table->editTriggers(), QAbstractItemView::NoEditTriggers);
}

void TestAlarmHistoryView::noRecorder_disablesInsteadOfShowingEmptyTable() {
    // 没有 recorder —— 数据库打不开的那种情况。
    qwhmi::history::AlarmHistoryView view(nullptr);

    auto* table = view.findChild<QTableWidget*>(QStringLiteral("alarmHistoryTable"));
    QVERIFY(table);
    QVERIFY2(!table->isEnabled(), "数据库没开却给一张能查的表,用户会以为'没有报警'");
    QVERIFY2(view.statusText().contains(QStringLiteral("未启用")),
             "必须说清楚是'库没启用',而不是让人以为程序坏了");
}

QTEST_MAIN(TestAlarmHistoryView)
#include "tst_AlarmHistoryView.moc"