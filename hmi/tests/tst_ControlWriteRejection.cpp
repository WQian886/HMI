// ============================================================================
// G-21 / G-22(2026-10-30)· tst_ControlWriteRejection —— "写被拒"必须被看见
//
// 【这两个缺陷是一对,必须一起测】
//   G-21:运行时画面上,【没绑定变量】的控件点一下会本地翻一下 ——
//         画面显示"阀门开了"而 PLC 纹丝不动。操作员据此判断管路状态。
//         这跟 ActionManager.cpp:75-86 修的"联动撒谎上报"是同一类事故,
//         但更严重:那个至少在审计里留了假记录,这个是画面直接骗人。
//   G-22:"写被拒"这件事【没有任何人能听见】——
//         信号只声明在 Valve/Pump/Motor 上,ToggleControl 连声明都没有,
//         全仓库没有任何 connect。现象跟"我压根没点到"一模一样。
//
// 【为什么用【运行时模式】而不是"绑没绑变量"当判据】
//   "未绑定"在编辑器和运行时是【两件完全不同的事】:
//     编辑器:作者在摆件,点一下看效果 —— 预览,合理。
//     运行时:操作员在操作设备 —— 点一下画面变了而设备没动,是在骗人。
//   判据是"宿主是编辑器还是运行时"(setRuntimeMode),
//   判据放在基类 ControlBase 里 —— 让 4 个控件各写一遍必然漏一个。
//
// 【关联】
//   ControlBase::mayFlipLocallyAsPreview / notifyWriteRejected / writeRejected
//   ValveControl / PumpControl / MotorControl / ToggleControl 的 mouseReleaseEvent
//   RuntimeCanvas::showWriteRejection —— 生产环境里唯一接这个信号的地方
//     (G-22 那个提交补齐:那条接收方还没进 hmi_core 之前,这里先不测它)
// ============================================================================

#include <QtTest>
#include <QSignalSpy>
#include <QApplication>
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QDir>
#include <QFile>
#include <QStandardPaths>

#include "widgets/ValveControl.h"
#include "widgets/PumpControl.h"
#include "widgets/MotorControl.h"
#include "widgets/ToggleControl.h"
#include "widgets/base/ControlBase.h"
#include "Variable.h"
#include "auth/AuditLog.h"
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

// ★ 发一次真实的「点一下」(press + release 在同一点)。
//   事件必须发给【scene】—— QGraphicsObject 不处理 QEvent::MouseButtonPress
//   (那是 QWidget 的事件类型),直接 sendEvent 给控件会被静默丢掉。
//   详见 tst_ControlBaseWrite.cpp 的 clickItem 注释。
void clickInScene(QGraphicsScene* s, QGraphicsItem* item) {
    const QPointF pt = item->sceneBoundingRect().center();
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
}

// 栈对象用:加进 scene 点一下,再摘出来(scene 析构会 delete 它)
void clickItem(QGraphicsScene* s, QGraphicsItem* item) {
    s->addItem(item);
    clickInScene(s, item);
    s->removeItem(item);   // ★ 栈对象:交回自己析构
}

} // namespace

class TestControlWriteRejection : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    // ===== G-21:运行时 + 未绑定 =====
    void runtimeUnbound_valve_doesNotFlipLocally();
    void runtimeUnbound_pump_doesNotFlipLocally();
    void runtimeUnbound_motor_doesNotFlipLocally();
    void runtimeUnbound_toggle_doesNotFlipLocally();
    void runtimeUnbound_toggle_doesNotEmitToggled();

    // ===== G-21 的另一半:编辑器预览【必须】保留 =====
    void editorUnbound_valve_stillFlipsLocally();
    void editorUnbound_toggle_stillFlipsLocally();
    void editorUnbound_toggle_doesNotReject();

    // ===== G-22:理由 =====
    void rejectionReason_isRecordedOnTheControl();
    void rejectionReason_saysWhyItWasRejected();
    void boundButRejected_alsoCarriesAReason();

    // ===== G-22:审计留痕 =====
    void audit_recordsTheRejection();
};

// ★ 审计文件要隔离到临时目录 —— 否则跑一次单元测试就把
//   现场当天的审计日志删了(2026-10-21 已经踩过一次)。
//   见 tst_AuditLog.cpp 的 initTestCase。
static QString auditTempDir() {
    return QStandardPaths::writableLocation(QStandardPaths::TempLocation)
           + QStringLiteral("/hmi_write_rejection_test");
}

void TestControlWriteRejection::initTestCase() {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    qputenv("HMI_DATA_DIR", auditTempDir().toUtf8());
    QDir().mkpath(auditTempDir());
}

void TestControlWriteRejection::cleanupTestCase() {
    qunsetenv("HMI_DATA_DIR");
    QDir(auditTempDir()).removeRecursively();
}

void TestControlWriteRejection::init() {
    CurrentUser::instance().login(makeUser("op1", Role::Operator));
    AuditLog::instance().resetForTesting();
}

void TestControlWriteRejection::cleanup() {
    CurrentUser::instance().logout();
}

