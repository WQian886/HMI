// ★ IMPROVE-5.3-收尾(2026-08-07):合并 High+Low + 加最小持续时间

#include "ThresholdAlarmChecker.h"
#include "PollScheduler.h"  // ★ BUGFIX-3(2026-08-05)保留:需要完整定义才能 connect PollScheduler::rawRead
#include "Variable.h"
#include "src/logging/Logging.h"   // ★ F4-b:hmiAlarm 分类
#include <QTimer>

ThresholdAlarmChecker::ThresholdAlarmChecker(const QString& alarmName,
                                             Variable* variable,
                                             PollScheduler* pollScheduler,
                                             const CheckerConfig& config,
                                             QObject* parent)
    : AlarmChecker(parent),
      variable_(variable),
      pollScheduler_(pollScheduler),
      config_(config)
{
    setName(alarmName);
    setLevel(config_.level);

    // ★ IMPROVE-5.3-收尾(2026-08-07):最小持续时间定时器(防抖)
    //   【背景】PLC 模拟信号在阈值附近会抖;值刚好过线一下(< 500ms)不该报
    //   【改法】QTimer 单次触发:值超限 → start(500);到点再确认仍处于"待触发"→ emit triggered
    //          值回到正常 → timer.stop() 取消未触发的定时器
    //   【取舍】单次 + setSingleShot(true) 够用;抖动场景下天然过滤(抖回正常 → 取消)
    //   【参照】PLC 程序员的 TON 定时器(同样功能)
    durationTimer_ = new QTimer(this);
    durationTimer_->setSingleShot(true);
    connect(durationTimer_, &QTimer::timeout, this, [this]() {
        // 计时到点:二次确认状态(防止值已经回到正常但定时器未及时 stop 的边界情况)
        //   ★ F2:判据从 active_ 换成 state_ == Pending —— 必须是"仍待触发"才发,
        //   Fired 状态说明已经报过了,不能再报第二次。
        if (state_ != State::Pending) return;
        state_ = State::Fired;
        Alarm a;
        a.id_           = QDateTime::currentMSecsSinceEpoch();
        currentId_      = a.id_;
        a.name          = name_;
        a.variableName  = variable_ ? variable_->name() : QString();
        a.level         = level_;
        a.state         = AlarmState::Active;
        a.triggeredAt   = QDateTime::currentDateTime();
        a.triggerValue  = QString::number(lastValue_);   // 触发时的值快照
        a.threshold     = (config_.direction == Direction::Above)
                          ? QString("> %1").arg(config_.threshold)
                          : QString("< %1").arg(config_.threshold);
        // ★ F6:区域从配置原样抄过来。抄【触发那一刻】的 config_.area,
        //   不是构造时的 —— setConfig() 能在运行中改配置(见 .h 里为什么不删了重建),
        //   用户中途改了区域,下一条记录就该是新的那个,不是旧的。
        a.area          = config_.area;
        emit triggered(a);
    });

    // ★ BUGFIX-3(2026-08-05)保留:订阅 rawRead(原始值,绕开 Variable 死区)
    //   【背景】Variable.deadband_ 吞掉小波动 → Checker 走 valueChanged 看不到 50.0+ 的值 → 漏报
    //   【改法】订阅 PollScheduler::rawRead,直接拿原始 16-bit register
    //   【取舍】PollScheduler 一次发 1 个 16-bit register;Int32/Float 留后续补
    //   【参照】commit 1139603 阶段 5.3+ Bug #3 修复
    if (pollScheduler_) {
        connect(pollScheduler_, &PollScheduler::rawRead,
                this, &ThresholdAlarmChecker::onRawRead);
    }
}

ThresholdAlarmChecker::~ThresholdAlarmChecker() = default;

// ============================================================================
// ★ F4-b(2026-10-08):运行中改配置
//
// 【核心决定:不重建 checker,就地改,判定交给下一个采集周期】
//   详见 ThresholdAlarmChecker.h 里的说明。这里只补两个容易被忽略的细节。
// ============================================================================

void ThresholdAlarmChecker::setConfig(const CheckerConfig& cfg) {
    // level 存在基类的 level_ 里,config_.level 只是 CheckerConfig 的一份副本。
    // 只改 config_ 的话,报警【等级】这一列在界面上改了、实际报出来的还是旧等级 ——
    // 而等级决定颜色和声音,是操作员第一时间看的东西。
    setLevel(cfg.level);
    config_ = cfg;

    // ★ 坑一:Pending 状态下 minDurationMs 变了,必须重新起表。
    //   durationTimer_ 是在进入 Pending 那一刻用【当时的】minDurationMs 起的。
    //   如果用户此刻把 500 改成 5000,定时器还按 500 走 —— 500ms 后照样报出去,
    //   新配的 5000 完全没生效,而界面上明明白白写着 5000。
    //   这正是本项目反复吃的那类:配置改了、界面显示改了、行为没改,且没有任何提示。
    //
    //   为什么是【重新计时】而不是【按比例调整剩余时间】
    //   —— 重新计时更保守:新值总是从"现在"起算满,
    //   绝不会因为改配置而让一条正在酝酿的报警提前报出去。
    //   (提前报 = 误报,工业现场比晚报更危险。)
    if (state_ == State::Pending) {
        durationTimer_->start(config_.minDurationMs);
    }

    // ★ 坑二:Fired 状态下什么都不做,是【故意的】,不是漏了。
    //   阈值刚改完就自己判一次"还该不该响",看着更聪明,其实是错的:
    //   lastValue_ 是【上一次采集】的值,可能是几秒前的。
    //   拿一个陈旧的值去改报警的生死,等于在传感器断线时也会"恢复"一条报警。
    //   正确做法是什么都不做 —— 下一个 rawRead 自然会用新阈值重新走一遍
    //   onRawRead 的状态机,那时手里的是新鲜值。
    //
    //   代价:改完配置到生效最多有一个采集周期的延迟(通常 100ms 量级)。
    //   这个延迟对着一台 7×24 的机器是可以接受的,换成"用陈旧值误判"不可接受。
    if (state_ == State::Fired) {
        qCDebug(hmiAlarm)
            << "[F4-b] 报警" << name_ << "配置已改,当前正在响;"
               "将在下一个采集周期按新阈值重新判定";
    }
}

