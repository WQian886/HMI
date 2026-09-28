// ============================================================================
// 阶段 E · E.1c · tst_MotorControl —— 电机
//
// 【为什么电机要有自己的一份测试而不是照抄泵】泵和电机只共用"启停+旋转"
//   这一小块逻辑(SpinAnimator),画面和语义完全不同:
//     泵 = 圆 + 顶部出口管嘴 + 三叶转子(泵是【输送介质】的)
//     电机 = 圆 + M 字 + 旋转箭头(M 是【定子绕组】,不转)
//   混淆意味着操作员对错设备下指令。所以这文件重点断言:
//     1. 两者行为一致的部分(写回/权限/动画条件)跟泵对齐
//     2. 两者【不】共用同一个注册名(注册表撞名会导致调色板只出一个)
//
// 【为什么本文件不测像素】tst_PumpControl 已经有 paint 不崩的覆盖,
//   电机画面细节靠手测。像素比对在改字体/抗锯齿时会无故变红。
//
// 【声明顺序陷阱】Variable 必须先于 MotorControl 声明(逆序析构会崩),
//   详见 tst_ControlBaseWrite.cpp。
// ============================================================================

#include <QtTest>
#include <QApplication>
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QImage>
#include <QPainter>
#include <QJsonObject>
#include "widgets/MotorControl.h"
#include "widgets/PumpControl.h"
#include "widgets/base/ControlBase.h"
#include "Variable.h"
#include "auth/CurrentUser.h"
#include "auth/Role.h"
#include "auth/User.h"

using namespace qwhmi::widgets;
using namespace qwhmi::auth;

namespace {
User makeUser(const QString& name, Role role) {
    User u;
    u.name = name;
    u.role = role;
    return u;
}
} // namespace

// ★ 见 tst_PumpControl.cpp 的同名函数,三个要点:
//   事件发给 scene(不是控件)、press/release 的 setButtons 要分别设对、
//   结尾 removeItem(栈对象不能交给 scene 析构 delete)
static void clickItem(QGraphicsScene* s, QGraphicsItem* item) {
    const QPointF pt = item->sceneBoundingRect().center();
    s->addItem(item);
    QGraphicsSceneMouseEvent press(QEvent::GraphicsSceneMousePress);
    press.setScenePos(pt);
    press.setButton(Qt::LeftButton);
    press.setButtons(Qt::LeftButton);
    QApplication::sendEvent(s, &press);
    QGraphicsSceneMouseEvent release(QEvent::GraphicsSceneMouseRelease);
    release.setScenePos(pt);
    release.setButton(Qt::LeftButton);
    release.setButtons(Qt::NoButton);
    QApplication::sendEvent(s, &release);
    s->removeItem(item);
}

class TestMotorControl : public QObject {
    Q_OBJECT
private slots:
    void init();
    void cleanup();

    void construct_defaults();
    void isRunning_followsValue();
    void setters_emitSignals();
    void spinPeriodMs_clamps();
    void paint_doesNotCrash();

    // ★ 写回
    void click_startsMotorOnPlc();
    void click_marksVariableDirty();
    void click_twice_stopsMotor();
    void click_unbound_flipsLocally();
    void click_readOnly_doesNotFlip();      // ★★ 最要紧的一条
    void click_viewer_rejected();
    void writeRejected_carriesReason();
    void canWrite_reflectsBindingAndRole();

    // ★ 动画(条件与泵一致)
    void animation_staysOffWhenUnbound();
    void animation_runsWhenBoundAndRunning();
    void animation_stopsWhenValueDropsToZero();
    void animation_angleKeepsAccumulating();
    void runningChanged_tracksTransitionsOnly();

    void remoteChange_updatesRunningState();
    void fromJson_restoresAllFields();
    void toJson_roundTripsThroughFromJson();
    void registeredInPalette();
    // ★ 泵和电机必须是两个独立条目
    void pumpAndMotor_areDistinctTypes();
};

void TestMotorControl::init() {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    CurrentUser::instance().login(makeUser("op1", Role::Operator));
}

void TestMotorControl::cleanup() {
    CurrentUser::instance().logout();
}

void TestMotorControl::construct_defaults() {
    MotorControl m;
    QCOMPARE(m.size(), QSizeF(80, 80));      // 电机是圆的,正方形
    QCOMPARE(m.spinPeriodMs(), 1200);
    QVERIFY(!m.isRunning());
    QVERIFY(!m.canWrite());
    QVERIFY(!m.spinAnimator().isRunning());
    QVERIFY(m.boundingRect().isValid());
}

