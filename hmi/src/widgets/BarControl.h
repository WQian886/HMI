// ============================================================================
// 阶段 E · E.1 · BarControl(显示类 — 进度条)
//
// 文件职责:水平进度条。value 在 [min, max] 间按比例填充。
//
// 关联:ControlBase.h, BarControl.cpp
// ============================================================================

#pragma once

#include "widgets/base/ControlBase.h"

namespace qwhmi::widgets {

class BarControl : public ControlBase {
    Q_OBJECT
    Q_PROPERTY(double minimum READ minimum WRITE setMinimum NOTIFY minimumChanged)
    Q_PROPERTY(double maximum READ maximum WRITE setMaximum NOTIFY maximumChanged)
    Q_PROPERTY(QColor fillColor READ fillColor WRITE setFillColor NOTIFY fillColorChanged)
    Q_PROPERTY(Qt::Orientation orientation READ orientation WRITE setOrientation NOTIFY orientationChanged)

public:
    explicit BarControl(QGraphicsItem* parent = nullptr);
    ~BarControl() override;

    double minimum() const { return minimum_; }
    void   setMinimum(double v);
    double maximum() const { return maximum_; }
    void   setMaximum(double v);
    QColor fillColor() const { return fillColor_; }
    void   setFillColor(const QColor& c);
    Qt::Orientation orientation() const { return orientation_; }
    void   setOrientation(Qt::Orientation o);

    QRectF boundingRect() const override;
    void   paint(QPainter* painter,
                 const QStyleOptionGraphicsItem* option,
                 QWidget* widget = nullptr) override;

signals:
    void minimumChanged();
    void maximumChanged();
    void fillColorChanged();
    void orientationChanged();

private:
    double minimum_ = 0.0;
    double maximum_ = 100.0;
    QColor fillColor_ = QColor(0, 120, 215);
    Qt::Orientation orientation_ = Qt::Horizontal;
};

} // namespace qwhmi::widgets