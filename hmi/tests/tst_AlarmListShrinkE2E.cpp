// ============================================================================
// tst_AlarmListShrinkE2E — 验证缩小时 widget 实际尺寸跟缩
// ============================================================================

#include <QtTest>
#include <QApplication>
#include <QGraphicsScene>
#include <QGraphicsProxyWidget>
#include <QTableWidget>

#include "widgets/AlarmListControl.h"
#include "widgets/TrendControl.h"
#include "widgets/TableControl.h"

using namespace qwhmi::widgets;

class TestAlarmListShrinkE2E : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();

    // ★ 关键回归:ControlBase.setSize 后,嵌入的 widget 实际尺寸必须跟缩
    //   修复前:widget 实际尺寸 = 320x192(不跟缩)
    //   修复后:widget 实际尺寸 = setSize 传入的新尺寸
    void test_alarmlist_widget_size_tracks_setSize();
    void test_trend_widget_size_tracks_setSize();
    void test_table_widget_size_tracks_setSize();
};

void TestAlarmListShrinkE2E::initTestCase() {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    qInfo() << "[tst_AlarmListShrinkE2E] init";
}
void TestAlarmListShrinkE2E::cleanupTestCase() {
    qInfo() << "[tst_AlarmListShrinkE2E] cleanup";
}

void TestAlarmListShrinkE2E::test_alarmlist_widget_size_tracks_setSize() {
    QGraphicsScene scene;
    AlarmListControl* a = new AlarmListControl;
    a->setPos(QPointF(0, 0));
    a->setSize(QSizeF(320, 100));
    scene.addItem(a);

    QGraphicsProxyWidget* proxy = nullptr;
    for (QGraphicsItem* ch : a->childItems()) {
        proxy = qgraphicsitem_cast<QGraphicsProxyWidget*>(ch);
        if (proxy) break;
    }
    QVERIFY(proxy != nullptr);
    QTableWidget* table = qobject_cast<QTableWidget*>(proxy->widget());
    QVERIFY(table != nullptr);

    qDebug() << "Before shrink: ControlBase.size =" << a->size();
    qDebug() << "Before shrink: table->size() =" << table->size();

    // 关键:缩到 170x80,widget 必须跟着缩
    a->setSize(QSizeF(170, 80));

    qDebug() << "After shrink: ControlBase.size =" << a->size();
    qDebug() << "After shrink: table->size() =" << table->size();
    qDebug() << "After shrink: table->minimumSize() =" << table->minimumSize();

    QCOMPARE(table->size().width(),  170);
    QCOMPARE(table->size().height(), 80);

    delete a;
}

void TestAlarmListShrinkE2E::test_trend_widget_size_tracks_setSize() {
    QGraphicsScene scene;
    TrendControl* t = new TrendControl;
    t->setPos(QPointF(0, 0));
    t->setSize(QSizeF(280, 100));
    scene.addItem(t);

    t->setSize(QSizeF(150, 50));

    QGraphicsProxyWidget* proxy = nullptr;
    for (QGraphicsItem* ch : t->childItems()) {
        proxy = qgraphicsitem_cast<QGraphicsProxyWidget*>(ch);
        if (proxy) break;
    }
    QVERIFY(proxy != nullptr);
    QWidget* plot = proxy->widget();
    QVERIFY(plot != nullptr);
    qDebug() << "Trend after shrink: ControlBase.size =" << t->size()
             << " plot->size() =" << plot->size();

    QCOMPARE(plot->size().width(),  150);
    QCOMPARE(plot->size().height(), 50);

    delete t;
}

void TestAlarmListShrinkE2E::test_table_widget_size_tracks_setSize() {
    QGraphicsScene scene;
    TableControl* t = new TableControl;
    t->setPos(QPointF(0, 0));
    t->setSize(QSizeF(300, 120));
    scene.addItem(t);

    t->setSize(QSizeF(150, 60));

    QGraphicsProxyWidget* proxy = nullptr;
    for (QGraphicsItem* ch : t->childItems()) {
        proxy = qgraphicsitem_cast<QGraphicsProxyWidget*>(ch);
        if (proxy) break;
    }
    QVERIFY(proxy != nullptr);
    QTableWidget* table = qobject_cast<QTableWidget*>(proxy->widget());
    QVERIFY(table != nullptr);
    qDebug() << "Table after shrink: ControlBase.size =" << t->size()
             << " table->size() =" << table->size();

    QCOMPARE(table->size().width(),  150);
    QCOMPARE(table->size().height(), 60);

    delete t;
}

QTEST_MAIN(TestAlarmListShrinkE2E)
#include "tst_AlarmListShrinkE2E.moc"
