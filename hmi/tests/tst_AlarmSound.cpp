// ============================================================================
// 阶段 F · F5(2026-10-08):报警声音 / 蜂鸣
//
// ★★ 先读这段,再决定这 46 条绿灯能证明什么 ★★
//   本文件测的是【决策】,不是【耳朵】。
//   "该响的时候响了没有、响的是哪一档、为什么这次没响"全部能断言;
//   "声音有没有真的从音箱出来"测不了 —— MessageBeep 不返回任何东西,
//   也没有一个能在 CI 上跑的声卡。
//   所以这些测试全绿之后,仍然必须人耳验一次。绿灯只是"逻辑没写错",
//   不是"功能能用" —— 这个项目已经为这句话栽过很多次。
//
//   ★ 为什么"为什么没响"必须可断言
//     现场只会反馈一句"报警不响"。能说出是哪一种原因,排查从半小时变一分钟;
//     说不出来就只能挨个改配置试 —— 那正是本项目反复吃亏的"静默失败"。
// ============================================================================

#include <QtTest>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QSignalSpy>
#include <QTime>

#include "Alarm.h"
#include "AlarmManager.h"
#include "AlarmSoundPlayer.h"
#include "ThresholdAlarmChecker.h"
#include "alarm/AlarmSoundDialog.h"
#include "auth/CurrentUser.h"
#include "auth/Permission.h"

class TestAlarmSound : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();

    // ---- 等级 → 下标 ----
    void levelIndex_coversAllFourLevelsInOrder();

    // ---- 静默时段(纯函数,最容易写错的地方)----
    void quietHours_disabledByDefault();
    void quietHours_sameDayRange();
    void quietHours_crossesMidnight_nightIsQuiet();
    void quietHours_crossesMidnight_dayIsNotQuiet();
    void quietHours_startEqualsEnd_meansNoQuietPeriod();

    // ---- 判定:每一种"故意不响"都要有名字 ----
    void decide_beepsOnNormalAlarm();
    void decide_masterSwitchOff_silencesEverything();
    void decide_infoLevelOffByDefault();
    void decide_acknowledgedSilences();
    void decide_quietHoursSilences();
    void decide_checkOrder_switchBeatsEverythingElse();

    // ---- 端到端:真的走一遍信号 ----
    void triggered_alarm_beepsOnce();
    void triggered_infoLevel_doesNotBeep();
    void acknowledged_alarm_doesNotBeepAgain();
    void quietHours_alarmDoesNotBeep_butIsStillRecorded();
    void repeatTick_beepsOnceEvenWithSeveralActiveAlarms();
    void repeatTick_usesHighestLevel();
    void repeatTick_stopsWhenAllAcknowledged();

    // ---- 边界 ----
    void settings_outOfRangeValuesAreClamped();
    void noManager_isSafeNotCrash();

    // ---- 静默时段结束后要补响(这条是写测试时发现的一个真 bug)----
    void quietHours_alarmIsRemindedAfterQuietHoursEnd();

    // ---- 设置界面 ----
    void dialog_opensShowingCurrentSettings();
    void dialog_okWritesSettingsIntoPlayer();
    void dialog_cancelChangesNothing();

private:
    Alarm makeAlarm(AlarmLevel level = AlarmLevel::Warning,
                    AlarmState state = AlarmState::Active) const;
    // 用一个会响的 checker 把报警真的塞进 AlarmManager ——
    // 不走"直接 emit 信号"那条捷径,走生产链路才算数。
    qint64 fireAlarm(AlarmManager& mgr, const QString& name, AlarmLevel level);
};

namespace {

// 一个"永远会触发"的 checker。
// 为什么不用 ThresholdAlarmChecker:它要靠 PollScheduler 喂实时值,
// 而这里要验的是声音层,不是检测层。绕一圈只会让测试变慢变脆。
//
// ★ 为什么这个类【不写 Q_OBJECT】
//   信号 triggered 是基类 AlarmChecker 声明的,派生类照样能 emit ——
//   信号本来就不需要"属于"发出它的那个类。
//   加 Q_OBJECT 只会多一个 moc 条目,以及"匿名 namespace 里放 Q_OBJECT
//   到底行不行"这个没必要冒的险。
class AlwaysTriggerChecker : public AlarmChecker {
public:
    explicit AlwaysTriggerChecker(const QString& n, AlarmLevel lv, QObject* parent = nullptr)
        : AlarmChecker(parent) { setName(n); setLevel(lv); }
    void fire(qint64 id) {
        Alarm a;
        a.id_          = id;
        a.name         = name();
        a.level        = level();
        a.state        = AlarmState::Active;
        a.triggeredAt  = QDateTime::currentDateTime();
        emit triggered(a);
    }
};

} // namespace

