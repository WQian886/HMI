// ============================================================================
// tst_AlarmShelve —— F2-c 报警屏蔽(shelve / inhibit)
//
// 【为什么必须做,而且优先级不低于滞回】
//   滞回压的是"报太多"。屏蔽解决的是另一类:"这条报警现在没意义,别烦我"。
//   典型场景 —— 传感器在检修,阈值报警会一直响;或者某台泵本来就不投用,
//   它的报警纯属噪声。操作员需要临时把它按掉。
//
// 【本项目反复吃的那类静默失败,在屏蔽上尤其致命】
//   如果"被屏蔽的 checker 不触发"是唯一表现,那就是:
//     屏幕上什么都没有,没有行、没有计数、没有日志 ——
//     操作员会以为"这次真的没报警",而实际是【被我们藏起来了】。
//   更糟的是:检修结束忘了取消屏蔽 → 传感器真的坏了 → 依然没有报警。
//   设备坏了但界面一片安静,这是能出人命的。
//   所以本设计的硬约束:**屏蔽必须是可见的、可撤销的。**
//     ① 界面显式显示"N 条报警被屏蔽"(shelveCount + shelveChanged 信号)
//     ② shelveCount() 随时可查,不只给 UI 用(日志/测试/未来的诊断面板)
//     ③ 手动可撤销,shelve/unshelve 成对
//   【没做的】自动到期(8 小时后自己恢复)。需要常驻定时器 + 到期语义 +
//   跨重启策略,在项目连报警编辑 UI 都还没有时做,容易做出一个
//   "以为会自己恢复、实际不会"的半成品。风险靠"常显屏蔽数 + 审计留痕"兜。
//
// 【怎么测】FireChecker 手动 emit triggered,不依赖 ThresholdAlarmChecker,
//   这样"屏蔽层"和"检测层"两件事被分开验证 —— 屏蔽要是只在 checker 里生效,
//   别的 Checker 类别(写失败报警等)就漏了。
// ============================================================================

#include <QtTest>
#include <QSignalSpy>
#include "AlarmManager.h"
#include "auth/CurrentUser.h"
#include "auth/Role.h"

// ★ 这个 FireChecker 放在【命名空间作用域】(同 tst_AlarmListControl.cpp 的位置)。
//   注意下面所有调用都写成了 tempHigh->fire() 而不是 tempHigh.fire() ——
//   checker 是 new 出来的裸指针,非静态成员函数只能用 ->。
//   【踩过的坑,记下来别再走一遍】写成 ptr.fire() 会报
//     error C2228: "'.fire' 的左边必须有类/结构/联合" + note "是否改用 '->'"
//   MSVC 只报【第一处】,后面 4 处同样错却不报 —— 删掉第一处,错误就顺移到下一处,
//   看起来像"只有这一行有问题",实际是 5 行全错。
//   我当时先怀疑是 moc 认不准嵌套类(还为此把类从 TestAlarmShelve 里挪到了外面),
//   那个判断是错的,类挪出去错误依旧,只是行号变了。
//   定位靠的是把报错行删掉看错误会不会顺移 —— 一顺移就说明是"全篇都错、只报第一个"。
class FireChecker : public AlarmChecker {
    Q_OBJECT
public:
    explicit FireChecker(const QString& n, AlarmLevel lvl, QObject* parent = nullptr)
        : AlarmChecker(parent) {
        setName(n);
        setLevel(lvl);
    }
    void fire() {
        Alarm a;
        // nextOffset_ 保证同一个毫秒里连发两次也拿到不同 id
        a.id_          = QDateTime::currentMSecsSinceEpoch() + nextOffset_++;
        a.name         = name();
        a.variableName = QStringLiteral("plc1.Speed");
        a.level        = level();
        a.state        = AlarmState::Active;
        a.triggeredAt  = QDateTime::currentDateTime();
        a.triggerValue = QStringLiteral("77");
        emit triggered(a);
        lastId_ = a.id_;
    }
    void recover() { emit recovered(lastId_); }
    qint64 lastId() const { return lastId_; }
private:
    qint64 lastId_ = 0;
    qint64 nextOffset_ = 0;
};

class TestAlarmShelve : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();

    // ===== 核心 =====
    void shelvedChecker_doesNotAppearInActiveAlarms();
    void shelveIsReversible_unshelvedAlarmTriggersAgain();
    void shelveCount_reflectsCurrentShelvedSet();
    void shelveOtherAlarms_stillWorkNormally();

    // ===== 可见性(防"藏起来"的关键)=====
    void shelveEmitsSignal_soUiCanShowIt();
    void shelvePersistsNotSilently_clearAllResetsIt();

    // ===== 边界 =====
    void shelveUnknownName_returnsFalse();
    void shelveTwice_isIdempotent();
    void doubleShelveDoesNotDoubleCount();

