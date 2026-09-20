// ============================================================================
// 阶段 E · E.1a-3 · LedControl 单测
// ============================================================================

#include <QtTest>
#include <QGraphicsScene>
#include <QImage>
#include <QPainter>
#include <QJsonObject>
#include <QApplication>
#include "widgets/LedControl.h"
#include "widgets/base/ControlBase.h"

using namespace qwhmi::widgets;

class TestLedControl : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { qputenv("QT_QPA_PLATFORM", "offscreen"); qInfo() << "[tst_LedControl] init"; }
    void cleanupTestCase() { qInfo() << "[tst_LedControl] cleanup"; }

    void construct_defaultColors();
    void paint_doesNotCrash();
    void isLit_followsValue();
    void colorChanges_litAndUnlit();
    void fromJson_restoresAllFields();
};

void TestLedControl::construct_defaultColors() {
    LedControl l;
    QCOMPARE(l.onColor(), QColor(40, 200, 40));     // 默认绿
    QCOMPARE(l.offColor(), QColor(80, 80, 80));    // 默认灰
    QVERIFY(!l.isLit());   // 默认 value=0 不亮
}

void TestLedControl::paint_doesNotCrash() {
    QGraphicsScene s;
    LedControl l;
    s.addItem(&l);
    l.setValue(1);  // 亮
    s.update();

    QImage img(60, 60, QImage::Format_ARGB32);
    img.fill(Qt::white);
    QPainter p(&img);
    s.render(&p);
    p.end();
    QVERIFY(!img.isNull());
}

void TestLedControl::isLit_followsValue() {
    LedControl l;
    QVERIFY(!l.isLit());        // 默认 0

    l.setValue(1.0);
    QVERIFY(l.isLit());         // > 0 亮

    l.setValue(0.5);
    QVERIFY(l.isLit());         // > 0 亮

    l.setValue(0.0);
    QVERIFY(!l.isLit());        // = 0 不亮
}

void TestLedControl::colorChanges_litAndUnlit() {
    LedControl l;
    QSignalSpy onSpy(&l, &LedControl::onColorChanged);
    QSignalSpy offSpy(&l, &LedControl::offColorChanged);

    l.setOnColor(QColor(255, 0, 0));        // 改成红色亮
    QCOMPARE(onSpy.count(), 1);
    QCOMPARE(l.onColor(), QColor(255, 0, 0));

    l.setOffColor(QColor(40, 40, 40));       // 改成深灰灭
    QCOMPARE(offSpy.count(), 1);
    QCOMPARE(l.offColor(), QColor(40, 40, 40));
}

void TestLedControl::fromJson_restoresAllFields() {
    LedControl l;
    QJsonObject obj;
    obj["x"] = 50.0; obj["y"] = 50.0;
    obj["w"] = 32.0; obj["h"] = 32.0;
    obj["variable"] = "plc1.Run";
    l.fromJson(obj);
    QCOMPARE(l.position(), QPointF(50.0, 50.0));
    QCOMPARE(l.size(), QSizeF(32.0, 32.0));
    QCOMPARE(l.variableName(), QString("plc1.Run"));
}

QTEST_MAIN(TestLedControl)
#include "tst_LedControl.moc"