// ============================================================================
// GroupControl 实现
// ============================================================================

#include "widgets/GroupControl.h"

#include <QFont>
#include <QPainter>

namespace qwhmi::widgets {

GroupControl::GroupControl(QGraphicsItem* parent)
    : ControlBase(parent)
{
    setSize(QSizeF(200, 150));
    registerControlType("Group", []() -> ControlBase* { return new GroupControl(); });
}

GroupControl::~GroupControl() = default;

void GroupControl::setBorderColor(const QColor& c) {
    if (c == borderColor_) return;
    borderColor_ = c;
    update();
    emit borderColorChanged();
}
void GroupControl::setTitle(const QString& t) {
    if (t == title_) return;
    title_ = t;
    update();
    emit titleChanged();
}

void GroupControl::addChild(ControlBase* c) {
    if (c && !children_.contains(c)) children_.append(c);
}

void GroupControl::removeChild(ControlBase* c) {
    children_.removeAll(c);
}

QRectF GroupControl::boundingRect() const {
    return QRectF(0, 0, size().width(), size().height());
}

void GroupControl::paintContent(QPainter* p,
                         const QStyleOptionGraphicsItem* /*option*/,
                         QWidget* /*widget*/) {
    // 浅灰半透明填充
    p->setPen(Qt::NoPen);
    p->setBrush(QColor(245, 245, 250, 180));
    p->drawRect(boundingRect());

    // 边框
    QPen pen(isSelected() ? QColor(0, 120, 215) : borderColor_);
    pen.setWidth(isSelected() ? 2 : 1);
    pen.setStyle(Qt::DashLine);
    p->setPen(pen);
    p->setBrush(Qt::NoBrush);
    p->drawRect(boundingRect());

    // title 文字(左上角)
    p->setPen(QColor(80, 80, 80));
    p->setFont(QFont("Microsoft YaHei", 9, QFont::Bold));
    p->drawText(QRectF(6, 2, 200, 16), Qt::AlignLeft, title_);
}

} // namespace qwhmi::widgets