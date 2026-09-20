// ============================================================================
// 阶段 E · E.1 · TextDisplay(显示类 — 多行文字/前缀后缀)
//
// 文件职责:与 Label 区别是支持 prefix(标签)+ value + suffix(单位/状态)。
//   显示: "速度: 123.50 rpm"
//
// 关联:ControlBase.h, TextDisplay.cpp
// ============================================================================

#pragma once

#include "widgets/base/ControlBase.h"

namespace qwhmi::widgets {

class TextDisplay : public ControlBase {
    Q_OBJECT
    Q_PROPERTY(QString prefix READ prefix WRITE setPrefix NOTIFY prefixChanged)
    Q_PROPERTY(QString suffix READ suffix WRITE setSuffix NOTIFY suffixChanged)

public:
    explicit TextDisplay(QGraphicsItem* parent = nullptr);
    ~TextDisplay() override;

    QString prefix() const { return prefix_; }       // 默认 ""
    void    setPrefix(const QString& p);
    QString suffix() const { return suffix_; }       // 默认 ""
    void    setSuffix(const QString& s);

    // ★ 覆盖 displayText:prefix + value + suffix
    QString displayText() const override;

    QRectF boundingRect() const override;
    void   paint(QPainter* painter,
                 const QStyleOptionGraphicsItem* option,
                 QWidget* widget = nullptr) override;

signals:
    void prefixChanged();
    void suffixChanged();

private:
    QString prefix_;
    QString suffix_;
};

} // namespace qwhmi::widgets