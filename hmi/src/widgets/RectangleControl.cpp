// ============================================================================
// RectangleControl 实现
// ============================================================================

#include "widgets/RectangleControl.h"

#include <QPainter>
#include <QPen>

namespace qwhmi::widgets {

RectangleControl::RectangleControl(QGraphicsItem* parent)
    : ControlBase(parent)
{
    setSize(QSizeF(100, 20));    // default 100x20
    registerControlType("Rectangle", []() -> ControlBase* { return new RectangleControl(); });
}

RectangleControl::~RectangleControl() = default;

void RectangleControl::setFillColor(const QColor& c) {
    if (c == fillColor_) return;
    fillColor_ = c;
    update();
    emit fillColorChanged();
}
void RectangleControl::setStrokeColor(const QColor& c) {
    if (c == strokeColor_) return;
    strokeColor_ = c;
    update();
    emit strokeColorChanged();
}
void RectangleControl::setStrokeWidth(int w) {
    if (w == strokeWidth_) return;
    strokeWidth_ = w;
    update();
    emit strokeWidthChanged();
}

QRectF RectangleControl::boundingRect() const {
    return QRectF(0, 0, size().width(), size().height());
}

void RectangleControl::paintContent(QPainter* p,
                             const QStyleOptionGraphicsItem* /*option*/,
                             QWidget* /*widget*/) {
    // 填充
    p->setPen(Qt::NoPen);
    p->setBrush(fillColor_);
    p->drawRect(boundingRect());

    // 边框(选中时 strokeColor 变深蓝)
    QPen pen(isSelected() ? QColor(0, 120, 215) : strokeColor_);
    pen.setWidth(isSelected() ? strokeWidth_ + 1 : strokeWidth_);
    p->setPen(pen);
    p->setBrush(Qt::NoBrush);
    p->drawRect(boundingRect());
}

} // namespace qwhmi::widgets