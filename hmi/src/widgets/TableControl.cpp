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
    applyColumnSizing();

    proxy_ = new QGraphicsProxyWidget(this);
    proxy_->setWidget(table_);
    proxy_->setAcceptedMouseButtons(Qt::NoButton);   // ★ 同源修复:Table 也吞事件

    // ★ 修复:监听 sizeChanged → 同时调 resize,强制 widget 跟缩(否则 user 看到 widget 视觉不缩)
    connect(this, &ControlBase::sizeChanged, this, [this]() {
        if (table_) {
            const int w = static_cast<int>(size().width());
            const int h = static_cast<int>(size().height());
            table_->setMinimumSize(w, h);
            table_->resize(w, h);
        }
    });

    registerControlType("Table", []() -> ControlBase* { return new TableControl(); });
}

TableControl::~TableControl() = default;

// ★ 2026-10-14:修列宽 —— 实测发现原来的 setStretchLastSection(true) 在这里
//   根本拿不到余量,所以一列都没被拉伸。
//
// 【实测基线(改之前)】300px 表格 + 3 列(点位名称/值/状态):
//     列0 = 100px   列1 = 100px   列2 = 100px
//     合计 300,而 viewport 只有 282(差 18px 是 Qt 给垂直滚动条预留的)
//     →  列比可视区【还宽 18px】
//   而「3#锅炉给水温度高高报警」需要 144px(实测 QFontMetrics 数字)
//     —— 列0 只有 100px,【被截断】。
//
// 【为什么原来不生效】默认 resizeMode 是 QHeaderView::Interactive,
//   三列都被平均分,stretchLastSection 拿不到任何余量 —— 那句话写了等于没写。
//
// ★ 顺带记一条我自己算错过的地方:我一开始写的是"空余 0px"。
//   那是拿 table->width()=300 当分母算的。QHeaderView 的列宽按
//   【viewport】分配,不是按整个 widget —— 正确分母是 282,
//   实际是【溢出 18px】而不是空余 0。三列均分不关心可视区有多窄。
//
// 【改法】照搬 F5-a 已验证的做法(AlarmListControl):
//     每列 ResizeToContents(按内容自己取宽)
//     最后一列 Stretch(吃掉剩余空间)
//     stretchLastSection(false) —— 必须关掉,否则和指定列 Stretch 打架
//
// 【为什么不指定"哪列最重要】列头是用户配的(headers_ 运行期可变),
//   写死索引就是写死假设。ResizeToContents 之后各列已按内容取宽,
//   最后一列再吃掉余量,任何列序都不会把别列挤到放不下内容。
void TableControl::applyColumnSizing() {
    if (!table_) return;
    auto* header = table_->horizontalHeader();
    // 全部列先按内容取宽
    header->setSectionResizeMode(QHeaderView::ResizeToContents);
    // 余量给最后一列(列数为 0 时不设,否则 setSectionResizeMode 会越界)
    const int last = table_->columnCount() - 1;
    if (last >= 0) {
        header->setSectionResizeMode(last, QHeaderView::Stretch);
        header->setStretchLastSection(false);
    }
}

void TableControl::setHeaders(const QStringList& h) {
    if (h == headers_) return;
    headers_ = h;
    if (table_) {
        table_->setColumnCount(h.size());
        table_->setHorizontalHeaderLabels(h);
        // ★ 列数变了必须重设 —— 否则用户改了表头就退回旧策略。
        //   setSectionResizeMode 是按【列索引】设的,setColumnCount 后
        //   新的最后一列没有 Stretch,余量就又漏掉了。
        applyColumnSizing();
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

void TableControl::paintContent(QPainter* p,
                         const QStyleOptionGraphicsItem* option,
                         QWidget* widget) {
    Q_UNUSED(p); Q_UNUSED(option); Q_UNUSED(widget);
    // ★ proxy 自己渲染 — paint 不画东西
}

} // namespace qwhmi::widgets