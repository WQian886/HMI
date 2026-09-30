// ============================================================================
// ImageControl 实现
//
// ★ E.8(2026-09-30):图片的加载/缓存/降级全在基类 ControlBase::paint 里,
//   这里只画"没图时的占位"和选中框 —— 图加载成功时本类什么都不画。
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

QRectF ImageControl::boundingRect() const {
    return QRectF(0, 0, size().width(), size().height());
}

void ImageControl::paintContent(QPainter* p,
                                const QStyleOptionGraphicsItem* /*option*/,
                                QWidget* /*widget*/) {
    // ★ 基类已把图画在下层。走到这里且"不可用"只有两种:
    //   ① 没设 imagePath —— 用户还没配图
    //   ② 设了但加载失败 —— 基类已 qWarning
    //   区分二者有意义:①不用管,②得去改路径。原来两种都画同一个 [Image]
    //   灰底,配错了图用户完全看不出来 —— 以为"设了没生效"。
    const bool noPath   = imagePath().isEmpty();
    const bool loadFail = !noPath && !imageLoaded();
    if (noPath || loadFail) {
        p->setPen(loadFail ? QColor(200, 60, 60) : QColor(150, 150, 150));
        p->setBrush(loadFail ? QColor(253, 235, 235) : QColor(245, 245, 245));
        p->drawRect(boundingRect());
        p->drawText(boundingRect(), Qt::AlignCenter | Qt::TextWordWrap,
                    noPath ? tr("[Image]") : tr("[图未加载]"));
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
