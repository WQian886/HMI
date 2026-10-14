// ============================================================================
// AlarmRowPalette 实现 —— 见头文件"为什么要有这个文件"
// ============================================================================

#include "alarm/AlarmRowPalette.h"

#include "../../Alarm.h"

#include <QDebug>   // qWarning(未知档位兜底处)

namespace qwhmi::alarm {

namespace {

// ★ 四档静止态配色。数值与 AlarmListControl 原 styleFor 逐位相同 ——
//   这一段是【搬运】不是【重新设计】,F3 当时逐档对比过两两差异。
//
//   【为什么用低饱和底色而不是纯饱和色】
//   工业屏常年亮着,大面积高饱和红/黄看久了刺眼,
//   还容易和"这一行被选中"混淆。底色压到浅色调,靠字色定对比,
//   四档之间的色相差异仍能一眼分出。
//
//   | 档位     | 底色              | 字色              | 静止态对比度 |
//   |----------|-------------------|-------------------|-------------|
//   | 信息     | #dee8f2 冷灰蓝    | #283c5a           |  8.99:1      |
//   | 警告     | #fff0be 黄        | #785000           |  6.27:1      |
//   | 严重     | #ffcdc3 橙红      | #96140a           |  6.15:1      |
//   | 危急     | #cd2828 深红+白字 | #ffffff           |  5.34:1      |
//
//   ⚠ 这一列以前写的是 11.6 / 10.1 / 8.3 / 5.34 ——【四行里错了三行】,
//     而且错得不小(8.3 实际是 6.15)。原因是那些数是"看着挺高"写上去的,
//     没有任何一个真的套 WCAG 公式算过 ——
//     而这个文件的全部存在意义就是"别让注释里的对比度变成一句没依据的话"。
//     现在表里每个数都由 tst_AlarmRowPalette::documentedRatiosAreTrue 实测钉住,
//     改颜色就会红,强制注释和代码两边一起改。
//
//   危急是【唯一用白字】的一档:白字 + 深红是"最重"最直接的约定。
//   注意它的对比度最低(5.34:1)—— 因为底色压得最深,白字能站住的余量本来就小。
struct LevelStyle { QColor bg; QColor fg; };

LevelStyle styleFor(AlarmLevel level) {
    switch (level) {
        // 信息 —— 冷灰蓝,最不显眼,符合"知会一声"的定位
        case AlarmLevel::Info:      return { QColor(222, 232, 242), QColor( 40,  60,  90) };
        // 警告 —— 黄,对应现场该去看一眼
        case AlarmLevel::Warning:   return { QColor(255, 240, 190), QColor(120,  80,   0) };
        // 严重 —— 橙红
        case AlarmLevel::Critical:  return { QColor(255, 205, 195), QColor(150,  20,  10) };
        // 危急 —— 深红 + 白字(最深 = 最重)
        case AlarmLevel::Emergency: return { QColor(205,  40,  40), QColor(255, 255, 255) };
    }
    // 不该到这:AlarmLevel 只有这 4 个值,switch 已全覆盖。
    // 仍然给一个【可见】的兜底而不是黑色 —— 黑色底在深色主题下等于"这行消失了",
    // 那正是本文件要消灭的那类静默失败。真到了这里,qWarning 会指出来。
    qWarning("AlarmRowPalette: 未知 AlarmLevel,配色兜底为 Info");
    return { QColor(222, 232, 242), QColor( 40,  60,  90) };
}

// ★ 已处理(已确认/已恢复)统一淡化成灰。
//   让"还没处理的"自己跳出来 —— 这是注意力经济:
//   报警列表里每一行都是报警,颜色必须能区分"要管"和"不用管"。
//
//   ⚠ 字色是【算出来的,不是挑的】。
//     F3 当年定的 120(灰底 #f0f0f0 上 #787878)对比度只有 3.87:1 ——
//     达不到 WCAG AA 的 4.5:1,于是 tst_AlarmRowPalette 直接判红。
//     这一档不是"装饰":它上面写着报警名、触发时间、触发值。
//     操作员事后回看"十分钟前那条确认过了没",读的就是这几个字 ——
//     把它淡化成"看得见但读不清",等于给追溯留了个坑。
//     压到 105 之后是 4.82:1,刚好越过 AA 线,而"最灰的那一档"这个层级关系没变
//     (在用的四档都在 5.2:1 以上,且带饱和底色,仍是全表最跳眼的元素)。
constexpr int kHandledBg = 240;
constexpr int kHandledFg = 105;

} // namespace

RowColors colorsFor(AlarmLevel level, RowVisualState state, bool blinkOn) {
    // 已处理的一律灰化 —— 【在闪烁之前短路】。
    // 顺序很重要:如果先判闪烁,已确认的危急行会在亮相时跳回红色,
    // 而它恰恰是"已经处理完、不该再抢注意力"的那一条。
    if (state == RowVisualState::Handled) {
        return { QColor(kHandledBg, kHandledBg, kHandledBg),
                 QColor(kHandledFg, kHandledFg, kHandledFg) };
    }

    const LevelStyle base = styleFor(level);

    if (!blinkOn || !shouldBlink(level, state)) {
        return { base.bg, base.fg };   // 暗相,或压根不该闪的档位
    }

    // ===== 亮相:本档底色【提亮】,色相不变 =====
    //
    //   【关键设计】闪的是【亮度】,不是【换一套颜色】。
    //   两件事各走各的通道,不互相覆盖:
    //     · 闪     = 在说"这条还没处理"
    //     · 颜色深浅 = 在说"这条有多重"
    //   换成固定红橙的话,危急/严重/警告会共用一个红 ——
    //   而"哪一档"正是颜色在表达的信息,操作员靠它分轻重,
    //   闪一下就分不出来了,闪烁反而害事。
    //
    //   ★【这里踩过一次坑,原文照搬以免再犯】
    //     最初写的是"亮红 #f05a5a + 白字",注释还特意标了"保证对比度不降"。
    //     拿 WCAG 相对亮度公式算了一遍才发现那句话是假的:
    //       暗相 #cd2828 + 白字 = 5.34:1
    //       亮相 #f05a5a + 白字 = 3.33:1   ← 掉了 2 个点
    //     也就是说闪烁最亮的那一瞬间,字恰恰是最难读的一瞬 ——
    //     而那正是操作员最想看清"哪条报警、多严重"的时候。
    //     根因:底色一旦大幅提亮,白字必然失效,两者不能同时要。
    //     解法:亮底配深字。
    //
    //   | 相位 | 底色     | 字色     | 相对亮度 | 对比度 |
    //   |------|----------|----------|----------|--------|
    //   | 危急暗 | #cd2828 | #ffffff | 0.1465 | 5.34:1 |
    //   | 危急亮 | #f05a5a | #3b0606 | 0.2658 | 5.20:1 |
    //   | 严重暗 | #ffcdc3 | #96140a | 0.6886 | 6.15:1 |
    //   | 严重亮 | #ff9b87 | #780a05 | 0.4647 | 5.58:1 |
    //
    //   两档提亮幅度不同:危急 +81%、严重 -32%。
    //   看着不对称,是刻意的 —— 危急是深底,往上提空间大;
    //   严重本来就是浅底(亮度 0.69),再提就顶到白、离底色太远不像橙红了。
    //   判据统一交给测试:两相相对亮度差 ≥20%,否则余光抓不到。
    //
    //   代价:危急在亮相那半拍不是白字了。
    //   取舍理由:"看得清"优先于"用白字标识最重的那一档" ——
    //   后者只是个约定,前者是能不能读。
    //
    //   ★ 严重亮相的 #ff9b87 不是随手挑的,是【算】出来的。
    //     色相 = 60 × (G−B) / (R−B)。暗相 #ffcdc3:R−B=60、G−B=10 → 色相 10°。
    //     #ff9b87:R−B=120、G−B=20 → 10×(120/60)=... 即 60×20/120 = 10°。【完全相同】。
    //     (最初定的 #ff9682:R−B=125、G−B=20 → 9.6°,
    //      QColor::hue() 取整后变成 9°,被 lit_keepsTheSameHueAsDark 判红。
    //      差 1° 肉眼看不出来,但那条测试守的是"色相不许变",
    //      与其把判据放宽成"差 1° 算过",不如把颜色改成【精确】保色相 ——
    //      反正多 1° 亮度余量还是想要的。)
    if (level == AlarmLevel::Emergency) {
        return { QColor(240,  90,  90), QColor( 59,   6,   6) };  // 深红 → 亮红,白字 → 深字
    }
    if (level == AlarmLevel::Critical) {
        // 严重档本来就是浅底配深字,提亮底色的同时把字压深,
        // 对比度仍稳在 5.58:1,不需要像危急那样翻过来配。
        return { QColor(255, 155, 135), QColor(120,  10,   5) };
    }
    // Info / Warning 走到这里说明 shouldBlink 的判据变了 —— 不会发生,但兜住。
    return { base.bg, base.fg };
}

bool shouldBlink(AlarmLevel level, RowVisualState state) {
    return state == RowVisualState::Unacknowledged
        && (level == AlarmLevel::Critical
            || level == AlarmLevel::Emergency);
}

RowVisualState visualStateFor(AlarmState st) {
    // ★ 只把 Active 当成"未确认"。
    //   Active 之外的两个值(已确认 / 已恢复)在配色上完全一样,
    //   所以这里不需要区分它们 —— 差异体现在文字上,那是调用方的事。
    return st == AlarmState::Active ? RowVisualState::Unacknowledged
                                    : RowVisualState::Handled;
}

} // namespace qwhmi::alarm