// =============================================================================
// G-21:运行时 + 未绑定 → 绝不本地翻
// =============================================================================

void TestControlWriteRejection::runtimeUnbound_valve_doesNotFlipLocally() {
    // ★★ 本文件最要紧的一条。
    //   运行时画面上摆了个没接线的阀门,操作员点它一下 ——
    //   画面显示"开"而 PLC 纹丝不动,阀门背后是高压管路。
    ValveControl v;
    v.setRuntimeMode(true);   // ★ 运行时画面

    QSignalSpy spy(&v, &ControlBase::writeRejected);
    QGraphicsScene s;
    clickItem(&s, &v);

    QVERIFY2(!v.isOpen(), "★ 运行时未绑定的阀门,点一下绝不能显示为开 —— PLC 侧什么都没发生");
    QVERIFY2(spy.count() == 1, "★ 运行时点不动必须说出去,不能一声不吭");
}

void TestControlWriteRejection::runtimeUnbound_pump_doesNotFlipLocally() {
    // 泵更严重:操作员看画面显示"运行"就认为下游有压
    PumpControl p;
    p.setRuntimeMode(true);

    QSignalSpy spy(&p, &ControlBase::writeRejected);
    QGraphicsScene s;
    clickItem(&s, &p);

    QVERIFY2(!p.isRunning(), "★ 运行时未绑定的泵,点一下绝不能显示为运行");
    QVERIFY2(!p.spinAnimator().isRunning(), "泵的动画也不能转 —— 它没在跑");
    QCOMPARE(spy.count(), 1);
}

void TestControlWriteRejection::runtimeUnbound_motor_doesNotFlipLocally() {
    MotorControl m;
    m.setRuntimeMode(true);

    QSignalSpy spy(&m, &ControlBase::writeRejected);
    QGraphicsScene s;
    clickItem(&s, &m);

    QVERIFY2(!m.isRunning(), "★ 运行时未绑定的电机,点一下绝不能显示为运行");
    QCOMPARE(spy.count(), 1);
}

void TestControlWriteRejection::runtimeUnbound_toggle_doesNotFlipLocally() {
    // ★ ToggleControl 以前【连 writeRejected 这个信号都没有】——
    //   它写被拒时既不翻也不吭声,是四个控件里最安静的一个。
    ToggleControl t;
    t.setRuntimeMode(true);

    QSignalSpy spy(&t, &ControlBase::writeRejected);
    QGraphicsScene s;
    clickItem(&s, &t);

    QVERIFY2(!t.value().toBool(), "★ 运行时未绑定的开关,点一下绝不能显示为 ON");
    QVERIFY2(spy.count() == 1,
             "★ ToggleControl 以前写被拒时一声不吭 —— 现在必须发信号");
}

void TestControlWriteRejection::runtimeUnbound_toggle_doesNotEmitToggled() {
    // toggled 的语义是"这个开关真的变了"。
    //   没变成还发,上层(画面日志、联动)就会记一条假的状态变化。
    ToggleControl t;
    t.setRuntimeMode(true);

    QSignalSpy toggled(&t, &ToggleControl::toggled);
    QGraphicsScene s;
    clickItem(&s, &t);

    QCOMPARE(toggled.count(), 0);
}

// =============================================================================
// G-21 的另一半:编辑器里的预览【必须】保留
// =============================================================================

void TestControlWriteRejection::editorUnbound_valve_stillFlipsLocally() {
    // ★ 这是"不误伤"的那一半。G-21 修的是"运行时撒谎",
    //   不是"编辑器里点不动"。
    //   作者在编辑器里摆阀门,点一下看效果 —— 这是预览,合理,不能被改掉。
    //   如果这条红了,说明修 G-21 时把编辑器的预览也一起干掉了 ——
    //   那是把一个静默失败换成了另一个(作者以为控件坏了)。
    ValveControl v;
    // 不调 setRuntimeMode —— 默认就是编辑模式
    QVERIFY(!v.runtimeMode());

    QSignalSpy spy(&v, &ControlBase::writeRejected);
    QGraphicsScene s;
    clickItem(&s, &v);

    QVERIFY2(v.isOpen(), "★ 编辑器里未绑定的阀门必须还能本地翻(预览)");
    QCOMPARE(spy.count(), 0);   // 预览不是"被拒"
}

void TestControlWriteRejection::editorUnbound_toggle_stillFlipsLocally() {
    ToggleControl t;
    QVERIFY(!t.runtimeMode());

    QSignalSpy spy(&t, &ControlBase::writeRejected);
    QGraphicsScene s;
    clickItem(&s, &t);

    QVERIFY2(t.value().toBool(), "★ 编辑器里未绑定的开关必须还能本地翻(预览)");
    QCOMPARE(spy.count(), 0);
}

