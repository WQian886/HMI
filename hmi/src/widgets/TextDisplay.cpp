// ============================================================================
// 阶段 E · E.1 · TextDisplay 实现
// 对应 TextDisplay.h
// ============================================================================

#include "widgets/TextDisplay.h"

#include <QFont>
#include <QPainter>
#include <QPen>

namespace qwhmi::widgets {

TextDisplay::TextDisplay(QGraphicsItem* parent)
    : ControlBase(parent)
{
    setSize(QSizeF(160, 32));   // 默认稍宽 + 矮一点
    registerControlType("TextDisplay", []() -> ControlBase* { return new TextDisplay(); });
}

TextDisplay::~TextDisplay() = default;

void TextDisplay::setPrefix(const QString& p) {
    if (p == prefix_) return;
    prefix_ = p;
    update();
    emit prefixChanged();
}

void TextDisplay::setSuffix(const QString& s) {
    if (s == suffix_) return;
    suffix_ = s;
    update();
    emit suffixChanged();
}

QString TextDisplay::displayText() const {
    // ★ "prefix + value + suffix" — 例:"速度: 123.50 rpm"
    return prefix_ + value().toString() + suffix_;
}

QRectF TextDisplay::boundingRect() const {
    return QRectF(0, 0, size().width(), size().height());
}

void TextDisplay::paint(QPainter* p,
                        const QStyleOptionGraphicsItem* /*option*/,
                        QWidget* /*widget*/) {
    QPen pen(isSelected() ? QColor(0, 120, 215) : QColor(160, 160, 160));
    pen.setWidth(isSelected() ? 2 : 1);
    p->setPen(pen);
    p->drawRect(boundingRect());

    p->setPen(QColor(32, 32, 32));
    p->setFont(QFont("Microsoft YaHei", 10));
    p->drawText(boundingRect().adjusted(6, 0, -6, 0),
                Qt::AlignVCenter | Qt::AlignLeft,
                displayText());
}

} // namespace qwhmi::widgets