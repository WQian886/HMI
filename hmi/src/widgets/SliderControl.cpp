// ============================================================================
// SliderControl 实现
// ============================================================================

#include "widgets/SliderControl.h"

#include <QGraphicsSceneMouseEvent>
#include <QPainter>

namespace qwhmi::widgets {

SliderControl::SliderControl(QGraphicsItem* parent)
    : ControlBase(parent)
{
    setSize(QSizeF(200, 24));
    registerControlType("Slider", []() -> ControlBase* { return new SliderControl(); });
}

SliderControl::~SliderControl() = default;

void SliderControl::setMinimum(double v) {
    if (qFuzzyCompare(v, minimum_)) return;
    minimum_ = v;
    update();
    emit minimumChanged();
}
void SliderControl::setMaximum(double v) {
    if (qFuzzyCompare(v, maximum_)) return;
    maximum_ = v;
    update();
    emit maximumChanged();
}
void SliderControl::setFillColor(const QColor& c) {
    if (c == fillColor_) return;
    fillColor_ = c;
    update();
    emit fillColorChanged();
}
void SliderControl::setOrientation(Qt::Orientation o) {
    if (o == orientation_) return;
    orientation_ = o;
    update();
    emit orientationChanged();
}

QRectF SliderControl::boundingRect() const {
    return QRectF(0, 0, size().width(), size().height());
}

double SliderControl::posToRatio(const QPointF& pos) const {
    const QRectF br = boundingRect();
    if (orientation_ == Qt::Horizontal) {
        const double t = (pos.x() - br.x()) / br.width();
        return qBound(0.0, t, 1.0);
    } else {
        const double t = (br.bottom() - pos.y()) / br.height();
        return qBound(0.0, t, 1.0);
    }
}

void SliderControl::updateValueFromPos(const QPointF& pos) {
    const double r = posToRatio(pos);
    const double range = maximum_ - minimum_;
    setValue(minimum_ + range * r);
}

void SliderControl::paint(QPainter* p,
                          const QStyleOptionGraphicsItem* option,
                          QWidget* widget) {
    Q_UNUSED(option); Q_UNUSED(widget);
    const QRectF br = boundingRect();

    // 槽(浅灰底)
    p->setPen(Qt::NoPen);
    p->setBrush(QColor(220, 220, 220));
    if (orientation_ == Qt::Horizontal) {
        const qreal slotH = 6.0;
        p->drawRect(QRectF(br.x(), br.center().y() - slotH/2, br.width(), slotH));
    } else {
        const qreal slotW = 6.0;
        p->drawRect(QRectF(br.center().x() - slotW/2, br.y(), slotW, br.height()));
    }

    // 填充
    const double r = posToRatio(QPointF(orientation_ == Qt::Horizontal
                                       ? br.x() + br.width() * (value().toDouble() - minimum_) / (maximum_ - minimum_)
                                       : 0,
                                       orientation_ == Qt::Vertical
                                       ? br.bottom() - br.height() * (value().toDouble() - minimum_) / (maximum_ - minimum_)
                                       : 0));
    p->setBrush(fillColor_);
    if (orientation_ == Qt::Horizontal) {
        const qreal fillX = br.width() * (value().toDouble() - minimum_) / qMax(maximum_ - minimum_, 0.001);
        p->drawRect(QRectF(br.x(), br.center().y() - 3, fillX, 6));
    } else {
        const qreal fillH = br.height() * (value().toDouble() - minimum_) / qMax(maximum_ - minimum_, 0.001);
        p->drawRect(QRectF(br.center().x() - 3, br.bottom() - fillH, 6, fillH));
    }

    // 圆点 thumb(简化:不画手柄)
}

void SliderControl::mousePressEvent(QGraphicsSceneMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        dragging_ = true;
        updateValueFromPos(event->pos());
    }
    QGraphicsObject::mousePressEvent(event);
}
void SliderControl::mouseMoveEvent(QGraphicsSceneMouseEvent* event) {
    if (dragging_) updateValueFromPos(event->pos());
    QGraphicsObject::mouseMoveEvent(event);
}
void SliderControl::mouseReleaseEvent(QGraphicsSceneMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        dragging_ = false;
        updateValueFromPos(event->pos());
    }
    QGraphicsObject::mouseReleaseEvent(event);
}

} // namespace qwhmi::widgets