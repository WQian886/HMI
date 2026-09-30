// ============================================================================
// 阶段 E · E.1c · PipeControl 实现
// 对应 PipeControl.h
// ============================================================================

#include "widgets/PipeControl.h"

#include <QPainter>
#include <QPainterPath>
#include <QPen>

namespace qwhmi::widgets {

PipeControl::PipeControl(QGraphicsItem* parent)
    : ControlBase(parent)
{
    // ★ 默认 40x120 —— 竖管要瘦高
    setSize(QSizeF(40, 120));
    registerControlType("Pipe", []() -> ControlBase* { return new PipeControl(); });
}

PipeControl::~PipeControl() = default;

void PipeControl::setPipeColor(const QColor& c) {
    if (c == pipeColor_) return;
    pipeColor_ = c;
    update();
    emit pipeColorChanged();
}
void PipeControl::setFlowColor(const QColor& c) {
    if (c == flowColor_) return;
    flowColor_ = c;
    update();
    emit flowColorChanged();
}

// ★ thickness 夹到 [4, 120]。不夹的话属性面板填个 0 会画出零宽管子,
//   Qt 画不出东西但 boundingRect 还在,后面选中框/handle 会跟一个
//   看不见的东西对不上,排查起来非常费劲
void PipeControl::setThickness(int px) {
    if (px < 4) px = 4;
    if (px > 120) px = 120;
    if (px == thickness_) return;
    ensureGeometryFor(px);
    thickness_ = px;
    update();
    emit thicknessChanged();
}

void PipeControl::ensureGeometryFor(int px) {
    // 管身变粗到超出 size().width() 时跟着撑宽,否则管子被 boundingRect 裁掉
    const qreal need = px + 12;
    if (size().width() < need) {
        setSize(QSizeF(need, size().height()));
    }
}

// ★ 阈值 0.01:现场流量计的分辨率通常 0.01,再小就是噪声了。
//   用严格 > 0 会让极小的噪声值(0.0001 之类)也点亮流动箭头,
//   画面上一片管子都在闪,看着像故障
bool PipeControl::isFlowing() const {
    return qAbs(value().toDouble()) > 0.01;
}

QRectF PipeControl::boundingRect() const {
    return QRectF(0, 0, size().width(), size().height());
}

void PipeControl::paintContent(QPainter* p,
                        const QStyleOptionGraphicsItem* /*option*/,
                        QWidget* /*widget*/) {
    p->setRenderHint(QPainter::Antialiasing);

    const QRectF br = boundingRect();
    const qreal cx = br.center().x();
    const qreal t = thickness_;
    // ★ 管身两端各留 2px,描边有一半在外面会被 boundingRect 裁掉
    const QRectF body(cx - t / 2, 2, t, br.height() - 4);

    // ① 管身(两端带法兰:工业管道的接头)
    p->setPen(Qt::NoPen);
    p->setBrush(pipeColor_);
    p->drawRoundedRect(body, t / 4, t / 4);

    const qreal flangeW = t * 0.9;
    const qreal flangeH = t * 0.35;
    p->drawRect(QRectF(cx - flangeW / 2, body.top(), flangeW, flangeH));
    p->drawRect(QRectF(cx - flangeW / 2, body.bottom() - flangeH, flangeW, flangeH));

    // ② 流动箭头。管内有流量时叠加画几个朝流向的箭头
    if (isFlowing()) {
        p->setPen(Qt::NoPen);
        p->setBrush(flowColor_);
        const bool up = !isReversed();
        const qreal step = t * 1.1;          // 箭头间距
        const qreal aw = t * 0.42;           // 箭头半宽
        const qreal ah = t * 0.55;           // 箭头半高
        // 从管子中部往两端均匀排,首尾各留半个间距,避免贴边
        const int n = qMax(1, static_cast<int>((body.height() - step) / step));
        const qreal span = n * step;
        const qreal start = body.center().y() - span / 2 + step / 2;
        for (int i = 0; i < n; ++i) {
            const qreal y = start + i * step;
            QPolygonF tri;
            if (up) {
                tri << QPointF(cx, y - ah) << QPointF(cx - aw, y + ah)
                     << QPointF(cx + aw, y + ah);
            } else {
                tri << QPointF(cx, y + ah) << QPointF(cx - aw, y - ah)
                     << QPointF(cx + aw, y - ah);
            }
            p->drawPolygon(tri);
        }
    }

    // ③ 描边。选中加深蓝 2px(全项目统一色 QColor(0,120,215))
    QPen border(isSelected() ? QColor(0, 120, 215) : QColor(110, 110, 115));
    border.setWidth(isSelected() ? 2 : 1);
    p->setPen(border);
    p->setBrush(Qt::NoBrush);
    p->drawRoundedRect(body.adjusted(0.5, 0.5, -0.5, -0.5), t / 4, t / 4);
}

} // namespace qwhmi::widgets
