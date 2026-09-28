// ============================================================================
// 阶段 E · E.1c · SpinAnimator 实现
// 对应 SpinAnimator.h
// ============================================================================

#include "widgets/base/SpinAnimator.h"

namespace qwhmi::widgets {

SpinAnimator::SpinAnimator(QObject* parent)
    : QObject(parent)
    , anim_(std::make_unique<QVariantAnimation>())
{
    anim_->setStartValue(0.0);
    anim_->setEndValue(360.0);
    anim_->setDuration(periodMs_);
    anim_->setLoopCount(-1);   // -1 = 无限循环(QAbstractAnimation::Infinite)
    anim_->setEasingCurve(QEasingCurve::Linear);

    connect(anim_.get(), &QVariantAnimation::valueChanged, this,
            [this](const QVariant& v) {
        // ★ 关键:QVariantAnimation 循环时每圈都把 value 从 0 重新开始。
        //   直接 `angle_ = v.toReal()` 的话,350° + 20° 会跳回 10° ——
        //   叶片每转一圈就在 12 点方向"闪"一下。

        // 这里用 currentLoop() 而不是自己数圈数,是因为"自己累加 delta"
        // 那套写法只在每帧都来一次的前提下成立:一旦卡一帧(角度从 355
        // 直接跳到下一圈的 5),累加会以为只走了 5°,丢掉整整一圈,
        // 表现为泵的转速随卡顿忽快忽慢。currentLoop() 是动画自己维护的
        // 真实圈数,跟帧率无关。
        //
        // ★ currentLoopChange() 是信号(只能 emit 不能调),这里用的是
        //   public 的 currentLoop() 取值 —— 之前误以为两者都是 protected
        //   而去手写圈数计数,是错的方向。
        angle_ = offset_ + static_cast<qreal>(anim_->currentLoop()) * 360.0 + v.toReal();
        emit angleChanged(angle_);
    });
}

SpinAnimator::~SpinAnimator() = default;

void SpinAnimator::setPeriodMs(int ms) {
    if (ms < 50) ms = 50;      // 一圈 < 50ms 看着是频闪不是旋转
    if (ms > 10000) ms = 10000;
    if (ms == periodMs_) return;
    const bool wasRunning = isRunning();
    anim_->stop();
    periodMs_ = ms;
    anim_->setDuration(ms);
    if (wasRunning) anim_->start();   // 改周期时不停转
}

void SpinAnimator::setRunning(bool on) {
    // ★ 幂等:已在转还调 start() 会把动画重置回 0° —— 叶片会明显"顿一下"。
    //   valueChanged 在每次 PLC 回读时都触发,那里是无条件调 setRunning 的
    if (on == isRunning()) return;

    if (on) {
        // ★ 从停下的角度接着转,不跳回 0°。offset_ 记的是"重新启动那一刻
        //   的累计角度",之后公式里加上它,新一圈就接在旧角度后面。
        //   泵停下再启动,叶片应该从它停住的地方继续转,而不是弹回 0°。
        offset_ = angle_;
        anim_->start();
    } else {
        anim_->stop();
        // 停在当前角度而不是回 0 —— 泵停下时叶片应该停在某处,
        // 跳回 0° 看着像设备复位了
    }
}

bool SpinAnimator::isRunning() const {
    return anim_->state() == QAbstractAnimation::Running;
}

void SpinAnimator::advance(qreal degrees) {
    // 测试用:不依赖事件循环直接推进。生产代码走 QVariantAnimation 的
    // valueChanged,这里只给单测用(单测不跑事件循环)
    angle_ += degrees;
    emit angleChanged(angle_);
}

void SpinAnimator::applyFrame(qreal value, int loop) {
    // 测试用:复现 valueChanged lambda 的算式,把"某一圈某一帧"的输入
    // 手动喂进来。写成独立函数而不是让单测去重算一遍公式 ——
    // 重算等于把公式抄成两份,以后改了生产代码测试还在测旧公式
    angle_ = offset_ + static_cast<qreal>(loop) * 360.0 + value;
    emit angleChanged(angle_);
}

} // namespace qwhmi::widgets