void TestMotorControl::isRunning_followsValue() {
    MotorControl m;
    m.setValue(0.0);   QVERIFY(!m.isRunning());
    m.setValue(0.49);  QVERIFY(!m.isRunning());
    m.setValue(0.5);   QVERIFY(m.isRunning());   // 阈值与泵/阀门一致
    m.setValue(1.0);   QVERIFY(m.isRunning());
    m.setValue(QVariant(true));   QVERIFY(m.isRunning());
    m.setValue(QVariant(false));  QVERIFY(!m.isRunning());
}

void TestMotorControl::setters_emitSignals() {
    MotorControl m;
    QSignalSpy bc(&m, &MotorControl::bodyColorChanged);
    QSignalSpy rc(&m, &MotorControl::runColorChanged);
    m.setBodyColor(QColor(1, 2, 3));
    QCOMPARE(m.bodyColor(), QColor(1, 2, 3));
    m.setRunColor(QColor(4, 5, 6));
    QCOMPARE(m.runColor(), QColor(4, 5, 6));
    QCOMPARE(bc.count(), 1);
    QCOMPARE(rc.count(), 1);
    m.setBodyColor(QColor(1, 2, 3));
    QCOMPARE(bc.count(), 1);
}

void TestMotorControl::spinPeriodMs_clamps() {
    MotorControl m;
    QSignalSpy spy(&m, &MotorControl::spinPeriodMsChanged);
    m.setSpinPeriodMs(1);
    QCOMPARE(m.spinPeriodMs(), 50);
    QCOMPARE(spy.count(), 1);
    m.setSpinPeriodMs(2000);
    QCOMPARE(m.spinPeriodMs(), 2000);
    QCOMPARE(spy.count(), 2);
    m.setSpinPeriodMs(5);
    m.setSpinPeriodMs(10);       // 也夹到 50,不算变化
    QCOMPARE(spy.count(), 3);
}

void TestMotorControl::paint_doesNotCrash() {
    // 旋转弧只在 running 时画,停机分支是另一条路 —— 两个都要跑
    for (double val : {0.0, 1.0}) {
        for (bool sel : {false, true}) {
            for (qreal angle : {0.0, 200.0}) {
                QGraphicsScene s;
                MotorControl m;
                m.setValue(val);
                m.setSelected(sel);
                s.addItem(&m);
                m.spinAnimator().applyFrame(angle, 0);
                QImage img(100, 100, QImage::Format_ARGB32);
                img.fill(Qt::white);
                QPainter pt(&img);
                s.render(&pt);
                pt.end();
                QVERIFY(!img.isNull());
                s.removeItem(&m);
            }
        }
    }
    // 极小尺寸:半径算出来接近 0
    {
        QGraphicsScene s;
        MotorControl m;
        m.setSize(QSizeF(8, 8));
        m.setValue(1.0);
        s.addItem(&m);
        QImage img(8, 8, QImage::Format_ARGB32);
        img.fill(Qt::white);
        QPainter pt(&img);
        s.render(&pt);
        pt.end();
        QVERIFY(!img.isNull());
        s.removeItem(&m);
    }
}

// ===== 写回 =====

void TestMotorControl::click_startsMotorOnPlc() {
    Variable v("M1", 0x6001, Variable::DataType::Bool, Variable::Access::ReadWrite);
    MotorControl m;
    m.bind(&v);
    QGraphicsScene s;

    QVERIFY(!m.isRunning());
    clickItem(&s, &m);
    QVERIFY2(m.isRunning(), "点一下没启电机");
    QCOMPARE(v.value(), QVariant(true));
}

void TestMotorControl::click_marksVariableDirty() {
    Variable v("M1", 0x6001, Variable::DataType::Bool, Variable::Access::ReadWrite);
    QVERIFY(!v.isDirty());
    MotorControl m;
    m.bind(&v);
    QGraphicsScene s;
    clickItem(&s, &m);
    QVERIFY2(v.isDirty(), "没 markDirty → WriteScheduler 不会把它发给 PLC");
}

void TestMotorControl::click_twice_stopsMotor() {
    Variable v("M1", 0x6001, Variable::DataType::Bool, Variable::Access::ReadWrite);
    MotorControl m;
    m.bind(&v);
    QGraphicsScene s;
    clickItem(&s, &m);
    QVERIFY(m.isRunning());
    clickItem(&s, &m);
    QVERIFY2(!m.isRunning(), "点第二下没停电机");
    QCOMPARE(v.value(), QVariant(false));
}

