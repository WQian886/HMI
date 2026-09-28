// ============================================================================
// 阶段 E · E.1c · tst_PumpControl —— 泵
//
// 【测什么】比阀门多两件事,因为泵多了个动画:
//   外观/属性 / isRunning 判定 / ★ 点击写回 / ★ 拒绝后画面不撒谎 /
//   ★ 动画启停条件 / 存读往返 / 调色板注册
//
// 【泵特有的:动画启停条件】转子只在【绑了真实变量 且 正在运行】时转。
//   两个条件缺一不可:
//     · 没绑定 → 它是编辑器里的空摆件,打开工程满屏乱转,像坏了一样
//     · 值 = 0  → 泵停了,叶片还转就是在骗人
//   这也是为什么不需要跟 EditorMainWindow 的 editorMode_ 做模式同步
//   (见 SpinAnimator.h 头注释)—— 绑没绑变量本身就是"是不是编辑器"的信号。
//
// 【声明顺序陷阱】见 tst_ControlBaseWrite.cpp 的说明:
//   Variable 必须【先于】PumpControl 声明,否则逆序析构时控件析构
//   去 disconnect 已释放的 Variable → 崩。
//
// 【鼠标事件的坑】见下方 clickItem 注释。
// ============================================================================

#include <QtTest>
#include <QApplication>
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QImage>
#include <QPainter>
#include <QJsonObject>
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

// ★ 发一次真实的「点一下」(press + release 在同一点)。
//   事件必须发给 scene —— QGraphicsObject 不处理 QEvent::MouseButtonPress
//   (那是 QWidget 的事件类型),直接 sendEvent 给控件会被静默丢掉。
//   press 时 setButtons(LeftButton),release 时 setButtons(NoButton) ——
//   两个都要设对,只设 button 不设 buttons 会走到 else 分支去。
//   结尾 removeItem:栈对象交回自己析构,否则 scene 析构会 delete 它。
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

class TestPumpControl : public QObject {
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
    void click_startsPumpOnPlc();
    void click_twice_stopsPump();          // 泵要能停回去
    void click_marksVariableDirty();
    void click_unbound_flipsLocally();     // 编辑器预览

    // ★ 四道闸
    void click_readOnly_doesNotFlip();     // ★ 最要紧的一条
    void click_viewer_rejected();
    void writeRejected_carriesReason();
    void canWrite_reflectsBindingAndRole();

    // ★ 动画
    void animation_staysOffWhenUnbound();
    void animation_runsWhenBoundAndRunning();
    void animation_stopsWhenValueDropsToZero();
    void animation_angleKeepsAccumulating();
    void runningChanged_tracksTransitionsOnly();

    void remoteChange_updatesRunningState();
    void fromJson_restoresAllFields();
    void toJson_roundTripsThroughFromJson();
    void registeredInPalette();
};

void TestPumpControl::init() {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    CurrentUser::instance().login(makeUser("op1", Role::Operator));
}

void TestPumpControl::cleanup() {
    CurrentUser::instance().logout();
}

void TestPumpControl::construct_defaults() {
    PumpControl p;
    QCOMPARE(p.size(), QSizeF(90, 100));
    QCOMPARE(p.spinPeriodMs(), 1200);
    QVERIFY(!p.isRunning());
    QVERIFY(!p.canWrite());          // 未绑定
    QVERIFY(!p.spinAnimator().isRunning());
    QVERIFY(p.boundingRect().isValid());
}

void TestPumpControl::isRunning_followsValue() {
    PumpControl p;
    p.setValue(0.0);   QVERIFY(!p.isRunning());
    p.setValue(0.49);  QVERIFY(!p.isRunning());
    p.setValue(0.5);   QVERIFY(p.isRunning());   // 阈值 0.5,跟阀门一致
    p.setValue(1.0);   QVERIFY(p.isRunning());
    // 绑 Bool 变量时 writeToPlc 会适配成 QVariant(bool),toDouble() 也得能判
    p.setValue(QVariant(true));   QVERIFY(p.isRunning());
    p.setValue(QVariant(false));  QVERIFY(!p.isRunning());
}

void TestPumpControl::setters_emitSignals() {
    PumpControl p;
    QSignalSpy bc(&p, &PumpControl::bodyColorChanged);
    QSignalSpy rc(&p, &PumpControl::runColorChanged);
    p.setBodyColor(QColor(1, 2, 3));
    QCOMPARE(p.bodyColor(), QColor(1, 2, 3));
    p.setRunColor(QColor(4, 5, 6));
    QCOMPARE(p.runColor(), QColor(4, 5, 6));
    QCOMPARE(bc.count(), 1);
    QCOMPARE(rc.count(), 1);
    p.setBodyColor(QColor(1, 2, 3));   // 同值不重复发
    QCOMPARE(bc.count(), 1);
}

