// 阶段 5.6(2026-08-04):ActionManager 实现

#include "ActionManager.h"
#include "Alarm.h"
#include "Variable.h"

ActionManager::ActionManager(QObject* parent) : QObject(parent) {}
ActionManager::~ActionManager() = default;

void ActionManager::addMapping(const QString& alarmName, Variable* variable, const QVariant& value) {
    if (!variable || alarmName.isEmpty()) return;
    mappings_[alarmName] = {variable, value};
}

void ActionManager::clear() {
    mappings_.clear();
}

// ★ IMPROVE-5.6(2026-08-04):复用阶段 4 WriteScheduler 机制
//   【背景】报警联动动作需要写 PLC,但不能直接调 modbus_
//   【改法】setValue 改本地值 + markDirty 标脏 → WriteScheduler 100ms tick 后写 PLC
//   【取舍】享受 WriteScheduler 的"写失败重试 + 审计日志 + 写失败上限"等机制
//   【参照】WriteScheduler.cpp:onTick 实现;阶段 4.3.0 拍板
void ActionManager::onAlarmTriggered(const Alarm& a) {
    auto it = mappings_.find(a.name);
    if (it == mappings_.end()) return;

    it->variable->setValue(it->value);  // 改本地值
    it->variable->markDirty();          // 标记 dirty → WriteScheduler 写 PLC

    qDebug() << "[Action]" << a.name << "→" << it->variable->name()
             << "=" << it->value.toString();
    emit actionExecuted(a.name, it->variable->name(), it->value.toString());
}