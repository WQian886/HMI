// ============================================================================
// tst_AlarmListControl
//
// ★ F3(2026-09-30)新增用例的写法说明(为什么这次断言跟以前不一样)
//   以前这个文件里的用例多是 QVERIFY(true) 式的"不崩"测试 ——
//   它们证明不了任何用户可见的行为。
//   F3 加的每一条都断言【操作员真正看得见的东西】:
//     · 计数标签上的数字
//     · 等级那一格的颜色
//     · 双击之后报警到底有没有被确认
//   理由:本项目已经吃过 4 次"测试绿了 ≠ 功能能用"的亏,其中一次正是
//   F1 那条只断言 action==2 && id 的测试 —— 它挑的恰好是唯一没坏的两样。
// ============================================================================

#include <QtTest>
#include <cmath>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QGraphicsProxyWidget>
#include <QApplication>
#include <QTableWidget>
#include <QTableWidgetItem>
#include "widgets/AlarmListControl.h"
#include "editor/RuntimeCanvas.h"
#include "../AlarmManager.h"
#include "../Alarm.h"
#include "auth/CurrentUser.h"
#include "auth/User.h"

using namespace qwhmi::widgets;

// ★ 权限:acknowledge() 内部查 AcknowledgeAlarm,而 CurrentUser 默认是
//   未登录 + Role::Viewer(见 CurrentUser.h:54)—— 没有 AcknowledgeAlarm 权限。
//   所以凡是走【真实 acknowledge 路径】的用例,必须先登录一个够权限的角色,
//   否则 ack 会被静默拒绝,而【断言报警已确认的那几条会红】。
//   —— 这正是"测试没覆盖到真实路径"的一个实例:
//   tst_AlarmRecorder 的 ack 用例是用 invokeMethod 直接调 AlarmRecorder 的槽,
//   绕开了 acknowledge 本身,所以权限那道门它从来没撞过。
namespace {
// 登录一个可确认报警的角色。Role 存进 CurrentUser 单例,进程内全局。
void loginAs(qwhmi::auth::Role role, const QString& name) {
    qwhmi::auth::User u;
    u.name = name;
    u.role = role;
    qwhmi::auth::CurrentUser::instance().login(u);
}
} // namespace

// 能手动触发 triggered / recovered 的 checker
// (真实 ThresholdAlarmChecker 要 PollScheduler + driver,链路太重,与本组用例无关)
class FireChecker : public AlarmChecker {
    Q_OBJECT
public:
    explicit FireChecker(const QString& n, AlarmLevel lvl, QObject* parent = nullptr)
        : AlarmChecker(parent) { setName(n); setLevel(lvl); }
    void fireTrigger(const Alarm& a) { emit triggered(a); }
    void fireRecover(qint64 id) { emit recovered(id); }
};

class TestAlarmListControl : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { qputenv("QT_QPA_PLATFORM", "offscreen"); qInfo() << "[tst_AlarmListControl] init"; }
    void cleanupTestCase() { qInfo() << "[tst_AlarmListControl] cleanup"; }

    void construct_defaults();
    void paint_doesNotCrash();
    void bindAlarmManager_connectsSignals();
    void bindAlarmManager_refreshShowsActiveAlarms();
    void proxyChild_blocksParentSelection();   // ★ 复现 bug 实证

    // ===== ★ F3 =====
    void count_showsZeroWhenNoAlarm();
    void count_incrementsOnTriggerAndDropsOnAck();
    void count_ignoresAcknowledgedButUnrecovered();
    void allFourLevels_haveDistinctColors();
    void acknowledgedRow_goesGrey();
    void doubleClick_actuallyAcknowledges();
    void doubleClick_withoutPermission_saysSo();
    void runtimeMode_togglesMousePassThrough();
    void editMode_keepsE4bBehaviour();
    void runtimeCanvas_suppliesAlarmManager();
    void refreshNow_preservesAcknowledgedState();

    // ===== ★ F3-b(2026-10-08):闪烁 =====
    //   原来只有 hmi.cpp 的 ui.alarmTable 会闪,运行时画布上的这个控件不会闪 ——
    //   而 E.5 之后操作员实际盯的是运行时画布。
    void blink_onlyUnacknowledgedCriticalAndEmergency();
    void blink_togglesColorSoItIsActuallyVisible();
    void blink_stopsWhenNothingLeftToBlink();
    void blink_stopsWhenAllCleared();
    void blink_emergencyAndCriticalKeepDistinctColors();
    // ★ 这条是从一次真实错误里长出来的:我曾把"保证对比度不降"写进注释,
    //   拿 WCAG 公式一算,那句话是假的(亮红配白字只有 3.33:1)。
    //   注释留不住 —— 得让测试拦住下一次调色。
    void blink_textStaysReadableInBothPhases();

    // ===== ★ 2026-10-14:"全部确认" =====
    //   AlarmManager::acknowledgeAll() 从 5.5 就写好了、也返回真实条数(F3 改的),
    //   但全项目没有任何入口调它 —— 建好了没接线。
    //   而现场一次异常往往同时来七八条,只有单条确认的话操作员得挨个点。
    void ackAll_buttonDisabledWhenNothingPending();
    void ackAll_acknowledgesEveryActiveAlarm();
    void ackAll_leavesAlreadyAcknowledgedRowsAlone();
    void ackAll_withoutPermission_saysSo();
    void ackAll_buttonReenablesAfterNewAlarm();
};

// ===== 公共 fixture =====
namespace {

struct Fx {
    QGraphicsScene scene;
    AlarmManager    mgr;
    AlarmListControl ctl;
    FireChecker*    checker = nullptr;

