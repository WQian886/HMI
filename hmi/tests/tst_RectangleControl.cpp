// ============================================================================
// tst_RectangleControl
// ============================================================================

#include <QtTest>
#include <QGraphicsScene>
#include <QImage>
#include <QPainter>
#include <QApplication>
#include "widgets/RectangleControl.h"
#include "widgets/base/ControlBase.h"

using namespace qwhmi::widgets;

class TestRectangleControl : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { qputenv("QT_QPA_PLATFORM", "offscreen"); qInfo() << "[tst_RectangleControl] init"; }
    void cleanupTestCase() { qInfo() << "[tst_RectangleControl] cleanup"; }

    void construct_defaultColors();
    void paint_doesNotCrash();
    void fillColor_changeTriggersUpdate();
    void fromJson_restoresAllFields();
};

void TestRectangleControl::construct_defaultColors() {
    RectangleControl r;
    QCOMPARE(r.size(), QSizeF(100, 20));   // default
    QCOMPARE(r.fillColor(), QColor(200, 220, 240));
    QCOMPARE(r.strokeColor(), QColor(80, 80, 80));
    QCOMPARE(r.strokeWidth(), 1);
}

void TestRectangleControl::paint_doesNotCrash() {
    QGraphicsScene s;
    RectangleControl r;
    s.addItem(&r);
    QImage img(120, 40, QImage::Format_ARGB32);
    img.fill(Qt::white);
    QPainter p(&img);
    s.render(&p);
    p.end();
    QVERIFY(!img.isNull());
}

void TestRectangleControl::fillColor_changeTriggersUpdate() {
    RectangleControl r;
    QSignalSpy spy(&r, &RectangleControl::fillColorChanged);
    r.setFillColor(QColor(255, 0, 0));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(r.fillColor(), QColor(255, 0, 0));
}

void TestRectangleControl::fromJson_restoresAllFields() {
    RectangleControl r;
    QJsonObject obj;
    obj["x"] = 50.0; obj["y"] = 30.0;
    obj["w"] = 200.0; obj["h"] = 60.0;
    obj["variable"] = "plc1.box";
    r.fromJson(obj);
    QCOMPARE(r.position(), QPointF(50.0, 30.0));
    QCOMPARE(r.size(), QSizeF(200.0, 60.0));
}

QTEST_MAIN(TestRectangleControl)
#include "tst_RectangleControl.moc"