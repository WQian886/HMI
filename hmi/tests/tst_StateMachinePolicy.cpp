// ============================================================================
// tst_StateMachinePolicy.cpp —— 业务状态机的【判据】不许变
//
// ============================================================================
// 【这个测试是为哪条问题写的】
//   2026-11-02(G-28):状态迁移表以前整张写在 hmi.h / hmi.cpp 里,
//   而它们不在 hmi_core 里 ⇒ 没有任何一条单元测试能看见它。
//
//   它是 IMPROVE-5.1(2026-08-03 Wqian 拍板)引入的,
//   设计一直是对的(所有状态变化走 tryTransition 查表,"非法迁移"这个概念存在),
//   但它【只有设计,没有钉子】——
//   G-26(2026-11-02)就是在这张表旁边出的事:
//   onAlarmTriggered 里【无条件】tryTransition(AlarmCritical),
//   判据写在 hmi.cpp 里没人看得见 ⇒ 一条 Info 报警就把机器打成 Fault。
//
//   ★ 同一个形状的两次事故(G-26 / G-28),同一个解法:
//     判据从主画面搬到公共层,再给它写测试。
//
// 【本文件钉住什么】
//   ① ★ 整张真值表:4 状态 × 6 触发源 = 24 种组合,【一种一种】写死。
//      —— 表里【多一条】或【少一条】,本文件都会红。
//   ② 表的条目数(11 条)—— 防止"顺手多加一条"混进来。
//   ③ AlarmCleared 全表没有 —— 这是【设计决定】,不是漏写。
//   ④ 非法迁移【不碰】out 参数 —— 契约,不是实现细节。
//   ⑤ hmi.cpp 真的在用这个共享模块(源码文本断言)。
//
//   关联:src/state/StateMachinePolicy.{h,cpp}、hmi/hmi.h、hmi/hmi.cpp
// ============================================================================

#include <QtTest>

#include <QFile>
#include <QRegularExpression>
#include <QString>
#include <QStringList>

#include "state/StateMachinePolicy.h"

using qwhmi::state::MachineState;
using qwhmi::state::MachineTrigger;
using qwhmi::state::nextState;
using qwhmi::state::transitionTable;

namespace {

// ★ 全部状态 / 全部触发源(顺序固定,真值表按这个顺序写)
const MachineState kAllStates[] = {
    MachineState::Stopped,
    MachineState::Running,
    MachineState::Resetting,
    MachineState::Fault,
};

const MachineTrigger kAllTriggers[] = {
    MachineTrigger::StartButton,
    MachineTrigger::StopButton,
    MachineTrigger::ResetButton,
    MachineTrigger::ResetTimerFinished,
    MachineTrigger::AlarmCritical,
    MachineTrigger::AlarmCleared,
};

// "非法"的标记:用 to 是不是这个哨兵值来表示
constexpr MachineState kIllegal = static_cast<MachineState>(-1);

struct Expect {
    bool  legal;        // 这一格是不是合法迁移
    MachineState to;    // 合法时的目标状态(非法时是 kIllegal)
};

// ★★★ 真值表 ★★★
//   行 = 当前状态,列 = 触发源。kIllegal = 非法迁移(状态不变)。
//
//   这张表是本文件的【核心】—— 它是"原来那张迁移表"的逐条转写。
//   任何一个格子对不上,测试就红。
//
//   逐条对照原表(11 条合法迁移):
//     Stopped   : Start→Running  Stop→Stopped  Reset→Resetting  AlarmCritical→Fault
//     Running   : Stop→Stopped   Reset→Resetting              AlarmCritical→Fault
//     Resetting : Reset→Resetting  ResetTimer→Stopped           AlarmCritical→Fault
//     Fault     : Reset→Stopped
const Expect kTruthTable[4][6] = {
    // Start  Stop  Reset  Timer  AlarmCrit  AlarmClr
    {  {true,  MachineState::Running},
       {true,  MachineState::Stopped},
       {true,  MachineState::Resetting},
       {false, kIllegal},
       {true,  MachineState::Fault},
       {false, kIllegal} },                                  // Stopped
    {  {false, kIllegal},
       {true,  MachineState::Stopped},
       {true,  MachineState::Resetting},
       {false, kIllegal},
       {true,  MachineState::Fault},
       {false, kIllegal} },                                  // Running
    {  {false, kIllegal},
       {false, kIllegal},
       {true,  MachineState::Resetting},
       {true,  MachineState::Stopped},
       {true,  MachineState::Fault},
       {false, kIllegal} },                                  // Resetting
    {  {false, kIllegal},
       {false, kIllegal},
       {true,  MachineState::Stopped},
       {false, kIllegal},
       {false, kIllegal},
       {false, kIllegal} },                                  // Fault
};

// 去掉注释再比对 —— hmi.cpp / hmi.h 的注释里会提到这些名字
// (说明它们为什么被搬走),不去掉就会把"注释提到了"当成"代码还在",
// 测试永远绿。手法同 tst_CapabilityReport.cpp / tst_MenuBar.cpp。
QString codeOnly(const QString& text) {
    QString out = text;
    out.remove(QRegularExpression(QStringLiteral("/\\*.*?\\*/"),
                                  QRegularExpression::DotMatchesEverythingOption));
    out.remove(QRegularExpression(QStringLiteral("//[^\n]*")));
    return out;
}

// 从源码里抠出某个函数的函数体(按大括号配平)。
static QString extractFunctionBody(const QString& code, const QString& signature) {
    const int sigAt = code.indexOf(signature);
    if (sigAt < 0) return QString();
    const int openAt = code.indexOf(QLatin1Char('{'), sigAt);
    if (openAt < 0) return QString();
    int depth = 0;
    for (int i = openAt; i < code.size(); ++i) {
        const QChar c = code.at(i);
        if (c == QLatin1Char('{')) {
            ++depth;
        } else if (c == QLatin1Char('}')) {
            --depth;
            if (depth == 0) return code.mid(openAt + 1, i - openAt - 1);
        }
    }
    return QString();
}

QString cellLabel(MachineState s, MachineTrigger t) {
    return QStringLiteral("%1 --(%2)-->")
        .arg(qwhmi::state::machineStateName(s), qwhmi::state::machineTriggerName(t));
}

} // namespace

