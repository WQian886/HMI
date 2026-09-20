// ============================================================================
// tst_SliderControl
// ============================================================================

#include <QtTest>
#include <QGraphicsScene>
#include <QImage>
#include <QPainter>
#include <QApplication>
#include "widgets/SliderControl.h"

using namespace qwhmi::widgets;

class TestSliderControl : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { qputenv("QT_QPA_PLATFORM", "offscreen"); qInfo() << "[tst_SliderControl] init"; }
    void cleanupTestCase() { qInfo() << "[tst_SliderControl] cleanup"; }

    void construct_defaults();
    void paint_doesNotCrash();
    void minMax_changeTriggersUpdate();
    void value_inRange();
};

void TestSliderControl::construct_defaults() {
    SliderControl s;
    QCOMPARE(s.size(), QSizeF(200, 24));
    QCOMPARE(s.minimum(), 0.0);
    QCOMPARE(s.maximum(), 100.0);
    QCOMPARE(s.fillColor(), QColor(0, 120, 215));
    QCOMPARE(s.orientation(), Qt::Horizontal);
}

void TestSliderControl::paint_doesNotCrash() {
    QGraphicsScene scene;
    SliderControl sl;
    sl.setValue(50);
    scene.addItem(&sl);
    QImage img(220, 40, QImage::Format_ARGB32);
    img.fill(Qt::white);
    QPainter p(&img);
    scene.render(&p);
    p.end();
    QVERIFY(!img.isNull());
}

void TestSliderControl::minMax_changeTriggersUpdate() {
    SliderControl s;
    QSignalSpy spy(&s, &SliderControl::minimumChanged);
    s.setMinimum(-10);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(s.minimum(), -10.0);
}

void TestSliderControl::value_inRange() {
    SliderControl s;
    s.setValue(75.0);
    QCOMPARE(s.value(), QVariant(75.0));
}

QTEST_MAIN(TestSliderControl)
#include "tst_SliderControl.moc"