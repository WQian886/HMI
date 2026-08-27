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
    //   【改法】QTimer 单次触发:值超限 → start(500);到点再确认 active_ 还为 true → emit triggered
    //          值回到正常 → timer.stop() 取消未触发的定时器
    //   【取舍】单次 + setSingleShot(true) 够用;抖动场景下天然过滤(抖回正常 → 取消)
    //   【参照】PLC 程序员的 TON 定时器(同样功能)
    durationTimer_ = new QTimer(this);
    durationTimer_->setSingleShot(true);
    connect(durationTimer_, &QTimer::timeout, this, [this]() {
        // 计时到点:二次确认 active_ 状态(防止值已经回到正常但定时器未及时 stop 的边界情况)
        if (!active_) return;
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
    bool overThreshold = false;
    switch (config_.direction) {
        case Direction::Above: overThreshold = (val >  config_.threshold); break;
        case Direction::Below: overThreshold = (val <  config_.threshold); break;
    }

    if (overThreshold && !active_) {
        // 进入"待触发"状态:启动最小持续时间定时器
        //   timer 到点才真正 emit(给抖动留 cancel 的机会)
        active_ = true;
        durationTimer_->start(config_.minDurationMs);
    } else if (!overThreshold && active_) {
        // 值回到正常:取消未触发的定时器 + 如果已经触发过则发 recovered
        active_ = false;
        durationTimer_->stop();
        if (currentId_ != 0) {
            emit recovered(currentId_);
            currentId_ = 0;
        }
    }
    // 其他情况:超限但已 active(待触发 / 已触发)→ 不动;正常且未 active → 不动
}