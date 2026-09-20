// ============================================================================
// 阶段 E · E.1 · BarControl 实现
// 对应 BarControl.h
// ============================================================================

#include "widgets/BarControl.h"

#include <QPainter>
#include <QPen>

namespace qwhmi::widgets {

BarControl::BarControl(QGraphicsItem* parent)
    : ControlBase(parent)
{
    setSize(QSizeF(160, 24));   // 默认稍长 + 矮(进度条比例)
    registerControlType("Bar", []() -> ControlBase* { return new BarControl(); });
}

BarControl::~BarControl() = default;

void BarControl::setMinimum(double v) {
    if (qFuzzyCompare(v, minimum_)) return;
    minimum_ = v;
    update();
    emit minimumChanged();
}
void BarControl::setMaximum(double v) {
    if (qFuzzyCompare(v, maximum_)) return;
    maximum_ = v;
    update();
    emit maximumChanged();
}
void BarControl::setFillColor(const QColor& c) {
    if (c == fillColor_) return;
    fillColor_ = c;
    update();
    emit fillColorChanged();
}
void BarControl::setOrientation(Qt::Orientation o) {
    if (o == orientation_) return;
    orientation_ = o;
    update();
    emit orientationChanged();
}

QRectF BarControl::boundingRect() const {
    return QRectF(0, 0, size().width(), size().height());
}

void BarControl::paint(QPainter* p,
                       const QStyleOptionGraphicsItem* /*option*/,
                       QWidget* /*widget*/) {
    const QRectF br = boundingRect();

    // 边框(选中加深蓝)
    QPen pen(isSelected() ? QColor(0, 120, 215) : QColor(160, 160, 160));
    pen.setWidth(isSelected() ? 2 : 1);
    p->setPen(pen);
    p->setBrush(Qt::NoBrush);
    p->drawRect(br);

    // ★ 计算填充比例(value 在 [min, max])
    const double range = maximum_ - minimum_;
    const double ratio = range > 0
        ? qBound(0.0, (value().toDouble() - minimum_) / range, 1.0)
        : 0.0;

    // ★ 填充矩形
    p->setPen(Qt::NoPen);
    p->setBrush(fillColor_);
    if (orientation_ == Qt::Horizontal) {
        const qreal fillW = br.width() * ratio;
        p->drawRect(QRectF(br.x(), br.y(), fillW, br.height()));
    } else {
        // Vertical 模式:从下往上填充
        const qreal fillH = br.height() * ratio;
        p->drawRect(QRectF(br.x(), br.bottom() - fillH, br.width(), fillH));
    }
}

} // namespace qwhmi::widgets