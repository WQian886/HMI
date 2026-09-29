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
    void toJson_fromJson_roundTrips();   // ★ 2026-09-29:锁住"写出去的读得回来"
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

    // ★★ 键名必须是 ControlBase 的 Q_PROPERTY 名,不是曾经瞎写的 x/y/w/h/variable。
    //   ControlBase.h:43-44 声明的是 position / size / variableName,
    //   而 propertiesFromJson 按 Q_PROPERTY 名反查(obj.contains(name) 不中就跳过),
    //   所以写成 x/w 的话【整个对象被静默忽略】,控件保持默认值 —— 而 QCOMPARE
    //   只报"值不等",完全看不出是"字段名没匹配上"。这批用例红了很久没人查,
    //   就是因为失败信息指向的是值,不像键名问题。
    //   正确形状:position/size 是【嵌套对象】,不是平铺的 x/y/w/h
    //   (见 ControlBase.cpp propertiesToJson 的 QMetaType::QPointF 分支)
    QJsonObject obj;
    obj["position"] = QJsonObject{{"x", 100.0}, {"y", 200.0}};
    obj["size"]     = QJsonObject{{"width", 250.0}, {"height", 60.0}};
    obj["variableName"] = "plc2.Pressure";

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
    // ★ 同上:键名跟 Q_PROPERTY 对齐,position/size 是嵌套对象
    const QJsonObject pos = obj["position"].toObject();
    const QJsonObject siz = obj["size"].toObject();
    QCOMPARE(pos["x"].toDouble(), 50.0);
    QCOMPARE(pos["y"].toDouble(), 75.0);
    QCOMPARE(siz["width"].toDouble(), 180.0);
    QCOMPARE(siz["height"].toDouble(), 50.0);
    QCOMPARE(obj["variableName"].toString(), QString("plc1.Temp"));
    QVERIFY(obj.contains("type"));   // 含类名(E.3 JSON 加载用)
}

// ★★ 补一条:toJson → fromJson 必须真能往返。
//   上一条 toJson 用例只验"输出里有什么",不验"fromJson 读得回来" ——
//   键名两边不一致的话(正是这批用例之前的状况),它照样绿。
//   这条才是真正锁住"写出去的读得回来"的断言。
void TestLabelControl::toJson_fromJson_roundTrips() {
    LabelControl src;
    src.setPosition(QPointF(11.0, 22.0));
    src.setSize(QSizeF(33.0, 44.0));
    src.setVariableName("plc9.RoundTrip");

    LabelControl dst;
    dst.fromJson(src.toJson());

    QCOMPARE(dst.position(), src.position());
    QCOMPARE(dst.size(), src.size());
    QCOMPARE(dst.variableName(), src.variableName());
}

// ★ QTEST_MAIN(创建 QApplication — paint 测试需要 GUI 主循环)
//   initTestCase 已经设 QT_QPA_PLATFORM=offscreen,无窗口弹出
QTEST_MAIN(TestLabelControl)
#include "tst_LabelControl.moc"