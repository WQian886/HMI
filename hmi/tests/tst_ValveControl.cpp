// ============================================================================
// 阶段 E · E.1c · tst_ValveControl —— 阀门(第一个能操作真实设备的控件)
//
// 【测什么】
//   外观/属性 / isOpen 判定 / ★ 点击真写回 PLC / ★ 四道闸在阀门上的表现 /
//   ★ 远程改值也要反映到画面 / 存读往返 / 调色板注册
//
// 【为什么点击测试是这里最要紧的】E.1c 之前的 17 个控件全是只读显示器。
//   阀门是第一个「点下去设备真的要动」的控件,也就是说 writeToPlc
//   第一次真正对上生产行为。ToggleControl 的写回测试(tst_ControlBaseWrite)
//   验证的是基类逻辑;这里验证的是【写回路径 + 拒绝路径 + 拒绝后的画面
//   不得撒谎】三者串起来仍成立。
//
// 【声明顺序陷阱】见 tst_ControlBaseWrite.cpp 的说明:
//   Variable 必须【先于】ValveControl 声明,否则逆序析构时控件析构
//   去 disconnect 已释放的 Variable → 崩。
// ============================================================================

#include <QtTest>
#include <QApplication>
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QImage>
#include <QPainter>
#include <QJsonObject>
#include "widgets/ValveControl.h"
#include "widgets/base/ControlBase.h"
#include "Variable.h"
#include "auth/CurrentUser.h"
#include "auth/Permission.h"
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
//   详见 tst_ControlBaseWrite.cpp 的 clickItem 注释。
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
    s->removeItem(item);   // ★ 栈对象:交回自己析构,否则 scene 析构会 delete 它
}

class TestValveControl : public QObject {
    Q_OBJECT
private slots:
    void init();
    void cleanup();

    void construct_defaults();
    void isOpen_followsValue();
    void setters_emitSignals();
    void paint_doesNotCrash();

    // ★ 写回
    void click_writesTrueToPlc();
    void click_twice_writesBackFalse();   // 阀门要能关回去
    void click_emitsOpenedChanged();
    void writeRejected_signalCarriesReason();

    // ★ 四道闸
    void click_unbound_togglesLocally();      // 编辑器预览
    void click_readOnly_doesNotFlip();         // ★ 阀门显示开着但没动 = 出事
    void click_viewer_rejected();
    void canWrite_reflectsBindingAndRole();

    // ★ 远程改值
    void remoteChange_updatesOpenState();

    void fromJson_restoresAllFields();
    void toJson_roundTripsThroughFromJson();
    void registeredInPalette();
};

void TestValveControl::init() {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    CurrentUser::instance().login(makeUser("op1", Role::Operator));
}

void TestValveControl::cleanup() {
    CurrentUser::instance().logout();
}

void TestValveControl::construct_defaults() {
    ValveControl v;
    QCOMPARE(v.size(), QSizeF(64, 64));              // 正方形,对顶三角才像阀门
    QCOMPARE(v.openColor(), QColor(40, 170, 90));
    QCOMPARE(v.closedColor(), QColor(190, 60, 60));
    QVERIFY(!v.isOpen());                            // 默认关
    QVERIFY(!v.canWrite());                          // 未绑定
}

void TestValveControl::isOpen_followsValue() {
    ValveControl v;
    v.setValue(1.0);   QVERIFY(v.isOpen());
    v.setValue(0.0);   QVERIFY(!v.isOpen());
    v.setValue(0.49);  QVERIFY(!v.isOpen());   // 阈值 0.5
    v.setValue(0.5);   QVERIFY(v.isOpen());
    // 绑 Bool 变量时 writeToPlc 适配成 QVariant(bool),toDouble() 也要能判
    v.setValue(QVariant(true));   QVERIFY(v.isOpen());
    v.setValue(QVariant(false));  QVERIFY(!v.isOpen());
}

void TestValveControl::setters_emitSignals() {
    ValveControl v;
    QSignalSpy oc(&v, &ValveControl::openColorChanged);
    QSignalSpy cc(&v, &ValveControl::closedColorChanged);
    v.setOpenColor(QColor(1, 2, 3));
    v.setClosedColor(QColor(4, 5, 6));
    QCOMPARE(oc.count(), 1);
    QCOMPARE(cc.count(), 1);
    v.setOpenColor(QColor(1, 2, 3));
    QCOMPARE(oc.count(), 1);   // 同值不重复发
}

