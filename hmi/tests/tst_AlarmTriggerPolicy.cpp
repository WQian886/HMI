// ============================================================================
// tst_AlarmTriggerPolicy.cpp —— G-26(2026-11-02):"这条报警要不要抢停机"
//
// ============================================================================
// 【这个测试是为哪条 bug 写的】
//   G-26:【任何等级的报警都会把整台机器推进 Fault】。
//   一条 Info 级报警 → counter 定时器停下、开始/停止按钮全灰、
//   状态标签变"状态:故障"。现场表现是"机器莫名其妙停了"。
//   而报警表、声音、审计日志一切正常 —— 没有任何地方说"是这条报警停的机"。
//
// 【为什么它能活到今天】—— 这条断言存在的理由
//   hmi.cpp 【不在 hmi_core 里】⇒ 单测链接不到主窗口
//   ⇒ "抢不停机"这件事没有任何一条测试看得见。
//   跟 G-14 / BUGFIX-C3(主画面报警表整张废了,68 个测试全绿)同一个形状。
//   所以本文件分两部分:
//     ① 判据本身(纯函数,可直接调)
//     ② hmi.cpp 源码文本(确认它真的【调了】那个判据,而不是自己写一份)
//
// 【② 为什么用读源码文本这种"土办法"】
//   仓库里 tst_MenuBar / tst_Theme / tst_ModbusPanel / tst_ActionWrite
//   都在用同一招,原因也一样:主窗口那套逻辑【一条测试都链接不到】,
//   读源码文本是唯一能把"hmi.cpp 有没有真的用这个函数"钉住的办法。
//   ★ 诚实的边界:它抓得住"没调 / 调错",抓不住"调了但 alarmMgr_ 是空的"
//     —— 那种情况由构造函数顺序保证。
//
// 【负向对照】
//   scripts/negctl_alarm_trip.py:两个变异都必须让本文件变红 ——
//     M1 判据函数改成恒返回 true(→"Info 不该抢停机"那条红)
//     M2 hmi.cpp 改成无条件 tryTransition(→"源码文本"那条红)
//   没有负向对照的测试,只是一段看起来在测东西的代码。
//
// 关联:src/alarm/AlarmTriggerPolicy.h(设计说明)、hmi.cpp(onAlarmTriggered)
// ============================================================================

#include <QtTest>

#include <QFile>
#include <QRegularExpression>
#include <QString>

#include "Alarm.h"
#include "alarm/AlarmTriggerPolicy.h"

using qwhmi::alarm::shouldTripMachine;

// ---------------------------------------------------------------------------
// 去掉注释再比对 —— hmi.cpp 的注释里满是 "shouldTripMachine" 这个名字
// (说明它为什么被抽出来),不去掉就会把"注释提到了"当成"代码还在",
// 测试永远绿。手法同 tst_MenuBar.cpp:947 / tst_ModbusPanel.cpp:90。
// ---------------------------------------------------------------------------
static QString codeOnly(const QString& text) {
    QString out = text;
    out.remove(QRegularExpression(QStringLiteral("/\\*.*?\\*/"),
                                  QRegularExpression::DotMatchesEverythingOption));
    out.remove(QRegularExpression(QStringLiteral("//[^\n]*")));
    return out;
}

static int countOf(const QString& text, const QString& needle) {
    int n = 0, i = 0;
    while ((i = text.indexOf(needle, i)) >= 0) { ++n; i += needle.size(); }
    return n;
}

// ★ 从源码里抠出某个函数的函数体(按大括号配平)。
//   不抠出函数体的话,整个 hmi.cpp 里的任何一个字都能满足断言 ——
//   那等于没断言。
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
    return QString();   // 大括号没配平 —— 返回空,让断言去报"找不到函数体"
}

class tst_AlarmTriggerPolicy : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();

    // ---- ① 判据本身 ----
    void info_doesNotTrip();
    void warning_doesNotTrip();
    void critical_trips();
    void emergency_trips();
    void everyLevel_hasAStableAnswer();

    // ★ 枚举顺序 = 严重程度顺序。判据写成 ">=" 就依赖这条。
    //   哪天有人调整了 enum 的顺序而没改判据,机器就会在别的档位上停机。
    void severityOrder_isAscending();

    // ---- ② hmi.cpp 真的用了这个判据吗 ----
    void hmiSource_tripsOnlyThroughTheSharedPolicy();
    void hmiSource_doesNotAutoResetOnAlarmRecovery();
};

