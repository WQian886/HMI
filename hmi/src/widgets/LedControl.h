// ============================================================================
// 阶段 E · E.1 · LedControl(显示类 — 圆点 LED)
//
// 文件职责:布尔指示灯(绿/红/灰)— value>0 亮绿,=0 暗灰,可单独切色。
//
// 关联:ControlBase.h, LedControl.cpp
// ============================================================================

#pragma once

#include "widgets/base/ControlBase.h"
#include <QColor>

namespace qwhmi::widgets {

class LedControl : public ControlBase {
    Q_OBJECT
    Q_PROPERTY(QColor onColor  READ onColor  WRITE setOnColor  NOTIFY onColorChanged)
    Q_PROPERTY(QColor offColor READ offColor WRITE setOffColor NOTIFY offColorChanged)

public:
    explicit LedControl(QGraphicsItem* parent = nullptr);
    ~LedControl() override;

    QColor onColor()  const { return onColor_; }
    void   setOnColor(const QColor& c);
    QColor offColor() const { return offColor_; }
    void   setOffColor(const QColor& c);

    // ★ 亮 = value > 0
    bool isLit() const { return value().toDouble() > 0.0; }

    QRectF boundingRect() const override;
    void   paint(QPainter* painter,
                 const QStyleOptionGraphicsItem* option,
                 QWidget* widget = nullptr) override;

signals:
    void onColorChanged();
    void offColorChanged();

private:
    QColor onColor_  = QColor(40, 200, 40);    // 默认绿
    QColor offColor_ = QColor(80, 80, 80);     // 默认灰
};

} // namespace qwhmi::widgets