// ============================================================================
// PositionEditor 实现
// ============================================================================

#include "editor/PositionEditor.h"

#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>

namespace qwhmi::editor {

PositionEditor::PositionEditor(QWidget* parent) : QWidget(parent) {
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);

    auto* xLabel = new QLabel(tr("X:"));
    auto* yLabel = new QLabel(tr("Y:"));
    x_ = new QDoubleSpinBox;
    y_ = new QDoubleSpinBox;
    x_->setDecimals(2);
    y_->setDecimals(2);
    x_->setRange(-100000, 100000);
    y_->setRange(-100000, 100000);
    x_->setSingleStep(1.0);
    y_->setSingleStep(1.0);
    x_->setSuffix(" px");
    y_->setSuffix(" px");

    layout->addWidget(xLabel);
    layout->addWidget(x_);
    layout->addWidget(yLabel);
    layout->addWidget(y_);
    layout->addStretch(1);

    connect(x_, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, &PositionEditor::onXChanged);
    connect(y_, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, &PositionEditor::onYChanged);
}

QPointF PositionEditor::value() const {
    return QPointF(x_->value(), y_->value());
}

void PositionEditor::setValue(const QPointF& p) {
    // ★ blockSignals:防止反向同步时(目标 → 表单)无限循环
    const QSignalBlocker blockX(x_);
    const QSignalBlocker blockY(y_);
    x_->setValue(p.x());
    y_->setValue(p.y());
}

void PositionEditor::onXChanged(double v) {
    emit valueChanged(QPointF(v, y_->value()));
}
void PositionEditor::onYChanged(double v) {
    emit valueChanged(QPointF(x_->value(), v));
}

} // namespace qwhmi::editor
