// ============================================================================
// tst_InputEditControl
// ============================================================================

#include <QtTest>
#include <QGraphicsScene>
#include <QApplication>
#include "widgets/InputEditControl.h"

using namespace qwhmi::widgets;

class TestInputEditControl : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { qputenv("QT_QPA_PLATFORM", "offscreen"); qInfo() << "[tst_InputEditControl] init"; }
    void cleanupTestCase() { qInfo() << "[tst_InputEditControl] cleanup"; }

    void construct_defaults();
    void placeholder_changeTriggersUpdate();
    void setText_appearsInTextGetter();
    void setValue_pushesToLineEdit();
    void readOnly_changeTriggersUpdate();
};

void TestInputEditControl::construct_defaults() {
    InputEditControl i;
    QCOMPARE(i.size(), QSizeF(160, 28));
    QCOMPARE(i.placeholder(), QString("输入..."));
    QVERIFY(!i.readOnly());
}

void TestInputEditControl::placeholder_changeTriggersUpdate() {
    InputEditControl i;
    QSignalSpy spy(&i, &InputEditControl::placeholderChanged);
    i.setPlaceholder("新提示");
    QCOMPARE(spy.count(), 1);
    QCOMPARE(i.placeholder(), QString("新提示"));
}

void TestInputEditControl::setText_appearsInTextGetter() {
    InputEditControl i;
    i.setText("12345");
    QCOMPARE(i.text(), QString("12345"));
}

void TestInputEditControl::setValue_pushesToLineEdit() {
    InputEditControl i;
    i.setValue(QString("abc"));   // Variable 变化推给 lineEdit
    QCOMPARE(i.text(), QString("abc"));
}

void TestInputEditControl::readOnly_changeTriggersUpdate() {
    InputEditControl i;
    QSignalSpy spy(&i, &InputEditControl::readOnlyChanged);
    i.setReadOnly(true);
    QCOMPARE(spy.count(), 1);
    QVERIFY(i.readOnly());
}

QTEST_MAIN(TestInputEditControl)
#include "tst_InputEditControl.moc"