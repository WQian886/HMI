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

    // ★ F3(2026-09-30):proxy 里包的【不再是表格本身】,而是一个 container,
    //   里面竖排放"未确认计数标签 + 表格"(计数是 F3 新增的硬要求)。
    //   所以断言的对象从 proxy->widget() 换成了 container,表格从它里面找。
    //
    //   【为什么改测试而不是改实现把 container 撤掉】
    //   计数必须有地方显示,撤掉 container 就没地方放了。这个测试守的是
    //   E.4b 的意图(内嵌 widget 必须跟着 setSize 缩),不是"表格必须是顶层"。
    //   意图没变,变的只是多了一层容器 —— 所以断言跟着结构改,守的还是原来那条。
    QWidget* container = proxy->widget();
    QVERIFY2(container != nullptr, "proxy 必须包着一个 widget");
    QTableWidget* table = container->findChild<QTableWidget*>();
    QVERIFY2(table != nullptr, "container 里必须能找到 QTableWidget(计数标签之下)");

    qDebug() << "Before shrink: ControlBase.size =" << a->size();
    qDebug() << "Before shrink: container->size() =" << container->size();
    qDebug() << "Before shrink: table->size() =" << table->size();

    // 关键:缩到 170x80,container 必须跟着缩(E.4b 修的就是这个)
    a->setSize(QSizeF(170, 80));

    qDebug() << "After shrink: ControlBase.size =" << a->size();
    qDebug() << "After shrink: container->size() =" << container->size();
    qDebug() << "After shrink: table->size() =" << table->size();

    // E.4b 的原意:【代理里的顶层 widget】尺寸 == ControlBase 尺寸。
    // 表格不再直接等于它 —— 它要分掉一行给计数标签。
    QCOMPARE(container->size().width(),  170);
    QCOMPARE(container->size().height(), 80);

    // 表格必须【撑满】容器的宽度(横向由布局保证),且高度小于容器
    // (纵向要让出计数标签那一条)。这两条是 F3 加了标签之后新出现的可能坏点:
    // 一旦有人把表格的 size policy 写死,这里立刻红。
    QCOMPARE(table->size().width(), container->size().width());
    QVERIFY2(table->size().height() < container->size().height(),
             "表格高度必须小于容器高度 —— 容器顶部还有一条未确认计数标签,"
             "两者相等说明布局没把标签算进去(计数标签会被挤没)");

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