void TestAlarmSound::initTestCase() {
    // qRegisterMetatype 不做的话,跨线程连接会直接断开而不是报错 ——
    // 那种 bug 在测试里表现为"信号好像没发",极难查。
    qRegisterMetaType<Alarm>("Alarm");

    // ★ 必须登录成 Operator,否则 acknowledge() 会被权限检查挡下、静默返回 false。
    //   默认角色是 Viewer —— 零权限(Permission.cpp:50)。
    //   而"确认了就不响"恰恰是 F5 最核心的语义,不登录就没法测。
    //   注意 acknowledge() 失败【不报错】,只返回 false:
    //   不登录的话症状是"pendingReminderCount 怎么还是 1",
    //   报错点离病根很远 —— 这就是为什么这里主动写死角色。
    qwhmi::auth::User u;
    u.name = QStringLiteral("tester");
    u.role = qwhmi::auth::Role::Operator;
    qwhmi::auth::CurrentUser::instance().login(u);
}

Alarm TestAlarmSound::makeAlarm(AlarmLevel level, AlarmState state) const {
    Alarm a;
    a.id_     = 1;
    a.name    = QStringLiteral("测试报警");
    a.level   = level;
    a.state   = state;
    return a;
}

qint64 TestAlarmSound::fireAlarm(AlarmManager& mgr, const QString& name, AlarmLevel level) {
    // id 用递增的固定值,不用 QDateTime::currentMSecsSinceEpoch() ——
    // 后者在同一毫秒里连发两条会撞号(见下面的说明)。
    static qint64 next = 1000;
    auto* c = new AlwaysTriggerChecker(name, level);
    mgr.addChecker(c);
    c->fire(next);
    return next++;
}

// ============================================================================
// 等级 → 下标
// ============================================================================

void TestAlarmSound::levelIndex_coversAllFourLevelsInOrder() {
    // ★ 为什么要锁"顺序":设置里每档是一个 array,下标就是等级。
    //   哪天有人调整了 AlarmLevel 的声明顺序,这里不红,
    //   而现场的"信息级不响"会悄悄变成"危急级不响" —— 最危险的一档不响。
    QCOMPARE(AlarmSoundPlayer::levelIndex(AlarmLevel::Info),      0);
    QCOMPARE(AlarmSoundPlayer::levelIndex(AlarmLevel::Warning),   1);
    QCOMPARE(AlarmSoundPlayer::levelIndex(AlarmLevel::Critical),  2);
    QCOMPARE(AlarmSoundPlayer::levelIndex(AlarmLevel::Emergency), 3);
    QCOMPARE(AlarmSoundPlayer::levelCount(), 4);
}

// ============================================================================
// 静默时段
// ============================================================================

void TestAlarmSound::quietHours_disabledByDefault() {
    AlarmManager mgr;
    AlarmSoundPlayer snd(&mgr);
    // 默认就该是关的 —— 出厂状态不该让整条产线半夜静悄悄。
    QVERIFY(!snd.inQuietHours(QDateTime::currentDateTime()));
}

void TestAlarmSound::quietHours_sameDayRange() {
    AlarmManager mgr;
    AlarmSoundPlayer snd(&mgr);
    AlarmSoundPlayer::Settings s = snd.settings();
    s.quietEnabled  = true;
    s.quietStartMin = 13 * 60;   // 13:00
    s.quietEndMin   = 15 * 60;   // 15:00
    snd.setSettings(s);

    auto at = [](int h, int m) {
        return QDateTime(QDate(2026, 10, 8), QTime(h, m));
    };
    QVERIFY(!snd.inQuietHours(at(12, 59)));
    QVERIFY( snd.inQuietHours(at(13, 0)));    // 起点含
    QVERIFY( snd.inQuietHours(at(14, 30)));
    QVERIFY(!snd.inQuietHours(at(15, 0)));    // 终点不含
    QVERIFY(!snd.inQuietHours(at(23, 59)));
}