class tst_StateMachinePolicy : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();

    // ---- ① ★ 整张真值表 ----
    void theWholeTruthTable_isPinned();

    // ---- ② 表的条目数 ----
    void tableHasExactlyElevenEntries();

    // ---- ③ AlarmCleared:故意没有迁移 ----
    void alarmCleared_hasNoEntryAtAll();

    // ---- ④ 非法迁移不碰 out ----
    void illegalTransition_leavesTheOutParamUntouched();

    // ---- ⑤ 几条单独拎出来的重要语义 ----
    void runningPlusStartButton_isIllegal();
    void faultPlusResetButton_goesStraightToStopped();
    void alarmCritical_tripsToFault_fromEveryLiveState();

    // ---- ⑥ 名字函数不许重样 ----
    void names_areDistinctAndNotEmpty();

    // ---- ⑦ 接线:hmi.cpp 真的在用这个共享模块 ----
    void hmiSource_usesTheSharedPolicy();

    // ---- ⑧ 迁移留痕那行的格式 ----
    void transitionTrace_hasTheExactFormatTheJudgesCount();

    // ---- ⑨ ★ 留痕那一行不许再被打回 DEBUG 级别 ★ ----
    void theTrace_isNotWrittenAtDebugLevel();
};

void tst_StateMachinePolicy::initTestCase() {
    // 一次都不该在 initTestCase 里判红 —— 判据全在各条用例里。
}

// ★★★ ① 整张真值表 ★★★
void tst_StateMachinePolicy::theWholeTruthTable_isPinned() {
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 6; ++j) {
            const MachineState from = kAllStates[i];
            const MachineTrigger t  = kAllTriggers[j];
            const Expect e = kTruthTable[i][j];
            const QString label = cellLabel(from, t);

            MachineState out = kIllegal;   // 初值故意设成一个"不可能是结果"的值
            const bool legal = nextState(from, t, &out);

            QVERIFY2(legal == e.legal,
                     qPrintable(QStringLiteral("★ 迁移合法性变了:%1 —— 期望 %2,实际 %3。"
                                               " 表里多一条/少一条,现场行为就变了。")
                                .arg(label,
                                     e.legal ? QStringLiteral("合法") : QStringLiteral("非法"),
                                     legal ? QStringLiteral("合法") : QStringLiteral("非法"))));

            if (!e.legal) {
                continue;   // 非法迁移的 out 由另一条用例专门盯
            }
            QVERIFY2(out == e.to,
                     qPrintable(QStringLiteral("★ 目标状态变了:%1 —— 期望 %2,实际 %3。")
                                .arg(label,
                                     qwhmi::state::machineStateName(e.to),
                                     qwhmi::state::machineStateName(out))));
        }
    }
}

