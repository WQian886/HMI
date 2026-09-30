// ============================================================================
// 阶段 E · E.1c · TankControl 实现
// 对应 TankControl.h
// ============================================================================

#include "widgets/TankControl.h"

#include <QFont>
#include <QPainter>
#include <QPen>

namespace qwhmi::widgets {

TankControl::TankControl(QGraphicsItem* parent)
    : ControlBase(parent)
{
    // ★ 默认 100x160 —— 储罐要瘦高,正方形看着像箱子不像罐
    setSize(QSizeF(100, 160));
    registerControlType("Tank", []() -> ControlBase* { return new TankControl(); });
}

TankControl::~TankControl() = default;

void TankControl::setMinimum(double v) {
    if (qFuzzyCompare(v, minimum_)) return;
    minimum_ = v;
    update();
    emit minimumChanged();
}
void TankControl::setMaximum(double v) {
    if (qFuzzyCompare(v, maximum_)) return;
    maximum_ = v;
    update();
    emit maximumChanged();
}
void TankControl::setTankColor(const QColor& c) {
    if (c == tankColor_) return;
    tankColor_ = c;
    update();
    emit tankColorChanged();
}
void TankControl::setLiquidColor(const QColor& c) {
    if (c == liquidColor_) return;
    liquidColor_ = c;
    update();
    emit liquidColorChanged();
}
void TankControl::setTickCount(int n) {
    // ★ 负数会让后面的 for 循环边界反了,这里直接夹到 0
    if (n < 0) n = 0;
    if (n == tickCount_) return;
    tickCount_ = n;
    update();
    emit tickCountChanged();
}

// ★ 比例算法跟 GaugeControl.cpp:69-72 同款,保证同一变量在两个控件上
//   显示同一个数。range <= 0 时返回 0 而不是除零/负比例 —— 现场有人
//   会把 max 配成 0(还没标定完就上线),这时显示空罐而不是画一堆乱线。
double TankControl::fillRatio() const {
    const double range = maximum_ - minimum_;
    if (range <= 0) return 0.0;
    return qBound(0.0, (value().toDouble() - minimum_) / range, 1.0);
}

QRectF TankControl::boundingRect() const {
    return QRectF(0, 0, size().width(), size().height());
}

void TankControl::paintContent(QPainter* p,
                        const QStyleOptionGraphicsItem* /*option*/,
                        QWidget* /*widget*/) {
    p->setRenderHint(QPainter::Antialiasing);

    const QRectF br = boundingRect();
    const double ratio = fillRatio();

    // ★ 留 3px 边距画内腔,外圈留给描边 —— 否则描边有一半在 boundingRect 外
    //   被裁掉,选中时框会显得比控件小一圈
    const QRectF inner = br.adjusted(3, 3, -3, -3);
    const qreal radius = 8;

    // ① 罐体内腔(空的部分)
    p->setPen(Qt::NoPen);
    p->setBrush(tankColor_);
    p->drawRoundedRect(inner, radius, radius);

    // ② 液体,从底部往上填 —— 现场量筒都是这个方向(下面多)
    if (ratio > 0.0) {
        const qreal fullH = inner.height();
        const qreal h = fullH * ratio;
        QRectF liquid(inner.x(), inner.bottom() - h + 1, inner.width(), h);
        p->setBrush(liquidColor_);
        p->drawRect(liquid);
        // 液面高光:一条浅色横线,让"液面在哪"一眼可见,
        // 不然深色液体贴在深色罐壁上会看不出边界
        p->setPen(QPen(QColor(255, 255, 255, 160), 1));
        p->drawLine(QPointF(inner.left(), liquid.top()), QPointF(inner.right(), liquid.top()));
    }

    // ③ 刻度线(等分)。画在液体上面,否则满罐时看不见刻度
    if (tickCount_ > 0) {
        QPen tickPen(isSelected() ? QColor(0, 120, 215) : QColor(140, 140, 145));
        tickPen.setWidth(1);
        p->setPen(tickPen);
        for (int i = 1; i < tickCount_; ++i) {
            const qreal y = inner.top() + inner.height() * i / tickCount_;
            p->drawLine(QPointF(inner.left(), y), QPointF(inner.right(), y));
        }
    }

    // ④ 外框。选中加深蓝 2px(全项目统一色 QColor(0,120,215),
    //   同 RectangleControl.cpp / GaugeControl.cpp)
    QPen border(isSelected() ? QColor(0, 120, 215) : QColor(120, 120, 125));
    border.setWidth(isSelected() ? 2 : 1);
    p->setPen(border);
    p->setBrush(Qt::NoBrush);
    p->drawRoundedRect(inner.adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);

    // ⑤ 数值标签。画在框外下方,需要时父项 clipping 兜住
    p->setPen(QColor(40, 40, 40));
    p->setFont(QFont("Consolas", 9));
    p->drawText(QRectF(br.left(), br.bottom() - 14, br.width(), 14),
                Qt::AlignCenter, displayText());
}

} // namespace qwhmi::widgets
