// ============================================================================
// src/alarm/AlarmTriggerPolicy.h —— "这条报警要不要把机器打成 Fault"的唯一出处
//
// ============================================================================
// 【背景】
//   2026-11-02 发现 G-26:【任何等级的报警都会把整台机器推进 Fault】。
//   一条"信息"级报警,能让 counter 定时器停下、开始/停止按钮全灰、
//   状态标签变成"状态:故障"。
//
// 【Bug 现象】
//   操作员配了一条 Warning 级的"温度偏高 3 度",机器就停了。
//   而界面上的报警表、声音、审计日志一切正常 —— 没有任何地方说
//   "是因为这条报警把机器停了",现场只能靠猜。
//
// 【根因】(两处,叠在一起)
//   ① hmi::onAlarmTriggered 里【无条件】tryTransition(Trigger::AlarmCritical)
//   ② AlarmManager 广播 alarmTriggered 时【不分等级】
//   —— 也就是说 hmi.cpp 把"任意等级"都当成了"AlarmCritical"。
//
// 【为什么它能活这么久】—— 这是本文件存在的根本理由
//   hmi.cpp 【不在 hmi_core 里】⇒ 106 个单测没有一个能链接到主窗口
//   ⇒ "任意报警都停机"这件事,没有任何一条测试能看见。
//   跟 G-14 / BUGFIX-C3(主画面报警表整张废了,68 个测试全绿)是同一个形状:
//   【主画面逻辑零覆盖】。
//
// 【参照】(设计稿白纸黑字,现在的实现跟它反着来)
//   docs/归档/2026-08-03_LEARNING_NOTES.md:219
//     | **任何** | AlarmCritical | **Fault** | 严重报警抢占 |
//   —— "AlarmCritical" 这个 trigger 名字本身,意思就是"【严重等级】的报警"。
//   现在的实现把"任何等级"都当成 AlarmCritical,是实现跑偏,不是设计如此。
//
// 【取舍】哪些等级抢停机
//   Critical(严重) / Emergency(危急)  → 抢停机(状态机进 Fault)
//   Info(信息)   / Warning(警告)       → 只记录、只显示、照常执行联动,但【不停机】
//
//   【为什么 Warning 不停机】
//     工业惯例:警告是"请注意",停不停机由工艺决定;抢停机是"严重/危急"的事。
//     而且停机有代价(在产的料全废)。把"温度偏高 3 度"直接变成停机,
//     现场很快会学会【忽略报警】—— 那比不停机更危险。
//
//   【为什么 Emergency 也抢停机】
//     它比 Critical 更严重,不可能不抢。
//     ★ 判据写成"比 Critical 重"而不是"等于 Critical",
//       将来加第 5 档时自动纳入,不会漏掉。
//
//   【不动的地方】联动(报警→写 PLC)【不】按等级过滤。
//     联动是用户在报警配置里【逐条配好】的:配了"温度高→速度写 0"就是配了,
//     跟机器停不停是两件事。动它会改掉用户配好的工艺,
//     而且会让 24 小时长跑里验证过的写通路判据(J1~J11)全部失效。
//
// 【为什么单独一个文件】
//   同 ActionWiring.h / AlarmTablePresenter.h / AlarmRowPalette.h 一个道理:
//   写在 hmi.cpp 里的判定,测试只能【复制一份】跑 —— 测的是副本,
//   而 hmi.cpp 那份被改错了,测试照样全绿(G-26 就是这么活下来的)。
//   抽出来之后,改一处两边一起变,测试绿了才说明生产也是绿的。
//
// 关联:hmi.cpp(onAlarmTriggered)、Alarm.h(AlarmLevel)、
//      tst_AlarmTriggerPolicy(单测)、scripts/negctl_alarm_trip.py(负向对照)
// ============================================================================

#pragma once

// ★ AlarmLevel 定义在全局命名空间(Alarm.h 没有 namespace 包裹)。
//   这里显式前向声明,而不是 include "Alarm.h" ——
//   本模块【不】需要 moc / QObject,把它拖进来没有任何好处。
//   (手法同 AlarmRowPalette.h / AlarmTablePresenter.h。)
enum class AlarmLevel;

namespace qwhmi::alarm {

// ★ 这条报警要不要把机器推进 Fault(抢停机)。
//
//   true  = Critical / Emergency → 状态机进 Fault,机器停
//   false = Info / Warning      → 只记录、只显示、照常执行联动,不停机
//
//   【纯函数】无状态、不依赖 QWidget / 不依赖主窗口 —— 可直接单测。
bool shouldTripMachine(AlarmLevel level);

} // namespace qwhmi::alarm
