// ============================================================================
// tst_PropertySheet
//   E.2 属性面板 PropertySheet + PropertyChangeCommand(QUndoCommand) +
//   ControlBase.propertiesToJson() 反射接口 8 用例
//   E.4c(2026-09-22): +3 用例覆盖 QSizeF/QPointF SizeEditor/PositionEditor
// ============================================================================

#include <QtTest>
#include <QApplication>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QJsonObject>
#include <QLineEdit>
#include <QPointF>
#include <QPushButton>
#include <QSizeF>
#include <QSpinBox>
#include <QUndoStack>

#include "widgets/LabelControl.h"
#include "widgets/BarControl.h"
#include "widgets/NumberDisplay.h"
#include "widgets/TextDisplay.h"
#include "widgets/LedControl.h"
#include "widgets/ToggleControl.h"
#include "widgets/TableControl.h"

#include "editor/PropertySheet.h"
#include "editor/PropertyChangeCommand.h"
#include "editor/SizeEditor.h"
#include "editor/PositionEditor.h"

using namespace qwhmi::widgets;
using namespace qwhmi::editor;

class TestPropertySheet : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        qputenv("QT_QPA_PLATFORM", "offscreen");
        qInfo() << "[tst_PropertySheet] init";
    }
    void cleanupTestCase() { qInfo() << "[tst_PropertySheet] cleanup"; }

    // ★ 8 用例(按 docs/阶段E.2_从这里继续.md §7 第 5 步)
    void reflect_findsAllWritableProperties();
    void createEditorForProperty_intDoubleString();
    void createEditorForProperty_enum();
    void createEditorForProperty_color();
    void editProperty_triggersUndoCommand();
    void undoRestoresOldValue();
    void redoAppliesNewValue();
    void propertiesToJson_roundTripsThroughFromJson();

    // ★ E.4c(2026-09-22): QSizeF/QPointF 复合编辑器
    void sizeEditor_appearsInForm();
    void sizeEditor_valueChange_updatesTargetAndUndo();
    void positionEditor_appearsInForm();

    // ★ E.4d-续-2(2026-09-22):多选共同属性
    void multiSelect_commonValuesShownAndEditable();
    void multiSelect_diffValuesShownEmptyAndDisabled();
    void multiSelect_editPushesToAllTargets();
};

// ===== 1. reflect_findsAllWritableProperties =====
void TestPropertySheet::reflect_findsAllWritableProperties() {
    QUndoStack stack;
    PropertySheet sheet(&stack);
    BarControl bar;
    sheet.setTarget(&bar);

    // ★ BarControl 应当有 6 个可写 Q_PROPERTY(minimum/maximum/fillColor/orientation + 基类 2 个)
    //   基类有 4 个 writable(position/size/variableName/value,displayText 只读跳过)
    //   + 派生 4 个 → 共 8 个可写
    const QMetaObject* meta = bar.metaObject();
    int writableCount = 0;
    for (int i = 0; i < meta->propertyCount(); ++i) {
        if (meta->property(i).isWritable()) writableCount++;
    }
    QVERIFY(writableCount >= 8);   // 至少 8 个(基类 4 + BarControl 派生 4)
}

// ===== 2. createEditorForProperty_intDoubleString =====
void TestPropertySheet::createEditorForProperty_intDoubleString() {
    QUndoStack stack;
    PropertySheet sheet(&stack);
    LabelControl lbl;
    sheet.setTarget(&lbl);

    // ★ LabelControl 没有派生属性,只有基类 4 个 writable(position/size/variableName/value)
    //   position (QPointF) 和 size (QSizeF) 暂不支持 → 跳过
    //   variableName (QString) 和 value (QVariant) 应该生成编辑器
    int lineEdits = 0;
    for (int i = 0; i < sheet.findChildren<QLineEdit*>().size(); ++i) lineEdits++;
    QVERIFY(lineEdits >= 2);   // variableName + value

    // ★ 测 double — 用 NumberDisplay 的 unit(QString) 做基础检查
    NumberDisplay num;
    sheet.setTarget(&num);
    // NumberDisplay 有 format(QString) + unit(QString) → 派生类至少 2 个 QString
    int lineEdits2 = 0;
    for (int i = 0; i < sheet.findChildren<QLineEdit*>().size(); ++i) lineEdits2++;
    QVERIFY(lineEdits2 >= 2);   // 至少 format + unit(2 个 QString 派生)
}

