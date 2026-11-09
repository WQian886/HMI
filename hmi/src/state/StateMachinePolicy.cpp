// ============================================================================
// src/state/StateMachinePolicy.cpp —— 见头文件"为什么抽出来"
//
//   关联:StateMachinePolicy.h(设计说明)、hmi/hmi.h(hmi::tryTransition)
// ============================================================================

#include "state/StateMachinePolicy.h"

namespace qwhmi::state {

// ★★★ 这张表就是本模块存在的全部理由 ★★★
//
//   11 条,与搬家之前 hmi.cpp 里那张【逐条相同】。
//   由 tst_StateMachinePolicy 把 4 状态 × 6 触发源 = 24 种组合全部钉死 ——
//   表里【多一条】或【少一条】,测试都会红。
//
//   ⚠ 改动这张表之前先想清楚:
//     · 少一条 = 那个迁移【变成非法】(调用方拿到 false,状态不变);
//     · 多一条 = 那个迁移【变成合法】(以前被挡住的现在放行了)。
//     两种改动都会改变现场行为,而这张表以前【从来没有被任何测试看过】。
const QHash<QPair<MachineState, MachineTrigger>, MachineState>& transitionTable() {
    static const QHash<QPair<MachineState, MachineTrigger>, MachineState> table = {
        // ---- Stopped:初始 / 主动停止 -------------------------------------
        {{MachineState::Stopped,   MachineTrigger::StartButton},        MachineState::Running},
        // ★ 重复按 stop:合法但无变化(表里显式写着,不是缺一条)
        {{MachineState::Stopped,   MachineTrigger::StopButton},         MachineState::Stopped},
        // ★ 启动 3s 复位定时器(到点由 ResetTimerFinished 转 Stopped)
        {{MachineState::Stopped,   MachineTrigger::ResetButton},        MachineState::Resetting},
        {{MachineState::Stopped,   MachineTrigger::AlarmCritical},      MachineState::Fault},

        // ---- Running ----------------------------------------------------
        {{MachineState::Running,   MachineTrigger::StopButton},         MachineState::Stopped},
        {{MachineState::Running,   MachineTrigger::ResetButton},        MachineState::Resetting},
        {{MachineState::Running,   MachineTrigger::AlarmCritical},      MachineState::Fault},
        // ★ Running + StartButton 【故意没有】:运行中再按启动是非法迁移。
        //   连按两次启动不会把机器开两遍。

        // ---- Resetting:等 3s 定时器,只能被报警打断 ----------------------
        {{MachineState::Resetting, MachineTrigger::ResetButton},        MachineState::Resetting},
        {{MachineState::Resetting, MachineTrigger::ResetTimerFinished}, MachineState::Stopped},
        {{MachineState::Resetting, MachineTrigger::AlarmCritical},      MachineState::Fault},
        // ★ Resetting + Start/Stop 【故意没有】:复位过程中按启动/停止都不动。

        // ---- Fault:只能复位清,且【直接】回 Stopped(不等定时器)-----------
        {{MachineState::Fault,     MachineTrigger::ResetButton},        MachineState::Stopped},
        // ★ Fault + AlarmCritical 【故意没有】:已经在故障态了,再报一次严重
        //   报警不应该"再迁移一次"(那会让 updateUi 多跑一趟)。
        //   ★ AlarmCleared 全表没有 —— 见头文件"表里故意缺的东西"。
    };
    return table;
}

bool nextState(MachineState from, MachineTrigger t, MachineState* out) {
    const auto it = transitionTable().find(qMakePair(from, t));
    if (it == transitionTable().end()) {
        // 非法迁移:不写 out(契约:返回 false 时绝不碰 out)——
        //   写了的话,调用方"忘了检查返回值"就会拿着一个陈旧状态继续跑。
        return false;
    }
    if (out) {
        *out = it.value();
    }
    return true;
}

QString machineStateName(MachineState s) {
    switch (s) {
    case MachineState::Stopped:   return QStringLiteral("Stopped");
    case MachineState::Running:   return QStringLiteral("Running");
    case MachineState::Resetting: return QStringLiteral("Resetting");
    case MachineState::Fault:     return QStringLiteral("Fault");
    }
    return QStringLiteral("?");
}

QString machineTriggerName(MachineTrigger t) {
    switch (t) {
    case MachineTrigger::StartButton:        return QStringLiteral("StartButton");
    case MachineTrigger::StopButton:         return QStringLiteral("StopButton");
    case MachineTrigger::ResetButton:        return QStringLiteral("ResetButton");
    case MachineTrigger::ResetTimerFinished: return QStringLiteral("ResetTimerFinished");
    case MachineTrigger::AlarmCritical:      return QStringLiteral("AlarmCritical");
    case MachineTrigger::AlarmCleared:       return QStringLiteral("AlarmCleared");
    }
    return QStringLiteral("?");
}

// ★ 迁移留痕那行的格式 —— 见头文件里的说明。
//   格式由这里【一处】出,tst_StateMachinePolicy 逐字钉住。
//   ★ 别把它挪回 hmi.cpp:判据 J13 是按这个格式数行数的,
//     格式散到调用点就迟早有一处对不上,而"数不到"会被误读成"没发生"。
QString transitionTrace(MachineState from, MachineTrigger t, MachineState to) {
    return QStringLiteral("[状态机] %1 --(%2)--> %3")
        .arg(machineStateName(from), machineTriggerName(t), machineStateName(to));
}

} // namespace qwhmi::state
