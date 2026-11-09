// 阶段 5.6(2026-08-04):ActionManager 实现
//
// ★ F 阶段(2026-11-09):执行部分改成"按模板逐步执行"
//   判据(哪一步合法、结果是什么)在公共层 src/action/ActionTemplate.{h,cpp};
//   本文件只留接线:怎么真的把变量写出去、怎么如实报结果。

#include "ActionManager.h"
#include "Alarm.h"
#include "Variable.h"
#include <algorithm>  // ★ std::sort(templates() 里按报警名排序)
#include "VariableManager.h"  // ★ E #29(2026-09-15):按名查 Variable 需要完整定义(findVariable)
#include "logging/Logging.h"  // ★ E #27 #28(2026-09-15):联动失败日志走 hmiAlarm 分类
// ★ 2026-10-25:类型适配。Variable::setValue 严格按 typeId 匹配,
//   而联动值来自 JSON(写 "write_value": 0 → QVariant(LongLong)【实测】)
//   或 C++ 字面量(addMapping(..., 0) → QVariant(Int)),
//   两者跟变量的声明类型(UInt16)对不上 → setValue 返 Rejected →
//   ★ 联动一次都没真正写出去过 ★(见下面 doWriteVariable 的长注释)
#include "src/util/ValueCoercion.h"
// 注:B3 决定联动不加 checkPermission(系统级行为,不是用户操作)
//   ActionManager.h 顶声明"便利性功能,不是安全功能"

ActionManager::ActionManager(QObject* parent) : QObject(parent) {}
ActionManager::~ActionManager() = default;

// ★ E #29(2026-09-15):改存变量名,不存裸指针
//   【背景】原代码持裸 Variable*,Variable 被 delete 后变悬空指针 → 阶段 6.3 临时
//          补丁 connect variableReplaced 来手动修指针
//   【改法】addMapping 收 QString 名字,触发时 varMgr_->findVariable(name) 拿当前指针
//   【取舍】多一次 hash 查找,但 mappings 数量 < 10 完全无感
//   【参照】docs/改善清单.md E #29
//
// ★★ F 阶段(2026-11-09):【追加】,不再是【覆盖】★★
//   原来是 mappings_[alarmName] = m; ⇒ 给同一条报警配第二个动作时,
//   第一个被【悄悄顶掉】 —— 界面上什么提示都没有,现场只会以为
//   "配了没生效"。现在同一个报警名可以配任意多个动作。
void ActionManager::addMapping(const QString& alarmName,
                               const QString& variableName, const QVariant& value) {
    if (variableName.isEmpty() || alarmName.isEmpty()) return;

    qwhmi::action::ActionStep step;
    step.type         = qwhmi::action::ActionType::WriteVariable;
    step.variableName = variableName;
    step.value        = value;

    auto it = templates_.find(alarmName);
    if (it == templates_.end()) {
        qwhmi::action::ActionTemplate tpl;
        tpl.alarmName = alarmName;
        tpl.steps.append(step);
        templates_.insert(alarmName, tpl);
    } else {
        // ★ 追加 —— 不覆盖已有的那几步。
        it->steps.append(step);
    }
}

// ★ F 阶段(2026-11-09):一条报警配一整组动作。
//   【语义】整组替换 —— 模板是"完整配置"。
//   【为什么空模板也存】
//     工程文件读入时,action_templates 里那条(哪怕 steps 是空的)
//     要把老格式 actions 数组里同名的残留【清掉】——
//     "这条报警没有动作"也是一种明确的意思,不能被老残留顶回来。
void ActionManager::addTemplate(const qwhmi::action::ActionTemplate& tpl) {
    if (tpl.alarmName.isEmpty()) return;
    templates_.insert(tpl.alarmName, tpl);
}

// ★ 返回所有模板,按报警名排序 —— 存盘时输出稳定(不然每次存盘的
//   字节序都可能不一样,git diff 全是噪声)。
QList<qwhmi::action::ActionTemplate> ActionManager::templates() const {
    QList<qwhmi::action::ActionTemplate> list;
    list.reserve(templates_.size());
    for (auto it = templates_.constBegin(); it != templates_.constEnd(); ++it) {
        list.append(it.value());
    }
    std::sort(list.begin(), list.end(),
              [](const qwhmi::action::ActionTemplate& a,
                 const qwhmi::action::ActionTemplate& b) {
                  return a.alarmName < b.alarmName;
              });
    return list;
}

