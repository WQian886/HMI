// ============================================================================
// 报警行配色(纯函数,无 Q_OBJECT)
//
// ★ 为什么要有这个文件 —— 2026-10-14
//
//   报警列表在这个产品里【有两套 UI】:
//     · 主画面的 ui.alarmTable   (hmi.cpp)
//     · 运行时画布上的 AlarmListControl (src/widgets/)
//   两套各自写了一套配色,到 2026-10-14 已经漂成这样:
//
//     |                主画面 hmi.cpp      | AlarmListControl   |
//     |----------------|--------------------|--------------------|
//     | 静止态四档色  | 【没有】            | 四档各有色         |
//     | 闪烁手法      | 换成固定红/橙       | 本档底色提亮        |
//     | 闪烁相位      | 函数内 static 变量  | 成员变量,停表时复位 |
//     | 等级从哪来    | 解析中文文字         | 解析英文文字         |
//
//   最要命的是第一条:主画面【静止时】非闪烁行是"清背景"(白底默认),
//   于是 Info / Warning 两档跟"这一行没有报警"长得一模一样 ——
//   这恰恰是 AlarmListControl 在 F3 里明确认定要修掉的缺陷
//   (见那边 styleFor 的注释:"报警列表里每一行都是报警,
//   没有'没报警'这个参照物")。也就是说主画面还留着 F3 的老毛病,
//   F3-b 只补了闪烁、没补静止色。
//
//   而"闪烁时换成固定红橙"那一套还有个更要命的后果:
//   危急/严重/警告会共用同一个红 —— 而"哪一档"正是颜色在表达的信息。
//   操作员靠颜色分轻重,闪一下就分不出来了,闪烁反而害事。
//
//   所以本文件把这套决策收到一处,两套 UI 都调它。
//   【判据一句话】配色只能有一个地方说了算,两处各写一套必然漂。
//
// 纯函数、无状态、可直接单测 —— 不依赖任何 QWidget。
// ============================================================================

#pragma once

#include <QColor>
#include <QMetaType>

// ★ Alarm / AlarmLevel / AlarmState 定义在全局命名空间(Alarm.h 没有 namespace 包裹)。
//   这里显式前向声明,而不是 include "Alarm.h" ——
//   因为 hmi.cpp 与 src/widgets/ 都要用本头文件,而 Alarm.h 本身带 Q_OBJECT,
//   让一个纯配色头把 moc 的依赖拖进来没有任何好处。
enum class AlarmLevel;
enum class AlarmState;

namespace qwhmi::alarm {

// ★ 行的【视觉状态】—— 注意它和 AlarmState 不是一回事
//
//   AlarmState 是业务事实(Active / Acknowledged / Cleared),
//   这里说的是"这一行【看起来】该怎么画"。
//   为什么只分两档而不是三档:已确认和已恢复在颜色上【本来就应该一样】
//   —— 两者都表示"这条已经有人管过了",该做的是让注意力转到还没管的行上。
//   它们的【文字】不同("已确认"/"已恢复"),但文字是调用方的事,不是配色的事。
//   分成三档的话,两档还得写出完全相同的配色分支,多一处能漂的地方。
enum class RowVisualState {
    Unacknowledged,   // 还没人确认 —— 唯一允许闪的
    Handled           // 已确认 或 已恢复 —— 一律淡化成灰
};

// ★ 行内携带等级 / 状态的 UserRole
//
//   【为什么必须存,不能靠解析文字反查】
//   两套 UI 原来都是"从等级列的文字反查档位":
//     主画面匹配中文"严重"/"危急",运行时画布匹配英文"Critical"/"Emergency"。
//   这有三个问题:
//     ① 两边语言不同 → 判据天生对不上,而"两套 UI 判据必须一致"是硬要求
//        (F3-b 特意把速度对齐到 500ms,理由就是"同一个产品里同类信号必须同一个节奏")
//     ② 翻译一改(比如把 Emergency 也译成"危急"跟 Critical 撞了),配色立刻错位,
//        而且**不会报错** —— 只是安静地闪错档
//     ③ 本项目已经被"静默失败"咬过 4 次(见 docs/现状.md 第七节)
//   存 enum 之后,文字只是文字,配色只认 enum。
//   ⚠ 约定:【所有】写这两列的代码都必须同时写 UserRole,否则配色读到无效值。
const int kLevelRole = Qt::UserRole + 1;   // 等级列(item(row,2))
const int kStateRole = Qt::UserRole + 2;   // 状态列(item(row,4))

struct RowColors {
    QColor bg;
    QColor fg;
};

// ★ 配色比较。
//   【为什么需要】"两相颜色必须一样/不一样"是这个模块最核心的几条断言
//   (不闪的档位两相必须相同、已处理的行两相必须相同)——
//   没有 == 就只能逐个字段比,测试里会散落一地 a.bg == b.bg && a.fg == b.fg,
//   漏比一个字段就变成"测了背景没测字色",而对比度恰恰是 bg+fg 两个数算出来的。
inline bool operator==(const RowColors& a, const RowColors& b) {
    return a.bg == b.bg && a.fg == b.fg;
}
inline bool operator!=(const RowColors& a, const RowColors& b) { return !(a == b); }

// ★ 配色主函数 —— 两套 UI 的配色【唯一入口】
//
//   @param level    报警等级(4 档)
//   @param state    行的视觉状态
//   @param blinkOn  当前处在闪烁的亮相还是暗相。
//                  ★ 只有 (未确认 && 严重/危急) 才会真的用它;
//                    其他档位传 true 与 false 结果相同 ——
//                    这是刻意的:不该闪的档位,相位对它没有意义。
RowColors colorsFor(AlarmLevel level, RowVisualState state, bool blinkOn);

// ★ 这一行此刻该不该闪 —— 未确认 + 严重/危急
//
//   【为什么"只闪这两档"是产品决定,不是省事】
//     · 已确认的还闪 = 骗操作员去处理一条已经处理过的
//     · 信息/警告也闪 = 现场最吵的永远在闪,真出事的那条被淹了
//   两套 UI 原来各写一遍这个 if(hmi.cpp 匹配中文、AlarmListControl 匹配英文),
//   判据天生可能对不上 —— 现在只有这一处。
bool shouldBlink(AlarmLevel level, RowVisualState state);

// ★ 业务状态 → 视觉状态
//
//   Cleared 也算 Handled:它表示条件已经消失,同样不需要再吸引注意力。
RowVisualState visualStateFor(AlarmState st);

} // namespace qwhmi::alarm
