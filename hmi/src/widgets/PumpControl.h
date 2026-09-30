// ============================================================================
// 阶段 E · E.1c · PumpControl(操作类 — 泵)
//
// 文件职责:离心泵标准符号(圆壳 + 出口管嘴) + 三叶转子。点击启停,
//   通过 ControlBase::writeToPlc 真写回 PLC;运行时转子旋转。
//
// 【符号】GB/T 泵的符号是「圆 + 顶部出口管嘴」。转子画成 3 片叶片 ——
//   离心泵真实就是 3 叶,画对了操作员一眼认出是泵不是电机。
//   (MotorControl 才是圆 + M 字,两者不能画混:泵是输送介质,
//    电机是提供动力。混淆意味着操作员对错设备下指令。)
//
// 【动画的启停条件】见 SpinAnimator.h 头注释:只在【绑了真实变量且值为非 0】
//   时转。不需要跟 EditorMainWindow 的 editorMode_ 同步 —— 绑没绑变量
//   本身就是"是不是编辑器"的信号。
//
// 关联:ControlBase.h, SpinAnimator.h, PumpControl.cpp
// ============================================================================

#pragma once

#include "widgets/base/ControlBase.h"
#include "widgets/base/SpinAnimator.h"
#include <QColor>

namespace qwhmi::widgets {

class PumpControl : public ControlBase {
    Q_OBJECT
    Q_PROPERTY(QColor bodyColor READ bodyColor WRITE setBodyColor NOTIFY bodyColorChanged)
    Q_PROPERTY(QColor runColor READ runColor WRITE setRunColor NOTIFY runColorChanged)
    // ★ 一圈多久(ms)。做成属性是因为大泵小泵转速观感该不同 ——
    //   现场经常要求"转得明显一点/转得不晃眼"
    Q_PROPERTY(int spinPeriodMs READ spinPeriodMs WRITE setSpinPeriodMs NOTIFY spinPeriodMsChanged)

public:
    explicit PumpControl(QGraphicsItem* parent = nullptr);
    ~PumpControl() override;

    QColor bodyColor() const { return bodyColor_; }
    void   setBodyColor(const QColor& c);
    QColor runColor() const { return runColor_; }
    void   setRunColor(const QColor& c);
    int    spinPeriodMs() const { return spin_.periodMs(); }
    void   setSpinPeriodMs(int ms);

    // ★ 运行中 = value >= 0.5。跟 ValveControl 同阈值
    bool   isRunning() const;
    // ★ 转子角度 —— paint 读它。spin() 直接暴露给测试和将来的 tooltip
    qreal  spinAngle() const { return spin_.angle(); }
    // ★ 动画对象暴露出来。单测要断言 state()(不跑事件循环),
    //   还要调 applyFrame() 手动喂帧复现跨圈 —— 那是 non-const 的,
    //   所以这里给两个版本,只给 const 版的话单测没法驱动它
    SpinAnimator&       spinAnimator()       { return spin_; }
    const SpinAnimator& spinAnimator() const { return spin_; }

    QRectF boundingRect() const override;
    void paintContent(QPainter* painter,
                 const QStyleOptionGraphicsItem* option,
                 QWidget* widget = nullptr) override;
    void   mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;

signals:
    void bodyColorChanged();
    void runColorChanged();
    void spinPeriodMsChanged();
    // ★ 跟的是"运行态变化"不是"点击" —— 远程启泵也要反映到画面
    void runningChanged();
    void writeRejected(const QString& detail);

private:
    // ★ 运行态变化时同步动画开关。没放 public 是因为它只该被 valueChanged
    //   的 lambda 调 —— 让外部调会让动画状态和 isRunning() 脱节
    void   updateRunning();

    QColor bodyColor_ = QColor(150, 155, 160);   // 泵壳灰
    QColor runColor_  = QColor(0, 160, 90);       // 运行绿
    SpinAnimator spin_;
    bool   lastRunning_ = false;
};

} // namespace qwhmi::widgets
