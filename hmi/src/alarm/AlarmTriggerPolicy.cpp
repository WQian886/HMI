// ============================================================================
// src/alarm/AlarmTriggerPolicy.cpp —— 见头文件"为什么要有这个文件"
//
//   【为什么这个文件只有一个函数】
//     它承担的是本项目反复吃过的那个坑:
//     "逻辑写在主程序里,测试只能复制一份跑,测的是副本"。
//     G-26 就是这么活下来的 —— 一条 Info 报警把机器打成 Fault,
//     而 106 个测试全绿,因为主窗口那套一条测试都没有。
//
// 关联:AlarmTriggerPolicy.h(设计说明)、hmi.cpp(调用点)、Alarm.h(AlarmLevel)
// ============================================================================

#include "alarm/AlarmTriggerPolicy.h"

#include "../../Alarm.h"

namespace qwhmi::alarm {

bool shouldTripMachine(AlarmLevel level) {
    // ★ 判据是"【比 Critical 重】",不是"【等于 Critical】"。
    //   AlarmLevel 的枚举顺序就是严重程度顺序(Alarm.h:Info < Warning < Critical < Emergency),
    //   所以 Emergency 自动被包含进来 —— 将来加第 5 档同样自动包含。
    //
    //   ★ 用 ">=" 而不是查表:
    //     查表(4 个分支)每加一档就得多写一行,漏一行 = 那一档悄悄不抢停机。
    //     枚举顺序是 Alarm.h 里定死的,单测里有一条断言盯着它不许乱序。
    return level >= AlarmLevel::Critical;
}

} // namespace qwhmi::alarm
