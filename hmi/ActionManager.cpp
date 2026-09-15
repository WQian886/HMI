// 阶段 5.6(2026-08-04):ActionManager 实现

#include "ActionManager.h"
#include "Alarm.h"
#include "Variable.h"
#include "logging/Logging.h"  // ★ E #27 #28(2026-09-15):联动失败日志走 hmiAlarm 分类

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

    // ★ E #27 FIX(2026-09-15):联动静默失效 → 改为日志告警 + return
    //   【背景】原 `if (!it->variable) return;` 默默离开:报警响了安全动作没执行
    //          却无任何痕迹,事故复盘看不到失败原因 → 现场事故级风险
    //   【改法】走 qCWarning(hmiAlarm),联动失效在日志里留下证据
    //   【参照】docs/改善清单.md E 类 #27
    if (!it->variable) {
        qCWarning(hmiAlarm) << "[Action] 联动失败:变量已不存在(被删除/未加载),alarm=" << a.name;
        return;
    }

    // ★ E #28 FIX(2026-09-15):联动撒谎上报 → 检查 setValue 返回值
    //   【背景】setValue 可能返 Rejected(类型不匹配 / 超范围)。原代码忽略返回值,
    //          照样 markDirty + emit actionExecuted,审计日志会记"已执行"
    //          但 PLC 实际啥也没收到 → 事故复盘唯一证据是假的
    //   【改法】Rejected 时 qCWarning + return;不 markDirty / 不 emit,审计不撒谎
    //   【参照】docs/改善清单.md E 类 #28;Variable.h SetValueResult 枚举
    const auto result = it->variable->setValue(it->value);
    if (result == Variable::SetValueResult::Rejected) {
        qCWarning(hmiAlarm) << "[Action] 联动失败:变量" << it->variable->name()
                            << "拒绝值" << it->value.toString();
        return;
    }
    // Accepted / NoChange 都走原路径:WriteScheduler 会处理写失败重试
    it->variable->markDirty();  // 标记 dirty → WriteScheduler 写 PLC

    qCInfo(hmiAlarm) << "[Action]" << a.name << "→" << it->variable->name()
                     << "=" << it->value.toString();
    emit actionExecuted(a.name, it->variable->name(), it->value.toString());
}