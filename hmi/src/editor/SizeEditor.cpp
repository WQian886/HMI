// ============================================================================
// SizeEditor 实现
// ============================================================================

#include "editor/SizeEditor.h"

#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <limits>

namespace qwhmi::editor {

SizeEditor::SizeEditor(QWidget* parent) : QWidget(parent) {
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);

    auto* wLabel = new QLabel(tr("W:"));
    auto* hLabel = new QLabel(tr("H:"));
    w_ = new QDoubleSpinBox;
    h_ = new QDoubleSpinBox;
    w_->setDecimals(2);
    h_->setDecimals(2);
    w_->setRange(0, 100000);   // ★ 上限 100k 像素,够用
    h_->setRange(0, 100000);
    w_->setSingleStep(1.0);
    h_->setSingleStep(1.0);
    w_->setSuffix(" px");
    h_->setSuffix(" px");

    layout->addWidget(wLabel);
    layout->addWidget(w_);
    layout->addWidget(hLabel);
    layout->addWidget(h_);
    layout->addStretch(1);

    connect(w_, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, &SizeEditor::onWidthChanged);
    connect(h_, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, &SizeEditor::onHeightChanged);
}

QSizeF SizeEditor::value() const {
    return QSizeF(w_->value(), h_->value());
}

void SizeEditor::setValue(const QSizeF& s) {
    // ★ blockSignals:防止反向同步时(目标 → 表单)无限循环
    const QSignalBlocker blockW(w_);
    const QSignalBlocker blockH(h_);
    w_->setValue(s.width());
    h_->setValue(s.height());
}

void SizeEditor::setWEnabled(bool on) { w_->setEnabled(on); }
void SizeEditor::setHEnabled(bool on) { h_->setEnabled(on); }
void SizeEditor::setWValid(bool valid) { wValid_ = valid; }
void SizeEditor::setHValid(bool valid) { hValid_ = valid; }

void SizeEditor::onWidthChanged(double v) {
    // ★ h 无效(异值置空)时发 NaN,别把 0 这个假值刷到所有控件上
    const double h = hValid_ ? h_->value() : std::numeric_limits<double>::quiet_NaN();
    emit valueChanged(QSizeF(v, h));
}
void SizeEditor::onHeightChanged(double v) {
    const double w = wValid_ ? w_->value() : std::numeric_limits<double>::quiet_NaN();
    emit valueChanged(QSizeF(w, v));
}

} // namespace qwhmi::editor
