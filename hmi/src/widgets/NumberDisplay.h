// ============================================================================
// 阶段 E · E.1 · NumberDisplay(显示类 — 数字格式化)
//
// 文件职责:数字格式化显示,可绑 Variable,可选 format 字符串("%.2f" 等)。
//
// 关联:ControlBase.h, NumberDisplay.cpp
// ============================================================================

#pragma once

#include "widgets/base/ControlBase.h"

namespace qwhmi::widgets {

class NumberDisplay : public ControlBase {
    Q_OBJECT
    Q_PROPERTY(QString format READ format WRITE setFormat NOTIFY formatChanged)
    Q_PROPERTY(QString unit READ unit WRITE setUnit NOTIFY unitChanged)

public:
    explicit NumberDisplay(QGraphicsItem* parent = nullptr);
    ~NumberDisplay() override;

    QString format() const { return format_; }       // 默认 "%.2f"
    void    setFormat(const QString& f);
    QString unit() const { return unit_; }           // 默认 ""
    void    setUnit(const QString& u);

    // ★ 覆盖 displayText:数字 + 单位
    QString displayText() const override;

    QRectF boundingRect() const override;
    void   paint(QPainter* painter,
                 const QStyleOptionGraphicsItem* option,
                 QWidget* widget = nullptr) override;

signals:
    void formatChanged();
    void unitChanged();

private:
    QString format_ = QStringLiteral("%.2f");
    QString unit_;
};

} // namespace qwhmi::widgets