void TestValveControl::paint_doesNotCrash() {
    for (double val : {0.0, 1.0}) {
        for (bool sel : {false, true}) {
            for (int roleIdx = 0; roleIdx < 2; ++roleIdx) {
                if (roleIdx == 1) CurrentUser::instance().login(makeUser("v1", Role::Viewer));
                QGraphicsScene s;
                ValveControl v;
                v.setValue(val);
                v.setSelected(sel);
                s.addItem(&v);
                QImage img(100, 100, QImage::Format_ARGB32);
                img.fill(Qt::white);
                QPainter p(&img);
                s.render(&p);
                p.end();
                QVERIFY(!img.isNull());
                s.removeItem(&v);
            }
        }
    }
}

// ===== 写回 =====

void TestValveControl::click_writesTrueToPlc() {
    // ★ 核心:点一下,PLC 那边真的收到待写标记
    Variable plcValve("Valve1", 0x4001, Variable::DataType::Bool, Variable::Access::ReadWrite);
    ValveControl v;
    v.bind(&plcValve);

    QGraphicsScene s;
    clickItem(&s, &v);

    QVERIFY2(plcValve.isDirty(), "★ 点击阀门必须真写回 PLC");
    QCOMPARE(plcValve.value(), QVariant(true));
    QVERIFY(v.isOpen());
}

void TestValveControl::click_twice_writesBackFalse() {
    // ★ 阀门要能关回去。只测"能开"不测"能关"的控件,
    //   现场会做个只能开的阀门 —— 那比不能操作的还危险
    Variable plcValve("Valve1", 0x4001, Variable::DataType::Bool, Variable::Access::ReadWrite);
    ValveControl v;
    v.bind(&plcValve);

    QGraphicsScene s;
    clickItem(&s, &v);
    QVERIFY(v.isOpen());
    clickItem(&s, &v);          // 再点一次
    QVERIFY(!v.isOpen());
    QCOMPARE(plcValve.value(), QVariant(false));
}

void TestValveControl::click_emitsOpenedChanged() {
    // openedChanged 跟的是"开合态变化"不是"点击" —— 测试要能区分
    Variable plcValve("Valve1", 0x4001, Variable::DataType::Bool, Variable::Access::ReadWrite);
    ValveControl v;
    v.bind(&plcValve);

    QSignalSpy spy(&v, &ValveControl::openedChanged);
    QGraphicsScene s;
    clickItem(&s, &v);
    QCOMPARE(spy.count(), 1);
    // 同一开合态下再点回同一状态不应重复发
    clickItem(&s, &v);
    clickItem(&s, &v);
    QCOMPARE(spy.count(), 3);   // 开→关→开,每次都变了
}

void TestValveControl::writeRejected_signalCarriesReason() {
    // 拒绝时必须给上层一个能弹给用户看的中文原因,
    // 否则操作员点了没反应只能猜
    Variable roValve("Valve2", 0x4002, Variable::DataType::Bool, Variable::Access::ReadOnly);
    ValveControl v;
    v.bind(&roValve);

    QSignalSpy spy(&v, &ValveControl::writeRejected);
    QGraphicsScene s;
    clickItem(&s, &v);

    QCOMPARE(spy.count(), 1);
    const QString reason = spy.at(0).at(0).toString();
    QVERIFY2(!reason.isEmpty(), "写被拒必须带原因,否则上层弹不出有用提示");
    QVERIFY(reason.contains(QStringLiteral("只读")));
}

// ===== 四道闸 =====

void TestValveControl::click_unbound_togglesLocally() {
    // 编辑器预览:没绑变量也得能点得动,否则作者以为控件坏了
    ValveControl v;
    QSignalSpy spy(&v, &ValveControl::writeRejected);
    QGraphicsScene s;
    clickItem(&s, &v);
    QVERIFY(v.isOpen());
    QCOMPARE(spy.count(), 0);   // 未绑定不算"被拒",是预览
}

void TestValveControl::click_readOnly_doesNotFlip() {
    // ★ 最关键的一条:只读变量上,画面绝不能显示"开"。
    //   阀门背后是高压管路 —— 操作员看画面显示"开"就认为管路通了,
    //   实际阀门还关着,后果比 ToggleControl 那条严重得多
    Variable roValve("Valve2", 0x4002, Variable::DataType::Bool, Variable::Access::ReadOnly);
    ValveControl v;
    v.bind(&roValve);

    QGraphicsScene s;
    clickItem(&s, &v);

    QVERIFY2(!v.isOpen(), "★ 只读变量上点击绝不能让画面显示为开 —— PLC 侧纹丝不动");
    QVERIFY(!roValve.isDirty());
    QVERIFY(!v.canWrite());
}

