// ============================================================================
// EditorMainWindow 实现
// ============================================================================

#include "editor/EditorMainWindow.h"
#include "editor/PaletteWidget.h"
#include "editor/PropertySheet.h"
#include "editor/ResizeHandleItem.h"
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
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QDockWidget>
#include <QFileInfo>
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsView>
#include <QKeySequence>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QStatusBar>
#include <QToolBar>
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
    setupAlignToolbar();   // ★ E.4d:对齐工具栏(6 按钮)

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

    // ★ E.4d:scene 装事件过滤器,实现多选移动(拖一个全部跟移)
    scene_->installEventFilter(this);

    updateAlignToolbarState();   // ★ E.4d:初始 0 选中 → 6 对齐按钮禁用
}

EditorMainWindow::~EditorMainWindow() {
    // ★ E.4d 修复:scene 销毁过程中 selectionChanged 可能被触发,
    //   回调访问已析构的 EditorMainWindow 会导致 qFatal
    //   ("Called object is not of the correct type(class destructor may have already run)")。
    //   先清空 selection 再让 QObject 自动析构 scene。
    if (scene_) {
        scene_->clearSelection();
        scene_->disconnect(this);   // 断开所有到 this 的信号连接
    }
    if (view_) view_->disconnect(this);
}

void EditorMainWindow::setupCentralWidget() {
    scene_ = new QGraphicsScene(this);
    scene_->setSceneRect(0, 0, 1000, 700);   // E.0 决策 #8:逻辑坐标 0-1000
    view_ = new QGraphicsView(scene_, this);
    view_->setRenderHint(QPainter::Antialiasing);
    view_->setBackgroundBrush(QColor(245, 245, 245));
    // ★ E.4d:启用 RubberBandDrag 模式,空白处拖出蓝框框选多个控件
    //   Qt 原生 Ctrl+点选(加/减选中)+ 框选(空白处拖)就都生效了
    view_->setDragMode(QGraphicsView::RubberBandDrag);
    // ★ E.4a:画布接收拖拽(从 PaletteWidget 拖入新控件)
    //   QGraphicsView 没有 setDragDropMode — 直接设 viewport 接受 drop
    //   drop event 实际走 viewport(),所以 eventFilter 装在 viewport 上
    view_->setAcceptDrops(true);
    view_->viewport()->setAcceptDrops(true);
    view_->viewport()->installEventFilter(this);
    setCentralWidget(view_);
}

void EditorMainWindow::setupDockWidgets() {
    // ★ E.4a:左侧 dock:控件调色板(拖源)
    leftDock_ = new QDockWidget(tr("控件调色板"), this);
    leftDock_->setObjectName("PaletteDock");
    leftDock_->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    palette_ = new PaletteWidget(leftDock_);
    leftDock_->setWidget(palette_);
    addDockWidget(Qt::LeftDockWidgetArea, leftDock_);

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

    // ★ E.4b:旧 target 卸 handle
    if (target_) removeHandlesFor(target_);

    target_ = target;
    if (propertySheet_) propertySheet_->setTarget(target);

    // ★ E.4b:新 target 挂 handle
    if (target_) {
        installHandlesFor(target_);
        // ★ 监听 target 移动/缩放 → 重排 handle
        connect(target_, &qwhmi::widgets::ControlBase::sizeChanged,
                this, [this, target]() { refreshHandlesFor(target); });
    }
}