private:
    // 登录一个有全部权限的角色 —— 屏蔽是工程师动作,不该被操作员权限挡住
    static void loginAsEngineer();
};

void TestAlarmShelve::loginAsEngineer() {
    // User 是聚合体,没有 (QString, Role) 构造 —— 逐字段赋值(同 tst_AlarmListControl)
    qwhmi::auth::User u;
    u.name = QStringLiteral("工程师甲");
    u.role = qwhmi::auth::Role::Engineer;
    qwhmi::auth::CurrentUser::instance().login(u);
}

void TestAlarmShelve::initTestCase() {
    loginAsEngineer();
    qInfo() << "[tst_AlarmShelve] init";
}

// ★ 被屏蔽的 checker 触发了,active 里不该有它。
//   注意:fire() 确实 emit 了 triggered —— 屏蔽拦的是"这条报警进不进 active",
//   不是"检测器还会不会算"。真实系统里传感器照样在越线。
void TestAlarmShelve::shelvedChecker_doesNotAppearInActiveAlarms() {
    AlarmManager mgr;
    auto* tempHigh = new FireChecker(QStringLiteral("温度高"), AlarmLevel::Critical);
    mgr.addChecker(tempHigh);

    QSignalSpy trig(&mgr, &AlarmManager::alarmTriggered);

    QVERIFY2(mgr.shelve(QStringLiteral("温度高")), "屏蔽一个已注册的 checker 应该成功");

    tempHigh->fire();
    QCOMPARE(trig.count(), 0);        // 不该有任何报警进系统
    QCOMPARE(mgr.count(), 0);
    QVERIFY2(mgr.activeAlarms().isEmpty(), "被屏蔽的报警绝不能出现在 active 列表里");
}

// ★ 屏蔽必须能撤销,且撤销后报警立刻恢复正常工作。
//   "只能屏蔽不能取消"是个陷阱功能 —— 检修完了却撤不掉。
void TestAlarmShelve::shelveIsReversible_unshelvedAlarmTriggersAgain() {
    AlarmManager mgr;
    auto* tempHigh = new FireChecker(QStringLiteral("温度高"), AlarmLevel::Critical);
    mgr.addChecker(tempHigh);

    QVERIFY(mgr.shelve(QStringLiteral("温度高")));
    tempHigh->fire();
    QCOMPARE(mgr.count(), 0);

    QVERIFY2(mgr.unshelve(QStringLiteral("温度高")), "取消屏蔽应该成功");
    QCOMPARE(mgr.shelveCount(), 0);

    tempHigh->fire();
    QCOMPARE(mgr.count(), 1);
}

// 屏蔽数必须可查 —— 界面"显示 N 条被屏蔽"和日志都依赖它。
// 只给 UI 一个 private 字段的话,测试和别的模块都问不到"现在屏蔽了几条"。
void TestAlarmShelve::shelveCount_reflectsCurrentShelvedSet() {
    AlarmManager mgr;
    auto* a = new FireChecker(QStringLiteral("温度高"), AlarmLevel::Critical);
    auto* b = new FireChecker(QStringLiteral("温度低"), AlarmLevel::Critical);
    auto* c = new FireChecker(QStringLiteral("压力高"), AlarmLevel::Warning);
    mgr.addChecker(a);
    mgr.addChecker(b);
    mgr.addChecker(c);

    QCOMPARE(mgr.shelveCount(), 0);
    QVERIFY(mgr.shelve(QStringLiteral("温度高")));
    QCOMPARE(mgr.shelveCount(), 1);
    QVERIFY(mgr.shelve(QStringLiteral("压力高")));
    QCOMPARE(mgr.shelveCount(), 2);
    QVERIFY(mgr.isShelved(QStringLiteral("温度高")));
    QVERIFY(!mgr.isShelved(QStringLiteral("温度低")));

    QVERIFY(mgr.unshelve(QStringLiteral("温度高")));
    QCOMPARE(mgr.shelveCount(), 1);
    QVERIFY(!mgr.isShelved(QStringLiteral("温度高")));
}

// 屏蔽一条不能连坐别的 —— 早期很容易写成"一屏蔽就全静音"。
void TestAlarmShelve::shelveOtherAlarms_stillWorkNormally() {
    AlarmManager mgr;
    auto* a = new FireChecker(QStringLiteral("温度高"), AlarmLevel::Critical);
    auto* b = new FireChecker(QStringLiteral("压力高"), AlarmLevel::Warning);
    mgr.addChecker(a);
    mgr.addChecker(b);

    QVERIFY(mgr.shelve(QStringLiteral("温度高")));
    a->fire();
    QCOMPARE(mgr.count(), 0);

    b->fire();                       // 没被屏蔽的那条必须照常报
    QCOMPARE(mgr.count(), 1);
    QCOMPARE(mgr.activeAlarms().first().name, QStringLiteral("压力高"));
}

