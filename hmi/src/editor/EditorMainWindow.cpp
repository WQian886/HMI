// ============================================================================
// EditorMainWindow 实现
// ============================================================================

#include "editor/EditorMainWindow.h"
#include "editor/PropertySheet.h"
#include "widgets/base/ControlBase.h"

#include <QAction>
#include <QDockWidget>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QKeySequence>
#include <QMenuBar>
#include <QUndoStack>

namespace qwhmi::editor {

EditorMainWindow::EditorMainWindow(QWidget* parent)
    : QMainWindow(parent),
      undoStack_(new QUndoStack(this)) {
    setWindowTitle(tr("HMI 编辑器 — 工程师模式"));
    resize(1024, 720);

    setupCentralWidget();
    setupDockWidgets();
    setupMenuBar();

    // ★ 选中变化 → 同步 PropertySheet target
    connect(scene_, &QGraphicsScene::selectionChanged,
            this, &EditorMainWindow::onSceneSelectionChanged);
}

EditorMainWindow::~EditorMainWindow() = default;

void EditorMainWindow::setupCentralWidget() {
    scene_ = new QGraphicsScene(this);
    scene_->setSceneRect(0, 0, 1000, 700);   // E.0 决策 #8:逻辑坐标 0-1000
    view_ = new QGraphicsView(scene_, this);
    view_->setRenderHint(QPainter::Antialiasing);
    view_->setBackgroundBrush(QColor(245, 245, 245));
    setCentralWidget(view_);
}

void EditorMainWindow::setupDockWidgets() {
    // ★ 右侧 dock:PropertySheet(E.2 反射属性面板)
    rightDock_ = new QDockWidget(tr("属性"), this);
    rightDock_->setObjectName("PropertyDock");
    rightDock_->setAllowedAreas(Qt::RightDockWidgetArea | Qt::LeftDockWidgetArea);

    propertySheet_ = new PropertySheet(undoStack_, rightDock_);
    rightDock_->setWidget(propertySheet_);

    addDockWidget(Qt::RightDockWidgetArea, rightDock_);
}

void EditorMainWindow::setupMenuBar() {
    auto* fileMenu = menuBar()->addMenu(tr("文件"));
    // (E.4c 接入 JSON 存读)

    auto* editMenu = menuBar()->addMenu(tr("编辑"));
    auto* undoAct = undoStack_->createUndoAction(this, tr("撤销"));
    undoAct->setShortcut(QKeySequence::Undo);
    editMenu->addAction(undoAct);

    auto* redoAct = undoStack_->createRedoAction(this, tr("重做"));
    redoAct->setShortcut(QKeySequence::Redo);
    editMenu->addAction(redoAct);

    // ★ 模式切换
    auto* viewMenu = menuBar()->addMenu(tr("视图"));
    auto* modeAct = new QAction(tr("运行时模式"), this);
    modeAct->setCheckable(true);
    modeAct->setChecked(!editorMode_);
    connect(modeAct, &QAction::toggled, this, &EditorMainWindow::setEditorMode);
    viewMenu->addAction(modeAct);
}

void EditorMainWindow::setEditorMode(bool enabled) {
    editorMode_ = enabled;
    // ★ 编辑模式:属性面板 enabled,运行时模式 disabled
    if (propertySheet_) {
        propertySheet_->setEnabled(editorMode_);
    }
}

void EditorMainWindow::addControl(qwhmi::widgets::ControlBase* c) {
    if (!c) return;
    if (controls_.contains(c)) return;
    controls_.append(c);
    scene_->addItem(c);
    // 注:不在这里 setSelected(true) — 避免循环触发 onSceneSelectionChanged → setTarget
    //     → setSelected 的递归。选中由 setTarget() 或用户点击触发。
}

void EditorMainWindow::removeControl(qwhmi::widgets::ControlBase* c) {
    if (!c) return;
    controls_.removeAll(c);
    if (target_ == c) setTarget(nullptr);
    scene_->removeItem(c);
}

void EditorMainWindow::setTarget(qwhmi::widgets::ControlBase* target) {
    if (target_ == target) return;
    target_ = target;
    if (propertySheet_) propertySheet_->setTarget(target);
    // 注:不在这里改 scene 选中状态 — 选中→setTarget 是单向的(由场景触发),
    //     反向同步会引入循环。E.4a 调色板拖入时直接调 setTarget + 同步选中。
}

void EditorMainWindow::onSceneSelectionChanged() {
    // ★ 场景选中变化 → 自动 setTarget
    const auto sel = scene_->selectedItems();
    if (sel.isEmpty()) {
        setTarget(nullptr);
    } else if (sel.size() == 1) {
        // ControlBase 是 QGraphicsObject 派生,QGraphicsItem 多继承安全 downcast
        auto* cb = dynamic_cast<qwhmi::widgets::ControlBase*>(sel.first());
        setTarget(cb);
    }
    // 多选暂不处理(E.4d 范围)
}

} // namespace qwhmi::editor
