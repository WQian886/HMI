// ============================================================================
// 阶段 E · E.4d 续-3 · GroupHandleItem(多选组缩放容器)
//
// 文件职责:QGraphicsRectItem 子类,作为多选时的"虚拟容器"。
//   - rect = 所有选中控件 sceneBoundingRect 的合并
//   - 8 个 ResizeHandleItem 挂到 group 上(伪 single target 模式,所有 ResizeHandleItem
//     现有逻辑无需改)
//   - group 监听自己的 itemGeometryChange → 按原始比例重新算每个真 target 的 size+pos
//   - paint() 不画任何东西(group 仅作为父容器 + 几何引用点)
//
// 关联:EditorMainWindow.cpp::onSceneSelectionChanged 多选时 install/uninstall,
//       ResizeHandleItem.cpp::applyResize(现有 applyResize 直接调 setSize/setPos 即可)
//
// 设计取舍:
//   不用新建 GroupResizeController 类 — 复用 ResizeHandleItem 现有的"单 target resize"逻辑
//   全部"多 target 协同"由 group 自己处理(itemChange 触发按比例重算)
// ============================================================================

#pragma once

#include <QGraphicsRectItem>
#include <QHash>
#include <QList>

namespace qwhmi::widgets { class ControlBase; }

namespace qwhmi::editor {

class GroupHandleItem : public QGraphicsRectItem {
public:
    explicit GroupHandleItem(QGraphicsItem* parent = nullptr);
    ~GroupHandleItem() override;

    // ★ 初始化:记录 targets 并按合并 sceneBoundingRect 设置 rect
    void setTargets(const QList<qwhmi::widgets::ControlBase*>& targets);

    // ★ 静态工厂:挂/卸/排 8 个 ResizeHandleItem(复用单选版 handle API)
    static void installHandlesFor(GroupHandleItem* group);
    static void removeHandlesFor(GroupHandleItem* group);
    static void layoutHandlesFor(GroupHandleItem* group);

    // ★ E.4d 续-3:public — ResizeHandleItem::handleMousePress 调这个拍照
    void captureOriginalRects();

    // ★ E.4d 续-3:public — ResizeHandleItem::applyResize 调这个,按比例缩放所有真 targets
    //   替代通过 setRect + itemChange 触发 — 后者 value 是 QPointF 不是 QRectF,容易误判
    //   handle 内部已经拍照 snapshot(group->captureOriginalRects 在 handleMousePress 时)
    void resizeToGroupRect(const QRectF& newGroupRect);

    const QList<qwhmi::widgets::ControlBase*>& targets() const { return targets_; }

protected:
    // ★ 关键:group 自己 rect 被拖动时触发,按原始快照等比缩放所有真 targets
    QVariant itemChange(QGraphicsItem::GraphicsItemChange change, const QVariant& value) override;

    // ★ group 本身不画
    void paint(QPainter* painter,
               const QStyleOptionGraphicsItem* option,
               QWidget* widget = nullptr) override;

private:
    // ★ ItemGeometryChange 触发的实际按比例重算
    void applyProportionalResize(const QRectF& newSceneRect);

    QList<qwhmi::widgets::ControlBase*> targets_;
    QHash<qwhmi::widgets::ControlBase*, QRectF> originalSceneRects_;  // 每个 target 的原 sceneBoundingRect
    QRectF originalGroupSceneRect_;                                   // group 自身的原 sceneBoundingRect
    bool geometryChanging_ = false;                                   // 抑制 itemChange 递归
};

} // namespace qwhmi::editor
