// ============================================================================
// 阶段 E · E.1c · tst_PipeControl —— 管道(纯显示类)
//
// 【测什么】
//   默认值 / 流动判定(含 0 死区、反向流) / 管径夹取 / paint 覆盖各分支 /
//   存读往返 / 调色板注册
//
// 【为什么管道不做写回】管道不是执行机构,没有"开关管道"这回事 ——
// 要关介质得关两端的阀门。流量是测量值,操作员改不了。
// 这一点跟 TankControl 一样,测试里也明确断言 canWrite() 为假,
// 免得将来有人"顺手给它加个点击"破坏语义。
//
// 【管径的坑】thickness 是像素值,属性面板是人手填的。填 0 会画出零宽
//   管子,Qt 什么都不画但 boundingRect 还在,后面选中框/handle 会去跟
//   一个看不见的东西对齐 —— 那种 bug 极难定位。setter 里夹到 [4,120]。
// ============================================================================

#include <QtTest>
#include <QApplication>
#include <QGraphicsScene>
#include <QImage>
#include <QPainter>
#include <QJsonObject>
#include "widgets/PipeControl.h"
#include "widgets/base/ControlBase.h"

using namespace qwhmi::widgets;

class TestPipeControl : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { qputenv("QT_QPA_PLATFORM", "offscreen"); }

    void construct_defaults();
    void isFlowing_usesDeadband();
    void isReversed_followsSign();
    void setters_emitSignals();
    void thickness_clampsOutOfRange();
    void thickPipe_widensBoundingRect();
    void paint_doesNotCrash();
    void paint_noWriteChannel();
    void fromJson_restoresAllFields();
    void toJson_roundTripsThroughFromJson();
    void registeredInPalette();
};

void TestPipeControl::construct_defaults() {
    PipeControl p;
    QCOMPARE(p.size(), QSizeF(40, 120));              // 默认竖管瘦高
    QCOMPARE(p.thickness(), 16);
    QCOMPARE(p.pipeColor(), QColor(160, 165, 170));
    QCOMPARE(p.flowColor(), QColor(0, 160, 220));
    QVERIFY(!p.isFlowing());
    QVERIFY(!p.isReversed());
}

// ★ 死区:现场流量计分辨率通常 0.01,再小就是噪声。
//   用严格 > 0 的话 0.0001 的噪声也会点亮箭头,画面上一片管子都在闪
void TestPipeControl::isFlowing_usesDeadband() {
    PipeControl p;
    p.setValue(0.0);     QVERIFY(!p.isFlowing());
    p.setValue(0.005);   QVERIFY(!p.isFlowing());   // 死区内
    p.setValue(0.01);    QVERIFY(!p.isFlowing());   // 边界:还是不算
    p.setValue(0.011);   QVERIFY(p.isFlowing());
    p.setValue(10.0);    QVERIFY(p.isFlowing());
    // ★ 取绝对值:回流管用负值表示反向,箭头朝反方向但确实是"在流"
    p.setValue(-50.0);   QVERIFY(p.isFlowing());
}

void TestPipeControl::isReversed_followsSign() {
    PipeControl p;
    p.setValue(10.0);   QVERIFY(!p.isReversed());
    p.setValue(-10.0);  QVERIFY(p.isReversed());
}

void TestPipeControl::setters_emitSignals() {
    PipeControl p;
    QSignalSpy pc(&p, &PipeControl::pipeColorChanged);
    QSignalSpy fc(&p, &PipeControl::flowColorChanged);
    QSignalSpy tc(&p, &PipeControl::thicknessChanged);
    p.setPipeColor(QColor(1, 2, 3));
    p.setFlowColor(QColor(4, 5, 6));
    p.setThickness(24);
    QCOMPARE(pc.count(), 1);
    QCOMPARE(fc.count(), 1);
    QCOMPARE(tc.count(), 1);
    p.setThickness(24);
    QCOMPARE(tc.count(), 1);   // 同值不重复发
}

void TestPipeControl::thickness_clampsOutOfRange() {
    // 0 宽管子画不出东西但 boundingRect 还在,选中框会去跟一个看不见的
    // 东西对齐 —— 那种 bug 极难定位,必须在 setter 挡掉
    PipeControl p;
    p.setThickness(0);
    QCOMPARE(p.thickness(), 4);
    p.setThickness(-50);
    QCOMPARE(p.thickness(), 4);
    p.setThickness(9999);
    QCOMPARE(p.thickness(), 120);
}