void ThresholdAlarmChecker::setVariable(Variable* v) {
    // ★ 为什么调用方必须拦住"正在响时换变量"
    //   activeAlarms_ 里那条记录的 variableName 是【触发那一刻】的变量名。
    //   正在响的时候把变量换掉,那条记录会永远挂着旧变量名,
    //   而恢复它的新判定用的是新变量的值 —— 历史表里会留下
    //   "A 变量触发、被 B 变量恢复"这种自相矛盾的一行。
    //   和改阈值不同:改阈值后新判定的还是同一个变量,记录自洽;
    //   换变量打破了这个前提,所以这里不做,要换先得让它恢复。
    //
    //   调用方(AlarmConfigDialog)据此拒绝并提示用户,不是静默拒绝。
    variable_ = v;
    // lastValue_ 是【旧变量】的采样值。换变量不清它的话,下一次新变量的
    // rawRead 之前,lastValue_ 仍会被读成旧变量的值 —— 界面上"当前值"会显示
    // 一个根本不在这条报警上的量。清掉,让触发快照只能是新变量的值。
    lastValue_ = 0.0;
    qCDebug(hmiAlarm) << "[F4-b] 报警" << name_ << "改监控变量 ->"
                      << (v ? v->name() : QStringLiteral("(无)"));
}

// ★★ 2026-10-19:onVariableReplaced 已删除,理由见 .h 里同名那一段。
//   它从来没有被 connect 过,所以"变量死了就换指针"这件事实际上没发生过;
//   改成 QPointer 之后,变量一被 delete 指针自动变空,不再依赖有人通知。

void ThresholdAlarmChecker::onRawRead(Variable* v, quint16 rawValue) {
    if (v != variable_) return;                       // ★ 只关心自己监控的 Variable
    if (!variable_) return;
    if (variable_->quality() != Variable::Quality::Good) return;  // ★ PLC 断线不触发

    // 把原始 register 转 double(只支持 Int16/UInt16/Bool,Int32/Float 后续再补)
    bool ok = false;
    double val = 0.0;
    switch (variable_->dataType()) {
        case Variable::DataType::Int16:
            val = static_cast<double>(static_cast<qint16>(rawValue));
            ok = true;
            break;
        case Variable::DataType::UInt16:
            val = static_cast<double>(rawValue);
            ok = true;
            break;
        case Variable::DataType::Bool:
            val = (rawValue & 0x01) ? 1.0 : 0.0;
            ok = true;
            break;
        default:
            return;
    }
    if (!ok) return;
    lastValue_ = val;   // 不管超不超限都记(给触发时的 triggerValue 用)

    // ★ IMPROVE-5.3-收尾(2026-08-07):方向参数化(> Above / < Below)
    //   【改法】switch 按 config_.direction 决定比较方向,合并 High/Lov 两份逻辑
    //   【取舍】二元方向够 5.3 用;区间(<a && >b)留阶段 7+
    //
    // ★ F2(2026-09-30):【触发沿 vs 恢复沿分开】
    //   overThreshold  = 触发沿,仍严格看 threshold(滞回不该让报警变迟钝)
    //   pastRecovery   = 恢复沿,看 threshold ∓ hysteresis
    //   hysteresis=0 时两者恒等 → 行为与 5.3 逐位相同。
    bool overThreshold = false;
    double recoveryLimit = config_.threshold;
    switch (config_.direction) {
        case Direction::Above:
            overThreshold = (val > config_.threshold);
            recoveryLimit = config_.threshold - config_.hysteresis;
            break;
        case Direction::Below:
            overThreshold = (val < config_.threshold);
            recoveryLimit = config_.threshold + config_.hysteresis;
            break;
    }
    // 已触发过之后,恢复要用带滞回的沿;还在"待触发"时退回触发沿(否则
    // 一个本来就没报出去的报警,会因为滞回永远挂着 pending —— 那是另一种静默漏报)。
    const bool pastRecovery = (state_ == State::Fired)
                           ? !(val > recoveryLimit)   // Above:跌到 threshold-hyst 以下才算恢复
                           : !overThreshold;           // 待触发阶段:回到 threshold 内即撤销

    switch (state_) {
    case State::Idle:
        if (overThreshold) {
            state_ = State::Pending;
            durationTimer_->start(config_.minDurationMs);
        }
        break;

    case State::Pending:
        // 抖回来了 → 撤销待触发。这是 minDurationMs 存在的全部意义。
        if (pastRecovery) {
            state_ = State::Idle;
            durationTimer_->stop();
        }
        break;

    case State::Fired:
        // ★ 已报警,等恢复沿。滞回在这一档起作用:值在死区里来回不算恢复,
        //   所以不会"恢复→再触发"刷出第二条记录(chatter 的根因)。
        if (pastRecovery) {
            state_ = State::Idle;
            durationTimer_->stop();
            if (currentId_ != 0) {
                emit recovered(currentId_);
                currentId_ = 0;
            }
        }
        break;
    }
}