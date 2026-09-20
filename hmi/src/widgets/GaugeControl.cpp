// ============================================================================
// 阶段 E · E.1 · GaugeControl 实现
// 对应 GaugeControl.h
// ============================================================================

#include "widgets/GaugeControl.h"

#include <QFont>
#include <QPainter>
#include <QPainterPath>
#include <QPen>

namespace qwhmi::widgets {

GaugeControl::GaugeControl(QGraphicsItem* parent)
    : ControlBase(parent)
{
    setSize(QSizeF(140, 140));   // 默认正方形
    registerControlType("Gauge", []() -> ControlBase* { return new GaugeControl(); });
}

GaugeControl::~GaugeControl() = default;

void GaugeControl::setMinimum(double v) {
    if (qFuzzyCompare(v, minimum_)) return;
    minimum_ = v;
    update();
    emit minimumChanged();
}
void GaugeControl::setMaximum(double v) {
    if (qFuzzyCompare(v, maximum_)) return;
    maximum_ = v;
    update();
    emit maximumChanged();
}
void GaugeControl::setNeedleColor(const QColor& c) {
    if (c == needleColor_) return;
    needleColor_ = c;
    update();
    emit needleColorChanged();
}

QRectF GaugeControl::boundingRect() const {
    return QRectF(0, 0, size().width(), size().height());
}

void GaugeControl::paint(QPainter* p,
                         const QStyleOptionGraphicsItem* /*option*/,
                         QWidget* /*widget*/) {
    p->setRenderHint(QPainter::Antialiasing);

    const QRectF br = boundingRect();
    const QPointF center = br.center();
    const qreal radius = qMin(br.width(), br.height()) / 2.0 - 6;

    // ★ 外圈(选中加深蓝)
    QPen ringPen(isSelected() ? QColor(0, 120, 215) : QColor(160, 160, 160));
    ringPen.setWidth(isSelected() ? 2 : 1);
    p->setPen(ringPen);
    p->setBrush(Qt::NoBrush);
    p->drawEllipse(center, radius, radius);

    // ★ 弧线背景(浅灰,135°~45°,即从左下到右下,共 270°)
    p->setPen(QPen(QColor(200, 200, 200), 4));
    QRectF arcRect(center.x() - radius, center.y() - radius, radius * 2, radius * 2);
    p->drawArc(arcRect, 135 * 16, 270 * 16);

    // ★ 弧线填充(value 比例 + 红/绿/蓝按区间)
    const double range = maximum_ - minimum_;
    const double ratio = range > 0
        ? qBound(0.0, (value().toDouble() - minimum_) / range, 1.0)
        : 0.0;
    const int spanAngle = static_cast<int>(270 * 16 * ratio);
    p->setPen(QPen(QColor(0, 160, 220), 6));
    p->drawArc(arcRect, 135 * 16, spanAngle);

    // ★ 指针(从中心到圆弧上对应角度的点)
    const qreal startDeg = 135.0;
    const qreal needleDeg = startDeg + 270.0 * ratio;
    const qreal needleRad = needleDeg * M_PI / 180.0;
    const QPointF tip(center.x() + radius * 0.85 * qCos(needleRad),
                      center.y() + radius * 0.85 * qSin(needleRad));
    p->setPen(QPen(needleColor_, 2));
    p->drawLine(center, tip);

    // ★ 中心圆
    p->setPen(Qt::NoPen);
    p->setBrush(needleColor_);
    p->drawEllipse(center, 4, 4);

    // ★ 中心值(数字)
    p->setPen(QColor(32, 32, 32));
    p->setFont(QFont("Consolas", 10, QFont::Bold));
    p->drawText(QRectF(center.x() - 30, center.y() + 8, 60, 20),
                Qt::AlignCenter,
                displayText());
}

} // namespace qwhmi::widgets