// ============================================================================
// src/alarm/ActionWiring.cpp —— 见 ActionWiring.h 的文件头
//
// 【为什么这个文件只有一个函数】
//   它承担的是本项目反复吃过的那个坑:
//   "逻辑写在主程序里,测试只能复制一份跑,测的是副本"。
//   抄一份不是问题,【抄完两边各改各的】才是问题 ——
//   而那正是 BUGFIX-C3 的成因(主画面报警表整张废了,68 个测试全绿)。
//   抽出来之后,改一处两边一起变,测试绿了才说明生产也是绿的。
//
// 关联:ActionWiring.h(设计说明)、hmi.cpp(调用点)、ActionManager.cpp(被接的槽)
// ============================================================================

#include "alarm/ActionWiring.h"

#include "../../ActionManager.h"
#include "../../AlarmManager.h"

namespace qwhmi::alarm {

void bindActionToAlarms(AlarmManager* alarmMgr, ActionManager* actionMgr) {
    // ★ 空指针直接返回,不连半截。
    //   少一个 connect 是"联动不执行",连一半是"行为不可预测" ——
    //   后者更难查。前者至少还有本文件里写的"接线被删了"那条断言会红。
    if (!alarmMgr || !actionMgr) return;

    // ★ UniqueConnection:同一个组合连两次,信号就发两遍。
    //   在这个模块上,那意味着"停机动作执行两次"+ 审计日志重复记两行。
    //   看起来像 bug,查起来极费时间 —— 而它的成因只是有人多调了一次本函数。
    //   Qt::UniqueConnection 按 (sender, signal, receiver, method) 去重,
    //   所以"多调一次"是安全的,而不是"多调一次就多执行一次"。
    QObject::connect(alarmMgr, &AlarmManager::alarmTriggered,
                     actionMgr, &ActionManager::onAlarmTriggered,
                     Qt::UniqueConnection);
}

} // namespace qwhmi::alarm
