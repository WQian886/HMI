// ============================================================================
// 阶段 E · E.1 · LabelControl(显示类 — 静态文字)
//
// 文件职责:E.1a 6 个显示控件第 1 个。最简单:画一行文字(可绑 Variable)。
//
// 关联:ControlBase.h, LabelControl.cpp, tst_LabelControl
// ============================================================================

#pragma once

#include "widgets/base/ControlBase.h"

namespace qwhmi::widgets {

class LabelControl : public ControlBase {
    Q_OBJECT
public:
    explicit LabelControl(QGraphicsItem* parent = nullptr);
    ~LabelControl() override;

    QRectF boundingRect() const override;
    void   paint(QPainter* painter,
                 const QStyleOptionGraphicsItem* option,
                 QWidget* widget = nullptr) override;
};

} // namespace qwhmi::widgets