// ============================================================================
// 阶段 E · E.1 · TrendControl(数据 — 实时曲线)
//
// 文件职责:包 QCustomPlot(QCustomPlot 实例嵌在 ControlBase 里)— 复用 D.2 TrendWidget 设计
//   不重新画 QCustomPlot,直接嵌 widget(损失性能但简单)
//
// 关联:ControlBase.h, TrendControl.cpp, qcustomplot.h
// ============================================================================

#pragma once

#include "widgets/base/ControlBase.h"

class QCustomPlot;
class QGraphicsProxyWidget;

namespace qwhmi::widgets {

class TrendControl : public ControlBase {
    Q_OBJECT
    Q_PROPERTY(double rangeMinutes READ rangeMinutes WRITE setRangeMinutes NOTIFY rangeMinutesChanged)

public:
    explicit TrendControl(QGraphicsItem* parent = nullptr);
    ~TrendControl() override;

    double rangeMinutes() const { return rangeMinutes_; }
    void   setRangeMinutes(double m);

    // ★ 重写 — 调 TrendWidget 的 query 拉历史
    void   refresh(const QString& variableName);

    QRectF boundingRect() const override;
    void paintContent(QPainter* painter,
                 const QStyleOptionGraphicsItem* option,
                 QWidget* widget = nullptr) override;

signals:
    void rangeMinutesChanged();

private:
    QCustomPlot*           plot_   = nullptr;
    QGraphicsProxyWidget*   proxy_  = nullptr;
    double                  rangeMinutes_ = 5.0;    // 默认最近 5 分钟
};

} // namespace qwhmi::widgets