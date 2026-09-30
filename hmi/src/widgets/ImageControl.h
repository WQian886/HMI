// ============================================================================
// 阶段 E · E.1 · ImageControl(元件 — 图片)
//
// 文件职责:显示一张图片(PNG/JPG/SVG)。
//
// ★ E.8(2026-09-30)改造:imagePath / scaleToFit 已【下沉到 ControlBase】,
//   本类不再自己存一份。原来它自己声明同名 Q_PROPERTY,基类加上同名属性后
//   就会出现"两份 imagePath、两个 setter" —— 属性面板反射到哪个全看继承顺序,
//   JSON 存读也会出现一个存了一个没存,是个典型的"两个真相"坑。
//   现在只保留"没图时画什么占位"这件本类特有的事。
// ============================================================================

#pragma once

#include "widgets/base/ControlBase.h"

namespace qwhmi::widgets {

class ImageControl : public ControlBase {
    Q_OBJECT

public:
    explicit ImageControl(QGraphicsItem* parent = nullptr);
    ~ImageControl() override;

    QRectF boundingRect() const override;
    void   paintContent(QPainter* painter,
                        const QStyleOptionGraphicsItem* option,
                        QWidget* widget = nullptr) override;
};

} // namespace qwhmi::widgets
