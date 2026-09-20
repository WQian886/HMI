// ============================================================================
// ButtonControl 实现
// ============================================================================

#include "widgets/ButtonControl.h"

#include <QFont>
#include <QGraphicsSceneMouseEvent>
#include <QPainter>
#include <QPen>

namespace qwhmi::widgets {

ButtonControl::ButtonControl(QGraphicsItem* parent)
    : ControlBase(parent)
{
    setSize(QSizeF(100, 36));
    setFlag(QGraphicsItem::ItemIsMovable, true);
    registerControlType("Button", []() -> ControlBase* { return new ButtonControl(); });
}

ButtonControl::~ButtonControl() = default;

void ButtonControl::setText(const QString& t) {
    if (t == text_) return;
    text_ = t;
    update();
    emit textChanged();
}
void ButtonControl::setBackground(const QColor& c) {
    if (c == background_) return;
    background_ = c;
    update();
    emit backgroundChanged();
}

QRectF ButtonControl::boundingRect() const {
    return QRectF(0, 0, size().width(), size().height());
}

void ButtonControl::paint(QPainter* p,
                          const QStyleOptionGraphicsItem* option,
                          QWidget* widget) {
    Q_UNUSED(option); Q_UNUSED(widget);
    const QRectF br = boundingRect();

    // 按下态变暗
    QColor fill = pressed_ ? background_.darker(130) : background_;
    if (isSelected()) fill = fill.lighter(110);

    // 圆角矩形按钮
    p->setPen(Qt::NoPen);
    p->setBrush(fill);
    p->drawRoundedRect(br, 6, 6);

    // 文字
    p->setPen(QColor(255, 255, 255));
    p->setFont(QFont("Microsoft YaHei", 10, QFont::Bold));
    p->drawText(br, Qt::AlignCenter, text_);
}

void ButtonControl::mousePressEvent(QGraphicsSceneMouseEvent* event) {
    pressed_ = true;
    update();
    QGraphicsObject::mousePressEvent(event);
}

void ButtonControl::mouseReleaseEvent(QGraphicsSceneMouseEvent* event) {
    pressed_ = false;
    update();
    if (event->button() == Qt::LeftButton && contains(event->pos())) {
        emit clicked();
    }
    QGraphicsObject::mouseReleaseEvent(event);
}

} // namespace qwhmi::widgets