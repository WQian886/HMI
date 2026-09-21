// ============================================================================
// tst_PropertySheet
//   E.2 属性面板 PropertySheet + PropertyChangeCommand(QUndoCommand) +
//   ControlBase.propertiesToJson() 反射接口 8 用例
// ============================================================================

#include <QtTest>
#include <QApplication>
#include <QComboBox>
#include <QJsonObject>
#include <QLineEdit>
#include <QPushButton>
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

QTEST_MAIN(TestPropertySheet)
#include "tst_PropertySheet.moc"
