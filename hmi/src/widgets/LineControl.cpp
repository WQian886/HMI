// ============================================================================
// LineControl 实现
// ============================================================================

#include "widgets/LineControl.h"

#include <QPainter>
#include <QPen>

namespace qwhmi::widgets {

LineControl::LineControl(QGraphicsItem* parent)
    : ControlBase(parent)
{
    setSize(QSizeF(120, 10));   // 默认宽 120, 高 10(线在中线)
    registerControlType("Line", []() -> ControlBase* { return new LineControl(); });
}

LineControl::~LineControl() = default;

void LineControl::setStrokeColor(const QColor& c) {
    if (c == strokeColor_) return;
    strokeColor_ = c;
    update();
    emit strokeColorChanged();
}
void LineControl::setStrokeWidth(int w) {
    if (w == strokeWidth_) return;
    strokeWidth_ = w;
    update();
    emit strokeWidthChanged();
}

QRectF LineControl::boundingRect() const {
    // 加 padding 让选中 handle 能点击到
    const qreal pad = strokeWidth_ + 2;
    return QRectF(-pad, -pad, size().width() + 2*pad, size().height() + 2*pad);
}

void LineControl::paintContent(QPainter* p,
                        const QStyleOptionGraphicsItem* /*option*/,
                        QWidget* /*widget*/) {
    // ★ 选中时 strokeColor 变深蓝
    QPen pen(isSelected() ? QColor(0, 120, 215) : strokeColor_);
    pen.setWidth(strokeWidth_);
    p->setPen(pen);
    p->drawLine(QPointF(0, size().height() / 2.0),
               QPointF(size().width(), size().height() / 2.0));
}

} // namespace qwhmi::widgets