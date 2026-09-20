// ============================================================================
// 阶段 D · D.2 · TrendWidget 实现
// 对应 TrendWidget.h
// ============================================================================

#include "widgets/TrendWidget.h"
#include "../../Variable.h"
#include "../../VariableManager.h"
#include "../history/HistoryRecorder.h"

#include "qcustomplot.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>
#include <QVariant>

namespace qwhmi::widgets {

TrendWidget::TrendWidget(QWidget* parent)
    : QWidget(parent)
{
    // ★ 整体布局:顶栏(标题 + 范围下拉) + QCustomPlot
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(4, 4, 4, 4);

    auto* topBar = new QHBoxLayout;
    titleLabel_ = new QLabel("(未选变量)", this);
    titleLabel_->setStyleSheet("font-weight: bold; color: #333;");
    topBar->addWidget(titleLabel_);

    topBar->addStretch();

    rangeCombo_ = new QComboBox(this);
    rangeCombo_->addItem("最近 1 小时",  static_cast<int>(Range::Hour));
    rangeCombo_->addItem("最近 1 天",   static_cast<int>(Range::Day));
    rangeCombo_->addItem("最近 1 周",   static_cast<int>(Range::Week));
    rangeCombo_->setCurrentIndex(0);   // 默认 Hour
    connect(rangeCombo_, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int idx) {
                range_ = static_cast<Range>(rangeCombo_->itemData(idx).toInt());
                refresh();
            });
    topBar->addWidget(new QLabel("时间范围:", this));
    topBar->addWidget(rangeCombo_);

    root->addLayout(topBar);

    // ★ QCustomPlot
    plot_ = new QCustomPlot(this);
    plot_->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom);
    plot_->xAxis->setLabel("时间");
    plot_->yAxis->setLabel("值");
    // ★ QCustomPlot v2.1.1:用 QCPAxisTickerDateTime 替换默认 ticker,设时间格式
    //   setTicker 收 QSharedPointer;传递后 QCustomPlot 接管所有权,无需手动 delete
    auto dateTimeTicker = QSharedPointer<QCPAxisTickerDateTime>::create();
    dateTimeTicker->setDateTimeFormat("HH:mm:ss");
    plot_->xAxis->setTicker(dateTimeTicker);
    graph_ = plot_->addGraph();
    graph_->setName("实时+历史");
    graph_->setLineStyle(QCPGraph::lsLine);
    graph_->setScatterStyle(QCPScatterStyle::ssDisc);
    graph_->setPen(QPen(QColor(0, 100, 200), 1.5));

    root->addWidget(plot_, /*stretch=*/1);
}

TrendWidget::~TrendWidget() = default;

void TrendWidget::setHistoryRecorder(qwhmi::history::HistoryRecorder* hr) {
    history_ = hr;
}

void TrendWidget::setVariableManager(VariableManager* vm) {
    // ★ 解绑旧的 variableReplaced(切换 vm 时清理)
    if (varMgr_ && varMgr_ != vm) {
        disconnect(varMgr_, nullptr, this, nullptr);
    }
    varMgr_ = vm;

    if (!varMgr_) return;

    // ★ 监听变量替换(自动换绑 valueChanged 信号)
    connect(varMgr_, &VariableManager::variableReplaced, this,
            [this](Variable* oldVar, Variable* newVar) {
                if (oldVar) disconnect(oldVar, nullptr, this, nullptr);
                if (newVar && newVar->plcId() == plcId_ && newVar->name() == varName_) {
                    connect(newVar, &Variable::valueChanged,
                            this, &TrendWidget::onVariableChanged);
                }
            });

    // ★ 当前已有变量 → connect
    if (!plcId_.isEmpty() && !varName_.isEmpty()) {
        for (Variable* v : varMgr_->allVariables()) {
            if (v->plcId() == plcId_ && v->name() == varName_) {
                connect(v, &Variable::valueChanged,
                        this, &TrendWidget::onVariableChanged);
                break;
            }
        }
    }
}

void TrendWidget::setVariable(const QString& plcId, const QString& varName) {
    // ★ 切变量 → 解旧 + 接新
    if (varMgr_) {
        for (Variable* v : varMgr_->allVariables()) {
            disconnect(v, nullptr, this, nullptr);
        }
    }

    plcId_ = plcId;
    varName_ = varName;
    currentVarName_ = plcId + "." + varName;

    titleLabel_->setText(QString("%1 · %2").arg(plcId_, varName_));

    if (varMgr_) {
        for (Variable* v : varMgr_->allVariables()) {
            if (v->plcId() == plcId_ && v->name() == varName_) {
                connect(v, &Variable::valueChanged,
                        this, &TrendWidget::onVariableChanged);
                break;
            }
        }
    }

    refresh();
}

void TrendWidget::setRange(Range r) {
    range_ = r;
    const int idx = rangeCombo_->findData(static_cast<int>(r));
    if (idx >= 0 && idx != rangeCombo_->currentIndex()) {
        rangeCombo_->setCurrentIndex(idx);   // 触发 currentIndexChanged → refresh
    } else {
        refresh();   // 直接 refresh(避免循环)
    }
}

void TrendWidget::refresh() {
    reloadFromHistory();
}

void TrendWidget::clearHistory() {
    if (graph_) graph_->data()->clear();
    if (plot_)  plot_->replot();
}

int TrendWidget::dataCount() const {
    return graph_ ? graph_->data()->size() : 0;
}

QPair<QDateTime, QDateTime> TrendWidget::rangeWindow() const {
    const QDateTime to = QDateTime::currentDateTime();
    QDateTime from;
    switch (range_) {
        case Range::Hour: from = to.addSecs(-3600); break;
        case Range::Day:  from = to.addSecs(-86400); break;
        case Range::Week: from = to.addSecs(-7 * 86400); break;
    }
    return {from, to};
}

void TrendWidget::reloadFromHistory() {
    if (!history_ || !graph_) return;
    if (currentVarName_.isEmpty()) return;

    graph_->data()->clear();

    const auto [from, to] = rangeWindow();
    const auto records = history_->query(currentVarName_, from, to);

    for (const auto& [ts, val] : records) {
        graph_->addData(ts.toMSecsSinceEpoch(), val);
    }

    // ★ X 轴范围 = 时间窗(留 5% 边距)
    const qint64 span = from.msecsTo(to);
    plot_->xAxis->setRange(from.toMSecsSinceEpoch() - span / 20,
                          to.toMSecsSinceEpoch()   + span / 20);
    plot_->yAxis->rescale();
    plot_->replot();

    qInfo().noquote() << "[TrendWidget]" << currentVarName_
                      << "range=" << static_cast<int>(range_)
                      << "loaded" << records.size() << "points";
}

void TrendWidget::onVariableChanged(const Variable& v) {
    if (!graph_ || !plot_) return;
    if (v.plcId() != plcId_ || v.name() != varName_) return;

    const auto [from, to] = rangeWindow();
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();

    // ★ 只在时间窗内追加(范围外的不入 graph)
    if (nowMs < from.toMSecsSinceEpoch() || nowMs > to.toMSecsSinceEpoch()) {
        return;
    }

    graph_->addData(nowMs, v.value().toDouble());
    plot_->replot(QCustomPlot::rpQueuedReplot);   // 异步重画,不卡 UI
}

} // namespace qwhmi::widgets