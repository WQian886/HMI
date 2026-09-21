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

namespace qwhmi::widgets { class ControlBase; }

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
                               qwhmi::widgets::ControlBase* target,
                               QGraphicsItem* parent = nullptr);

    HandleType handleType() const { return type_; }
    qwhmi::widgets::ControlBase* target() const { return target_; }

    // ★ 给 EditorMainWindow 调 — 把 8 个 handle 摆到目标 boundingRect 的 8 个位置
    //   假设 8 个 handle 都是 target 的 child,按 HandleType 顺序对应 8 个角/边
    static void layoutHandlesFor(qwhmi::widgets::ControlBase* target);

    // ★ 测试 + 通用:为目标挂上 8 个 handle(已挂则跳过)
    static void installHandlesFor(qwhmi::widgets::ControlBase* target);

    // ★ 反向 — 移除目标上的所有 handle
    static void removeHandlesFor(qwhmi::widgets::ControlBase* target);

public:
    // ★ 测试入口(QGraphicsItem 的 mouse*Event 是 protected,测试要能调)
    void handleMousePress(QPointF scenePos);
    void handleMouseMove(QPointF scenePos, Qt::KeyboardModifiers mods);
    void handleMouseRelease(QPointF scenePos);

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent* e) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* e) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* e) override;

private:
    void applyResize(const QPointF& delta, bool shift);

    HandleType type_;
    qwhmi::widgets::ControlBase* target_ = nullptr;

    // ★ mousePress 时的快照
    QPointF pressScenePos_;
    QSizeF  originalSize_;
    QPointF originalTargetPos_;
};

} // namespace qwhmi::editor