void TestAlarmSound::quietHours_crossesMidnight_nightIsQuiet() {
    AlarmManager mgr;
    AlarmSoundPlayer snd(&mgr);
    AlarmSoundPlayer::Settings s = snd.settings();
    s.quietEnabled  = true;
    s.quietStartMin = 22 * 60;   // 22:00
    s.quietEndMin   = 7 * 60;    // 07:00 —— 跨零点
    snd.setSettings(s);

    auto at = [](int h, int m) {
        return QDateTime(QDate(2026, 10, 8), QTime(h, m));
    };
    // ★ 这是现场最常见的夜班配置。
    //   写成 "m >= start && m < end" 的话,跨零点的时段【永远不生效】——
    //   而用户以为静音已经配好了,于是夜班被吵醒却还找不到原因。
    QVERIFY( snd.inQuietHours(at(22, 0)));
    QVERIFY( snd.inQuietHours(at(23, 59)));
    QVERIFY( snd.inQuietHours(at(0, 0)));    // ★ 跨到第二天凌晨
    QVERIFY( snd.inQuietHours(at(3, 15)));
    QVERIFY( snd.inQuietHours(at(6, 59)));
    QVERIFY(!snd.inQuietHours(at(7, 0)));
    QVERIFY(!snd.inQuietHours(at(12, 0)));
    QVERIFY(!snd.inQuietHours(at(21, 59)));
}

void TestAlarmSound::quietHours_crossesMidnight_dayIsNotQuiet() {
    // 与上一条互为对照:白天必须照常响。
    AlarmManager mgr;
    AlarmSoundPlayer snd(&mgr);
    AlarmSoundPlayer::Settings s = snd.settings();
    s.quietEnabled  = true;
    s.quietStartMin = 22 * 60;
    s.quietEndMin   = 7 * 60;
    snd.setSettings(s);

    QVERIFY(!snd.inQuietHours(QDateTime(QDate(2026, 10, 8), QTime(10, 0))));
}

void TestAlarmSound::quietHours_startEqualsEnd_meansNoQuietPeriod() {
    AlarmManager mgr;
    AlarmSoundPlayer snd(&mgr);
    AlarmSoundPlayer::Settings s = snd.settings();
    s.quietEnabled  = true;
    s.quietStartMin = 0;
    s.quietEndMin   = 0;    // 两端相同
    snd.setSettings(s);

    // ★ 判"没有静默时段"而不是"全天静默"。
    //   理由:想全天不响,用户已经有总开关;而把 00:00-00:00 解释成"全天静音",
    //   会让一次手滑(想拖 22:00-06:00 拖错了)变成"声音彻底没了且无任何提示"。
    //   两种解释都有人这么用,但前者破坏性小、更好排查。
    QVERIFY(!snd.inQuietHours(QDateTime(QDate(2026, 10, 8), QTime(3, 0))));
}

// ============================================================================
// 判定
// ============================================================================

void TestAlarmSound::decide_beepsOnNormalAlarm() {
    AlarmManager mgr;
    AlarmSoundPlayer snd(&mgr);
    QCOMPARE(snd.decide(makeAlarm(AlarmLevel::Warning),
                        QDateTime(QDate(2026, 10, 8), QTime(10, 0))),
             AlarmSoundPlayer::Decision::Beep);
}

void TestAlarmSound::decide_masterSwitchOff_silencesEverything() {
    AlarmManager mgr;
    AlarmSoundPlayer snd(&mgr);
    AlarmSoundPlayer::Settings s = snd.settings();
    s.masterEnabled = false;
    snd.setSettings(s);

    // 最严重的一档也不响 —— 总开关就是总开关。
    QCOMPARE(snd.decide(makeAlarm(AlarmLevel::Emergency),
                        QDateTime(QDate(2026, 10, 8), QTime(10, 0))),
             AlarmSoundPlayer::Decision::SilencedBySwitch);
}