void TestMotorControl::click_unbound_flipsLocally() {
    MotorControl m;
    QSignalSpy spy(&m, &MotorControl::writeRejected);
    QGraphicsScene s;
    clickItem(&s, &m);
    QVERIFY(m.isRunning());
    QCOMPARE(spy.count(), 0);
    QVERIFY(!m.spinAnimator().isRunning());
}

// ★★ 只读变量上,画面绝不能显示"运行"。
//   电机停了可能整条产线停了 —— 操作员看画面显示运行就认为下游在处理,
//   实际没在处理。这个 bug 演示时看不出来,现场会出事。
void TestMotorControl::click_readOnly_doesNotFlip() {
    Variable v("M2", 0x6002, Variable::DataType::Bool, Variable::Access::ReadOnly);
    MotorControl m;
    m.bind(&v);
    QGraphicsScene s;

    clickItem(&s, &m);
    QVERIFY2(!m.isRunning(), "★ 只读变量上点击绝不能让画面显示为运行");
    QVERIFY(!v.isDirty());
    QVERIFY(!m.canWrite());
    QVERIFY(!m.spinAnimator().isRunning());
}

void TestMotorControl::click_viewer_rejected() {
    CurrentUser::instance().login(makeUser("v1", Role::Viewer));
    Variable v("M1", 0x6001, Variable::DataType::Bool, Variable::Access::ReadWrite);
    MotorControl m;
    m.bind(&v);
    QGraphicsScene s;
    QSignalSpy spy(&m, &MotorControl::writeRejected);
    clickItem(&s, &m);
    QVERIFY(!m.isRunning());
    QVERIFY(!v.isDirty());
    QCOMPARE(spy.count(), 1);
}

void TestMotorControl::writeRejected_carriesReason() {
    Variable v("M2", 0x6002, Variable::DataType::Bool, Variable::Access::ReadOnly);
    MotorControl m;
    m.bind(&v);
    QGraphicsScene s;
    QSignalSpy spy(&m, &MotorControl::writeRejected);
    clickItem(&s, &m);
    QCOMPARE(spy.count(), 1);
    const QString reason = spy.at(0).at(0).toString();
    QVERIFY2(!reason.isEmpty(), "写被拒必须带原因");
    QVERIFY(reason.contains(QStringLiteral("只读")));
}

void TestMotorControl::canWrite_reflectsBindingAndRole() {
    MotorControl m;
    QVERIFY(!m.canWrite());
    Variable ro("M3", 0x6003, Variable::DataType::Bool, Variable::Access::ReadOnly);
    m.bind(&ro);
    QVERIFY(!m.canWrite());
    Variable rw("M4", 0x6004, Variable::DataType::Bool, Variable::Access::ReadWrite);
    m.bind(&rw);
    QVERIFY(m.canWrite());
    CurrentUser::instance().login(makeUser("v1", Role::Viewer));
    QVERIFY(!m.canWrite());
}

// ===== 动画 =====

void TestMotorControl::animation_staysOffWhenUnbound() {
    MotorControl m;
    m.setValue(1.0);
    QVERIFY(m.isRunning());
    QVERIFY2(!m.spinAnimator().isRunning(), "未绑变量的电机不该转 —— 这是编辑器摆件");
}

void TestMotorControl::animation_runsWhenBoundAndRunning() {
    Variable v("M1", 0x6001, Variable::DataType::Bool, Variable::Access::ReadWrite);
    MotorControl m;
    m.bind(&v);
    v.setValue(QVariant(true));
    QVERIFY(m.isRunning());
    QVERIFY2(m.spinAnimator().isRunning(), "绑了变量且运行中,转子该转");
}

void TestMotorControl::animation_stopsWhenValueDropsToZero() {
    Variable v("M1", 0x6001, Variable::DataType::Bool, Variable::Access::ReadWrite);
    MotorControl m;
    m.bind(&v);
    v.setValue(QVariant(true));
    QVERIFY(m.spinAnimator().isRunning());
    v.setValue(QVariant(false));
    QVERIFY2(!m.spinAnimator().isRunning(), "电机停了转子该停");
}

void TestMotorControl::animation_angleKeepsAccumulating() {
    Variable v("M1", 0x6001, Variable::DataType::Bool, Variable::Access::ReadWrite);
    MotorControl m;
    m.bind(&v);
    v.setValue(QVariant(true));
    QSignalSpy spy(&m.spinAnimator(), &SpinAnimator::angleChanged);
    m.spinAnimator().applyFrame(90.0, 0);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(m.spinAngle(), 90.0);
    m.spinAnimator().applyFrame(10.0, 1);
    QCOMPARE(m.spinAngle(), 370.0);   // 跨圈不归零
}

