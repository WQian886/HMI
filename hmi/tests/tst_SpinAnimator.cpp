// ============================================================================
// 阶段 E · E.1c · tst_SpinAnimator —— 转子旋转动画
//
// 【测什么】
//   起停幂等 / 周期夹取 / 角度累加 / ★ 跨 360° 不回跳 / 掉帧不丢圈
//
// 【为什么单测不跑事件循环】QTest 只在 exec() 里转事件循环,用例体里
//   写 QTest::qWait(100) 等动画跑完是不行的(动画走的是 QTimer)。
//   所以:
//     · 状态类断言(是不是 Running / 周期是多少)直接读
//     · 角度靠 applyFrame(value, loop) 这个测试入口手动喂帧
//   画面像素不在这里验 —— 像素比对在改字体/抗锯齿时会无故变红,
//   项目里已有先例(tst_HandleVisualAlignment 也不做像素比对)。
//
// ★ 本文件锁的是 SpinAnimator 存在的全部理由:跨圈不回跳。
//   QVariantAnimation 循环时每圈都把值从 0 重来,直接赋值的话
//   350° + 20° 会跳回 10° —— 泵的叶片每转一圈就在 12 点方向闪一下。
//   这 bug 在跑起来之前看不出来,一旦转起来就一直闪。
// ============================================================================

#include <QtTest>
#include <QApplication>
#include "widgets/base/SpinAnimator.h"

using namespace qwhmi::widgets;

class TestSpinAnimator : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { qputenv("QT_QPA_PLATFORM", "offscreen"); }

    void construct_defaults();
    void setPeriodMs_clamps();
    void setPeriodMs_keepsRunning();
    void setRunning_togglesState();
    void setRunning_isIdempotent();          // ★ 重复 start 不能把角度重置
    void stop_keepsCurrentAngle();           // ★ 停泵不能跳回 0°
    void restart_continuesFromStoppedAngle();// ★ 重新启动不弹回 0°
    void advance_accumulates();
    void angleChanged_emitsOnAdvance();
    // ★★ 跨圈,本文件最要紧的两条
    void angleDoesNotJumpBackAt360();
    void droppedFrame_doesNotLoseAWholeTurn();
};

void TestSpinAnimator::construct_defaults() {
    SpinAnimator s;
    QCOMPARE(s.periodMs(), 1200);
    QVERIFY(!s.isRunning());
    QCOMPARE(s.angle(), 0.0);
}

void TestSpinAnimator::setPeriodMs_clamps() {
    // 一圈 < 50ms 看着是频闪不是旋转;太长则是卡住了
    SpinAnimator s;
    s.setPeriodMs(1);
    QCOMPARE(s.periodMs(), 50);
    s.setPeriodMs(-100);
    QCOMPARE(s.periodMs(), 50);
    s.setPeriodMs(999999);
    QCOMPARE(s.periodMs(), 10000);
    s.setPeriodMs(2000);
    QCOMPARE(s.periodMs(), 2000);
}

void TestSpinAnimator::setPeriodMs_keepsRunning() {
    SpinAnimator s;
    s.setRunning(true);
    s.setPeriodMs(800);
    QVERIFY2(s.isRunning(), "改转速时不该把正在转的泵停下来");
}

void TestSpinAnimator::setRunning_togglesState() {
    SpinAnimator s;
    QVERIFY(!s.isRunning());
    s.setRunning(true);
    QVERIFY(s.isRunning());
    s.setRunning(false);
    QVERIFY(!s.isRunning());
}

void TestSpinAnimator::setRunning_isIdempotent() {
    // ★ 为什么幂等很重要:泵的 valueChanged 在【每次 PLC 回读】时都触发,
    //   那里是无条件调 setRunning(isRunning()) 的。如果 start() 不幂等,
    //   每次回读都会把动画重置回 0° —— 叶片每秒"顿"一下,画面看着像卡住。
    //   注意 QVariantAnimation.start() 【本身就不幂等】,它会重置 currentTime。
    SpinAnimator s;
    s.setRunning(true);
    s.advance(137.0);
    const qreal before = s.angle();

    s.setRunning(true);      // 重复 start
    QCOMPARE(s.angle(), before);   // 角度不能被重置

    s.setRunning(true);
    s.setRunning(true);
    QCOMPARE(s.angle(), before);
}

// ★ 停泵应该停在当前角度,不是跳回 0°。跳回 0° 看着像设备复位了
void TestSpinAnimator::stop_keepsCurrentAngle() {
    SpinAnimator s;
    s.setRunning(true);
    s.advance(212.0);
    s.setRunning(false);
    QCOMPARE(s.angle(), 212.0);
    QVERIFY(!s.isRunning());
}