void TestAlarmSound::decide_infoLevelOffByDefault() {
    AlarmManager mgr;
    AlarmSoundPlayer snd(&mgr);

    QCOMPARE(snd.decide(makeAlarm(AlarmLevel::Info),
                        QDateTime(QDate(2026, 10, 8), QTime(10, 0))),
             AlarmSoundPlayer::Decision::SilencedByLevel);
    // 对照:其余三档默认都响
    QCOMPARE(snd.decide(makeAlarm(AlarmLevel::Critical),
                        QDateTime(QDate(2026, 10, 8), QTime(10, 0))),
             AlarmSoundPlayer::Decision::Beep);
}

void TestAlarmSound::decide_acknowledgedSilences() {
    AlarmManager mgr;
    AlarmSoundPlayer snd(&mgr);
    const QDateTime now = QDateTime(QDate(2026, 10, 8), QTime(10, 0));

    QCOMPARE(snd.decide(makeAlarm(AlarmLevel::Critical, AlarmState::Acknowledged), now),
             AlarmSoundPlayer::Decision::SilencedByAcknowledged);
    QCOMPARE(snd.decide(makeAlarm(AlarmLevel::Critical, AlarmState::Cleared), now),
             AlarmSoundPlayer::Decision::SilencedByAcknowledged);
}

void TestAlarmSound::decide_quietHoursSilences() {
    AlarmManager mgr;
    AlarmSoundPlayer snd(&mgr);
    AlarmSoundPlayer::Settings s = snd.settings();
    s.quietEnabled  = true;
    s.quietStartMin = 22 * 60;
    s.quietEndMin   = 7 * 60;
    snd.setSettings(s);

    QCOMPARE(snd.decide(makeAlarm(AlarmLevel::Emergency),
                        QDateTime(QDate(2026, 10, 8), QTime(23, 30))),
             AlarmSoundPlayer::Decision::SilencedByQuietHours);
}

void TestAlarmSound::decide_checkOrder_switchBeatsEverythingElse() {
    // 一条同时踩了三个坑的报警(总开关关 + 等级关 + 已确认),
    // 报出来的原因必须是【总开关】—— 那是用户最该先看的那一条。
    AlarmManager mgr;
    AlarmSoundPlayer snd(&mgr);
    AlarmSoundPlayer::Settings s = snd.settings();
    s.masterEnabled = false;
    s.quietEnabled  = true;
    s.quietStartMin = 0;
    s.quietEndMin   = 1439;   // 几乎全天
    snd.setSettings(s);

    QCOMPARE(snd.decide(makeAlarm(AlarmLevel::Info, AlarmState::Acknowledged),
                        QDateTime(QDate(2026, 10, 8), QTime(3, 0))),
             AlarmSoundPlayer::Decision::SilencedBySwitch);
}

// ============================================================================
// 端到端(走真信号)
// ============================================================================

void TestAlarmSound::triggered_alarm_beepsOnce() {
    AlarmManager mgr;
    AlarmSoundPlayer snd(&mgr);

    int  beeps = 0;
    AlarmLevel heard = AlarmLevel::Info;
    snd.setSoundSink([&](AlarmLevel lv) { ++beeps; heard = lv; });

    fireAlarm(mgr, QStringLiteral("温度高"), AlarmLevel::Critical);

    QCOMPARE(beeps, 1);
    // ★ 连"响的是哪一档"都要断 —— 等级映射错了,
    //   现场会表现为"危急报警发出的是叮的一声",而功能"是好的"。
    QCOMPARE(heard, AlarmLevel::Critical);
    QCOMPARE(snd.beepCount(), 1);
}

void TestAlarmSound::triggered_infoLevel_doesNotBeep() {
    AlarmManager mgr;
    AlarmSoundPlayer snd(&mgr);
    int beeps = 0;
    snd.setSoundSink([&](AlarmLevel) { ++beeps; });

    fireAlarm(mgr, QStringLiteral("泵启动"), AlarmLevel::Info);

    // 默认不响 —— 但报警本身照常进系统(下面这条单独验)
    QCOMPARE(beeps, 0);
    QCOMPARE(mgr.count(), 1);   // 报警存在,只是没声音
}

