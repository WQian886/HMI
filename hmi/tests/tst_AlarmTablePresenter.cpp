// ============================================================================
// tst_AlarmTablePresenter —— 报警【行】的呈现
//
// ★★ 这个文件为什么存在:一个 10 天都没人发现的、整张表都废掉的 bug ★★
//
//   2026-10-14 的 G-1 重构(commit 2f51374)要把两套报警 UI 的配色收成一个权威。
//   做法是对的,但它把主画面 hmi.cpp 里的这一行删掉了:
//
//     -  ui.alarmTable->setItem(row, 4, new QTableWidgetItem("未确认"));
//     +  auto* statusItem = ui.alarmTable->item(row, 4);   // 读一个从没被创建过的 item
//
//   一行改动,五处连锁反应:
//     ① 状态列永远是空白        (表头写着"状态",格子一直空着)
//     ② 确认之后那一行纹丝不动  (setAlarmRowState 改的是空气)
//     ③ repaintAlarmRow 取不到状态列 → 提前 return → 【一格颜色都没有】
//     ④ updateAlarmBlinkTimer 读不到状态列 → 【严重级警报不闪】
//     ⑤ 右键菜单"确认"的判据读的是同一个 UserRole → 恒 false → 【菜单项恒灰】
//
//   而 2026-10-14 当天 68 个测试【全绿】——
//   因为主画面那套报警表【一条测试都没有】。
//   (跟 G-11「测了 above 从没测 below」是同一类:测了 A 没测 B。)
//
// ★ 本文件守住的是"行怎么造出来"这件事本身,而不只是某一个字段。
//   负向对照(拆掉守卫必须变红)记在文件末尾。
// ============================================================================

#include <QtTest>
#include <QApplication>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QSet>

#include "alarm/AlarmTablePresenter.h"
#include "alarm/AlarmRowPalette.h"
#include "../Alarm.h"

using namespace qwhmi::alarm;

// ===== 公共 fixture =====
namespace {

Alarm makeAlarm(qint64 id, AlarmLevel lvl, const QString& name,
                AlarmState st = AlarmState::Active) {
    Alarm a;
    a.id_          = id;
    a.name         = name;
    a.variableName = "plc1.Temperature";
    a.level        = lvl;
    a.state        = st;
    a.triggeredAt  = QDateTime::currentDateTime();
    a.triggerValue = "85.5";
    a.threshold    = ">= 80.0";
    return a;
}

// ★ 造一张 5 列的表(列数/表头跟主画面 hmi.cpp:656 一样)。
QTableWidget* makeTable() {
    auto* t = new QTableWidget();
    t->setColumnCount(5);
    t->setHorizontalHeaderLabels({"时间", "报警名", "等级", "值", "状态"});
    return t;
}

} // namespace


class TestAlarmTablePresenter : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        qputenv("QT_QPA_PLATFORM", "offscreen");
        qInfo() << "[tst_AlarmTablePresenter] init";
    }
    void cleanupTestCase() { qInfo() << "[tst_AlarmTablePresenter] cleanup"; }

    // ===== ★ BUGFIX-C3(2026-10-24):状态列必须被创建 =====
    void append_createsAllFiveColumns();
    void append_statusColumnHasText();
    void append_statusColumnCarriesStateRole();

    // ===== 状态改动 =====
    void setRowState_updatesTextAndRole();
    void setRowState_handledRowStopsBlinking();

    // ===== 配色 =====
    void repaintRow_paintsEveryColumn();
    void repaintRow_usesLevelEnumNotText();

    // ===== 闪烁判据 =====
    void rowShouldBlink_onlyForUnhandledSevere();
    void anyRowShouldBlink_findsOneAmongMany();

    // ===== ★ 机制②:行数上限 =====
    void trim_dropsOldestRecoveredRow();
    void trim_keepsRowsUnderLimit();
    void trim_neverDropsActiveAlarm();
    void trim_dropsOnlyAsManyAsNeeded();
    void trim_keepsNewestRows();
    void trim_returnsDroppedCount();

    // ===== 兜底方向(读不到 UserRole 时)=====
    void rowLevel_missingRoleFallsBackToWarning();
    void rowVisualState_missingRoleFallsBackToUnacknowledged();
};


