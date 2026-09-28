// ============================================================================
// 阶段 E · E.1c · TankControl(显示类 — 储罐 / 液位)
//
// 文件职责:竖立罐体 + 底部液位填充 + 刻度线。value 按 [minimum, maximum]
//   比例算出液位高度。跟 GaugeControl 的弧形不同,这个是【量筒】语义 ——
//   现场的水池/油罐/料仓都长这样,一眼能看出"还剩多少"。
//
// 【跟 GaugeControl 一样的比例算法】两者共用
//   range = maximum - minimum
//   ratio = qBound(0.0, (value - minimum) / range, 1.0)
//   照抄 GaugeControl.cpp:69-72,保证两个控件对同一变量显示同一个数。
//
// 【不做写回】储罐是只读的显示件。液位来自 PLC 的液位计,操作员改不了。
//   真要人工干预得走 InputEditControl 写 setpoint,那是另一回事。
//
// 关联:ControlBase.h, TankControl.cpp
// ============================================================================

#pragma once

#include "widgets/base/ControlBase.h"
#include <QColor>

namespace qwhmi::widgets {

class TankControl : public ControlBase {
    Q_OBJECT
    Q_PROPERTY(double minimum READ minimum WRITE setMinimum NOTIFY minimumChanged)
    Q_PROPERTY(double maximum READ maximum WRITE setMaximum NOTIFY maximumChanged)
    Q_PROPERTY(QColor tankColor READ tankColor WRITE setTankColor NOTIFY tankColorChanged)
    Q_PROPERTY(QColor liquidColor READ liquidColor WRITE setLiquidColor NOTIFY liquidColorChanged)
    // ★ 刻度条数。0 = 不画刻度(纯色块)。默认 4 档,现场量筒一般 25%/50%/75%/满
    Q_PROPERTY(int tickCount READ tickCount WRITE setTickCount NOTIFY tickCountChanged)

public:
    explicit TankControl(QGraphicsItem* parent = nullptr);
    ~TankControl() override;

    double minimum() const { return minimum_; }
    void   setMinimum(double v);
    double maximum() const { return maximum_; }
    void   setMaximum(double v);
    QColor tankColor() const { return tankColor_; }
    void   setTankColor(const QColor& c);
    QColor liquidColor() const { return liquidColor_; }
    void   setLiquidColor(const QColor& c);
    int    tickCount() const { return tickCount_; }
    void   setTickCount(int n);

    // ★ 当前液位比例 0~1。抽成公有是因为派生/测试都要用,
    //   免得 paint 之外的地方各写一份算比例的代码(迟早写出不一致的两份)
    double fillRatio() const;

    QRectF boundingRect() const override;
    void   paint(QPainter* painter,
                 const QStyleOptionGraphicsItem* option,
                 QWidget* widget = nullptr) override;

signals:
    void minimumChanged();
    void maximumChanged();
    void tankColorChanged();
    void liquidColorChanged();
    void tickCountChanged();

private:
    double minimum_ = 0.0;
    double maximum_ = 100.0;
    QColor tankColor_   = QColor(225, 225, 230);   // 罐体浅灰(不锈钢)
    QColor liquidColor_ = QColor(0, 140, 220);     // 液体蓝
    int    tickCount_   = 4;
};

} // namespace qwhmi::widgets
