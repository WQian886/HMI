// 阶段 F · F5(2026-10-08):报警声音 / 蜂鸣 —— 实现
//
// 判定逻辑全部是纯函数,只有最后一行的 doBeep 碰真实声卡。
// 这样"响不响"的每一种分支都能被 tst_AlarmSound 覆盖,
// 而"声音有没有真的出来"人耳验一次即可(见 .h 顶部的提醒)。

#include "AlarmSoundPlayer.h"

#include "AlarmManager.h"
#include "Alarm.h"
#include "logging/Logging.h"

#include <QTimer>

#ifdef Q_OS_WIN
#  include <windows.h>
#endif

namespace {

// 把 Decision 变成能进日志的字符串。
//
// ★ 为什么每一种"故意不响"都要留名字
//   现场反馈"报警不响"时,能直接说出是哪一种,排查时间从半小时缩到一分钟。
//   说不出来就只能挨个改配置试 —— 那正是本项目吃过多次亏的"静默失败"。
const char* decisionName(AlarmSoundPlayer::Decision d) {
    switch (d) {
    case AlarmSoundPlayer::Decision::Beep:                  return "响";
    case AlarmSoundPlayer::Decision::SilencedBySwitch:      return "总开关关闭";
    case AlarmSoundPlayer::Decision::SilencedByLevel:       return "该等级已单独关闭";
    case AlarmSoundPlayer::Decision::SilencedByAcknowledged:return "已确认";
    case AlarmSoundPlayer::Decision::SilencedByQuietHours:  return "静默时段内";
    case AlarmSoundPlayer::Decision::DuplicateSuppressed:   return "重复触发已去重";
    }
    return "未知";
}

// ★ 4 档 → 4 种【互不相同】的系统音。
//   MB_ICONHAND 最刺耳,所以留给 Emergency —— 危急报警必须是"最难受"的那个,
//   操作员不该只靠看颜色才知道事情严重。
//
//   单独抽成自由函数而不是留在 doBeep 里,是因为"试听"和"报警要响"
//   必须走【同一段代码】:两处各写一份,迟早有人只改其中一处,
//   于是用户听到的试听和现场实际响的不是同一个声音。
void playSystemSound(AlarmLevel level) {
#ifdef Q_OS_WIN
    UINT type = MB_OK;
    switch (level) {
    case AlarmLevel::Info:      type = MB_ICONASTERISK;      break;  // 轻的一声"叮"
    case AlarmLevel::Warning:   type = MB_ICONEXCLAMATION;   break;  // "注意!"
    case AlarmLevel::Critical:  type = MB_OK;                break;
    case AlarmLevel::Emergency: type = MB_ICONHAND;          break;  // 刺耳的连续错误音
    }
    // MessageBeep 是【异步】的(投递声卡立即返回),不会卡住事件循环。
    //
    // ★ 但它听系统的"声音方案"设置:用户在 Windows 里把系统音关掉,
    //   这里就一声不响。这是平台行为,本程序绕不过去 ——
    //   真要不受影响就得自己播 wav,那正是 .h 里说的"以后再说"。
    //   【所以"试听"这一条必须留着】用户唯一能确认声音通道通不通的办法。
    ::MessageBeep(type);
#else
    // 非 Windows:没有等价物。这里【不静默失败】——
    // 编译期就会看见(本项目只发 Windows 版),而不是运行时一声不响。
    Q_UNUSED(level)
#endif
}

} // namespace

// ---------------------------------------------------------------------------
// 等级下标
// ---------------------------------------------------------------------------

int AlarmSoundPlayer::levelIndex(AlarmLevel level) {
    // ★ 不写 static_cast<int>(level):那等于把"枚举的数值"当成"配置数组的下标"。
    //   将来有人调整 AlarmLevel 的声明顺序(比如把 Emergency 提到最前),
    //   静默时段、每档开关会【全部静默错位】,而编译通过、一切照跑。
    //   显式 switch 慢得可以忽略,换来的是"改顺序时编译器会提醒你"。
    switch (level) {
    case AlarmLevel::Info:      return 0;
    case AlarmLevel::Warning:   return 1;
    case AlarmLevel::Critical:  return 2;
    case AlarmLevel::Emergency: return 3;
    }
    // 兜底用 Warning(默认会响):宁可多响,也不能因为一个没见过的等级值而集体静默。
    return 1;
}

int AlarmSoundPlayer::levelCount() { return 4; }

// ---------------------------------------------------------------------------
// 构造 / 析构
// ---------------------------------------------------------------------------

