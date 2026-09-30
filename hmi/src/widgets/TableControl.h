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

    // ★ 数据行(row, col, value)
    void setCell(int row, int col, const QString& value);

    QRectF boundingRect() const override;
    void paintContent(QPainter* painter,
                 const QStyleOptionGraphicsItem* option,
                 QWidget* widget = nullptr) override;

signals:
    void headersChanged();

private:
    QTableWidget*         table_   = nullptr;
    QGraphicsProxyWidget* proxy_   = nullptr;
    QStringList           headers_ = { "时间", "值" };
};

} // namespace qwhmi::widgets