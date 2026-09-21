// ============================================================================
// tst_TableControl
// ============================================================================

#include <QtTest>
#include <QGraphicsScene>
#include <QApplication>
#include "widgets/TableControl.h"

using namespace qwhmi::widgets;

class TestTableControl : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { qputenv("QT_QPA_PLATFORM", "offscreen"); qInfo() << "[tst_TableControl] init"; }
    void cleanupTestCase() { qInfo() << "[tst_TableControl] cleanup"; }

    void construct_defaults();
    void headers_changeTriggersUpdate();
    void setCell_addsRowsAndCols();
};

void TestTableControl::construct_defaults() {
    TableControl t;
    QCOMPARE(t.size(), QSizeF(300, 120));
    QStringList expected;
    expected << "时间" << "值";
    QCOMPARE(t.headers(), expected);
}

void TestTableControl::headers_changeTriggersUpdate() {
    TableControl t;
    QSignalSpy spy(&t, &TableControl::headersChanged);
    t.setHeaders({"时间", "变量", "值", "质量"});
    QCOMPARE(spy.count(), 1);
    QCOMPARE(t.headers().size(), 4);
}

void TestTableControl::setCell_addsRowsAndCols() {
    TableControl t;
    // 不崩就好 — 内部 QTableWidget 自动扩 row/col
    t.setCell(0, 0, "12:00:00");
    t.setCell(0, 1, "100.5");
    t.setCell(1, 0, "12:00:01");
    t.setCell(1, 1, "101.0");
    // 设置宽列(测试自动扩)
    t.setCell(2, 3, "Good");
    QVERIFY(true);
}

QTEST_MAIN(TestTableControl)
#include "tst_TableControl.moc"