// ============================================================================
// 阶段 E · E.1 · ImageControl(元件 — 图片)
//
// 文件职责:显示一张图片(PNG/JPG/SVG)。
// ============================================================================

#pragma once

#include "widgets/base/ControlBase.h"
#include <QPixmap>

namespace qwhmi::widgets {

class ImageControl : public ControlBase {
    Q_OBJECT
    Q_PROPERTY(QString imagePath READ imagePath WRITE setImagePath NOTIFY imagePathChanged)
    Q_PROPERTY(bool scaleToFit READ scaleToFit WRITE setScaleToFit NOTIFY scaleToFitChanged)

public:
    explicit ImageControl(QGraphicsItem* parent = nullptr);
    ~ImageControl() override;

    QString imagePath() const { return imagePath_; }
    void    setImagePath(const QString& path);
    bool    scaleToFit() const { return scaleToFit_; }
    void    setScaleToFit(bool s);

    QRectF boundingRect() const override;
    void   paint(QPainter* painter,
                 const QStyleOptionGraphicsItem* option,
                 QWidget* widget = nullptr) override;

signals:
    void imagePathChanged();
    void scaleToFitChanged();

private:
    QString  imagePath_;
    QPixmap  pixmap_;
    bool     scaleToFit_ = true;
};

} // namespace qwhmi::widgets