void TestPumpControl::spinPeriodMs_clamps() {
    PumpControl p;
    QSignalSpy spy(&p, &PumpControl::spinPeriodMsChanged);
    p.setSpinPeriodMs(1);
    QCOMPARE(p.spinPeriodMs(), 50);     // 一圈 < 50ms 看着是频闪
    QCOMPARE(spy.count(), 1);
    p.setSpinPeriodMs(2000);
    QCOMPARE(p.spinPeriodMs(), 2000);
    QCOMPARE(spy.count(), 2);
    // 被夹到同一个值不算变化
    p.setSpinPeriodMs(5);
    QCOMPARE(spy.count(), 3);
    p.setSpinPeriodMs(10);              // 也夹到 50
    QCOMPARE(spy.count(), 3);
}

void TestPumpControl::paint_doesNotCrash() {
    // 未选/选中 × 停/转,再叠一个旋转到非 0 角度的
    // (角度 0 和角度 77 走的是不同的 rotate 数值,分开跑)
    for (double val : {0.0, 1.0}) {
        for (bool sel : {false, true}) {
            for (qreal angle : {0.0, 77.0}) {
                QGraphicsScene s;
                PumpControl p;
                p.setValue(val);
                p.setSelected(sel);
                s.addItem(&p);
                p.spinAnimator().applyFrame(angle, 0);
                QImage img(110, 120, QImage::Format_ARGB32);
                img.fill(Qt::white);
                QPainter pt(&img);
                s.render(&pt);
                pt.end();
                QVERIFY(!img.isNull());
                s.removeItem(&p);
            }
        }
    }
    // 极小尺寸:泵壳半径算出来会接近 0,别在这里除零/画出负矩形
    {
        QGraphicsScene s;
        PumpControl p;
        p.setSize(QSizeF(10, 10));
        p.setValue(1.0);
        s.addItem(&p);
        QImage img(10, 10, QImage::Format_ARGB32);
        img.fill(Qt::white);
        QPainter pt(&img);
        s.render(&pt);
        pt.end();
        QVERIFY(!img.isNull());
        s.removeItem(&p);
    }
}

// ===== 写回 =====

void TestPumpControl::click_startsPumpOnPlc() {
    Variable v("Pump1", 0x5001, Variable::DataType::Bool, Variable::Access::ReadWrite);
    PumpControl p;
    p.bind(&v);
    QGraphicsScene s;

    QVERIFY(!p.isRunning());
    clickItem(&s, &p);

    QVERIFY2(p.isRunning(), "点一下没启泵");
    QCOMPARE(v.value(), QVariant(true));
}

// ★ 为什么这条不能省:markDirty() 是 WriteScheduler 唯一的发送开关
//   (WriteScheduler 扫 isDirty())。漏了它,值改了、界面变了,
//   但永远发不到 PLC —— 泵看着转了实际没动。本控件最容易犯的错
void TestPumpControl::click_marksVariableDirty() {
    Variable v("Pump1", 0x5001, Variable::DataType::Bool, Variable::Access::ReadWrite);
    QVERIFY(!v.isDirty());
    PumpControl p;
    p.bind(&v);
    QGraphicsScene s;

    clickItem(&s, &p);
    QVERIFY2(v.isDirty(), "没 markDirty → WriteScheduler 不会把它发给 PLC");
}

// ★ 泵要能停回去。只测"能启"的泵,现场会做个只能启不能停的 ——
//   那比不能操作的还危险
void TestPumpControl::click_twice_stopsPump() {
    Variable v("Pump1", 0x5001, Variable::DataType::Bool, Variable::Access::ReadWrite);
    PumpControl p;
    p.bind(&v);
    QGraphicsScene s;

    clickItem(&s, &p);
    QVERIFY(p.isRunning());
    clickItem(&s, &p);
    QVERIFY2(!p.isRunning(), "点第二下没停泵");
    QCOMPARE(v.value(), QVariant(false));
}

void TestPumpControl::click_unbound_flipsLocally() {
    // 编辑器预览:没绑变量也得能点得动,否则作者以为控件坏了
    PumpControl p;
    QSignalSpy spy(&p, &PumpControl::writeRejected);
    QGraphicsScene s;
    clickItem(&s, &p);
    QVERIFY(p.isRunning());
    QCOMPARE(spy.count(), 0);   // 未绑定不算"被拒",是预览
    // 但仍然不该转 —— 它是摆件
    QVERIFY(!p.spinAnimator().isRunning());
}

