// 阶段 5.2(2026-08-04):AlarmManager 实现

#include "AlarmManager.h"

AlarmManager::AlarmManager(QObject* parent) : QObject(parent) {}
AlarmManager::~AlarmManager() = default;

bool AlarmManager::addChecker(AlarmChecker* c) {
    if (!c || c->name().isEmpty() || checkers_.contains(c->name())) return false;
    c->setParent(this);  // 接管生命周期(析构时自动 delete)
    connect(c, &AlarmChecker::triggered, this, &AlarmManager::onCheckerTriggered);
    connect(c, &AlarmChecker::recovered, this, &AlarmManager::onCheckerRecovered);
    checkers_.insert(c->name(), c);
    return true;
}

bool AlarmManager::removeChecker(const QString& name) {
    auto it = checkers_.find(name);
    if (it == checkers_.end()) return false;
    delete it.value();  // setParent(this) 的对象 delete 会自动从 QObject 树移除
    checkers_.erase(it);
    return true;
}

bool AlarmManager::contains(qint64 id) const { return activeAlarms_.contains(id); }
int  AlarmManager::count() const { return activeAlarms_.size(); }

QList<Alarm> AlarmManager::activeAlarms() const {
    return activeAlarms_.values();
}

void AlarmManager::acknowledge(qint64 id) {
    auto it = activeAlarms_.find(id);
    if (it == activeAlarms_.end()) return;
    if (it->state != AlarmState::Active) return;  // 已经确认/清除就不重复
    it->state = AlarmState::Acknowledged;
    it->acknowledgedAt = QDateTime::currentDateTime();
    emit alarmAcknowledged(id);
}

void AlarmManager::clearAll() {
    if (activeAlarms_.isEmpty()) return;
    activeAlarms_.clear();
    emit allCleared();
}

// ★ BUGFIX-5.5(2026-08-04 Wqian 反馈):保留记录,只标记已确认
//   详见 h 文件说明
void AlarmManager::acknowledgeAll() {
    // ★ 拷贝 keys 避免遍历中容器被改(虽然 acknowledge 不删,但保险)
    const QList<qint64> ids = activeAlarms_.keys();
    for (qint64 id : ids) {
        acknowledge(id);  // 单个 acknowledge 已经会 emit alarmAcknowledged
    }
}

// ★ BUGFIX-5.2(2026-08-04):同一报警重复触发 → 不覆盖已有的(否则丢失原始 triggeredAt)
//   【取舍】保留首次触发时间,事故分析更准;重复触发只更新 triggerValue
//   ★ BUGFIX-5.5(2026-08-04 Wqian 反馈):改用 ID 匹配,避免同名报警错配
void AlarmManager::onCheckerTriggered(const Alarm& alarm) {
    auto it = activeAlarms_.find(alarm.id_);
    if (it != activeAlarms_.end()) {
        it->triggerValue = alarm.triggerValue;  // 更新当前值
        return;
    }
    activeAlarms_.insert(alarm.id_, alarm);
    emit alarmTriggered(alarm);
}

// ★ IMPROVE-5.2(2026-08-04):5.2 简化版——恢复直接从 activeAlarms_ 移除
//   【取舍】完整版应该保留 archived_ 存档,但 5.4 UI 暂时不需要
//   【后续】5.4 + 之后可加 QList<Alarm> archived_; clearAll() 才清掉
//   ★ BUGFIX-5.5(2026-08-04 Wqian 反馈):改用 ID 匹配
void AlarmManager::onCheckerRecovered(qint64 id) {
    auto it = activeAlarms_.find(id);
    if (it == activeAlarms_.end()) return;
    it->recoveredAt = QDateTime::currentDateTime();
    activeAlarms_.erase(it);
    emit alarmRecovered(id);
}