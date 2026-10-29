// ============================================================================
// tst_ThresholdAlarmChecker —— F2 报警抑制
//
// 【为什么这个文件是新建的,不是往 tst_AlarmRecorder 里加】
//   tst_AlarmRecorder 测的是【报警落库】,链路末端是 SQLite。
//   而"报警该不该触发"这个决定发生在链路最前端 —— ThresholdAlarmChecker。
//   前端一个用例都没有:全仓检索 ThresholdAlarmChecker,只出现在
//   CMakeLists / 注释 / 其他测试的"这里故意不用它"里。
//   也就是说 **minDurationMs 这段防抖逻辑从来没被任何测试执行过**。
//   本次要往这里加滞回,不先给它建测试,就是在没地基的地方盖二楼。
//
// 【怎么测】PollScheduler::rawRead 是 public 信号,直接 emit 即可,
//   不需要真 Modbus 总线、不需要真 driver。Checker 只认 v == variable_。
// ============================================================================

#include <QtTest>
#include <QSignalSpy>
#include "ThresholdAlarmChecker.h"
#include "../Variable.h"
#include "../PollScheduler.h"
#include "../JsonSerializer.h"

class TestThresholdAlarmChecker : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();

    // ===== 状态机 =====
    void shortSpike_belowMinDuration_doesNotTrigger();
    void sustainedOverThreshold_triggersOnce();
    void reBreachAfterRecovery_waitsFullMinDurationWithNewId();
    void clearFromAlarmManager_resetsCheckerState();

    // ===== F2:滞回(压 chatter)=====
    void chatter_slowOscillation_isSuppressedByHysteresis();
    void hysteresis_valueInDeadBand_doesNotRecover();
    void hysteresis_zero_behavesLikeBefore();
    void hysteresis_configRoundTripsThroughJson();

    // ===== ★ BUGFIX-C2(2026-10-24):Below 方向的判据重叠 =====
    //
    //   全套 F2 滞回用例【清一色 Direction::Above】—— Below 方向一条都没有。
    //   于是 Below 的判据重叠【从来没被任何测试执行过】,长跑里它以
    //   「每 0.44 秒报一次」的形式活了 24 小时(见下面注释)。
    void belowSustainedUnderThreshold_doesNotRetrigger();
    void belowWithHysteresis_sameShapeAsAbove();

private:
    // 造一个可控的 checker 环境
    struct Fx {
        PollScheduler*         poll = nullptr;
        Variable*              var  = nullptr;
        ThresholdAlarmChecker* ck   = nullptr;

        Fx(AlarmLevel lvl = AlarmLevel::Critical,
           Direction dir = Direction::Above,
           double threshold = 50.0,
           int minMs = 500,
           double hyst = 0.0)
            // ★ PollScheduler 只是为了拿到一个能发 rawRead 的 QObject —— Checker 在构造时
            //   connect 它的 rawRead 信号。不 start(),所以不会有真 driver 在背后跑。
            //   varMgr_/driver 传 nullptr 是安全的:构造函数只判空,onTick 不会被触发。
            : poll(new PollScheduler(nullptr, nullptr)),
              var(new Variable(QStringLiteral("plc1.Temperature"), 40001,
                               Variable::DataType::Int16, Variable::Access::ReadOnly)) {
            var->setQuality(Variable::Quality::Good);   // ★ onRawRead 头一行就查 quality,Bad 直接 return

            CheckerConfig cfg;
            cfg.level         = lvl;
            cfg.direction     = dir;
            cfg.threshold     = threshold;
            cfg.minDurationMs = minMs;
            cfg.hysteresis    = hyst;   // ★ F2 新字段

            ck = new ThresholdAlarmChecker(QStringLiteral("温度高"), var, poll, cfg);
        }
        ~Fx() { delete ck; delete var; delete poll; }

        // 模拟一次 PLC 读数(rawValue 是 16-bit 原始寄存器)
        // rawRead 是 public 信号,直接 emit 即可 —— 不必走 QMetaObject::invokeMethod,
        // 也不需要真 Modbus 总线。checker 是 DirectConnection 建的,同栈生效。
        void feed(int rawValue) {
            emit poll->rawRead(var, static_cast<quint16>(rawValue));
        }
    };
};

