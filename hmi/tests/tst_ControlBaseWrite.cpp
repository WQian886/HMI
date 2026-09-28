// ============================================================================
// 阶段 E · E.1c · tst_ControlBaseWrite —— UI → PLC 反向写通道
//
// 【背景】E.1c 开工前发现:ControlBase 是单向的。onVariableValueChanged 只做
//   「PLC 推过来 → 更新显示」,反方向推不出去。ToggleControl 点一下只翻本地
//   value_ + emit toggled,而全仓库没有任何人接那个信号 —— 17 个控件全是
//   只读显示器,一个都写不回 PLC。对阀门/泵/电机是致命的。
//
// 【测什么】writeToPlc 的四道闸 + 成功路径的副作用:
//   闸 1 未绑定变量    → 拒,不碰本地值
//   闸 2 只读变量      → 拒(Sensor 数据不该被 UI 改)
//   闸 3 权限不足      → 拒(Viewer 没 WriteVariable)
//   闸 4 Variable 拒收 → 拒(类型/范围),且【绝不 markDirty】
//   成功               → setValue + markDirty + 审计留痕
//
// 【为什么单独测 markDirty】markDirty 是让 WriteScheduler 真正动起来的开关
//   (WriteScheduler.cpp:51-52 每帧扫 isDirty)。光 setValue 不写 PLC。
//   如果哪天有人把 markDirty 删了,画面照样变、测试照样过大部分用例,
//   但 PLC 上的阀门永远不会动 —— 所以这里直接把 isDirty 断言死。
//
// 【事故对照】ActionManager.cpp:75-86 修过一个「联动撒谎上报」:
//   setValue 被 Rejected 却照样 markDirty + 记审计「已执行」,
//   PLC 实际啥也没收到,事故复盘时唯一证据是假的。
//   本测试的 rejectsByRange_notMarkDirty 就是防这个复发。
//
// 关联:ControlBase::writeToPlc / Variable.h:113-141 / WriteScheduler.cpp:51
// ============================================================================

#include <QtTest>
#include <QSignalSpy>
#include <QApplication>
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QImage>
#include <QPainter>
#include "widgets/ToggleControl.h"
#include "widgets/base/ControlBase.h"
#include "Variable.h"
#include "auth/AuditLog.h"
#include "auth/CurrentUser.h"
#include "auth/Permission.h"
#include "auth/User.h"

using namespace qwhmi::widgets;
using namespace qwhmi::auth;

namespace {
// 构造测试用 User(只填 name + role,同 tst_UIPermission.cpp 的做法)
User makeUser(const QString& name, Role role) {
    User u;
    u.name = name;
    u.role = role;
    return u;
}
} // namespace

class TestControlBaseWrite : public QObject {
    Q_OBJECT
private slots:
    void init();       // 每个用例前:登录 Operator(有 WriteVariable)+ 清审计文件
    void cleanup();    // logout 复位

    // ===== 成功路径 =====
    void writeToPlc_marksDirtyAndSetsValue();
    void writeToPlc_emitsValueChangedOnce();
    void writeToPlc_refreshesLocalDisplayImmediately();   // 乐观 UI:不等 PLC 回读
    void writeToPlc_coercesValueToVariableType();        // 控件传 double,变量要 Bool → 必须适配

    // ===== 四道闸 =====
    void rejectsWhenUnbound();                           // 闸 1
    void rejectsWhenReadOnly();                           // 闸 2
    void rejectsWhenNoPermission();                       // 闸 3(Viewer)
    void rejectsByRange_notMarkDirty();                   // 闸 4 —— 最关键的一条

    // ===== 闸 4 的另一半:Rejected 后本地不许撒谎 =====
    void rejectsByRange_localValueUnchanged();

    // ===== canWrite 谓词 =====
    void canWrite_reflectsBindAccessAndRole();

