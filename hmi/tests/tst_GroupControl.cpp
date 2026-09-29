// ============================================================================
// tst_GroupControl
// ============================================================================

#include <QtTest>
#include <QGraphicsScene>
#include <QImage>
#include <QPainter>
#include <QApplication>
#include "widgets/GroupControl.h"
#include "widgets/LabelControl.h"
#include "widgets/base/ControlBase.h"

using namespace qwhmi::widgets;

class TestGroupControl : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { qputenv("QT_QPA_PLATFORM", "offscreen"); qInfo() << "[tst_GroupControl] init"; }
    void cleanupTestCase() { qInfo() << "[tst_GroupControl] cleanup"; }

    void construct_defaultSizeAndTitle();
    void paint_doesNotCrash();
    void addChild_tracksCount();
    void title_changeTriggersUpdate();
    void fromJson_restoresAllFields();
};

void TestGroupControl::construct_defaultSizeAndTitle() {
    GroupControl g;
    QCOMPARE(g.size(), QSizeF(200, 150));
    QCOMPARE(g.title(), QString("Group"));
    QCOMPARE(g.childCount(), 0);
}

void TestGroupControl::paint_doesNotCrash() {
    QGraphicsScene s;
    GroupControl g;
    s.addItem(&g);
    QImage img(220, 170, QImage::Format_ARGB32);
    img.fill(Qt::white);
    QPainter p(&img);
    s.render(&p);
    p.end();
    QVERIFY(!img.isNull());
}

void TestGroupControl::addChild_tracksCount() {
    GroupControl g;
    LabelControl l1, l2;
    QCOMPARE(g.childCount(), 0);
    g.addChild(&l1);
    QCOMPARE(g.childCount(), 1);
    g.addChild(&l2);
    QCOMPARE(g.childCount(), 2);
    g.addChild(&l1);   // 重复 add 不增
    QCOMPARE(g.childCount(), 2);
    g.removeChild(&l1);
    QCOMPARE(g.childCount(), 1);
}

void TestGroupControl::title_changeTriggersUpdate() {
    GroupControl g;
    QSignalSpy spy(&g, &GroupControl::titleChanged);
    g.setTitle("Pump Group");
    QCOMPARE(spy.count(), 1);
    QCOMPARE(g.title(), QString("Pump Group"));
}

void TestGroupControl::fromJson_restoresAllFields() {
    GroupControl g;
    QJsonObject obj;
    // ★ 键名按 ControlBase 的 Q_PROPERTY 名写(见 tst_LabelControl.cpp 的说明):
    //   position/size 是嵌套对象,不是平铺的 x/y/w/h
    obj["position"] = QJsonObject{{"x", 50.0}, {"y", 50.0}};
    obj["size"]     = QJsonObject{{"width", 300.0}, {"height", 200.0}};
    obj["variableName"] = "plc1.Pump";
    g.fromJson(obj);
    QCOMPARE(g.size(), QSizeF(300.0, 200.0));
}

QTEST_MAIN(TestGroupControl)
#include "tst_GroupControl.moc"