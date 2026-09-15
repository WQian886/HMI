// 阶段 5.6(2026-08-04):ActionManager 实现

#include "ActionManager.h"
#include "Alarm.h"
#include "Variable.h"
#include "VariableManager.h"  // ★ E #29(2026-09-15):按名查 Variable 需要完整定义(findVariable)
#include "logging/Logging.h"  // ★ E #27 #28(2026-09-15):联动失败日志走 hmiAlarm 分类

ActionManager::ActionManager(QObject* parent) : QObject(parent) {}
ActionManager::~ActionManager() = default;

// ★ E #29(2026-09-15):改存变量名,不存裸指针
//   【背景】原代码持裸 Variable*,Variable 被 delete 后变悬空指针 → 阶段 6.3 临时
//          补丁 connect variableReplaced 来手动修指针
//   【改法】addMapping 收 QString 名字,触发时 varMgr_->findVariable(name) 拿当前指针
//   【取舍】多一次 hash 查找,但 mappings 数量 < 10 完全无感
//   【参照】docs/改善清单.md E #29
void ActionManager::addMapping(const QString& alarmName,
                               const QString& variableName, const QVariant& value) {
    if (variableName.isEmpty() || alarmName.isEmpty()) return;
    Mapping m;
    m.alarmName    = alarmName;
    m.variableName = variableName;
    m.value        = value;
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
        m.alarmName    = it.key();
        m.variableName = it.value().variableName;  // ★ E #29
        m.value        = it.value().value;
        list.append(m);
    }
    return list;
}

// ★ E #29(2026-09-15):onVariableReplaced 已删除 — Mapping 改存变量名,不再需要监听 variableReplaced 广播

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

    // ★ E #29(2026-09-15):按变量名查当前 Variable*(替代裸指针持有)
    //   【背景】原 Mapping.variable 是裸 Variable*,Variable 被 delete 后悬空 → 6.3
    //          临时补丁 connect variableReplaced 监听广播,新模块忘 connect 就崩
    //   【改法】存 variableName,触发时按名查。Variable 被删了,findVariable 返回
    //          nullptr → 走"变量不存在"分支,自然安全
    //   【参照】docs/改善清单.md E #29
    Variable* v = varMgr_ ? varMgr_->findVariable(it->variableName) : nullptr;
    if (!v) {
        qCWarning(hmiAlarm) << "[Action] 联动失败:变量" << it->variableName << "已不存在(被删除/未加载),alarm=" << a.name;
        return;
    }

    // ★ E #28 FIX(2026-09-15):联动撒谎上报 → 检查 setValue 返回值
    //   【背景】setValue 可能返 Rejected(类型不匹配 / 超范围)。原代码忽略返回值,
    //          照样 markDirty + emit actionExecuted,审计日志会记"已执行"
    //          但 PLC 实际啥也没收到 → 事故复盘唯一证据是假的
    //   【改法】Rejected 时 qCWarning + return;不 markDirty / 不 emit,审计不撒谎
    //   【参照】docs/改善清单.md E 类 #28;Variable.h SetValueResult 枚举
    const auto result = v->setValue(it->value);
    if (result == Variable::SetValueResult::Rejected) {
        qCWarning(hmiAlarm) << "[Action] 联动失败:变量" << v->name()
                            << "拒绝值" << it->value.toString();
        return;
    }
    // Accepted / NoChange 都走原路径:WriteScheduler 会处理写失败重试
    v->markDirty();  // 标记 dirty → WriteScheduler 写 PLC

    qCInfo(hmiAlarm) << "[Action]" << a.name << "→" << v->name()
                     << "=" << it->value.toString();
    emit actionExecuted(a.name, v->name(), it->value.toString());
}