void EditorMainWindow::onSceneSelectionChanged() {
    // ★ 场景选中变化 → 自动 setTarget + 跟踪多选列表
    const auto sel = scene_->selectedItems();

    // ★ 卸旧 target 的 handle(单选情况)
    if (target_) removeHandlesFor(target_);

    // ★ 收集所有 ControlBase 类型的选中项
    QList<qwhmi::widgets::ControlBase*> cbs;
    for (QGraphicsItem* item : sel) {
        if (auto* cb = dynamic_cast<qwhmi::widgets::ControlBase*>(item)) {
            cbs.append(cb);
        }
    }

    if (cbs.isEmpty()) {
        targets_.clear();
        target_ = nullptr;
    } else if (cbs.size() == 1) {
        targets_ = cbs;
        target_ = cbs.first();
    } else {
        // ★ E.4d 多选
        targets_ = cbs;
        target_ = nullptr;       // 多选时无单一 target(单独 handle 暂不实现组 handle)
    }

    // ★ PropertySheet:暂只支持单选 target,多选时传 nullptr(显示"未选中")
    if (propertySheet_) propertySheet_->setTarget(target_);

    // ★ 装新 target 的 handle(单选 8 个,多选暂不装 — 后续 commit 加组 handle)
    if (target_) installHandlesFor(target_);

    updateAlignToolbarState();   // ★ E.4d:多选状态变化时更新工具栏按钮
}

// ===== E.4d:对齐工具栏 =====
void EditorMainWindow::setupAlignToolbar() {
    alignToolbar_ = addToolBar(tr("对齐(&A)"));
    alignToolbar_->setObjectName("AlignToolbar");
    alignToolbar_->setIconSize(QSize(16, 16));

    auto make = [this](const QString& text, void (EditorMainWindow::*slot)()) {
        auto* a = new QAction(text, this);
        connect(a, &QAction::triggered, this, slot);
        alignToolbar_->addAction(a);
        return a;
    };
    make(tr("左对齐"),   &EditorMainWindow::onAlignLeft);
    make(tr("右对齐"),   &EditorMainWindow::onAlignRight);
    make(tr("顶对齐"),   &EditorMainWindow::onAlignTop);
    make(tr("底对齐"),   &EditorMainWindow::onAlignBottom);
    make(tr("水平居中"), &EditorMainWindow::onAlignCenterH);
    make(tr("垂直居中"), &EditorMainWindow::onAlignCenterV);
}

void EditorMainWindow::updateAlignToolbarState() {
    if (!alignToolbar_) return;
    // ★ 6 按钮:targets_.size() >= 2 才启用
    const bool enabled = hasMultipleSelection();
    for (QAction* a : alignToolbar_->actions()) {
        a->setEnabled(enabled);
    }
}

// ★ 对齐槽实现:以 targets_ 的第一个为参考,所有目标按指定边界对齐
void EditorMainWindow::onAlignLeft() {
    if (targets_.size() < 2) return;
    const qreal x = targets_.first()->position().x();
    for (auto* t : targets_) {
        if (t == targets_.first()) continue;
        t->setPosition(QPointF(x, t->position().y()));
    }
}
void EditorMainWindow::onAlignRight() {
    if (targets_.size() < 2) return;
    const qreal right = targets_.first()->position().x() + targets_.first()->size().width();
    for (auto* t : targets_) {
        if (t == targets_.first()) continue;
        const qreal newX = right - t->size().width();
        t->setPosition(QPointF(newX, t->position().y()));
    }
}
void EditorMainWindow::onAlignTop() {
    if (targets_.size() < 2) return;
    const qreal y = targets_.first()->position().y();
    for (auto* t : targets_) {
        if (t == targets_.first()) continue;
        t->setPosition(QPointF(t->position().x(), y));
    }
}
void EditorMainWindow::onAlignBottom() {
    if (targets_.size() < 2) return;
    const qreal bottom = targets_.first()->position().y() + targets_.first()->size().height();
    for (auto* t : targets_) {
        if (t == targets_.first()) continue;
        const qreal newY = bottom - t->size().height();
        t->setPosition(QPointF(t->position().x(), newY));
    }
}
void EditorMainWindow::onAlignCenterH() {
    if (targets_.size() < 2) return;
    const qreal centerX = targets_.first()->position().x() + targets_.first()->size().width() / 2.0;
    for (auto* t : targets_) {
        if (t == targets_.first()) continue;
        t->setPosition(QPointF(centerX - t->size().width() / 2.0, t->position().y()));
    }
}
void EditorMainWindow::onAlignCenterV() {
    if (targets_.size() < 2) return;
    const qreal centerY = targets_.first()->position().y() + targets_.first()->size().height() / 2.0;
    for (auto* t : targets_) {
        if (t == targets_.first()) continue;
        t->setPosition(QPointF(t->position().x(), centerY - t->size().height() / 2.0));
    }
}

