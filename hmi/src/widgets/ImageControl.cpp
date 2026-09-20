// ============================================================================
// ImageControl 实现
// ============================================================================

#include "widgets/ImageControl.h"

#include <QPainter>

namespace qwhmi::widgets {

ImageControl::ImageControl(QGraphicsItem* parent)
    : ControlBase(parent)
{
    setSize(QSizeF(80, 80));
    registerControlType("Image", []() -> ControlBase* { return new ImageControl(); });
}

ImageControl::~ImageControl() = default;

void ImageControl::setImagePath(const QString& path) {
    if (path == imagePath_) return;
    imagePath_ = path;
    pixmap_ = path.isEmpty() ? QPixmap() : QPixmap(path);
    update();
    emit imagePathChanged();
}

void ImageControl::setScaleToFit(bool s) {
    if (s == scaleToFit_) return;
    scaleToFit_ = s;
    update();
    emit scaleToFitChanged();
}

QRectF ImageControl::boundingRect() const {
    return QRectF(0, 0, size().width(), size().height());
}

void ImageControl::paint(QPainter* p,
                         const QStyleOptionGraphicsItem* /*option*/,
                         QWidget* /*widget*/) {
    if (!pixmap_.isNull()) {
        if (scaleToFit_) {
            p->drawPixmap(boundingRect().toRect(), pixmap_);
        } else {
            p->drawPixmap(0, 0, pixmap_);
        }
    } else {
        // 占位灰底 + 文字
        p->setPen(QColor(160, 160, 160));
        p->setBrush(QColor(245, 245, 245));
        p->drawRect(boundingRect());
        p->drawText(boundingRect(), Qt::AlignCenter,
                    imagePath_.isEmpty() ? "[Image]" : imagePath_);
    }

    // 选中时画蓝色边框
    if (isSelected()) {
        QPen pen(QColor(0, 120, 215));
        pen.setWidth(2);
        pen.setStyle(Qt::DashLine);
        p->setPen(pen);
        p->setBrush(Qt::NoBrush);
        p->drawRect(boundingRect());
    }
}

} // namespace qwhmi::widgets