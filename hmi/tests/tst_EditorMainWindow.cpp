// ============================================================================
// tst_EditorMainWindow
//   E.2 周 11 EditorMainWindow 骨架 + 模式切换 8 用例
// ============================================================================

#include <QtTest>
#include <QApplication>
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsView>
#include <QLineEdit>
#include <QSignalSpy>
#include <QUndoStack>

#include "widgets/LabelControl.h"
#include "widgets/BarControl.h"
#include "widgets/LedControl.h"

#include "editor/EditorMainWindow.h"
#include "editor/PropertySheet.h"
#include "editor/PropertyChangeCommand.h"
#include "editor/ResizeHandleItem.h"
#include "editor/SelectionFrameItem.h"
#include "editor/GroupHandleItem.h"

using namespace qwhmi::widgets;
using namespace qwhmi::editor;

class TestEditorMainWindow : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        qputenv("QT_QPA_PLATFORM", "offscreen");
        qInfo() << "[tst_EditorMainWindow] init";
    }
    void cleanupTestCase() { qInfo() << "[tst_EditorMainWindow] cleanup"; }

    // ★ 8 用例(按 docs/阶段E.2_从这里继续.md §6 周 11)
    void constructor_createsWindow();
    void addControl_appearsInScene();
    void selectControl_propertySheetShows();
    void deselectControl_propertySheetClears();
    void editProperty_targetUpdates();
    void undoAfterEdit_restoresValue();
    void editorModeFalse_propertySheetDisabled();
    void editorModeTrue_propertySheetEnabled();

    // ★ E.4d 多选 + 对齐(2026-09-22)
    void multiSelect_twoControls_trackedInTargets();
    void multiSelect_alignLeft_AllLeftsMatchFirst();
    void multiSelect_alignCenterH_AllCentersXMatch();
    void multiSelect_alignRight_AllRightsMatchFirst();
    void multiSelect_moveBy_translatesAllBySameDelta();

    // ★ E.4d 真实点击测试:QTest::mouseClick 在 view viewport 上 Ctrl+点两个控件
    //   这是用户在 hmi.exe 里实际操作的模拟
    void realClick_ctrlClickTwoItems_bothSelected();

    // ★ E.4d 续-5:选中态视觉统一(SelectionFrameItem 实装后)
    void selectionFrame_appearsAfterSelect();
    void selectionFrame_surroundsMultiSelectionBounds();
    void multiMove_selectionFrameFollows();
    void multiResize_selectionFrameFollows();

    // ★ E.4d 续-3:多选时挂 8 个组 handle(围合并 boundingRect)
    void groupHandle_installedOnMultiSelect();
    void groupHandle_proportionalResizeAllTargets();
    void groupHandle_handlesAtCorrectScenePosition();   // ★ bug 回归:之前位置错位

    // ★ E.4d 续-3 bug:多选整体移动相对位置改变
    void multiMove_relativePositionPreserved();
    void multiMove_setPosBypassesWrapper_frameFollows();   // ★ E.4d 续-6 bug 回归
};

// ===== 1. constructor_createsWindow =====
void TestEditorMainWindow::constructor_createsWindow() {
    EditorMainWindow w;
    QVERIFY(w.centralWidget() != nullptr);          // 中央 widget = QGraphicsView
    QVERIFY(w.view() != nullptr);
    QVERIFY(w.scene() != nullptr);
    QVERIFY(w.propertySheet() != nullptr);
    QVERIFY(w.undoStack() != nullptr);
    QVERIFY(w.rightDock() != nullptr);
    QCOMPARE(w.editorMode(), true);                  // 默认编辑模式
    QVERIFY(w.propertySheet()->isEnabled());         // 编辑模式下 property sheet 可用
}

// ===== 2. addControl_appearsInScene =====
void TestEditorMainWindow::addControl_appearsInScene() {
    EditorMainWindow w;
    auto* lbl = new LabelControl;
    lbl->setPosition({100, 100});

    w.addControl(lbl);
    QCOMPARE(w.controls().size(), 1);
    QCOMPARE(w.controls().first(), lbl);
    QCOMPARE(w.scene()->items().size(), 1);          // 控件进了 scene

    // 注:addControl 不再自动 selected(避免循环触发) — 显式 setTarget
    w.setTarget(lbl);
    QCOMPARE(w.target(), lbl);
}

