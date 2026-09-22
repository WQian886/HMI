// ============================================================================
// SelectionFrameItem 实现
// ============================================================================

#include "editor/SelectionFrameItem.h"

#include <QPainter>
#include <QPen>

namespace qwhmi::editor {

SelectionFrameItem::SelectionFrameItem(QGraphicsItem* parent)
    : QGraphicsRectItem(parent) {
    // ★ E.4d 选中态统一视觉:2px 蓝色虚线,跟微软 Office/Figma 选中风格一致
    QPen pen(QColor(0, 120, 215));   // #0078D7 微软蓝
    pen.setWidthF(FRAME_WIDTH);
    pen.setCosmetic(true);            // 像素宽度,不随 view zoom 缩放
    pen.setStyle(Qt::DashLine);
    pen.setJoinStyle(Qt::MiterJoin);
    setPen(pen);
    setBrush(Qt::NoBrush);

    // ★ 永远在最上层,不被任何控件遮挡
    setZValue(PEN_Z_VALUE);

    // ★ 不接受鼠标事件(不阻塞点选下面控件)
    setAcceptedMouseButtons(Qt::NoButton);
    setFlag(QGraphicsItem::ItemIsSelectable, false);
    setFlag(QGraphicsItem::ItemIsMovable, false);
}

} // namespace qwhmi::editor