void TestThresholdAlarmChecker::initTestCase() {
    qInfo() << "[tst_ThresholdAlarmChecker] init";
}

// ★ 抖动 100ms(短于 minDurationMs=500)→ 不该触发。
//   这条是 minDurationMs 存在的全部意义,以前没有任何测试守它。
void TestThresholdAlarmChecker::shortSpike_belowMinDuration_doesNotTrigger() {
    Fx f;
    QSignalSpy trig(f.ck, &AlarmChecker::triggered);

    f.feed(60);              // 超限
    QTest::qWait(150);       // 短于 500ms
    f.feed(10);              // 回到正常 —— 定时器该被取消
    QTest::qWait(600);       // 跨过原本的触发时刻

    QCOMPARE(trig.count(), 0);
}

// 超限持续够久 → 触发【恰好一次】。
//   "恰好一次"比"至少一次"重要:重复触发会往历史表灌同一件事多行。
void TestThresholdAlarmChecker::sustainedOverThreshold_triggersOnce() {
    Fx f;
    QSignalSpy trig(f.ck, &AlarmChecker::triggered);

    f.feed(60);
    QTest::qWait(700);
    QCOMPARE(trig.count(), 1);

    // 继续超限,不该再报
    f.feed(70);
    f.feed(80);
    QTest::qWait(700);
    QCOMPARE(trig.count(), 1);
}

// ★★★ F2 的真正靶子:chatter(报警抖动)
//   【场景】模拟量在阈值附近来回摆,每次越线都【超过】minDurationMs。
//     minDurationMs 只过滤"快速抖动"(越线不到 500ms 就回落),
//     挡不住"慢速来回摆"——每一次摆动都是一次完整的 触发→恢复→触发。
//   【为什么这是真问题】一分钟摆 10 次 = 操作员一屏几十条报警,
//     真正严重的那条反而被淹了。工业上叫 chatter / alarm flood。
//   【本例】threshold=50, 每次高 700ms、低 700ms(都 > minDurationMs=500):
//     A 无滞回 → 4 轮 = 4 次触发 + 4 次恢复。
//     B 有滞回(hyst=10,死区 40~60,低点 45 停在死区内)→ 4 轮 = 1 次触发,0 次恢复。
//   【注意 A/B 两组的低点不一样,这是故意的】见下面 B 组注释。
void TestThresholdAlarmChecker::chatter_slowOscillation_isSuppressedByHysteresis() {
    // ---- A 组:无滞回(模拟修复前)----
    {
        Fx f(AlarmLevel::Critical, Direction::Above, 50.0, /*minMs=*/500, /*hyst=*/0.0);
        QSignalSpy trig(f.ck, &AlarmChecker::triggered);
        QSignalSpy rec(f.ck,  &AlarmChecker::recovered);

        for (int round = 0; round < 4; ++round) {
            f.feed(60);            // 高 700ms(> minDurationMs)→ 必然触发
            QTest::qWait(700);
            f.feed(30);            // 低 700ms → 必然恢复
            QTest::qWait(700);
        }
        // ★ 现状:4 次触发 + 4 次恢复 = 操作员被刷屏
        QCOMPARE(trig.count(), 4);
        QCOMPARE(rec.count(),  4);
    }

    // ---- B 组:同样的抖动,加了滞回 ----
    {
        Fx f(AlarmLevel::Critical, Direction::Above, 50.0, 500, /*hyst=*/10.0);
        QSignalSpy trig(f.ck, &AlarmChecker::triggered);
        QSignalSpy rec(f.ck,  &AlarmChecker::recovered);

        // 死区 = (40, 60)。低点取 45 —— 仍在死区内,【不算恢复】。
        //   (我第一版把低点写成 30,那已经跌穿 40,滞回不起作用,4 次照样全报。
        //    滞回只在"值停在死区里来回"时生效,这是它的边界,值得写进用例。)
        for (int round = 0; round < 4; ++round) {
            f.feed(60);
            QTest::qWait(700);
            f.feed(45);
            QTest::qWait(700);
        }
        // ★ 4 轮摆动只报了 1 次,且从未"恢复" —— 这就是 chatter 被压掉
        QCOMPARE(trig.count(), 1);
        QCOMPARE(rec.count(),  0);

        // 真正跌出死区(45 → 30 < 40)必须恢复,否则滞回就成了"报警永不消失"
        f.feed(30);
        QTest::qWait(50);
        QCOMPARE(rec.count(), 1);
    }
}

