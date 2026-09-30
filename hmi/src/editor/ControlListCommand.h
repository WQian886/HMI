// ============================================================================
// 阶段 E · E.7 · ControlListCommand(QUndoCommand 子类)
//
// 文件职责:封装"一批控件的整体加入 / 整体移除"为一个可撤销命令。
//   粘贴 → Kind::Add(控件已由调用方建好并入画布,undo 把它们销毁)
//   删除 → Kind::Remove(控件此刻还在画布上,redo 第一次把它们销毁)
//
// 【为什么把增删合成一个类而不是写 AddCommand + RemoveCommand】
//   两者的 undo/redo 逻辑是严格对称的:都是"从 JSON 快照造出一批控件"跟
//   "销毁一批控件"来回切,差别只在【第一次 redo 的语义】。合成一个之后,
//   快照格式、销毁顺序、选中态处理只有一份实现 —— 这类命令写两遍几乎必然
//   改了一处忘了另一处,而"撤销后画面不对"是最难自查的一类 bug。
//
// 【为什么存 JSON 快照而不是裸 ControlBase*】
//   增删命令是本项目里【唯一】会真的 delete 控件的地方,也是悬空指针风险
//   最高的地方:
//     · 删了控件再撤销 → 指针已失效
//     · 删了控件再撤销再删再撤销 → 命令被复用,指针必须是活的
//     · 窗口析构时 QUndoStack 连同命令一起销毁,命令若持有控件指针,
//       那一刻控件可能已经被 controls_ 的清理逻辑 delete 过一遍
//   换成 JSON 快照(值类型)之后,命令跟控件的生命周期彻底解耦:
//   造控件用 ControlBase::create(注册表短名) + propertiesFromJson(反射写回),
//   跟 RuntimeCanvas / Project::loadFromFile 是同一条路径 —— E.5 已经验证过。
//   代价是撤销后拿到的是一个【新对象】(指针不同),所以命令不管选中态,
//   选中由 EditorMainWindow 在 createLive/destroyLive 之后统一处理。
//
// 【快照用 propertiesToJson + registryKeyFor,不是 toJson】
//   ControlBase::toJson() 里的 type 字段写的是 className("BarControl"),
//   而 create() 查的注册表 key 是短名("Bar") —— 两者对不上,拿 toJson 的
//   快照去还原会 create 返回 nullptr(静默丢控件)。Project::controlsToJson
//   用的就是 propertiesToJson + registryKeyFor,这里必须完全对齐。
//
// 关联:EditorMainWindow(onCopy/onPaste/onDelete), ControlBase, QUndoStack
// ============================================================================

#pragma once

#include <QUndoCommand>
#include <QJsonObject>
#include <QList>
#include <QPointer>
#include <QString>

#include "editor/EditorMainWindow.h"
#include "widgets/base/ControlBase.h"

namespace qwhmi::editor {

class ControlListCommand : public QUndoCommand {
public:
    enum class Kind {
        Add,       // ★ 粘贴:调用方已把控件建好并 addControl 了
        Remove,    // ★ 删除:调用方只是点名要删,此刻控件还在画布上
    };

    // —— 两个工厂,语义不同不要混用 ——
    //
    // forPaste:created 里的控件【已经在画布上】。push 时 QUndoStack 会立刻调
    //   redo(),而这次 redo 必须是空操作(否则刚建好的控件当场被自己删掉)。
    //   靠 live_ 非空来区分"首次"与"撤销后重来"。
    static ControlListCommand* forPaste(QList<qwhmi::widgets::ControlBase*> created,
                                       EditorMainWindow* window,
                                       const QString& text);

    // forDelete:doomed 里的控件【此刻还在画布上】,第一次 redo 才真正销毁。
    static ControlListCommand* forDelete(QList<qwhmi::widgets::ControlBase*> doomed,
                                         EditorMainWindow* window,
                                         const QString& text);

    void undo() override;
    void redo() override;

    // ★ 增删【不合并】。连续删两个控件要按两次 Ctrl+Z,连续粘两次也要两次 ——
    //   合并会让"我刚才删了两个"变成一个说不清的动作。id() 返回 -1 是
    //   QUndoCommand 的默认(表示不参与任何 mergeWith),这里显式写出来留个记录。
    int id() const override { return -1; }

private:
    ControlListCommand(Kind kind,
                       const QList<QJsonObject>& snapshots,
                       const QList<qwhmi::widgets::ControlBase*>& initial,
                       EditorMainWindow* window,
                       const QString& text);

    // 从 snapshots_ 造控件 → 加进画布 → 全选。undo 与"撤销后 redo"都走这里。
    void createLive();
    // 销毁 live_ 里的控件(经 EditorMainWindow::destroyControl,它负责
    // 从 controls_/scene 摘除 + 清选中态 + delete)。
    void destroyLive();

    Kind                        kind_;
    QList<QJsonObject>          snapshots_;   // 值类型 —— 命令与控件生命周期解耦的关键
    QList<qwhmi::widgets::ControlBase*> live_; // 当前实际在画布上的那批
    // ★ QPointer 不是多余的:undoStack_ 是本窗口的 QObject 子对象,窗口析构时
    //   命令会跟着一起被销毁,而那一刻窗口正处于半析构状态。指针自动置 null,
    //   至少保证 createLive/destroyLive 不会拿着野指针往窗口上打。
    QPointer<EditorMainWindow>  window_;
};

} // namespace qwhmi::editor
