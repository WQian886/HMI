// ============================================================================
// 阶段 E · E.7 · ControlListCommand 实现
// 对应 ControlListCommand.h
// ============================================================================

#include "editor/ControlListCommand.h"

namespace qwhmi::editor {

namespace {
    // 控件 → JSON 快照。跟 Project::controlsToJson(ControlBase 那一段)完全一致:
    //   propertiesToJson 拿全部 Q_PROPERTY,type 字段用【注册表短名】。
    // 唯一不许用的是 ControlBase::toJson() —— 它写的 type 是 className,
    // 跟 create() 查的 key("Bar" vs "BarControl")对不上,还原时静默返回 nullptr。
    QJsonObject snapshotOf(qwhmi::widgets::ControlBase* c) {
        if (!c) return {};
        QJsonObject o = c->propertiesToJson();
        o["type"] = qwhmi::widgets::ControlBase::registryKeyFor(c->metaObject());
        return o;
    }
} // namespace

ControlListCommand::ControlListCommand(Kind kind,
                                       const QList<QJsonObject>& snapshots,
                                       const QList<qwhmi::widgets::ControlBase*>& initial,
                                       EditorMainWindow* window,
                                       const QString& text)
    : QUndoCommand(text),
      kind_(kind),
      snapshots_(snapshots),
      live_(initial),
      window_(window) {
    // 快照和 live_ 必须等长 —— 每个快照对应一个活控件,一一配对。
    // 对不上就是调用方的 bug(比如快照时跳过了一个 null),宁可当场发现。
    Q_ASSERT(snapshots_.size() == live_.size());
}

ControlListCommand* ControlListCommand::forPaste(QList<qwhmi::widgets::ControlBase*> created,
                                                EditorMainWindow* window,
                                                const QString& text) {
    QList<QJsonObject> snaps;
    snaps.reserve(created.size());
    for (auto* c : created) snaps.append(snapshotOf(c));
    // live_ 传实际控件:首次 redo 靠"live_ 非空"判定为空操作
    return new ControlListCommand(Kind::Add, snaps, created, window, text);
}

ControlListCommand* ControlListCommand::forDelete(QList<qwhmi::widgets::ControlBase*> doomed,
                                                  EditorMainWindow* window,
                                                  const QString& text) {
    QList<QJsonObject> snaps;
    snaps.reserve(doomed.size());
    for (auto* c : doomed) snaps.append(snapshotOf(c));
    // live_ 传待删控件:首次 redo 就要把它们销毁
    return new ControlListCommand(Kind::Remove, snaps, doomed, window, text);
}

void ControlListCommand::createLive() {
    if (!window_) return;
    // 顺序还原:贴回去时层级跟原来一致,选中态也按原顺序累积
    for (const QJsonObject& snap : snapshots_) {
        const QString type = snap.value("type").toString();
        auto* c = qwhmi::widgets::ControlBase::create(type);
        if (!c) {
            // 静默跳过 = 复现 E.5 那个坑(控件凭空消失且不报错)。这里必须出声。
            qWarning() << "[ControlListCommand] 还原失败:未知控件类型" << type;
            continue;
        }
        c->propertiesFromJson(snap);
        window_->addControl(c);
        live_.append(c);
    }
    // 撤销删除 / 粘贴完成后,新控件应当是当前选中 —— 否则用户撤销了一次删除,
    // 控件回到了画布上却没有任何选中反馈,像是"没撤销成功"。
    window_->selectControls(live_);
}

void ControlListCommand::destroyLive() {
    if (!window_) return;
    // 倒序销毁:undo 栈里靠后的命令引用靠前的控件时,靠后先死不会留悬空引用
    for (int i = live_.size() - 1; i >= 0; --i) {
        window_->destroyControl(live_.at(i));
    }
    live_.clear();
}

void ControlListCommand::redo() {
    switch (kind_) {
    case Kind::Add:
        // ★ 首次 redo 是空操作:粘贴时控件已经被调用方建好放进画布了,
        //   这里再"创建一遍"会凭空多出一批控件。
        //   靠 live_ 是否为空区分 —— 撤销后 live_ 被清空,redo 就该真的重建。
        if (live_.isEmpty()) createLive();
        break;
    case Kind::Remove:
        // 删除:每次 redo 都要真的销毁(undo 重建过一次,得再毁掉)
        if (!live_.isEmpty()) destroyLive();
        break;
    }
}

void ControlListCommand::undo() {
    switch (kind_) {
    case Kind::Add:
        // 撤销粘贴 = 把刚贴上来的销毁
        if (!live_.isEmpty()) destroyLive();
        break;
    case Kind::Remove:
        // 撤销删除 = 从快照还原
        if (live_.isEmpty()) createLive();
        break;
    }
}

} // namespace qwhmi::editor
