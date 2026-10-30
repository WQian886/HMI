// ============================================================================
// 阶段 D · D.2 · 历史曲线控件(QCustomPlot)
//
// 文件职责:从 HistoryRecorder::query 取历史数据,在 QCustomPlot 上画折线;
//   监听 VariableManager::valueChanged 实时追加点;时间范围 1h/1d/1w 可切换。
//
// 关键设计:
//   - QCustomPlot(LGPL v2.1)— 已下到 hmi/3rdparty/qcustomplot/
//   - 数据 = (timestamp_ms, value);折线图;X 轴 = 时间(自动格式),Y 轴 = 值
//   - 历史 + 实时共用 1 个 QCPGraph(query 返回值 + 实时追加都 addData 到同一 graph)
//   - 切时间范围 → 重新 query + 重画 graph
//   - 实时点:监 valueChanged,只在 [from, to] 范围内才 add(避免范围外的点无限增长)
//   - D.3 报警报表会复用 TrendWidget 模板
//
// 关联:D.1 HistoryRecorder, D.3 AlarmRecorder, hmi.cpp
// ============================================================================

#pragma once

#include <QWidget>
#include <QString>
#include <QDateTime>
#include <QVector>
#include <QPair>

class QCustomPlot;
class QCPGraph;
class QComboBox;
class QLabel;
namespace qwhmi::history { class HistoryRecorder; }
class Variable;
class VariableManager;

namespace qwhmi::widgets {

class TrendWidget : public QWidget {
    Q_OBJECT
public:
    // ★ 时间范围(决定 query 窗口 + X 轴显示)
    enum class Range { Hour, Day, Week };
    Q_ENUM(Range)

    explicit TrendWidget(QWidget* parent = nullptr);
    ~TrendWidget() override;

    // ★ 必接 — 缺一不可(否则曲线为空)
    void setHistoryRecorder(qwhmi::history::HistoryRecorder* hr);
    void setVariableManager(VariableManager* vm);

    // ★ D.2 主接口
    //   plcId + varName:如 "plc1" + "Speed" → query key = "plc1.Speed"
    void setVariable(const QString& plcId, const QString& varName);
    void setRange(Range r);

    // ★ 测试用 getter
    int dataCount() const;            // 当前 graph 数据点总数
    QString currentVariable() const { return currentVarName_; }
    Range currentRange() const { return range_; }

public slots:
    // ★ 重新拉历史 query → 重画 graph(范围变化 / 手动刷新)
    void refresh();
    // ★ 清空 graph(测试 + 切工程时用)
    void clearHistory();

    // ★ 内部槽:VariableManager 数据源触发(由 setVariableManager 内部 connect)
    void onVariableChanged(const Variable& v);

private:
    // ★ 计算时间窗口 from/to(QDateTime)
    //   ★ now 由调用方传进来,内部【不许】再取一次时钟。
    //     原因:取两次的话,两次之间跨过 1 毫秒边界,
    //     数据点的时间戳就会比窗口上界还大 → 被判成"窗外"丢掉。
    //     详见 .cpp 里 onVariableChanged 那处的注释。
    QPair<QDateTime, QDateTime> rangeWindow(const QDateTime& now) const;

    // ★ 查询 + 重画 graph
    void reloadFromHistory();

    QCustomPlot* plot_ = nullptr;
    QCPGraph*    graph_ = nullptr;
    QComboBox*   rangeCombo_ = nullptr;     // "1 小时 / 1 天 / 1 周"
    QLabel*      titleLabel_ = nullptr;      // 显示 "plc1.Speed · 最近 1 小时"

    qwhmi::history::HistoryRecorder* history_ = nullptr;
    VariableManager* varMgr_ = nullptr;

    QString plcId_;
    QString varName_;
    QString currentVarName_;    // "plc1.Speed" — 给 query 用
    Range   range_ = Range::Hour;
};

} // namespace qwhmi::widgets