void tst_AlarmTriggerPolicy::initTestCase() {
    // ★ 这条测试钉的是"机器会不会因为一条小报警停掉"。
    //   如果哪天判据整个反过来了(变成 Info 也抢停机),本文件会红 ——
    //   那正是它该干的事。
}

void tst_AlarmTriggerPolicy::info_doesNotTrip() {
    // ★ 信息级报警只是"告诉操作员发生了什么"。
    //   抢停机是严重/危急的事 —— 见 AlarmTriggerPolicy.h 的【取舍】一节。
    QVERIFY2(!shouldTripMachine(AlarmLevel::Info),
             "★ Info(信息)级报警把机器抢停了。"
             "现场表现是『机器莫名其妙停了』,而报警表上只写着一条『信息』。");
}

void tst_AlarmTriggerPolicy::warning_doesNotTrip() {
    // ★ 警告级不停机 —— 这是本条 bug 最常被踩到的形态。
    //   用户配一条"温度偏高 3 度"就停机,现场很快会学会忽略报警。
    QVERIFY2(!shouldTripMachine(AlarmLevel::Warning),
             "★ Warning(警告)级报警把机器抢停了。"
             "警告是『请注意』,抢停机是严重/危急的事。");
}

void tst_AlarmTriggerPolicy::critical_trips() {
    // ★ 严重级【必须】抢停机 —— 这是 G-26 修完之后不能丢的那一半。
    //   修之前它是靠"所有等级都停"顺带对的,现在必须自己站得住。
    QVERIFY2(shouldTripMachine(AlarmLevel::Critical),
             "★ Critical(严重)级报警【没有】抢停机 —— 机器该停的时候没停。"
             "设计稿(docs/归档/2026-08-03_LEARNING_NOTES.md:219):"
             "『| 任何 | AlarmCritical | Fault | 严重报警抢占 |』。"
             "这条不能因为修了 G-26 就一起丢了。");
}

void tst_AlarmTriggerPolicy::emergency_trips() {
    // ★ 危急级比严重级更重,当然也要抢停机。
    //   这条同时钉住"判据是 >= 而不是 =="。
    QVERIFY2(shouldTripMachine(AlarmLevel::Emergency),
             "★ Emergency(危急)级报警【没有】抢停机 —— "
             "危急比严重更重,不可能不抢停。");
}

void tst_AlarmTriggerPolicy::everyLevel_hasAStableAnswer() {
    // ★ 四档都过一遍,答"抢/不抢"必须都有明确答案 —— 不许有"未知/未定义"那种洞。
    //   (实现里如果写成查表而漏了一档,漏掉的那档会走到 default 分支。)
    const AlarmLevel all[] = {
        AlarmLevel::Info, AlarmLevel::Warning,
        AlarmLevel::Critical, AlarmLevel::Emergency
    };
    for (AlarmLevel l : all) {
        const bool trip = shouldTripMachine(l);
        QVERIFY2(trip == true || trip == false, "判据返回了非布尔值");
    }
    // 两档抢两档不抢 —— 数量对不上说明某一档的归属被改动过
    int trips = 0;
    for (AlarmLevel l : all) if (shouldTripMachine(l)) ++trips;
    QCOMPARE(trips, 2);
}

void tst_AlarmTriggerPolicy::severityOrder_isAscending() {
    // ★ 判据写成 "level >= AlarmLevel::Critical",依赖的就是"枚举顺序 = 严重程度顺序"。
    //   哪天有人为了别的理由调了 enum 的顺序,机器就会在别的档位上停机。
    QVERIFY2(static_cast<int>(AlarmLevel::Info)
                  < static_cast<int>(AlarmLevel::Warning),
             "AlarmLevel 的枚举顺序变了:Info 必须比 Warning 轻。");
    QVERIFY2(static_cast<int>(AlarmLevel::Warning)
                  < static_cast<int>(AlarmLevel::Critical),
             "AlarmLevel 的枚举顺序变了:Warning 必须比 Critical 轻。");
    QVERIFY2(static_cast<int>(AlarmLevel::Critical)
                  < static_cast<int>(AlarmLevel::Emergency),
             "AlarmLevel 的枚举顺序变了:Critical 必须比 Emergency 轻。");
}

// ---------------------------------------------------------------------------
// ② hmi.cpp 真的用了这个判据吗
// ---------------------------------------------------------------------------

