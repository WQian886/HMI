// ============================================================================
// EditorMainWindow 实现
// ============================================================================

#include "editor/EditorMainWindow.h"
#include "editor/PaletteWidget.h"
#include "editor/PropertySheet.h"
#include "editor/ResizeHandleItem.h"
#include "editor/SelectionFrameItem.h"
#include "editor/SizeEditor.h"
#include "editor/PositionEditor.h"
#include "editor/GroupHandleItem.h"
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
// ★ E.1c(2026-09-28):5 个工业控件的 include 也必须跟着预热列表一起加
//   (漏 include 是编译期 error,漏列表项才是运行期静默丢失 —— 两者要一起改)
#include "widgets/TankControl.h"
#include "widgets/PipeControl.h"
#include "widgets/ValveControl.h"
#include "widgets/PumpControl.h"
#include "widgets/MotorControl.h"
#include "Project.h"  // 修正:从相对路径 "../../Project.h" 改为工程根 include(vcxproj + CMake 都已配 target_include_directories(hmi PRIVATE .))

#include <QAction>
#include <QDragEnterEvent>
#include <QTimer>
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
    //
    // ★★ 2026-09-28 加控件时【必须同时往这里加一行】,漏了不会有任何编译错误 ——
    //   只是那个控件:① 调色板上不出现 ② 工程文件里存着它,loadFromFile
    //   静默跳过。两个症状都是"看起来像坏了但说不清哪坏",极难定位。
    //   少一个控件的判断方法:调色板项数 < 22。tst_EditorMainWindow 有对应断言。
    using namespace qwhmi::widgets;
    delete new LabelControl;       delete new NumberDisplay;    delete new TextDisplay;
    delete new BarControl;         delete new GaugeControl;     delete new LedControl;
    delete new ImageControl;       delete new RectangleControl; delete new LineControl;
    delete new GroupControl;       delete new ButtonControl;    delete new ToggleControl;
    delete new SliderControl;      delete new InputEditControl;
    delete new TrendControl;       delete new TableControl;     delete new AlarmListControl;
    // ★ E.1c(2026-09-28):5 个工业控件(储罐/管道/阀门/泵/电机)
    delete new TankControl;        delete new PipeControl;      delete new ValveControl;
    delete new PumpControl;        delete new MotorControl;

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
    // ★ E.4d/E.4d-续 安全析构顺序:
    //   1) 删除 SelectionFrameItem(它可能引用 scene_/controls_)— removeSelectionFrame
    //      同时断所有 frame 监听 ControlBase::positionChanged/sizeChanged 的连接
    //   2) 断开所有到 this 的信号连接(防止半析构回调)
    //   3) 才让 scene 清空 selection(不会触发我们的回调)
    removeSelectionFrame();
    if (scene_) {
        scene_->disconnect(this);
        scene_->clearSelection();
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
    //   Qt 在 RubberBandDrag 下:拖框 = 多选;Ctrl+点选 = 切换;Shift+点选 = 范围;
    //   普通点选 = 覆盖(单选)。
    //   注意:QGraphicsScene 没有 setSelectionMode,选择模式由 view 的 drag mode
    //   + 修饰键共同决定。
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
        // ★ 监听 target 缩放 → 重排 handle
        connect(target_, &qwhmi::widgets::ControlBase::sizeChanged,
                this, [this, t = target_]() {
                    refreshHandlesFor(t);
                });
    }
}