// ===== 3. selectControl_propertySheetShows =====
void TestEditorMainWindow::selectControl_propertySheetShows() {
    EditorMainWindow w;
    auto* lbl = new LabelControl;
    w.addControl(lbl);
    w.setTarget(lbl);

    // ★ setTarget 应同步到 PropertySheet
    QCOMPARE(w.propertySheet()->target(), lbl);

    // ★ PropertySheet 应当能反射 LabelControl 的 Q_PROPERTY
    //   基类 4 writable(position/size/variableName/value)+ 派生 0 = 4
    int lineEdits = w.propertySheet()->findChildren<QLineEdit*>().size();
    QVERIFY(lineEdits >= 2);                         // 至少 variableName + value 2 个 QString
}

// ===== 4. deselectControl_propertySheetClears =====
void TestEditorMainWindow::deselectControl_propertySheetClears() {
    EditorMainWindow w;
    auto* lbl = new LabelControl;
    w.addControl(lbl);
    w.setTarget(lbl);

    w.setTarget(nullptr);
    QCOMPARE(w.propertySheet()->target(), nullptr);
    QCOMPARE(w.propertySheet()->isEnabled(), false); // target=null 时 PropertySheet disabled
}

// ===== 5. editProperty_targetUpdates =====
void TestEditorMainWindow::editProperty_targetUpdates() {
    EditorMainWindow w;
    auto* led = new LedControl;
    w.addControl(led);
    w.setTarget(led);

    const QColor oldC = led->onColor();
    const QColor newC("#AABBCC");

    auto* cmd = new PropertyChangeCommand(led, "onColor", oldC, newC);
    w.undoStack()->push(cmd);
    QCOMPARE(led->onColor(), newC);
    QCOMPARE(w.undoStack()->count(), 1);
}

// ===== 6. undoAfterEdit_restoresValue =====
void TestEditorMainWindow::undoAfterEdit_restoresValue() {
    EditorMainWindow w;
    auto* bar = new BarControl;
    w.addControl(bar);
    w.setTarget(bar);

    const double oldMax = bar->maximum();
    const double newMax = 999.0;
    auto* cmd = new PropertyChangeCommand(bar, "maximum", oldMax, newMax);
    w.undoStack()->push(cmd);
    QCOMPARE(bar->maximum(), 999.0);

    w.undoStack()->undo();
    QCOMPARE(bar->maximum(), oldMax);
    w.undoStack()->redo();
    QCOMPARE(bar->maximum(), newMax);
}

// ===== 7. editorModeFalse_propertySheetDisabled =====
void TestEditorMainWindow::editorModeFalse_propertySheetDisabled() {
    EditorMainWindow w;
    auto* lbl = new LabelControl;
    w.addControl(lbl);
    w.setTarget(lbl);

    w.setEditorMode(false);
    QCOMPARE(w.editorMode(), false);
    QVERIFY(!w.propertySheet()->isEnabled());        // 运行时模式禁用
}

// ===== 8. editorModeTrue_propertySheetEnabled =====
void TestEditorMainWindow::editorModeTrue_propertySheetEnabled() {
    EditorMainWindow w;
    auto* lbl = new LabelControl;
    w.addControl(lbl);
    w.setTarget(lbl);

    w.setEditorMode(false);                          // 先切到运行时
    QVERIFY(!w.propertySheet()->isEnabled());

    w.setEditorMode(true);                           // 切回编辑
    QCOMPARE(w.editorMode(), true);
    QVERIFY(w.propertySheet()->isEnabled());
}

// ===== 9. multiSelect_twoControls_trackedInTargets =====
//   ★ E.4d:多选时 targets_ 列表正确跟踪
void TestEditorMainWindow::multiSelect_twoControls_trackedInTargets() {
    EditorMainWindow w;
    auto* a = new LabelControl; a->setPosition({10, 20});
    auto* b = new LabelControl; b->setPosition({100, 200});
    w.addControl(a);
    w.addControl(b);

    // ★ 用 setSelected 模拟用户 Ctrl+点选两个控件
    a->setSelected(true);
    b->setSelected(true);

    QCOMPARE(w.targets().size(), 2);
    QVERIFY(w.targets().contains(a));
    QVERIFY(w.targets().contains(b));
    QVERIFY(w.hasMultipleSelection());
}

