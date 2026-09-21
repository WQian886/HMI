// ============================================================================
// ResizeHandleItem 实现
// ============================================================================

#include "editor/ResizeHandleItem.h"
#include "widgets/base/ControlBase.h"

#include <QBrush>
#include <QCursor>
#include <QGraphicsSceneMouseEvent>
#include <QPen>

namespace qwhmi::editor {

ResizeHandleItem::ResizeHandleItem(HandleType type,
                                   qwhmi::widgets::ControlBase* target,
                                   QGraphicsItem* parent)
    : QGraphicsRectItem(parent), type_(type), target_(target) {
    setRect(-HANDLE_SIZE / 2, -HANDLE_SIZE / 2, HANDLE_SIZE, HANDLE_SIZE);
    setBrush(QBrush(QColor("#0078D7")));
    setPen(QPen(Qt::white, 1));
    setFlag(QGraphicsItem::ItemIgnoresTransformations, true);

    // ★ 鼠标光标按位置设置
    switch (type) {
        case HandleType::TopLeft:
        case HandleType::BottomRight:
            setCursor(Qt::SizeFDiagCursor); break;
        case HandleType::TopRight:
        case HandleType::BottomLeft:
            setCursor(Qt::SizeBDiagCursor); break;
        case HandleType::Top:
        case HandleType::Bottom:
            setCursor(Qt::SizeVerCursor); break;
        case HandleType::Left:
        case HandleType::Right:
            setCursor(Qt::SizeHorCursor); break;
    }
}

void ResizeHandleItem::layoutHandlesFor(qwhmi::widgets::ControlBase* target) {
    if (!target) return;
    const QRectF br = target->boundingRect();
    struct Pos { qreal x; qreal y; };
    // ★ 顺序:TL T TR L R BL B BR (与 HandleType 枚举值对应 0..7)
    const Pos positions[8] = {
        {0,             0},
        {br.width()/2,  0},
        {br.width(),    0},
        {0,             br.height()/2},
        {br.width(),    br.height()/2},
        {0,             br.height()},
        {br.width()/2,  br.height()},
        {br.width(),    br.height()},
    };
    int idx = 0;
    for (QGraphicsItem* child : target->childItems()) {
        auto* h = qgraphicsitem_cast<ResizeHandleItem*>(child);
        if (!h) continue;
        if (idx >= 8) break;
        h->setPos(positions[idx].x, positions[idx].y);
        ++idx;
    }
}

void ResizeHandleItem::installHandlesFor(qwhmi::widgets::ControlBase* target) {
    if (!target) return;
    // ★ 已挂载就跳过(避免重复)
    const auto children = target->childItems();
    for (QGraphicsItem* c : children) {
        if (qgraphicsitem_cast<ResizeHandleItem*>(c)) return;
    }
    // ★ 按 HandleType 顺序(TL/T/TR/L/R/BL/B/BR)挂 8 个
    for (int i = 0; i < 8; ++i) {
        auto* h = new ResizeHandleItem(static_cast<HandleType>(i), target, target);
        h->setZValue(100);   // 永远在 target 之上
    }
    layoutHandlesFor(target);
}

void ResizeHandleItem::removeHandlesFor(qwhmi::widgets::ControlBase* target) {
    if (!target) return;
    QList<QGraphicsItem*> toDel;
    for (QGraphicsItem* c : target->childItems()) {
        if (qgraphicsitem_cast<ResizeHandleItem*>(c)) toDel.append(c);
    }
    qDeleteAll(toDel);
}

void ResizeHandleItem::mousePressEvent(QGraphicsSceneMouseEvent* e) {
    if (!target_) { e->ignore(); return; }
    e->accept();
    handleMousePress(e->scenePos());
}

void ResizeHandleItem::mouseMoveEvent(QGraphicsSceneMouseEvent* e) {
    if (!target_) { e->ignore(); return; }
    e->accept();
    handleMouseMove(e->scenePos(), e->modifiers());
}

void ResizeHandleItem::mouseReleaseEvent(QGraphicsSceneMouseEvent* e) {
    e->accept();
    handleMouseRelease(e->scenePos());
}

// ===== 测试入口(public)— 绕过 protected mouse*Event =====
void ResizeHandleItem::handleMousePress(QPointF scenePos) {
    if (!target_) return;
    pressScenePos_     = scenePos;
    originalSize_      = target_->size();
    originalTargetPos_ = target_->pos();
}

void ResizeHandleItem::handleMouseMove(QPointF scenePos, Qt::KeyboardModifiers mods) {
    if (!target_) return;
    const QPointF delta = scenePos - pressScenePos_;
    applyResize(delta, mods & Qt::ShiftModifier);
}

void ResizeHandleItem::handleMouseRelease(QPointF scenePos) {
    Q_UNUSED(scenePos);
}

void ResizeHandleItem::applyResize(const QPointF& delta, bool shift) {
    if (!target_) return;

    QSizeF newSize  = originalSize_;
    QPointF newPos  = originalTargetPos_;
    const qreal aspect = (originalSize_.height() > 0.0)
                       ? originalSize_.width() / originalSize_.height()
                       : 1.0;

    switch (type_) {
        case HandleType::BottomRight:
            newSize.setWidth (originalSize_.width()  + delta.x());
            newSize.setHeight(originalSize_.height() + delta.y());
            break;
        case HandleType::BottomLeft:
            newSize.setWidth (originalSize_.width()  - delta.x());
            newSize.setHeight(originalSize_.height() + delta.y());
            newPos.setX(originalTargetPos_.x() + delta.x());
            break;
        case HandleType::TopRight:
            newSize.setWidth (originalSize_.width()  + delta.x());
            newSize.setHeight(originalSize_.height() - delta.y());
            newPos.setY(originalTargetPos_.y() + delta.y());
            break;
        case HandleType::TopLeft:
            newSize.setWidth (originalSize_.width()  - delta.x());
            newSize.setHeight(originalSize_.height() - delta.y());
            newPos = originalTargetPos_ + delta;
            break;
        case HandleType::Right:
            newSize.setWidth(originalSize_.width() + delta.x());
            break;
        case HandleType::Left:
            newSize.setWidth(originalSize_.width() - delta.x());
            newPos.setX(originalTargetPos_.x() + delta.x());
            break;
        case HandleType::Bottom:
            newSize.setHeight(originalSize_.height() + delta.y());
            break;
        case HandleType::Top:
            newSize.setHeight(originalSize_.height() - delta.y());
            newPos.setY(originalTargetPos_.y() + delta.y());
            break;
    }

    // ★ Shift:锁定纵横比(只对 4 角有效 — 边 resize 不锁定)
    if (shift && (type_ == HandleType::TopLeft || type_ == HandleType::TopRight ||
                  type_ == HandleType::BottomLeft || type_ == HandleType::BottomRight)) {
        if (aspect > 0.0) {
            // ★ 用 width 作主轴,height 按比例算;反之亦然(取较大变化的那个)
            if (qAbs(delta.x()) > qAbs(delta.y())) {
                newSize.setHeight(newSize.width() / aspect);
            } else {
                newSize.setWidth(newSize.height() * aspect);
            }
        }
    }

    // ★ 最小尺寸约束(20x20)
    if (newSize.width()  < MIN_SIZE) newSize.setWidth(MIN_SIZE);
    if (newSize.height() < MIN_SIZE) newSize.setHeight(MIN_SIZE);

    // ★ 应用到 target(会触发 sizeChanged 信号)
    target_->setSize(newSize);
    target_->setPos(newPos);
}

} // namespace qwhmi::editor
