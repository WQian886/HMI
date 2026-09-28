// ============================================================================
// 阶段 E · E.1c · SpinAnimator —— 转子旋转动画(泵/电机共用)
//
// 文件职责:一个极小的混入类,给"带转子"的控件提供旋转动画。
//   PumpControl 和 MotorControl 都需要"运行时转、停止时停",逻辑完全一样,
//   抽出来避免各写一遍 —— 而各写一遍的必然结果是两份动画起停条件
//   慢慢漂移不一致。
//
// 【为什么不直接放 ControlBase】ControlBase 有 22 个派生类,其中 20 个
//   根本不转(标签/管道/储罐/表格/曲线...)。给它们每个都挂一个
//   QPropertyAnimation 是白占内存 + 白添一个 Q_PROPERTY 出现在属性面板里
//   (PropertySheet 反射 Q_PROPERTY,会多出一栏没意义的"旋转动画")。
//
// 【动画起停的判定】只在【已绑定真实变量 且 当前处于运行态】时转:
//     · 未绑定 → 编辑器里的空摆件,不该动
//     · 绑了但值为 0 → 设备停了,不该动
//   好处:不需要跟 EditorMainWindow 的 editorMode_ 做模式同步
//   (见 hmi/src/editor/EditorMainWindow.h:146)—— 绑定与否本身就是"是不是
//   编辑器"的充分信号。多引入一个模式标志就多一处可能不同步的地方。
//
// 【为什么不用 QPropertyAnimation 改 rotation】QPropertyAnimation 要改
//   被动画对象的属性,rotation 是 ControlBase::rotation(第 3 块才加)。
//   在那之前用不了,而且会把"控件整体转 90°"和"转子在转"两个概念混进
//   同一个属性 —— 那是最容易出 bug 的耦合。这里动画只维护一个自己的
//   角度值,转子画的时候读它,两者互不干扰。
//
// 【单测怎么测】单测不跑事件循环(QTest 的事件循环只在 exec() 里转),
//   所以只断言:动画对象存在、Running/Stopped 状态正确、setRunning 前后的
//   state() 变化。画面像素不在这里验(见 tst_HandleVisualAlignment 也不做
//   像素比对 —— 像素比对在改字体/抗锯齿时会无故变红)。
//
// 关联:PumpControl, MotorControl, ControlBase
// ============================================================================

#pragma once

#include <QObject>
#include <QVariantAnimation>
#include <memory>

namespace qwhmi::widgets {

class SpinAnimator : public QObject {
    Q_OBJECT
public:
    explicit SpinAnimator(QObject* parent = nullptr);
    ~SpinAnimator() override;

    // ★ 转一圈要多久(ms)。默认 1200 —— 约 50 RPM 的观感,
    //   太快看不清叶片在转,太慢像卡住了
    void   setPeriodMs(int ms);
    int    periodMs() const { return periodMs_; }

    // ★ 开关动画。start() 是幂等的(已在转就什么都不做),
    //   stop() 也一样 —— 上层可以在每次 valueChanged 时无条件调
    void   setRunning(bool on);
    bool   isRunning() const;

    // ★ 当前转子角度(度)。【不是 0~360 的环量,是单调递增的累计角度】
    //   画叶片时用 qFmod 取模即可 —— 之所以要累计,就是为了跨圈不回跳。
    //   单测断言这个值,别断言 < 360。
    qreal  angle() const { return angle_; }

    // ★ 手动推进(测试用,不依赖事件循环)。单测靠它验证角度真的会累加
    void   advance(qreal degrees);

    // ★ 手动喂一次"动画帧值"(测试用)。单测要复现跨圈行为时用得上 ——
    //   正常路径只有 QVariantAnimation 的 valueChanged 会调它
    void   applyFrame(qreal value, int loop);

signals:
    // ★ 角度变了 → 宿主控件 update() 重绘
    void   angleChanged(qreal angle);

private:
    std::unique_ptr<QVariantAnimation> anim_;
    int    periodMs_ = 1200;
    qreal  angle_    = 0.0;
    // ★ 重新启动那一刻的累计角度。公式见 SpinAnimator.cpp 的 valueChanged:
    //   angle_ = offset_ + currentLoop()*360 + 当前帧值
    //   offset_ 让"停下再启动"从原角度接着转,而不是弹回 0°
    qreal  offset_   = 0.0;
};

} // namespace qwhmi::widgets
