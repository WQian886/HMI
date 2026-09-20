// ============================================================================
// 阶段 E · E.1 · InputEditControl(控制 — 文本输入)
//
// 文件职责:用 QGraphicsProxyWidget 包 QLineEdit — 编辑文本
//   QGraphicsObject 不能直接 host widget — 走 QGraphicsProxyWidget(损失些性能)
//   可选:Mode::ReadOnly / Mode::WriteOnly / Mode 数字
//
// 关联:ControlBase.h
// ============================================================================

#pragma once

#include "widgets/base/ControlBase.h"
#include <QString>

class QLineEdit;
class QGraphicsProxyWidget;

namespace qwhmi::widgets {

class InputEditControl : public ControlBase {
    Q_OBJECT
    Q_PROPERTY(QString text READ text NOTIFY textChanged)             // 只读(get from QLineEdit)
    Q_PROPERTY(QString placeholder READ placeholder WRITE setPlaceholder NOTIFY placeholderChanged)
    Q_PROPERTY(bool readOnly READ readOnly WRITE setReadOnly NOTIFY readOnlyChanged)

public:
    explicit InputEditControl(QGraphicsItem* parent = nullptr);
    ~InputEditControl() override;

    QString text() const;
    void    setText(const QString& t);
    // ★ Variable 变化时也推给 QLineEdit(ControlBase::setValue 非 virtual,但子类仍可隐藏)
    void    setValue(const QVariant& v);
    QString placeholder() const { return placeholder_; }
    void    setPlaceholder(const QString& p);
    bool    readOnly() const { return readOnly_; }
    void    setReadOnly(bool r);

    QRectF boundingRect() const override;
    void   paint(QPainter* painter,
                 const QStyleOptionGraphicsItem* option,
                 QWidget* widget = nullptr) override;

signals:
    void textChanged();
    void placeholderChanged();
    void readOnlyChanged();
    void editingFinished(const QString& finalText);   // 编辑完发一次(写 PLC 用)

private slots:
    void onLineEditTextChanged(const QString& t);
    void onLineEditEditingFinished();

private:
    QLineEdit*           lineEdit_ = nullptr;
    QGraphicsProxyWidget* proxy_    = nullptr;
    QString               placeholder_ = QStringLiteral("输入...");
    bool                  readOnly_ = false;
};

} // namespace qwhmi::widgets