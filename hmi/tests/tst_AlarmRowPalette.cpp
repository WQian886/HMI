// ============================================================================
// tst_AlarmRowPalette
//
// ★ 为什么要有这个测试文件(2026-10-14)
//
//   这个文件测的是【纯函数】—— 没有 QWidget、没有定时器、没有对象树,
//   所以每条用例都在几微秒内跑完,可以写得比平时密。
//
//   它守的是"配色只能有一个权威"这条约定本身:
//     · 两套报警 UI(主画面 / 运行时画布)现在都调这一个文件,
//       所以这里定下的四条不变量,两套 UI 同时受约束
//     · 尤其第 4 条(亮相不许换色相)—— 它正是把主画面从
//       "换成固定红/橙"换掉的那条判据。哪天有人图省事改回填红色,
//       这条会立刻红。
//
//   ★ 负向对照(改坏 → 跑 → 必须红)已在提交前实测,见文件末尾注释。
// ============================================================================

#include <QtTest>
#include <cmath>
#include <QColor>

#include "alarm/AlarmRowPalette.h"
#include "../Alarm.h"

using namespace qwhmi::alarm;

namespace {

// WCAG 2.x 相对亮度 + 对比度。
// 【为什么不直接用 QColor::luminance()】它用的是另一套算法(不区分 sRGB gamma),
// 算出来的数跟 WCAG 不一样,拿它断言等于用一把自己削的尺子量东西。
double relativeLuminance(const QColor& c) {
    auto f = [](int v) {
        const double x = v / 255.0;
        return x <= 0.03928 ? x / 12.92 : std::pow((x + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * f(c.red()) + 0.7152 * f(c.green()) + 0.0722 * f(c.blue());
}

double contrastRatio(const QColor& a, const QColor& b) {
    const double la = relativeLuminance(a);
    const double lb = relativeLuminance(b);
    const double hi = std::max(la, lb);
    const double lo = std::min(la, lb);
    return (hi + 0.05) / (lo + 0.05);
}

const AlarmLevel kAllLevels[] = {
    AlarmLevel::Info, AlarmLevel::Warning, AlarmLevel::Critical, AlarmLevel::Emergency
};

QString levelName(AlarmLevel l) {
    switch (l) {
        case AlarmLevel::Info:      return QStringLiteral("Info");
        case AlarmLevel::Warning:   return QStringLiteral("Warning");
        case AlarmLevel::Critical:  return QStringLiteral("Critical");
        case AlarmLevel::Emergency: return QStringLiteral("Emergency");
    }
    return QStringLiteral("?");
}

} // namespace

class TestAlarmRowPalette : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { qputenv("QT_QPA_PLATFORM", "offscreen"); }

    // ===== 1. 四档在静止态必须两两可分 =====
    void fourLevels_haveDistinctStaticColors();

    // ===== 2. 已处理的一律灰化 =====
    void handledRow_isGreyRegardlessOfLevel();
    void handledRow_ignoresBlinkPhase();

    // ===== 3. 闪烁只影响该闪的两档 =====
    void onlyCriticalAndEmergency_changeColorWhenLit();
    void litAndDark_differForBlinkingLevels();

    // ===== 4. ★ 亮相不许换色相(核心不变量)=====
    void lit_keepsTheSameHueAsDark();

    // ===== 5. ★ 两相都得看得清(WCAG AA)=====
    void bothPhases_stayAboveWcagAA();

    // ===== 6. 判据与状态映射 =====
    void shouldBlink_onlyUnacknowledgedCriticalAndEmergency();
    void visualStateFor_mapsActiveToUnacknowledged();

    // ===== 7. ★ 注释里的对比度不许是编的 =====
    void documentedRatiosAreTrue();
};

namespace {
RowColors dark(AlarmLevel l) { return colorsFor(l, RowVisualState::Unacknowledged, false); }
RowColors lit (AlarmLevel l) { return colorsFor(l, RowVisualState::Unacknowledged, true); }
} // namespace

// ---------------------------------------------------------------- 1

// 【为什么这条不是废话】
//   F3 就是在修"Info / Warning 跟普通行一样、根本分不出档位"这个缺陷。
//   主画面那套到 2026-10-14 都还没有四档色(非闪烁行被清成白底),
//   也就是说主画面一直留着这个缺陷 —— 本次一并修掉。
//   而"分得出"这件事【没有任何几何断言能查】,只能直接比色值。
void TestAlarmRowPalette::fourLevels_haveDistinctStaticColors() {
    for (size_t i = 0; i < 4; ++i) {
        for (size_t j = i + 1; j < 4; ++j) {
            const RowColors a = dark(kAllLevels[i]);
            const RowColors b = dark(kAllLevels[j]);
            QVERIFY2(a.bg != b.bg || a.fg != b.fg,
                     qPrintable(QStringLiteral("%1 与 %2 的配色撞了")
                                    .arg(levelName(kAllLevels[i]), levelName(kAllLevels[j]))));
        }
    }
    // 每档自身的字色也不能跟底色一样 —— 那样字就"消失"了。
    for (AlarmLevel l : kAllLevels) {
        const RowColors c = dark(l);
        QVERIFY2(c.bg != c.fg,
                 qPrintable(QStringLiteral("%1 字色与底色相同,字看不见").arg(levelName(l))));
    }
}

// ---------------------------------------------------------------- 2

void TestAlarmRowPalette::handledRow_isGreyRegardlessOfLevel() {
    const RowColors ref = colorsFor(AlarmLevel::Info, RowVisualState::Handled, false);
    for (AlarmLevel l : kAllLevels) {
        const RowColors c = colorsFor(l, RowVisualState::Handled, false);
        QVERIFY2(c.bg == ref.bg && c.fg == ref.fg,
                 qPrintable(QStringLiteral("%1 已处理后没有被灰化").arg(levelName(l))));
    }
}

void TestAlarmRowPalette::handledRow_ignoresBlinkPhase() {
    // 已处理的行【必须】在任何相位下都是灰的。
    // 若先判闪烁再判灰,已确认的危急行会在亮相时跳回红色 ——
    // 而它恰恰是"已经处理完、不该再抢注意力"的那一条。
    for (AlarmLevel l : kAllLevels) {
        QVERIFY2(colorsFor(l, RowVisualState::Handled, true)
                     == colorsFor(l, RowVisualState::Handled, false),
                 qPrintable(QStringLiteral("%1 已处理行的颜色被闪烁相位影响了").arg(levelName(l))));
    }
}

// ---------------------------------------------------------------- 3

void TestAlarmRowPalette::onlyCriticalAndEmergency_changeColorWhenLit() {
    // 信息/警告不闪 —— 现场最吵的一直在闪,真出事的那条就被淹了。
    for (AlarmLevel l : {AlarmLevel::Info, AlarmLevel::Warning}) {
        QVERIFY2(lit(l) == dark(l),
                 qPrintable(QStringLiteral("%1 不该闪,但亮相时颜色变了").arg(levelName(l))));
    }
    for (AlarmLevel l : {AlarmLevel::Critical, AlarmLevel::Emergency}) {
        QVERIFY2(lit(l).bg != dark(l).bg,
                 qPrintable(QStringLiteral("%1 该闪,但亮相与暗相底色相同 —— 闪了看不出来")
                                .arg(levelName(l))));
    }
}

void TestAlarmRowPalette::litAndDark_differForBlinkingLevels() {
    // 而且要【肉眼可辨】地不同,不只是"数值不一样"。
    // 相邻两相之间相对亮度差至少 20%,余光才抓得住。
    for (AlarmLevel l : {AlarmLevel::Critical, AlarmLevel::Emergency}) {
        const RowColors a = dark(l);
        const RowColors b = lit(l);
        const double da = relativeLuminance(a.bg);
        const double db = relativeLuminance(b.bg);
        const double delta = da > 0 ? std::abs(db - da) / da : 1.0;
        QVERIFY2(delta >= 0.20,
                 qPrintable(QStringLiteral("%1 两相亮度只差 %2%,余光抓不到")
                                .arg(levelName(l))
                                .arg(delta * 100.0, 0, 'f', 1)));
    }
}

// ---------------------------------------------------------------- 4

// ★ 本文件最要紧的一条。
//
//   把主画面从"闪的时候换成固定红/橙"改成"本档底色提亮"的,
//   就是这条不变量。换成固定色的后果不是不好看,是【危急/严重/警告共用一个红】——
//   而"哪一档"正是颜色在表达的信息,闪一下就分不出来了,闪烁反而害事。
void TestAlarmRowPalette::lit_keepsTheSameHueAsDark() {
    for (AlarmLevel l : {AlarmLevel::Critical, AlarmLevel::Emergency}) {
        const QColor a = dark(l).bg;
        const QColor b = lit(l).bg;
        QVERIFY2(a.hue() == b.hue(),
                 qPrintable(QStringLiteral("%1 亮相换了色相:%2(hue %3)→ %4(hue %5)—— "
                                           "这意味着两档会共用同一个红,闪一下就分不出档位")
                                .arg(levelName(l))
                                .arg(a.name()).arg(a.hue())
                                .arg(b.name()).arg(b.hue())));
    }
}

// ---------------------------------------------------------------- 5

// ★ 从一次真实错误里长出来的:曾把"保证对比度不降"写进注释,
//   拿 WCAG 一算那句话是假的(亮红配白字只有 3.33:1)。
//   注释留不住 —— 得让测试拦住下一次调色。
void TestAlarmRowPalette::bothPhases_stayAboveWcagAA() {
    for (AlarmLevel l : kAllLevels) {
        for (bool blink : {false, true}) {
            const RowColors c = colorsFor(l, RowVisualState::Unacknowledged, blink);
            const double r = contrastRatio(c.bg, c.fg);
            QVERIFY2(r >= 4.5,
                     qPrintable(QStringLiteral("%1 %2相对比度只有 %3:1 —— 字看不清")
                                    .arg(levelName(l),
                                         blink ? QStringLiteral("亮") : QStringLiteral("暗"))
                                    .arg(r, 0, 'f', 2)));
        }
        // 已处理的行同样要看得清 —— 它是灰底浅灰字,别忘了。
        // ★ 这条真的红过:F3 当年定的 #787878 只有 3.87:1。
        //   失败一次就说明"淡化"很容易顺手淡化过头 —— 它是唯一一个
        //   不带饱和底色的档位,WCAG 一旦没人算就没人看得见。
        const RowColors h = colorsFor(l, RowVisualState::Handled, false);
        const double hr = contrastRatio(h.bg, h.fg);
        QVERIFY2(hr >= 4.5,
                 qPrintable(QStringLiteral("%1 已处理行对比度只有 %2:1(%3 底 %4 字)")
                                .arg(levelName(l))
                                .arg(hr, 0, 'f', 2)
                                .arg(h.bg.name(), h.fg.name())));
    }
}

// ---------------------------------------------------------------- 6

void TestAlarmRowPalette::shouldBlink_onlyUnacknowledgedCriticalAndEmergency() {
    for (AlarmLevel l : kAllLevels) {
        const bool expect = (l == AlarmLevel::Critical || l == AlarmLevel::Emergency);
        QVERIFY2(shouldBlink(l, RowVisualState::Unacknowledged) == expect,
                 qPrintable(QStringLiteral("%1 未确认时的闪烁判据不对").arg(levelName(l))));
        // 已处理的永远不闪 —— 已确认的还闪 = 骗操作员去处理一条已经处理过的。
        QVERIFY2(!shouldBlink(l, RowVisualState::Handled),
                 qPrintable(QStringLiteral("%1 已处理却还在闪").arg(levelName(l))));
    }
}

void TestAlarmRowPalette::visualStateFor_mapsActiveToUnacknowledged() {
    QCOMPARE(visualStateFor(AlarmState::Active),       RowVisualState::Unacknowledged);
    QCOMPARE(visualStateFor(AlarmState::Acknowledged), RowVisualState::Handled);
    // Cleared(已恢复)同样算已处理 —— 条件消失后不该再抢注意力。
    QCOMPARE(visualStateFor(AlarmState::Cleared),      RowVisualState::Handled);
}

// ★ 注释里的对比度不许是编的。
//
//   【为什么这条值得单独占一个用例】
//   写 AlarmRowPalette.cpp 时我顺手在注释表里填了四行对比度,
//   写完看都没看。今天第一次真拿 WCAG 公式去算,四行里错了三行:
//       注释  实际
//       11.6   8.99
//       10.1   6.27
//        8.3   6.15
//        5.34  5.34
//   最严重的"严重"档,注释说 8.3,实际 6.15,虚高了 35%。
//
//   讽刺的是:这个模块存在的全部理由就是"别让注释说没依据的话",
//   而它自己的第一版注释就是没依据的话。
//   那句"注释留不住 —— 得让测试拦住"如果自己不照做,就等于白写。
//
//   【所以这条断言的是"注释里的数 == 实际的数",不是"实际 >= 某个线"】
//   上一条 bothPhases_stayAboveWcagAA 已经守住了 ≥4.5 的地板。
//   这条守的是另一件事:改了颜色之后,注释不会悄悄变成一句谎话。
//   【它是有意脆弱的】颜色一动它就红,那是设计:
//   红了就说明"该改注释了",改颜色和改注释必须一起做,不能只做一个。
void TestAlarmRowPalette::documentedRatiosAreTrue() {
    struct Row { AlarmLevel level; const char* name; double dark; bool blinks; double lit; };
    // 数值 = AlarmRowPalette.cpp 两张注释表里写的那些数。
    //      改了那边记得改这边 —— 改一边不改另一边,这条会红,那就是它存在的意义。
    const Row rows[] = {
        { AlarmLevel::Info,      "信息", 8.99, false, 0.0  },
        { AlarmLevel::Warning,   "警告", 6.27, false, 0.0  },
        { AlarmLevel::Critical,  "严重", 6.15, true,  5.58 },
        { AlarmLevel::Emergency, "危急", 5.34, true,  5.20 },
    };
    for (const Row& r : rows) {
        const RowColors d = colorsFor(r.level, RowVisualState::Unacknowledged, false);
        const double actual = contrastRatio(d.bg, d.fg);
        // 容差 0.01:注释里写的是两位小数,比对也要留两位小数的余量。
        QVERIFY2(qAbs(actual - r.dark) < 0.01,
                 qPrintable(QStringLiteral("%1 静止态注释写 %2:1,实际 %3:1(%4 底 %5 字)—— "
                                           "注释和代码对不上了,两边一起改")
                                .arg(QString::fromUtf8(r.name))
                                .arg(r.dark, 0, 'f', 2)
                                .arg(actual, 0, 'f', 2)
                                .arg(d.bg.name(), d.fg.name())));
        if (!r.blinks) continue;
        const RowColors t = colorsFor(r.level, RowVisualState::Unacknowledged, true);
        const double tl = contrastRatio(t.bg, t.fg);
        QVERIFY2(qAbs(tl - r.lit) < 0.01,
                 qPrintable(QStringLiteral("%1 亮相注释写 %2:1,实际 %3:1(%4 底 %5 字)")
                                .arg(QString::fromUtf8(r.name))
                                .arg(r.lit, 0, 'f', 2)
                                .arg(tl, 0, 'f', 2)
                                .arg(t.bg.name(), t.fg.name())));
    }
    // 已处理那档也在注释里承诺了 4.82:1,同样钉住。
    const RowColors h = colorsFor(AlarmLevel::Info, RowVisualState::Handled, false);
    const double hr = contrastRatio(h.bg, h.fg);
    QVERIFY2(qAbs(hr - 4.82) < 0.01,
             qPrintable(QStringLiteral("已处理行注释写 4.82:1,实际 %1:1").arg(hr, 0, 'f', 2)));
}

QTEST_MAIN(TestAlarmRowPalette)
#include "tst_AlarmRowPalette.moc"