void TestMotorControl::runningChanged_tracksTransitionsOnly() {
    MotorControl m;
    QSignalSpy spy(&m, &MotorControl::runningChanged);
    m.setValue(0.0);
    QCOMPARE(spy.count(), 0);
    m.setValue(1.0);
    QCOMPARE(spy.count(), 1);
    m.setValue(1.0);
    m.setValue(1.0);
    QCOMPARE(spy.count(), 1);
    m.setValue(0.0);
    QCOMPARE(spy.count(), 2);
}

void TestMotorControl::remoteChange_updatesRunningState() {
    Variable v("M1", 0x6001, Variable::DataType::Bool, Variable::Access::ReadWrite);
    MotorControl m;
    m.bind(&v);
    QSignalSpy spy(&m, &MotorControl::runningChanged);
    v.setValue(QVariant(true));
    QVERIFY(m.isRunning());
    QCOMPARE(spy.count(), 1);
    v.setValue(QVariant(false));
    QVERIFY(!m.isRunning());
    QCOMPARE(spy.count(), 2);
}

// ===== 存读 =====

void TestMotorControl::fromJson_restoresAllFields() {
    MotorControl m;
    QJsonObject obj;
    obj["position"] = QJsonObject{{"x", 20.0}, {"y", 40.0}};
    obj["size"]     = QJsonObject{{"width", 100.0}, {"height", 100.0}};
    obj["variableName"] = "plc1.Motor3";
    obj["bodyColor"]    = "#0a0a0a";
    obj["runColor"]     = "#0b0b0b";
    obj["spinPeriodMs"] = 1800;
    m.fromJson(obj);
    QCOMPARE(m.position(), QPointF(20.0, 40.0));
    QCOMPARE(m.size(), QSizeF(100.0, 100.0));
    QCOMPARE(m.variableName(), QString("plc1.Motor3"));
    QCOMPARE(m.bodyColor(), QColor(0x0a, 0x0a, 0x0a));
    QCOMPARE(m.runColor(), QColor(0x0b, 0x0b, 0x0b));
    QCOMPARE(m.spinPeriodMs(), 1800);
}

void TestMotorControl::toJson_roundTripsThroughFromJson() {
    MotorControl src;
    src.setPosition({11.0, 22.0});
    src.setSize({80.0, 80.0});
    src.setVariableName("plc1.Motor9");
    src.setBodyColor(QColor(1, 2, 3));
    src.setRunColor(QColor(4, 5, 6));
    src.setSpinPeriodMs(1500);

    MotorControl dst;
    dst.fromJson(src.toJson());
    QCOMPARE(dst.position(), src.position());
    QCOMPARE(dst.size(), src.size());
    QCOMPARE(dst.variableName(), src.variableName());
    QCOMPARE(dst.bodyColor(), src.bodyColor());
    QCOMPARE(dst.runColor(), src.runColor());
    QCOMPARE(dst.spinPeriodMs(), src.spinPeriodMs());
}

void TestMotorControl::registeredInPalette() {
    QVERIFY(ControlBase::registeredTypeNames().contains("Motor"));
    ControlBase* c = ControlBase::create("Motor");
    QVERIFY(c != nullptr);
    delete c;
}

// ★ 泵和电机是两个不同的设备,调色板上必须是两项。
//   注册表是 file-local 的 QHash,撞名会【静默覆盖】——
//   上一项后注册的赢,调色板里就少一个,没人会注意到。
//
//   ★ 注意 registerControlType 写在【构造函数里】,所以本文件从头到尾
//   没 new 过 PumpControl 的话,"Pump" 压根不在注册表里 —— 上面
//   registeredInPalette() 只断言 "Motor" 正是这个原因。
//   这里手动 new 一个泵来触发它的注册。
void TestMotorControl::pumpAndMotor_areDistinctTypes() {
    delete new PumpControl;        // 触发 registerControlType("Pump")

    const QStringList names = ControlBase::registeredTypeNames();
    QVERIFY2(names.contains("Pump"), "泵没注册上");
    QVERIFY(names.contains("Motor"));

    ControlBase* pump = ControlBase::create("Pump");
    ControlBase* motor = ControlBase::create("Motor");
    QVERIFY(pump);
    QVERIFY(motor);
    QVERIFY2(qobject_cast<PumpControl*>(pump), "create(\"Pump\") 造出来的不是泵");
    QVERIFY(qobject_cast<MotorControl*>(motor));
    QVERIFY2(pump->metaObject() != motor->metaObject(), "泵和电机是同一个类 —— 画错了");
    delete pump;
    delete motor;
}

QTEST_MAIN(TestMotorControl)
#include "tst_MotorControl.moc"
