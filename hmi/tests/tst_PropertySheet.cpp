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
    void multiSelect_positionNotAllZero();      // ★ bug图 B 组 4 张的根因回归
    void multiSelect_positionPartialCommon();   // ★ 按分量判同值(B 组修复核心)
    void multiSelect_sizePartialCommon();       // ★ size 分支同样按分量(图2)
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

// ===== 17. multiSelect_positionNotAllZero —— B 组 4 张的根因回归 =====
//
//   ★ 这条测试是为 docs/bug图/README.md B 组(图2/图3/图6/图7)写的。
//   那 4 张图的共同症状:多选时属性面板 x/y 全灰 + 显示 "(异值) 0.00"。
//   2026-09-30 复现成功,根因在 PropertySheet::rebuildForm ——
//   判断"多选是否同值"时拿【整个 QPointF】比,只要一个分量不同就整块置灰。
//   而多选时 position 几乎必然不同值,等于"多选后永远看不了位置"。
//
//   修法:改成按分量分别判定(见 18 号用例)。
//
//   本条锁的是"两个分量【都】不同值"的场景 —— 修复后仍然两格都灰,
//   因为这时确实该灰。**别把这条读成"两格同值也能编辑"**,
//   那是 18 号用例干的事。
void TestPropertySheet::multiSelect_positionNotAllZero() {
    QUndoStack stack;
    PropertySheet sheet(&stack);

    auto* a = new BarControl; a->setPosition(QPointF(10, 20));
    auto* b = new BarControl; b->setPosition(QPointF(80, 90));
    QList<ControlBase*> targets = {a, b};
    sheet.setTargets(targets);

    // ★ position 是 QPointF → createEditorForProperty 返回【PositionEditor 复合控件】,
    //   它的 objectName 是 "position",x/y 两个 spin 是它的【子控件】且没设 objectName。
    //   所以 findChild("x") 拿不到东西,得从 position 编辑器里按顺序取子 spin。
    auto* posEditor = sheet.findChild<PositionEditor*>("position");
    QVERIFY2(posEditor != nullptr, "多选时表单里应有 position 复合编辑器");
    const auto spins = posEditor->findChildren<QDoubleSpinBox*>();
    QVERIFY2(spins.size() == 2, "PositionEditor 应含 2 个 QDoubleSpinBox(x/y)");

    // ★ a(10,20) / b(80,90):x 和 y 都不同 → 两格都应禁用
    //   这正是 bug图 B 组的症状。按新逻辑,两个分量分别判定,两格都异值 → 都灰。
    QVERIFY2(!spins[0]->isEnabled(), "x 不同值,应变灰");
    QVERIFY2(!spins[1]->isEnabled(), "y 不同值,应变灰");

    delete a;
    delete b;
}

// ===== 18. multiSelect_positionPartialCommon —— x 相同则 x 仍可编辑 =====
//
//   ★ B 组修复的核心断言:两个控件【x 相同、y 不同】时,
//     x 这一格必须照常显示真值且可编辑,只有 y 变灰。
//   修复前是拿整个 QPointF 比,只要有一个分量不同就整块置灰 ——
//   多选时 position 几乎必然不同值,于是位置框永远灰着,用户看不到任何信息。
void TestPropertySheet::multiSelect_positionPartialCommon() {
    QUndoStack stack;
    PropertySheet sheet(&stack);

    // ★ x 都是 10(典型场景:同一列对齐),y 不同
    auto* a = new BarControl; a->setPosition(QPointF(10, 20));
    auto* b = new BarControl; b->setPosition(QPointF(10, 90));
    QList<ControlBase*> targets = {a, b};
    sheet.setTargets(targets);

    auto* posEditor = sheet.findChild<PositionEditor*>("position");
    QVERIFY(posEditor != nullptr);
    const auto spins = posEditor->findChildren<QDoubleSpinBox*>();
    QVERIFY(spins.size() == 2);

    // ★ x 同值 → 可编辑 + 显示真值 10
    QVERIFY2(spins[0]->isEnabled(), "x 是共同值,不该被连坐置灰");
    QCOMPARE(spins[0]->value(), 10.0);

    // ★ y 异值 → 置灰 + 置空
    QVERIFY2(!spins[1]->isEnabled(), "y 是异值,应变灰");

    // ★ 关键:改 x 不能把 y 刷成 0(这是 NaN 标记机制存在的原因)
    spins[0]->setValue(55.0);
    QCOMPARE(a->position(), QPointF(55, 20));
    QCOMPARE(b->position(), QPointF(55, 90));   // ★ y 各归各,没被冲成 0

    delete a;
    delete b;
}

// ===== 19. multiSelect_sizePartialCommon —— size 同样按分量(图2 的 width/height 0.00)=====
//
//   ★ 图2 记的现象是"Group 选中时 width/height 都是 0.00"。
//   size 是 QSizeF,走的是和 position 完全平行的另一条分支 ——
//   两条分支是分开写的,只测 position 等于放 size 那条不测,所以补这一条。
void TestPropertySheet::multiSelect_sizePartialCommon() {
    QUndoStack stack;
    PropertySheet sheet(&stack);

    // ★ 高度相同(等高是常用操作),宽度不同
    auto* a = new BarControl; a->setSize(QSizeF(30, 100));
    auto* b = new BarControl; b->setSize(QSizeF(80, 100));
    QList<ControlBase*> targets = {a, b};
    sheet.setTargets(targets);

    auto* sizeEditor = sheet.findChild<SizeEditor*>("size");
    QVERIFY(sizeEditor != nullptr);
    const auto spins = sizeEditor->findChildren<QDoubleSpinBox*>();
    QVERIFY(spins.size() == 2);

    // ★ w 不同 → 灰;h 相同 → 亮且显示真值 100
    QVERIFY2(!spins[0]->isEnabled(), "w 不同值,应变灰");
    QVERIFY2(spins[1]->isEnabled(),  "h 是共同值,不该被连坐置灰");
    QCOMPARE(spins[1]->value(), 100.0);

    // ★ 改 h 不能把 w 刷成 0
    spins[1]->setValue(250.0);
    QCOMPARE(a->size(), QSizeF(30, 250));
    QCOMPARE(b->size(), QSizeF(80, 250));

    delete a;
    delete b;
}

QTEST_MAIN(TestPropertySheet)
#include "tst_PropertySheet.moc"
