// ============================================================================
// 阶段 E · E.4c · PositionEditor(QPointF 复合编辑器)
//
// 文件职责:两个 QDoubleSpinBox 拼一个 (x, y) 编辑器,发出
//   valueChanged(QPointF) 信号。在 PropertySheet 中作为 position 属性的编辑器。
//
// 关联:PropertySheet.cpp, ControlBase.cpp (position QPointF Q_PROPERTY)
// ============================================================================

#pragma once

#include <QWidget>
#include <QPointF>

class QDoubleSpinBox;

namespace qwhmi::editor {

class PositionEditor : public QWidget {
    Q_OBJECT
public:
    explicit PositionEditor(QWidget* parent = nullptr);

    QPointF value() const;
    void    setValue(const QPointF& p);

signals:
    void valueChanged(const QPointF& p);

private slots:
    void onXChanged(double v);
    void onYChanged(double v);

private:
    QDoubleSpinBox* x_ = nullptr;
    QDoubleSpinBox* y_ = nullptr;
};

} // namespace qwhmi::editor
