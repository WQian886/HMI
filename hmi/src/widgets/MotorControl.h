// ============================================================================
// 阶段 E · E.1c · MotorControl(操作类 — 电机)
//
// 文件职责:电机标准符号(圆 + M 字) + 旋转的通电符号。点击启停,
//   通过 ControlBase::writeToPlc 真写回 PLC;运行时旋转。
//
// 【为什么不能跟泵画混】泵是输送介质的设备,电机是提供动力的。
//   符号上:泵 = 圆 + 顶部出口管嘴 + 三叶转子;电机 = 圆 + M 字。
//   操作员对错设备下指令是要出事的 —— 泵停了可能是管路没压,
//   电机停了可能整条产线停了,处理方式完全不同。
//
// 【跟泵的差别】泵有介质进出口(所以画管嘴),电机只有轴 —— 所以
//   画成"圆 + M",没有管嘴。这也是为什么两个控件不能共用一个类:
//   画法完全不同,只有"启停 + 旋转"这部分逻辑一样,那部分抽到了
//   SpinAnimator。
//
// 关联:ControlBase.h, SpinAnimator.h, MotorControl.cpp
// ============================================================================

#pragma once

#include "widgets/base/ControlBase.h"
#include "widgets/base/SpinAnimator.h"
#include <QColor>

namespace qwhmi::widgets {

class MotorControl : public ControlBase {
    Q_OBJECT
    Q_PROPERTY(QColor bodyColor READ bodyColor WRITE setBodyColor NOTIFY bodyColorChanged)
    Q_PROPERTY(QColor runColor READ runColor WRITE setRunColor NOTIFY runColorChanged)
    Q_PROPERTY(int spinPeriodMs READ spinPeriodMs WRITE setSpinPeriodMs NOTIFY spinPeriodMsChanged)

public:
    explicit MotorControl(QGraphicsItem* parent = nullptr);
    ~MotorControl() override;

    QColor bodyColor() const { return bodyColor_; }
    void   setBodyColor(const QColor& c);
    QColor runColor() const { return runColor_; }
    void   setRunColor(const QColor& c);
    int    spinPeriodMs() const { return spin_.periodMs(); }
    void   setSpinPeriodMs(int ms);

    bool   isRunning() const;
    // ★ 累计角度(不是 0~360 的环量)。画的时候用 qFmod 取模,
    //   之所以要累计是为了跨圈不回跳 —— 见 SpinAnimator.h
    qreal  spinAngle() const { return spin_.angle(); }
    // 两个版本:单测要用 applyFrame() 手动喂帧,那是 non-const 的
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
    void runningChanged();
    void writeRejected(const QString& detail);

private:
    void   updateRunning();

    QColor bodyColor_ = QColor(120, 125, 130);   // 电机壳深灰
    QColor runColor_  = QColor(0, 160, 90);       // 运行绿
    SpinAnimator spin_;
    bool   lastRunning_ = false;
};

} // namespace qwhmi::widgets
