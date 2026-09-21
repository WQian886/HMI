// ============================================================================
// 阶段 E · E.2 · PropertyChangeCommand(QUndoCommand 子类)
//
// 文件职责:封装"修改 1 个 Q_PROPERTY"为可撤销命令。push 到 QUndoStack 后,
//   redo() 应用新值,undo() 恢复旧值;Ctrl+Z / Ctrl+Y 标准工作。
//
// 关联:PropertySheet.h, QUndoStack
// ============================================================================

#pragma once

#include <QUndoCommand>
#include <QString>
#include <QVariant>

namespace qwhmi::widgets { class ControlBase; }

namespace qwhmi::editor {

class PropertyChangeCommand : public QUndoCommand {
public:
    PropertyChangeCommand(qwhmi::widgets::ControlBase* target,
                          const QString& propName,
                          const QVariant& oldValue,
                          const QVariant& newValue,
                          QUndoCommand* parent = nullptr);

    void undo() override;
    void redo() override;

    // ★ 同控件 + 同属性 + 时间间隔 < 200ms 的连续修改合并为 1 个命令
    int  id() const override { return 1001; }
    bool mergeWith(const QUndoCommand* other) override;

private:
    qwhmi::widgets::ControlBase* target_ = nullptr;
    QString  propName_;
    QVariant oldValue_;
    QVariant newValue_;
    qint64   timestamp_ = 0;
};

} // namespace qwhmi::editor
