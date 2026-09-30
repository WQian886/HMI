// ============================================================================
// 阶段 E · E.1 · LineControl(元件 — 直线)
//
// 文件职责:分隔线/箭头。width 取 size.width,起点 (0, size.height/2)。
// ============================================================================

#pragma once

#include "widgets/base/ControlBase.h"
#include <QColor>

namespace qwhmi::widgets {

class LineControl : public ControlBase {
    Q_OBJECT
    Q_PROPERTY(QColor strokeColor READ strokeColor WRITE setStrokeColor NOTIFY strokeColorChanged)
    Q_PROPERTY(int strokeWidth READ strokeWidth WRITE setStrokeWidth NOTIFY strokeWidthChanged)

public:
    explicit LineControl(QGraphicsItem* parent = nullptr);
    ~LineControl() override;

    QColor strokeColor() const { return strokeColor_; }
    void   setStrokeColor(const QColor& c);
    int    strokeWidth() const { return strokeWidth_; }
    void   setStrokeWidth(int w);

    QRectF boundingRect() const override;
    void paintContent(QPainter* painter,
                 const QStyleOptionGraphicsItem* option,
                 QWidget* widget = nullptr) override;

signals:
    void strokeColorChanged();
    void strokeWidthChanged();

private:
    QColor strokeColor_ = QColor(80, 80, 80);
    int    strokeWidth_ = 2;
};

} // namespace qwhmi::widgets