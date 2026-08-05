// 阶段 5.3(2026-08-04):HighAlarmChecker 子类(5.3 最小版本,无死区协调 + 无最小持续时间)
//   订阅单个 Variable 的 valueChanged,值超阈值 → emit triggered;值回到阈值 → emit recovered
//   5.3+ 后续才补:LowAlarmChecker / 死区协调 / 最小持续时间

#pragma once

#include "Alarm.h"

class Variable;

class HighAlarmChecker : public AlarmChecker {
    Q_OBJECT
public:
    // alarmName: 报警名("温度高")
    // variable:  要监控的变量(不接管生命周期,Variable 由 VariableManager 管)
    // threshold: 阈值(> 该值触发)
    // level:     报警等级
    HighAlarmChecker(const QString& alarmName,
                     Variable* variable,
                     double threshold,
                     AlarmLevel level,
                     QObject* parent = nullptr);
    ~HighAlarmChecker() override;

private slots:
    void onValueChanged(const Variable& v);

private:
    Variable* variable_;   // 不 delete(VariableManager 管)
    double    threshold_;
    bool      active_ = false;  // 当前是否已触发(防重复 emit)
    qint64    currentId_ = 0;   // 当前 active Alarm 的 ID(5.5 BUGFIX,恢复时需要)
};