// ★ 屏蔽状态变化必须发信号 —— 否则界面上的"N 条被屏蔽"永远不会更新,
//   界面会一直显示 0,这就是"藏起来了"。
void TestAlarmShelve::shelveEmitsSignal_soUiCanShowIt() {
    AlarmManager mgr;
    auto* a = new FireChecker(QStringLiteral("温度高"), AlarmLevel::Critical);
    mgr.addChecker(a);

    QSignalSpy spy(&mgr, &AlarmManager::shelveChanged);

    QVERIFY(mgr.shelve(QStringLiteral("温度高")));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("温度高"));
    QVERIFY(spy.at(0).at(1).toBool());   // true = 现在被屏蔽了

    QVERIFY(mgr.unshelve(QStringLiteral("温度高")));
    QCOMPARE(spy.count(), 2);
    QVERIFY(!spy.at(1).at(1).toBool());  // false = 现在不被屏蔽
}

// clearAll() 必须把屏蔽状态一起清掉。
//   换工程时留着上个工程的屏蔽名单,新工程一开局就有报警被静默藏起来。
void TestAlarmShelve::shelvePersistsNotSilently_clearAllResetsIt() {
    AlarmManager mgr;
    // ★ 必须 new,不能放栈上。
    //   addChecker 内部 setParent(this),clearAll() 会 delete 每个 checker ——
    //   对栈对象 delete 就是野指针,表现为测试跑到这条直接挂死(我踩过一次)。
    //   本文件其余用例不调 clearAll,侥幸没暴露这个。
    auto* a = new FireChecker(QStringLiteral("温度高"), AlarmLevel::Critical);
    mgr.addChecker(a);
    QVERIFY(mgr.shelve(QStringLiteral("温度高")));
    QCOMPARE(mgr.shelveCount(), 1);

    mgr.clearAll();
    QCOMPARE(mgr.shelveCount(), 0);
    QVERIFY2(!mgr.isShelved(QStringLiteral("温度高")),
             "clearAll 之后不该还有任何 checker 处于屏蔽态");
}

// 屏蔽一个不存在的名字必须返回 false —— 不能默默成功,
// 否则操作员以为自己屏蔽了,实际那条报警还在响。
void TestAlarmShelve::shelveUnknownName_returnsFalse() {
    AlarmManager mgr;
    QVERIFY2(!mgr.shelve(QStringLiteral("根本不存在的报警")),
             "屏蔽不存在的报警必须失败,不能假装成功");
    QVERIFY(!mgr.unshelve(QStringLiteral("根本不存在的报警")));
    QCOMPARE(mgr.shelveCount(), 0);
}

// 重复屏蔽同一条应该幂等 —— 不该重复计数,也不该重复发信号。
void TestAlarmShelve::shelveTwice_isIdempotent() {
    AlarmManager mgr;
    auto* a = new FireChecker(QStringLiteral("温度高"), AlarmLevel::Critical);
    mgr.addChecker(a);

    QVERIFY(mgr.shelve(QStringLiteral("温度高")));
    QSignalSpy spy(&mgr, &AlarmManager::shelveChanged);

    QVERIFY2(mgr.shelve(QStringLiteral("温度高")), "重复屏蔽不算失败");
    QCOMPARE(mgr.shelveCount(), 1);        // 关键:不能变成 2
    QCOMPARE(spy.count(), 0);              // 状态没变就不该发信号

    // 重复取消也要幂等:已注册但当前没屏蔽 → true(no-op 成功),
    //   这跟"名字没注册过"→ false 是两回事,UI 要能分清。
    QVERIFY(mgr.unshelve(QStringLiteral("温度高")));
    QVERIFY(mgr.unshelve(QStringLiteral("温度高")));
    QCOMPARE(mgr.shelveCount(), 0);
    QVERIFY(!mgr.unshelve(QStringLiteral("根本没注册过的报警")));
}

void TestAlarmShelve::doubleShelveDoesNotDoubleCount() {
    AlarmManager mgr;
    auto* a = new FireChecker(QStringLiteral("温度高"), AlarmLevel::Critical);
    mgr.addChecker(a);
    for (int i = 0; i < 5; ++i) mgr.shelve(QStringLiteral("温度高"));
    QCOMPARE(mgr.shelveCount(), 1);
    // 反复屏蔽之后仍然能正常撤销
    QVERIFY(mgr.unshelve(QStringLiteral("温度高")));
    QCOMPARE(mgr.shelveCount(), 0);
}

QTEST_MAIN(TestAlarmShelve)
#include "tst_AlarmShelve.moc"
