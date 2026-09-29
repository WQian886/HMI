// ============================================================================
// 阶段 E · E.4d-follow-8 · GeometryChangeCommand 实现
// 对应 GeometryChangeCommand.h
// ============================================================================

#include "editor/GeometryChangeCommand.h"
#include "widgets/base/ControlBase.h"

#include <QDateTime>
#include <QElapsedTimer>

namespace qwhmi::editor {

GeometryChangeCommand::GeometryChangeCommand(
        const QHash<qwhmi::widgets::ControlBase*, QRectF>& oldRects,
        const QHash<qwhmi::widgets::ControlBase*, QRectF>& newRects,
        const QString& text,
        QUndoCommand* parent)
    : QUndoCommand(text, parent)
    , oldRects_(oldRects)
    , newRects_(newRects)
    , timestamp_(QDateTime::currentMSecsSinceEpoch())
    , actionTag_(text)   // ★ 默认用显示文案当合并标签
{
}

void GeometryChangeCommand::redo() {
    applyRects(newRects_);
}

void GeometryChangeCommand::undo() {
    applyRects(oldRects_);
}

void GeometryChangeCommand::applyRects(
        const QHash<qwhmi::widgets::ControlBase*, QRectF>& rects) {
    // ★ QHash 遍历顺序不确定,但对齐/统一尺寸的每个控件互不依赖(每个都是
    //   独立的 setPos),所以顺序无关。这点跟"缩放组 handle"不同 ——
    //   那边有相互依赖,必须按固定顺序。见 GroupHandleItem::applyProportionalResize。
    for (auto it = rects.constBegin(); it != rects.constEnd(); ++it) {
        auto* c = it.key();
        if (!c) continue;   // 控件已被删除的防御
        const QRectF& r = it.value();
        // ★ 分两步而不是一次 setGeometry:
        //   ControlBase 没有 setGeometry 覆盖,position 和 size 是两个独立
        //   Q_PROPERTY,分开设会 emit 两次(selectionFrame / groupHandle 各更新一次),
        //   但那两者的更新是幂等的,多一次无所谓。
        //   反过来合成一次则不行 —— 那需要新增一个 setGeometry,接口面变大不值当。
        c->setPosition(r.topLeft());
        c->setSize(r.size());
    }
}

bool GeometryChangeCommand::mergeWith(const QUndoCommand* other) {
    auto* o = dynamic_cast<const GeometryChangeCommand*>(other);
    if (!o) return false;

    // ★ 三道条件都必要:
    //   1. actionTag 相同 —— 「左对齐」再「左对齐」合并(第二次多半是误触),
    //      但「左对齐」后「等大」不合并,否则 Ctrl+Z 语义变得不可预测
    //   2. 控件集合相同 —— 否则"我以为在撤销刚才那步",实际撤了一堆别的
    //   3. 200ms 内 —— 再晚就是用户想分两次操作,强行合并会让"撤销两次回到原位"
    //      变成做不到
    if (o->actionTag_ != actionTag_) return false;
    if (o->timestamp_ - timestamp_ > 200) return false;
    if (o->oldRects_.keys() != oldRects_.keys()) return false;

    // ★ 合并方向:保留【最早】的 oldRects,把 newRects 更新成最新的。
    //   写反了的话(拿最新的 old 覆盖),撤销会只退掉最后一次操作 ——
    //   用户连按两次左对齐,Ctrl+Z 一次后右边还有 5 个没对齐,看着像没生效。
    newRects_ = o->newRects_;
    timestamp_ = o->timestamp_;
    return true;
}

} // namespace qwhmi::editor