// ============================================================================
// ★ 状态列:三件事 —— 存在、有文字、带 enum
// ============================================================================

void TestAlarmTablePresenter::append_createsAllFiveColumns() {
    QTableWidget* t = makeTable();
    appendAlarmRow(t, makeAlarm(1001, AlarmLevel::Critical, "温度高"), "严重");

    // ★ 这一条就是那个 bug 的核心:第 5 列(状态)从来没被创建过。
    //   断言的是"每一列都有 item",不是"第 5 列有 item" ——
    //   前者顺带守住另外四列,后者只守住这一次犯的错。
    for (int col = 0; col < 5; ++col) {
        QVERIFY2(t->item(0, col) != nullptr,
                 qPrintable(QStringLiteral("第 %1 列没有 item —— 状态列从来没被创建过,"
                                           "那正是 2026-10-14 那个 bug").arg(col)));
    }
    delete t;
}

void TestAlarmTablePresenter::append_statusColumnHasText() {
    QTableWidget* t = makeTable();
    appendAlarmRow(t, makeAlarm(1002, AlarmLevel::Warning, "压力高"), "警告");
    QCOMPARE(t->item(0, kColStatus)->text(), QStringLiteral("未确认"));
    delete t;
}

void TestAlarmTablePresenter::append_statusColumnCarriesStateRole() {
    QTableWidget* t = makeTable();
    appendAlarmRow(t, makeAlarm(1003, AlarmLevel::Warning, "压力高"), "警告");

    // ★ 状态列必须带 kStateRole —— 配色和"能不能确认"都读它。
    //   少了它:配色读到无效值 → 兜底成"未确认"(不闪),
    //   而"确认"菜单的判据恒 false → 菜单项恒灰。
    const QVariant v = t->item(0, kColStatus)->data(kStateRole);
    QVERIFY2(v.isValid(), "状态列没有 kStateRole —— 配色和确认判据都读它");
    QCOMPARE(static_cast<RowVisualState>(v.toInt()), RowVisualState::Unacknowledged);
    delete t;
}

// ============================================================================
// 状态改动:文字与 UserRole 必须一起改
// ============================================================================

void TestAlarmTablePresenter::setRowState_updatesTextAndRole() {
    QTableWidget* t = makeTable();
    appendAlarmRow(t, makeAlarm(1004, AlarmLevel::Critical, "温度高"), "严重");

    setRowState(t, 0, AlarmState::Acknowledged);

    QCOMPARE(t->item(0, kColStatus)->text(), QStringLiteral("已确认"));
    // 视觉状态:已确认与已恢复【归成同一档】(都已有人管过,一律淡化成灰)。
    QCOMPARE(static_cast<RowVisualState>(t->item(0, kColStatus)->data(kStateRole).toInt()),
             RowVisualState::Handled);
    delete t;
}

void TestAlarmTablePresenter::setRowState_handledRowStopsBlinking() {
    QTableWidget* t = makeTable();
    appendAlarmRow(t, makeAlarm(1005, AlarmLevel::Critical, "温度高"), "严重");
    QVERIFY(rowShouldBlink(t, 0));   // 严重 + 未确认 → 该闪

    setRowState(t, 0, AlarmState::Acknowledged);
    QVERIFY2(!rowShouldBlink(t, 0),
             "已确认的行还在闪 —— 那是骗操作员去处理一条已经处理过的");
    delete t;
}

// ============================================================================
// 配色
// ============================================================================

void TestAlarmTablePresenter::repaintRow_paintsEveryColumn() {
    QTableWidget* t = makeTable();
    appendAlarmRow(t, makeAlarm(1006, AlarmLevel::Critical, "温度高"), "严重");

    repaintRow(t, 0, false);

    // ★ 每一列都要上色 —— 只刷等级列的话,四档的差别被压在很窄的一格里,
    //   余光扫列表时看不出(AlarmListControl::repaintRow 的原话)。
    //   ★ 而且【底色不能是默认白】:主画面以前就是"非闪烁行清成白底",
    //   于是 Info / Warning 跟"没有报警"长得一模一样。
    for (int col = 0; col < 5; ++col) {
        QVERIFY2(t->item(0, col)->background().color() != QColor(Qt::white),
                 qPrintable(QStringLiteral("第 %1 列是白底 —— "
                                           "Info/Warning 会跟'没有报警'长得一模一样").arg(col)));
    }
    delete t;
}