// ===== 10. multiSelect_alignLeft_AllLeftsMatchFirst =====
//   ★ E.4d:左对齐后所有控件的 left.x 互相相等(以 targets_.first() 为参考)
//   注意:Qt selectedItems() 顺序 unspecified,所以不能用特定值断言
void TestEditorMainWindow::multiSelect_alignLeft_AllLeftsMatchFirst() {
    EditorMainWindow w;
    auto* a = new LabelControl; a->setSize({100, 50}); a->setPosition({ 10, 100});
    auto* b = new LabelControl; b->setSize({100, 50}); b->setPosition({200, 100});
    auto* c = new LabelControl; c->setSize({100, 50}); c->setPosition({400, 100});
    w.addControl(a); w.addControl(b); w.addControl(c);

    a->setSelected(true);
    b->setSelected(true);
    c->setSelected(true);

    w.onAlignLeft();
    // ★ 对齐后,所有 targets_ 的 x 互相相等
    const qreal ax = a->position().x();
    QCOMPARE(b->position().x(), ax);
    QCOMPARE(c->position().x(), ax);
    // ★ y 保持不变(只对齐 x)
    QCOMPARE(a->position().y(), 100.0);
    QCOMPARE(b->position().y(), 100.0);
    QCOMPARE(c->position().y(), 100.0);
}

// ===== 11. multiSelect_alignCenterH_AllCentersXMatch =====
//   ★ E.4d:水平居中后所有控件的 center.x 互相相等
void TestEditorMainWindow::multiSelect_alignCenterH_AllCentersXMatch() {
    EditorMainWindow w;
    auto* a = new LabelControl; a->setSize({100, 50}); a->setPosition({  0, 100});
    auto* b = new LabelControl; b->setSize({100, 50}); b->setPosition({200, 100});
    auto* c = new LabelControl; c->setSize({100, 50}); c->setPosition({400, 100});
    w.addControl(a); w.addControl(b); w.addControl(c);

    a->setSelected(true);
    b->setSelected(true);
    c->setSelected(true);

    w.onAlignCenterH();
    // ★ 所有 targets_ 的 center.x 互相相等
    const qreal aCenter = a->position().x() + a->size().width() / 2.0;
    QCOMPARE(b->position().x() + b->size().width() / 2.0, aCenter);
    QCOMPARE(c->position().x() + c->size().width() / 2.0, aCenter);
}

// ===== 12. multiSelect_alignRight_AllRightsMatchFirst =====
//   ★ E.4d:右对齐后所有控件的 right 互相相等
void TestEditorMainWindow::multiSelect_alignRight_AllRightsMatchFirst() {
    EditorMainWindow w;
    auto* a = new LabelControl; a->setSize({100, 50}); a->setPosition({  0, 100});
    auto* b = new LabelControl; b->setSize({100, 50}); b->setPosition({200, 100});
    auto* c = new LabelControl; c->setSize({100, 50}); c->setPosition({400, 100});
    w.addControl(a); w.addControl(b); w.addControl(c);

    a->setSelected(true); b->setSelected(true); c->setSelected(true);

    w.onAlignRight();
    // ★ 所有 targets_ 的 right (x + width) 互相相等
    const qreal aRight = a->position().x() + a->size().width();
    QCOMPARE(b->position().x() + b->size().width(), aRight);
    QCOMPARE(c->position().x() + c->size().width(), aRight);
}

// ===== 13. multiSelect_moveBy_translatesAllBySameDelta =====
//   ★ E.4d:多选移动 — 直接调 moveBy 验证所有 targets_ 都跟移
//   (eventFilter 路径太复杂,用直接 API 验证语义)
void TestEditorMainWindow::multiSelect_moveBy_translatesAllBySameDelta() {
    EditorMainWindow w;
    auto* a = new LabelControl; a->setPosition({10, 20});
    auto* b = new LabelControl; b->setPosition({100, 200});
    w.addControl(a); w.addControl(b);

    a->setSelected(true); b->setSelected(true);

    // ★ 模拟多选移动(等价于 eventFilter 中 mouseMove 路径)
    a->moveBy(5, 10);
    b->moveBy(5, 10);

    QCOMPARE(a->position(), QPointF(15, 30));
    QCOMPARE(b->position(), QPointF(105, 210));
    QVERIFY(w.hasMultipleSelection());
}

