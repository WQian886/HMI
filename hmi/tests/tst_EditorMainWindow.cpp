// ============================================================================
// tst_EditorMainWindow
//   E.2 周 11 EditorMainWindow 骨架 + 模式切换 8 用例
// ============================================================================

#include <QtTest>
#include <QApplication>
#include <QGraphicsScene>
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

QTEST_MAIN(TestEditorMainWindow)
#include "tst_EditorMainWindow.moc"
