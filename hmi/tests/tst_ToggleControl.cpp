// ============================================================================
// tst_ToggleControl
// ============================================================================

#include <QtTest>
#include <QGraphicsScene>
#include <QImage>
#include <QPainter>
#include <QApplication>
#include "widgets/ToggleControl.h"

using namespace qwhmi::widgets;

class TestToggleControl : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { qputenv("QT_QPA_PLATFORM", "offscreen"); qInfo() << "[tst_ToggleControl] init"; }
    void cleanupTestCase() { qInfo() << "[tst_ToggleControl] cleanup"; }

    void construct_defaults();
    void paint_doesNotCrash();
    void onText_changeTriggersUpdate();
    void value_one_meansOn();
};

void TestToggleControl::construct_defaults() {
    ToggleControl t;
    QCOMPARE(t.onText(), QString("ON"));
    QCOMPARE(t.offText(), QString("OFF"));
    QCOMPARE(t.onColor(), QColor(40, 180, 40));
    // value 默认 invalid QVariant(ControlBase 未显式初始化)
    QVERIFY(!t.value().isValid());
}

void TestToggleControl::paint_doesNotCrash() {
    QGraphicsScene s;
    ToggleControl t;
    s.addItem(&t);
    QImage img(100, 60, QImage::Format_ARGB32);
    img.fill(Qt::white);
    QPainter p(&img);
    s.render(&p);
    p.end();
    QVERIFY(!img.isNull());
}

void TestToggleControl::onText_changeTriggersUpdate() {
    ToggleControl t;
    QSignalSpy spy(&t, &ToggleControl::onTextChanged);
    t.setOnText("运行");
    QCOMPARE(spy.count(), 1);
    QCOMPARE(t.onText(), QString("运行"));
}

void TestToggleControl::value_one_meansOn() {
    ToggleControl t;
    t.setValue(1.0);
    QCOMPARE(t.value(), QVariant(1.0));   // ON 状态
    t.setValue(0.0);
    QCOMPARE(t.value(), QVariant(0.0));   // OFF 状态
}

QTEST_MAIN(TestToggleControl)
#include "tst_ToggleControl.moc"