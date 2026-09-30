// ============================================================================
// PositionEditor 实现
// ============================================================================

#include "editor/PositionEditor.h"

#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <limits>

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

void PositionEditor::setXEnabled(bool on) { x_->setEnabled(on); }
void PositionEditor::setYEnabled(bool on) { y_->setEnabled(on); }

void PositionEditor::setXValid(bool valid) { xValid_ = valid; }
void PositionEditor::setYValid(bool valid) { yValid_ = valid; }
bool PositionEditor::isXValid() const { return xValid_; }
bool PositionEditor::isYValid() const { return yValid_; }

void PositionEditor::onXChanged(double v) {
    // ★ y 是"异值置空"时,y_->value() 是 0 这个【假值】。
    //   直接把它塞进 QPointF 发出去,PropertySheet 会把 0 写到【所有】控件上 ——
    //   用户只想改 x,却把 y 全刷成 0 了(这正是 bug图 里"控件跑位"的观感来源)。
    //   y 无效时发 NaN 作为"这一分量别动"的标记,PropertySheet 侧负责跳过。
    const double y = yValid_ ? y_->value() : std::numeric_limits<double>::quiet_NaN();
    emit valueChanged(QPointF(v, y));
}

void PositionEditor::onYChanged(double v) {
    const double x = xValid_ ? x_->value() : std::numeric_limits<double>::quiet_NaN();
    emit valueChanged(QPointF(x, v));
}

} // namespace qwhmi::editor