void EditorMainWindow::onSceneSelectionChanged() {
    // ★ 场景选中变化 → 自动 setTarget + 跟踪多选列表
    const auto sel = scene_->selectedItems();

    // ★ 卸旧 target 的 handle(单选情况)
    if (target_) removeHandlesFor(target_);
    // ★ 卸旧 group handle(多选情况)
    removeGroupHandles();

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

    // ★ PropertySheet:单选/多选用 setTargets(空时显示"未选中控件")
    if (propertySheet_) propertySheet_->setTargets(targets_);

    // ★ 装新 target 的 handle(单选 8 个;多选时挂 8 个组 handle)
    if (target_) {
        installHandlesFor(target_);
    } else if (targets_.size() >= 2) {
        installGroupHandles(targets_);
    }

    // ★ E.4d 续-5:实装 SelectionFrame — 单选/多选都装,rect = union
    //   installSelectionFrame 内部幂等(先 removeSelectionFrame 再装),直接调即可
    if (targets_.isEmpty()) {
        removeSelectionFrame();
    } else {
        installSelectionFrame();
    }

    // ★ E.4d 续修复:只有单选(target_ 非空)才 connect 缩放/位置信号,
    //   多选时不能 connect target_(nullptr 会触发 qFatal — Qt 6 严格模式)。
    //   多选的位置/大小同步由 selectionChanged 触发 + 后续 align 操作时一次性刷新。
    if (target_) {
        connect(target_, &qwhmi::widgets::ControlBase::sizeChanged,
                this, [this, t = target_]() {
                    refreshHandlesFor(t);
                });
    }

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
    // ★ E.4d 续-7:统一尺寸 — 把 6 对齐和 3 尺寸用分隔条分开
    alignToolbar_->addSeparator();
    make(tr("等宽"), &EditorMainWindow::onSameWidth);
    make(tr("等高"), &EditorMainWindow::onSameHeight);
    make(tr("等大"), &EditorMainWindow::onSameSize);
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

// ===== E.4d 续-7:统一尺寸(3 个)=====
//   参考基准 = targets_.first()(第 0 个选中控件),与现有 6 个对齐动作保持一致
//   直调 setSize:内部 prepareGeometryChange + 更新 size_ + emit sizeChanged,
//   后续 SelectionFrame / GroupHandle 监听 sizeChanged 自动跟随。
//   注:与对齐一样目前无 QUndoCommand 包裹(E.4d 收尾阶段统一加)。
void EditorMainWindow::onSameWidth() {
    if (targets_.size() < 2) return;
    const qreal refW = targets_.first()->size().width();
    for (auto* t : targets_) {
        if (t == targets_.first()) continue;
        const QSizeF s = t->size();
        if (qFuzzyCompare(s.width(), refW)) continue;   // 已经是 ref 宽度,跳过
        t->setSize(QSizeF(refW, s.height()));
    }
}
void EditorMainWindow::onSameHeight() {
    if (targets_.size() < 2) return;
    const qreal refH = targets_.first()->size().height();
    for (auto* t : targets_) {
        if (t == targets_.first()) continue;
        const QSizeF s = t->size();
        if (qFuzzyCompare(s.height(), refH)) continue;
        t->setSize(QSizeF(s.width(), refH));
    }
}
void EditorMainWindow::onSameSize() {
    if (targets_.size() < 2) return;
    const QSizeF refS = targets_.first()->size();
    for (auto* t : targets_) {
        if (t == targets_.first()) continue;
        if (t->size() == refS) continue;   // QSizeF == 支持浮点容差(Qt 6 内置)
        t->setSize(refS);
    }
}

// ===== E.4d 续:选中态视觉统一(SelectionFrameItem) =====
//   单选:frame.rect = target.sceneBoundingRect
//   多选:frame.rect = 所有 targets.sceneBoundingRect 的 union
//   ★ 关键:接每个 target 的 positionChanged + sizeChanged → updateSelectionFrame,
//     这是为什么"多选整体移动后 frame 跟随"的实现点(E.4d 续-5 bug 修复)。
//   采用 GroupHandleItem 同款 trick:pos = unionRect.topLeft,rect = 本地 (0,0,w,h),
//     这样 frame 跟着 union 平移时,Qt scene 自动重绘;不需要我们手动平移 frame。
void EditorMainWindow::installSelectionFrame() {
    // ★ 先清旧 frame(同时断旧信号),防止 selection 切换后 frame 没刷新
    removeSelectionFrame();
    if (targets_.isEmpty()) return;

    selectionFrame_ = new SelectionFrameItem;
    scene_->addItem(selectionFrame_);

    // ★ 接每个 target 的 positionChanged + sizeChanged,无论单选/多选都接
    for (auto* t : targets_) {
        if (!t) continue;
        connect(t, &qwhmi::widgets::ControlBase::positionChanged,
                this, &EditorMainWindow::updateSelectionFrame);
        connect(t, &qwhmi::widgets::ControlBase::sizeChanged,
                this, &EditorMainWindow::updateSelectionFrame);
    }

    updateSelectionFrame();   // 设初始 rect
}

void EditorMainWindow::removeSelectionFrame() {
    if (!selectionFrame_) return;

    // ★ 断所有旧信号(防止析构后 dangling lambda 回调)
    for (auto* t : targets_) {
        if (!t) continue;
        disconnect(t, &qwhmi::widgets::ControlBase::positionChanged,
                   this, &EditorMainWindow::updateSelectionFrame);
        disconnect(t, &qwhmi::widgets::ControlBase::sizeChanged,
                   this, &EditorMainWindow::updateSelectionFrame);
    }

    scene_->removeItem(selectionFrame_);
    delete selectionFrame_;
    selectionFrame_ = nullptr;
}

void EditorMainWindow::updateSelectionFrame() {
    if (!selectionFrame_ || targets_.isEmpty()) {
        if (selectionFrame_) selectionFrame_->setVisible(false);
        return;
    }

    // ★ 算 union sceneBoundingRect
    QRectF unionRect;
    bool first = true;
    for (auto* t : targets_) {
        if (!t) continue;
        const QRectF sb = t->sceneBoundingRect();
        if (sb.isEmpty()) continue;
        if (first) { unionRect = sb; first = false; }
        else        unionRect = unionRect.united(sb);
    }
    if (unionRect.isEmpty()) {
        selectionFrame_->setVisible(false);
        return;
    }

    // ★ frame 是 scene-level item,pos+rect 同 GroupHandleItem 模式
    selectionFrame_->setPos(unionRect.topLeft());
    selectionFrame_->setRect(QRectF(0, 0, unionRect.width(), unionRect.height()));
    selectionFrame_->setVisible(true);
}

// ===== E.4d 续-3:多选组 handle =====
void EditorMainWindow::installGroupHandles(const QList<qwhmi::widgets::ControlBase*>& targets) {
    if (targets.size() < 2) return;

    if (!groupHandle_) {
        // ★ 虚拟组容器:rect = 合并 bbox,8 个 handle 挂上去
        //   QGraphicsRectItem 不能以 QGraphicsScene 为 parent,先 nullptr 再 addItem
        groupHandle_ = new GroupHandleItem;
        scene_->addItem(groupHandle_);
    }
    groupHandle_->setTargets(targets);
    // ★ mousePress 时快照所有 target 的原 rect(group 内部存)
    //   不在 handle 自己的 handleMousePress 里调,因为 group 不知道 mousePress 时机
    //   解决:监听每个 handle 的 sizeChanged / positionChanged? 不,太啰嗦
    //   更简单:直接在 group 里 listen scene 的 selectionChanged → 拍照
    //   或者:在 EditorMainWindow 里 connect handles 的 handleMousePress
    //   最简单:让 ResizeHandleItem 在 mousePress 时主动告诉 group 拍照
    //
    // 实际上 ResizeHandleItem::handleMousePress 在 EditMode 测试中显示
    // 它的 size/pos 来自 target_->boundingRect().size()/pos().
    // 我们需要在 userPress 时 group 拍照 — 通过 QTimer::singleShot(0)
    // 或者直接调 group->captureOriginalRects()
    GroupHandleItem::installHandlesFor(groupHandle_);
    groupHandle_->captureOriginalRects();
    GroupHandleItem::layoutHandlesFor(groupHandle_);
}

void EditorMainWindow::removeGroupHandles() {
    if (groupHandle_) {
        GroupHandleItem::removeHandlesFor(groupHandle_);
        delete groupHandle_;
        groupHandle_ = nullptr;
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
    // ★ E.4d 续-4:删掉自定义 multi-move anchor 平移 — Qt ItemIsMovable 已自动处理
    //   所有 selected items 一起按增量移动,不需要我们手动 translate。每个 item 自带
    //   positionChanged 信号,group handle 会自动跟随。
    if (watched == scene_) {
        Q_UNUSED(event);
        return false;   // 不消费,Qt 自己处理 multi-move
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