void TestAlarmSound::acknowledged_alarm_doesNotBeepAgain() {
    AlarmManager mgr;
    AlarmSoundPlayer snd(&mgr);
    int beeps = 0;
    snd.setSoundSink([&](AlarmLevel) { ++beeps; });

    const qint64 id = fireAlarm(mgr, QStringLiteral("压力高"), AlarmLevel::Critical);
    QCOMPARE(beeps, 1);

    // ★ 操作员点了确认 —— 这是 F5 最重要的语义:
    //   "确认"在操作员心里等于"我知道了,别再叫了"。
    //   不这么实现的话,报警会一直响到它自己恢复,
    //   而现场最常见的做法是—— 操作员直接去把声音关了,然后再也不打开。
    mgr.acknowledge(id);
    QCOMPARE(beeps, 1);
    QCOMPARE(snd.pendingReminderCount(), 0);
}

void TestAlarmSound::quietHours_alarmDoesNotBeep_butIsStillRecorded() {
    AlarmManager mgr;
    AlarmSoundPlayer snd(&mgr);
    AlarmSoundPlayer::Settings s = snd.settings();
    // 把静默时段设成"包含现在",免得这条用例半夜才通过、早上就红。
    s.quietEnabled  = true;
    s.quietStartMin = 0;
    s.quietEndMin   = 1439;
    snd.setSettings(s);

    int beeps = 0;
    snd.setSoundSink([&](AlarmLevel) { ++beeps; });

    fireAlarm(mgr, QStringLiteral("夜间报警"), AlarmLevel::Emergency);

    // ★ 核心断言只有一条,但它分两面:
    //   不响 —— 但报警【必须还在、还必须仍然算"未确认"】。
    //   如果实现时图省事,静默时段里直接把触发扔掉,这两条都会红。
    //   "不响"只关掉声音,绝不等于"这件事没发生" ——
    //   这正是 F2-c 屏蔽定下的同一条原则:静音不能等于隐藏。
    //
    //   pendingReminderCount()【刻意】不看静默时段,就是为此:
    //   夜班界面显示"0 条报警"、而活动列表里躺着一条没处理的 Emergency,
    //   那才叫藏起来了。
    QCOMPARE(beeps, 0);
    QCOMPARE(mgr.count(), 1);
    QCOMPARE(snd.pendingReminderCount(), 1);
}

void TestAlarmSound::repeatTick_beepsOnceEvenWithSeveralActiveAlarms() {
    AlarmManager mgr;
    AlarmSoundPlayer snd(&mgr);
    AlarmSoundPlayer::Settings s = snd.settings();
    s.repeatUntilAck  = true;
    s.repeatIntervalMs = 500;    // 下限 500,设最小值免得测试等 5 秒
    snd.setSettings(s);

    int beeps = 0;
    snd.setSoundSink([&](AlarmLevel) { ++beeps; });

    fireAlarm(mgr, QStringLiteral("报警A"), AlarmLevel::Warning);
    fireAlarm(mgr, QStringLiteral("报警B"), AlarmLevel::Critical);
    fireAlarm(mgr, QStringLiteral("报警C"), AlarmLevel::Critical);
    QCOMPARE(beeps, 3);          // 触发时各响一次

    // 等重复定时器转一圈
    QTest::qWait(700);

    // ★ 三条未确认,只应该【再响一声】,不是三声。
    //   现场同时挂五条未确认时响五下,只会变成一片噪音,
    //   操作员听不出严重程度,唯一的结果是把所有声音都关掉。
    QCOMPARE(beeps, 4);
}

void TestAlarmSound::repeatTick_usesHighestLevel() {
    AlarmManager mgr;
    AlarmSoundPlayer snd(&mgr);
    AlarmSoundPlayer::Settings s = snd.settings();
    s.repeatUntilAck  = true;
    s.repeatIntervalMs = 500;
    snd.setSettings(s);

    QList<AlarmLevel> heard;
    snd.setSoundSink([&](AlarmLevel lv) { heard.append(lv); });

    fireAlarm(mgr, QStringLiteral("报警A"), AlarmLevel::Warning);
    fireAlarm(mgr, QStringLiteral("报警B"), AlarmLevel::Emergency);
    QTest::qWait(700);

    // 第二次(定时器那次)必须按最高档响
    QVERIFY(heard.size() >= 2);
    QCOMPARE(heard.last(), AlarmLevel::Emergency);
}

