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
#include <QList>

class QGraphicsScene;
class QGraphicsView;
class QDockWidget;
class QUndoStack;
class QToolBar;
class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;
class Project;            // ★ E.3:文件菜单保存/加载用

namespace qwhmi::widgets { class ControlBase; }
namespace qwhmi::editor  { class PropertySheet; class PaletteWidget; class ResizeHandleItem; }

namespace qwhmi::editor {

class EditorMainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit EditorMainWindow(QWidget* parent = nullptr);
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

public slots:
    // ★ E.4d 对齐动作(6 个,public 让测试也能直接调)
    void onAlignLeft();
    void onAlignRight();
    void onAlignTop();
    void onAlignBottom();
    void onAlignCenterH();
    void onAlignCenterV();

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

    QGraphicsView*  view_          = nullptr;
    QGraphicsScene* scene_         = nullptr;
    PropertySheet*  propertySheet_ = nullptr;
    QDockWidget*    rightDock_     = nullptr;
    QDockWidget*    leftDock_      = nullptr;     // ★ E.4a:调色板 dock
    PaletteWidget*  palette_       = nullptr;     // ★ E.4a
    QUndoStack*     undoStack_     = nullptr;
    Project*        project_       = nullptr;     // ★ E.3
    QToolBar*       alignToolbar_  = nullptr;     // ★ E.4d:对齐工具栏
    QString         currentPath_;                // ★ E.3:当前打开的 .hmi.json 路径

    // ★ E.4b:8 handle 管理(target → 8 ResizeHandleItem 实例)
    void installHandlesFor(qwhmi::widgets::ControlBase* target);
    void removeHandlesFor(qwhmi::widgets::ControlBase* target);
    void refreshHandlesFor(qwhmi::widgets::ControlBase* target);

    QList<qwhmi::widgets::ControlBase*> controls_;
    qwhmi::widgets::ControlBase* target_ = nullptr;       // 单选 target(多选时为 nullptr)
    QList<qwhmi::widgets::ControlBase*> targets_;         // ★ E.4d 多选列表(包含单选)
    QPointF          multiMoveAnchor_ = QPointF();         // ★ E.4d:多选移动的上一帧 scene 坐标
    bool             editorMode_ = true;   // EditorMainWindow 默认编辑模式
};

} // namespace qwhmi::editor
