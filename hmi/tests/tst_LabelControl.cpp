// ============================================================================
// 阶段 E · E.1a · LabelControl 单测
//
// 验证 5 个核心行为:
//   1. construct_createsItem         构造 → QGraphicsItem 在场景里
//   2. paint_doesNotCrash             paint() 调用不崩(QPainter 测试用 offscreen)
//   3. bind_setsValueAndEmits         bind(Variable) → setValue 触发 valueChanged
//   4. fromJson_loadsRestoresValues   JSON 反序列化恢复属性
//   5. toJson_serializesAllFields     JSON 序列化包含所有字段
//
// 关联:LabelControl.h, ControlBase.h, VariableManager.h
// ============================================================================

#include <QtTest>
#include <QSignalSpy>
#include <QGraphicsScene>
#include <QJsonObject>
#include <QImage>
#include <QPainter>
#include <QApplication>      // paint() 需要 QApplication(不是 QCoreApplication)
#include "widgets/LabelControl.h"
#include "widgets/base/ControlBase.h"
#include "VariableManager.h"
#include "Variable.h"

using namespace qwhmi::widgets;

class TestLabelControl : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();    // QApplication + offscreen
    void cleanupTestCase();

    void construct_createsItem();
    void paint_doesNotCrash();
    void bind_setsValueAndEmits();
    void fromJson_loadsRestoresValues();
    void toJson_serializesAllFields();
};

void TestLabelControl::initTestCase() {
    // ★ paint() 需要 GUI 主循环(QGraphicsScene/View + QPainter)
    qputenv("QT_QPA_PLATFORM", "offscreen");
    qInfo() << "[tst_LabelControl] init,QT_QPA_PLATFORM=" << qgetenv("QT_QPA_PLATFORM");
}

void TestLabelControl::cleanupTestCase() {
    qInfo() << "[tst_LabelControl] cleanup";
}

// ★ 用例 1:构造 → item 在 scene 里
void TestLabelControl::construct_createsItem() {
    QGraphicsScene s;
    auto* lbl = new LabelControl;
    s.addItem(lbl);

    QVERIFY(lbl != nullptr);
    QCOMPARE(lbl->size(), QSizeF(120, 40));        // 默认 120x40
    QCOMPARE(lbl->variableName(), QString(""));    // 默认无变量
    QCOMPARE(lbl->value(), QVariant());            // 默认空值

    // ★ 选中 + 可移动 flag 应已设
    QVERIFY(lbl->flags() & QGraphicsItem::ItemIsMovable);
    QVERIFY(lbl->flags() & QGraphicsItem::ItemIsSelectable);
    QVERIFY(lbl->flags() & QGraphicsItem::ItemSendsGeometryChanges);

    delete lbl;
}

// ★ 用例 2:paint 不崩(QGraphicsScene render to offscreen image)
void TestLabelControl::paint_doesNotCrash() {
    QGraphicsScene s;
    auto* lbl = new LabelControl;
    lbl->setValue(QString("Hello"));
    s.addItem(lbl);

    // ★ 渲染到 QImage(offscreen 平台不会真弹窗)
    QImage img(200, 100, QImage::Format_ARGB32);
    img.fill(Qt::white);
    QPainter p(&img);
    s.render(&p);
    p.end();

    QVERIFY(!img.isNull());
    // 选 + 移 → 触发 itemChange + 重绘
    lbl->setSelected(true);
    lbl->setPos(50, 30);
    QVERIFY(true);  // 没崩 = 通过

    delete lbl;
}

// ★ 用例 3:bind Variable → setValue + emit valueChanged
void TestLabelControl::bind_setsValueAndEmits() {
    VariableManager mgr;
    auto* speed = new Variable("Speed", 0, Variable::DataType::Float,
                               Variable::Access::ReadOnly, &mgr);
    speed->setPlcId("plc1");
    speed->setValue(QVariant::fromValue<float>(123.45f));

    LabelControl lbl;
    QSignalSpy spy(&lbl, &ControlBase::valueChanged);

    lbl.bind(speed);
    QCOMPARE(lbl.variableName(), QString("plc1.Speed"));
    QCOMPARE(lbl.value().toFloat(), 123.45f);
    QCOMPARE(spy.count(), 1);   // bind → setValue → emit 1 次

    // ★ Variable setValue → 控件 valueChanged
    speed->setValue(QVariant::fromValue<float>(99.9f));
    QTest::qWait(50);
    QCOMPARE(lbl.value().toFloat(), 99.9f);
    QVERIFY(spy.count() >= 2);
}

// ★ 用例 4:fromJson 加载恢复属性
void TestLabelControl::fromJson_loadsRestoresValues() {
    LabelControl lbl;

    QJsonObject obj;
    obj["x"] = 100.0;
    obj["y"] = 200.0;
    obj["w"] = 250.0;
    obj["h"] = 60.0;
    obj["variable"] = "plc2.Pressure";

    lbl.fromJson(obj);

    QCOMPARE(lbl.position(), QPointF(100.0, 200.0));
    QCOMPARE(lbl.size(), QSizeF(250.0, 60.0));
    QCOMPARE(lbl.variableName(), QString("plc2.Pressure"));
}

// ★ 用例 5:toJson 包含所有字段
void TestLabelControl::toJson_serializesAllFields() {
    LabelControl lbl;
    lbl.setPosition(QPointF(50, 75));
    lbl.setSize(QSizeF(180, 50));
    lbl.setVariableName("plc1.Temp");

    QJsonObject obj = lbl.toJson();
    QCOMPARE(obj["x"].toDouble(), 50.0);
    QCOMPARE(obj["y"].toDouble(), 75.0);
    QCOMPARE(obj["w"].toDouble(), 180.0);
    QCOMPARE(obj["h"].toDouble(), 50.0);
    QCOMPARE(obj["variable"].toString(), QString("plc1.Temp"));
    QVERIFY(obj.contains("type"));   // 含类名(E.3 JSON 加载用)
}

// ★ QTEST_MAIN(创建 QApplication — paint 测试需要 GUI 主循环)
//   initTestCase 已经设 QT_QPA_PLATFORM=offscreen,无窗口弹出
QTEST_MAIN(TestLabelControl)
#include "tst_LabelControl.moc"