void TestAlarmSound::repeatTick_stopsWhenAllAcknowledged() {
    AlarmManager mgr;
    AlarmSoundPlayer snd(&mgr);
    AlarmSoundPlayer::Settings s = snd.settings();
    s.repeatUntilAck  = true;
    s.repeatIntervalMs = 500;
    snd.setSettings(s);

    int beeps = 0;
    snd.setSoundSink([&](AlarmLevel) { ++beeps; });

    const qint64 id = fireAlarm(mgr, QStringLiteral("压力高"), AlarmLevel::Critical);
    QCOMPARE(beeps, 1);

    mgr.acknowledge(id);

    QTest::qWait(1200);   // 足够转两圈

    // ★ 确认之后彻底安静 —— 而且【一直】安静。
    //   只测"下一圈不响"是不够的:某处若留着定时器不关,
    //   现象是"偶尔又响一下",现场会当成闹鬼,极难定位。
    QCOMPARE(beeps, 1);
}

// ============================================================================
// 边界
// ============================================================================

void TestAlarmSound::settings_outOfRangeValuesAreClamped() {
    AlarmManager mgr;
    AlarmSoundPlayer snd(&mgr);
    AlarmSoundPlayer::Settings s = snd.settings();
    s.repeatIntervalMs = 1;        // 用户手滑拖到了 1ms
    s.quietStartMin    = -500;     // 负数
    s.quietEndMin      = 99999;    // 大到离谱
    snd.setSettings(s);

    const AlarmSoundPlayer::Settings out = snd.settings();
    QVERIFY2(out.repeatIntervalMs >= 500, "间隔过小会变成每秒几十声,直接变成噪音");
    QVERIFY(out.quietStartMin >= 0 && out.quietStartMin < 24 * 60);
    QVERIFY(out.quietEndMin   >= 0 && out.quietEndMin   < 24 * 60);
}

void TestAlarmSound::noManager_isSafeNotCrash() {
    // 没有 AlarmManager 就构造 —— 不能崩。
    // 【为什么单独测这条】sound 模块理论上总会有 manager,
    // 但"理论上"这种话在这个项目里已经错过好几次了
    // (EF1 "理论上不会重复连接"、F1 "理论上会先 erase 再 emit")。
    AlarmSoundPlayer snd(nullptr);
    QCOMPARE(snd.pendingReminderCount(), 0);
    QVERIFY(!snd.inQuietHours(QDateTime::currentDateTime()));
}

// ============================================================================
// 静默时段结束后补响
// ============================================================================

void TestAlarmSound::quietHours_alarmIsRemindedAfterQuietHoursEnd() {
    AlarmManager mgr;
    AlarmSoundPlayer snd(&mgr);
    AlarmSoundPlayer::Settings s = snd.settings();
    s.repeatUntilAck  = true;
    s.repeatIntervalMs = 500;
    s.quietEnabled    = true;
    s.quietStartMin   = 0;
    s.quietEndMin     = 1439;   // 先设成"全天静默"
    snd.setSettings(s);

    int beeps = 0;
    snd.setSoundSink([&](AlarmLevel) { ++beeps; });

    fireAlarm(mgr, QStringLiteral("夜班报警"), AlarmLevel::Emergency);
    QCOMPARE(beeps, 0);
    QCOMPARE(snd.pendingReminderCount(), 1);

    // 关掉静默时段 —— 模拟"早班来了,免打扰结束"
    s.quietEnabled = false;
    snd.setSettings(s);

    // ★ 这条曾经是红的,而且红得有道理:
    //   静默时段里 decide() 全返回 SilencedByQuietHours,老代码据此把
    //   重复提醒的定时器停掉了 —— 而且【没有任何东西会再把它启动起来】。
    //   结果就是:夜里响过的 Critical,天亮后一声不响,直到它自己恢复。
    //   而它恰恰是夜班里最该被补响的那一条。
    QTest::qWait(900);
    QVERIFY2(beeps > 0, "静默时段结束后,未确认的报警必须补响 —— 否则夜里响过的东西天亮就没人知道了");
}

// ============================================================================
// 设置界面
// ============================================================================