void tst_AlarmTriggerPolicy::hmiSource_tripsOnlyThroughTheSharedPolicy() {
    const QString path = QStringLiteral(HMI_SRC_DIR_STRING) + QStringLiteral("/hmi.cpp");
    QFile f(path);
    QVERIFY2(f.open(QIODevice::ReadOnly | QIODevice::Text),
             qPrintable(QStringLiteral("读不到 hmi.cpp: ") + path));
    const QString code = codeOnly(QString::fromUtf8(f.readAll()));

    // ★ 抠出 onAlarmTriggered 的函数体 —— 不抠的话整个 hmi.cpp 里的
    //   任何一个字都能满足下面的断言,那等于没断言。
    const QString body = extractFunctionBody(code, QStringLiteral("void hmi::onAlarmTriggered"));
    QVERIFY2(!body.isEmpty(),
             "★ hmi.cpp 里找不到 onAlarmTriggered 的函数体 —— "
             "主窗口那个槽被改名或挪走了?那抢停机这条线就没有唯一入口了。");

    // ★ 必须真的调了共享判据
    const int iGuard = body.indexOf(QStringLiteral("shouldTripMachine"));
    QVERIFY2(iGuard >= 0,
             "★ hmi.cpp 的 onAlarmTriggered 里没有调 qwhmi::alarm::shouldTripMachine。"
             "那它就是【无条件】抢停机 —— 一条 Info 报警也能让整台机器停,"
             "而本文件的行为部分照样全绿(因为本文件自己调了共享函数)。"
             "这正是 G-26 活到今天的那个形状。");

    // ★ 而且必须【只】由这个判据把门 —— 条件里不能夹带别的
    const QString before = body.left(iGuard);
    const int iIf = before.lastIndexOf(QStringLiteral("if ("));
    QVERIFY2(iIf >= 0,
             "★ hmi.cpp 里 shouldTripMachine 前面没有 if —— 抢停机又变成无条件的了。");
    const QString cond = before.mid(iIf + 4);   // "if (" 之后、调用之前
    QVERIFY2(QStringLiteral("qwhmi::alarm::").startsWith(cond),
             qPrintable(QStringLiteral(
                 "★ 抢停机的 if 条件被夹带了别的东西:条件是『%1』。"
                 "要求它【只能】是 shouldTripMachine 这一个判据。").arg(cond)));

    // ★ 抢停机那一下必须还在(不能把这条 bug 修成"谁都不停机")
    QVERIFY2(body.contains(QStringLiteral("tryTransition(Trigger::AlarmCritical)")),
             "★ hmi.cpp 的 onAlarmTriggered 里没有 tryTransition(Trigger::AlarmCritical) —— "
             "严重/危急报警不再抢停机了。");

    // ★ 而且只能有一处 —— 两处 = 状态被推两次
    QCOMPARE(countOf(body, QStringLiteral("tryTransition(Trigger::AlarmCritical)")), 1);
}

void tst_AlarmTriggerPolicy::hmiSource_doesNotAutoResetOnAlarmRecovery() {
    const QString path = QStringLiteral(HMI_SRC_DIR_STRING) + QStringLiteral("/hmi.cpp");
    QFile f(path);
    QVERIFY2(f.open(QIODevice::ReadOnly | QIODevice::Text),
             qPrintable(QStringLiteral("读不到 hmi.cpp: ") + path));
    const QString code = codeOnly(QString::fromUtf8(f.readAll()));

    const QString body = extractFunctionBody(code, QStringLiteral("void hmi::onAlarmTriggered"));
    QVERIFY2(!body.isEmpty(), "★ hmi.cpp 里找不到 onAlarmTriggered 的函数体");

    // ★ 报警一恢复就自动把机器放回 Stopped,是【更危险】的做法:
    //   条件反复越限时,机器会自己爬起来、跑两秒、又自己停 —— 比停着更糟。
    //   抢停机后必须人工按复位(迁移表里 Fault + ResetButton → Stopped)。
    QVERIFY2(!body.contains(QStringLiteral("AlarmCleared")),
             "★ hmi.cpp 的 onAlarmTriggered 里出现了 Trigger::AlarmCleared —— "
             "报警一恢复就自动把机器放回去。条件反复越限时机器会自己爬起来又自己停,"
             "那比停着更危险。Fault 必须只能靠人工复位清。");
}

QTEST_MAIN(tst_AlarmTriggerPolicy)
#include "tst_AlarmTriggerPolicy.moc"
