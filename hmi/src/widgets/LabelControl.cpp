// ============================================================================
// 阶段 E · E.1 · LabelControl 实现
// 对应 LabelControl.h
// ============================================================================

#include "widgets/LabelControl.h"

#include <QFont>
#include <QPainter>
#include <QPen>

namespace qwhmi::widgets {

LabelControl::LabelControl(QGraphicsItem* parent)
    : ControlBase(parent)
{
    // ★ E.4 调色板注册("Label" 类型字符串 → new LabelControl)
    registerControlType("Label", []() -> ControlBase* { return new LabelControl(); });
}

LabelControl::~LabelControl() = default;

QRectF LabelControl::boundingRect() const {
    // ★ 默认 120x40(基类 size() 默认值)
    return QRectF(0, 0, size().width(), size().height());
}

void LabelControl::paint(QPainter* p,
                         const QStyleOptionGraphicsItem* /*option*/,
                         QWidget* /*widget*/) {
    // ★ 边框(选中加深蓝)
    QPen pen(isSelected() ? QColor(0, 120, 215) : QColor(160, 160, 160));
    pen.setWidth(isSelected() ? 2 : 1);
    p->setPen(pen);
    p->drawRect(boundingRect());

    // ★ 文字(默认字体)
    p->setPen(QColor(32, 32, 32));
    p->setFont(QFont("Microsoft YaHei", 10));
    p->drawText(boundingRect().adjusted(4, 0, -4, 0),
                Qt::AlignVCenter | Qt::AlignLeft,
                displayText());
}

} // namespace qwhmi::widgets