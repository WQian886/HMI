// ============================================================================
// 阶段 E · E.2 · EditorMainWindow 骨架
//
// 文件职责:工程师模式主窗口。包 hmi.exe 主窗口(QMainWindow 派生),中央是
//   QGraphicsView 画布,右侧 QDockWidget 放 PropertySheet,菜单栏含撤销/重做。
//   提供 setEditorMode(bool) 切运行时 / 编辑模式(运行时 = 属性面板 disabled)。
//
// 关键设计:
//   - 复用 PropertySheet + QUndoStack(E.2 周 10 已有)
//   - 选中控件自动同步到 PropertySheet(QGraphicsScene::selectionChanged)
//   - E.4a 调色板(本阶段先留 addControl 接口,具体拖入逻辑 E.4a 再做)
//
// 关联:PropertySheet.h, PropertyChangeCommand.h, ControlBase.h
// ============================================================================

#pragma once

#include <QMainWindow>
#include <QHash>
#include <QJsonObject>     // ★ E.7:剪贴板存 JSON 快照(按值成员,要完整类型)
#include <QList>
#include <QRectF>
#include "editor/SelectionFrameItem.h"   // ★ E.4d 续:public 访问器要返回完整类型

class QGraphicsScene;
class QGraphicsView;
class QDockWidget;
class QUndoStack;
class QToolBar;
class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;
class QKeyEvent;         // ★ E.7:keyPressEvent 覆写
class Project;            // ★ E.3:文件菜单保存/加载用
class VariableManager;    // ★ E.5:查变量做绑定

namespace qwhmi::widgets { class ControlBase; }
namespace qwhmi::editor  { class PropertySheet; class PaletteWidget; class ResizeHandleItem; class GroupHandleItem; }

namespace qwhmi::editor {

class EditorMainWindow : public QMainWindow {
    Q_OBJECT
public:
    // ★ E.5(2026-09-29)签名变了:编辑器不再自己 new Project,改为接收主画面的。
    //   【为什么改】原来构造里是 project_ = new Project(nullptr,×5,this),
    //   4 个 manager 全传 nullptr —— 意味着编辑器:
    //     ① 存盘只写 screens[0].controls,variables/alarms/actions/plcs 全部丢弃
    //     ② 存出来的文件跟主画面的 project_ 是两份独立数据,各改各的
    //   改成共用主画面那个 Project 后,两边是【同一个数据源】,
    //   编辑器保存的就是主画面加载的,不存在"两份会漂移的副本"。
    //   【向后兼容】project 传 nullptr 时退回旧行为(自建一个),55 个既有测试
    //   里那些 new EditorMainWindow(nullptr) 的用例不用改。
    explicit EditorMainWindow(Project* project = nullptr,
                              VariableManager* varMgr = nullptr,
                              QWidget* parent = nullptr);
    ~EditorMainWindow() override;

    // ★ 模式切换 — true = 编辑模式(属性面板可编辑),false = 运行时模式(disabled)
    void setEditorMode(bool enabled);
    bool editorMode() const { return editorMode_; }

    // ★ 控件调色板(E.4a 完整版会拆出去,这里先 inline 简单接口)
    void addControl(qwhmi::widgets::ControlBase* c);
    void removeControl(qwhmi::widgets::ControlBase* c);
    QList<qwhmi::widgets::ControlBase*> controls() const { return controls_; }

    // ★ 选中 target(同步到 PropertySheet)
    void setTarget(qwhmi::widgets::ControlBase* target);
    qwhmi::widgets::ControlBase* target() const { return target_; }

    // ★ E.4d 多选:返回当前选中的所有控件(0 个 = 空,1 个 = target_,>=2 = 多选)
    QList<qwhmi::widgets::ControlBase*> targets() const { return targets_; }
    bool hasMultipleSelection() const { return targets_.size() >= 2; }

    // ★ E.4d 续:选中边框测试访问器
    SelectionFrameItem* selectionFrame() const { return selectionFrame_; }

    // ★ E.4d 续-3:group handle 测试访问器
    GroupHandleItem* groupHandle() const { return groupHandle_; }

    // ★ Undo stack 访问器
    QUndoStack* undoStack() const { return undoStack_; }

    // ★ 组件访问器(测试用)
    QGraphicsView* view() const { return view_; }
    QGraphicsScene* scene() const { return scene_; }
    PropertySheet* propertySheet() const { return propertySheet_; }
    QDockWidget* rightDock() const { return rightDock_; }
    PaletteWidget* paletteWidget() const { return palette_; }   // ★ 改名避免跟 QWidget::palette() 冲突
    Project* project() const { return project_; }

