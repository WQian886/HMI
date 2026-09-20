// ============================================================================
// 阶段 E · E.1 · ButtonControl(控制 — 按钮)
//
// 文件职责:可点击按钮,点击 emit clicked(信号)。
//   8 类预定义动作(E.6)会 emit 不同 clicked 携带不同参数。
//
// 关联:ControlBase.h, ButtonControl.cpp
// ============================================================================

#pragma once

#include "widgets/base/ControlBase.h"
#include <QColor>

namespace qwhmi::widgets {

class ButtonControl : public ControlBase {
    Q_OBJECT
    Q_PROPERTY(QString text READ text WRITE setText NOTIFY textChanged)
    Q_PROPERTY(QColor background READ background WRITE setBackground NOTIFY backgroundChanged)

public:
    explicit ButtonControl(QGraphicsItem* parent = nullptr);
    ~ButtonControl() override;

    QString text() const { return text_; }
    void    setText(const QString& t);
    QColor  background() const { return background_; }
    void    setBackground(const QColor& c);

    QRectF boundingRect() const override;
    void   paint(QPainter* painter,
                 const QStyleOptionGraphicsItem* option,
                 QWidget* widget = nullptr) override;

    // ★ 鼠标事件 — emit clicked
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;

signals:
    void textChanged();
    void backgroundChanged();
    void clicked();

private:
    QString text_    = QStringLiteral("Button");
    QColor  background_ = QColor(0, 120, 215);
    bool    pressed_ = false;    // 视觉按下态
};

} // namespace qwhmi::widgets