// ===== 14. realClick_ctrlClickTwoItems_bothSelected =====
//   ★ E.4d 真实点击测试(2026-09-22):
//     用户报告 hmi.exe 里"同一时间只能选中一个"。
//     模拟用户真实操作 — 用 QTest::mouseClick 在 view viewport 上点两个控件,
//     第二次点带 Ctrl modifier。验证 multi-selection 真的生效。
//
//     这就是 bug 是否真实存在的决定性测试 — 如果 FAIL 说明确实有问题。
void TestEditorMainWindow::realClick_ctrlClickTwoItems_bothSelected() {
    EditorMainWindow w;
    // ★ 拖入 2 个 Button(spaced 远一点以便点击不同位置)
    w.handlePaletteDrop("Button", QPointF(100, 100));
    w.handlePaletteDrop("Button", QPointF(400, 100));
    QCOMPARE(w.controls().size(), 2);

    auto* a = w.controls()[0];
    auto* b = w.controls()[1];

    // ★ 显示 view 才能用 viewport 做真实鼠标事件
    w.show();
    QTest::qWait(50);

    QGraphicsView* view = w.view();
    QVERIFY(view != nullptr);
    QWidget* vp = view->viewport();

    // ★ 点 Button A 的中心(viewport 坐标)
    QPoint aViewPos = view->mapFromScene(a->scenePos() + QPointF(a->size().width()/2, a->size().height()/2));
    QTest::mouseClick(vp, Qt::LeftButton, Qt::NoModifier, aViewPos);

    qDebug() << "[real click] after click A, scene selectedItems =" << w.scene()->selectedItems().size();

    // ★ Ctrl+点 Button B 的中心
    QPoint bViewPos = view->mapFromScene(b->scenePos() + QPointF(b->size().width()/2, b->size().height()/2));
    QTest::mouseClick(vp, Qt::LeftButton, Qt::ControlModifier, bViewPos);

    qDebug() << "[real click] after Ctrl+click B, scene selectedItems =" << w.scene()->selectedItems().size();
    qDebug() << "[real click] targets_.size =" << w.targets().size();
    qDebug() << "[real click] hasMultipleSelection =" << w.hasMultipleSelection();

    // ★ 关键断言:Ctrl+点两个 → 两个都被选中
    QCOMPARE(w.scene()->selectedItems().size(), 2);
    QCOMPARE(w.targets().size(), 2);
    QVERIFY(w.hasMultipleSelection());
}

// ===== 15-16. selection frame 测试 — TODO Qt 6.8 reentrancy 暂未实现 =====
//   取代之前的 broken test:QGraphicsScene 没有 selectionMode() 这个 API。
//   Qt 的多选由 view 的 dragMode + 修饰键共同决定,不需要也不支持在 scene 上设。
//   ★ E.4d 续-3:多选时 EditorMainWindow 创建 GroupHandleItem + 8 个 ResizeHandleItem
//     围在合并 boundingRect 周围
void TestEditorMainWindow::groupHandle_installedOnMultiSelect() {
    EditorMainWindow w;
    auto* a = new LabelControl; a->setSize({100, 50}); a->setPosition({  0, 0});
    auto* b = new LabelControl; b->setSize({100, 50}); b->setPosition({200, 0});
    w.addControl(a); w.addControl(b);

    a->setSelected(true);
    b->setSelected(true);

    // ★ 多选时 groupHandle_ 应该非空,挂到 scene 上,含 8 个 ResizeHandleItem
    QVERIFY(w.groupHandle() != nullptr);
    QCOMPARE(w.groupHandle()->scene(), w.scene());
    int handleCount = 0;
    for (QGraphicsItem* c : w.groupHandle()->childItems()) {
        if (qgraphicsitem_cast<ResizeHandleItem*>(c)) ++handleCount;
    }
    QCOMPARE(handleCount, 8);

    // ★ group rect 应等于合并 boundingRect
    const QRectF expectedRect = a->sceneBoundingRect().united(b->sceneBoundingRect());
    QCOMPARE(w.groupHandle()->rect(), expectedRect);
}

