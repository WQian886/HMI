// ============================================================================
// EditorMainWindow 实现
// ============================================================================

#include "editor/EditorMainWindow.h"
#include "editor/PropertySheet.h"
#include "widgets/base/ControlBase.h"
// ★ E.3 修复:加载 JSON 时需要所有控件已注册,这里一次性实例化各控件
//   (delete new 触发构造函数里的 registerControlType,填好注册表)
#include "widgets/AlarmListControl.h"
#include "widgets/BarControl.h"
#include "widgets/ButtonControl.h"
#include "widgets/GaugeControl.h"
#include "widgets/GroupControl.h"
#include "widgets/ImageControl.h"
#include "widgets/InputEditControl.h"
#include "widgets/LabelControl.h"
#include "widgets/LedControl.h"
#include "widgets/LineControl.h"
#include "widgets/NumberDisplay.h"
#include "widgets/RectangleControl.h"
#include "widgets/SliderControl.h"
#include "widgets/TableControl.h"
#include "widgets/TextDisplay.h"
#include "widgets/ToggleControl.h"
#include "widgets/TrendControl.h"
#include "../../Project.h"

#include <QAction>
#include <QFileDialog>
#include <QDockWidget>
#include <QFileInfo>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QKeySequence>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>
#include <QUndoStack>

namespace qwhmi::editor {

EditorMainWindow::EditorMainWindow(QWidget* parent)
    : QMainWindow(parent),
      undoStack_(new QUndoStack(this)) {
    setWindowTitle(tr("HMI 编辑器 — 工程师模式"));
    resize(1024, 720);

    // ★ E.3 修复:实例化每个控件一次触发 registerControlType,确保 loadFromFile 前注册表填好
    //   (否则:JSON 里有 "Bar" 但没人 new 过 BarControl → create("Bar") 返回 null)
    using namespace qwhmi::widgets;
    delete new LabelControl;       delete new NumberDisplay;    delete new TextDisplay;
    delete new BarControl;         delete new GaugeControl;     delete new LedControl;
    delete new ImageControl;       delete new RectangleControl; delete new LineControl;
    delete new GroupControl;       delete new ButtonControl;    delete new ToggleControl;
    delete new SliderControl;      delete new InputEditControl;
    delete new TrendControl;       delete new TableControl;     delete new AlarmListControl;

    setupCentralWidget();
    setupDockWidgets();
    setupMenuBar();

    // ★ E.3:构造 Project(4 个 manager 暂传 nullptr — E.4c 集成时换成 hmi.cpp 的真实 manager)
    project_ = new Project(nullptr, nullptr, nullptr, nullptr, nullptr, this);

    // ★ E.3:监听 controlsLoaded(loadFromFile 后) → 清空旧控件 + 加入新控件
    connect(project_, &Project::controlsLoaded,
            this, [this](const QList<qwhmi::widgets::ControlBase*>& loaded) {
        clearControls();
        for (auto* c : loaded) {
            addControl(c);
        }
    });

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

    // ★ E.3:文件菜单 — 保存 / 另存为 / 打开
    auto* openAct = fileMenu->addAction(tr("打开..."));
    openAct->setShortcut(QKeySequence::Open);   // Ctrl+O
    connect(openAct, &QAction::triggered, this, &EditorMainWindow::onFileOpen);

    auto* saveAct = fileMenu->addAction(tr("保存"));
    saveAct->setShortcut(QKeySequence::Save);   // Ctrl+S
    connect(saveAct, &QAction::triggered, this, &EditorMainWindow::onFileSave);

    auto* saveAsAct = fileMenu->addAction(tr("另存为..."));
    saveAsAct->setShortcut(QKeySequence::SaveAs);   // Ctrl+Shift+S
    connect(saveAsAct, &QAction::triggered, this, &EditorMainWindow::onFileSaveAs);

    fileMenu->addSeparator();

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

// ===== E.3:文件菜单实现 =====

void EditorMainWindow::clearControls() {
    // ★ 删所有 controls_ + 从 scene 移除
    while (!controls_.isEmpty()) {
        auto* c = controls_.takeFirst();
        if (c && scene_->items().contains(c)) {
            scene_->removeItem(c);
        }
        delete c;   // ★ EditorMainWindow 拥有 controls_ 的所有权
    }
    setTarget(nullptr);
}

void EditorMainWindow::onFileSave() {
    if (currentPath_.isEmpty()) {
        onFileSaveAs();
        return;
    }
    if (project_->saveToFile(currentPath_, controls_)) {
        statusBar()->showMessage(tr("已保存到 %1").arg(currentPath_), 3000);
    } else {
        QMessageBox::warning(this, tr("保存失败"),
                             tr("写入 %1 失败,详情看 qDebug。").arg(currentPath_));
    }
}

void EditorMainWindow::onFileSaveAs() {
    QString path = QFileDialog::getSaveFileName(
        this, tr("另存为工程"), currentPath_.isEmpty() ? QStringLiteral("untitled.hmi.json") : currentPath_,
        tr("HMI 工程 (*.hmi.json);;JSON (*.json);;所有文件 (*)"));
    if (path.isEmpty()) return;
    if (!path.endsWith(QStringLiteral(".hmi.json"), Qt::CaseInsensitive) &&
        !path.endsWith(QStringLiteral(".json"), Qt::CaseInsensitive)) {
        path += QStringLiteral(".hmi.json");
    }
    currentPath_ = path;
    onFileSave();
}

void EditorMainWindow::onFileOpen() {
    QString path = QFileDialog::getOpenFileName(
        this, tr("打开工程"), currentPath_.isEmpty() ? QString() : QFileInfo(currentPath_).absolutePath(),
        tr("HMI 工程 (*.hmi.json);;JSON (*.json);;所有文件 (*)"));
    if (path.isEmpty()) return;
    currentPath_ = path;
    if (!project_->loadFromFile(currentPath_)) {
        QMessageBox::warning(this, tr("打开失败"),
                             tr("加载 %1 失败,详情看 qDebug。").arg(currentPath_));
    } else {
        statusBar()->showMessage(tr("已加载 %1").arg(currentPath_), 3000);
    }
}

} // namespace qwhmi::editor
