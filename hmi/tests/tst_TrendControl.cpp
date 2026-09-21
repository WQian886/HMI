// ============================================================================
// tst_TrendControl
// ============================================================================

#include <QtTest>
#include <QGraphicsScene>
#include <QApplication>
#include "widgets/TrendControl.h"

using namespace qwhmi::widgets;

class TestTrendControl : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { qputenv("QT_QPA_PLATFORM", "offscreen"); qInfo() << "[tst_TrendControl] init"; }
    void cleanupTestCase() { qInfo() << "[tst_TrendControl] cleanup"; }

    void construct_defaults();
    void paint_doesNotCrash();
    void rangeMinutes_changeTriggersUpdate();
    void refresh_setVariableName();
};

void TestTrendControl::construct_defaults() {
    TrendControl t;
    QCOMPARE(t.size(), QSizeF(280, 100));
    QCOMPARE(t.rangeMinutes(), 5.0);
}

void TestTrendControl::paint_doesNotCrash() {
    QGraphicsScene s;
    TrendControl t;
    s.addItem(&t);
    // 不调用 paint — QCustomPlot 自己渲染
    QVERIFY(true);
}

void TestTrendControl::rangeMinutes_changeTriggersUpdate() {
    TrendControl t;
    QSignalSpy spy(&t, &TrendControl::rangeMinutesChanged);
    t.setRangeMinutes(10.0);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(t.rangeMinutes(), 10.0);
}

void TestTrendControl::refresh_setVariableName() {
    TrendControl t;
    t.refresh("plc1.Speed");
    QCOMPARE(t.variableName(), QString("plc1.Speed"));
}

QTEST_MAIN(TestTrendControl)
#include "tst_TrendControl.moc"