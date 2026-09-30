// ============================================================================
// 阶段 E · E.1 · GaugeControl(显示类 — 仪表盘)
//
// 文件职责:圆弧 + 指针仪表盘。value 在 [min, max] 间按比例画指针角度。
//
// 关联:ControlBase.h, GaugeControl.cpp
// ============================================================================

#pragma once

#include "widgets/base/ControlBase.h"
#include <QColor>

namespace qwhmi::widgets {

class GaugeControl : public ControlBase {
    Q_OBJECT
    Q_PROPERTY(double minimum READ minimum WRITE setMinimum NOTIFY minimumChanged)
    Q_PROPERTY(double maximum READ maximum WRITE setMaximum NOTIFY maximumChanged)
    Q_PROPERTY(QColor needleColor READ needleColor WRITE setNeedleColor NOTIFY needleColorChanged)

public:
    explicit GaugeControl(QGraphicsItem* parent = nullptr);
    ~GaugeControl() override;

    double minimum() const { return minimum_; }
    void   setMinimum(double v);
    double maximum() const { return maximum_; }
    void   setMaximum(double v);
    QColor needleColor() const { return needleColor_; }
    void   setNeedleColor(const QColor& c);

    QRectF boundingRect() const override;
    void paintContent(QPainter* painter,
                 const QStyleOptionGraphicsItem* option,
                 QWidget* widget = nullptr) override;

signals:
    void minimumChanged();
    void maximumChanged();
    void needleColorChanged();

private:
    double minimum_ = 0.0;
    double maximum_ = 100.0;
    QColor needleColor_ = QColor(220, 40, 40);
};

} // namespace qwhmi::widgets