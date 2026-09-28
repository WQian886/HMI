// ============================================================================
// 阶段 E · E.1c · tst_TankControl —— 储罐/液位控件
//
// 【测什么】
//   构造默认值 / fillRatio 比例算法(边界 + 越界 + max<=0) /
//   属性 setter 发信号 / paint 不崩 / fromJson 往返
//
// 【比例算法为什么单独测】fillRatio 是 TankControl 的全部业务逻辑。
//   现场最常见的两个坑:
//     · 液位计没标定(max 配成 0 或小于 min)→ 除零 / 负比例 → 画一圈乱线
//     · PLC 来的值超过程量范围 → 液位冲出罐外
//   qBound 夹不夹得住,只能靠测试锁。
//
// 【fromJson 用 width/height 不是 w/h】注意跟 tst_RectangleControl.cpp:59-60
//   的写法不一样。那边写 obj["w"]/obj["h"],而 ControlBase::propertiesToJson
//   (ControlBase.cpp:170-172)序列化 QSizeF 用的是 {"width","height"},
//   propertiesFromJson(ControlBase.cpp:194-197)也只认 width/height ——
//   所以 tst_RectangleControl::fromJson_restoresAllFields 是【既有失败】
//   (10 个控件测试全栽在这)。本测试用正确键名,不去复制那个 bug。
// ============================================================================

#include <QtTest>
#include <QApplication>
#include <QGraphicsScene>
#include <QImage>
#include <QPainter>
#include "widgets/TankControl.h"
#include "widgets/base/ControlBase.h"

using namespace qwhmi::widgets;

class TestTankControl : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { qputenv("QT_QPA_PLATFORM", "offscreen"); }

    void construct_defaults();
    void fillRatio_mapsValueToRange();
    void fillRatio_clampsOutOfRange();
    void fillRatio_zeroRangeReturnsZero();     // ★ 没标定的液位计
    void setters_emitSignals();
    void tickCount_clampsNegative();
    void paint_doesNotCrash();
    void paint_ticksSuppressedWhenZero();
    void fromJson_restoresAllFields();
    void toJson_roundTripsThroughFromJson();
    void registeredInPalette();               // ★ 调色板能拖出来
};

void TestTankControl::construct_defaults() {
    TankControl t;
    QCOMPARE(t.size(), QSizeF(100, 160));      // 储罐默认瘦高
    QCOMPARE(t.minimum(), 0.0);
    QCOMPARE(t.maximum(), 100.0);
    QCOMPARE(t.liquidColor(), QColor(0, 140, 220));
    QCOMPARE(t.tickCount(), 4);
    QVERIFY(t.canWrite() == false);            // 未绑定 → 不可写
}

void TestTankControl::fillRatio_mapsValueToRange() {
    TankControl t;
    t.setMinimum(0.0);
    t.setMaximum(200.0);
    t.setValue(50.0);
    QCOMPARE(t.fillRatio(), 0.25);
    t.setValue(100.0);
    QCOMPARE(t.fillRatio(), 0.5);
    t.setValue(200.0);
    QCOMPARE(t.fillRatio(), 1.0);
}

void TestTankControl::fillRatio_clampsOutOfRange() {
    // ★ PLC 来的值超过程量范围是常事(换了一台量程更大的液位计)。
    //   不夹的话液位会画出罐外,看着像罐子破了。
    TankControl t;
    t.setMinimum(0.0);
    t.setMaximum(100.0);
    t.setValue(150.0);
    QCOMPARE(t.fillRatio(), 1.0);
    t.setValue(-20.0);
    QCOMPARE(t.fillRatio(), 0.0);
}

void TestTankControl::fillRatio_zeroRangeReturnsZero() {
    // ★ 没标定就上线:maximum == minimum。range=0 直接 return 0,
    //   不能走 (v-min)/range 那条路(除零 → NaN → 画不出来)。
    TankControl t;
    t.setMinimum(50.0);
    t.setMaximum(50.0);
    t.setValue(50.0);
    QCOMPARE(t.fillRatio(), 0.0);

    // 配反了(max < min)同样不能出负比例
    t.setMinimum(100.0);
    t.setMaximum(0.0);
    t.setValue(50.0);
    QCOMPARE(t.fillRatio(), 0.0);
}

void TestTankControl::setters_emitSignals() {
    TankControl t;
    QSignalSpy m(&t, &TankControl::minimumChanged);
    QSignalSpy M(&t, &TankControl::maximumChanged);
    QSignalSpy tc(&t, &TankControl::tankColorChanged);
    QSignalSpy lc(&t, &TankControl::liquidColorChanged);
    QSignalSpy k(&t, &TankControl::tickCountChanged);

    t.setMinimum(-10.0);
    t.setMaximum(250.0);
    t.setTankColor(QColor(1, 2, 3));
    t.setLiquidColor(QColor(4, 5, 6));
    t.setTickCount(8);

    QCOMPARE(m.count(), 1);
    QCOMPARE(M.count(), 1);
    QCOMPARE(tc.count(), 1);
    QCOMPARE(lc.count(), 1);
    QCOMPARE(k.count(), 1);

    // 重复设同值不该再发信号(否则属性面板会无限刷)
    t.setMinimum(-10.0);
    QCOMPARE(m.count(), 1);
}

