// ============================================================================
// PropertyChangeCommand 实现
// ============================================================================

#include "editor/PropertyChangeCommand.h"
#include "widgets/base/ControlBase.h"

#include <QDateTime>

namespace qwhmi::editor {

PropertyChangeCommand::PropertyChangeCommand(qwhmi::widgets::ControlBase* target,
                                             const QString& propName,
                                             const QVariant& oldValue,
                                             const QVariant& newValue,
                                             QUndoCommand* parent)
    : QUndoCommand(parent),
      target_(target),
      propName_(propName),
      oldValue_(oldValue),
      newValue_(newValue),
      timestamp_(QDateTime::currentMSecsSinceEpoch())
{
    // ★ undo 栈显示文本 — Ctrl+Z 菜单显示
    setText(QObject::tr("修改 %1").arg(propName_));
}

void PropertyChangeCommand::undo() {
    if (!target_) return;
    target_->setProperty(propName_.toUtf8().constData(), oldValue_);
}

void PropertyChangeCommand::redo() {
    if (!target_) return;
    target_->setProperty(propName_.toUtf8().constData(), newValue_);
}

bool PropertyChangeCommand::mergeWith(const QUndoCommand* other) {
    // ★ 连续同属性修改(如拖控件 1px)合并为 1 个命令,避免 Undo 栈膨胀
    if (other->id() != id()) return false;
    const auto* o = static_cast<const PropertyChangeCommand*>(other);
    if (o->target_ != target_)   return false;
    if (o->propName_ != propName_) return false;
    // 200ms 内的合并,超过则当独立命令
    if (o->timestamp_ - timestamp_ > 200) return false;
    // ★ 合并:保留旧 oldValue_,更新 newValue_ 为本次新值
    newValue_ = o->newValue_;
    timestamp_ = o->timestamp_;
    return true;
}

} // namespace qwhmi::editor