// ★★★ ② 表的条目数 ★★★
void tst_StateMachinePolicy::tableHasExactlyElevenEntries() {
    // ★ 为什么单独数一遍:
    //   真值表只说"这 24 格里哪些合法",它【不】能区分
    //   "表里有 11 条"和"表里有 11 条但其中一条是重复的键"。
    //   QHash 的键唯一,重复的键会【覆盖】前一条 ⇒ 条目数不变但行为变了。
    //   所以这一条把"表里到底有几条"也钉住。
    QCOMPARE(transitionTable().size(), 11);
}

// ★★★ ③ AlarmCleared:故意没有迁移 ★★★
void tst_StateMachinePolicy::alarmCleared_hasNoEntryAtAll() {
    // ★ 这一条是【设计决定】,不是"待补的坑":
    //   抢停机后不自动恢复,必须人工按复位。
    //   反过来做(报警一恢复就自动回 Stopped)的话,机器会在条件反复越限的
    //   情况下自己爬起来、跑两秒、又自己停 —— 那比停着更危险。
    //
    //   ⇒ 有人哪天"顺手"给 AlarmCleared 加一条迁移,机器就会在报警反复时
    //     自己爬起来 —— 这是【真会出事】的方向,所以专门钉住。
    for (int i = 0; i < 4; ++i) {
        const MachineState from = kAllStates[i];
        MachineState out = MachineState::Stopped;
        const bool legal = nextState(from, MachineTrigger::AlarmCleared, &out);
        QVERIFY2(!legal,
                 qPrintable(QStringLiteral("★ 表里多了一条 AlarmCleared 迁移(%1 → %2)—— "
                                           "抢停机之后机器会【自己爬起来】,"
                                           "条件反复越限时它会跑两秒又自己停,比停着更危险。")
                                .arg(qwhmi::state::machineStateName(from),
                                     qwhmi::state::machineStateName(out))));
    }
    // 而且表里一个 AlarmCleared 的键都不该有
    for (auto it = transitionTable().cbegin(); it != transitionTable().cend(); ++it) {
        QVERIFY2(it.key().second != MachineTrigger::AlarmCleared,
                 "★ 迁移表里出现了 AlarmCleared 的键 —— 见本用例的注释:这是故意缺的。");
    }
}

// ★★★ ④ 非法迁移不碰 out ★★★
void tst_StateMachinePolicy::illegalTransition_leavesTheOutParamUntouched() {
    // ★ 契约:"返回 false 时绝不碰 out"。
    //   为什么这条值得钉:
    //   调用方"忘了检查返回值"是极常见的写法 ——
    //   如果非法迁移顺手把 out 写成了某个状态(比如初值 Stopped),
    //   忘了检查返回值的调用方就会拿着一个【假的】目标状态继续跑。
    //   这跟"返回值语义"是两回事:返回值可能被忽略,out 参数被污染却是隐蔽的。
    MachineState out = MachineState::Fault;   // 故意放一个"看起来像结果"的值
    const bool legal = nextState(MachineState::Running, MachineTrigger::StartButton, &out);
    QVERIFY2(!legal, "★ Running + StartButton 应该是非法迁移");
    QVERIFY2(out == MachineState::Fault,
             "★ 非法迁移把 out 改了 —— 契约是'返回 false 时绝不碰 out'。"
             "忘了检查返回值的调用方会拿着这个假状态继续跑。");

    // 传 nullptr 也不能崩
    const bool legal2 = nextState(MachineState::Running, MachineTrigger::StartButton, nullptr);
    QVERIFY2(!legal2, "★ Running + StartButton 应该是非法迁移");
}

// ---- ⑤ 单独拎出来的重要语义 ------------------------------------------

