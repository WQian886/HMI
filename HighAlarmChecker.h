// 阶段 5.3(2026-08-04):HighAlarmChecker 子类(5.3 最小版本,无死区协调 + 无最小持续时间)
//   订阅单个 Variable 的 valueChanged,值超阈值 → emit triggered;值回到阈值 → emit recovered
//   5.3+ 后续才补:LowAlarmChecker / 死区协调 / 最小持续时间

#pragma once

#include "Alarm.h"

class Variable;
class PollScheduler;

class HighAlarmChecker : public AlarmChecker {
    Q_OBJECT
public:
    // alarmName:     报警名("温度高")
    // variable:      要监控的变量(不接管生命周期,Variable 由 VariableManager 管)
    // pollScheduler: 订阅 PollScheduler::rawRead(原始 PLC 值,死区过滤前)—— 修 Bug #3
    //                不接管生命周期(PollScheduler 由 hmi 管)
    // threshold:     阈值(> 该值触发)
    // level:         报警等级
    HighAlarmChecker(const QString& alarmName,
                     Variable* variable,
                     PollScheduler* pollScheduler,
                     double threshold,
                     AlarmLevel level,
                     QObject* parent = nullptr);
    ~HighAlarmChecker() override;

private slots:
    // ★ 阶段 5.3+ 修 Bug #3(2026-08-05):订阅 PollScheduler::rawRead(原始值)
    //   不再订阅 Variable::valueChanged(被 deadband 过滤过)
    //   v 可能是 PollScheduler 监控的任何 Variable,Checker 只关心 variable_
    void onRawRead(Variable* v, quint16 rawValue);

private:
    Variable*     variable_;       // 不 delete(VariableManager 管)
    PollScheduler* pollScheduler_; // 不 delete(hmi 管)
    double        threshold_;
    bool          active_ = false; // 当前是否已触发(防重复 emit)
    qint64        currentId_ = 0;  // 当前 active Alarm 的 ID(5.5 BUGFIX,恢复时需要)
};