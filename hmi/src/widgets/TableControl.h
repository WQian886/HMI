// ============================================================================
// 阶段 E · E.1 · TableControl(数据 — 表格)
//
// 文件职责:用 QGraphicsProxyWidget 包 QTableWidget — 多列历史数据展示
//
// 关联:ControlBase.h
// ============================================================================

#pragma once

#include "widgets/base/ControlBase.h"
#include <QStringList>

class QTableWidget;
class QGraphicsProxyWidget;

namespace qwhmi::widgets {

class TableControl : public ControlBase {
    Q_OBJECT
    Q_PROPERTY(QStringList headers READ headers WRITE setHeaders NOTIFY headersChanged)

public:
    explicit TableControl(QGraphicsItem* parent = nullptr);
    ~TableControl() override;

    QStringList headers() const { return headers_; }
    void        setHeaders(const QStringList& h);

    // ★ 2026-10-14:给测量型测试用的只读出口。
    //   列宽这件事【测不到就等于没做】—— 列头是用户配的,哪列最宽
    //   事前不知道,只有把真实控件量一遍才说得清。
    //   ★ 为什么不用"在测试里复刻一份 QTableWidget 配置"(F5-a 的做法):
    //     复刻的那份会和生产代码漂移 —— 改了 TableControl 忘了改测试里的
    //     副本,测试照样绿,而用户看到的表格还是坏的。这里直接读真控件。
    const QTableWidget* tableWidgetForTest() const { return table_; }

    // ★ 数据行(row, col, value)
    void setCell(int row, int col, const QString& value);

    QRectF boundingRect() const override;
    void paintContent(QPainter* painter,
                 const QStyleOptionGraphicsItem* option,
                 QWidget* widget = nullptr) override;

signals:
    void headersChanged();

private:
    // ★ 2026-10-14:列宽策略(实测发现原来的 setStretchLastSection(true) 不起作用)
    void applyColumnSizing();

    QTableWidget*         table_   = nullptr;
    QGraphicsProxyWidget* proxy_   = nullptr;
    QStringList           headers_ = { "时间", "值" };
};

} // namespace qwhmi::widgets