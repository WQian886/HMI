// ============================================================================
// ToggleControl 实现
// ============================================================================

#include "widgets/ToggleControl.h"

#include <QFont>
#include <QGraphicsSceneMouseEvent>
#include <QPainter>

namespace qwhmi::widgets {

ToggleControl::ToggleControl(QGraphicsItem* parent)
    : ControlBase(parent)
{
    setSize(QSizeF(80, 36));
    registerControlType("Toggle", []() -> ControlBase* { return new ToggleControl(); });
}

ToggleControl::~ToggleControl() = default;

void ToggleControl::setOnText(const QString& t) {
    if (t == onText_) return;
    onText_ = t;
    update();
    emit onTextChanged();
}
void ToggleControl::setOffText(const QString& t) {
    if (t == offText_) return;
    offText_ = t;
    update();
    emit offTextChanged();
}
void ToggleControl::setOnColor(const QColor& c) {
    if (c == onColor_) return;
    onColor_ = c;
    update();
    emit onColorChanged();
}

QRectF ToggleControl::boundingRect() const {
    return QRectF(0, 0, size().width(), size().height());
}

void ToggleControl::paint(QPainter* p,
                          const QStyleOptionGraphicsItem* option,
                          QWidget* widget) {
    Q_UNUSED(option); Q_UNUSED(widget);
    const QRectF br = boundingRect();
    const bool on = value().toDouble() > 0.5;

    // 背景色(开绿/关灰)
    p->setPen(Qt::NoPen);
    p->setBrush(on ? onColor_ : QColor(120, 120, 120));
    p->drawRoundedRect(br, 8, 8);

    // 文字
    p->setPen(QColor(255, 255, 255));
    p->setFont(QFont("Microsoft YaHei", 11, QFont::Bold));
    p->drawText(br, Qt::AlignCenter, on ? onText_ : offText_);
}

void ToggleControl::mouseReleaseEvent(QGraphicsSceneMouseEvent* event) {
    if (event->button() == Qt::LeftButton && contains(event->pos())) {
        const bool nowOn = value().toDouble() < 0.5;  // 翻转
        setValue(nowOn ? 1.0 : 0.0);
        emit toggled(nowOn);
    }
    QGraphicsObject::mouseReleaseEvent(event);
}

} // namespace qwhmi::widgets