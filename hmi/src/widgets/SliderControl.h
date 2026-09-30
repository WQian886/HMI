// ============================================================================
// 阶段 E · E.1 · SliderControl(控制 — 滑动条)
//
// 文件职责:水平滑动条(min/max + value),拖动改 value。
//   value 双向 bind(control.valueChanged + setValue)
//
// 关联:ControlBase.h
// ============================================================================

#pragma once

#include "widgets/base/ControlBase.h"
#include <QColor>

namespace qwhmi::widgets {

class SliderControl : public ControlBase {
    Q_OBJECT
    Q_PROPERTY(double minimum READ minimum WRITE setMinimum NOTIFY minimumChanged)
    Q_PROPERTY(double maximum READ maximum WRITE setMaximum NOTIFY maximumChanged)
    Q_PROPERTY(QColor fillColor READ fillColor WRITE setFillColor NOTIFY fillColorChanged)
    Q_PROPERTY(Qt::Orientation orientation READ orientation WRITE setOrientation NOTIFY orientationChanged)

public:
    explicit SliderControl(QGraphicsItem* parent = nullptr);
    ~SliderControl() override;

    double minimum() const { return minimum_; }
    void   setMinimum(double v);
    double maximum() const { return maximum_; }
    void   setMaximum(double v);
    QColor fillColor() const { return fillColor_; }
    void   setFillColor(const QColor& c);
    Qt::Orientation orientation() const { return orientation_; }
    void   setOrientation(Qt::Orientation o);

    QRectF boundingRect() const override;
    void paintContent(QPainter* painter,
                 const QStyleOptionGraphicsItem* option,
                 QWidget* widget = nullptr) override;
    void   mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void   mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;
    void   mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;

signals:
    void minimumChanged();
    void maximumChanged();
    void fillColorChanged();
    void orientationChanged();

private:
    // 由点击位置 → value(0~1 ratio)
    double posToRatio(const QPointF& pos) const;
    void   updateValueFromPos(const QPointF& pos);

    double minimum_ = 0.0;
    double maximum_ = 100.0;
    QColor fillColor_ = QColor(0, 120, 215);
    Qt::Orientation orientation_ = Qt::Horizontal;
    bool dragging_ = false;
};

} // namespace qwhmi::widgets