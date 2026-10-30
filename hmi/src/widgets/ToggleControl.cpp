// ============================================================================
// ToggleControl 实现
// ============================================================================

#include "widgets/ToggleControl.h"

#include <QFont>
#include <QGraphicsSceneMouseEvent>
#include <QPainter>

namespace qwhmi::widgets {

ToggleControl::ToggleControl(QGraphicsItem* parent)
    : ControlBase(parent)
{
    setSize(QSizeF(80, 36));
    registerControlType("Toggle", []() -> ControlBase* { return new ToggleControl(); });
}

ToggleControl::~ToggleControl() = default;

void ToggleControl::setOnText(const QString& t) {
    if (t == onText_) return;
    onText_ = t;
    update();
    emit onTextChanged();
}
void ToggleControl::setOffText(const QString& t) {
    if (t == offText_) return;
    offText_ = t;
    update();
    emit offTextChanged();
}
void ToggleControl::setOnColor(const QColor& c) {
    if (c == onColor_) return;
    onColor_ = c;
    update();
    emit onColorChanged();
}

QRectF ToggleControl::boundingRect() const {
    return QRectF(0, 0, size().width(), size().height());
}

void ToggleControl::paintContent(QPainter* p,
                          const QStyleOptionGraphicsItem* option,
                          QWidget* widget) {
    Q_UNUSED(option); Q_UNUSED(widget);
    const QRectF br = boundingRect();
    const bool on = value().toDouble() > 0.5;

    // 背景色(开绿/关灰)
    p->setPen(Qt::NoPen);
    p->setBrush(on ? onColor_ : QColor(120, 120, 120));
    p->drawRoundedRect(br, 8, 8);

    // 文字
    p->setPen(QColor(255, 255, 255));
    p->setFont(QFont("Microsoft YaHei", 11, QFont::Bold));
    p->drawText(br, Qt::AlignCenter, on ? onText_ : offText_);
}

// ★ E.1c(2026-09-28)修复:原来这里调 setValue(只翻本地),emit toggled 后
//   【全仓库没有任何人接这个信号】—— 也就是说开关点了永远不会写回 PLC,
//   整个控件只是个装饰。同批修法见 ControlBase::writeToPlc。
void ToggleControl::mouseReleaseEvent(QGraphicsSceneMouseEvent* event) {
    if (event->button() == Qt::LeftButton && contains(event->pos())) {
        const bool nowOn = value().toDouble() < 0.5;  // 翻转
        const qreal newVal = nowOn ? 1.0 : 0.0;

        if (writeToPlc(newVal)) {
            // 写成功(含 Accepted 和 NoChange):值已由 writeToPlc 内部 setValue 刷过
            emit toggled(nowOn);
            return;
        }

        // 写被拒。两种情况要分开处理,不能一刀切:
        //  · 未绑定变量 —— 编辑器里的空摆件,没有 PLC 对象可写。
        //    这时本地翻一下当预览,免得操作员/作者在编辑器里点了没反应以为控件坏了。
        //  · 已绑定但被拒(只读变量 / 无权限 / 值非法) —— 这是【真设备的拒绝】,
        //    必须让用户看见没生效,绝不能本地翻,否则画面显示"ON"而 PLC 纹丝不动,
        //    操作员会据此判断设备状态 —— 这跟 ActionManager.cpp:75-86 修的
        //    "联动撒谎上报"是同一类事故。
        //
        // ★ G-21(2026-10-30):"未绑定就本地翻"这条【只限编辑器】。
        //   运行时画面上摆了个没接线的开关,点它一下画面显示"ON"而 PLC 纹丝不动 ——
        //   那就是在骗操作员。所以运行时一律不翻,改为把拒绝说出去。
        if (!boundVariable() && mayFlipLocallyAsPreview()) {
            setValue(newVal);
            emit toggled(nowOn);
            return;
        }
        // ★ G-22:理由统一由基类产生(每道闸都记了),这里只负责发出去。
        //   ★ 原来这里【什么都不做】—— 已绑定但被拒时是静默的,
        //     操作员点了开关没反应,只能猜。ToggleControl 本来连信号都没声明。
        notifyWriteRejected();
    }
    QGraphicsObject::mouseReleaseEvent(event);
}

} // namespace qwhmi::widgets