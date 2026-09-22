// ============================================================================
// 阶段 E · E.4c · SizeEditor(QSizeF 复合编辑器)
//
// 文件职责:两个 QDoubleSpinBox 拼一个 (width, height) 编辑器,发出
//   valueChanged(QSizeF) 信号。在 PropertySheet 中作为 size 属性的编辑器。
//
// 关联:PropertySheet.cpp, ControlBase.cpp (size QSizeF Q_PROPERTY)
// ============================================================================

#pragma once

#include <QWidget>
#include <QSizeF>

class QDoubleSpinBox;

namespace qwhmi::editor {

class SizeEditor : public QWidget {
    Q_OBJECT
public:
    explicit SizeEditor(QWidget* parent = nullptr);

    QSizeF value() const;
    void   setValue(const QSizeF& s);

signals:
    void valueChanged(const QSizeF& s);

private slots:
    void onWidthChanged(double v);
    void onHeightChanged(double v);

private:
    QDoubleSpinBox* w_ = nullptr;
    QDoubleSpinBox* h_ = nullptr;
};

} // namespace qwhmi::editor
