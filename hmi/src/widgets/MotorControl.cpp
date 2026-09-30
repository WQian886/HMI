// ============================================================================
// 阶段 E · E.1c · MotorControl 实现
// 对应 MotorControl.h
// ============================================================================

#include "widgets/MotorControl.h"

#include <QGraphicsSceneMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>

namespace qwhmi::widgets {

MotorControl::MotorControl(QGraphicsItem* parent)
    : ControlBase(parent)
    , spin_(this)
{
    setSize(QSizeF(80, 80));
    registerControlType("Motor", []() -> ControlBase* { return new MotorControl(); });

    connect(&spin_, &SpinAnimator::angleChanged, this, [this](qreal) { update(); });

    connect(this, &ControlBase::valueChanged, this, [this] {
        const bool nowRunning = isRunning();
        if (nowRunning != lastRunning_) {
            lastRunning_ = nowRunning;
            updateRunning();
            emit runningChanged();
        }
    });
}

MotorControl::~MotorControl() = default;

void MotorControl::setBodyColor(const QColor& c) {
    if (c == bodyColor_) return;
    bodyColor_ = c;
    update();
    emit bodyColorChanged();
}
void MotorControl::setRunColor(const QColor& c) {
    if (c == runColor_) return;
    runColor_ = c;
    update();
    emit runColorChanged();
}
void MotorControl::setSpinPeriodMs(int ms) {
    const int before = spin_.periodMs();
    spin_.setPeriodMs(ms);
    if (spin_.periodMs() == before) return;
    update();
    emit spinPeriodMsChanged();
}

bool MotorControl::isRunning() const {
    return value().toDouble() >= 0.5;
}

// ★ 同 PumpControl:只在【已绑定真实变量 且 运行中】时转
void MotorControl::updateRunning() {
    spin_.setRunning(boundVariable() != nullptr && isRunning());
}

QRectF MotorControl::boundingRect() const {
    return QRectF(0, 0, size().width(), size().height());
}

void MotorControl::paintContent(QPainter* p,
                         const QStyleOptionGraphicsItem* /*option*/,
                         QWidget* /*widget*/) {
    p->setRenderHint(QPainter::Antialiasing);

    const QRectF br = boundingRect();
    const bool running = isRunning();
    const qreal radius = qMin(br.width(), br.height()) / 2.0 - 5;
    const QPointF c = br.center();

    // ① 机壳
    p->setPen(Qt::NoPen);
    p->setBrush(running ? bodyColor_ : bodyColor_.darker(115));
    p->drawEllipse(c, radius, radius);

    // ② 旋转的通电标记(弧 + 箭头)。这是 IEC 的电机符号:圆里画一个
    //   表示旋转的箭头,跟 M 字配合 —— M 是绕组,箭头是转子
    if (running) {
        p->setPen(QPen(runColor_, 2.5));
        p->setBrush(Qt::NoBrush);
        // 画 3/4 圈弧(留 1/4 缺口放箭头)
        const int startDeg = 40 * 16;
        const int spanDeg  = 270 * 16;
        p->drawArc(QRectF(c.x() - radius * 0.72, c.y() - radius * 0.72,
                          radius * 1.44, radius * 1.44),
                   startDeg, spanDeg);
        // 弧末端箭头(切线方向)
        const qreal endRad = (40 + 270) * M_PI / 180.0;
        const QPointF tip(c.x() + radius * 0.72 * cos(endRad),
                          c.y() + radius * 0.72 * sin(endRad));
        const QPointF tangent(-sin(endRad), cos(endRad));
        const QPointF normal(-tangent.y(), tangent.x());
        const qreal as = radius * 0.20;
        p->setBrush(runColor_);
        p->setPen(Qt::NoPen);
        p->drawPolygon(QPolygonF({tip,
                                  tip - tangent * as * 1.6 + normal * as * 0.7,
                                  tip - tangent * as * 1.6 - normal * as * 0.7}));
    }

    // ③ M 字(定子绕组)。★ 画在旋转箭头【之下】:M 是不动的部分,
    //   箭头才是动的。画反了就变成"壳在转"了
    p->setPen(QPen(isSelected() ? QColor(0, 120, 215) : QColor(240, 240, 240),
                   qMax(2.0, radius * 0.12)));
    const qreal mw = radius * 0.52;
    const qreal mh = radius * 0.40;
    const qreal mx = c.x() - mw / 2;
    const qreal my = c.y() - mh / 2;
    QPainterPath m;
    m.moveTo(mx,              my + mh);
    m.lineTo(mx + mw / 4,     my);
    m.lineTo(mx + mw / 2,     my + mh);
    m.lineTo(mx + mw * 3 / 4, my);
    m.lineTo(mx + mw,         my + mh);
    p->drawPath(m);

    // ④ 中心轴
    p->setPen(Qt::NoPen);
    p->setBrush(bodyColor_.darker(140));
    p->drawEllipse(c, radius * 0.12, radius * 0.12);

    // ⑤ 描边 / 选中态
    QPen border(isSelected() ? QColor(0, 120, 215) : QColor(85, 90, 95));
    border.setWidth(isSelected() ? 2 : 1);
    p->setPen(border);
    p->setBrush(Qt::NoBrush);
    p->drawEllipse(c, radius, radius);

    // ⑥ 运行文字冗余(同泵)
    p->setPen(running ? runColor_ : QColor(120, 120, 125));
    p->drawText(QRectF(br.left(), br.bottom() - 14, br.width(), 13),
                Qt::AlignCenter, running ? QStringLiteral("运行") : QStringLiteral("停止"));
}

void MotorControl::mouseReleaseEvent(QGraphicsSceneMouseEvent* event) {
    if (event->button() == Qt::LeftButton && contains(event->pos())) {
        const bool targetRunning = !isRunning();

        if (writeToPlc(targetRunning ? 1.0 : 0.0)) {
            return;
        }

        if (!boundVariable()) {
            setValue(targetRunning ? 1.0 : 0.0);
            return;
        }
        // 【真设备的拒绝】绝不本地翻。电机显示"运行"而实际没转,
        //   操作员可能据此判断下游设备该不该处理 —— 与泵同等级的事故
        emit writeRejected(
            boundVariable()->access() != Variable::Access::ReadWrite
                ? QStringLiteral("该变量是只读的,电机不能启停")
                : QStringLiteral("无写权限或值超出范围,电机未启停"));
    }
    QGraphicsObject::mouseReleaseEvent(event);
}

} // namespace qwhmi::widgets