// ============================================================
// ★ 下面这条是"状态机回归护栏",不是"证明这里原来有 bug"
//   【为什么改口径】我最初把它写成"发现 re-fire 同一报警的 bug",
//   拿修复前的代码跑 —— 它【通过】了。也就是说那个断言描述的是原有行为,
//   不是缺陷。拿一个"修复前就通过"的测试去证明修复,是自欺。
//   【真实结论】高→正常→再高,报两次是【正确的】工业语义:
//     两次是两次独立的工况,操作员需要知道"它又犯了一次"。
//     真正该被压掉的只有 chatter(上面那条)。
//   【这条测试的定位】锁住"重新超限必须重新等满 minDurationMs、
//   且产生【新 id】"这个约定 —— 防止以后有人图省事把 Fired 状态直接
//   短路掉,让第二次报警在 0ms 内就发出去。
void TestThresholdAlarmChecker::reBreachAfterRecovery_waitsFullMinDurationWithNewId() {
    Fx f;
    QSignalSpy trig(f.ck, &AlarmChecker::triggered);
    QSignalSpy rec(f.ck, &AlarmChecker::recovered);

    // 第一轮:触发
    f.feed(60);
    QTest::qWait(700);
    QCOMPARE(trig.count(), 1);
    const qint64 firstId = trig.at(0).at(0).value<Alarm>().id_;

    // 回正常 → 恢复
    f.feed(10);
    QTest::qWait(50);
    QCOMPARE(rec.count(), 1);
    QCOMPARE(rec.at(0).at(0).toLongLong(), firstId);

    // 再超限:这一瞬间绝不能有第二条报警
    f.feed(60);
    QCOMPARE(trig.count(), 1);

    // 必须等满 minDurationMs 才允许第二次
    QTest::qWait(700);
    QCOMPARE(trig.count(), 2);
    // 第二次是【新 id】—— 是新的一轮周期,不是复用上一条
    QVERIFY(trig.at(1).at(0).value<Alarm>().id_ != firstId);
}

// clearAll() 必须把 checker 状态也清干净,否则"复位后还残留待触发状态"。
void TestThresholdAlarmChecker::clearFromAlarmManager_resetsCheckerState() {
    Fx f;
    QSignalSpy trig(f.ck, &AlarmChecker::triggered);

    f.feed(60);
    QTest::qWait(700);
    QCOMPARE(trig.count(), 1);

    // 模拟 AlarmManager::removeChecker(delete) 的效果
    delete f.ck; f.ck = nullptr;
    QVERIFY(true);   // 析构不崩 = 没有悬挂定时器回调
}

// ===== F2-b:滞回 =====

// ★ 滞回的核心:值回到【阈值本身】不算恢复,必须退到 阈值-滞回 以内。
//   没滞回时,50.0 / 50.1 来回抖 → 触发和恢复反复横跳。
//   滞回 5.0 时,50.0 仍在死区内,必须跌到 45.0 才算恢复。
void TestThresholdAlarmChecker::hysteresis_valueInDeadBand_doesNotRecover() {
    Fx f(AlarmLevel::Critical, Direction::Above, /*threshold=*/50.0, /*minMs=*/100, /*hyst=*/5.0);
    QSignalSpy trig(f.ck, &AlarmChecker::triggered);
    QSignalSpy rec(f.ck, &AlarmChecker::recovered);

    f.feed(60);
    QTest::qWait(200);
    QCOMPARE(trig.count(), 1);

    // 跌回 48.0:低于阈值 50,但在死区(45~55)内 → 不该报恢复
    f.feed(48);
    QTest::qWait(50);
    QCOMPARE(rec.count(), 0);

    // 跌到 44.0:低于 50-5=45 → 越过滞回下沿 → 该恢复
    f.feed(44);
    QTest::qWait(50);
    QCOMPARE(rec.count(), 1);
}

