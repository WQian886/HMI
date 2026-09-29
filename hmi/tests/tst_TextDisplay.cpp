// ============================================================================
// 阶段 E · E.1a-2 · TextDisplay 单测
//
// 验证:
//   1. construct_defaultSize
//   2. paint_doesNotCrash
//   3. displayText_prefixValueSuffix
//   4. prefixSuffix_changesUpdateDisplay
//   5. fromJson_restoresAllFields
// ============================================================================

#include <QtTest>
#include <QGraphicsScene>
#include <QImage>
#include <QPainter>
#include <QJsonObject>
#include <QApplication>
#include "widgets/TextDisplay.h"
#include "widgets/base/ControlBase.h"

using namespace qwhmi::widgets;

class TestTextDisplay : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { qputenv("QT_QPA_PLATFORM", "offscreen"); qInfo() << "[tst_TextDisplay] init"; }
    void cleanupTestCase() { qInfo() << "[tst_TextDisplay] cleanup"; }

    void construct_defaultSize();
    void paint_doesNotCrash();
    void displayText_prefixValueSuffix();
    void prefixSuffix_changesUpdateDisplay();
    void fromJson_restoresAllFields();
};

void TestTextDisplay::construct_defaultSize() {
    TextDisplay t;
    QCOMPARE(t.size(), QSizeF(160, 32));
    QCOMPARE(t.prefix(), QString(""));
    QCOMPARE(t.suffix(), QString(""));
}

void TestTextDisplay::paint_doesNotCrash() {
    QGraphicsScene s;
    TextDisplay* t = new TextDisplay;
    t->setPrefix("速度:");
    t->setValue(123.5);
    t->setSuffix(" rpm");
    s.addItem(t);

    QImage img(220, 80, QImage::Format_ARGB32);
    img.fill(Qt::white);
    QPainter p(&img);
    s.render(&p);
    p.end();
    QVERIFY(!img.isNull());

    delete t;
}

void TestTextDisplay::displayText_prefixValueSuffix() {
    TextDisplay t;
    QCOMPARE(t.displayText(), QString(""));   // 无 prefix/value/suffix

    t.setPrefix("速度:");
    QCOMPARE(t.displayText(), QString("速度:"));

    t.setValue(123.5);
    QCOMPARE(t.displayText(), QString("速度:123.5"));

    t.setSuffix(" rpm");
    QCOMPARE(t.displayText(), QString("速度:123.5 rpm"));
}

void TestTextDisplay::prefixSuffix_changesUpdateDisplay() {
    TextDisplay t;
    QSignalSpy prefixSpy(&t, &TextDisplay::prefixChanged);
    QSignalSpy suffixSpy(&t, &TextDisplay::suffixChanged);

    t.setPrefix("P:");
    QCOMPARE(prefixSpy.count(), 1);
    t.setSuffix("S");
    QCOMPARE(suffixSpy.count(), 1);
    // 重复设不触发
    t.setPrefix("P:");
    QCOMPARE(prefixSpy.count(), 1);
}

void TestTextDisplay::fromJson_restoresAllFields() {
    TextDisplay t;
    QJsonObject obj;
    // ★ 键名按 ControlBase 的 Q_PROPERTY 名写(见 tst_LabelControl.cpp 的说明):
    //   position/size 是嵌套对象,不是平铺的 x/y/w/h
    obj["position"] = QJsonObject{{"x", 50.0}, {"y", 60.0}};
    obj["size"]     = QJsonObject{{"width", 220.0}, {"height", 40.0}};
    obj["variableName"] = "plc1.Status";
    t.fromJson(obj);

    QCOMPARE(t.position(), QPointF(50.0, 60.0));
    QCOMPARE(t.size(), QSizeF(220.0, 40.0));
    QCOMPARE(t.variableName(), QString("plc1.Status"));
}

QTEST_MAIN(TestTextDisplay)
#include "tst_TextDisplay.moc"