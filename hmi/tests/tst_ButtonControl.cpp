// ============================================================================
// tst_ButtonControl
// ============================================================================

#include <QtTest>
#include <QGraphicsScene>
#include <QImage>
#include <QPainter>
#include <QApplication>
#include "widgets/ButtonControl.h"

using namespace qwhmi::widgets;

class TestButtonControl : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { qputenv("QT_QPA_PLATFORM", "offscreen"); qInfo() << "[tst_ButtonControl] init"; }
    void cleanupTestCase() { qInfo() << "[tst_ButtonControl] cleanup"; }

    void construct_defaults();
    void paint_doesNotCrash();
    void text_changeTriggersUpdate();
};

void TestButtonControl::construct_defaults() {
    ButtonControl b;
    QCOMPARE(b.text(), QString("Button"));
    QCOMPARE(b.size(), QSizeF(100, 36));
    QCOMPARE(b.background(), QColor(0, 120, 215));
}

void TestButtonControl::paint_doesNotCrash() {
    QGraphicsScene s;
    ButtonControl b;
    s.addItem(&b);
    QImage img(120, 60, QImage::Format_ARGB32);
    img.fill(Qt::white);
    QPainter p(&img);
    s.render(&p);
    p.end();
    QVERIFY(!img.isNull());
}

void TestButtonControl::text_changeTriggersUpdate() {
    ButtonControl b;
    QSignalSpy spy(&b, &ButtonControl::textChanged);
    b.setText("启动");
    QCOMPARE(spy.count(), 1);
    QCOMPARE(b.text(), QString("启动"));
}

QTEST_MAIN(TestButtonControl)
#include "tst_ButtonControl.moc"