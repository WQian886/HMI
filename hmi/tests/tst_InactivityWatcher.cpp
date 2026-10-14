// ============================================================================
// 阶段 B · B2.2 · InactivityWatcher 单元测试
//
// 【为什么这个文件在 2026-10-14 才存在】
// 做 24h 长跑的 --soak 旁路时发现:这个类【零测试覆盖】,而且它是个
// 【安全特性】—— 5 分钟无操作自动注销,防止有人离开工位后权限一直挂着。
//
// --soak 要在长跑期间把它关掉(长跑没人碰键盘鼠标,5 分钟必然触发,
// 触发后会弹模态登录框把轮询和报警引擎全堵停 —— 24h 弹 288 次)。
// 于是我改了这个类的【生命周期】:从"总是 installTo + start"改成
// "soak 时跳过这两步"。
//
// ★ 这跟改善清单 #13 撞的是同一面墙:
//     【没有测试 ≠ 改动安全。】
//   一个零覆盖的安全特性,被改了启动时机,却没有任何东西能回答
//   "正常路径有没有被弄坏"。所以动手前先补上这一层。
//
// 【本文件的纪律】先钉住【当前实际行为】,不改一行生产代码。这就是基线。
//   断言必须能咬住"安全特性被悄悄关掉"这件事 —— 而不只是"计时器会响"。
//
// 关联:../src/auth/InactivityWatcher.{h,cpp}
//       ../main.cpp:265-282(--soak 跳过 installTo/start 的那两处)
// ============================================================================

#include <QtTest>
#include <QSignalSpy>
#include <QCoreApplication>
#include <QEvent>

#include "auth/InactivityWatcher.h"

using namespace qwhmi::auth;

class TestInactivityWatcher : public QObject {
    Q_OBJECT

private slots:
    // ---- 契约:默认值 ----
    void defaultTimeout_isFiveMinutes();

    // ---- 契约:生命周期(★ --soak 依赖的就是这条)----
    void setTimeoutMs_actuallyChangesTheTimer();  // ★ 修复前是红的
    void start_withoutInstallTo_doesNotArmTimer();
    void installTo_thenStart_armsTimer();
    void stop_haltsTimer();
    void start_afterInstallTo_restartsTimer();

    // ---- 契约:超时发信号 ----
    void firesAfterTimeout();
    void doesNotFireBeforeTimeout();

    // ---- 契约:用户操作重置倒计时 ----
    void userActivity_resetsCountdown();
    void nonActivityEvent_doesNotReset();

private:
    // ★ 送"用户操作事件"的辅助。
    //
    //   【为什么不能直接 sendEvent(qApp, ...)】—— 踩过:
    //   第一版这么写,4 条用例全红(该触发时没触发)。
    //   原因是 eventFilter 装在 qApp 上,而送进 qApp 的事件未必会走
    //   它自己的 eventFilter 链 —— 观察到的现象就是戳了等于没戳,
    //   计时器照原时间到期,和"戳了但没重置"完全一样,分不出来。
    //
    //   改成送一个【普通的子 QObject】:事件发给它,事件沿
    //   "发给接收者 → 冒泡到 parent 的 filter"这条路上行,于是 qApp
    //   上装的 InactivityWatcher::eventFilter 真的会被调用。
    //   这也正是真实运行时的情况 —— 用户的鼠标键盘事件发给具体控件,
    //   由 Qt 的事件传播走到 QApplication 的过滤器,不是直接发给 qApp。
    //
    //   【为什么这个坑值得写下来】它伪装成"生产代码有 bug":
    //   "戳了不重置"和"戳根本没送到"的现象一模一样。
    //   当时差点去改 InactivityWatcher::eventFilter —— 那会把一个
    //   正确的生产代码改坏,去迁就一个写错的测试。
    //   先分清"没送到"和"送到了但没处理",再动手。
    void poke(QEvent::Type type) {
        if (!probe_) probe_ = new QObject(qApp);
        QEvent ev(type);
        QCoreApplication::sendEvent(probe_, &ev);
    }

    QObject* probe_ = nullptr;
};

// 5 分钟是安全参数的默认值,写死是为了"有人改小它好让测试跑得快"时被立刻发现。
// ★ 用 setTimeoutMs() 改小的测试(下面那些)没问题 —— 它们显式改了并显式断言。
void TestInactivityWatcher::defaultTimeout_isFiveMinutes() {
    InactivityWatcher w;
    QCOMPARE(w.timeoutMs(), 5 * 60 * 1000);
}