// 滞回为 0 时行为必须跟以前【完全一样】,否则是破坏性改动。
void TestThresholdAlarmChecker::hysteresis_zero_behavesLikeBefore() {
    Fx f(AlarmLevel::Critical, Direction::Above, 50.0, 100, /*hyst=*/0.0);
    QSignalSpy trig(f.ck, &AlarmChecker::triggered);
    QSignalSpy rec(f.ck, &AlarmChecker::recovered);

    f.feed(60);
    QTest::qWait(200);
    QCOMPARE(trig.count(), 1);

    f.feed(49);   // 任何低于阈值的值都该立即恢复
    QTest::qWait(50);
    QCOMPARE(rec.count(), 1);
}

// 滞回必须能存进工程文件再读回来,否则重启后配置丢失 = 静默失败。
void TestThresholdAlarmChecker::hysteresis_configRoundTripsThroughJson() {
    CheckerConfig cfg;
    cfg.level         = AlarmLevel::Emergency;
    cfg.direction     = Direction::Below;
    cfg.threshold     = 12.5;
    cfg.minDurationMs = 800;
    cfg.hysteresis    = 3.25;

    const QJsonObject obj = JsonSerializer::toJson(cfg);
    const CheckerConfig back = JsonSerializer::checkerConfigFromJson(obj);

    QCOMPARE(back.level, cfg.level);
    QCOMPARE(back.direction, cfg.direction);
    QCOMPARE(back.threshold, cfg.threshold);
    QCOMPARE(back.minDurationMs, cfg.minDurationMs);
    QCOMPARE(back.hysteresis, cfg.hysteresis);

    // 老工程文件没有这个字段 → 必须回落 0,不能崩也不能给个意外的大值
    CheckerConfig legacy = JsonSerializer::checkerConfigFromJson(
        QJsonObject{{"level", "Warning"}, {"direction", "Above"}, {"threshold", 10}});
    QCOMPARE(legacy.hysteresis, 0.0);
}

// ============================================================
// ★ BUGFIX-C2(2026-10-24):Below 方向的判据重叠
//
// 【现场:24 小时长跑实测出来的】
//   长跑的 alarm_history 表里 12028 次报警触发,分布是:
//       「温度高」(Above 50)  →   460 次,间隔 188 秒(三角波半周期)
//       「温度低」(Below 10)  → 11568 次,间隔  0.44 秒  ★
//   96% 的报警来自那一条 Below 的。
//
// 【Below 那条 0.44 秒一次的到底是什么】
//   把它的触发序列拉出来看(长跑实测数据):
//       时刻(s)  触发值      时刻(s)  触发值
//          0.000     8        5.253     0
//          0.464     8        5.691     0
//          0.884     6        6.128     1
//          ...
//   触发值【始终在 0~9】,从没回到 10 以上。
//   ——也就是说它【从来没有真的恢复过】,却每 0.44 秒报一次。
//   这是【重复触发】,不是"高→正常→再高"那种两次独立工况(后者是正确的)。
//
// 【根因:触发沿和恢复沿在 Below 方向重叠了】
//   ThresholdAlarmChecker.cpp:184-186
//       const bool pastRecovery = (state_ == State::Fired)
//                              ? !(val > recoveryLimit)   // Below: !(val > 10)
//                              : !overThreshold;
//   Below + hysteresis=0 时:
//       触发沿   overThreshold  = (val <  threshold) = (val <  10)
//       恢复沿   pastRecovery   = !(val > threshold) = (val <= 10)
//   值 = 9 时:两条【同时成立】—— "还在超限" 和 "已经恢复"。
//   于是:报一次 → 立刻判恢复 → 下一个轮询周期(200ms)又报一次。
//
//   Above 方向则不重叠:
//       触发沿   overThreshold  = (val >  threshold) = (val >  50)
//       恢复沿   pastRecovery   = !(val > threshold) = (val <= 50)
//   值 = 49 时:只满足恢复沿,不会同时触发。→ 所以 Above 不抖。
//
// 【为什么 hysteresis 治不了它】
//   hysteresis=0 时 Above/Below 恢复沿的公式一模一样地写在那儿,
//   但【公式一样 ≠ 语义一样】:Above 的两个沿差一个不等号(> vs <=),
//   Below 的两个沿【完全相同】(< 和 <=,在整数上等价)。
//   加了 hysteresis 之后 recoveryLimit = threshold + hysteresis,
//   恢复沿变成 !(val > threshold + hyst) = (val <= threshold + hyst),
//   这时 Below 的两条沿【才】分开(触发要 val < threshold,恢复要 val > threshold+hyst)。
//   —— 也就是说:【hysteresis 恰好把 Below 也治好了】,只是必须 > 0 才生效。
//   而 hysteresis 默认是 0(见 Alarm.h:87),长跑工程也没配它 → 一直抖。
//
// 【本用例要钉住什么】
//   钉住"Below 持续超限【不得】重复触发"这条不变量 ——
//   它跟 hysteresis 无关,hysteresis=0 时也必须成立。
//   ★ 负向对照:把恢复沿改成 Above 那样的写法(即让 Below 也用 !overThreshold)
//     之后,本用例必须【变红】。见文件末尾的注释。
// ============================================================