// ★ 管身比 boundingRect 还宽时要跟着撑开,否则管子被裁掉一截
void TestPipeControl::thickPipe_widensBoundingRect() {
    PipeControl p;
    p.setThickness(16);
    QVERIFY(p.size().width() >= 16);
    p.setThickness(100);
    QVERIFY2(p.size().width() >= 100, "管径 100 但控件宽度不够,管子会被 boundingRect 裁掉");
    QVERIFY(p.boundingRect().contains(QRectF(0, 0, 100, p.size().height())));
}

void TestPipeControl::paint_doesNotCrash() {
    // 覆盖 paint 全部分支:静止 / 正向流 / 反向流 × 细管/粗管 × 选中与否
    for (double v : {0.0, 5.0, -5.0}) {
        for (int th : {4, 16, 80}) {
            for (bool sel : {false, true}) {
                QGraphicsScene s;
                PipeControl p;
                p.setValue(v);
                p.setThickness(th);
                p.setSelected(sel);
                s.addItem(&p);
                QImage img(160, 200, QImage::Format_ARGB32);
                img.fill(Qt::white);
                QPainter pt(&img);
                s.render(&pt);
                pt.end();
                QVERIFY(!img.isNull());
                s.removeItem(&p);
            }
        }
    }
}

// ★ 管道是纯显示件,没有写回通道。
//   显式锁住:将来有人"顺手给它加个点击"时这条会红,提醒他停下来想想
void TestPipeControl::paint_noWriteChannel() {
    PipeControl p;
    QVERIFY2(!p.canWrite(), "管道不该有写回通道 —— 要关介质得关两端的阀门");
    // 写回接口本身存在(基类的),但对未绑定的管道必须拒
    QVERIFY(!p.writeToPlc(1.0));
    // 本地值也不许变。QVariant() 是 invalid 的,QCOMPARE 对 invalid 比对会打印
    // 不可读的差异,所以直接断言 isValid()
    QVERIFY2(!p.value().isValid(), "写被拒时本地值必须原样不动");
}

void TestPipeControl::fromJson_restoresAllFields() {
    PipeControl p;
    QJsonObject obj;
    obj["position"] = QJsonObject{{"x", 15.0}, {"y", 25.0}};
    obj["size"]     = QJsonObject{{"width", 50.0}, {"height", 150.0}};
    obj["variableName"] = "plc1.Flow1";
    obj["pipeColor"] = "#0a0a0a";
    obj["flowColor"] = "#0b0b0b";
    obj["thickness"] = 30;
    p.fromJson(obj);
    QCOMPARE(p.position(), QPointF(15.0, 25.0));
    QCOMPARE(p.size(), QSizeF(50.0, 150.0));
    QCOMPARE(p.variableName(), QString("plc1.Flow1"));
    QCOMPARE(p.pipeColor(), QColor(0x0a, 0x0a, 0x0a));
    QCOMPARE(p.flowColor(), QColor(0x0b, 0x0b, 0x0b));
    QCOMPARE(p.thickness(), 30);
}

void TestPipeControl::toJson_roundTripsThroughFromJson() {
    PipeControl src;
    src.setPosition({33.0, 44.0});
    src.setSize({55.0, 160.0});
    src.setVariableName("plc1.Flow2");
    src.setPipeColor(QColor(1, 2, 3));
    src.setFlowColor(QColor(4, 5, 6));
    src.setThickness(28);
    src.setValue(12.5);

    PipeControl dst;
    dst.fromJson(src.toJson());
    QCOMPARE(dst.position(), src.position());
    QCOMPARE(dst.size(), src.size());
    QCOMPARE(dst.variableName(), src.variableName());
    QCOMPARE(dst.pipeColor(), src.pipeColor());
    QCOMPARE(dst.flowColor(), src.flowColor());
    QCOMPARE(dst.thickness(), src.thickness());
    QCOMPARE(dst.value(), src.value());
}

void TestPipeControl::registeredInPalette() {
    QVERIFY(ControlBase::registeredTypeNames().contains("Pipe"));
    ControlBase* p = ControlBase::create("Pipe");
    QVERIFY(p != nullptr);
    delete p;
}

QTEST_MAIN(TestPipeControl)
#include "tst_PipeControl.moc"
