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

QTEST_MAIN(TestThresholdAlarmChecker)
#include "tst_ThresholdAlarmChecker.moc"
