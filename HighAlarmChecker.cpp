// 阶段 5.3(2026-08-04):HighAlarmChecker 实现

#include "HighAlarmChecker.h"
#include "Variable.h"

HighAlarmChecker::HighAlarmChecker(const QString& alarmName,
                                   Variable* variable,
                                   double threshold,
                                   AlarmLevel level,
                                   QObject* parent)
    : AlarmChecker(parent), variable_(variable), threshold_(threshold)
{
    setName(alarmName);
    setLevel(level);
    if (variable_) {
        // ★ IMPROVE-5.3(2026-08-04):订阅单 Variable 而非 VariableManager
        //   【背景】VariableManager 没 valueChanged 信号,信号在每个 Variable 自身
        //   【改法】每个 HighAlarmChecker 只订阅它要监控的那个 Variable
        //   【取舍】一对多订阅才用 VariableManager::allVariables();现在一对一更直接
        connect(variable_, &Variable::valueChanged, this, &HighAlarmChecker::onValueChanged);
    }
}

HighAlarmChecker::~HighAlarmChecker() = default;

// ★ BUGFIX-5.3(2026-08-04):quality != Good 不触发
//   【背景】PLC 断线时 Variable 会被标 Bad,值是旧的 — 不能再用旧值触发报警
//   【改法】只有 Good 质量才走阈值判断
//   【取舍】Stale(陈旧)也跳过,只信任实时值
void HighAlarmChecker::onValueChanged(const Variable& v) {
    if (v.quality() != Variable::Quality::Good) return;

    bool ok = false;
    const double val = v.value().toDouble(&ok);
    if (!ok) return;  // 类型转换失败

    const bool overThreshold = (val > threshold_);

    if (overThreshold && !active_) {
        // ★ IMPROVE-5.3(2026-08-04):防重复触发
        //   【取舍】active_ 标志确保"已触发就不要再 emit";值持续超限不会反复 emit
        //   ★ BUGFIX-5.5(2026-08-04 Wqian 反馈):生成唯一 ID 并保存
        active_ = true;
        Alarm a;
        a.id_           = QDateTime::currentMSecsSinceEpoch();  // 唯一 ID,避免同名报警匹配错
        currentId_      = a.id_;
        a.name          = name_;
        a.variableName  = v.name();
        a.level         = level_;
        a.state         = AlarmState::Active;
        a.triggeredAt   = QDateTime::currentDateTime();
        a.triggerValue  = QString::number(val);
        a.threshold     = QString("> %1").arg(threshold_);
        emit triggered(a);
    } else if (!overThreshold && active_) {
        // ★ IMPROVE-5.3(2026-08-04):恢复判定 = 值回到阈值以下
        //   【取舍】严格 > 才触发,<= 都算"正常",避免边界值反复触发/恢复
        //   ★ BUGFIX-5.5(2026-08-04 Wqian 反馈):用保存的 ID 发 recovered
        active_ = false;
        emit recovered(currentId_);
        currentId_ = 0;
    }
}