// ============================================================================
// 阶段 E · E.1 · RectangleControl(元件 — 矩形)
//
// 文件职责:装饰性矩形,可调 fill/stroke。
// ============================================================================

#pragma once

#include "widgets/base/ControlBase.h"
#include <QColor>

namespace qwhmi::widgets {

class RectangleControl : public ControlBase {
    Q_OBJECT
    Q_PROPERTY(QColor fillColor READ fillColor WRITE setFillColor NOTIFY fillColorChanged)
    Q_PROPERTY(QColor strokeColor READ strokeColor WRITE setStrokeColor NOTIFY strokeColorChanged)
    Q_PROPERTY(int strokeWidth READ strokeWidth WRITE setStrokeWidth NOTIFY strokeWidthChanged)

public:
    explicit RectangleControl(QGraphicsItem* parent = nullptr);
    ~RectangleControl() override;

    QColor fillColor() const { return fillColor_; }
    void   setFillColor(const QColor& c);
    QColor strokeColor() const { return strokeColor_; }
    void   setStrokeColor(const QColor& c);
    int    strokeWidth() const { return strokeWidth_; }
    void   setStrokeWidth(int w);

    QRectF boundingRect() const override;
    void paintContent(QPainter* painter,
                 const QStyleOptionGraphicsItem* option,
                 QWidget* widget = nullptr) override;

signals:
    void fillColorChanged();
    void strokeColorChanged();
    void strokeWidthChanged();

private:
    QColor fillColor_ = QColor(200, 220, 240);
    QColor strokeColor_ = QColor(80, 80, 80);
    int    strokeWidth_ = 1;
};

} // namespace qwhmi::widgets