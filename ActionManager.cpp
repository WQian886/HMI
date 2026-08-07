// 阶段 5.6(2026-08-04):ActionManager 实现

#include "ActionManager.h"
#include "Alarm.h"
#include "Variable.h"

ActionManager::ActionManager(QObject* parent) : QObject(parent) {}
ActionManager::~ActionManager() = default;

void ActionManager::addMapping(const QString& alarmName, Variable* variable, const QVariant& value) {
    if (!variable || alarmName.isEmpty()) return;
    // ★ IMPROVE-6.2(2026-08-07):Mapping 加 alarmName 字段,改用 explicit 赋值
    Mapping m;
    m.alarmName = alarmName;
    m.variable  = variable;
    m.value     = value;
    mappings_[alarmName] = m;
}

// ★ IMPROVE-6.2(2026-08-07):返回所有 mappings(给 saveToFile 用)
//   【背景】JsonSerializer 需要 alarmName + variable + value 三个字段
//   【改法】遍历 QHash,拷贝到 QList(避免直接暴露内部容器 + 给调用方独立副本)
//   【取舍】拷贝有 O(n) 开销,但 mappings 数量通常 < 10,可忽略
//   【参照】Wqian 2026-08-07 阶段 6.2 开干
QList<ActionManager::Mapping> ActionManager::mappings() const {
    QList<Mapping> list;
    list.reserve(mappings_.size());
    for (auto it = mappings_.constBegin(); it != mappings_.constEnd(); ++it) {
        Mapping m;
        m.alarmName = it.key();
        m.variable  = it.value().variable;
        m.value     = it.value().value;
        list.append(m);
    }
    return list;
}

// ★ 阶段 6.3(2026-08-07):loadFromFile 重建 Variable 时,自动更新 mappings_ 里的 Variable* 指针
//   旧 Variable 被 delete,新 Variable 同名替换;由 hmi.cpp connect varMgr_->variableReplaced 触发
void ActionManager::onVariableReplaced(Variable* oldVar, Variable* newVar) {
    for (auto& m : mappings_) {
        if (m.variable == oldVar) {
            m.variable = newVar;  // 可能是 nullptr(被 clear)
        }
    }
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

    // ★ 6.3 FIX:旧 Variable 已死,清空指针(联动不写)避免 dangling
    if (!it->variable) return;
    it->variable->setValue(it->value);  // 改本地值
    it->variable->markDirty();          // 标记 dirty → WriteScheduler 写 PLC

    qDebug() << "[Action]" << a.name << "→" << it->variable->name()
             << "=" << it->value.toString();
    emit actionExecuted(a.name, it->variable->name(), it->value.toString());
}