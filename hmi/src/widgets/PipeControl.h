// ============================================================================
// 阶段 E · E.1c · PipeControl(显示类 — 管道)
//
// 文件职责:管身 + 流向箭头。value 表示流量,非 0 时管内画流动箭头。
//
// 【竖管怎么画】默认画竖管(上下走向)。水平管由 E.1c 第 3 块的
//   ControlBase::rotation 属性转 90° 得到 —— 阀门/泵这类都需要转,
//   所以旋转做在基类而不是每加一个控件就各写一遍。
//
// 【为什么管道是只读的】管道本身不是执行机构,没有"开关管道"这回事。
//   要关介质得关两端的阀门。所以这里【不做写回】,跟 TankControl 一样。
//   流量是测量值,操作员改不了。
//
// 关联:ControlBase.h, PipeControl.cpp
// ============================================================================

#pragma once

#include "widgets/base/ControlBase.h"
#include <QColor>

namespace qwhmi::widgets {

class PipeControl : public ControlBase {
    Q_OBJECT
    Q_PROPERTY(QColor pipeColor READ pipeColor WRITE setPipeColor NOTIFY pipeColorChanged)
    Q_PROPERTY(QColor flowColor READ flowColor WRITE setFlowColor NOTIFY flowColorChanged)
    // ★ 管径(像素)。工业管道图靠粗细区分 DN50 / DN100 / DN300,
    //   属性面板能改比做多个尺寸类型方便
    Q_PROPERTY(int thickness READ thickness WRITE setThickness NOTIFY thicknessChanged)

public:
    explicit PipeControl(QGraphicsItem* parent = nullptr);
    ~PipeControl() override;

    QColor pipeColor() const { return pipeColor_; }
    void   setPipeColor(const QColor& c);
    QColor flowColor() const { return flowColor_; }
    void   setFlowColor(const QColor& c);
    int    thickness() const { return thickness_; }
    void   setThickness(int px);

    // ★ 有流量 = value 绝对值大于阈值。取绝对值是因为有的现场用负值
    //   表示反向流动(比如回流管),画出来箭头方向不同但都是"在流"
    bool   isFlowing() const;
    // ★ 反向流动时箭头朝下,正向朝上
    bool   isReversed() const { return value().toDouble() < 0.0; }

    QRectF boundingRect() const override;
    void paintContent(QPainter* painter,
                 const QStyleOptionGraphicsItem* option,
                 QWidget* widget = nullptr) override;

signals:
    void pipeColorChanged();
    void flowColorChanged();
    void thicknessChanged();

private:
    QColor pipeColor_ = QColor(160, 165, 170);   // 管身灰(裸钢)
    QColor flowColor_ = QColor(0, 160, 220);     // 流动蓝
    int    thickness_ = 16;                       // 默认管径 px

    // ★ 管径改了 boundingRect 形状就变了(管身变粗会超出默认尺寸),
    //   paint 前先 prepareGeometryChange —— 跟 ControlBase::setSize 同一理由:
    //   不调的话 BspTreeIndex cache 不更新,缩小/变细后旧区域留残影
    void ensureGeometryFor(int px);
};

} // namespace qwhmi::widgets