    // ★ E.3:文件菜单槽
    void onFileSave();        // 保存到 currentPath_(或弹 Save As)
    void onFileSaveAs();      // 弹 QFileDialog
    void onFileOpen();        // 弹 QFileDialog + loadFromFile
    QString currentPath() const { return currentPath_; }

    // ★ E.4a:从 PaletteWidget 拖入时调(测试也可直接调,绕过 QDropEvent 复杂度)
    void handlePaletteDrop(const QString& typeName, const QPointF& scenePos);

    // ===== ★ E.7 复制 / 粘贴 / 删除 =====
    //
    // 【为什么 destroyControl 要单独一个 public 接口】removeControl 是"从画布
    //   摘下来但不销毁" —— 它只服务 clearControls 那条"先全摘再逐个 delete"
    //   的路径,中途不能 delete(还得遍历 controls_)。删除功能需要的是
    //   "摘下来 + 立刻销毁 + 清干净所有指向它的状态",两者的收尾动作不同,
    //   硬复用 removeControl 就会漏掉 handles / targets_ / 变量绑定三处。
    //   ControlListCommand 只认这一个入口。
    void destroyControl(qwhmi::widgets::ControlBase* c);

    // ★ 把一批控件设为当前选中(多选用)。走 scene 的选中通道,
    //   借 selectionChanged 把 targets_ / target_ / 选中框 / handle 全套对齐,
    //   避免这里再手写一遍同步逻辑(那是 E.4d 最容易漏的地方)。
    void selectControls(const QList<qwhmi::widgets::ControlBase*>& controls);

    // ★ E.5(2026-09-29):把 22 个控件类型全部实例化一次,填满 ControlBase 注册表。
    //   【为什么抽成 public static】原来这段"delete new XControl × 22"写在构造函数里,
    //   只有 new 过 EditorMainWindow 的人才跑过。E.5 加了 RuntimeCanvas —— 它也
    //   要 create(typeName) 重建控件,如果主画面先开 RuntimeCanvas、没开过编辑器,
    //   注册表就是空的 → create 全部返回 nullptr → 画面空白且【不报任何错】。
    //   抽出来之后两个入口共用一份列表,加控件只需改这一处。
    //   幂等:重复调用只是多 new/delete 一轮,无副作用。
    static void ensureControlTypesRegistered();

public slots:
    // ★ E.4d 对齐动作(6 个,public 让测试也能直接调)
    void onAlignLeft();
    void onAlignRight();
    void onAlignTop();
    void onAlignBottom();
    void onAlignCenterH();
    void onAlignCenterV();
    // ★ E.4d 续-7:统一尺寸动作(3 个)— 让多选控件 width/height/整体一致
    //   参考基准 = targets_.first()(与现有对齐动作保持一致)
    void onSameWidth();
    void onSameHeight();
    void onSameSize();

    // ★ E.7 复制 / 粘贴 / 删除(Ctrl+C / Ctrl+V / Delete)
    //   public slots 让测试能直接调,跟 9 个对齐动作一个待遇。
    //   三个都是幂等友好的:无选中 / 空剪贴板时静默返回,不弹窗不报错。
    void onCopy();
    void onPaste();
    void onDelete();

    // ★ E.7 测试/调试用:当前剪贴板里的控件快照
    QList<QJsonObject> clipboard() const { return clipboard_; }

private slots:
    void onSceneSelectionChanged();

    // ★ E.4d 多选移动:scene 鼠标事件钩子
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void setupCentralWidget();
    void setupDockWidgets();
    void setupMenuBar();
    void setupAlignToolbar();      // ★ E.4d
    void clearControls();   // ★ E.3:删所有 controls_(load 时清理旧画布)
    void updateAlignToolbarState();   // ★ E.4d:根据多选状态启禁用

    // ★ E.4d-follow-8(2026-09-29):几何 Undo 辅助
    //   9 个对齐/等尺寸动作共用一套快照-修改-入栈套路,抽这两个出来是为了
    //   保证 9 处写法的【完全一致】—— merge 规则依赖 actionTag 和控件集合,
    //   只要有一处漏了快照或 push,那一个动作就会变成"撤不回来"的那个。
    using ControlBase = qwhmi::widgets::ControlBase;
    QHash<ControlBase*, QRectF> snapshotGeometry() const;
    void pushGeometryCommand(const QHash<ControlBase*, QRectF>& before,
                             const QString& actionText);

