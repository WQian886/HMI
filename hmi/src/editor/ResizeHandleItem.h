// ============================================================================
// 阶段 E · E.4b · 缩放手柄 ResizeHandleItem
//
// 文件职责:8 个缩放控制点(TL/T/TR/L/R/BL/B/BR)中的一个。
//   - 视觉:8x8 蓝色方块 + 合适光标
//   - 行为:mousePress 记录 target 初始状态 → mouseMove 算 delta → 应用 resize
//   - Shift:纵横比锁定
//   - 最小尺寸 20x20
//
// 关联:EditorMainWindow.h(挂载/卸载)、ControlBase.h
// ============================================================================

#pragma once

#include <QGraphicsRectItem>
#include <QPainterPath>

namespace qwhmi::widgets { class ControlBase; }

class QGraphicsItem;

namespace qwhmi::editor {

enum class HandleType {
    TopLeft     = 0, Top = 1, TopRight  = 2,
    Left        = 3,         Right     = 4,
    BottomLeft  = 5, Bottom = 6, BottomRight = 7,
};

class ResizeHandleItem : public QGraphicsRectItem {
public:
    static constexpr qreal HANDLE_SIZE = 8.0;
    static constexpr qreal MIN_SIZE     = 20.0;

    explicit ResizeHandleItem(HandleType type,
                               QGraphicsItem* target,
                               QGraphicsItem* parent = nullptr);

    HandleType handleType() const { return type_; }
    QGraphicsItem* target() const { return target_; }

    // ★ 给 EditorMainWindow 调 — 把 8 个 handle 摆到目标 boundingRect 的 8 个位置
    //   假设 8 个 handle 都是 target 的 child,按 HandleType 顺序对应 8 个角/边
    static void layoutHandlesFor(qwhmi::widgets::ControlBase* target);

    // ★ 测试 + 通用:为目标挂上 8 个 handle(已挂则跳过)
    static void installHandlesFor(qwhmi::widgets::ControlBase* target);

    // ★ 反向 — 移除目标上的所有 handle
    static void removeHandlesFor(qwhmi::widgets::ControlBase* target);

    // ★ E.4d-follow-10(2026-09-29):按 target 当前的 rotation 重算光标
    //   背景:handle 是 target 的【子 item】,父一转 8 个 handle 跟着转,
    //         位置自动就对上了(Qt 的父子 transform 干的活,不用我们管)。
    //   但 type_ 是构造时定死的 —— 那个 handle 原本在左边,转 90° 之后
    //   它在视觉上到了上边,可光标还是 SizeHor(左右箭头)。
    //   用户看到的是"朝下的箭头配一个横向的方块",鼠标拖动方向和视觉对不上。
    //   这个静态函数给 target 的所有 handle 重刷一遍光标。
    static void refreshCursorsFor(qwhmi::widgets::ControlBase* target);

public:
    // ★ 测试入口(QGraphicsItem 的 mouse*Event 是 protected,测试要能调)
    void handleMousePress(QPointF scenePos);
    void handleMouseMove(QPointF scenePos, Qt::KeyboardModifiers mods);
    void handleMouseRelease(QPointF scenePos);

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent* e) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* e) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* e) override;

    // ★ 方案 A:重写 paint 缩视觉到 4x4(贴边),shape 保持 8x8(易点中)
    void paint(QPainter* painter,
               const QStyleOptionGraphicsItem* option,
               QWidget* widget = nullptr) override;
    QPainterPath shape() const override;

private:
    void applyResize(const QPointF& delta, bool shift);
    // ★ 根据【旋转后视觉上所处的位置】决定光标,而非构造时的 type_
    void applyCursorForRotation();

    HandleType type_;
    QGraphicsItem* target_ = nullptr;

    // ★ mousePress 时的快照
    QPointF pressScenePos_;
    QSizeF  originalSize_;
    QPointF originalTargetPos_;
};

} // namespace qwhmi::editor
