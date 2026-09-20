// ============================================================================
// tst_LineControl
// ============================================================================

#include <QtTest>
#include <QGraphicsScene>
#include <QImage>
#include <QPainter>
#include <QApplication>
#include "widgets/LineControl.h"
#include "widgets/base/ControlBase.h"

using namespace qwhmi::widgets;

class TestLineControl : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { qputenv("QT_QPA_PLATFORM", "offscreen"); qInfo() << "[tst_LineControl] init"; }
    void cleanupTestCase() { qInfo() << "[tst_LineControl] cleanup"; }

    void construct_defaults();
    void paint_doesNotCrash();
    void strokeColor_changeTriggersUpdate();
    void fromJson_restoresAllFields();
};

void TestLineControl::construct_defaults() {
    LineControl l;
    QCOMPARE(l.size(), QSizeF(120, 10));
    QCOMPARE(l.strokeColor(), QColor(80, 80, 80));
    QCOMPARE(l.strokeWidth(), 2);
}

void TestLineControl::paint_doesNotCrash() {
    QGraphicsScene s;
    LineControl l;
    s.addItem(&l);
    QImage img(140, 20, QImage::Format_ARGB32);
    img.fill(Qt::white);
    QPainter p(&img);
    s.render(&p);
    p.end();
    QVERIFY(!img.isNull());
}

void TestLineControl::strokeColor_changeTriggersUpdate() {
    LineControl l;
    QSignalSpy spy(&l, &LineControl::strokeColorChanged);
    l.setStrokeColor(QColor(255, 0, 0));
    QCOMPARE(spy.count(), 1);
}

void TestLineControl::fromJson_restoresAllFields() {
    LineControl l;
    QJsonObject obj;
    obj["x"] = 10.0; obj["y"] = 100.0;
    obj["w"] = 200.0; obj["h"] = 5.0;
    l.fromJson(obj);
    QCOMPARE(l.size(), QSizeF(200.0, 5.0));
}

QTEST_MAIN(TestLineControl)
#include "tst_LineControl.moc"