    // ===== ToggleControl 断线修复的回归 =====
    void toggle_writesBackWhenBound();                    // 绑定后点击真写 PLC
    void toggle_unbound_stillTogglesLocally();            // 编辑器预览不受影响
    void toggle_readOnly_doesNotFlipLocally();            // 真设备拒绝时不能装成功

private:
    // 清理审计文件,避免单例句柄写已删除文件(同 AuditLog.h resetForTesting 的用法)
    void clearAudit() { AuditLog::instance().resetForTesting(); }

    // ★ 声明顺序陷阱(本测试最初就踩了,崩在 ~ToggleControl → unbind):
    //   栈对象逆序析构 —— 后声明��先死。如果先声明控件 t 再声明 Variable v,
    //   析构时 v 先没,t 的析构函数再调 unbind() 去 disconnect 悬空指针 → 崩。
    //   所以 Variable 必须【先于】ControlBase 声明。
    //   真实运行时 Variable 由 VariableManager 持有,生命周期长于场景里的控件,
    //   不存在这个顺序问题;但测试里要自己保证。
};

// ★ 发一次真实的「点一下」(press + release 在同一点)
//
// 【踩过的坑,别再改回去】
//  1. 事件必须发给【scene】,不能直接发给控件。QGraphicsObject 不处理
//     QEvent::MouseButtonPress/Release(那是 QWidget 的事件类型)——
//     QGraphicsScene 才认 GraphicsSceneMousePress/Release,并由它的
//     eventFilter 派发到 item。直接 QApplication::sendEvent(&t, ...)
//     事件被静默丢掉,writeToPlc 一次都不会被调。
//     正确写法见 tst_EditorMainWindow.cpp:571-586。
//  2. setButton + setButtons 两个都得设。只设 button 的话 scene 认为
//     按键"还按着",mouseReleaseEvent 收不到。
//  3. scene.addItem(栈对象) 之后 scene 析构会 delete 它 —— 栈对象用完
//     必须 removeItem,否则函数返回时后声明的 scene 先析构 → delete 栈上对象
//     → 紧接着析构 t 时崩。这个曾把测试进程直接挂死,不是断言失败。
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

    s->removeItem(item);   // ★ scene 不再拥有它,交回栈上析构
}

void TestControlBaseWrite::init() {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    // Operator 有 WriteVariable(Viewer 没有)—— 默认身份取"能写"的,
    // 拒写用例再单独切到 Viewer
    CurrentUser::instance().login(makeUser("op1", Role::Operator));
    clearAudit();
}

void TestControlBaseWrite::cleanup() {
    CurrentUser::instance().logout();
}

// ===== 成功路径 =====

void TestControlBaseWrite::writeToPlc_marksDirtyAndSetsValue() {
    // ★ 类型必须精确匹配 —— Variable::setValue 的 matchesDataType(Variable.cpp:35-45)
    //   拿 v.typeId() 跟声明的 DataType 逐个比:Bool 要 QMetaType::Bool、
    //   Float 要 QMetaType::Float,double 一律拒。
    //   控件传上来的值由 ControlBase 按 bound_->dataType() 转型后才安全。
    Variable v("Valve1", 0x0001, Variable::DataType::Bool, Variable::Access::ReadWrite);
    ToggleControl t;
    t.bind(&v);

    QVERIFY(!v.isDirty());          // 初始干净
    QVERIFY(t.writeToPlc(true));

    // ★ markDirty 是 WriteScheduler 的开关,必须断言死
    QVERIFY(v.isDirty());
    QCOMPARE(v.value(), QVariant(true));
    QCOMPARE(t.value(), QVariant(true));   // 本地也刷了
}

void TestControlBaseWrite::writeToPlc_emitsValueChangedOnce() {
    Variable v("Valve1", 0x0001, Variable::DataType::Bool, Variable::Access::ReadWrite);
    ToggleControl t;
    t.bind(&v);
    t.setValue(false);

    QSignalSpy spy(&t, &ToggleControl::valueChanged);
    t.writeToPlc(true);
    QCOMPARE(spy.count(), 1);
}

