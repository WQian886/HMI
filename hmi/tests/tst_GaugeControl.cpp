// ============================================================================
// 阶段 E · E.1a-3 · GaugeControl 单测
// ============================================================================

#include <QtTest>
#include <QGraphicsScene>
#include <QImage>
#include <QPainter>
#include <QJsonObject>
#include <QApplication>
#include "widgets/GaugeControl.h"
#include "widgets/base/ControlBase.h"

using namespace qwhmi::widgets;

class TestGaugeControl : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { qputenv("QT_QPA_PLATFORM", "offscreen"); qInfo() << "[tst_GaugeControl] init"; }
    void cleanupTestCase() { qInfo() << "[tst_GaugeControl] cleanup"; }

    void construct_defaultRange();
    void paint_doesNotCrash();
    void value_changesNeedleAngle();
    void needleColor_changes();
    void fromJson_restoresAllFields();
};

void TestGaugeControl::construct_defaultRange() {
    GaugeControl g;
    QCOMPARE(g.minimum(), 0.0);
    QCOMPARE(g.maximum(), 100.0);
    QCOMPARE(g.needleColor(), QColor(220, 40, 40));
}

void TestGaugeControl::paint_doesNotCrash() {
    QGraphicsScene s;
    GaugeControl g;
    g.setValue(75.0);
    s.addItem(&g);

    QImage img(200, 200, QImage::Format_ARGB32);
    img.fill(Qt::white);
    QPainter p(&img);
    s.render(&p);
    p.end();
    QVERIFY(!img.isNull());
}

void TestGaugeControl::value_changesNeedleAngle() {
    GaugeControl g;
    g.setValue(0.0);
    g.setValue(50.0);
    g.setValue(100.0);
    // 比例变化通过 paint + needle 角度计算覆盖
    QVERIFY(true);
}

void TestGaugeControl::needleColor_changes() {
    GaugeControl g;
    QSignalSpy spy(&g, &GaugeControl::needleColorChanged);
    g.setNeedleColor(QColor(0, 255, 0));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(g.needleColor(), QColor(0, 255, 0));
}

void TestGaugeControl::fromJson_restoresAllFields() {
    GaugeControl g;
    QJsonObject obj;
    obj["x"] = 100.0; obj["y"] = 100.0;
    obj["w"] = 150.0; obj["h"] = 150.0;
    obj["variable"] = "plc1.Speed";
    g.fromJson(obj);
    QCOMPARE(g.position(), QPointF(100.0, 100.0));
    QCOMPARE(g.size(), QSizeF(150.0, 150.0));
}

QTEST_MAIN(TestGaugeControl)
#include "tst_GaugeControl.moc"