void TestTankControl::tickCount_clampsNegative() {
    // 负数会让 for 循环里 i < tickCount_ 的边界反了
    TankControl t;
    t.setTickCount(-5);
    QCOMPARE(t.tickCount(), 0);
}

void TestTankControl::paint_doesNotCrash() {
    // 覆盖 paint 里所有分支:空罐 / 半满 / 满罐 / 无刻度
    for (double v : {0.0, 50.0, 100.0}) {
        for (int ticks : {0, 4}) {
            for (bool sel : {false, true}) {
                QGraphicsScene s;
                TankControl t;
                t.setValue(v);
                t.setTickCount(ticks);
                t.setSelected(sel);
                s.addItem(&t);
                QImage img(140, 200, QImage::Format_ARGB32);
                img.fill(Qt::white);
                QPainter p(&img);
                s.render(&p);
                p.end();
                QVERIFY(!img.isNull());
                s.removeItem(&t);   // 栈对象:交回自己析构
            }
        }
    }
}

void TestTankControl::paint_ticksSuppressedWhenZero() {
    // tickCount_=0 时不画刻度 —— 靠不崩 + 不抛异常体现,像素级断言
    // 在这个项目里不做(见 tst_HandleVisualAlignment 也不做像素比对)
    QGraphicsScene s;
    TankControl t;
    t.setTickCount(0);
    t.setValue(75.0);
    s.addItem(&t);
    QImage img(140, 200, QImage::Format_ARGB32);
    img.fill(Qt::white);
    QPainter p(&img);
    s.render(&p);
    p.end();
    s.removeItem(&t);
    QVERIFY(!img.isNull());
}

void TestTankControl::fromJson_restoresAllFields() {
    TankControl t;
    QJsonObject obj;
    // ★ position / size 是复合类型,ControlBase::propertiesToJson 写成
    //   JSON object,key 分别是 x/y 和 width/height(不是 w/h)
    obj["position"] = QJsonObject{{"x", 50.0}, {"y", 30.0}};
    obj["size"]     = QJsonObject{{"width", 200.0}, {"height", 60.0}};
    obj["variableName"] = "plc1.Level";
    obj["minimum"]  = -20.0;
    obj["maximum"]  = 180.0;
    // ★ 颜色存的是 "#RRGGBB" 字符串(QColor::name() 的行为,Qt 6 默认)
    obj["liquidColor"] = "#0ac81e";
    obj["tickCount"] = 6;

    t.fromJson(obj);
    QCOMPARE(t.position(), QPointF(50.0, 30.0));
    QCOMPARE(t.size(), QSizeF(200.0, 60.0));
    QCOMPARE(t.variableName(), QString("plc1.Level"));
    QCOMPARE(t.minimum(), -20.0);
    QCOMPARE(t.maximum(), 180.0);
    QCOMPARE(t.liquidColor(), QColor(10, 200, 30));
    QCOMPARE(t.tickCount(), 6);
}

// ★ 存-读往返:E.3 的核心承诺(存盘再打开画面一模一样)。
//   走 propertiesToJson → fromJson 而不手写 JSON,才能测出
//   「序列化格式」和「反序列化格式」这对不上 —— 上面那个 w/h 的坑
//   就是这么漏到别的控件测试里的。
void TestTankControl::toJson_roundTripsThroughFromJson() {
    TankControl src;
    src.setPosition({123.0, 456.0});
    src.setSize({88.0, 132.0});
    src.setVariableName("plc1.Tank2.Level");
    src.setMinimum(-50.0);
    src.setMaximum(50.0);
    src.setLiquidColor(QColor(1, 2, 3));
    src.setTankColor(QColor(4, 5, 6));
    src.setTickCount(10);
    src.setValue(12.5);

    TankControl dst;
    dst.fromJson(src.toJson());

    QCOMPARE(dst.position(), src.position());
    QCOMPARE(dst.size(), src.size());
    QCOMPARE(dst.variableName(), src.variableName());
    QCOMPARE(dst.minimum(), src.minimum());
    QCOMPARE(dst.maximum(), src.maximum());
    QCOMPARE(dst.liquidColor(), src.liquidColor());
    QCOMPARE(dst.tankColor(), src.tankColor());
    QCOMPARE(dst.tickCount(), src.tickCount());
    QCOMPARE(dst.value(), src.value());
}

// ★ 构造时 registerControlType("Tank") → 调色板能拖出来。
//   忘了注册的话控件写得再好,用户也拖不出来 —— 但编译和所有单测都绿,
//   是个典型的"测不出来"的功能缺口,必须显式锁住
void TestTankControl::registeredInPalette() {
    const QStringList names = ControlBase::registeredTypeNames();
    QVERIFY2(names.contains("Tank"), "Tank 没进注册表 → 调色板拖不出储罐控件");
    QVERIFY(ControlBase::create("Tank") != nullptr);
    delete ControlBase::create("Tank");
}

QTEST_MAIN(TestTankControl)
#include "tst_TankControl.moc"
