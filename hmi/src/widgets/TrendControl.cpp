// ============================================================================
// TrendControl 实现
// ============================================================================

#include "widgets/TrendControl.h"
#include "qcustomplot.h"

#include <QGraphicsProxyWidget>

namespace qwhmi::widgets {

TrendControl::TrendControl(QGraphicsItem* parent)
    : ControlBase(parent)
{
    setSize(QSizeF(280, 100));   // 默认曲线大小
    setFlag(QGraphicsItem::ItemIsMovable, true);
    setFlag(QGraphicsItem::ItemIsSelectable, true);

    // ★ 用 QGraphicsProxyWidget 包 QCustomPlot(损失些性能,简单)
    plot_ = new QCustomPlot;
    plot_->setMinimumSize(static_cast<int>(size().width()),
                          static_cast<int>(size().height()));
    // 配置基本外观
    plot_->xAxis->setLabel("时间");
    plot_->yAxis->setLabel("值");
    plot_->addGraph();
    plot_->graph(0)->setName("实时");
    plot_->graph(0)->setPen(QPen(QColor(0, 120, 215), 1.5));

    proxy_ = new QGraphicsProxyWidget(this);
    proxy_->setWidget(plot_);
    proxy_->setAcceptedMouseButtons(Qt::NoButton);   // ★ 同源修复:Trend 也吞事件

    // ★ 修复:E.4b 缩放后 proxy widget 不跟变 — 监听 sizeChanged 更新 plot minSize + resize
    //   setMinimumSize 只能限制最小值,不强制 resize;必须同时调 resize 才能让 widget
    //   视觉跟缩,否则用户看到的是 "data 缩了但 widget 视觉没缩" 的撕裂状态
    connect(this, &ControlBase::sizeChanged, this, [this]() {
        if (plot_) {
            const int w = static_cast<int>(size().width());
            const int h = static_cast<int>(size().height());
            plot_->setMinimumSize(w, h);
            plot_->resize(w, h);
        }
    });

    registerControlType("Trend", []() -> ControlBase* { return new TrendControl(); });
}

TrendControl::~TrendControl() = default;

void TrendControl::setRangeMinutes(double m) {
    if (qFuzzyCompare(m, rangeMinutes_)) return;
    rangeMinutes_ = m;
    emit rangeMinutesChanged();
}

void TrendControl::refresh(const QString& variableName) {
    if (!plot_) return;
    setVariableName(variableName);
    // ★ 简化:显示一个静态 sin 波演示(E.4 编辑器接 HistoryRecorder 后改)
    QVector<double> xs, ys;
    const int n = 100;
    const double now = QDateTime::currentMSecsSinceEpoch() / 1000.0;
    for (int i = 0; i < n; ++i) {
        xs.append(now - (n - i));
        ys.append(qSin((now + i) * 0.5) * 100);
    }
    plot_->graph(0)->setData(xs, ys);
    plot_->rescaleAxes();
    plot_->replot();
}

QRectF TrendControl::boundingRect() const {
    return QRectF(0, 0, size().width(), size().height());
}

void TrendControl::paintContent(QPainter* p,
                         const QStyleOptionGraphicsItem* option,
                         QWidget* widget) {
    Q_UNUSED(p); Q_UNUSED(option); Q_UNUSED(widget);
    // ★ proxy 自己渲染 QCustomPlot — paint 不画东西
}

} // namespace qwhmi::widgets