// ★ IMPROVE-6.2(2026-08-07):返回所有 mappings(给 saveToFile 用)
//   【背景】JsonSerializer 需要 alarmName + variable + value 三个字段
//   【★ F 阶段 2026-11-09:改成【展平】】
//     老工程文件格式(actions 数组)只有"写变量"一种动作,
//     所以这个展平视图正好就是老格式该有的样子 ——
//     每个"写变量"步骤摊成一条 Mapping。
//     ⚠️ "只记日志"那类步骤在这个视图里【看不见】——
//       完整信息在 templates() 里;存盘时两个数组都写
//       (见 Project.cpp:actions + action_templates)。
QList<ActionManager::Mapping> ActionManager::mappings() const {
    QList<Mapping> list;
    for (const qwhmi::action::ActionTemplate& tpl : templates()) {
        for (const qwhmi::action::ActionStep& step : tpl.steps) {
            if (step.type != qwhmi::action::ActionType::WriteVariable) continue;
            Mapping m;
            m.alarmName    = tpl.alarmName;
            m.variableName = step.variableName;  // ★ E #29
            m.value        = step.value;
            list.append(m);
        }
    }
    return list;
}

// ★ E #29(2026-09-15):onVariableReplaced 已删除 — Mapping 改存变量名,不再需要监听 variableReplaced 广播

void ActionManager::clear() {
    templates_.clear();
}

// ★ IMPROVE-5.6(2026-08-04):复用阶段 4 WriteScheduler 机制
//   【背景】报警联动动作需要写 PLC,但不能直接调 modbus_
//   【改法】setValue 改本地值 + markDirty 标脏 → WriteScheduler 100ms tick 后写 PLC
//   【取舍】享受 WriteScheduler 的"写失败重试 + 审计日志 + 写失败上限"等机制
//   【参照】WriteScheduler.cpp:onTick 实现;阶段 4.3.0 拍板
//
// ★★ F 阶段(2026-11-09):一条报警可以配【多步】,按顺序逐步执行 ★★
//   原来这里只处理【一条】"写变量"动作(而且一条报警只能配一条)。
//   现在:
//     · 找不到这条报警的模板 → 同原来,DEBUG 一行,返回;
//     · 找到 → 把模板里的步骤【按顺序】逐步执行,
//       每一步的结果都如实分类(成功 / 变量没了 / 值不合法 / 被拒收);
//     · ★ 一步失败【不】中断后面的步骤 ——
//       "停机写失败"不该导致"留一条痕"也不做。
//       (但失败的那一步【必须】留日志,不能装作没发生。)
void ActionManager::onAlarmTriggered(const Alarm& a) {
    auto it = templates_.find(a.name);
    if (it == templates_.end() || it->steps.isEmpty()) {
        // ★ 2026-10-25:这条路径原来是【一声不吭】地 return。
        //   联动配错名字(工程里改了报警名、联动里还写着老名字)的后果是
        //   "动作再也不执行",而日志里一个字都没有 —— 现场只能靠猜。
        //   为什么放 DEBUG 而不是 INFO/WARN:
        //     大多数报警【本来就没有】联动(比如写失败报警),
        //     这条会对每一条没配联动的报警都打一行 —— 那会把日志刷爆。
        //   放 DEBUG 的取舍:默认 INFO 级别下看不见,但这是"配错了"的信号,
        //     不是"正常运行"的噪声 —— 排查联动失效时把 hmi.alarm 调到 DEBUG 就有了。
        qCDebug(hmiAlarm) << "[Action] 报警" << a.name << "没有配联动(忽略)";
        return;
    }

    const qwhmi::action::ActionTemplate& tpl = it.value();

    // ★ 逐步执行。sink 带着报警名 —— 失败日志和审计都要它,
    //   少了它,现场看到"联动失败:变量 X 不存在"根本不知道是哪条报警。
    Sink sink(this, a.name);

    int done = 0;
    int failed = 0;
    for (const qwhmi::action::ActionStep& step : tpl.steps) {
        const qwhmi::action::StepResult r = qwhmi::action::executeStep(step, sink);
        if (r.ok()) {
            ++done;
        } else {
            ++failed;
            // ★ 失败必须留痕 —— 见上面 doWriteVariable 里的分档日志。
            //   这里再补一行汇总:一组动作里失败了几步。
            qCWarning(hmiAlarm) << "[Action] 联动步骤失败:" << a.name
                                << "结果=" << qwhmi::action::stepOutcomeToString(r.outcome)
                                << "(本组共 " << tpl.steps.size() << " 步)";
        }
    }

    // ★ 一条都没做成 ⇒ 说明这次报警【什么也没做】——
    //   要说清楚(否则现场看到"报了警但什么反应都没有",只能靠猜)。
    if (done == 0 && failed > 0) {
        qCWarning(hmiAlarm) << "[Action] 报警" << a.name
                            << "的联动【全部失败】(" << failed << " 步)—— 现场什么也没执行";
    }
}

