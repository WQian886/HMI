// ============================================================================
// TableControl 实现
// ============================================================================

#include "widgets/TableControl.h"

#include <QGraphicsProxyWidget>
#include <QHeaderView>
#include <QTableWidget>

namespace qwhmi::widgets {

TableControl::TableControl(QGraphicsItem* parent)
    : ControlBase(parent)
{
    setSize(QSizeF(300, 120));
    setFlag(QGraphicsItem::ItemIsMovable, true);
    setFlag(QGraphicsItem::ItemIsSelectable, true);

    table_ = new QTableWidget;
    table_->setColumnCount(headers_.size());
    table_->setHorizontalHeaderLabels(headers_);
    table_->setMinimumSize(static_cast<int>(size().width()),
                          static_cast<int>(size().height()));
    table_->horizontalHeader()->setStretchLastSection(true);

    proxy_ = new QGraphicsProxyWidget(this);
    proxy_->setWidget(table_);
    proxy_->setAcceptedMouseButtons(Qt::NoButton);   // ★ 同源修复:Table 也吞事件

    registerControlType("Table", []() -> ControlBase* { return new TableControl(); });
}

TableControl::~TableControl() = default;

void TableControl::setHeaders(const QStringList& h) {
    if (h == headers_) return;
    headers_ = h;
    if (table_) {
        table_->setColumnCount(h.size());
        table_->setHorizontalHeaderLabels(h);
    }
    emit headersChanged();
}

void TableControl::setCell(int row, int col, const QString& value) {
    if (!table_) return;
    if (row >= table_->rowCount()) table_->setRowCount(row + 1);
    if (col >= table_->columnCount()) table_->setColumnCount(col + 1);
    auto* item = table_->item(row, col);
    if (!item) {
        item = new QTableWidgetItem(value);
        table_->setItem(row, col, item);
    } else {
        item->setText(value);
    }
}

QRectF TableControl::boundingRect() const {
    return QRectF(0, 0, size().width(), size().height());
}

void TableControl::paint(QPainter* p,
                         const QStyleOptionGraphicsItem* option,
                         QWidget* widget) {
    Q_UNUSED(p); Q_UNUSED(option); Q_UNUSED(widget);
    // ★ proxy 自己渲染 — paint 不画东西
}

} // namespace qwhmi::widgets