void tst_StateMachinePolicy::runningPlusStartButton_isIllegal() {
    // 连按两次"启动"不会把机器开两遍。
    MachineState out = kIllegal;
    QVERIFY2(!nextState(MachineState::Running, MachineTrigger::StartButton, &out),
             "★ 运行中再按启动变成了合法迁移 —— 连按两次启动会把机器重开一遍。");
}

void tst_StateMachinePolicy::faultPlusResetButton_goesStraightToStopped() {
    // ★ 故障态复位是【直接】回 Stopped,【不】经过 Resetting ——
    //   所以按复位不会起那个 3 秒定时器,hmi.cpp 里
    //   `if (tryTransition(ResetButton) && state == State::Resetting)` 那个判断
    //   才成立(定时器只在 Stopped/Running 复位时才起)。
    MachineState out = kIllegal;
    QVERIFY2(nextState(MachineState::Fault, MachineTrigger::ResetButton, &out),
             "★ 故障态按复位应该是合法迁移(只能靠它清故障)");
    QVERIFY2(out == MachineState::Stopped,
             qPrintable(QStringLiteral("★ 故障态复位的目标状态变成了 %1 —— "
                                       "应该是直接回 Stopped(不等 3 秒定时器,"
                                       "避免长时间卡在故障态)。")
                            .arg(qwhmi::state::machineStateName(out))));
}

void tst_StateMachinePolicy::alarmCritical_tripsToFault_fromEveryLiveState() {
    // ★ 三个"活着"的态都能被严重报警打成故障。
    //   (Fault 态再来一次严重报警是【非法】的:不该"再迁移一次"。)
    const MachineState liveStates[] = {
        MachineState::Stopped, MachineState::Running, MachineState::Resetting};
    for (MachineState from : liveStates) {
        MachineState out = kIllegal;
        QVERIFY2(nextState(from, MachineTrigger::AlarmCritical, &out),
                 qPrintable(QStringLiteral("★ %1 态遇到严重报警应该被打成故障")
                                .arg(qwhmi::state::machineStateName(from))));
        QVERIFY2(out == MachineState::Fault,
                 qPrintable(QStringLiteral("★ %1 态遇到严重报警,目标状态是 %2 —— 应该是 Fault")
                                .arg(qwhmi::state::machineStateName(from),
                                     qwhmi::state::machineStateName(out))));
    }
    // 而 Fault 态再来一次:非法(不重复迁移)
    MachineState out = kIllegal;
    QVERIFY2(!nextState(MachineState::Fault, MachineTrigger::AlarmCritical, &out),
             "★ 已经在故障态了,再来一次严重报警不该再迁移一次。");
}

// ---- ⑥ 名字函数不许重样 ----------------------------------------------

void tst_StateMachinePolicy::names_areDistinctAndNotEmpty() {
    // ★ 这两个函数只用于日志和测试报错信息 —— 但它们一旦写重,
    //   日志里就会打出 "Stopped --> Stopped" 这种看不出是哪条迁移的行,
    //   而那正是现场查状态机问题时的唯一线索。
    QSet<QString> stateNames;
    for (MachineState s : kAllStates) {
        const QString n = qwhmi::state::machineStateName(s);
        QVERIFY2(!n.isEmpty(), "★ 状态名是空串");
        QVERIFY2(n != QStringLiteral("?"), "★ 某个状态没对上名字(枚举加了新的但 switch 没加)");
        QVERIFY2(!stateNames.contains(n),
                 qPrintable(QStringLiteral("★ 两个状态用了同一个名字:%1").arg(n)));
        stateNames.insert(n);
    }
    QCOMPARE(stateNames.size(), 4);

    QSet<QString> trigNames;
    for (MachineTrigger t : kAllTriggers) {
        const QString n = qwhmi::state::machineTriggerName(t);
        QVERIFY2(!n.isEmpty(), "★ 触发源名是空串");
        QVERIFY2(n != QStringLiteral("?"), "★ 某个触发源没对上名字");
        QVERIFY2(!trigNames.contains(n),
                 qPrintable(QStringLiteral("★ 两个触发源用了同一个名字:%1").arg(n)));
        trigNames.insert(n);
    }
    QCOMPARE(trigNames.size(), 6);
}

// ---- ⑦ 接线:hmi.cpp 真的在用这个共享模块 ----------------------------