// ★★★ 【本文件发现的第一个真 bug】setTimeoutMs() 是个【假 setter】。
//
//   写它的时候就觉得不对劲:设了 50ms,计时器却按 5 分钟走。
//   查 InactivityWatcher.h:41 ——
//       void setTimeoutMs(int ms) { timeoutMs_ = ms; if (timer_->isActive()) start(); }
//
//   它只改了【成员变量】timeoutMs_,从没调过 timer_->setInterval()。
//   而构造函数(InactivityWatcher.cpp:19)只在【构造那一刻】调过一次
//   setInterval(timeoutMs_),之后 interval 再没被同步过。
//   那个 if (timer_->isActive()) start() 也只是【重新启动】计时器,
//   启动时用的是 timer_ 上【原来的 interval】—— 还是 5 分钟。
//
//   【为什么这条值得钉】这个 setter 的存在让人以为"超时可以调",
//   于是谁都能写出 setTimeoutMs(1000) 然后以为 1 秒后会触发 ——
//   实际是 5 分钟。这类 bug 不崩不报错,只是【安静地不生效】。
//   本机第一次跑本文件时 4 条用例全红,红的原因就是这个:
//   我设了 50ms 却等了 400ms,当然等不到 5 分钟后的信号。
//
//   【怎么发现的】不是读代码看出来的,是【测试红】逼出来的。
//   而且第一反应差点搞错 —— 我先怀疑 eventFilter 收不到事件,
//   还做了个 sendEvent 的探针实验(那个实验本身是对的,另有价值,
//   见 poke() 的注释)。是探针实验证明"timer 本身能正常触发",
//   才把矛头转向这个 setter。
//   ★ 教训:【测试红】不等于【生产代码有 bug】,也可能【测试的假设错了】。
//     先排除自己的错,再动生产代码 —— 顺序反了就会把对的代码改坏。
void TestInactivityWatcher::setTimeoutMs_actuallyChangesTheTimer() {
    InactivityWatcher w;
    QSignalSpy spy(&w, &InactivityWatcher::inactivityTimeout);

    // 先 start,让计时器真的在跑
    w.installTo(qApp);
    w.start();

    // ★ 现在设成 50ms。修复前:timeoutMs() 返 50,但计时器仍是 5 分钟,
    //   下面 spy.wait(1000) 必然失败 —— 这就是负向对照。
    w.setTimeoutMs(50);
    QCOMPARE(w.timeoutMs(), 50);          // 字段确实改了(这条修复前也是绿的)
    QVERIFY2(spy.wait(1000),
             "setTimeoutMs(50) 之后 1 秒内就该触发 —— "
             "如果这里失败,说明 setTimeoutMs 只改了字段没改计时器 interval。"
             "这正是修复前的实际行为。");
}

// ★★★ 本文件最重要的一条(安全边界)。
//
//   InactivityWatcher::start() 第一件事是查 installed_,没 installTo 就
//   qCWarning 后【直接 return】(InactivityWatcher.cpp:40-43)。
//
//   【为什么这条比"计时器会响"重要】main.cpp 里的 --soak 分支正是靠它 ——
//   我们跳过 installTo 和 start,而这个"跳过安全特性"的做法要成立,
//   前提是"没装就真的不会跑"。如果哪天有人把那个 if (!installed_) 提前门
//   去掉(看起来像"清理"或"优化"),--soak 就会在长跑期间照样自动注销,
//   而那条路径一行测试都跑不到 —— 后果是长跑又变成 UNKNOWN,
//   而且没人知道是"我改坏了"还是"程序本来就那样"。
//
//   这正是本项目反复吃的坑:测试绿着,功能没了。
void TestInactivityWatcher::start_withoutInstallTo_doesNotArmTimer() {
    InactivityWatcher w;
    QSignalSpy spy(&w, &InactivityWatcher::inactivityTimeout);
    // 故意【不调 installTo】
    w.setTimeoutMs(50);        // 缩短超时,让测试跑得快
    w.start();

    QVERIFY(spy.isEmpty());
    // 等 4 倍超时:如果计时器真的被启动了,这里必然已经发过信号
    QTest::qWait(200);
    QCOMPARE(spy.count(), 0);   // ★ 一个都没有 —— 这才是本条的意义
}

void TestInactivityWatcher::installTo_thenStart_armsTimer() {
    InactivityWatcher w;
    QSignalSpy spy(&w, &InactivityWatcher::inactivityTimeout);
    w.setTimeoutMs(50);
    w.installTo(qApp);         // ★ 必须先装
    w.start();

    QVERIFY(spy.wait(500));    // 装了就应该能触发
    QCOMPARE(spy.count(), 1);
}

void TestInactivityWatcher::stop_haltsTimer() {
    InactivityWatcher w;
    QSignalSpy spy(&w, &InactivityWatcher::inactivityTimeout);
    w.setTimeoutMs(50);
    w.installTo(qApp);
    w.start();
    w.stop();                  // ★ 装 + 起,然后立刻停

    QTest::qWait(200);
    QCOMPARE(spy.count(), 0);
}

