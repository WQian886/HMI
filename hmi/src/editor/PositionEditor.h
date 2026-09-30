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

    // ★ B 组修复:多选时按【分量】分别判断同值。
    //   PropertySheet 原来只按整个 QPointF 判异值,导致 x 相同、y 不同的两个控件,
    //   x 也被连坐置灰(见 docs/bug图/README.md B 组 图2/图3/图6/图7)。
    //   下面两个接口把"这个分量是不是真值"这件事告诉编辑器。
    void    setXEnabled(bool on);
    void    setYEnabled(bool on);
    //   分量被判定为异值时置空 → 它的当前值是【假值】,
    //   改动另一个分量时不能拿它去覆盖所有控件(否则会误改)。
    void    setXValid(bool valid);
    void    setYValid(bool valid);
    bool    isXValid() const;
    bool    isYValid() const;

signals:
    void valueChanged(const QPointF& p);

private slots:
    void onXChanged(double v);
    void onYChanged(double v);

private:
    QDoubleSpinBox* x_ = nullptr;
    QDoubleSpinBox* y_ = nullptr;
    // ★ 该分量的值是不是"真值"(多选同值 = 真;异值置空 = 假)
    bool xValid_ = true;
    bool yValid_ = true;
};

} // namespace qwhmi::editor
