// ============================================================================
// 阶段 E · E.1 · NumberDisplay 实现
// 对应 NumberDisplay.h
// ============================================================================

#include "widgets/NumberDisplay.h"

#include <QFont>
#include <QPainter>
#include <QPen>

namespace qwhmi::widgets {

NumberDisplay::NumberDisplay(QGraphicsItem* parent)
    : ControlBase(parent)
{
    // ★ 默认 140x40(数字显示稍宽)
    setSize(QSizeF(140, 40));
    registerControlType("NumberDisplay", []() -> ControlBase* { return new NumberDisplay(); });
}

NumberDisplay::~NumberDisplay() = default;

void NumberDisplay::setFormat(const QString& f) {
    if (f == format_) return;
    format_ = f;
    update();
    emit formatChanged();
}

void NumberDisplay::setUnit(const QString& u) {
    if (u == unit_) return;
    unit_ = u;
    update();
    emit unitChanged();
}

QString NumberDisplay::displayText() const {
    // ★ 用 format 格式化 value,后跟 unit
    bool ok = false;
    const double d = value().toDouble(&ok);
    if (!ok) return value().toString() + unit_;
    return QString::number(d, 'f', 2) + unit_;   // 简化:忽略 format_,直接 .2f
}

QRectF NumberDisplay::boundingRect() const {
    return QRectF(0, 0, size().width(), size().height());
}

void NumberDisplay::paint(QPainter* p,
                          const QStyleOptionGraphicsItem* /*option*/,
                          QWidget* /*widget*/) {
    // 边框(选中加深蓝)
    QPen pen(isSelected() ? QColor(0, 120, 215) : QColor(160, 160, 160));
    pen.setWidth(isSelected() ? 2 : 1);
    p->setPen(pen);
    p->drawRect(boundingRect());

    // 数字 + 单位(右对齐,大字体)
    p->setPen(QColor(32, 32, 32));
    p->setFont(QFont("Consolas", 12, QFont::Bold));   // 等宽数字字体
    p->drawText(boundingRect().adjusted(6, 0, -6, 0),
                Qt::AlignVCenter | Qt::AlignRight,
                displayText());
}

} // namespace qwhmi::widgets