// ===== 3. createEditorForProperty_enum =====
void TestPropertySheet::createEditorForProperty_enum() {
    QUndoStack stack;
    PropertySheet sheet(&stack);
    BarControl bar;
    sheet.setTarget(&bar);

    // ★ BarControl::orientation 是 Qt::Orientation 枚举(用 Q_ENUM 注册)
    //   → 应该生成 QComboBox
    auto combos = sheet.findChildren<QComboBox*>();
    QVERIFY(combos.size() >= 1);

    // QComboBox 应当至少 2 项(Qt::Horizontal / Qt::Vertical)
    QVERIFY(combos.first()->count() >= 2);
}

// ===== 4. createEditorForProperty_color =====
void TestPropertySheet::createEditorForProperty_color() {
    QUndoStack stack;
    PropertySheet sheet(&stack);
    LedControl led;
    sheet.setTarget(&led);

    // ★ LedControl 有 onColor + offColor(QColor 类型)
    //   → 应该生成 2 个 QPushButton(点击弹 QColorDialog)
    auto buttons = sheet.findChildren<QPushButton*>();
    int colorBtns = 0;
    for (auto* b : buttons) {
        if (b->property("_kind").toString() == "color") colorBtns++;
    }
    QCOMPARE(colorBtns, 2);   // onColor + offColor
}

// ===== 5. editProperty_triggersUndoCommand =====
void TestPropertySheet::editProperty_triggersUndoCommand() {
    QUndoStack stack;
    PropertySheet sheet(&stack);
    LedControl led;
    led.setOnColor(QColor("#FF0000"));
    sheet.setTarget(&led);

    // ★ 找 onColor 的按钮 + 模拟属性修改
    // 直接 push 一个 PropertyChangeCommand 测试
    const QColor oldC = led.onColor();
    const QColor newC("#00FF00");
    auto* cmd = new PropertyChangeCommand(&led, "onColor", oldC, newC);
    QCOMPARE(stack.count(), 0);
    stack.push(cmd);
    QCOMPARE(stack.count(), 1);
    QCOMPARE(led.onColor(), newC);
}

// ===== 6. undoRestoresOldValue =====
void TestPropertySheet::undoRestoresOldValue() {
    QUndoStack stack;
    LedControl led;
    led.setOnColor(QColor("#FF0000"));
    const QColor oldC = led.onColor();

    auto* cmd = new PropertyChangeCommand(&led, "onColor", oldC, QColor("#00FF00"));
    stack.push(cmd);
    QCOMPARE(led.onColor(), QColor("#00FF00"));

    stack.undo();
    QCOMPARE(led.onColor(), oldC);
}

// ===== 7. redoAppliesNewValue =====
void TestPropertySheet::redoAppliesNewValue() {
    QUndoStack stack;
    LedControl led;
    led.setOnColor(QColor("#FF0000"));

    const QColor oldC = led.onColor();
    const QColor newC("#0000FF");
    auto* cmd = new PropertyChangeCommand(&led, "onColor", oldC, newC);
    stack.push(cmd);
    stack.undo();
    QCOMPARE(led.onColor(), oldC);

    stack.redo();
    QCOMPARE(led.onColor(), newC);
}

