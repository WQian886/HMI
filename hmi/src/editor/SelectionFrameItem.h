// ============================================================================
// 阶段 E · E.4d 续 · SelectionFrameItem(选中态视觉统一)
//
// 文件职责:QGraphicsRectItem 子类,在选中控件周围画蓝色虚线边框,作为
//   "选中态"的统一视觉反馈。之前每个控件自己实现 isSelected 视觉,五花八门:
//   Button 只调亮 10%(看不出),Bar/Label 改 stroke 颜色但很多控件没 stroke,
//   Image/Led 画虚线/实线边框。
//
// 优点:与具体控件解耦,EditorMainWindow 统一管理,新增控件零成本支持。
//
// 关联:EditorMainWindow.cpp::onSceneSelectionChanged(装/卸 + 更新位置)
// ============================================================================

#pragma once

#include <QGraphicsRectItem>

namespace qwhmi::editor {

class SelectionFrameItem : public QGraphicsRectItem {
public:
    static constexpr qreal FRAME_WIDTH  = 2.0;    // 边框宽(px)
    static constexpr int    PEN_Z_VALUE = 200;   // 永远在最上

    explicit SelectionFrameItem(QGraphicsItem* parent = nullptr);
};

} // namespace qwhmi::editor
