// ============================================================================
// 阶段 E · E.1c · PumpControl 实现
// 对应 PumpControl.h
// ============================================================================

#include "widgets/PumpControl.h"

#include <QGraphicsSceneMouseEvent>
#include <QPainter>
#include <QPen>
#include <QtMath>

namespace qwhmi::widgets {

PumpControl::PumpControl(QGraphicsItem* parent)
    : ControlBase(parent)
    , spin_(this)
{
    setSize(QSizeF(90, 100));
    registerControlType("Pump", []() -> ControlBase* { return new PumpControl(); });

    // ★ 角度一变就重绘
    connect(&spin_, &SpinAnimator::angleChanged, this, [this](qreal) { update(); });

    // ★ 运行态变化 → 动画跟着开关 + 发信号
    //   注意 setRunning 的调用在 updateRunning() 里,那里才判 bound_
    connect(this, &ControlBase::valueChanged, this, [this] {
        const bool nowRunning = isRunning();
        if (nowRunning != lastRunning_) {
            lastRunning_ = nowRunning;
            updateRunning();
            emit runningChanged();
        }
    });
}

PumpControl::~PumpControl() = default;

void PumpControl::setBodyColor(const QColor& c) {
    if (c == bodyColor_) return;
    bodyColor_ = c;
    update();
    emit bodyColorChanged();
}
void PumpControl::setRunColor(const QColor& c) {
    if (c == runColor_) return;
    runColor_ = c;
    update();
    emit runColorChanged();
}
void PumpControl::setSpinPeriodMs(int ms) {
    const int before = spin_.periodMs();
    spin_.setPeriodMs(ms);
    if (spin_.periodMs() == before) return;   // 被夹到同一个值,不算变化
    update();
    emit spinPeriodMsChanged();
}

bool PumpControl::isRunning() const {
    return value().toDouble() >= 0.5;
}

// ★ 动画只在【已绑定真实变量 且 运行中】时转。
//   没绑定 = 编辑器里的空摆件,不该动;这样也不需要跟 EditorMainWindow
//   的 editorMode_ 做模式同步(详见 SpinAnimator.h 头注释)
void PumpControl::updateRunning() {
    spin_.setRunning(boundVariable() != nullptr && isRunning());
}

QRectF PumpControl::boundingRect() const {
    return QRectF(0, 0, size().width(), size().height());
}

void PumpControl::paintContent(QPainter* p,
                        const QStyleOptionGraphicsItem* /*option*/,
                        QWidget* /*widget*/) {
    p->setRenderHint(QPainter::Antialiasing);

    const QRectF br = boundingRect();
    const bool running = isRunning();
    const qreal radius = qMin(br.width(), br.height() - 18) / 2.0 - 4;
    // 圆心比几何中心【偏下】,上方留给出管嘴的位置(泵的标准符号)
    const QPointF c(br.center().x(), br.center().y() + 5);

    // ① 顶部出管嘴(泵的标志形状之一,先画,被泵壳盖住接缝)
    const qreal neckW = radius * 0.55;
    const qreal neckH = 14;
    p->setPen(Qt::NoPen);
    p->setBrush(bodyColor_.darker(110));
    p->drawRect(QRectF(c.x() - neckW / 2, c.y() - radius - neckH + 2, neckW, neckH + 4));

    // ② 泵壳
    p->setBrush(running ? bodyColor_ : bodyColor_.darker(115));
    p->drawEllipse(c, radius, radius);

    // ③ 三叶转子。只在运行时画 —— 停泵的转子也画出来但不着色,
    //   这样能看出"这是个泵"而不是"这坨灰是什么"
    p->setPen(Qt::NoPen);
    p->setBrush(running ? runColor_ : QColor(110, 112, 115));
    const qreal bladeLen = radius * 0.72;
    const qreal bladeW   = radius * 0.30;
    // ★ 角速度跟动画角度一致 —— 画面上叶片转到哪,动画就转到哪。
    //   用 spin_.angle() 而不是 value:转速由 spinPeriodMs 控制,跟流量无关
    p->save();
    p->translate(c);
    p->rotate(spin_.angle());
    for (int i = 0; i < 3; ++i) {
        p->save();
        p->rotate(120.0 * i);
        // 每片叶子从中心往一个方向伸出,末梢略弯(离心泵叶片是后弯的)
        QPainterPath path;
        path.moveTo(0, 0);
        path.lineTo(bladeLen * 0.35, -bladeW / 2);
        path.lineTo(bladeLen, -bladeW * 0.28);
        path.lineTo(bladeLen,  bladeW * 0.28);
        path.lineTo(bladeLen * 0.35,  bladeW / 2);
        path.closeSubpath();
        p->drawPath(path);
        p->restore();
    }
    p->restore();

    // ④ 中心轴
    p->setBrush(bodyColor_.darker(140));
    p->drawEllipse(c, radius * 0.16, radius * 0.16);

    // ⑤ 描边 / 选中态
    QPen border(isSelected() ? QColor(0, 120, 215) : QColor(100, 105, 110));
    border.setWidth(isSelected() ? 2 : 1);
    p->setPen(border);
    p->setBrush(Qt::NoBrush);
    p->drawEllipse(c, radius, radius);

    // ⑥ 运行文字。跟 ValveControl 一样给颜色一个文字冗余 ——
    //   红绿色弱视/灰度打印下颜色不可靠
    p->setPen(running ? runColor_ : QColor(120, 120, 125));
    p->drawText(QRectF(br.left(), br.bottom() - 15, br.width(), 14),
                Qt::AlignCenter, running ? QStringLiteral("运行") : QStringLiteral("停止"));
}

void PumpControl::mouseReleaseEvent(QGraphicsSceneMouseEvent* event) {
    if (event->button() == Qt::LeftButton && contains(event->pos())) {
        const bool targetRunning = !isRunning();

        // ★ 唯一的写回出口。启停泵是有后果的操作,必须走四道闸
        if (writeToPlc(targetRunning ? 1.0 : 0.0)) {
            return;
        }

        if (!boundVariable() && mayFlipLocallyAsPreview()) {
            // 未绑定 = 编辑器预览,本地翻一下
            setValue(targetRunning ? 1.0 : 0.0);
            return;
        }
        // 【真设备的拒绝】绝不能本地翻。跟 ValveControl 同理:
        //   画面显示"运行"而泵实际没动,操作员据此判断管路有压 —— 出事
        // ★ G-21:未绑定也一样 —— 运行时画面上摆了个没接线的泵,
        //   点一下就"转起来"是骗人。编辑器里(预览)才允许翻。
        // ★ G-22:理由统一由基类产生,这里只负责发出去。
        notifyWriteRejected();
    }
    QGraphicsObject::mouseReleaseEvent(event);
}

} // namespace qwhmi::widgets