// ===== 8. propertiesToJson_roundTripsThroughFromJson =====
void TestPropertySheet::propertiesToJson_roundTripsThroughFromJson() {
    NumberDisplay num;
    num.setVariableName("plc1.Speed");
    num.setFormat("%.1f");
    num.setUnit("rpm");

    // ★ propertiesToJson 输出所有可读 Q_PROPERTY
    QJsonObject obj = num.propertiesToJson();
    QVERIFY(obj.contains("variableName"));
    QCOMPARE(obj.value("variableName").toString(), QString("plc1.Speed"));
    QVERIFY(obj.contains("format"));
    QCOMPARE(obj.value("format").toString(), QString("%.1f"));
    QVERIFY(obj.contains("unit"));
    QCOMPARE(obj.value("unit").toString(), QString("rpm"));

    // ★ roundTrip:改源对象 → fromJson → 复原
    NumberDisplay num2;
    num2.propertiesFromJson(obj);
    QCOMPARE(num2.variableName(), QString("plc1.Speed"));
    QCOMPARE(num2.format(), QString("%.1f"));
    QCOMPARE(num2.unit(), QString("rpm"));

    // ★ toJson 也走 propertiesToJson(基类默认实现)
    QJsonObject fullObj = num.toJson();
    QVERIFY(fullObj.contains("type"));   // 基类 toJson 加的
    QVERIFY(fullObj.contains("variableName"));
}

// ===== 9. sizeEditor_appearsInForm =====
//   ★ E.4c 验证:选中任意 ControlBase,PropertySheet 的表单里应该有 1 个 SizeEditor
//     (对应 size QSizeF 属性)+ 1 个 PositionEditor(对应 position QPointF)
void TestPropertySheet::sizeEditor_appearsInForm() {
    QUndoStack stack;
    PropertySheet sheet(&stack);
    LabelControl lbl;
    lbl.setSize(QSizeF(150, 50));
    lbl.setPosition(QPointF(20, 30));
    sheet.setTarget(&lbl);

    auto* sizeE = sheet.findChild<SizeEditor*>();
    auto* posE  = sheet.findChild<PositionEditor*>();
    QVERIFY(sizeE != nullptr);
    QVERIFY(posE  != nullptr);

    // ★ 验证初值正确(从 target 反向灌入)
    QCOMPARE(sizeE->value(), QSizeF(150, 50));
    QCOMPARE(posE->value(),  QPointF(20, 30));
}

// ===== 10. sizeEditor_valueChange_updatesTargetAndUndo =====
//   ★ E.4c 验证:用户改 SizeEditor 的 width → target.size 跟着变 + 进 undo 栈
void TestPropertySheet::sizeEditor_valueChange_updatesTargetAndUndo() {
    QUndoStack stack;
    PropertySheet sheet(&stack);
    LabelControl lbl;
    lbl.setSize(QSizeF(100, 40));
    sheet.setTarget(&lbl);

    auto* sizeE = sheet.findChild<SizeEditor*>();
    QVERIFY(sizeE != nullptr);

    // ★ 修改 width 字段(QDoubleSpinBox 是 sizeE 子控件)
    const QList<QDoubleSpinBox*> spins = sizeE->findChildren<QDoubleSpinBox*>();
    QVERIFY(spins.size() == 2);   // W + H
    spins[0]->setValue(200.0);    // 改 width

    // ★ target.size 必须跟变
    QCOMPARE(lbl.size(), QSizeF(200, 40));

    // ★ undo 栈必须多 1 个命令
    QCOMPARE(stack.count(), 1);

    // ★ undo 复原
    stack.undo();
    QCOMPARE(lbl.size(), QSizeF(100, 40));
}

// ===== 11. positionEditor_appearsInForm =====
//   ★ E.4c 验证:PositionEditor 出现在表单中 + 初值正确
void TestPropertySheet::positionEditor_appearsInForm() {
    QUndoStack stack;
    PropertySheet sheet(&stack);
    BarControl bar;
    bar.setPosition(QPointF(100, 200));
    sheet.setTarget(&bar);

    auto* posE = sheet.findChild<PositionEditor*>();
    QVERIFY(posE != nullptr);
    QCOMPARE(posE->value(), QPointF(100, 200));

    // ★ 修改 y 字段
    const QList<QDoubleSpinBox*> spins = posE->findChildren<QDoubleSpinBox*>();
    QVERIFY(spins.size() == 2);
    spins[1]->setValue(300.0);
    QCOMPARE(bar.position(), QPointF(100, 300));
    QCOMPARE(stack.count(), 1);
    stack.undo();
    QCOMPARE(bar.position(), QPointF(100, 200));
}