// ★ 停→启 要从停住的地方接着转。弹回 0° 看着像泵被复位了一次。
//   靠 offset_ 实现(见 SpinAnimator.h),没有它的话这个 bug 很隐蔽:
//   画面只是"每次启停转子跳一下",很容易被当成正常。
void TestSpinAnimator::restart_continuesFromStoppedAngle() {
    SpinAnimator s;
    s.setRunning(true);
    s.applyFrame(250.0, 0);      // 第 0 圈走到 250°
    QCOMPARE(s.angle(), 250.0);

    s.setRunning(false);
    QCOMPARE(s.angle(), 250.0);

    // 重新启动:第一帧 currentLoop() 归 0,但 offset_ 让它接在 250 后面
    s.setRunning(true);
    s.applyFrame(0.0, 0);
    QVERIFY2(qFuzzyCompare(s.angle(), 250.0),
             qPrintable(QStringLiteral("重新启动时角度弹回了 %1,应为 250")
                            .arg(s.angle())));
    s.applyFrame(30.0, 0);
    QCOMPARE(s.angle(), 280.0);
}

void TestSpinAnimator::advance_accumulates() {
    SpinAnimator s;
    s.advance(10.0);
    QCOMPARE(s.angle(), 10.0);
    s.advance(5.5);
    QCOMPARE(s.angle(), 15.5);
    s.advance(-15.5);
    QCOMPARE(s.angle(), 0.0);
}

void TestSpinAnimator::angleChanged_emitsOnAdvance() {
    SpinAnimator s;
    QSignalSpy spy(&s, &SpinAnimator::angleChanged);
    s.advance(45.0);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toReal(), 45.0);
}

// ★★ 跨圈。第一圈末尾 359°,第二圈开头 0° —— 角度必须接着涨。
//   少了跨圈补偿的话这里会得到 0°,上一帧 359° → 回跳 359°。
void TestSpinAnimator::angleDoesNotJumpBackAt360() {
    SpinAnimator s;
    s.setRunning(true);
    QSignalSpy spy(&s, &SpinAnimator::angleChanged);

    // (当前帧值, 圈号) —— 就是 QVariantAnimation 会发出来的组合
    const qreal frames[]  = { 340.0, 350.0, 359.0,   0.0,   5.0,  20.0 };
    const int    loops[]   = {     0,     0,     0,     1,    1,     1 };
    const qreal expect[]  = { 340.0, 350.0, 359.0, 360.0, 365.0, 380.0 };

    for (int i = 0; i < 6; ++i) {
        const qreal prev = s.angle();
        s.applyFrame(frames[i], loops[i]);
        QVERIFY2(s.angle() >= prev,
                 qPrintable(QStringLiteral("第 %1 帧角度回跳:%2 < %3 —— "
                                           "跨圈补偿失效,叶片每转一圈闪一下")
                                .arg(i).arg(s.angle()).arg(prev)));
        QVERIFY2(qFuzzyCompare(s.angle(), expect[i]),
                 qPrintable(QStringLiteral("第 %1 帧角度应为 %2,实际 %3")
                                .arg(i).arg(expect[i]).arg(s.angle())));
    }
    QCOMPARE(spy.count(), 6);
    QCOMPARE(s.angle(), 380.0);
}

// ★★ 掉帧。这是"自己累加 delta"那种写法独有的失败模式:
//   界面卡一帧,角度从第 0 圈的 355° 直接跳到第 1 圈的 5°。
//   如果实现是 angle_ += delta,这一帧会以为只走了 5°,
//   整整一圈(360°)凭空消失 —— 泵的转速会随卡顿忽快忽慢。
//   用 currentLoop() 的实现不受影响:圈数是动画自己数的,跟帧率无关。
void TestSpinAnimator::droppedFrame_doesNotLoseAWholeTurn() {
    SpinAnimator s;
    s.setRunning(true);

    s.applyFrame(355.0, 0);
    QCOMPARE(s.angle(), 355.0);

    // 卡了一帧,直接落到第 1 圈开头
    s.applyFrame(5.0, 1);
    QVERIFY2(qFuzzyCompare(s.angle(), 365.0),
             qPrintable(QStringLiteral("掉一帧后角度应为 365(355+10),实际 %1 —— "
                                       "整圈被吞掉了,转速会随卡顿忽快忽慢")
                            .arg(s.angle())));
    QCOMPARE(s.angle() - 355.0, 10.0);   // 只推进了这 5° 对应的量

    // 再跨 3 圈,验证多圈累加
    s.applyFrame(0.0, 4);
    QCOMPARE(s.angle(), 1440.0);
}

QTEST_MAIN(TestSpinAnimator)
#include "tst_SpinAnimator.moc"