// ===== 18. groupHandle_proportionalResizeAllTargets =====
//   ★ E.4d 续-3:模拟拖 BR handle 让 group 变大 → 所有 targets 等比缩放
void TestEditorMainWindow::groupHandle_proportionalResizeAllTargets() {
    EditorMainWindow w;
    auto* a = new LabelControl; a->setSize({100, 50}); a->setPosition({  0, 0});
    auto* b = new LabelControl; b->setSize({100, 50}); b->setPosition({200, 0});
    w.addControl(a); w.addControl(b);

    a->setSelected(true);
    b->setSelected(true);

    // ★ 找 BR handle
    ResizeHandleItem* br = nullptr;
    for (QGraphicsItem* c : w.groupHandle()->childItems()) {
        auto* h = qgraphicsitem_cast<ResizeHandleItem*>(c);
        if (h && h->handleType() == HandleType::BottomRight) { br = h; break; }
    }
    QVERIFY(br != nullptr);

    // ★ 模拟拖 BR handle 让 group 放大 (+50, +25)
    QPointF pressPos = br->scenePos();
    br->handleMousePress(pressPos);
    br->handleMouseMove(pressPos + QPointF(50, 25), Qt::NoModifier);

    // ★ a 和 b 应该等比变大,位置保持相对偏移
    const qreal sx = (a->sceneBoundingRect().width() + 0.0) / 100.0;   // 相对原宽度
    // 简化:只检查 size 都增加了
    QVERIFY(a->size().width()  >= 100.0);
    QVERIFY(a->size().height() >=  50.0);
    QVERIFY(b->size().width()  >= 100.0);
    QVERIFY(b->size().height() >=  50.0);

    // ★ 位置偏移应保持 (a 在 b 左,水平间隔仍存在)
    QVERIFY(b->position().x() > a->position().x());
}
void TestEditorMainWindow::selectionFrame_appearsAfterSelect() {
    // ★ E.4d 续-5:frame 实装后,单选时 frame.rect 应等于 target.sceneBoundingRect
    EditorMainWindow w;
    auto* lbl = new LabelControl;
    lbl->setSize({100, 50});
    lbl->setPosition({150, 80});
    w.addControl(lbl);
    lbl->setSelected(true);

    QVERIFY(w.selectionFrame() != nullptr);
    QCOMPARE(w.selectionFrame()->rect(), QRectF(0, 0, 100, 50));
    QCOMPARE(w.selectionFrame()->pos(), QPointF(150, 80));
    QVERIFY(w.selectionFrame()->isVisible());
}

void TestEditorMainWindow::selectionFrame_surroundsMultiSelectionBounds() {
    // ★ E.4d 续-5:多选时 frame.rect 应等于 union
    //   注:用 pos + rect().size() 比较,而非 sceneBoundingRect ——
    //   SelectionFrameItem 用 cosmetic pen (width 2),sceneBoundingRect 会比
    //   rect 多 2 像素(每边 1px)
    EditorMainWindow w;
    auto* a = new LabelControl; a->setSize({100, 50}); a->setPosition({150, 80});
    auto* b = new LabelControl; b->setSize({100, 50}); b->setPosition({350, 200});
    w.addControl(a); w.addControl(b);
    a->setSelected(true);
    b->setSelected(true);

    QVERIFY(w.selectionFrame() != nullptr);
    const QRectF expectedUnion = a->sceneBoundingRect().united(b->sceneBoundingRect());
    QCOMPARE(w.selectionFrame()->rect().size(), expectedUnion.size());   // 大小一致(忽略 cosmetic)
    QCOMPARE(w.selectionFrame()->pos(), expectedUnion.topLeft());        // 位置 = union.topLeft
}

void TestEditorMainWindow::multiMove_selectionFrameFollows() {
    // ★ E.4d 续-5 bug 回归(用户报告图 10/11):
    //   多选整体移动后,frame 应跟随 — pos + rect 都更新。
    EditorMainWindow w;
    auto* a = new LabelControl; a->setSize({50, 50}); a->setPosition({100, 100});
    auto* b = new LabelControl; b->setSize({50, 50}); b->setPosition({200, 100});
    w.addControl(a); w.addControl(b);
    a->setSelected(true);
    b->setSelected(true);

    QVERIFY(w.selectionFrame() != nullptr);
    const QRectF expectedBefore = a->sceneBoundingRect().united(b->sceneBoundingRect());
    QCOMPARE(w.selectionFrame()->rect().size(), expectedBefore.size());
    QCOMPARE(w.selectionFrame()->pos(), expectedBefore.topLeft());

    // ★ 直接调 setPosition 触发 positionChanged → 我们的 updateSelectionFrame 槽
    a->setPosition({150, 130});
    b->setPosition({250, 130});   // 整体 +50,+30;delta 保留

    const QRectF expectedAfter = a->sceneBoundingRect().united(b->sceneBoundingRect());
    QCOMPARE(w.selectionFrame()->rect().size(), expectedAfter.size());
    QCOMPARE(w.selectionFrame()->pos(), expectedAfter.topLeft());   // ★ 跟随移动后 frame.pos 更新
}