AlarmSoundPlayer::AlarmSoundPlayer(AlarmManager* mgr, QObject* parent)
    : QObject(parent), mgr_(mgr) {
    // 【为什么默认 sink 是 nullptr 而不是直接 MessageBeep】
    //   doBeep 里 "有 sink 用 sink,没有才走系统音"。
    //   测试不注入 sink 时会真的发声 —— 62 个测试并行跑时,
    //   几十个测试进程一起 beep 是很糟的体验。测试一律注入。
    sink_ = nullptr;

    repeatTimer_ = new QTimer(this);
    repeatTimer_->setInterval(settings_.repeatIntervalMs);
    connect(repeatTimer_, &QTimer::timeout, this, &AlarmSoundPlayer::onRepeatTick);

    if (mgr_) {
        connect(mgr_, &AlarmManager::alarmTriggered,    this, &AlarmSoundPlayer::onTriggered);
        connect(mgr_, &AlarmManager::alarmAcknowledged, this, &AlarmSoundPlayer::onAcknowledged);
        connect(mgr_, &AlarmManager::alarmRecovered,    this, &AlarmSoundPlayer::onRecovered);
        connect(mgr_, &AlarmManager::allCleared,        this, &AlarmSoundPlayer::onAllCleared);
    }
}

AlarmSoundPlayer::~AlarmSoundPlayer() = default;

// ---------------------------------------------------------------------------
// 设置
// ---------------------------------------------------------------------------

AlarmSoundPlayer::Settings AlarmSoundPlayer::settings() const { return settings_; }

void AlarmSoundPlayer::setSettings(const Settings& s) {
    settings_ = s;
    // 静默时段要有意义,两端必须同在一天之内。超范围直接夹住而不是忽略 ——
    // 忽略的话"静默 25:00-06:00"这种手滑配置会静悄悄地不生效,用户以为设好了。
    const int kDay = 24 * 60;
    settings_.quietStartMin = qBound(0, settings_.quietStartMin, kDay - 1);
    settings_.quietEndMin   = qBound(0, settings_.quietEndMin,   kDay - 1);
    settings_.repeatIntervalMs = qMax(500, settings_.repeatIntervalMs);

    repeatTimer_->setInterval(settings_.repeatIntervalMs);

    // 【为什么改了设置要立刻重算一次定时器】
    //   用户刚点开声音(从关到开),如果此时正有未确认的 Critical,
    //   他期望下一声很快听到,而不是干等一整个旧周期。
    const bool wantRun = settings_.repeatUntilAck && pendingReminderCount() > 0;
    if (wantRun && !repeatTimer_->isActive())    repeatTimer_->start();
    if (!wantRun && repeatTimer_->isActive())    repeatTimer_->stop();
}

void AlarmSoundPlayer::setSoundSink(std::function<void(AlarmLevel)> sink) {
    sink_ = std::move(sink);
}

// ---------------------------------------------------------------------------
// 判定(纯函数)
// ---------------------------------------------------------------------------

bool AlarmSoundPlayer::inQuietHours(const QDateTime& now) const {
    if (!settings_.quietEnabled) return false;

    const int minute = now.time().hour() * 60 + now.time().minute();
    const int start  = settings_.quietStartMin;
    const int end    = settings_.quietEndMin;

    // ★ start == end 判为"没有静默时段",而不是"全天静默"。
    //   理由:想全天不响,用户已经有总开关了,不需要绕时段来表达。
    //   而反过来 —— 把 00:00-00:00 解释成"全天静默" —— 会让一个手滑
    //   (想设 22:00-06:00 结果拖错了框)变成"声音彻底没了且没有任何提示"。
    //   两种解释都有人这么用,但"没配"这个解释的破坏性更小、更好排查。
    if (start == end) return false;

    // 跨零点(22:00 → 07:00)是常态,必须单独处理:
    // 直接写 m >= start && m < end 会让跨零点的时段【永远不生效】——
    // 而这恰恰是现场最常配的夜班免打扰。
    if (start < end) return (minute >= start && minute < end);
    return (minute >= start || minute < end);
}

AlarmSoundPlayer::Decision AlarmSoundPlayer::decide(const Alarm& a, const QDateTime& now) const {
    if (!settings_.masterEnabled) return Decision::SilencedBySwitch;

    const int idx = levelIndex(a.level);
    if (idx < 0 || idx >= static_cast<int>(settings_.levelEnabled.size()))
        return Decision::SilencedByLevel;   // 理论上到不了这里,但静默错位比多响更糟
    if (!settings_.levelEnabled[static_cast<size_t>(idx)]) return Decision::SilencedByLevel;

    // 【为什么"已确认"排在"静默时段"前面】
    //   两个原因都会导致不响,但排查方向完全不同:
    //   前者说明操作员处理过了,后者说明这是班次配置问题。
    //   先判前者,日志里出现的就是更贴近现场的那个原因。
    if (a.state == AlarmState::Acknowledged || a.state == AlarmState::Cleared)
        return Decision::SilencedByAcknowledged;

    if (inQuietHours(now)) return Decision::SilencedByQuietHours;

    return Decision::Beep;
}

int AlarmSoundPlayer::pendingReminderCount() const {
    if (!mgr_) return 0;
    int n = 0;
    const QList<Alarm> actives = mgr_->activeAlarms();
    for (const Alarm& a : actives) {
        if (a.state == AlarmState::Acknowledged || a.state == AlarmState::Cleared) continue;
        // 等级被单独关掉的不算"正在提醒",否则界面会显示"N 条正在提醒"
        // 而实际一声不响 —— 又是一次"界面和实际不一致"。
        const int idx = levelIndex(a.level);
        if (idx < 0 || idx >= static_cast<int>(settings_.levelEnabled.size())) continue;
        if (!settings_.levelEnabled[static_cast<size_t>(idx)]) continue;
        ++n;
    }
    return n;
}

