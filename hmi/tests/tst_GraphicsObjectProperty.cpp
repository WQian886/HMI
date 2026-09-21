// ============================================================================
// tst_GraphicsObjectProperty — 验证 QGraphicsObject 的 Q_PROPERTY 行为
// ============================================================================

#include <QtTest>
#include <QGraphicsScene>
#include <QApplication>
#include <QUndoStack>

#include "widgets/AlarmListControl.h"
#include "widgets/BarControl.h"
#include "widgets/base/ControlBase.h"
#include "editor/PropertySheet.h"

using namespace qwhmi::widgets;
using namespace qwhmi::editor;

class TestGraphicsObjectProperty : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { qputenv("QT_QPA_PLATFORM", "offscreen"); qInfo() << "[tst_GOP] init"; }
    void cleanupTestCase() { qInfo() << "[tst_GOP] cleanup"; }

    void verify_qgraphicsobject_properties_on_alarmlist();
    void verify_propertySheet_builds_correctly();
    void verify_sceneBoundingRect_updates_after_setSize();   // ★ Bug 1 残影回归
};

void TestGraphicsObjectProperty::verify_qgraphicsobject_properties_on_alarmlist() {
    AlarmListControl a;
    a.setSize(QSizeF(320, 100));   // 默认 320x100
    a.setPos(QPointF(100, 50));

    qDebug() << "==== QGraphicsObject Q_PROPERTY on AlarmListControl (size=320x100, pos=100,50) ====";
    qDebug() << "x =" << a.property("x").toDouble();
    qDebug() << "y =" << a.property("y").toDouble();
    qDebug() << "width =" << a.property("width").toDouble();
    qDebug() << "height =" << a.property("height").toDouble();
    qDebug() << "opacity =" << a.property("opacity").toDouble();
    qDebug() << "visible =" << a.property("visible").toBool();
    qDebug() << "enabled =" << a.property("enabled").toBool();
    qDebug() << "objectName =" << a.property("objectName").toString();

    qDebug() << "==== ControlBase Q_PROPERTY ====";
    qDebug() << "position =" << a.property("position").toPointF();
    qDebug() << "size =" << a.property("size").toSizeF();

    // ★ 关键断言:Bug 2a 修复后 width/height 跟 size 同步
    QCOMPARE(a.property("width").toDouble(), 320.0);
    QCOMPARE(a.property("height").toDouble(), 100.0);
    QCOMPARE(a.property("size").toSizeF(), QSizeF(320, 100));
}

void TestGraphicsObjectProperty::verify_propertySheet_builds_correctly() {
    AlarmListControl a;
    a.setSize(QSizeF(320, 100));
    a.setPos(QPointF(100, 50));

    QUndoStack undoStack;
    PropertySheet ps(&undoStack);
    ps.setTarget(&a);

    qDebug() << "==== PropertySheet 显示的字段 ====";
    qDebug() << "(反射时 width/height 由 ControlBase 重定义,跟随 size)";
    qDebug() << "property('width') =" << a.property("width").toDouble();
    qDebug() << "property('height') =" << a.property("height").toDouble();
}

// ★ Bug 1 残影回归测试:setSize 后 sceneBoundingRect 必须正确反映新 size
//   (Qt QGraphicsItem 强制要求:setSize 调 prepareGeometryChange,
//    否则 BspTreeIndex 缓存不更新,缩小时旧区域内容残留)
void TestGraphicsObjectProperty::verify_sceneBoundingRect_updates_after_setSize() {
    QGraphicsScene scene;
    AlarmListControl a;
    scene.addItem(&a);
    a.setPos(QPointF(0, 0));
    a.setSize(QSizeF(200, 100));

    const QRectF before = a.sceneBoundingRect();
    QCOMPARE(before.size(), QSizeF(200, 100));

    a.setSize(QSizeF(50, 30));   // 缩到很小

    const QRectF after = a.sceneBoundingRect();
    qDebug() << "after setSize(50,30): sceneBoundingRect =" << after;
    QCOMPARE(after.size(), QSizeF(50, 30));   // ★ 必须等于新 size,不残留旧 200x100
}

QTEST_MAIN(TestGraphicsObjectProperty)
#include "tst_GraphicsObjectProperty.moc"