void TestValveControl::click_viewer_rejected() {
    CurrentUser::instance().login(makeUser("v1", Role::Viewer));
    Variable plcValve("Valve1", 0x4001, Variable::DataType::Bool, Variable::Access::ReadWrite);
    ValveControl v;
    v.bind(&plcValve);

    QSignalSpy spy(&v, &ValveControl::writeRejected);
    QGraphicsScene s;
    clickItem(&s, &v);

    QVERIFY(!v.isOpen());
    QVERIFY(!plcValve.isDirty());
    QCOMPARE(spy.count(), 1);
}

void TestValveControl::canWrite_reflectsBindingAndRole() {
    ValveControl v;
    QVERIFY(!v.canWrite());
    Variable ro("V2", 0x4003, Variable::DataType::Bool, Variable::Access::ReadOnly);
    v.bind(&ro);
    QVERIFY(!v.canWrite());
    Variable rw("V1", 0x4004, Variable::DataType::Bool, Variable::Access::ReadWrite);
    v.bind(&rw);
    QVERIFY(v.canWrite());
    CurrentUser::instance().login(makeUser("v1", Role::Viewer));
    QVERIFY(!v.canWrite());
}

// ===== 远程改值 =====

// ★ 远程改值也必须反映到画面。PLC 侧有人远程关了阀门,
//   画面还显示"开"的话操作员据此判断管路状态 —— 事故
void TestValveControl::remoteChange_updatesOpenState() {
    Variable plcValve("Valve1", 0x4001, Variable::DataType::Bool, Variable::Access::ReadWrite);
    ValveControl v;
    v.bind(&plcValve);

    QSignalSpy spy(&v, &ValveControl::openedChanged);
    // PLC 回读路径:Variable::setValue → valueChanged → onVariableValueChanged
    plcValve.setValue(QVariant(true));
    QVERIFY(v.isOpen());
    QCOMPARE(spy.count(), 1);

    plcValve.setValue(QVariant(false));
    QVERIFY(!v.isOpen());
    QCOMPARE(spy.count(), 2);
}

// ===== 存读 =====

void TestValveControl::fromJson_restoresAllFields() {
    ValveControl v;
    QJsonObject obj;
    obj["position"] = QJsonObject{{"x", 20.0}, {"y", 40.0}};
    obj["size"]     = QJsonObject{{"width", 80.0}, {"height", 80.0}};
    obj["variableName"] = "plc1.Valve3";
    obj["openColor"]   = "#0a0a0a";
    obj["closedColor"] = "#0b0b0b";
    v.fromJson(obj);
    QCOMPARE(v.position(), QPointF(20.0, 40.0));
    QCOMPARE(v.size(), QSizeF(80.0, 80.0));
    QCOMPARE(v.variableName(), QString("plc1.Valve3"));
    QCOMPARE(v.openColor(), QColor(0x0a, 0x0a, 0x0a));
    QCOMPARE(v.closedColor(), QColor(0x0b, 0x0b, 0x0b));
}

void TestValveControl::toJson_roundTripsThroughFromJson() {
    ValveControl src;
    src.setPosition({11.0, 22.0});
    src.setSize({72.0, 72.0});
    src.setVariableName("plc1.Valve9");
    src.setOpenColor(QColor(1, 2, 3));
    src.setClosedColor(QColor(4, 5, 6));
    src.setValue(1.0);

    ValveControl dst;
    dst.fromJson(src.toJson());
    QCOMPARE(dst.position(), src.position());
    QCOMPARE(dst.size(), src.size());
    QCOMPARE(dst.variableName(), src.variableName());
    QCOMPARE(dst.openColor(), src.openColor());
    QCOMPARE(dst.closedColor(), src.closedColor());
    QCOMPARE(dst.value(), src.value());
}

void TestValveControl::registeredInPalette() {
    QVERIFY(ControlBase::registeredTypeNames().contains("Valve"));
    ControlBase* c = ControlBase::create("Valve");
    QVERIFY(c != nullptr);
    delete c;
}

QTEST_MAIN(TestValveControl)
#include "tst_ValveControl.moc"