void tst_StateMachinePolicy::hmiSource_usesTheSharedPolicy() {
    const QString dir = QStringLiteral(HMI_SRC_DIR_STRING);

    // ---- hmi.cpp ----
    QFile cppFile(dir + QStringLiteral("/hmi.cpp"));
    QVERIFY2(cppFile.open(QIODevice::ReadOnly | QIODevice::Text),
             qPrintable(QStringLiteral("读不到 hmi.cpp")));
    const QString cpp = codeOnly(QString::fromUtf8(cppFile.readAll()));

    const QString body = extractFunctionBody(cpp, QStringLiteral("bool hmi::tryTransition"));
    QVERIFY2(!body.isEmpty(),
             "★ hmi.cpp 里找不到 tryTransition 的函数体 —— 它被改名或挪走了?");

    QVERIFY2(body.contains(QStringLiteral("qwhmi::state::nextState")),
             "★ hmi.cpp 的 tryTransition 里没有调 qwhmi::state::nextState —— "
             "判据又被搬回主窗口里硬编码了,而那正是它当初开始出事的原因"
             "(hmi.cpp 不在 hmi_core 里,没人能测)。");

    // ★ 而且 hmi.cpp 里【不许】再有自己的一张表
    QVERIFY2(!cpp.contains(QStringLiteral("kTransitionTable")),
             "★ hmi.cpp 里还有 kTransitionTable —— "
             "现在那张表住在 src/state/StateMachinePolicy.cpp 里。"
             "两处各有一张表,迟早走偏(而这一张以前是假的)。");

    // ---- hmi.h ----
    QFile hFile(dir + QStringLiteral("/hmi.h"));
    QVERIFY2(hFile.open(QIODevice::ReadOnly | QIODevice::Text),
             qPrintable(QStringLiteral("读不到 hmi.h")));
    const QString h = codeOnly(QString::fromUtf8(hFile.readAll()));

    QVERIFY2(!h.contains(QStringLiteral("kTransitionTable")),
             "★ hmi.h 里还声明着 kTransitionTable —— "
             "那张表已经搬到公共层了,hmi 这边只留别名。");

    // ★ 枚举必须是指向公共层的别名 —— 不是各自一份
    QVERIFY2(h.contains(QStringLiteral("using State = qwhmi::state::MachineState")),
             "★ hmi.h 里的 State 不是公共层那个的别名 —— "
             "枚举要是不搬过去,公共层就没法查表。");
    QVERIFY2(h.contains(QStringLiteral("using Trigger = qwhmi::state::MachineTrigger")),
             "★ hmi.h 里的 Trigger 不是公共层那个的别名。");
}

// ---- ⑧ 迁移留痕那行的格式 ----------------------------------------------

void tst_StateMachinePolicy::transitionTrace_hasTheExactFormatTheJudgesCount() {
    // ★ 这个格式是【判据的一部分】,不是随手写的:
    //   进程级判据 J13 靠正则数"进了 Fault 的迁移行":
    //       \[状态机\].*--\(AlarmCritical\)-->\s*Fault
    //   格式一改,判据就数不到了 ——
    //   而"判据数不到"在报告里跟"事情没发生"长得一模一样。
    //   ⇒ 格式由公共层出(transitionTrace),这里逐字钉住。
    QCOMPARE(qwhmi::state::transitionTrace(MachineState::Stopped,
                                           MachineTrigger::AlarmCritical,
                                           MachineState::Fault),
             QStringLiteral("[状态机] Stopped --(AlarmCritical)--> Fault"));
    QCOMPARE(qwhmi::state::transitionTrace(MachineState::Running,
                                           MachineTrigger::StopButton,
                                           MachineState::Stopped),
             QStringLiteral("[状态机] Running --(StopButton)--> Stopped"));

    // 而且它必须真的能被判据用的那条正则命中
    const QRegularExpression re(
        QStringLiteral("\\[(状态机)\\].*--\\(([^)]*)\\)-->\\s*(\\w+)"));
    const auto m = re.match(qwhmi::state::transitionTrace(
        MachineState::Stopped, MachineTrigger::AlarmCritical, MachineState::Fault));
    QVERIFY2(m.hasMatch(),
             "★ 迁移留痕那行匹配不上判据 J13 用的正则 —— 判据会数不到,"
             "而'数不到'会被读成'没迁移'。");
    QCOMPARE(m.captured(1), QStringLiteral("状态机"));
    QCOMPARE(m.captured(2), QStringLiteral("AlarmCritical"));
    QCOMPARE(m.captured(3), QStringLiteral("Fault"));
}

