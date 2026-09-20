// ============================================================================
// InputEditControl 实现
// ============================================================================

#include "widgets/InputEditControl.h"

#include <QLineEdit>
#include <QGraphicsProxyWidget>
#include <QPainter>

namespace qwhmi::widgets {

InputEditControl::InputEditControl(QGraphicsItem* parent)
    : ControlBase(parent)
{
    setSize(QSizeF(160, 28));
    setFlag(QGraphicsItem::ItemIsMovable, true);
    setFlag(QGraphicsItem::ItemIsSelectable, true);

    // ★ 用 QGraphicsProxyWidget 包 QLineEdit(QGraphicsObject 不能 host widget)
    lineEdit_ = new QLineEdit;
    lineEdit_->setPlaceholderText(placeholder_);
    proxy_ = new QGraphicsProxyWidget(this);   // ★ 关键: parent 是 this,不是 scene
    proxy_->setWidget(lineEdit_);

    connect(lineEdit_, &QLineEdit::textChanged,
            this, &InputEditControl::onLineEditTextChanged);
    connect(lineEdit_, &QLineEdit::editingFinished,
            this, &InputEditControl::onLineEditEditingFinished);

    registerControlType("InputEdit", []() -> ControlBase* { return new InputEditControl(); });
}

InputEditControl::~InputEditControl() = default;

QString InputEditControl::text() const {
    return lineEdit_ ? lineEdit_->text() : QString();
}

void InputEditControl::setText(const QString& t) {
    if (lineEdit_ && lineEdit_->text() != t) {
        lineEdit_->setText(t);
    }
}

void InputEditControl::setValue(const QVariant& v) {
    ControlBase::setValue(v);          // 调基类
    // ★ 同步推给 QLineEdit(Variable 变化 → 框显示)
    if (lineEdit_ && lineEdit_->text() != v.toString()) {
        lineEdit_->setText(v.toString());
    }
}

void InputEditControl::setPlaceholder(const QString& p) {
    if (p == placeholder_) return;
    placeholder_ = p;
    if (lineEdit_) lineEdit_->setPlaceholderText(p);
    emit placeholderChanged();
}

void InputEditControl::setReadOnly(bool r) {
    if (r == readOnly_) return;
    readOnly_ = r;
    if (lineEdit_) lineEdit_->setReadOnly(r);
    emit readOnlyChanged();
}

QRectF InputEditControl::boundingRect() const {
    return QRectF(0, 0, size().width(), size().height());
}

void InputEditControl::paint(QPainter* p,
                             const QStyleOptionGraphicsItem* option,
                             QWidget* widget) {
    Q_UNUSED(p); Q_UNUSED(option); Q_UNUSED(widget);
    // ★ paint 不画东西 — proxy 自己渲染 QLineEdit
    // 这里 paint 只是声明;proxy 由 scene render 时自动画
}

void InputEditControl::onLineEditTextChanged(const QString& t) {
    setValue(t);                  // 写 value(QVariant 装 QString)— emit valueChanged
    emit textChanged();
}

void InputEditControl::onLineEditEditingFinished() {
    emit editingFinished(lineEdit_->text());
}

} // namespace qwhmi::widgets