void TestControlBaseWrite::writeToPlc_refreshesLocalDisplayImmediately() {
    // 乐观 UI:操作员点了要马上看到反应,不等 PLC 回读(100ms tick)
    Variable v("Valve1", 0x0001, Variable::DataType::Bool, Variable::Access::ReadWrite);
    ToggleControl t;
    t.bind(&v);

    t.writeToPlc(true);
    // 关键:不发任何 PLC 侧信号,本地已经是 true
    QCOMPARE(t.value(), QVariant(true));
    QVERIFY(v.isDirty());   // 脏着,等 WriteScheduler 去发
}

// ===== 类型适配(控件传 double / 变量要 Bool 的场景)=====

void TestControlBaseWrite::writeToPlc_coercesValueToVariableType() {
    // ★ 本测试最初就是红的:控件内部用 double 算(value().toDouble() < 0.5),
    //   但 Variable::setValue 严格按 typeId 匹配,double 写不进 Bool 变量。
    //   结果:每个 Bool 变量的开关点了都被静默拒绝 —— 画面不动、PLC 不动,
    //   日志里只有一行 type mismatch。写回通道形同虚设。
    //   writeToPlc 必须按 bound_->dataType() 把值转型后再交给 Variable。
    Variable v("Valve1", 0x0001, Variable::DataType::Bool, Variable::Access::ReadWrite);
    ToggleControl t;
    t.bind(&v);

    QVERIFY2(t.writeToPlc(1.0), "传 double 给 Bool 变量应被适配,而不是静默拒绝");
    QCOMPARE(v.value(), QVariant(true));
    QVERIFY(t.writeToPlc(0.0));
    QCOMPARE(v.value(), QVariant(false));
}

// ===== 四道闸 =====

void TestControlBaseWrite::rejectsWhenUnbound() {
    // 闸 1:编辑器里的空摆件,没有 PLC 对象可写
    ToggleControl t;
    t.setValue(false);
    QVERIFY(!t.writeToPlc(true));
    QCOMPARE(t.value(), QVariant(false));   // 本地不许变
    QVERIFY(!t.canWrite());
}

void TestControlBaseWrite::rejectsWhenReadOnly() {
    // 闸 2:传感器数据。跟权限闸并列不合并 —— 一个是「PLC 不许写」,
    // 一个是「这个人没资格写」,拒绝理由不同,排查时要能分开
    Variable v("Temp", 0x0002, Variable::DataType::Float, Variable::Access::ReadOnly);
    ToggleControl t;
    t.bind(&v);

    QVERIFY(!t.writeToPlc(42.0f));
    QVERIFY(!v.isDirty());
    QCOMPARE(v.value(), t.value());   // 没写进去
    QVERIFY(!t.canWrite());
}

void TestControlBaseWrite::rejectsWhenNoPermission() {
    // 闸 3:Viewer 没 WriteVariable(见 Permission.cpp:29-46 的角色矩阵)
    CurrentUser::instance().login(makeUser("viewer1", Role::Viewer));
    Variable v("Valve1", 0x0001, Variable::DataType::Bool, Variable::Access::ReadWrite);
    ToggleControl t;
    t.bind(&v);

    QVERIFY(!t.writeToPlc(true));
    QVERIFY(!v.isDirty());
    QVERIFY(!t.canWrite());
}