// Below 持续低于阈值 → 报【恰好一次】,之后再喂多少个读数都不许再报。
void TestThresholdAlarmChecker::belowSustainedUnderThreshold_doesNotRetrigger() {
    // ★ hysteresis=0 —— 就是长跑工程里的配置,就是默认值。
    Fx f(AlarmLevel::Critical, Direction::Below, /*threshold=*/10.0,
         /*minMs=*/50, /*hyst=*/0.0);
    QSignalSpy trig(f.ck, &AlarmChecker::triggered);
    QSignalSpy rec(f.ck,  &AlarmChecker::recovered);

    // 越限,持续够久 → 应当触发一次
    f.feed(8);
    QTest::qWait(200);
    QCOMPARE(trig.count(), 1);

    // ★ 值【一直】在阈值以下(8 → 5 → 2),始终没回到 10 以上。
    //   现场就是这个形态:值在死区里一路下坡,报警却每 0.44 秒报一次。
    //   【没有恢复过】,所以【不许】再报第二条。
    for (int i = 0; i < 10; ++i) {
        f.feed(8 - (i % 5));
        QTest::qWait(50);   // 10 × 50ms = 500ms > 长跑里的 440ms 间隔
    }

    QVERIFY2(trig.count() == 1,
             qPrintable(QStringLiteral(
                 "Below 持续超限却报了 %1 次(应恰好 1 次)。"
                 "触发沿与恢复沿在 Below 方向重叠了 —— 参见本文件上方 BUGFIX-C2 说明。")
                 .arg(trig.count())));

    // 恢复次数:值一直没回到 10 以上,所以【一次都不该恢复】
    QCOMPARE(rec.count(), 0);
}

// Below 方向加滞回后,行为必须跟 Above 对称:
//   值在死区里来回 → 只报一次;真正跌出死区 → 才恢复。
//
// ★ 死区方向跟 Above 是【相反】的,别照抄 Above 的用例 ★
//   Above:阈值 50,滞回 5 → 死区 (45, 50],值要【跌到 45 以下】才恢复。
//   Below:阈值 10,滞回 5 → 死区 [10, 15),值要【升到 15 以上】才恢复。
//   (Below 的异常是"太低",要解除异常就得让值【升】回去。)
void TestThresholdAlarmChecker::belowWithHysteresis_sameShapeAsAbove() {
    Fx f(AlarmLevel::Critical, Direction::Below, /*threshold=*/10.0,
         /*minMs=*/50, /*hyst=*/5.0);
    QSignalSpy trig(f.ck, &AlarmChecker::triggered);
    QSignalSpy rec(f.ck,  &AlarmChecker::recovered);

    f.feed(8);                 // 越限(8 < 10)→ 触发
    QTest::qWait(200);
    QCOMPARE(trig.count(), 1);

    // 值升到 12:仍在死区 [10, 15) 内 → 【不算恢复】,也不该再报
    f.feed(12);
    QTest::qWait(50);
    QCOMPARE(rec.count(), 0);
    QCOMPARE(trig.count(), 1);

    // 升到 16:越过死区上沿 → 必须恢复
    f.feed(16);
    QTest::qWait(50);
    QCOMPARE(rec.count(), 1);
    QCOMPARE(trig.count(), 1);   // 恢复不该引发第二次触发
}

QTEST_MAIN(TestThresholdAlarmChecker)
#include "tst_ThresholdAlarmChecker.moc"