// ★ 公共层的 sink 回调:真的把变量写出去。
//   【★ E #29(2026-09-15):按变量名查当前 Variable*(替代裸指针持有)】
//     【背景】原 Mapping.variable 是裸 Variable*,Variable 被 delete 后悬空 → 6.3
//            临时补丁 connect variableReplaced 监听广播,新模块忘 connect 就崩
//     【改法】存 variableName,触发时按名查。Variable 被删了,findVariable 返回
//            nullptr → 走"变量不存在"分支,自然安全
//     【参照】docs/改善清单.md E #29
qwhmi::action::StepResult ActionManager::doWriteVariable(const QString& alarmName,
                                                          const QString& variableName,
                                                          const QVariant& value) {
    qwhmi::action::StepResult r;

    Variable* v = varMgr_ ? varMgr_->findVariable(variableName) : nullptr;
    if (!v) {
        qCWarning(hmiAlarm) << "[Action] 联动失败:变量" << variableName
                            << "已不存在(被删除/未加载),alarm=" << alarmName;
        r.outcome = qwhmi::action::StepOutcome::NoVariable;
        return r;
    }

    // ★★ 2026-10-25(BUGFIX-C5):写入前按变量的声明类型适配值 ★★
    //
    //   【这是本文件修过的一个真 bug,24 小时长跑实测出来的】
    //     长跑工程(soaks/20261021_164804)里配了两条联动:
    //       "温度高" → 把 Speed 写 0,"温度低" → 把 Speed 写 0
    //     Speed 的声明类型是 UInt16,而 write_value 是 JSON 的 0
    //     → QJsonValue::toVariant() 给的是 QVariant(LongLong)【实测,typeId=4】
    //     而 Variable::setValue 的 matchesDataType 是【严格 typeId 相等】:
    //       case Var::UInt16: return v.typeId() == QMetaType::UShort;
    //     LongLong(4) ≠ UShort(36) → Rejected → 【联动一次都没写出去过】。
    //     现场证据:从站日志里 FC6/FC16 计数 = 0(整条写通路根本没被用过),
    //     而"温度高就停机"这条联动在这 24 小时里【从没生效过】。
    //
    //   【为什么不改 Variable::setValue 去接受任意数值】
    //     Variable 的严格性有测试在守(tst_Variable::setValue_typeMismatch),
    //     而且"严格"是对的:数据层就该知道自己值的类型。
    //     本项目的既定做法是【在调用方适配】:
    //       · tst_ControlBaseWrite.cpp:188 修过一次一模一样的 bug
    //       · Binding.cpp:130-138 的 inline edit 也是这么做的
    //     现在共用 util/ValueCoercion 一份,不再各写各的。
    //
    //   【适配转不了怎么办】
    //     负数写进 UInt16、字符串写不进数值型 —— 这些一律当【非法值】拒掉,
    //     而且必须 log + return(不能"当作 0"继续写)。
    const QVariant typed = qwhmi::util::coerceToDataType(value, v->dataType());
    if (!typed.isValid()) {
        qCWarning(hmiAlarm) << "[Action] 联动失败:值" << value.toString()
                            << "无法适配成变量" << v->name() << "的类型"
                            << Variable::dataTypeName(v->dataType())
                            << "(alarm=" << alarmName << ")";
        r.outcome = qwhmi::action::StepOutcome::InvalidValue;
        return r;
    }

    // ★ E #28 FIX(2026-09-15):联动撒谎上报 → 检查 setValue 返回值
    //   【背景】setValue 可能返 Rejected(类型不匹配 / 超范围)。原代码忽略返回值,
    //          照样 markDirty + emit actionExecuted,审计日志会记"已执行"
    //          但 PLC 实际啥也没收到 → 事故复盘唯一证据是假的
    //   【改法】Rejected 时 qCWarning + return;不 markDirty / 不 emit,审计不撒谎
    //   【参照】docs/改善清单.md E 类 #28;Variable.h SetValueResult 枚举
    const auto result = v->setValue(typed);
    if (result == Variable::SetValueResult::Rejected) {
        qCWarning(hmiAlarm) << "[Action] 联动失败:变量" << v->name()
                            << "拒绝值" << typed.toString()
                            << "(超范围? alarm=" << alarmName << ")";
        r.outcome = qwhmi::action::StepOutcome::Rejected;
        return r;
    }
    // Accepted / NoChange 都走原路径:WriteScheduler 会处理写失败重试
    v->markDirty();  // 标记 dirty → WriteScheduler 写 PLC

    qCInfo(hmiAlarm) << "[Action]" << alarmName << "→" << v->name()
                     << "=" << typed.toString();
    emit actionExecuted(alarmName, v->name(), typed.toString());

    r.outcome      = qwhmi::action::StepOutcome::Executed;
    r.appliedValue = typed.toString();
    return r;
}

// ★ 公共层的 sink 回调:只留一条痕,不动 PLC。
//   【级别】INFO —— 这是【有人配了、也确实发生了】的事,
//     现场查"这条报警到底做了什么"时,这一行就是证据。
void ActionManager::doLogOnly(const QString& alarmName, const QString& text) {
    qCInfo(hmiAlarm) << "[Action]" << alarmName << "→" << text;
}
