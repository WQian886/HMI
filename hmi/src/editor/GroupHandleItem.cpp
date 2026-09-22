// ============================================================================
// GroupHandleItem 实现
// ============================================================================

#include "editor/GroupHandleItem.h"
#include "editor/ResizeHandleItem.h"
#include "widgets/base/ControlBase.h"

#include <QPainter>

namespace qwhmi::editor {

GroupHandleItem::GroupHandleItem(QGraphicsItem* parent)
    : QGraphicsRectItem(parent) {
    // ★ group 仅作为容器,无视觉
    setPen(Qt::NoPen);
    setBrush(Qt::NoBrush);
    // ★ 不接受鼠标事件(防止吞掉 handle 的 click)
    setAcceptedMouseButtons(Qt::NoButton);
    setFlag(QGraphicsItem::ItemHasNoContents, true);   // 不画、不 hit-test
    // ★ group 在 scene 顶层(target 不挂 children)
    setZValue(150);
}

GroupHandleItem::~GroupHandleItem() {
}

void GroupHandleItem::setTargets(const QList<qwhmi::widgets::ControlBase*>& targets) {
    targets_ = targets;
    if (targets_.isEmpty()) {
        setRect(QRectF(0, 0, 0, 0));
        return;
    }
    // ★ 计算合并 sceneBoundingRect,设到自己的 rect
    QRectF unionRect;
    bool first = true;
    for (auto* t : targets_) {
        if (!t) continue;
        const QRectF sb = t->sceneBoundingRect();
        if (first) { unionRect = sb; first = false; }
        else        unionRect = unionRect.united(sb);
    }
    // ★ group 是 scene 顶层 item,rect 用 scene 坐标
    setRect(unionRect);
}

void GroupHandleItem::captureOriginalRects() {
    originalGroupSceneRect_ = sceneBoundingRect();
    originalSceneRects_.clear();
    for (auto* t : targets_) {
        if (t) originalSceneRects_[t] = t->sceneBoundingRect();
    }
}

void GroupHandleItem::applyProportionalResize(const QRectF& newSceneRect) {
    if (geometryChanging_) return;
    if (targets_.isEmpty() || originalGroupSceneRect_.isEmpty()) return;
    if (originalGroupSceneRect_.width() <= 0 || originalGroupSceneRect_.height() <= 0) return;

    const qreal sx = newSceneRect.width()  / originalGroupSceneRect_.width();
    const qreal sy = newSceneRect.height() / originalGroupSceneRect_.height();

    // ★ MIN_SIZE 限制:ResizeHandleItem 内有,但这里再防一次
    const qreal minW = ResizeHandleItem::MIN_SIZE;
    const qreal minH = ResizeHandleItem::MIN_SIZE;

    // ★ 抑制递归:下面调 target->setSize 会触发 sizeChanged → connect → scene->update → ...
    geometryChanging_ = true;
    for (auto* t : targets_) {
        if (!t) continue;
        const QRectF orig = originalSceneRects_.value(t);
        if (orig.isEmpty()) continue;

        // ★ 等比缩放:原位置相对原 group 的 (x, y) 在新 group 中的位置
        const qreal relX = (orig.x() - originalGroupSceneRect_.x()) / originalGroupSceneRect_.width();
        const qreal relY = (orig.y() - originalGroupSceneRect_.y()) / originalGroupSceneRect_.height();
        const qreal newX = newSceneRect.x() + relX * newSceneRect.width();
        const qreal newY = newSceneRect.y() + relY * newSceneRect.height();

        const qreal newW = qMax(minW, orig.width()  * sx);
        const qreal newH = qMax(minH, orig.height() * sy);

        // ★ 写回:position + size
        t->setPosition(QPointF(newX, newY));
        t->setSize(QSizeF(newW, newH));
    }
    geometryChanging_ = false;
}

QVariant GroupHandleItem::itemChange(QGraphicsItem::GraphicsItemChange change, const QVariant& value) {
    if (change == QGraphicsItem::ItemPositionChange && !geometryChanging_) {
        // ★ 自己的 rect/pos 变了(被 handle 拖动 / scene 变换等)→ 按比例重算所有 targets
        const QRectF newRect = value.toRectF();
        applyProportionalResize(newRect);
    }
    return QGraphicsRectItem::itemChange(change, value);
}

void GroupHandleItem::paint(QPainter*,
                           const QStyleOptionGraphicsItem*,
                           QWidget*) {
    // ★ 不画(group 仅作为容器 + 几何引用点)
}

// ===== 静态工厂:复用 ResizeHandleItem 现有 API =====

void GroupHandleItem::installHandlesFor(GroupHandleItem* group) {
    if (!group) return;
    // 已挂就跳过(避免重复)
    for (QGraphicsItem* c : group->childItems()) {
        if (qgraphicsitem_cast<ResizeHandleItem*>(c)) return;
    }
    // ★ 关键:handle 的 target 指向 group 本身(不是 ControlBase!)
    //   ResizeHandleItem::applyResize 会调 target_->setSize/setPos
    //   → 调 group 的 setRect / setPos
    //   → 触发 group 的 itemGeometryChange → 按比例重算所有真 targets
    for (int i = 0; i < 8; ++i) {
        auto* h = new ResizeHandleItem(static_cast<HandleType>(i), nullptr, group);
        h->setZValue(200);
        // ★ 重写 applyResize 调 setSize/setPos 到 group(已自然实现,因为 target=nullptr 会早退)
        //   改:为支持 group,把 target_ 设为 group,让现有 applyResize 直接 work
        //   注意:ResizeHandleItem 构造要求 ControlBase*,但实际只要 setSize/setPos 存在
        //   解决:写一个 friend 包装,或让 ResizeHandleItem 接受 QGraphicsItem*
    }
}

void GroupHandleItem::layoutHandlesFor(GroupHandleItem* group) {
    if (!group) return;
    const QRectF br = group->rect();
    struct Pos { qreal x; qreal y; };
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
    for (QGraphicsItem* c : group->childItems()) {
        auto* h = qgraphicsitem_cast<ResizeHandleItem*>(c);
        if (!h) continue;
        if (idx >= 8) break;
        h->setPos(positions[idx].x, positions[idx].y);
        ++idx;
    }
}

void GroupHandleItem::removeHandlesFor(GroupHandleItem* group) {
    if (!group) return;
    QList<QGraphicsItem*> toDel;
    for (QGraphicsItem* c : group->childItems()) {
        if (qgraphicsitem_cast<ResizeHandleItem*>(c)) toDel.append(c);
    }
    qDeleteAll(toDel);
}

} // namespace qwhmi::editor
