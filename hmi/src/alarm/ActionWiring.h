// ============================================================================
// src/alarm/ActionWiring.h —— "报警触发了要执行联动"这根线的【唯一出处】
//
// 【为什么单独一个文件,而不是留在 hmi.cpp 里】
//   同一个理由,跟 src/ui/MenuBuilder.h / src/alarm/AlarmTablePresenter.h 一模一样:
//   写在 hmi.cpp 里的逻辑,测试只能【复制一份】到测试文件里跑 ——
//   测的是副本。而 hmi.cpp 那份被改错了(接线被删、接错槽),测试照样全绿。
//
// ★ 而"接线断了"的后果,在这个模块上是最严重的:
//   BUGFIX-C3(2026-10-14)的教训 —— 主画面的报警表【整张是废的】
//   (状态列从来没被创建过 → 状态列空白 / 确认无效 / 无配色 / 不闪烁),
//   而当时 68 个测试【全绿】。原因就一句话:
//     主画面那套【一条测试都没有】。
//   同样的形状放到联动上:
//     "报警触发了但联动不执行" —— 界面上一片正常、报警正常响、
//     审计日志正常记"已执行",而【PLC 一个字节都没收到】。
//     从站那边看:FC6/FC16 计数 = 0。现场只能靠猜。
//   而 24 小时长跑实测(soaks/20261021_164804)里,这条联动
//   ("温度高 → 速度写 0",也就是"停机")【真的 24 小时一次都没执行过】。
//   (那个洞本身是 BUGFIX-C5:值没按变量声明类型适配 → setValue 拒收。)
//
// 【本文件解决什么】
//   把 connect 从 hmi.cpp 里抽出来,hmi.cpp 和测试调的是【同一个函数】。
//   抽完之后:
//     · 行为部分(报警→联动→变量→写)走的是生产类,不是替身
//     · 接线部分(alarmTriggered → onAlarmTriggered)走的是同一个函数,不是抄的
//   剩下唯一的"读源码文本"那条断言,守的是【hmi.cpp 有没有真的调这个函数】——
//   接线被删了它会变红。(同 tst_MenuBar / tst_Theme 的做法。)
//
// 【★ 为什么不是把整个 ActionManager 塞进这里】
//   本文件只管"接哪根线",不管"联动做什么"。
//   联动的值怎么适配类型、变量不存在怎么办、写失败怎么办 ——
//   那些在 ActionManager 自己身上(见 ActionManager.cpp:onAlarmTriggered),
//   那里有完整的失败路径和日志。
//   这里多管一件事就会变成"两个地方都能改同一件事",迟早走偏。
//
// 关联:hmi.cpp(建好 actionMgr_ 之后调本函数)、
//      ActionManager::onAlarmTriggered(被接上的那个槽)、
//      AlarmManager::alarmTriggered(信号源)
// ============================================================================

#pragma once

class AlarmManager;
class ActionManager;

namespace qwhmi::alarm {

// 把 AlarmManager::alarmTriggered 接到 ActionManager::onAlarmTriggered。
//
// ★ 幂等性:重复调用【不会】导致一次报警触发多次联动。
//   Qt 的 connect 默认是"允许重复连接"的 —— 同一个 (sender, signal,
//   receiver, method) 组合连两次,信号就发两遍。
//   而"连了两次"在这个模块上的表现是【停机动作执行两次】,
//   以及审计日志里同一个动作记两行 —— 看起来像 bug,查起来极费时间。
//   所以这里用 Qt::UniqueConnection 兜住(它按
//   (sender, signal, receiver, method) 这个组合去重)。
//
// ★ 参数为 nullptr 时什么都不做(不崩、不连半截)。
//   生产里两个指针都不会是空的(构造顺序保证),
//   但这个函数是公开的,调用方不只有 hmi.cpp。
void bindActionToAlarms(AlarmManager* alarmMgr, ActionManager* actionMgr);

} // namespace qwhmi::alarm