// ===== E.4a:画布 drop event 处理 =====

void EditorMainWindow::handlePaletteDrop(const QString& typeName, const QPointF& scenePos) {
    if (typeName.isEmpty()) return;
    auto* ctrl = qwhmi::widgets::ControlBase::create(typeName);
    if (!ctrl) {
        qWarning() << "[EditorMainWindow] 未知控件类型:" << typeName;
        return;
    }
    ctrl->setPosition(scenePos);
    addControl(ctrl);
}

// ===== E.4b:8 handle 管理 =====
//   这些薄包装委托给 ResizeHandleItem 静态方法,避免重复实现

void EditorMainWindow::installHandlesFor(qwhmi::widgets::ControlBase* target) {
    ResizeHandleItem::installHandlesFor(target);
}

void EditorMainWindow::removeHandlesFor(qwhmi::widgets::ControlBase* target) {
    ResizeHandleItem::removeHandlesFor(target);
}

void EditorMainWindow::refreshHandlesFor(qwhmi::widgets::ControlBase* target) {
    ResizeHandleItem::layoutHandlesFor(target);
}

bool EditorMainWindow::eventFilter(QObject* watched, QEvent* event) {
    // ★ E.4d:scene 事件 — 多选移动(mousePress 记录 anchor,mouseMove 把 delta 平移到所有其他 targets_)
    if (watched == scene_) {
        if (targets_.size() >= 2) {
            auto* me = static_cast<QGraphicsSceneMouseEvent*>(event);
            QGraphicsItem* grabber = scene_->mouseGrabberItem();
            auto* grabberCb = dynamic_cast<qwhmi::widgets::ControlBase*>(grabber);
            const bool grabberIsInMultiSel = grabberCb && targets_.contains(grabberCb);

            if (event->type() == QEvent::GraphicsSceneMousePress) {
                if (grabberIsInMultiSel) {
                    multiMoveAnchor_ = me->scenePos();
                }
            } else if (event->type() == QEvent::GraphicsSceneMouseMove) {
                if (grabberIsInMultiSel) {
                    const QPointF delta = me->scenePos() - multiMoveAnchor_;
                    multiMoveAnchor_ = me->scenePos();
                    for (auto* t : targets_) {
                        if (t != grabberCb) {
                            t->moveBy(delta.x(), delta.y());
                        }
                    }
                }
            } else if (event->type() == QEvent::GraphicsSceneMouseRelease) {
                multiMoveAnchor_ = QPointF();
            }
        }
        return false;   // 不消费,让 scene 继续分发
    }

    if (watched != view_->viewport()) return QMainWindow::eventFilter(watched, event);

    if (event->type() == QEvent::DragEnter || event->type() == QEvent::DragMove) {
        auto* de = static_cast<QDragEnterEvent*>(event);
        if (de->mimeData()->hasFormat(PaletteWidget::controlMimeType())) {
            de->acceptProposedAction();
            return true;
        }
        return false;
    }

    if (event->type() == QEvent::Drop) {
        auto* de = static_cast<QDropEvent*>(event);
        const QMimeData* mime = de->mimeData();
        if (!mime->hasFormat(PaletteWidget::controlMimeType())) return false;
        const QString typeName = QString::fromUtf8(
            mime->data(PaletteWidget::controlMimeType()));
        // ★ 委托给 handlePaletteDrop(也供测试直接调,绕过 QDropEvent 复杂构造)
        handlePaletteDrop(typeName, view_->mapToScene(de->pos()));
        de->acceptProposedAction();
        return true;
    }

    return QMainWindow::eventFilter(watched, event);
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
