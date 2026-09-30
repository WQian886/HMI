// ============================================================================
// 阶段 E · E.1 · LedControl 实现
// 对应 LedControl.h
// ============================================================================

#include "widgets/LedControl.h"

#include <QPainter>
#include <QRadialGradient>

namespace qwhmi::widgets {

LedControl::LedControl(QGraphicsItem* parent)
    : ControlBase(parent)
{
    setSize(QSizeF(32, 32));   // 小圆点
    registerControlType("LED", []() -> ControlBase* { return new LedControl(); });
}

LedControl::~LedControl() = default;

void LedControl::setOnColor(const QColor& c) {
    if (c == onColor_) return;
    onColor_ = c;
    update();
    emit onColorChanged();
}
void LedControl::setOffColor(const QColor& c) {
    if (c == offColor_) return;
    offColor_ = c;
    update();
    emit offColorChanged();
}

QRectF LedControl::boundingRect() const {
    return QRectF(0, 0, size().width(), size().height());
}

void LedControl::paintContent(QPainter* p,
                       const QStyleOptionGraphicsItem* /*option*/,
                       QWidget* /*widget*/) {
    p->setRenderHint(QPainter::Antialiasing);

    const QRectF br = boundingRect();
    const QPointF center = br.center();
    const qreal radius = qMin(br.width(), br.height()) / 2.0 - 4;

    // ★ 外圈(选中加边框)
    if (isSelected()) {
        QPen pen(QColor(0, 120, 215));
        pen.setWidth(2);
        p->setPen(pen);
        p->setBrush(Qt::NoBrush);
        p->drawEllipse(center, radius + 2, radius + 2);
    }

    // ★ 灯本体(亮 = onColor 带高光,暗 = offColor 平)
    const QColor bodyColor = isLit() ? onColor_ : offColor_;
    p->setPen(Qt::NoPen);

    // ★ 高光效果(亮时用 QRadialGradient 模拟立体感)
    if (isLit()) {
        QRadialGradient grad(center, radius);
        grad.setColorAt(0.0, bodyColor.lighter(150));
        grad.setColorAt(0.7, bodyColor);
        grad.setColorAt(1.0, bodyColor.darker(150));
        p->setBrush(QBrush(grad));
    } else {
        p->setBrush(bodyColor);
    }
    p->drawEllipse(center, radius, radius);
}

} // namespace qwhmi::widgets