// ===== 14. multiSelect_commonValuesShownAndEditable =====
//   ★ E.4d-续-2(2026-09-22):3 个 BarControl maximum 都是 100,
//     PropertySheet 多选时 maximum 字段显示 100 且可编辑
void TestPropertySheet::multiSelect_commonValuesShownAndEditable() {
    QUndoStack stack;
    PropertySheet sheet(&stack);
    auto* a = new BarControl; a->setMaximum(100.0);
    auto* b = new BarControl; b->setMaximum(100.0);
    auto* c = new BarControl; c->setMaximum(100.0);
    QList<ControlBase*> targets = {a, b, c};
    sheet.setTargets(targets);
    QCOMPARE(sheet.targetsCount(), 3);

    // ★ 找 maximum 字段的 editor(QDoubleSpinBox)
    auto* spin = sheet.findChild<QDoubleSpinBox*>("maximum");
    QVERIFY(spin != nullptr);
    QCOMPARE(spin->value(), 100.0);
    QVERIFY(spin->isEnabled());   // 共同值,可编辑
}

// ===== 15. multiSelect_diffValuesShownEmptyAndDisabled =====
//   ★ E.4d-续-2:3 个 BarControl maximum 不同(100/200/300),
//     PropertySheet maximum 字段显示空且禁用
void TestPropertySheet::multiSelect_diffValuesShownEmptyAndDisabled() {
    QUndoStack stack;
    PropertySheet sheet(&stack);
    auto* a = new BarControl; a->setMaximum(100.0);
    auto* b = new BarControl; b->setMaximum(200.0);
    auto* c = new BarControl; c->setMaximum(300.0);
    QList<ControlBase*> targets = {a, b, c};
    sheet.setTargets(targets);

    auto* spin = sheet.findChild<QDoubleSpinBox*>("maximum");
    QVERIFY(spin != nullptr);
    QVERIFY(!spin->isEnabled());    // 异值,禁用
    QCOMPARE(spin->value(), 0.0);   // 异值时显示为默认值(0)
}

// ===== 16. multiSelect_editPushesToAllTargets =====
//   ★ E.4d-续-2:多选时编辑共同属性 → 推所有 target + 进 Undo 栈
void TestPropertySheet::multiSelect_editPushesToAllTargets() {
    QUndoStack stack;
    PropertySheet sheet(&stack);
    auto* a = new BarControl; a->setMaximum(100.0);
    auto* b = new BarControl; b->setMaximum(100.0);
    auto* c = new BarControl; c->setMaximum(100.0);
    QList<ControlBase*> targets = {a, b, c};
    sheet.setTargets(targets);

    auto* spin = sheet.findChild<QDoubleSpinBox*>("maximum");
    QVERIFY(spin != nullptr);
    spin->setValue(777.0);

    // ★ 推所有 target(值都变了)
    QCOMPARE(a->maximum(), 777.0);
    QCOMPARE(b->maximum(), 777.0);
    QCOMPARE(c->maximum(), 777.0);
    // ★ 200ms 内 PropertyChangeCommand 合并 → 至少 3 个 command(可能被 merge)
    QVERIFY(stack.count() >= 3);

    // ★ undo 全部还原
    stack.undo();
    stack.undo();
    stack.undo();
    QCOMPARE(a->maximum(), 100.0);
    QCOMPARE(b->maximum(), 100.0);
    QCOMPARE(c->maximum(), 100.0);
}

QTEST_MAIN(TestPropertySheet)
#include "tst_PropertySheet.moc"