    QGraphicsView*  view_          = nullptr;
    QGraphicsScene* scene_         = nullptr;
    PropertySheet*  propertySheet_ = nullptr;
    QDockWidget*    rightDock_     = nullptr;
    QDockWidget*    leftDock_      = nullptr;     // ★ E.4a:调色板 dock
    PaletteWidget*  palette_       = nullptr;     // ★ E.4a
    QUndoStack*     undoStack_     = nullptr;
    // ★ E.5:project_ 现在可能是【外部传入】的(主画面的 Project),不再必然自建。
    //   ownsProject_ 标记所有权 —— 自建的要 delete,外部给的不碰。
    //   漏了这条就是析构时把主画面的 Project 一起删了,主画面随后 loadFromFile
    //   访问已释放对象 —— 又是一次"看起来好好的,一用就崩"。
    Project*        project_       = nullptr;     // ★ E.3
    bool            ownsProject_   = false;       // ★ E.5:project_ 是否归本窗口所有
    VariableManager* varMgr_       = nullptr;     // ★ E.5:绑定变量用
    QToolBar*       alignToolbar_  = nullptr;     // ★ E.4d:对齐工具栏
    QString         currentPath_;                // ★ E.3:当前打开的 .hmi.json 路径

    // ★ E.4b:8 handle 管理(target → 8 ResizeHandleItem 实例)
    void installHandlesFor(qwhmi::widgets::ControlBase* target);
    void removeHandlesFor(qwhmi::widgets::ControlBase* target);
    void refreshHandlesFor(qwhmi::widgets::ControlBase* target);

    // ★ E.4d 续-3:多选时挂组 handle(8 个蓝色方块围组合并 boundingRect)
    void installGroupHandles(const QList<qwhmi::widgets::ControlBase*>& targets);
    void removeGroupHandles();

    // ★ E.4d 续:选中态视觉统一(蓝色虚线边框)
    void installSelectionFrame();   // 按当前 targets_ 安装
    void removeSelectionFrame();
    void updateSelectionFrame();    // 选中变化或 target 缩放/移动后重摆
    // ★ E.7:键盘事件钩子 —— Delete / Ctrl+C / Ctrl+V 走这里而不是 QAction。
    //   原因:焦点在画布时 QAction 的快捷键也能触发,但 PropertySheet 的
    //   QLineEdit 正在输入时按 Ctrl+C 应该是"复制文字",而 QAction 是窗口级的,
    //   会把画布复制一起抢走。eventFilter 能判"焦点在不在 view_ 的 viewport 上"。
    void keyPressEvent(QKeyEvent* event) override;

    // ★ E.4d-follow-10(2026-09-29):旋转后要刷两样东西 ——
    //   ① 选中框(rect 尺寸随 sceneBoundingRect 变)
    //   ② handle 光标(原来在左边的点转 90° 后到了上边,光标要跟着换)
    //   position/size 变更只影响 ①,所以旋转单独走这个槽。
    void onRotationChanged();
    void refreshHandlesCursor();

    SelectionFrameItem* selectionFrame_ = nullptr;   // ★ 当前选中边框(单选/多选合一)
    GroupHandleItem*   groupHandle_  = nullptr;   // ★ E.4d 续-3:多选组 handle 容器

    QList<qwhmi::widgets::ControlBase*> controls_;
    // ★ E.7:剪贴板内容 —— 存 JSON 快照而不是 ControlBase*(同 ControlListCommand
    //   的理由:剪贴板可能活得比控件久,存指针就是等着悬空)。
    //   为什么不走 QApplication::clipboard():系统剪贴板是进程全局的,
    //   离屏测试里读不到也不该读;编辑器内这份私有的够用,且不会跟别的程序串味。
    QList<QJsonObject> clipboard_;
    // ★ E.7:连续粘贴计数 —— 让第 N 次粘贴偏移 N×10px,否则多份完全重叠。
    //   复制时归零(换了内容就该从头偏移),撤销时也归零(撤销后剪贴板语义重置)。
    int              pasteCount_ = 0;
    qwhmi::widgets::ControlBase* target_ = nullptr;       // 单选 target(多选时为 nullptr)
    QList<qwhmi::widgets::ControlBase*> targets_;         // ★ E.4d 多选列表(包含单选)
    QPointF          multiMoveAnchor_ = QPointF();         // ★ E.4d:多选移动的上一帧 scene 坐标
    bool             editorMode_ = true;   // EditorMainWindow 默认编辑模式
};

} // namespace qwhmi::editor