// ---- ⑨ ★ 留痕那一行不许再被打回 DEBUG 级别 ★ ---------------------------

void tst_StateMachinePolicy::theTrace_isNotWrittenAtDebugLevel() {
    // ★★★ 这一条是 2026-11-02 实测踩出来的 ★★★
    //
    //   迁移留痕那一行,第一版写的是 qCDebug(hmiApp)。
    //   而默认日志配置是 { "logging": { "default_level": "info" } },
    //   hmi.app 没有单独的级别覆盖 ⇒ 这一行 severity=Debug(0) < Info(1)
    //   ⇒ 【一行都不落盘】。
    //
    //   后果不是"少一行日志",是【判据开始骗人】:
    //     短跑判据 J13 数"进了 Fault 的迁移行",数到 0 ⇒ 红;
    //     而那个"0"其实分不清是
    //       (a) 状态【真的】没切,
    //       (b) 状态切了,但证据被级别过滤【扔了】。
    //   第二轮更糟:0 条迁移行 ⇒ "一次都没进 Fault"成立 ⇒ 判成【绿】。
    //   ⇒ 整条通路坏掉,判据照样绿 —— 这就是假绿。
    //
    //   ⇒ 所以把级别钉住:迁移留痕必须在 INFO / WARNING 上打。
    const QString dir = QStringLiteral(HMI_SRC_DIR_STRING);
    QFile cppFile(dir + QStringLiteral("/hmi.cpp"));
    QVERIFY2(cppFile.open(QIODevice::ReadOnly | QIODevice::Text),
             qPrintable(QStringLiteral("读不到 hmi.cpp")));
    const QString cpp = codeOnly(QString::fromUtf8(cppFile.readAll()));

    const QString body = extractFunctionBody(cpp, QStringLiteral("bool hmi::tryTransition"));
    QVERIFY2(!body.isEmpty(),
             "★ hmi.cpp 里找不到 tryTransition 的函数体 —— 它被改名或挪走了?");

    // ★ 只钉【级别】,不钉调用长相:.noquote() 是装饰,改了不该红;
    //   从 INFO/WARNING 改成 DEBUG 才该红。
    const auto emittedAt = [&body](const QString& level) {
        return QRegularExpression(
            QStringLiteral("qC%1\\(hmiApp\\)(\\.noquote\\(\\))?\\s*<<\\s*trace").arg(level))
            .match(body).hasMatch();
    };
    QVERIFY2(!emittedAt(QStringLiteral("Debug")),
             "★ 迁移留痕那一行又被打回 DEBUG 级别了 —— "
             "默认日志级别是 info,DEBUG 一行都不落盘。"
             "于是'迁移行 0 条'既可能是'没迁移',也可能是'迁移了但没落盘',"
             "这两件事在日志里长得一模一样 —— 判据会开始骗人。");

    QVERIFY2(emittedAt(QStringLiteral("Info")),
             "★ tryTransition 里没有用 INFO 打迁移留痕 —— "
             "普通迁移(启动/停止/复位)在默认日志级别下就看不见了。");

    QVERIFY2(emittedAt(QStringLiteral("Warning")),
             "★ tryTransition 里没有用 WARNING 打迁移留痕 —— "
             "进 Fault 是现场事件,应该是报警级别的(默认 info 之上也要看得见)。");

    // ★ 还有"状态机就绪"那一行 —— 它是判据 J13a 的【阳性对照】:
    //   没有它,"这一路上没有迁移"就是空断言 ——
    //   通路整个坏掉时,判据照样是绿的。
    QVERIFY2(cpp.contains(QStringLiteral("[状态机就绪]")),
             "★ hmi.cpp 里没有『[状态机就绪]』那一行 —— "
             "判据 J13a 靠它证明'状态机这一路的日志是通的'。"
             "没有它,通路整个坏掉的时候判据照样是绿的。");
}

QTEST_MAIN(tst_StateMachinePolicy)
#include "tst_StateMachinePolicy.moc"
