// ============================================================================
// ResizeHandleItem 实现
// ============================================================================

#include "editor/ResizeHandleItem.h"
#include "editor/GroupHandleItem.h"
#include "widgets/base/ControlBase.h"

#include <QBrush>
#include <QCursor>
#include <QGraphicsSceneMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>

namespace qwhmi::editor {

ResizeHandleItem::ResizeHandleItem(HandleType type,
                                   QGraphicsItem* target,
                                   QGraphicsItem* parent)
    : QGraphicsRectItem(parent), type_(type), target_(target) {
    // ★ hit area 保持 8x8(用户体验:容易点中);视觉画 4x4(方案 A,贴边)
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

// ★ 方案 A:视觉缩到 4x4(贴边),hit area 保持 8x8(易点中)
//   旧实现:8x8 视觉横跨控件边内外各 4px → "handle 边和控件边不在同一线上"
//   新实现:4x4 视觉横跨 2px,接近"贴边"感,不再有"半埋半外"的违和感
void ResizeHandleItem::paint(QPainter* painter,
                             const QStyleOptionGraphicsItem* option,
                             QWidget* widget) {
    Q_UNUSED(option); Q_UNUSED(widget);
    painter->setBrush(brush());
    painter->setPen(pen());
    const qreal inset = HANDLE_SIZE / 4;   // 8 → 内缩 2 → 画 4x4
    painter->drawRect(rect().adjusted(inset, inset, -inset, -inset));
}

QPainterPath ResizeHandleItem::shape() const {
    // ★ hit area 保持完整 8x8(本地坐标),用户点中范围不变
    QPainterPath p;
    p.addRect(rect());
    return p;
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
    // ★ target 可能是 ControlBase 或 QGraphicsRectItem(group):用 boundingRect 取 size
    //   (QGraphicsItem 没 size() 方法,boundingRect 是两个基类都有的 virtual)
    originalSize_      = target_->boundingRect().size();
    originalTargetPos_ = target_->pos();

    // ★ E.4d 续-3:多选 group path — 拖 handle 开始时 group 快照所有真 targets
    //   这样后续 applyResize 按原始快照算等比缩放
    if (auto* gri = qgraphicsitem_cast<QGraphicsRectItem*>(target_)) {
        if (auto* group = dynamic_cast<GroupHandleItem*>(gri)) {
            group->captureOriginalRects();
        }
    }
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

    // ★ 应用:target 可能是 ControlBase(单选) 或 QGraphicsRectItem(GroupHandleItem 多选)
    //   通过 dynamic_cast 分发 — QGraphicsItem* 到 ControlBase* 是合法的向下转型
    if (auto* cb = dynamic_cast<qwhmi::widgets::ControlBase*>(target_)) {
        // ★ 单选 path
        cb->setPos(newPos);
        cb->setSize(newSize);   // 触发 ControlBase::sizeChanged 信号
    } else if (auto* gri = qgraphicsitem_cast<QGraphicsRectItem*>(target_)) {
        // ★ 多选 group path:setRect 改 rect(itemGeometryChange 触发
        //   GroupHandleItem::itemChange → 按比例缩放所有真 targets)
        gri->setPos(newPos);
        const QRectF r = gri->rect();
        gri->setRect(QRectF(r.x(), r.y(), newSize.width(), newSize.height()));
    } else {
        // ★ 兜底:当作普通 QGraphicsItem,只 setPos
        target_->setPos(newPos);
    }
}

} // namespace qwhmi::editor