void TestControlWriteRejection::editorUnbound_toggle_doesNotReject() {
    // 预览【不许】写审计 —— 它不是"被拒",是"没接线"。
    //   要是这里也写审计,审计日志里会塞满"用户试图写一个没接线的元件",
    //   真出事的时候反而找不到该找的那条。
    //
    // ★ 先写一条 marker,把审计文件【造出来】——
    //   否则文件压根不存在,下面那个"文件里没有 rejected"的断言就是空的
    //   (查无此文件 ⇒ 恒真 ⇒ 测不到任何东西)。
    AuditLog::instance().appendCurrent("test.marker", QStringLiteral("x"),
                                        QStringLiteral("y"), QStringLiteral("ok"));

    ToggleControl t;
    QGraphicsScene s;
    clickItem(&s, &t);

    QFile f(AuditLog::defaultFilePath());
    QVERIFY2(f.open(QIODevice::ReadOnly | QIODevice::Text),
             "审计文件打不开 —— 本用例验证不到任何东西");
    const QString text = QString::fromUtf8(f.readAll());
    f.close();

    QVERIFY2(text.contains(QStringLiteral("action=test.marker")),
             "marker 应该在文件里 —— 否则下面的断言是空的");
    QVERIFY2(!text.contains(QStringLiteral("result=rejected")),
             "★ 编辑器预览不是被拒,不该写审计");
}

// =============================================================================
// G-22:理由
// =============================================================================

void TestControlWriteRejection::rejectionReason_isRecordedOnTheControl() {
    // 上层要弹提示,得先知道【为什么】。理由存在控件上,
    //   免得每个上层各自编一套说辞(编三遍就编出三个版本了)。
    ValveControl v;
    v.setRuntimeMode(true);

    QGraphicsScene s;
    clickItem(&s, &v);

    QVERIFY2(!v.lastWriteRejection().isEmpty(),
             "★ 写被拒必须留下理由 —— 没有理由上层只能弹'操作失败'");
}

void TestControlWriteRejection::rejectionReason_saysWhyItWasRejected() {
    // 理由要【具体】:"未绑定"和"只读"是两回事,排查时要能分开。
    //   这里测未绑定(闸 1)。
    {
        ValveControl v;
        v.setRuntimeMode(true);
        QGraphicsScene s;
        clickItem(&s, &v);
        QVERIFY2(v.lastWriteRejection().contains(QStringLiteral("未绑定")),
                 qPrintable(QStringLiteral("理由里应说明是未绑定,实际是: %1")
                                .arg(v.lastWriteRejection())));
    }
    // 这里测只读(闸 2)—— 同一条路径,不同的理由。
    {
        Variable ro("Valve2", 0x4002, Variable::DataType::Bool, Variable::Access::ReadOnly);
        ValveControl v;
        v.bind(&ro);
        v.setRuntimeMode(true);
        QGraphicsScene s;
        clickItem(&s, &v);
        QVERIFY2(v.lastWriteRejection().contains(QStringLiteral("只读")),
                 qPrintable(QStringLiteral("理由里应说明是只读,实际是: %1")
                                .arg(v.lastWriteRejection())));
    }
}

void TestControlWriteRejection::boundButRejected_alsoCarriesAReason() {
    // 已绑定但被拒(只读 / 没权限 / 值超范围)也必须有理由。
    //   这三种以前只有"只读"这一种有原因,另外两种是【一声不吭】。
    Variable ro("Temp", 0x4003, Variable::DataType::Bool, Variable::Access::ReadOnly);
    ToggleControl t;
    t.bind(&ro);

    QSignalSpy spy(&t, &ControlBase::writeRejected);
    QGraphicsScene s;
    clickItem(&s, &t);

    QCOMPARE(spy.count(), 1);
    QVERIFY(!t.lastWriteRejection().isEmpty());
    QCOMPARE(spy.at(0).at(0).toString(), t.lastWriteRejection());
}

// =============================================================================
// G-22:审计留痕
// =============================================================================

void TestControlWriteRejection::audit_recordsTheRejection() {
    // ★ "点了但没写成"必须查得到。
    //   原来闸 1/闸 2/闸 3 一条审计都不写 —— 最常见的三种被拒【查不到】。
    //   事故复盘时唯一能拿出来的证据是"什么都没发生",跟真没点过一样。
    Variable ro("Valve2", 0x4002, Variable::DataType::Bool, Variable::Access::ReadOnly);
    ValveControl v;
    v.bind(&ro);
    v.setRuntimeMode(true);

    QGraphicsScene s;
    clickItem(&s, &v);

    QFile f(AuditLog::defaultFilePath());
    QVERIFY2(f.open(QIODevice::ReadOnly | QIODevice::Text),
             "审计文件打不开 —— 本用例验证不到任何东西");
    const QString text = QString::fromUtf8(f.readAll());
    f.close();

    QVERIFY2(text.contains(QStringLiteral("result=rejected")),
             qPrintable(QStringLiteral("审计里必须有 result=rejected,实际内容:\n%1").arg(text)));
    QVERIFY2(text.contains(QStringLiteral("action=control.write")),
             "审计条目的动作名必须是 control.write");
    QVERIFY2(text.contains(QStringLiteral("Valve2")),
             "审计里必须记清是哪个变量");
}

QTEST_MAIN(TestControlWriteRejection)
#include "tst_ControlWriteRejection.moc"
