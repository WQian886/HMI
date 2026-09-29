// ============================================================================
// 阶段 E · E.4d-follow-8 · GeometryChangeCommand(QUndoCommand 子类)
//
// 文件职责:封装"一批控件的位置/尺寸变化"为一个可撤销命令。
//   undo() 把每个控件还原到执行前的几何,redo() 应用新几何。
//
// 【为什么不直接复用 PropertyChangeCommand】那个是"单控件 + 单属性",
//   而对齐/统一尺寸天然是【一次操作动 N 个控件】:选 8 个控件按左对齐,
//   要么 push 8 条命令(Ctrl+Z 要按 8 次才能退干净,退一半时的画面还是歪的),
//   要么就得在这里重新实现一遍合并逻辑。多选场景下"一次 Ctrl+Z = 一步操作"
//   是硬要求 —— 半撤销的排版比不撤销更让人困惑。
//
// 【为什么记【矩形】而不是记【属性】】记录 QPointF/QSizeF 快照而不是
//   property 名 + 值,是因为这批动作里有些不是属性变更:
//   「等大」同时改 width 和 height,「右对齐」改的是 x 依赖 width 的组合。
//   记矩形就统一成一种形状,undo/redo 两条路径都是"设回这个矩形",
//   不用在两处分别推演各动作的逆运算 —— 逆运算写错是这类代码最常见的 bug。
//
// 【生命周期】QUndoCommand 被 delete 时控件可能已经没了(用户删了控件再撤销删除)。
//   所以裸存 ControlBase* 是安全的:控件的删除本身【不在】本命令范围内,
//   且 CommandBase::undo() 在栈清空时才被 delete,那时机已在窗口析构之后。
//   现状.md 第五节对"变量"要求用 QPointer,这里是另一回事 —— 控件的
//   所有权在 EditorMainWindow::controls_,和 Undo 栈无生命周期耦合。
//
// 关联:EditorMainWindow.cpp(6 个对齐 + 3 个等尺寸), QUndoStack
// ============================================================================

#pragma once

#include <QUndoCommand>
#include <QHash>
#include <QList>
#include <QPointF>
#include <QRectF>
#include <QString>

namespace qwhmi::widgets { class ControlBase; }

namespace qwhmi::editor {

class GeometryChangeCommand : public QUndoCommand {
public:
    // ★ newRects 必须与 oldRects 的 key 集合完全一致(按控件指针索引)。
    //   构造后两个 hash 都是 immutable 的 —— undo/redo 只读不写,
    //   所以同一个命令对象被反复 undo/redo 也不会自我污染。
    //   (改成可变的"当前矩形"字段是常见错误:undo 一次之后 redo 就会用错数据)
    GeometryChangeCommand(const QHash<qwhmi::widgets::ControlBase*, QRectF>& oldRects,
                          const QHash<qwhmi::widgets::ControlBase*, QRectF>& newRects,
                          const QString& text,
                          QUndoCommand* parent = nullptr);

    void undo() override;
    void redo() override;

    // ★ 两次连续"按左对齐"应该合并成一条命令,而不是撤销两次。
    //   mergeWith 要求:id 相同 + 时间间隔 < 200ms + 命令类型相同。
    //   这里只合并【连续的同类几何操作】,不合并跨类型的 ——
    //   "对齐后改宽度"和"改宽度后对齐"不能合成一条,那会让 Ctrl+Z 的语义变得不可预测。
    int  id() const override { return 1002; }
    bool mergeWith(const QUndoCommand* other) override;

private:
    // ★ 统一走 setPos(不直接 setPosition):
    //   ControlBase::setPosition 会额外 emit positionChanged,
    //   而 ItemIsMovable 拖动走的是 Qt 内部的 setPos(靠 itemChange 补 emit,
    //   见 ControlBase.cpp 的 itemChange override)。撤销必须跟拖动的效果一致 ——
    //   否则撤销后 SelectionFrame / GroupHandle 不跟随,画面和实际状态脱节。
    static void applyRects(const QHash<qwhmi::widgets::ControlBase*, QRectF>& rects);

    // 时间戳用 QDateTime::currentMSecsSinceEpoch 取不到确定值时的问题:
    //   单元测里两条命令可能同一毫秒,所以额外用计数器兜底见 cpp
    QHash<qwhmi::widgets::ControlBase*, QRectF> oldRects_;
    QHash<qwhmi::widgets::ControlBase*, QRectF> newRects_;
    qint64 timestamp_ = 0;
    // ★ 本命令自己算的"这次操作的标签"。mergeWith 只在标签相同时才合并 ——
    //   「左对齐」连点两次合一条(第二次是误触,想撤就一步退掉),
    //   但「左对齐」后紧接「等大」不该合并成一步。
    QString actionTag_;
};

} // namespace qwhmi::editor
