// ============================================================================
// 阶段 E · E.1c · ValveControl 实现
// 对应 ValveControl.h
// ============================================================================

#include "widgets/ValveControl.h"

#include <QGraphicsSceneMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>

namespace qwhmi::widgets {

ValveControl::ValveControl(QGraphicsItem* parent)
    : ControlBase(parent)
{
    // ★ 阀门符号是正方形(对顶三角),扁了就不像阀门了
    setSize(QSizeF(64, 64));
    registerControlType("Valve", []() -> ControlBase* { return new ValveControl(); });

    // ★ 监听"开合态"变化而不是"点击"。PLC 远程改值也要能反映到画面,
    //   否则远程关了阀门画面上还显示开着 —— 操作员据此判断会出事。
    connect(this, &ControlBase::valueChanged, this, [this] {
        const bool nowOpen = isOpen();
        if (nowOpen != lastOpen_) {
            lastOpen_ = nowOpen;
            emit openedChanged();
        }
    });
}

ValveControl::~ValveControl() = default;

void ValveControl::setOpenColor(const QColor& c) {
    if (c == openColor_) return;
    openColor_ = c;
    update();
    emit openColorChanged();
}
void ValveControl::setClosedColor(const QColor& c) {
    if (c == closedColor_) return;
    closedColor_ = c;
    update();
    emit closedColorChanged();
}

// ★ value >= 0.5 算开。绑 Bool 变量时 coerceToDataType 把 double 适配成
//   QVariant(bool),QVariant::toDouble() 对 bool 返回 1/0 —— 两种类型都对。
bool ValveControl::isOpen() const {
    return value().toDouble() >= 0.5;
}

QRectF ValveControl::boundingRect() const {
    return QRectF(0, 0, size().width(), size().height());
}

void ValveControl::paintContent(QPainter* p,
                         const QStyleOptionGraphicsItem* /*option*/,
                         QWidget* /*widget*/) {
    p->setRenderHint(QPainter::Antialiasing);

    const QRectF br = boundingRect();
    const bool open = isOpen();
    const QColor body = open ? openColor_ : closedColor_;

    // ★ 不可写时(未绑定 / 只读 / Viewer 权限)画成灰的。
    //   理由:让操作员【点之前】就知道点不动,而不是点了没反应再去猜。
    //   灰色只影响观感,真正的拦截在 writeToPlc 的四道闸里。
    const bool writable = canWrite();
    const QColor drawColor = writable ? body : QColor(150, 150, 155);

    // ① 中心竖杆(GB 符号里贯穿两片挡板的那根)
    p->setPen(QPen(isSelected() ? QColor(0, 120, 215) : QColor(70, 70, 75), 2));
    p->drawLine(QPointF(br.center().x(), br.top() + 4),
                QPointF(br.center().x(), br.bottom() - 4));

    // ② 上下两片对顶三角 —— 阀门的标准符号
    const qreal halfW = br.width() / 2 - 8;
    const qreal halfH = br.height() / 2 - 8;
    const QPointF c = br.center();
    const QPointF left (c.x() - halfW, c.y());
    const QPointF right(c.x() + halfW, c.y());
    const QPointF top  (c.x(), c.y() - halfH);
    const QPointF bot  (c.x(), c.y() + halfH);

    p->setBrush(drawColor);
    p->setPen(Qt::NoPen);
    p->drawPolygon(QPolygonF({left, top, right}));   // 上片
    p->drawPolygon(QPolygonF({left, bot, right}));   // 下片

    // ③ 顶部手轮(阀门都有,画出来操作员一眼认得出)
    p->setPen(QPen(isSelected() ? QColor(0, 120, 215) : QColor(70, 70, 75), 2));
    p->setBrush(Qt::NoBrush);
    p->drawEllipse(QPointF(c.x(), br.top() + 6), 6, 6);

    // ④ 开/关文字。现场画面通常很小,颜色之外再给一个文字冗余 ——
    //   红色弱视/灰度打印下颜色不可靠,文字一定看得清
    p->setPen(drawColor.darker(120));
    p->drawText(QRectF(br.left(), br.bottom() - 15, br.width(), 14),
                Qt::AlignCenter, open ? QStringLiteral("开") : QStringLiteral("关"));
}

void ValveControl::mouseReleaseEvent(QGraphicsSceneMouseEvent* event) {
    if (event->button() == Qt::LeftButton && contains(event->pos())) {
        const bool targetOpen = !isOpen();

        // ★ 唯一的写回出口 —— 走基类的四道闸(绑定/只读/权限/值合法)。
        //   阀门控制的是高压管路,绝不能绕过 writeToPlc 自己 setValue,
        //   否则权限和审计全失效(见 ControlBase::writeToPlc 头注释)
        if (writeToPlc(targetOpen ? 1.0 : 0.0)) {
            return;   // 写成功,值已由 writeToPlc 内部 setValue 刷过
        }

        // 写被拒。【绝不能本地翻】—— 画面显示"开"而 PLC 纹丝不动,
        //   操作员据此判断管路状态,这是跟 ActionManager.cpp:75-86 修的
        //   "联动撒谎上报"同一类事故(参见 ToggleControl.cpp:77-87 的同类处理)。
        //   区别:阀门比开关更严重 —— 开关错了操作员按一下就好,
        //   阀门显示开着但实际关着,人可能直接伸手去动管路。
        //
        // ★ G-21(2026-10-30):"未绑定就本地翻"这条【只限编辑器】。
        //   运行时画面上摆了个没接线的阀门,点它一下画面显示"开"而 PLC 纹丝不动 ——
        //   那就是在骗操作员。所以运行时一律不翻,改为把拒绝说出去。
        if (!boundVariable() && mayFlipLocallyAsPreview()) {
            // 编辑器里的空摆件:本地翻一下当预览
            setValue(targetOpen ? 1.0 : 0.0);
            return;
        }
        // ★ G-22:理由统一由基类产生(每道闸都记了),这里只负责发出去。
        notifyWriteRejected();
    }
    QGraphicsObject::mouseReleaseEvent(event);
}

} // namespace qwhmi::widgets
