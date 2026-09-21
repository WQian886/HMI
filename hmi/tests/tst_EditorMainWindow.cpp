// ============================================================================
// tst_EditorMainWindow
//   E.2 周 11 EditorMainWindow 骨架 + 模式切换 8 用例
// ============================================================================

#include <QtTest>
#include <QApplication>
#include <QGraphicsScene>
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

QTEST_MAIN(TestEditorMainWindow)
#include "tst_EditorMainWindow.moc"