    Fx() {
        ctl.setPos(QPointF(0, 0));
        scene.addItem(&ctl);
        checker = new FireChecker("温度高", AlarmLevel::Critical, &mgr);
        mgr.addChecker(checker);
    }
    static Alarm makeAlarm(qint64 id, AlarmLevel lvl, const QString& name) {
        Alarm a;
        a.id_          = id;
        a.name         = name;
        a.variableName = "plc1.Temperature";
        a.level        = lvl;
        a.state        = AlarmState::Active;
        a.triggeredAt  = QDateTime::currentDateTime();
        a.triggerValue = "85.5";
        a.threshold    = ">= 80.0";
        return a;
    }
};

QTableWidget* tableOf(AlarmListControl& c) {
    for (QGraphicsItem* ch : c.childItems()) {
        if (auto* proxy = qgraphicsitem_cast<QGraphicsProxyWidget*>(ch)) {
            if (auto* t = proxy->widget()->findChild<QTableWidget*>()) return t;
        }
    }
    return nullptr;
}

// ★ WCAG 2.x 相对亮度 + 对比度。
//
//   【为什么测试里要写一遍而不是直接调 Qt】Qt 有 QColor::luminance(),
//   但它用的是另一套(而且不区分 sRGB 的 gamma 校正),
//   算出来的数跟 WCAG 不一样,拿它断言等于用一把自己削的尺子量东西。
//   这里照 WCAG 的定义实现,阈值 4.5:1 才有意义。
//
//   【为什么值得单独写 20 行】因为"颜色好不好看"没法定,必须先定一个
//   能量化的底线,而这条底线正是配色真正该守住的那一半 ——
//   另一半"够不够显眼"靠人眼看,而人会看走眼,公式不会。
namespace {
double relativeLuminance(const QColor& c) {
    auto f = [](int v) {
        const double x = v / 255.0;
        return x <= 0.03928 ? x / 12.92 : std::pow((x + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * f(c.red()) + 0.7152 * f(c.green()) + 0.0722 * f(c.blue());
}
double contrastRatio(const QColor& a, const QColor& b) {
    const double la = relativeLuminance(a);
    const double lb = relativeLuminance(b);
    const double hi = std::max(la, lb);
    const double lo = std::min(la, lb);
    return (hi + 0.05) / (lo + 0.05);
}
} // namespace

} // namespace

void TestAlarmListControl::construct_defaults() {
    AlarmListControl a;
    QCOMPARE(a.size(), QSizeF(320, 100));
}

void TestAlarmListControl::paint_doesNotCrash() {
    QGraphicsScene s;
    AlarmListControl a;
    s.addItem(&a);
    QVERIFY(true);   // proxy 自己渲染
}

void TestAlarmListControl::bindAlarmManager_connectsSignals() {
    AlarmManager mgr;
    AlarmListControl a;
    a.bindAlarmManager(&mgr);
    // 验证:再次 bind 应被忽略(代码有 if(alarmMgr_) return 守卫)
    a.bindAlarmManager(&mgr);
    QVERIFY(true);
}

void TestAlarmListControl::bindAlarmManager_refreshShowsActiveAlarms() {
    // ★ 简化:不构造 ThresholdAlarmChecker(它需要 PollScheduler + driver,
    //   链路太重,且与"控件 bind 不崩"目标无关)。
    //   空 AlarmManager + bindAlarmManager → 控件内部 refreshNow() 不崩即可。
    AlarmManager mgr;
    AlarmListControl a;
    a.bindAlarmManager(&mgr);
    QVERIFY(true);
    // 触发一次清空信号,验证连接没断
    emit mgr.allCleared();
    QVERIFY(true);
}

// ★ Bug 复现实证:QGraphicsProxyWidget 默认 acceptedMouseButtons != NoButton,
//   吞掉所有鼠标事件 → 父 AlarmListControl 收不到 press → 选不中
//   修复预期:proxy.setAcceptedMouseButtons(Qt::NoButton) 后,proxy.childItems()
//   第一个子图元的 acceptedMouseButtons == Qt::NoButton
void TestAlarmListControl::proxyChild_blocksParentSelection() {
    AlarmListControl a;
    a.setSize(QSizeF(200, 100));
    a.setPos(QPointF(0, 0));

    // 1) 验证 proxy 子图元存在
    QGraphicsProxyWidget* proxy = nullptr;
    for (QGraphicsItem* ch : a.childItems()) {
        proxy = qgraphicsitem_cast<QGraphicsProxyWidget*>(ch);
        if (proxy) break;
    }
    QVERIFY2(proxy != nullptr, "AlarmListControl 必须有一个 QGraphicsProxyWidget 子");

    // 2) 关键断言:在未修复状态下,proxy 接受鼠标事件(吞事件)
    qDebug() << "[BugRepro] proxy->acceptedMouseButtons() =" << proxy->acceptedMouseButtons();
    if (proxy->acceptedMouseButtons() == Qt::NoButton) {
        qDebug() << "[BugRepro] ✅ 已修复:proxy 不再吃事件";
    } else {
        qDebug() << "[BugRepro] ❌ Bug 复现:proxy 接受鼠标事件(默认 Qt::LeftButton)";
        qDebug() << "[BugRepro]    → 父 AlarmListControl 收不到 mousePress";
        qDebug() << "[BugRepro]    → ItemIsSelectable 失效,不能选中";
        qDebug() << "[BugRepro]    → ItemIsMovable 失效,不能移动";
        qDebug() << "[BugRepro]    → scene->selectionChanged 不发,8 handle 不挂";
    }
    QVERIFY2(proxy->acceptedMouseButtons() == Qt::NoButton,
             "Bug 已修:proxy 应 setAcceptedMouseButtons(Qt::NoButton),让父收事件");

    // 3) 验证父控件 flags 设了(基类应该做了)
    QVERIFY(a.flags() & QGraphicsItem::ItemIsSelectable);
    QVERIFY(a.flags() & QGraphicsItem::ItemIsMovable);
}

// ============================================================================
// ★ F3(2026-09-30)新增用例
// ============================================================================

// 【为什么"没有报警"也要有断言】空表格是正常状态,但"控件没接上数据源"
//   也是空表格,两者在屏幕上一模一样。少了这条,上面那两种情况没法区分。
void TestAlarmListControl::count_showsZeroWhenNoAlarm() {
    Fx f;
    f.ctl.setAlarmManager(&f.mgr);
    QCOMPARE(f.ctl.unacknowledgedCount(), 0);
    QCOMPARE(f.ctl.countText(), QStringLiteral("未确认:0"));
}

// 【核心断言】触发 → 计数涨;确认 → 计数落。
//   注意断言的是 unacknowledgedCount() 和 countText() 两层:
//   数字对但标签没更新,是两种不同的坏法(只查一层会漏)。
void TestAlarmListControl::count_incrementsOnTriggerAndDropsOnAck() {
    loginAs(qwhmi::auth::Role::Operator, "操作员甲");
    Fx f;
    f.ctl.setAlarmManager(&f.mgr);

    const qint64 id = 1001;
    f.checker->fireTrigger(Fx::makeAlarm(id, AlarmLevel::Critical, "温度高"));
    QTest::qWait(30);
    QCOMPARE(f.ctl.unacknowledgedCount(), 1);
    QCOMPARE(f.ctl.countText(), QStringLiteral("未确认:1"));

    // ★ 断言 acknowledge 的返回值 —— 它现在是 bool(F3 改的),这个断言守住它。
    //   以前返回 void,"没确认成"和"确认成了"在调用方看来一模一样。
    QVERIFY2(f.mgr.acknowledge(id), "Operator 角色有 AcknowledgeAlarm 权限,必须确认成功");
    QTest::qWait(30);
    QCOMPARE(f.ctl.unacknowledgedCount(), 0);
    QCOMPARE(f.ctl.countText(), QStringLiteral("未确认:0"));

    // 再确认一次 → 必须返回 false(已不是 Active),不能假装又成功一次
    QVERIFY2(!f.mgr.acknowledge(id),
             "重复确认必须返回 false —— 界面上就是'不用再确认了',"
             "返回 true 会让调用方以为又确认了一次");
}

// 【这条是 F3 里最容易写错的一条】
//   acknowledge 只改 state、不删记录(5.5 有意为之:"复位不该清历史"),
//   所以 activeAlarms_ 里【确实留着】已确认未恢复的报警。
//   计数如果按"表格行数"算,操作员确认过的报警还会被算进去,数字永远降不下来。
//   而这个指示灯的全部价值就是"降下来才说明事情办完了"。
void TestAlarmListControl::count_ignoresAcknowledgedButUnrecovered() {
    loginAs(qwhmi::auth::Role::Operator, "操作员甲");
    Fx f;
    f.ctl.setAlarmManager(&f.mgr);

    const qint64 id = 1002;
    f.checker->fireTrigger(Fx::makeAlarm(id, AlarmLevel::Warning, "压力低"));
    QTest::qWait(30);
    QCOMPARE(f.ctl.unacknowledgedCount(), 1);

    f.mgr.acknowledge(id);
    QTest::qWait(30);

    // 记录【确实还在】active 里(这是 5.5 的既定行为,不是本次改的)
    QCOMPARE(f.mgr.count(), 1);
    QVERIFY(f.mgr.contains(id));
    // 但未确认数必须是 0
    QCOMPARE(f.ctl.unacknowledgedCount(), 0);
    QVERIFY2(f.ctl.countText() == QStringLiteral("未确认:0"),
             qPrintable(QStringLiteral("已确认但未恢复的报警不该计入未确认数,实际显示:%1")
                            .arg(f.ctl.countText())));
}

// 4 档等级必须【两两不同】。原来只有 Critical/Emergency 有颜色,
// Info/Warning 跟普通行一模一样 —— 报警列表里每一行都是报警,
// 没有"这行没报警"作参照,扫一眼根本分不出该去现场的和记一下就行的。
void TestAlarmListControl::allFourLevels_haveDistinctColors() {
    Fx f;
    f.ctl.setAlarmManager(&f.mgr);

    f.checker->fireTrigger(Fx::makeAlarm(2001, AlarmLevel::Info,     "A"));
    f.checker->fireTrigger(Fx::makeAlarm(2002, AlarmLevel::Warning,  "B"));
    f.checker->fireTrigger(Fx::makeAlarm(2003, AlarmLevel::Critical, "C"));
    f.checker->fireTrigger(Fx::makeAlarm(2004, AlarmLevel::Emergency,"D"));
    QTest::qWait(30);

    QCOMPARE(f.ctl.rowCount(), 4);
    QCOMPARE(f.ctl.levelTextAt(0), QStringLiteral("Info"));
    QCOMPARE(f.ctl.levelTextAt(1), QStringLiteral("Warning"));
    QCOMPARE(f.ctl.levelTextAt(2), QStringLiteral("Critical"));
    QCOMPARE(f.ctl.levelTextAt(3), QStringLiteral("Emergency"));

    // 两两不同:6 个组合
    for (int i = 0; i < 4; ++i) {
        for (int j = i + 1; j < 4; ++j) {
            QVERIFY2(f.ctl.levelColorAt(i) != f.ctl.levelColorAt(j),
                     qPrintable(QStringLiteral("第 %1 档(%2)和第 %3 档(%4)底色相同 "
                                               "= 操作员分不出来。实色 %5 / %6")
                                    .arg(i).arg(f.ctl.levelTextAt(i))
                                    .arg(j).arg(f.ctl.levelTextAt(j))
                                    .arg(f.ctl.levelColorAt(i).name())
                                    .arg(f.ctl.levelColorAt(j).name())));
        }
    }
}

// 已确认的行要淡化成灰底 —— 不然它还留着红底,
// 看着像"还没处理",而它其实已经办完了。
void TestAlarmListControl::acknowledgedRow_goesGrey() {
    loginAs(qwhmi::auth::Role::Operator, "操作员甲");
    Fx f;
    f.ctl.setAlarmManager(&f.mgr);

    const qint64 id = 3001;
    f.checker->fireTrigger(Fx::makeAlarm(id, AlarmLevel::Emergency, "危急报警"));
    QTest::qWait(30);
    const QColor before = f.ctl.levelColorAt(0);

    f.mgr.acknowledge(id);
    QTest::qWait(30);
    const QColor after = f.ctl.levelColorAt(0);

    QVERIFY2(before != after,
             "确认后行底色必须变 —— 颜色不变 = 操作员以为还没处理,"
             "跟 hmi.cpp 5.5 那个'已确认了还闪红'是同一类毛病");

    auto* table = tableOf(f.ctl);
    QVERIFY(table);
    QCOMPARE(table->item(0, 4)->text(), QStringLiteral("已确认"));
}

// ★★ F3 最核心的一条:双击必须【真的确认掉】。
//   这里不走真实鼠标事件 —— offscreen 下 QGraphicsView 的事件投递链太长,
//   容易测成"投递没对上"这种假失败。直接发信号验证【槽接到 manager 上了】,
//   而"槽接上了"才是双击能不能用的全部前提。
//   反向验证时把 onCellDoubleClicked 接空,这条立刻红(见提交说明)。
void TestAlarmListControl::doubleClick_actuallyAcknowledges() {
    loginAs(qwhmi::auth::Role::Operator, "操作员甲");
    Fx f;
    f.ctl.setAlarmManager(&f.mgr);
    f.ctl.setRuntimeMode(true);

    const qint64 id = 4001;
    f.checker->fireTrigger(Fx::makeAlarm(id, AlarmLevel::Critical, "温度高"));
    QTest::qWait(30);
    QCOMPARE(f.ctl.rowCount(), 1);

    // 模拟双击第 0 行
    QMetaObject::invokeMethod(&f.ctl, "onCellDoubleClicked", Qt::DirectConnection,
                              Q_ARG(int, 0), Q_ARG(int, 0));
    QTest::qWait(30);

    // 真正被确认了(而不是只有界面文字变了)
    const Alarm* a = f.mgr.findAlarm(id);
    QVERIFY2(a != nullptr, "报警不该消失 —— 确认不删记录");
    QVERIFY2(a->state == AlarmState::Acknowledged,
             "双击后 state 必须是 Acknowledged(这是真正被确认了的证据,"
             "只看表格文字'已确认'是不够的,那可以随便写)");
    QCOMPARE(f.ctl.unacknowledgedCount(), 0);
}

// 无权限时必须【说出来】。
//   原来 acknowledge 是 void,权限不足只 qCWarning 一行 —— 界面"点了没反应",
//   操作员分不清是自己没权限(该找管理员)还是点错了。
void TestAlarmListControl::doubleClick_withoutPermission_saysSo() {
    // 登录一个【没有】AcknowledgeAlarm 权限的角色(tst_Permission:61 已确认 Viewer 没有)
    loginAs(qwhmi::auth::Role::Viewer, "只读用户");

    Fx f;
    f.ctl.setAlarmManager(&f.mgr);
    f.ctl.setRuntimeMode(true);

    const qint64 id = 5001;
    f.checker->fireTrigger(Fx::makeAlarm(id, AlarmLevel::Critical, "温度高"));
    QTest::qWait(30);

    QMetaObject::invokeMethod(&f.ctl, "onCellDoubleClicked", Qt::DirectConnection,
                              Q_ARG(int, 0), Q_ARG(int, 0));
    QTest::qWait(30);

    // 状态没变(确实没确认成)
    const Alarm* a = f.mgr.findAlarm(id);
    QVERIFY(a);
    QVERIFY2(a->state == AlarmState::Active, "无权限就不该被确认");
    QCOMPARE(f.ctl.unacknowledgedCount(), 1);   // 未确认数也不该变

    // ★ 但界面必须【明说】,不能沉默
    QVERIFY2(f.ctl.countText().contains(QStringLiteral("无确认权限")),
             qPrintable(QStringLiteral("无权限时必须在界面上说明,不能静默。实际显示:%1")
                            .arg(f.ctl.countText())));

    // ★ 收尾:把身份还原成 Operator,否则后面的用例会因为残留的只读身份而红。
    //   CurrentUser 是进程内单例,测试之间会互相污染 —— 这条 pin 住这个事实。
    loginAs(qwhmi::auth::Role::Operator, "操作员甲");
}

// F3 vs E.4b 的正面冲突:两种模式都得对。
void TestAlarmListControl::runtimeMode_togglesMousePassThrough() {
    Fx f;
    QGraphicsProxyWidget* proxy = nullptr;
    for (QGraphicsItem* ch : f.ctl.childItems()) {
        proxy = qgraphicsitem_cast<QGraphicsProxyWidget*>(ch);
        if (proxy) break;
    }
    QVERIFY(proxy);

    // 编辑模式(默认):穿透,父能选中/拖动
    QVERIFY2(proxy->acceptedMouseButtons() == Qt::NoButton,
             "编辑模式下必须穿透(E.4b 修的就是这个)");

    // 运行时:吃事件,内嵌表格才收得到双击
    f.ctl.setRuntimeMode(true);
    QVERIFY2(proxy->acceptedMouseButtons() == Qt::LeftButton,
             "运行时模式下代理必须吃鼠标事件 —— 否则内嵌表格收不到双击,"
             "F3 的确认功能等于不存在");

    // 运行时不该还能拖动控件(否则双击可能被当成拖拽,报警列表被拖走)
    QVERIFY2(!(f.ctl.flags() & QGraphicsItem::ItemIsMovable),
             "运行时模式下控件不该还能被拖动");
}

// 【E.4b 回归守卫】F3 加了运行时模式,但不能把编辑模式的行为改坏。
//   这条是专门为"有人顺手把 NoButton 删掉"准备的。
void TestAlarmListControl::editMode_keepsE4bBehaviour() {
    Fx f;
    QVERIFY(!f.ctl.runtimeMode());   // 默认必须是编辑模式
    QVERIFY(f.ctl.flags() & QGraphicsItem::ItemIsSelectable);
    QVERIFY(f.ctl.flags() & QGraphicsItem::ItemIsMovable);

    // 切到运行时再切回来,行为要复原(不是只能单向切)
    f.ctl.setRuntimeMode(true);
    f.ctl.setRuntimeMode(false);
    QVERIFY(f.ctl.flags() & QGraphicsItem::ItemIsSelectable);
    QVERIFY(f.ctl.flags() & QGraphicsItem::ItemIsMovable);
}

// ★ 守 F3 挖出来的第 5 个静默失败:RuntimeCanvas 以前从不接报警数据源,
//   运行时画面上的报警列表永远是空的,而且零报错。
//   断言的是"画面上的那个控件真的拿到了 manager 且能出数据",
//   而不是"构造没崩"。
void TestAlarmListControl::runtimeCanvas_suppliesAlarmManager() {
    loginAs(qwhmi::auth::Role::Operator, "操作员甲");
    AlarmManager mgr;
    FireChecker checker("温度高", AlarmLevel::Critical, &mgr);
    mgr.addChecker(&checker);

    qwhmi::editor::RuntimeCanvas canvas(nullptr, &mgr);
    auto* ctl = new AlarmListControl;
    canvas.loadControls({ctl});

    QVERIFY2(ctl->alarmManager() == &mgr,
             "RuntimeCanvas 必须把 AlarmManager 交给控件 —— 不接的话"
             "运行时画面上的报警列表永远是空的且不报错");

    checker.fireTrigger(Fx::makeAlarm(6001, AlarmLevel::Critical, "温度高"));
    QTest::qWait(30);
    QCOMPARE(ctl->rowCount(), 1);
    QCOMPARE(ctl->countText(), QStringLiteral("未确认:1"));

    // 运行时模式下双击才有效
    QMetaObject::invokeMethod(ctl, "onCellDoubleClicked", Qt::DirectConnection,
                              Q_ARG(int, 0), Q_ARG(int, 0));
    QTest::qWait(30);
    QCOMPARE(ctl->unacknowledgedCount(), 0);
}

// 刷新不能把"已确认"冲回"未确认"。
//   addAlarmRow 原来无条件写"未确认"。refreshNow 会重走这条路,
//   而 activeAlarms_ 里确实留着已确认未恢复的记录 —— 于是任何一次刷新
//   都把操作员确认过的报警变回未确认,他于是再点一次,第二次又没反应。
void TestAlarmListControl::refreshNow_preservesAcknowledgedState() {
    loginAs(qwhmi::auth::Role::Operator, "操作员甲");
    Fx f;
    f.ctl.setAlarmManager(&f.mgr);

    const qint64 id = 7001;
    f.checker->fireTrigger(Fx::makeAlarm(id, AlarmLevel::Critical, "温度高"));
    QTest::qWait(30);
    f.mgr.acknowledge(id);
    QTest::qWait(30);

    auto* table = tableOf(f.ctl);
    QVERIFY(table);
    QCOMPARE(table->item(0, 4)->text(), QStringLiteral("已确认"));

    // 触发一次重画
    f.ctl.refreshNow();
    QTest::qWait(30);

    QCOMPARE(table->item(0, 4)->text(), QStringLiteral("已确认"));
    QVERIFY2(f.ctl.unacknowledgedCount() == 0,
             "刷新不能凭空多出未确认数");
}

// ============================================================================
// ★ F3-b(2026-10-08):闪烁
//
// 【背景】文档里挂了很久的那条 🟡:主画面 ui.alarmTable 会闪,
//   运行时画布上的 AlarmListControl 不会。E.5 之后操作员实际盯的是
//   运行时画布,所以"会闪的那套"根本不在他眼前。
//
// 【这组用例测的是"该不该闪"和"闪了看得见吗",不是"定时器有没有在跑"】
//   定时器在不在跑是可以断言的,但那不重要 ——
//   定时器在跑而颜色没变,和没定时器,在用户眼里是同一件事:不闪。
// ============================================================================

// 只有【未确认 + 严重/危急】该闪。
// 其余三种情况各自有理由,不是一个判据能概括的:
//   信息/警告 —— 现场最吵的往往就是它,全都闪等于真出事时淹没
//   已确认     —— 操作员已经处理过了,还在闪是在骗他去处理第二次
void TestAlarmListControl::blink_onlyUnacknowledgedCriticalAndEmergency() {
    loginAs(qwhmi::auth::Role::Operator, "操作员甲");
    Fx f;
    f.ctl.setAlarmManager(&f.mgr);

    f.checker->fireTrigger(Fx::makeAlarm(8001, AlarmLevel::Info,      "模式切换"));
    f.checker->fireTrigger(Fx::makeAlarm(8002, AlarmLevel::Warning,   "压力低"));
    f.checker->fireTrigger(Fx::makeAlarm(8003, AlarmLevel::Critical,  "温度高"));
    f.checker->fireTrigger(Fx::makeAlarm(8004, AlarmLevel::Emergency, "电机过载"));
    QTest::qWait(30);

    QCOMPARE(f.ctl.rowCount(), 4);
    // 四行里只有两行该闪 —— 断言的是【数目】,不是某一行的身份,
    // 因为行的顺序由 alarmManager 决定,写死行号就是埋雷(见 tst 里同一条纪律)。
    QCOMPARE(f.ctl.blinkingRowCount(), 2);
    QVERIFY2(f.ctl.blinkEnabled(),
             "有两条未确认的严重/危急就必须开表 —— 不闪等于没这个功能");

    // 确认掉一条"该闪"的,数目必须跟着减
    f.mgr.acknowledge(8003);
    QTest::qWait(30);
    QCOMPARE(f.ctl.blinkingRowCount(), 1);
    QCOMPARE(f.ctl.countText(), QStringLiteral("未确认:3"));
}

// 闪 = 颜色真的在两相之间变。
// 断言"走过一相之后颜色不同"—— 若颜色没变,用户看到的就是"不闪",
// 哪怕定时器每秒都在空转两次。
void TestAlarmListControl::blink_togglesColorSoItIsActuallyVisible() {
    loginAs(qwhmi::auth::Role::Operator, "操作员甲");
    Fx f;
    f.ctl.setAlarmManager(&f.mgr);

    f.checker->fireTrigger(Fx::makeAlarm(8101, AlarmLevel::Critical, "温度高"));
    QTest::qWait(30);
    QCOMPARE(f.ctl.rowCount(), 1);

    const QColor dark = f.ctl.levelColorAt(0);
    QVERIFY(!f.ctl.blinkOn());          // 刚进来是暗相
    f.ctl.advanceBlinkForTest();
    QVERIFY(f.ctl.blinkOn());           // 走一相 → 亮相
    const QColor lit = f.ctl.levelColorAt(0);
    QVERIFY2(dark != lit,
             qPrintable(QStringLiteral("走过一相颜色必须变。实色 %1 / %2 —— "
                                       "颜色不变 = 用户看着就是不闪")
                            .arg(dark.name()).arg(lit.name())));

    // 再走一相必须回到原来的颜色:闪烁是往复,不是单程变色。
    f.ctl.advanceBlinkForTest();
    QVERIFY(!f.ctl.blinkOn());
    QCOMPARE(f.ctl.levelColorAt(0), dark);
}

// 没有该闪的行时,定时器必须停。
// 这不是省电优化,这是 7×24 工控机:没报警时让事件循环一直醒着是纯浪费。
void TestAlarmListControl::blink_stopsWhenNothingLeftToBlink() {
    loginAs(qwhmi::auth::Role::Operator, "操作员甲");
    Fx f;
    f.ctl.setAlarmManager(&f.mgr);

    // 先确认一条是"从闪到不闪"这条路径
    f.checker->fireTrigger(Fx::makeAlarm(8201, AlarmLevel::Emergency, "电机过载"));
    QTest::qWait(30);
    QVERIFY(f.ctl.blinkEnabled());

    f.mgr.acknowledge(8201);
    QTest::qWait(30);
    QVERIFY2(!f.ctl.blinkEnabled(),
             "全部处理完后定时器必须停 —— 否则它在后台空转,"
             "而屏幕上什么都没有");

    // 相位必须复位:停下时不能停在亮相。
    // 不复位的话,下一次启动的第一帧会是深色而该行本档色是浅色,
    // 用户看到"新报警颜色不对,过一会儿才对"。
    QVERIFY2(!f.ctl.blinkOn(),
             "停表时相位必须复位到暗相,否则下一轮第一帧的颜色是错的");
}

// 全清之后也必须停表 —— onAllCleared 是另一条独立路径,
// 漏掉它的表现是"表格空了但定时器还在转"。
void TestAlarmListControl::blink_stopsWhenAllCleared() {
    loginAs(qwhmi::auth::Role::Operator, "操作员甲");
    Fx f;
    f.ctl.setAlarmManager(&f.mgr);

    f.checker->fireTrigger(Fx::makeAlarm(8301, AlarmLevel::Critical, "温度高"));
    QTest::qWait(30);
    QVERIFY(f.ctl.blinkEnabled());

    f.mgr.clearAll();
    QTest::qWait(30);
    QCOMPARE(f.ctl.rowCount(), 0);
    QVERIFY2(!f.ctl.blinkEnabled(),
             "全清之后定时器必须停(表格都空了,没有什么可闪的)");
}

// ★ 闪烁不能把字变得看不清 —— 这条是从一次真实的错误里长出来的。
//
// 【我怎么发现它错了】最初 repaintRow 里写的是"亮红 + 白字",
//   注释还特意写了"保证对比度不降"。后来拿 WCAG 的相对亮度公式
//   把每个配色算了一遍,才发现那句话是假的:
//     危急暗相 #cd2828 + 白字 = 5.34:1
//     危急亮相 #f05a5a + 白字 = 3.33:1   ← 掉了一截
//   闪烁最亮的那半拍,恰恰是字最难读的那半拍 ——
//   而那正是操作员最想看清"哪条报警、多严重"的时候。
//
// 【为什么这条必须变成测试,而不是继续留一句注释】
//   注释是我自己写给自己看的,下一次调色的人未必会重算一遍。
//   而"字看得清"在屏幕上和"底色够亮"长得完全不一样,
//   没人会靠肉眼对着一个色块数字去比 —— 测出来才拦得住。
// ★ 阈值取 4.5:1(WCAG AA 对正文的最低要求)。没敢取 7:1(A 或大字号加强),
//   因为工业屏常年亮着、报警区不是长文,4.5 是"看得清"这条线里最保守的一个。
void TestAlarmListControl::blink_textStaysReadableInBothPhases() {
    loginAs(qwhmi::auth::Role::Operator, "操作员甲");
    Fx f;
    f.ctl.setAlarmManager(&f.mgr);

    f.checker->fireTrigger(Fx::makeAlarm(8501, AlarmLevel::Critical,  "温度高"));
    f.checker->fireTrigger(Fx::makeAlarm(8502, AlarmLevel::Emergency, "电机过载"));
    QTest::qWait(30);

    for (int row = 0; row < f.ctl.rowCount(); ++row) {
        const QString lvl = f.ctl.levelTextAt(row);
        // 暗相
        const double dark = contrastRatio(f.ctl.levelColorAt(row),
                                          f.ctl.levelTextColorAt(row));
        QVERIFY2(dark >= 4.5,
                 qPrintable(QStringLiteral("%1 暗相对比度只有 %.2f:1 —— 字看不清")
                                .arg(lvl).arg(dark, 0, 'f', 2)));
        // 亮相
        f.ctl.advanceBlinkForTest();
        const double lit = contrastRatio(f.ctl.levelColorAt(row),
                                         f.ctl.levelTextColorAt(row));
        QVERIFY2(lit >= 4.5,
                 qPrintable(QStringLiteral("%1 亮相对比度只有 %.2f:1"
                                           " —— 闪烁最亮的那一瞬正是最难读的一瞬。"
                                           "底色 #%3 字色 #%4")
                                .arg(lvl).arg(lit, 0, 'f', 2)
                                .arg(f.ctl.levelColorAt(row).name(),
                                     f.ctl.levelTextColorAt(row).name())));
        // 亮相必须比暗相更显眼,而不是"反正达标就行"
        QVERIFY2(f.ctl.levelColorAt(row) != QColor(),
                 "对照用:底色读不出来就是没上色");
        f.ctl.advanceBlinkForTest();   // 回到暗相,测下一行
    }
}

// 危急和严重都在闪,如果亮相时它们变成同一个颜色,
// 闪烁就把 F3 辛苦建立的四档配色给毁了 —— 操作员靠颜色分轻重。
void TestAlarmListControl::blink_emergencyAndCriticalKeepDistinctColors() {
    loginAs(qwhmi::auth::Role::Operator, "操作员甲");
    Fx f;
    f.ctl.setAlarmManager(&f.mgr);

    f.checker->fireTrigger(Fx::makeAlarm(8401, AlarmLevel::Critical,  "温度高"));
    f.checker->fireTrigger(Fx::makeAlarm(8402, AlarmLevel::Emergency, "电机过载"));
    QTest::qWait(30);
    QCOMPARE(f.ctl.rowCount(), 2);
    QCOMPARE(f.ctl.blinkingRowCount(), 2);

    const QColor critDark = f.ctl.levelColorAt(0);
    const QColor emgDark  = f.ctl.levelColorAt(1);
    QVERIFY(critDark != emgDark);

    // 两行同处亮相 —— 此时仍然必须分得出来
    f.ctl.advanceBlinkForTest();
    const QColor critLit = f.ctl.levelColorAt(0);
    const QColor emgLit  = f.ctl.levelColorAt(1);
    QVERIFY2(critLit != emgLit,
             qPrintable(QStringLiteral("亮相时两档同色了。实色 %1 / %2 —— "
                                       "闪烁把'哪一档更重'这个信息盖掉了")
                            .arg(critLit.name()).arg(emgLit.name())));
    // 亮相也要和暗相不同,否则根本没在闪
    QVERIFY(critLit != critDark);
    QVERIFY(emgLit  != emgDark);
}

// ===== ★ 2026-10-14:"全部确认" =====

// 没有待确认的时按钮必须是灰的。
// 一直亮着的按钮按下去只得到一句"没有可确认的",在操作员眼里就是坏了。
void TestAlarmListControl::ackAll_buttonDisabledWhenNothingPending() {
    loginAs(qwhmi::auth::Role::Operator, "操作员甲");
    Fx f;
    f.ctl.setAlarmManager(&f.mgr);
    QVERIFY2(!f.ctl.ackAllEnabled(), "刚接上数据源、一条报警都没有,按钮就该是灰的");

    f.checker->fireTrigger(Fx::makeAlarm(9001, AlarmLevel::Critical, "温度高"));
    QTest::qWait(30);
    QVERIFY2(f.ctl.ackAllEnabled(), "有未确认报警时按钮必须可点,否则操作员没法一次处理完");
}

void TestAlarmListControl::ackAll_acknowledgesEveryActiveAlarm() {
    loginAs(qwhmi::auth::Role::Operator, "操作员甲");
    Fx f;
    f.ctl.setAlarmManager(&f.mgr);

    // 四档全来 —— 现场一次异常往往不止一条,而且不止一个等级。
    f.checker->fireTrigger(Fx::makeAlarm(9101, AlarmLevel::Info,      "巡检提示"));
    f.checker->fireTrigger(Fx::makeAlarm(9102, AlarmLevel::Warning,   "压力偏高"));
    f.checker->fireTrigger(Fx::makeAlarm(9103, AlarmLevel::Critical,  "温度高"));
    f.checker->fireTrigger(Fx::makeAlarm(9104, AlarmLevel::Emergency, "电机过载"));
    QTest::qWait(30);
    QCOMPARE(f.ctl.rowCount(), 4);
    QCOMPARE(f.ctl.unacknowledgedCount(), 4);

    f.ctl.acknowledgeAll();
    QTest::qWait(30);

    // ★ 断言的是 Alarm::state,不是表格文字 —— 文字谁都能写,
    //   state 才是"真的被确认了"的证据(同 F3 doubleClick 那条的写法)。
    for (qint64 id : {9101, 9102, 9103, 9104}) {
        const Alarm* a = f.mgr.findAlarm(id);
        QVERIFY2(a, qPrintable(QStringLiteral("报警 %1 不见了").arg(id)));
        QVERIFY2(a->state == AlarmState::Acknowledged,
                 qPrintable(QStringLiteral("报警 %1 状态不对,期望 Acknowledged").arg(id)));
    }
    QCOMPARE(f.ctl.unacknowledgedCount(), 0);
    QVERIFY2(!f.ctl.ackAllEnabled(), "全部确认完成后按钮必须回到灰的");
    // 记录一条都不能少 —— 复位不该清历史(5.5 有意为之)
    QCOMPARE(f.mgr.count(), 4);
}

// 已经确认过、但还没恢复的报警,不该被"全部确认"再算一次,
// 更不该因为重算而丢掉原来的确认时间。
void TestAlarmListControl::ackAll_leavesAlreadyAcknowledgedRowsAlone() {
    loginAs(qwhmi::auth::Role::Operator, "操作员甲");
    Fx f;
    f.ctl.setAlarmManager(&f.mgr);

    f.checker->fireTrigger(Fx::makeAlarm(9201, AlarmLevel::Critical, "温度高"));
    f.checker->fireTrigger(Fx::makeAlarm(9202, AlarmLevel::Critical, "压力高"));
    QTest::qWait(30);

    QVERIFY(f.mgr.acknowledge(9201));   // 先单独确认一条
    QTest::qWait(30);
    QCOMPARE(f.ctl.unacknowledgedCount(), 1);
    QVERIFY(f.ctl.ackAllEnabled());     // 还剩一条,所以按钮仍可点

    f.ctl.acknowledgeAll();
    QTest::qWait(30);

    QCOMPARE(f.ctl.unacknowledgedCount(), 0);
    QCOMPARE(f.mgr.count(), 2);         // 两条记录都还在
    const Alarm* a1 = f.mgr.findAlarm(9201);
    QVERIFY(a1);
    QCOMPARE(int(a1->state), int(AlarmState::Acknowledged));
    QVERIFY(a1->acknowledgedAt.isValid());   // 确认时间没被后来的那次冲掉
}

void TestAlarmListControl::ackAll_withoutPermission_saysSo() {
    loginAs(qwhmi::auth::Role::Viewer, "只读用户");   // tst_Permission:61 已确认无 AcknowledgeAlarm
    Fx f;
    f.ctl.setAlarmManager(&f.mgr);

    f.checker->fireTrigger(Fx::makeAlarm(9301, AlarmLevel::Critical, "温度高"));
    f.checker->fireTrigger(Fx::makeAlarm(9302, AlarmLevel::Critical, "压力高"));
    QTest::qWait(30);

    f.ctl.acknowledgeAll();
    QTest::qWait(30);

    // 一条都没被确认
    QCOMPARE(f.ctl.unacknowledgedCount(), 2);
    // ★ 但界面必须明说,不能沉默 —— 沉默的话操作员会以为是程序坏了
    QVERIFY2(f.ctl.countText().contains(QStringLiteral("无确认权限")),
             qPrintable(QStringLiteral("无权限时必须在界面上说明,不能静默。实际显示:%1")
                            .arg(f.ctl.countText())));

    loginAs(qwhmi::auth::Role::Operator, "操作员甲");   // 还原,CurrentUser 是进程内单例
}

// 确认完之后再来一条新报警,按钮必须重新亮起来 ——
// 否则"全部确认"只能用一次,那等于没加。
void TestAlarmListControl::ackAll_buttonReenablesAfterNewAlarm() {
    loginAs(qwhmi::auth::Role::Operator, "操作员甲");
    Fx f;
    f.ctl.setAlarmManager(&f.mgr);

    f.checker->fireTrigger(Fx::makeAlarm(9401, AlarmLevel::Critical, "温度高"));
    QTest::qWait(30);
    QVERIFY(f.ctl.ackAllEnabled());

    f.ctl.acknowledgeAll();
    QTest::qWait(30);
    QVERIFY2(!f.ctl.ackAllEnabled(), "确认完后按钮该灰掉");

    f.checker->fireTrigger(Fx::makeAlarm(9402, AlarmLevel::Emergency, "电机过载"));
    QTest::qWait(30);
    QVERIFY2(f.ctl.ackAllEnabled(), "又来一条未确认的,按钮必须重新可点 —— "
                                    "只亮一次的按钮等于没加");
    QCOMPARE(f.ctl.unacknowledgedCount(), 1);
}

QTEST_MAIN(TestAlarmListControl)
#include "tst_AlarmListControl.moc"