void TestInactivityWatcher::start_afterInstallTo_restartsTimer() {
    InactivityWatcher w;
    QSignalSpy spy(&w, &InactivityWatcher::inactivityTimeout);
    w.setTimeoutMs(80);
    w.installTo(qApp);
    w.start();

    // 连着戳几下,每次都该重置倒计时。
    // ★ 如果重置逻辑坏了(比如那个 if (timer_->isActive()) 的判断写反),
    //   第一次戳之后计时器就不再重启,后面的戳是空转 ——
    //   而这条断言正是在那个瞬间抓到它。
    for (int i = 0; i < 4; ++i) {
        QTest::qWait(50);      // < 80ms,单独看每次都该"还没到"
        poke(QEvent::MouseMove);
        QCOMPARE(spy.count(), 0);
    }
    // 停止戳之后,应该按最后一次戳起算 80ms 触发
    QVERIFY(spy.wait(400));
}

// 超时确实会发信号 —— 这是"自动注销"能工作的前提。
void TestInactivityWatcher::firesAfterTimeout() {
    InactivityWatcher w;
    QSignalSpy spy(&w, &InactivityWatcher::inactivityTimeout);
    w.setTimeoutMs(30);
    w.installTo(qApp);
    w.start();
    QVERIFY(spy.wait(400));
}

void TestInactivityWatcher::doesNotFireBeforeTimeout() {
    InactivityWatcher w;
    QSignalSpy spy(&w, &InactivityWatcher::inactivityTimeout);
    w.setTimeoutMs(500);
    w.installTo(qApp);
    w.start();
    QTest::qWait(150);         // 远小于 500ms
    QCOMPARE(spy.count(), 0);
}

// ★ 这条守的是"什么算操作"。
//   InactivityWatcher.cpp:53-64 白名单了 6 种事件:
//   MouseButtonPress / MouseButtonRelease / MouseMove / KeyPress / KeyRelease / Wheel
//   其余一律不重置 —— 这是有意的(窗口激活之类不该算"人在操作"),
//   否则程序自己弹个窗就把计时器重置了,自动注销形同虚设。
void TestInactivityWatcher::userActivity_resetsCountdown() {
    InactivityWatcher w;
    QSignalSpy spy(&w, &InactivityWatcher::inactivityTimeout);
    w.setTimeoutMs(120);
    w.installTo(qApp);
    w.start();

    // 6 种白名单事件各戳一次,每次都在超时之前
    const QEvent::Type acts[] = {
        QEvent::MouseButtonPress, QEvent::MouseButtonRelease, QEvent::MouseMove,
        QEvent::KeyPress,        QEvent::KeyRelease,        QEvent::Wheel
    };
    for (QEvent::Type t : acts) {
        QTest::qWait(40);      // < 120ms
        poke(t);
        QCOMPARE(spy.count(), 0);
    }
}

// 非白名单事件【不得】重置 —— 这条是"自动注销不会被程序自己的动作骗过"的保证。
//
// ★ 这里【必须查 spy.count() 而不是 spy.wait()】—— 踩过:
//   第一版写的是 QVERIFY(spy.wait(400)),结果红了。
//   但日志明明白白打了 "emit inactivityTimeout" —— 信号确实发了。
//   原因:spy.wait() 是【从调用那一刻起】等信号,而信号在上面的
//   qWait(40) 循环里早就发过了(100ms 超时 < 4×40ms),wait() 错过了它。
//
//   【教训】又是一条"测试红 ≠ 生产代码错"。
//   如果当时照着红就改 eventFilter 的白名单,就会把一份正确的
//   安全逻辑改坏 —— 罪魁祸首是自己的 API 误用。
//   判据:先确认【信号到底发没发】(看 spy.count() 或日志),再决定动谁。
void TestInactivityWatcher::nonActivityEvent_doesNotReset() {
    InactivityWatcher w;
    QSignalSpy spy(&w, &InactivityWatcher::inactivityTimeout);
    w.setTimeoutMs(100);
    w.installTo(qApp);
    w.start();

    // 100ms 内连发 4 个"不算操作"的事件,每次间隔 40ms。
    // 若它们能重置计时器,到期时间会被一次次往后推,一个信号都不会有。
    for (int i = 0; i < 4; ++i) {
        QTest::qWait(40);
        poke(QEvent::WindowActivate);   // 白名单里没有
    }
    QTest::qWait(300);                  // 留足余量等它到期
    QVERIFY2(spy.count() > 0,
             "非白名单事件不该重置倒计时 —— 计时器应该照常到期并发出信号。"
             "如果这里失败,说明白名单被意外扩大了(程序自己的动作就能骗过自动注销)。");
}

QTEST_MAIN(TestInactivityWatcher)
#include "tst_InactivityWatcher.moc"