namespace {

// 真去点「确定」按钮 —— 不直接调 saveToPlayer()。
// 【为什么必须这样】用户报的问题永远是"点了没反应 / 改了没生效"。
// 直接调私有方法只能证明那段代码对,证明不了"按钮 → 槽 → 写回 player"
// 这整条线接上了、按钮没被禁用、信号连对了。
// F3 在 AlarmListControl 上已经栽过这个(见其 .h:47 的说明)。
QPushButton* findOkButton(QWidget* dlg) {
    auto* box = dlg->findChild<QDialogButtonBox*>();
    if (!box) return nullptr;
    return box->button(QDialogButtonBox::Ok);
}

} // namespace

void TestAlarmSound::dialog_opensShowingCurrentSettings() {
    AlarmManager mgr;
    AlarmSoundPlayer snd(&mgr);

    AlarmSoundPlayer::Settings s = snd.settings();
    s.masterEnabled = false;
    s.levelEnabled[2] = false;      // Critical 关掉
    s.repeatIntervalMs = 12000;
    s.quietEnabled  = true;
    s.quietStartMin = 22 * 60;
    s.quietEndMin   = 7 * 60;
    snd.setSettings(s);

    qwhmi::alarm::AlarmSoundDialog dlg(&snd);

    // ★ 对话框打开时必须显示【真实当前值】,不能是出厂默认。
    //   显示错的界面比没有界面更糟:用户会以为自己的设置没保存成功。
    QVERIFY(!dlg.masterEnabled());
    QVERIFY(!dlg.levelEnabled(2));
    QVERIFY(dlg.quietEnabled());
    QCOMPARE(dlg.quietStartText(), QStringLiteral("22:00"));
    QCOMPARE(dlg.quietEndText(),   QStringLiteral("07:00"));
}

void TestAlarmSound::dialog_okWritesSettingsIntoPlayer() {
    AlarmManager mgr;
    AlarmSoundPlayer snd(&mgr);

    qwhmi::alarm::AlarmSoundDialog dlg(&snd);
    QPushButton* ok = findOkButton(&dlg);
    QVERIFY2(ok, "对话框里找不到「确定」按钮 —— 用户按什么?");

    dlg.findChild<QCheckBox*>(QStringLiteral("alarmSoundMasterChk"))->setChecked(false);
    dlg.findChild<QCheckBox*>(QStringLiteral("alarmSoundLevelChk0"))->setChecked(true);
    dlg.findChild<QCheckBox*>(QStringLiteral("alarmSoundQuietChk"))->setChecked(true);

    ok->click();

    // ★ 断言落到 player 上,不是断言界面上的复选框。
    //   只测界面 = 只测了"用户改了他看得见的东西",
    //   完全测不到"到底有没有写进真正生效的那份设置"。
    const AlarmSoundPlayer::Settings out = snd.settings();
    QCOMPARE(out.masterEnabled, false);
    QCOMPARE(out.levelEnabled[0], true);    // Info 改成了响
    QCOMPARE(out.quietEnabled, true);
}

void TestAlarmSound::dialog_cancelChangesNothing() {
    AlarmManager mgr;
    AlarmSoundPlayer snd(&mgr);

    const AlarmSoundPlayer::Settings before = snd.settings();

    {
        qwhmi::alarm::AlarmSoundDialog dlg(&snd);
        dlg.findChild<QCheckBox*>(QStringLiteral("alarmSoundMasterChk"))->setChecked(false);
        auto* cancel = dlg.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Cancel);
        QVERIFY(cancel);
        cancel->click();
    }

    // ★ 取消必须真的什么都不写。
    //   "先写进去再取消"是最自然的一种写错 —— 界面回到原样了,
    //   而声音行为已经变了,而且下次打开对话框【显示的还是旧值】,
    //   用户会以为程序有 bug,其实是他刚才"没保存"的那次改动生效了。
    const AlarmSoundPlayer::Settings after = snd.settings();
    QCOMPARE(after.masterEnabled, before.masterEnabled);
    QCOMPARE(after.quietEnabled,  before.quietEnabled);
    QCOMPARE(after.repeatUntilAck, before.repeatUntilAck);
    QCOMPARE(after.repeatIntervalMs, before.repeatIntervalMs);
}

QTEST_MAIN(TestAlarmSound)
#include "tst_AlarmSound.moc"