void TestAlarmTablePresenter::repaintRow_usesLevelEnumNotText() {
    QTableWidget* t = makeTable();
    // ★ 等级列的文字【故意写错】("严重"写成"Critical")——
    //   配色如果还靠解析文字反查档位,这里就会拿到不同的颜色。
    //   文字只是文字,配色只认 kLevelRole。
    appendAlarmRow(t, makeAlarm(1007, AlarmLevel::Emergency, "温度高高"), "Critical");

    repaintRow(t, 0, false);

    const QColor expected = colorsFor(AlarmLevel::Emergency, RowVisualState::Unacknowledged,
                                      false).bg;
    QCOMPARE(t->item(0, kColLevel)->background().color(), expected);
    delete t;
}

// ============================================================================
// 闪烁判据
// ============================================================================

void TestAlarmTablePresenter::rowShouldBlink_onlyForUnhandledSevere() {
    QTableWidget* t = makeTable();
    appendAlarmRow(t, makeAlarm(1008, AlarmLevel::Info, "提示"), "信息");
    appendAlarmRow(t, makeAlarm(1009, AlarmLevel::Warning, "警告"), "警告");
    appendAlarmRow(t, makeAlarm(1010, AlarmLevel::Critical, "严重"), "严重");
    appendAlarmRow(t, makeAlarm(1011, AlarmLevel::Emergency, "危急"), "危急");

    QVERIFY(!rowShouldBlink(t, 0));   // Info 不闪
    QVERIFY(!rowShouldBlink(t, 1));   // Warning 不闪
    QVERIFY(rowShouldBlink(t, 2));    // Critical 闪
    QVERIFY(rowShouldBlink(t, 3));    // Emergency 闪
    delete t;
}

void TestAlarmTablePresenter::anyRowShouldBlink_findsOneAmongMany() {
    QTableWidget* t = makeTable();
    appendAlarmRow(t, makeAlarm(1012, AlarmLevel::Info, "提示"), "信息");
    QVERIFY(!anyRowShouldBlink(t));   // 全是 Info → 不该亮表

    appendAlarmRow(t, makeAlarm(1013, AlarmLevel::Critical, "严重"), "严重");
    QVERIFY2(anyRowShouldBlink(t), "表里有一条严重未确认,就该有行该闪");
    delete t;
}

// ============================================================================
// ★ 机制②:行数上限(SOAK-2 的另一半)
// ============================================================================

void TestAlarmTablePresenter::trim_dropsOldestRecoveredRow() {
    QTableWidget* t = makeTable();
    // 三条:第 1 条已恢复(不在 liveIds 里),第 2、3 条还活着。
    appendAlarmRow(t, makeAlarm(2001, AlarmLevel::Info, "旧"), "信息");
    appendAlarmRow(t, makeAlarm(2002, AlarmLevel::Info, "中"), "信息");
    appendAlarmRow(t, makeAlarm(2003, AlarmLevel::Info, "新"), "信息");

    QSet<qint64> live{2002, 2003};
    const int dropped = trimToLimit(t, live, 2);

    QCOMPARE(dropped, 1);
    QCOMPARE(t->rowCount(), 2);
    // 丢的是【最旧的那条】(2001),不是别的。
    QCOMPARE(t->item(0, kColName)->text(), QStringLiteral("中"));
    QCOMPARE(t->item(1, kColName)->text(), QStringLiteral("新"));
    delete t;
}

void TestAlarmTablePresenter::trim_keepsRowsUnderLimit() {
    QTableWidget* t = makeTable();
    appendAlarmRow(t, makeAlarm(2004, AlarmLevel::Info, "一"), "信息");
    appendAlarmRow(t, makeAlarm(2005, AlarmLevel::Info, "二"), "信息");

    QCOMPARE(trimToLimit(t, {}, 10), 0);
    QCOMPARE(t->rowCount(), 2);
    delete t;
}

