// ============================================================================
// src/state/StateMachinePolicy.h —— 业务状态机的【判据】搬到这里
//
//   【一句话】"什么条件下机器切到哪个状态"这张表,以前整张写在 hmi.cpp 里,
//   而 hmi.cpp 不在 hmi_core 里 ⇒ 没有任何一条单元测试能看见它。
//
// ============================================================================
// 【为什么现在才抽】
//
//   这张表是 IMPROVE-5.1(2026-08-03 Wqian 拍板)引入的,当时的设计是对的:
//   所有状态变化都必须走 tryTransition(Trigger),查表决定新状态,
//   "非法迁移"这个概念从此存在(以前是 state = State::xxx 随便赋值)。
//
//   ★ 但它一直【只有设计,没有钉子】——
//     G-26(2026-11-02)就是在这张表旁边出的事:
//     onAlarmTriggered 里【无条件】tryTransition(Trigger::AlarmCritical),
//     而判据"这条报警要不要抢停机"写在 hmi.cpp 里 ⇒ 没有测试看得见它
//     ⇒ 一条 Info 报警就把机器打成 Fault。
//
//   ⇒ 本模块跟 AlarmTriggerPolicy 是【同一个形状的两半】:
//     AlarmTriggerPolicy 回答"这条报警要不要触发停机",
//     StateMachinePolicy 回答"触发源来了之后能切到哪、不能切到哪"。
//     两个都进 hmi_core,两个都能被单测直接调。
//
// ============================================================================
// 【本模块【不】管什么】
//
//   · 不管 UI(按钮变灰、标签变红、timer 启停)—— 那是 hmi::updateUi() 的事;
//   · 不管触发源【什么时候】发生(报警来了、按钮点了、3 秒定时器到)
//     —— 那是 hmi.cpp 里各槽函数的事;
//   · 不管状态机"当前是什么" —— 那是 hmi::state 这个成员变量的事。
//
//   本模块只回答一个问题:
//     【给定"现在是什么状态"和"来了什么触发源",能切到哪个状态?】
//     答案是"不知道/不许切"时,状态保持不变。
//
// ============================================================================
// 【行为逐位不变】
//
//   ★ 本次是【搬家 + 加测试】,【不改语义】。
//     迁移表的内容与 hmi.cpp 里原来的那张【逐条相同】(11 条),
//     由 tst_StateMachinePolicy 把 4×6=24 种组合全部钉死。
//
//   ★ 判据是"表里【有没有】这一条",不是"状态变没变"——
//     表里有、但目标状态跟当前状态相同(例如 Stopped --Stop--> Stopped),
//     也算【合法迁移】(返回 true)。这是原实现的行为,照搬。
//
// ============================================================================
// 【表里【故意】缺的东西】
//
//   1) AlarmCleared —— 一个字都没有。
//      抢停机后【不自动恢复】,必须人工按复位。
//      ★ 反过来做(报警一恢复就自动回 Stopped)的话,机器会在条件反复越限的
//        情况下自己爬起来、跑两秒、又自己停 —— 那比停着更危险。
//      ⇒ 这个"缺"是【设计决定】,不是漏写。测试专门钉它。
//
//   2) Running --Start--> Running —— 缺。
//      运行中再按"启动"是【非法迁移】(返回 false,状态不变)。
//      连按两次启动不会把机器开两遍。
//
//   3) Fault --Start--> —— 缺。
//      故障态只能靠"复位"清(而且复位是直接回 Stopped,不等 3 秒定时器)。
//
//   4) Stopped --ResetTimerFinished--> —— 缺。
//      3 秒复位定时器只从 Resetting 态到期;不在那个态时定时器根本没启动。
//
//   5) Resetting --Start/Stop--> —— 缺。
//      复位过程中按启动/停止都是非法迁移(状态不变)。
//      ★ 注意:表里【有】Resetting --Reset--> Resetting(自己→自己),
//        那是"复位过程中再按复位 = 什么都不变",跟 Start/Stop 的区别在于
//        它被显式写进了表里,迁移是【合法】的(会重发一次 updateUi)。
//
// ============================================================================

#ifndef QWHMI_STATE_STATEMACHINEPOLICY_H
#define QWHMI_STATE_STATEMACHINEPOLICY_H

#include <QHash>
#include <QPair>
#include <QString>

namespace qwhmi::state {

// ★ 业务状态机(对应 hmi 里的 hmi::State)。
//   【为什么搬出来】枚举跟着判据一起搬 ——
//   判据的键是 (状态, 触发源),枚举不搬过来就没法在公共层里查表。
enum class MachineState {
    Stopped,     // 已停止(初始态)
    Running,     // 运行中
    Resetting,   // 复位中(3 秒定时器在跑)
    Fault        // 故障(严重报警抢停机进来的)
};

// ★ 状态迁移触发源(对应 hmi 里的 hmi::Trigger)。
enum class MachineTrigger {
    StartButton,        // 点了"启动"
    StopButton,         // 点了"停止"
    ResetButton,        // 点了"复位"
    ResetTimerFinished, // 3 秒复位定时器到点
    AlarmCritical,      // 严重/危急报警(已经按等级过滤过了,见 AlarmTriggerPolicy)
    AlarmCleared        // 报警恢复 —— ★ 表里【故意】没有这一条的迁移
};

/// 迁移表(只读)。
/// ★ 返回的是【引用】而不是拷贝:这份表是常量,每次调用拷一份纯属浪费。
const QHash<QPair<MachineState, MachineTrigger>, MachineState>& transitionTable();

/// 查表:给定"现在是什么"和"来了什么触发源",算出"能切到哪"。
///
/// @param from  当前状态
/// @param t     触发源
/// @param out   【可选】目标状态;只有返回 true 时才会被写。
///              返回 false 时【不写】它 —— 非法迁移必须"什么都不变"。
/// @return true = 合法迁移(已写出目标状态);false = 非法迁移(状态保持不变)
///
/// ★ 为什么 out 是"可选"而不是必填:
///   调用方绝大多数时候只需要知道"能不能切",不需要目标状态本身。
///   但要 out 的场合是有的(比如想知道切过去之后要不要起定时器)。
///   传 nullptr 就不会写 —— 契约是"返回 false 时绝不碰 out"。
bool nextState(MachineState from, MachineTrigger t, MachineState* out);

/// 状态名(给日志和测试报错信息用)。英文标识符,不是界面上的中文。
QString machineStateName(MachineState s);

/// 触发源名(给日志和测试报错信息用)。
QString machineTriggerName(MachineTrigger t);

/// 迁移留痕那一行的【格式】(给日志用)。
///
/// ★ 格式写在公共层而不是散在调用点,是因为进程级判据 J13
///   是【按这个格式数行数】的 ——
///   格式一改,判据就数不到了,
///   而"判据数不到"和"事情没发生"在日志里长得一模一样。
///   ⇒ 格式由公共层出,tst_StateMachinePolicy 把它逐字钉住。
///
/// 格式:  [状态机] From --(Trigger)--> To
///   例:  [状态机] Stopped --(AlarmCritical)--> Fault
QString transitionTrace(MachineState from, MachineTrigger t, MachineState to);

} // namespace qwhmi::state

#endif // QWHMI_STATE_STATEMACHINEPOLICY_H