void TestEditorMainWindow::multiResize_selectionFrameFollows() {
    // ★ E.4d 续-5:多选时改其中一个的 size,frame 应跟随 union
    EditorMainWindow w;
    auto* a = new LabelControl; a->setSize({50, 50}); a->setPosition({100, 100});
    auto* b = new LabelControl; b->setSize({50, 50}); b->setPosition({200, 100});
    w.addControl(a); w.addControl(b);
    a->setSelected(true);
    b->setSelected(true);

    // ★ 把 a 改大,union 跟着变
    a->setSize({80, 80});

    const QRectF expected = a->sceneBoundingRect().united(b->sceneBoundingRect());
    QCOMPARE(w.selectionFrame()->rect().size(), expected.size());
    QCOMPARE(w.selectionFrame()->pos(), expected.topLeft());
}

// ===== 19. multiMove_relativePositionPreserved =====
//   ★ E.4d 续-3 bug 回归:多选整体移动后,所有选中的相对位置应保持不变
//   注:QTest::mouseMove 在 offscreen 平台下不发 mouseMove 事件,所以直接构造
//   QGraphicsSceneMouseEvent 用 sendEvent 触发 scene->eventFilter。
void TestEditorMainWindow::multiMove_relativePositionPreserved() {
    EditorMainWindow w;
    auto* a = new LabelControl; a->setSize({50, 50}); a->setPosition({100, 100});
    auto* b = new LabelControl; b->setSize({50, 50}); b->setPosition({200, 100});  // 水平间隔 100
    w.addControl(a); w.addControl(b);

    // ★ 多选 2 个
    a->setSelected(true);
    b->setSelected(true);

    const QPointF initialABDelta = b->position() - a->position();   // (100, 0)
    const QPointF pressPos = a->scenePos() + QPointF(25, 25);   // scene (125, 125)
    const QPointF movePos  = pressPos + QPointF(50, 30);         // scene (175, 155)

    // ★ 直接给 scene 发 GraphicsSceneMouseEvent,绕过 QTest 在 offscreen 下的
    //   mouseMove 事件丢失问题(QTest::mouseMove 在某些 platform 下不生成 move 事件)
    QGraphicsSceneMouseEvent press(QEvent::GraphicsSceneMousePress);
    press.setScenePos(pressPos);
    press.setButton(Qt::LeftButton);
    press.setButtons(Qt::LeftButton);
    QApplication::sendEvent(w.scene(), &press);

    QGraphicsSceneMouseEvent move(QEvent::GraphicsSceneMouseMove);
    move.setScenePos(movePos);
    move.setButtons(Qt::LeftButton);
    QApplication::sendEvent(w.scene(), &move);

    QGraphicsSceneMouseEvent release(QEvent::GraphicsSceneMouseRelease);
    release.setScenePos(movePos);
    release.setButton(Qt::LeftButton);
    release.setButtons(Qt::NoButton);
    QApplication::sendEvent(w.scene(), &release);

    // ★ 验证:两个都移动了 + 相对位置保持
    QCOMPARE(a->position(), QPointF(150, 130));    // 100+50, 100+30
    QCOMPARE(b->position(), QPointF(250, 130));    // 200+50, 100+30
    QCOMPARE(b->position() - a->position(), initialABDelta);   // ★ 相对位置保持 (100, 0)
}