// ---------------------------------------------------------------------------
// 实际发声
// ---------------------------------------------------------------------------

void AlarmSoundPlayer::doBeep(AlarmLevel level) {
    ++beepCount_;
    if (sink_) {   // 测试路径:不碰声卡
        sink_(level);
        return;
    }
    playSystemSound(level);
}

void AlarmSoundPlayer::previewBeep(AlarmLevel level) {
    // ★ 故意不走 sink_:测试注入的 sink 是个计数器,
    //   试听若经过它,单测里的"响了几次"会被用户点试听污染。
    playSystemSound(level);
}

// ---------------------------------------------------------------------------
// 信号处理
// ---------------------------------------------------------------------------

void AlarmSoundPlayer::onTriggered(const Alarm& a) {
    const QDateTime now = QDateTime::currentDateTime();
    const Decision d = decide(a, now);

    if (d != Decision::Beep) {
        // ★ 静默时段内的不响【必须留痕】。
        //   不留痕的话,夜班发生的 Critical 报警会彻底"没发生过"——
        //   声音没有、没人看见、第二天早上谁也不知道昨夜响过。
        //   而报警本身已经进了 activeAlarms_ 和 SQLite,这里只是把
        //   "当时为什么不响"记下来,供追查。
        if (d == Decision::SilencedByQuietHours) {
            suppressedByQuietHours_.insert(a.id_);
            qCInfo(hmiAlarm) << "[F5] 静默时段内触发,不响 name=" << a.name
                             << "level=" << static_cast<int>(a.level)
                             << "(报警本身照常记录与显示)";
        }
        return;
    }

    doBeep(a.level);
    qCDebug(hmiAlarm) << "[F5] 响 name=" << a.name << "level=" << static_cast<int>(a.level);

    if (settings_.repeatUntilAck && !repeatTimer_->isActive())
        repeatTimer_->start();
}

void AlarmSoundPlayer::onAcknowledged(qint64 id) {
    Q_UNUSED(id);
    // 【为什么这里什么都不做,只靠定时器重新评估】
    //   因为"确认了就不响"是 decide() 里的规则,不是这里的 if。
    //   规则只写一份,就不会出现"这里忘了改、那里还按旧规则响"。
    //   这里唯一要做的就是:如果已经没东西要提醒了,把定时器停掉。
    if (pendingReminderCount() == 0 && repeatTimer_->isActive())
        repeatTimer_->stop();
}

void AlarmSoundPlayer::onRecovered(qint64 id, const Alarm& recovered) {
    Q_UNUSED(recovered);
    suppressedByQuietHours_.remove(id);
    if (pendingReminderCount() == 0 && repeatTimer_->isActive())
        repeatTimer_->stop();
}

void AlarmSoundPlayer::onAllCleared() {
    suppressedByQuietHours_.clear();
    repeatTimer_->stop();
}

void AlarmSoundPlayer::onRepeatTick() {
    if (!mgr_) return;
    if (!settings_.repeatUntilAck) { repeatTimer_->stop(); return; }

    const QDateTime now = QDateTime::currentDateTime();

    // ★ 一次 tick【只响一声,按最高等级响】,不是"有几条未确认就响几声"。
    //   现场同时挂着 5 条未确认报警时,后者会变成一片噪音,
    //   操作员听不出严重程度,只嫌烦 —— 而"嫌烦"的结果就是把所有声音都关掉。
    //   一次一声 + 取最高档,既保留了"有多严重"的信息,又不至于刷屏。
    bool found = false;
    int  highest = -1;
    const QList<Alarm> actives = mgr_->activeAlarms();
    for (const Alarm& a : actives) {
        if (decide(a, now) != Decision::Beep) continue;
        const int idx = levelIndex(a.level);
        if (idx > highest) highest = idx;
        found = true;
    }

    if (!found) {
        // ★ 这里【必须】按"有没有待处理的报警"来停,不能按"这一圈有没有响"。
        //   差别在静默时段:静默时段里 decide() 全部返回 SilencedByQuietHours,
        //   found 恒为 false —— 定时器会在 22:00 停掉,然后【永远不会再启动】,
        //   于是 07:00 出了静默时段之后,那条报警一声不响,直到它自己恢复。
        //   而它恰好是夜班里最该被补响的那一条。
        if (pendingReminderCount() == 0) repeatTimer_->stop();
        return;
    }

    // highest 是下标,反查回等级 —— 不留"下标当等级用"的中间态。
    static const AlarmLevel kByIndex[4] = { AlarmLevel::Info, AlarmLevel::Warning,
                                             AlarmLevel::Critical, AlarmLevel::Emergency };
    doBeep(kByIndex[highest]);
}