// ===== 四道闸 =====

// ★★ 本文件最要紧的一条。
//   只读变量上,画面绝不能显示"运行"。泵背后是整条管路 ——
//   操作员看画面显示运行就认为下游有压,实际泵没动,后果比阀门那条还严重。
//   这个 bug 在演示时完全看不出来,现场会出事。
void TestPumpControl::click_readOnly_doesNotFlip() {
    Variable v("Pump2", 0x5002, Variable::DataType::Bool, Variable::Access::ReadOnly);
    PumpControl p;
    p.bind(&v);
    QGraphicsScene s;

    clickItem(&s, &p);

    QVERIFY2(!p.isRunning(), "★ 只读变量上点击绝不能让画面显示为运行 —— PLC 侧纹丝不动");
    QVERIFY(!v.isDirty());
    QVERIFY(!p.canWrite());
    QVERIFY(!p.spinAnimator().isRunning());
}

void TestPumpControl::click_viewer_rejected() {
    CurrentUser::instance().login(makeUser("v1", Role::Viewer));
    Variable v("Pump1", 0x5001, Variable::DataType::Bool, Variable::Access::ReadWrite);
    PumpControl p;
    p.bind(&v);
    QGraphicsScene s;

    QSignalSpy spy(&p, &PumpControl::writeRejected);
    clickItem(&s, &p);

    QVERIFY(!p.isRunning());
    QVERIFY(!v.isDirty());
    QCOMPARE(spy.count(), 1);
}

void TestPumpControl::writeRejected_carriesReason() {
    // 拒绝时必须给上层一个能弹给用户看的中文原因
    Variable v("Pump2", 0x5002, Variable::DataType::Bool, Variable::Access::ReadOnly);
    PumpControl p;
    p.bind(&v);
    QGraphicsScene s;

    QSignalSpy spy(&p, &PumpControl::writeRejected);
    clickItem(&s, &p);
    QCOMPARE(spy.count(), 1);
    const QString reason = spy.at(0).at(0).toString();
    QVERIFY2(!reason.isEmpty(), "写被拒必须带原因,否则上层弹不出有用提示");
    QVERIFY(reason.contains(QStringLiteral("只读")));
}

void TestPumpControl::canWrite_reflectsBindingAndRole() {
    PumpControl p;
    QVERIFY(!p.canWrite());
    Variable ro("P2", 0x5003, Variable::DataType::Bool, Variable::Access::ReadOnly);
    p.bind(&ro);
    QVERIFY(!p.canWrite());
    Variable rw("P1", 0x5004, Variable::DataType::Bool, Variable::Access::ReadWrite);
    p.bind(&rw);
    QVERIFY(p.canWrite());
    CurrentUser::instance().login(makeUser("v1", Role::Viewer));
    QVERIFY(!p.canWrite());
}

// ===== 动画 =====

// ★ 编辑器里的泵是没绑变量的空摆件,它要是自己转起来,
//   打开工程就满屏乱转,像坏了一样
void TestPumpControl::animation_staysOffWhenUnbound() {
    PumpControl p;
    p.setValue(1.0);
    QVERIFY(p.isRunning());
    QVERIFY2(!p.spinAnimator().isRunning(), "未绑变量的泵不该转 —— 这是编辑器摆件");
}

void TestPumpControl::animation_runsWhenBoundAndRunning() {
    Variable v("Pump1", 0x5001, Variable::DataType::Bool, Variable::Access::ReadWrite);
    PumpControl p;
    p.bind(&v);
    v.setValue(QVariant(true));
    QVERIFY(p.isRunning());
    QVERIFY2(p.spinAnimator().isRunning(), "绑了变量且运行中,转子该转");
}

void TestPumpControl::animation_stopsWhenValueDropsToZero() {
    Variable v("Pump1", 0x5001, Variable::DataType::Bool, Variable::Access::ReadWrite);
    PumpControl p;
    p.bind(&v);
    v.setValue(QVariant(true));
    QVERIFY(p.spinAnimator().isRunning());

    v.setValue(QVariant(false));
    QVERIFY2(!p.spinAnimator().isRunning(), "泵停了转子该停");
    QVERIFY(!p.isRunning());
}