void TestControlBaseWrite::rejectsByRange_notMarkDirty() {
    // ★ 闸 4 —— 最关键的一条。对应 ActionManager.cpp:75-86 修的「联动撒谎上报」。
    //   变量配了 range 0~100,写 999.0f 会被 Variable::setValue 的范围检查返 Rejected。
    //   此时【绝不能】markDirty:否则 WriteScheduler 会把没被接受的写发出去,
    //   而审计里还留着「已执行」—— 事故复盘时唯一证据是假的。
    //   ★ 注意:这里必须用 float(不是 double),否则会先被类型检查拒掉,
    //     测试就变成在测"类型不符"而不是"超范围",测不到声称的东西。
    Variable v("Level", 0x0003, Variable::DataType::Float, Variable::Access::ReadWrite);
    ToggleControl t;
    v.setRange(0.0, 100.0);
    v.setValue(50.0f);
    t.bind(&v);

    QVERIFY2(!t.writeToPlc(999.0f), "999 超 range 0~100,Variable 应返 Rejected");
    QVERIFY2(!v.isDirty(), "值被 Variable 拒收时绝不能标 dirty,否则 WriteScheduler 会发出去");
    QCOMPARE(v.value(), QVariant(50.0f));
}

void TestControlBaseWrite::rejectsByRange_localValueUnchanged() {
    // 闸 4 的另一半:本地不许撒谎。画面显示的值必须跟 PLC 里的值一致,
    // 否则操作员会照着画面判断设备状态
    Variable v("Level", 0x0003, Variable::DataType::Float, Variable::Access::ReadWrite);
    ToggleControl t;
    v.setRange(0.0, 100.0);
    v.setValue(50.0f);
    t.bind(&v);

    QVERIFY(!t.writeToPlc(999.0f));
    QCOMPARE(t.value(), QVariant(50.0f));   // 不是 999
}

// ===== canWrite 谓词 =====

void TestControlBaseWrite::canWrite_reflectsBindAccessAndRole() {
    ToggleControl t;
    QVERIFY(!t.canWrite());                       // 没绑
    Variable ro("Temp", 0x0004, Variable::DataType::Float, Variable::Access::ReadOnly);
    t.bind(&ro);
    QVERIFY(!t.canWrite());                       // 只读
    Variable rw("Valve1", 0x0005, Variable::DataType::Bool, Variable::Access::ReadWrite);
    t.bind(&rw);
    QVERIFY(t.canWrite());                        // Operator + ReadWrite
    CurrentUser::instance().login(makeUser("viewer1", Role::Viewer));
    QVERIFY(!t.canWrite());                       // 切 Viewer 后立刻失效
}

// ===== ToggleControl 断线修复的回归 =====

void TestControlBaseWrite::toggle_writesBackWhenBound() {
    // 修复前:点击只翻本地,PLC 侧纹丝不动。这里锁住"真写回"
    Variable v("Valve1", 0x0001, Variable::DataType::Bool, Variable::Access::ReadWrite);
    ToggleControl t;
    t.bind(&v);

    QGraphicsScene s;
    clickItem(&s, &t);

    QVERIFY2(v.isDirty(), "★ 绑定后点击必须真写回 PLC(这是本次修复的核心)");
    QCOMPARE(v.value(), QVariant(true));
    QCOMPARE(t.value(), QVariant(true));
}

void TestControlBaseWrite::toggle_unbound_stillTogglesLocally() {
    // 编辑器预览:没绑变量也要能点得动,否则作者以为控件坏了
    ToggleControl t;
    QGraphicsScene s;
    clickItem(&s, &t);

    QCOMPARE(t.value(), QVariant(true));
}

void TestControlBaseWrite::toggle_readOnly_doesNotFlipLocally() {
    // ★ 关键区分:绑定的是只读变量 → 这是【真设备的拒绝】,
    //   本地绝不能翻。翻了就会画面显示 ON 而 PLC 纹丝不动,
    //   操作员据此判断设备状态 —— 与「联动撒谎」同一类事故。
    Variable v("Temp", 0x0002, Variable::DataType::Bool, Variable::Access::ReadOnly);
    ToggleControl t;
    t.bind(&v);

    QGraphicsScene s;
    clickItem(&s, &t);

    QVERIFY(!t.value().isValid());   // 没翻
    QVERIFY(!v.isDirty());
}

QTEST_MAIN(TestControlBaseWrite)
#include "tst_ControlBaseWrite.moc"
