// ============================================================================
// 阶段 E · E.1 · ToggleControl(控制 — 开关 2 态)
//
// 文件职责:2 态开关(on/off),点击翻转 value(0↔1)。
//   value==1 → 显示"ON"绿;value==0 → "OFF"灰。
//
// 关联:ControlBase.h
// ============================================================================

#pragma once

#include "widgets/base/ControlBase.h"
#include <QColor>

namespace qwhmi::widgets {

class ToggleControl : public ControlBase {
    Q_OBJECT
    Q_PROPERTY(QString onText  READ onText  WRITE setOnText  NOTIFY onTextChanged)
    Q_PROPERTY(QString offText READ offText WRITE setOffText NOTIFY offTextChanged)
    Q_PROPERTY(QColor onColor  READ onColor  WRITE setOnColor  NOTIFY onColorChanged)

public:
    explicit ToggleControl(QGraphicsItem* parent = nullptr);
    ~ToggleControl() override;

    QString onText()  const { return onText_; }
    void    setOnText(const QString& t);
    QString offText() const { return offText_; }
    void    setOffText(const QString& t);
    QColor  onColor()  const { return onColor_; }
    void    setOnColor(const QColor& c);

    QRectF boundingRect() const override;
    void   paint(QPainter* painter,
                 const QStyleOptionGraphicsItem* option,
                 QWidget* widget = nullptr) override;

    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;

signals:
    void onTextChanged();
    void offTextChanged();
    void onColorChanged();
    void toggled(bool on);

private:
    QString onText_  = QStringLiteral("ON");
    QString offText_ = QStringLiteral("OFF");
    QColor  onColor_ = QColor(40, 180, 40);
};

} // namespace qwhmi::widgets