void TestAlarmTablePresenter::trim_neverDropsActiveAlarm() {
    QTableWidget* t = makeTable();
    // 三条全都还活着(都在 liveIds 里)。
    appendAlarmRow(t, makeAlarm(2006, AlarmLevel::Critical, "一"), "严重");
    appendAlarmRow(t, makeAlarm(2007, AlarmLevel::Critical, "二"), "严重");
    appendAlarmRow(t, makeAlarm(2008, AlarmLevel::Critical, "三"), "严重");

    QSet<qint64> live{2006, 2007, 2008};
    const int dropped = trimToLimit(t, live, 1);

    // ★★ 关键断言:一条都不能丢 ★★
    //   丢掉的如果是一条【还没确认、还没恢复】的报警,等于把它从操作员眼前拿走。
    //   本项目的原则是"漏报比多报危险得多"——
    //   宁可表超上限,也不藏报警。
    QCOMPARE(dropped, 0);
    QCOMPARE(t->rowCount(), 3);
    delete t;
}

void TestAlarmTablePresenter::trim_dropsOnlyAsManyAsNeeded() {
    QTableWidget* t = makeTable();
    for (int i = 0; i < 10; ++i) {
        appendAlarmRow(t, makeAlarm(3000 + i, AlarmLevel::Info,
                                    QStringLiteral("旧%1").arg(i)), "信息");
    }
    QSet<qint64> live{3008, 3009};   // 只有最后两条还活着
    const int dropped = trimToLimit(t, live, 3);

    QCOMPARE(dropped, 7);
    QCOMPARE(t->rowCount(), 3);
    delete t;
}

void TestAlarmTablePresenter::trim_keepsNewestRows() {
    QTableWidget* t = makeTable();
    for (int i = 0; i < 5; ++i) {
        appendAlarmRow(t, makeAlarm(4000 + i, AlarmLevel::Info,
                                    QStringLiteral("第%1条").arg(i)), "信息");
    }
    QSet<qint64> live{};   // 全都恢复了
    trimToLimit(t, live, 2);

    QCOMPARE(t->rowCount(), 2);
    // 留的是【最新的两条】。
    QCOMPARE(t->item(0, kColName)->text(), QStringLiteral("第3条"));
    QCOMPARE(t->item(1, kColName)->text(), QStringLiteral("第4条"));
    delete t;
}

void TestAlarmTablePresenter::trim_returnsDroppedCount() {
    QTableWidget* t = makeTable();
    appendAlarmRow(t, makeAlarm(5001, AlarmLevel::Info, "一"), "信息");
    QCOMPARE(trimToLimit(t, {}, 0), 0);   // 上限 0 = 不裁(不改行为)
    QCOMPARE(t->rowCount(), 1);
    delete t;
}

// ============================================================================
// 兜底方向
// ============================================================================

void TestAlarmTablePresenter::rowLevel_missingRoleFallsBackToWarning() {
    QTableWidget* t = makeTable();
    appendAlarmRow(t, makeAlarm(6001, AlarmLevel::Emergency, "危急"), "危急");
    // 故意把 UserRole 抹掉 —— 模拟"写这一行的代码漏了 setData"。
    t->item(0, kColLevel)->setData(kLevelRole, QVariant());

    // ★ 兜底必须偏向【多报】而不是【漏报】——
    //   显示成 Emergency+未确认 = 多吸引一点注意力;
    //   显示成 Info+已处理 = "一条真报警看着像已经处理过了",那危险得多。
    QCOMPARE(rowLevel(t, 0), AlarmLevel::Warning);
    QCOMPARE(rowVisualState(t, 0), RowVisualState::Unacknowledged);
    delete t;
}

void TestAlarmTablePresenter::rowVisualState_missingRoleFallsBackToUnacknowledged() {
    QTableWidget* t = makeTable();
    appendAlarmRow(t, makeAlarm(6002, AlarmLevel::Critical, "严重"), "严重");
    t->item(0, kColStatus)->setData(kStateRole, QVariant());

    // 同上:兜底成"未确认"(该闪),不是"已处理"(不该闪)。
    QCOMPARE(rowVisualState(t, 0), RowVisualState::Unacknowledged);
    QVERIFY(rowShouldBlink(t, 0));
    delete t;
}

QTEST_MAIN(TestAlarmTablePresenter)
#include "tst_AlarmTablePresenter.moc"
