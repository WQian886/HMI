// 阶段 5.2(2026-08-04):AlarmManager 实现

#include "AlarmManager.h"
#include "auth/CurrentUser.h"
#include "auth/Permission.h"

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
    // ★ B3(2026-09-17):确认报警前 checkPermission(AcknowledgeAlarm)
    if (!qwhmi::auth::checkPermission(qwhmi::auth::CurrentUser::instance().role(),
                              qwhmi::auth::Permission::AcknowledgeAlarm)) {
        qCWarning(qwhmi::auth::hmiPerm)
            << "本用户" << qwhmi::auth::CurrentUser::instance().name()
            << "无 AcknowledgeAlarm 权限,确认被拒 alarmId=" << id;
        return;
    }
    auto it = activeAlarms_.find(id);
    if (it == activeAlarms_.end()) return;
    if (it->state != AlarmState::Active) return;  // 已经确认/清除就不重复
    it->state = AlarmState::Acknowledged;
    it->acknowledgedAt = QDateTime::currentDateTime();
    emit alarmAcknowledged(id);
}

// ★ 阶段 6.3 FIX(2026-08-07):clearAll 必须真清一切 —— 不光清 activeAlarms_,也要清 checkers_
//   【背景】原来只清 active alarms → loadFromFile 调 clearAll 后,代码默认加的 checker(温度高/低)还在
//          → addChecker "温度高" 名字冲突 → 返回 false → emit loadError → 整个 loadFromFile 中止
//   【改法】delete 每个 checker(checkers_ 裸指针,addChecker 内部 setParent(this),delete 自动从 QObject 树移除)
//   【取舍】clearAll 名字模糊,严格说应该拆成 clearActiveAlarms() + clearCheckers(),
//          但 6.3 阶段先最小改动(合并清),6.4 阶段再考虑拆(影响面可控)
//   【参照】Wqian 2026-08-07 阶段 6.3 调试过程;Q4-A 拍板(内存 = JSON)
void AlarmManager::clearAll() {
    if (!activeAlarms_.isEmpty()) {
        activeAlarms_.clear();
    }
    if (!checkers_.isEmpty()) {
        for (auto it = checkers_.begin(); it != checkers_.end(); ++it) {
            delete it.value();   // setParent(this) 后 delete 会从 QObject 树自动移除
        }
        checkers_.clear();
    }
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