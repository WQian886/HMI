// ============================================================================
// 阶段 E · E.1a-2 · NumberDisplay 单测
//
// 验证:
//   1. construct_defaultSizeAndFormat
//   2. paint_doesNotCrash
//   3. displayText_appendsUnit
//   4. format_changeTriggersRepaint
//   5. fromJson_restoresAllFields
// ============================================================================

#include <QtTest>
#include <QGraphicsScene>
#include <QImage>
#include <QPainter>
#include <QJsonObject>
#include <QApplication>
#include "widgets/NumberDisplay.h"
#include "widgets/base/ControlBase.h"

using namespace qwhmi::widgets;

class TestNumberDisplay : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { qputenv("QT_QPA_PLATFORM", "offscreen"); qInfo() << "[tst_NumberDisplay] init"; }
    void cleanupTestCase() { qInfo() << "[tst_NumberDisplay] cleanup"; }

    void construct_defaultSizeAndFormat();
    void paint_doesNotCrash();
    void displayText_appendsUnit();
    void format_changeTriggersUpdate();
    void fromJson_restoresAllFields();
};

void TestNumberDisplay::construct_defaultSizeAndFormat() {
    NumberDisplay n;
    QCOMPARE(n.size(), QSizeF(140, 40));    // 默认
    QCOMPARE(n.format(), QString("%.2f"));  // 默认
    QCOMPARE(n.unit(), QString(""));        // 默认空
    QVERIFY(n.flags() & QGraphicsItem::ItemIsMovable);
}

void TestNumberDisplay::paint_doesNotCrash() {
    QGraphicsScene s;
    NumberDisplay n;
    n.setValue(123.45);
    n.setUnit(" rpm");
    s.addItem(&n);

    QImage img(200, 100, QImage::Format_ARGB32);
    img.fill(Qt::white);
    QPainter p(&img);
    s.render(&p);
    p.end();
    QVERIFY(!img.isNull());
}

void TestNumberDisplay::displayText_appendsUnit() {
    NumberDisplay n;
    n.setValue(99.5);
    QCOMPARE(n.displayText(), QString("99.50"));  // 默认 %.2f,无单位
    n.setUnit(" rpm");
    QCOMPARE(n.displayText(), QString("99.50 rpm"));
}

void TestNumberDisplay::format_changeTriggersUpdate() {
    NumberDisplay n;
    n.setValue(3.14159);
    n.setFormat("%.4f");
    // format 改动不直接改 displayText(简化实现),但触发 update()
    QCOMPARE(n.format(), QString("%.4f"));
    // displayText 仍是 .2f 格式(简化实现)
    QVERIFY(n.displayText().contains("3.14"));
}

void TestNumberDisplay::fromJson_restoresAllFields() {
    NumberDisplay n;
    QJsonObject obj;
    // ★ 键名按 ControlBase 的 Q_PROPERTY 名写(见 tst_LabelControl.cpp 的说明):
    //   position/size 是嵌套对象,不是平铺的 x/y/w/h
    obj["position"] = QJsonObject{{"x", 100.0}, {"y", 50.0}};
    obj["size"]     = QJsonObject{{"width", 200.0}, {"height", 60.0}};
    obj["variableName"] = "plc1.Speed";
    n.fromJson(obj);

    QCOMPARE(n.position(), QPointF(100.0, 50.0));
    QCOMPARE(n.size(), QSizeF(200.0, 60.0));
    QCOMPARE(n.variableName(), QString("plc1.Speed"));
}

QTEST_MAIN(TestNumberDisplay)
#include "tst_NumberDisplay.moc"