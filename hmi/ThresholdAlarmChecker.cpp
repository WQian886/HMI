// ★ IMPROVE-5.3-收尾(2026-08-07):合并 High+Low + 加最小持续时间

#include "ThresholdAlarmChecker.h"
#include "PollScheduler.h"  // ★ BUGFIX-3(2026-08-05)保留:需要完整定义才能 connect PollScheduler::rawRead
#include "Variable.h"
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

// ★ 阶段 6.3(2026-08-07):loadFromFile 重建 Variable 时,自动更新 variable_ 指针
//   旧 Variable 被 delete,新 Variable 同名替换;由 hmi.cpp connect varMgr_->variableReplaced 触发
void ThresholdAlarmChecker::onVariableReplaced(Variable* oldVar, Variable* newVar) {
    if (oldVar == variable_) {
        variable_ = newVar;  // 可能是 nullptr(被 clear)
    }
}

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