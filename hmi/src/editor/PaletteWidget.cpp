// ============================================================================
// PaletteWidget 实现
// ============================================================================

#include "editor/PaletteWidget.h"
#include "widgets/base/ControlBase.h"

#include <QDrag>
#include <QMimeData>

namespace qwhmi::editor {

PaletteWidget::PaletteWidget(QWidget* parent)
    : QListWidget(parent) {
    setDragEnabled(true);
    setDragDropMode(QAbstractItemView::DragOnly);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setUniformItemSizes(true);
    setAlternatingRowColors(true);

    // ★ 遍历注册表填项
    const QStringList types = qwhmi::widgets::ControlBase::registeredTypeNames();
    for (const QString& type : types) {
        // ★ 每项 text = 类型名,Data = 类型名(给 mime 用)
        QListWidgetItem* item = new QListWidgetItem(type, this);
        item->setData(Qt::UserRole, type);
        item->setToolTip(tr("拖到画布添加 %1").arg(type));
    }
}

PaletteWidget::~PaletteWidget() = default;

void PaletteWidget::startDrag(Qt::DropActions supportedActions) {
    QListWidgetItem* item = currentItem();
    if (!item) return;
    const QString typeName = item->data(Qt::UserRole).toString();
    if (typeName.isEmpty()) return;

    // ★ 构造 mime:自定义 type + 文本(双保险)
    auto* mime = new QMimeData;
    mime->setData(controlMimeType(), typeName.toUtf8());
    mime->setText(typeName);

    auto* drag = new QDrag(this);
    drag->setMimeData(mime);
    drag->setPixmap(QPixmap());   // 后续可换控件缩略图
    drag->exec(supportedActions, Qt::CopyAction);
}

} // namespace qwhmi::editor