// ★ 泵不是只读显示器:远程启泵也必须转起来。
//   画面显示"运行"但叶片不转,操作员会以为画面卡了/PLC 断了,
//   从而重复点击启停 —— 点多了会误停正在运行的泵
void TestPumpControl::animation_angleKeepsAccumulating() {
    Variable v("Pump1", 0x5001, Variable::DataType::Bool, Variable::Access::ReadWrite);
    PumpControl p;
    p.bind(&v);
    v.setValue(QVariant(true));

    QSignalSpy spy(&p.spinAnimator(), &SpinAnimator::angleChanged);
    p.spinAnimator().applyFrame(90.0, 0);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(p.spinAngle(), 90.0);

    // 跨圈:角度必须接着涨,不能跳回 0(否则叶片每圈闪一下)
    p.spinAnimator().applyFrame(10.0, 1);
    QCOMPARE(p.spinAngle(), 370.0);
    QVERIFY(p.spinAngle() > 360.0);
}

// ★ 远程连续回读同一个值(Poll 循环每秒一次)是常态。
//   每次都发 runningChanged 会让下游(动画 start/stop、报警联动)被无谓唤醒,
//   而 SpinAnimator.setRunning 每次都调 —— 不幂等的话画面每秒"顿"一下
void TestPumpControl::runningChanged_tracksTransitionsOnly() {
    PumpControl p;
    QSignalSpy spy(&p, &PumpControl::runningChanged);
    p.setValue(0.0);
    QCOMPARE(spy.count(), 0);
    p.setValue(1.0);
    QCOMPARE(spy.count(), 1);
    p.setValue(1.0);
    p.setValue(1.0);
    QCOMPARE(spy.count(), 1);      // 同值不重复发
    p.setValue(0.0);
    QCOMPARE(spy.count(), 2);
}

void TestPumpControl::remoteChange_updatesRunningState() {
    Variable v("Pump1", 0x5001, Variable::DataType::Bool, Variable::Access::ReadWrite);
    PumpControl p;
    p.bind(&v);
    QSignalSpy spy(&p, &PumpControl::runningChanged);

    v.setValue(QVariant(true));
    QVERIFY(p.isRunning());
    QCOMPARE(spy.count(), 1);
    v.setValue(QVariant(false));
    QVERIFY(!p.isRunning());
    QCOMPARE(spy.count(), 2);
}

// ===== 存读 =====

void TestPumpControl::fromJson_restoresAllFields() {
    PumpControl p;
    QJsonObject obj;
    obj["position"] = QJsonObject{{"x", 20.0}, {"y", 40.0}};
    obj["size"]     = QJsonObject{{"width", 120.0}, {"height", 130.0}};
    obj["variableName"] = "plc1.Pump7";
    obj["bodyColor"]    = "#0a0a0a";
    obj["runColor"]     = "#0b0b0b";
    obj["spinPeriodMs"] = 1800;
    p.fromJson(obj);
    QCOMPARE(p.position(), QPointF(20.0, 40.0));
    QCOMPARE(p.size(), QSizeF(120.0, 130.0));
    QCOMPARE(p.variableName(), QString("plc1.Pump7"));
    QCOMPARE(p.bodyColor(), QColor(0x0a, 0x0a, 0x0a));
    QCOMPARE(p.runColor(), QColor(0x0b, 0x0b, 0x0b));
    QCOMPARE(p.spinPeriodMs(), 1800);
}

void TestPumpControl::toJson_roundTripsThroughFromJson() {
    PumpControl src;
    src.setPosition({11.0, 22.0});
    src.setSize({100.0, 110.0});
    src.setVariableName("plc1.Pump9");
    src.setBodyColor(QColor(1, 2, 3));
    src.setRunColor(QColor(4, 5, 6));
    src.setSpinPeriodMs(1500);

    PumpControl dst;
    dst.fromJson(src.toJson());
    QCOMPARE(dst.position(), src.position());
    QCOMPARE(dst.size(), src.size());
    QCOMPARE(dst.variableName(), src.variableName());
    QCOMPARE(dst.bodyColor(), src.bodyColor());
    QCOMPARE(dst.runColor(), src.runColor());
    QCOMPARE(dst.spinPeriodMs(), src.spinPeriodMs());
}

void TestPumpControl::registeredInPalette() {
    QVERIFY(ControlBase::registeredTypeNames().contains("Pump"));
    ControlBase* c = ControlBase::create("Pump");
    QVERIFY(c != nullptr);
    delete c;
}

QTEST_MAIN(TestPumpControl)
#include "tst_PumpControl.moc"