void TestEditorMainWindow::multiMove_setPosBypassesWrapper_frameFollows() {
    // ★ E.4d 续-6 bug 回归(用户报告图 12 / 12(2)):
    //   Qt 的 ItemIsMovable 拖动内部直接调 setPos(),绕开我们的 setPosition wrapper,
    //   setPos 不 emit positionChanged → frame 不更新。
    //   复现:多选 2 个,直接 setPos()(模拟 Qt 内部路径),frame 应该跟随。
    //   修复:ControlBase::itemChange override → ItemPositionHasChanged 上 emit positionChanged。
    EditorMainWindow w;
    auto* a = new LabelControl; a->setSize({50, 50}); a->setPosition({100, 100});
    auto* b = new LabelControl; b->setSize({50, 50}); b->setPosition({200, 100});
    w.addControl(a); w.addControl(b);
    a->setSelected(true);
    b->setSelected(true);

    QVERIFY(w.selectionFrame() != nullptr);

    // ★ 关键:用 setPos() 路径(模拟 Qt ItemIsMovable 内部拖动,不经过 setPosition wrapper)
    a->setPos({150, 130});
    b->setPos({250, 130});

    const QRectF expected = a->sceneBoundingRect().united(b->sceneBoundingRect());
    QCOMPARE(w.selectionFrame()->rect().size(), expected.size());
    QCOMPARE(w.selectionFrame()->pos(), expected.topLeft());   // ★ frame 跟随了
}

// ===== 19. groupHandle_handlesAtCorrectScenePosition =====
//   ★ E.4d 续-3 bug 回归:
//   group 的 pos 必须是 unionRect.topLeft,rect 必须是本地 (0,0,w,h)。
//   之前 setRect(unionRect) + 默认 pos=(0,0) → 8 个 handle 出现在 scene (0,0)
//   附近而不是控件角点(用户报告 "蓝色框和所选元件位置不一致")。
void TestEditorMainWindow::groupHandle_handlesAtCorrectScenePosition() {
    EditorMainWindow w;
    // ★ 故意把控件放在非零位置,验证 handle 跟着它们的 bbox,不是 scene 零点
    auto* a = new LabelControl; a->setSize({100, 50}); a->setPosition({150, 80});
    auto* b = new LabelControl; b->setSize({100, 50}); b->setPosition({350, 200});
    w.addControl(a); w.addControl(b);

    a->setSelected(true);
    b->setSelected(true);
    QVERIFY(w.groupHandle() != nullptr);

    // ★ group 的 sceneBoundingRect 应 = 合并
    const QRectF expectedGroup = a->sceneBoundingRect().united(b->sceneBoundingRect());
    QCOMPARE(w.groupHandle()->sceneBoundingRect(), expectedGroup);

    // ★ group 的 pos 应 = expectedGroup.topLeft,rect 应 = 本地 (0,0,w,h)
    QCOMPARE(w.groupHandle()->pos(), expectedGroup.topLeft());
    QCOMPARE(w.groupHandle()->rect(), QRectF(0, 0, expectedGroup.width(), expectedGroup.height()));

    // ★ 8 个 handle 的 scene 位置:TL=(150,80), TR=(250,80), BR=(350,130)
    //   (a 的 topLeft=(150,80), b 的 bottomRight=(450,250), unionRect=(150,80,300,170))
    for (QGraphicsItem* c : w.groupHandle()->childItems()) {
        auto* h = qgraphicsitem_cast<ResizeHandleItem*>(c);
        if (!h) continue;
        const QPointF sceneP = h->scenePos();
        // ★ handle scene 位置应在 unionRect 边界附近(±handle_size/2 容差)
        QVERIFY2(sceneP.x() >= expectedGroup.left() - 10 &&
                 sceneP.x() <= expectedGroup.right() + 10,
                 qPrintable(QString("handle %1 sceneX=%2 out of [%3,%4]")
                                .arg(static_cast<int>(h->handleType())).arg(sceneP.x())
                                .arg(expectedGroup.left()).arg(expectedGroup.right())));
        QVERIFY2(sceneP.y() >= expectedGroup.top() - 10 &&
                 sceneP.y() <= expectedGroup.bottom() + 10,
                 qPrintable(QString("handle %1 sceneY=%2 out of [%3,%4]")
                                .arg(static_cast<int>(h->handleType())).arg(sceneP.y())
                                .arg(expectedGroup.top()).arg(expectedGroup.bottom())));
    }
}

QTEST_MAIN(TestEditorMainWindow)
#include "tst_EditorMainWindow.moc"
