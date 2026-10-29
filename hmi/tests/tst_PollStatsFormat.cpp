// ============================================================================
// tst_PollStatsFormat —— 钉住"轮询统计行"这个【两方契约】
//
// 这个文件为什么存在
// ------------------
// 2026-10-21 的实测:长跑的启动自检等满 120 秒,一条轮询统计行都没匹配上,
// 判成"这一轮没资格跑" —— 而那 120 秒里程序一直在正常轮询
// (成功 271 次、失败 0 次)。判据坏掉的样子,和程序坏掉的样子,一模一样。
//
// 根因在两边的格式对不上:
//
//   程序打出来的(真的):
//     [PollScheduler] 轮询统计:发出= 271  成功= 271  失败= 0  在途= 0  PLC= 1  变量= 3
//                                   ↑ 等号后多一个空格
//   判据要的:
//     轮询统计:发出=271 成功=271 失败=0 在途=0 PLC=1
//
// 那个空格哪来的:QDebug 在【相邻两项之间插一个空格】。
// 于是 `qCInfo(cat) << "发出=" << count` 出来的是 "发出= 271",
// 而不是任何一处注释里写的 "发出=271" ——
// 也就是说程序一直在【违反自己的文档】,而且没有任何测试发现它。
//
// ★ 为什么不只写个 C++ 测试就完事
//   因为真正要守的【不是"程序输出什么"】,是【程序的输出和判据对不对得上】。
//   单独钉程序那一侧,两边照样能各改各的、一起漂走。
//   所以这个文件【把 scripts/long_run.ps1 里的 $rePollStats 原样读出来】,
//   再拿它去匹配程序【真的打出来的那一行】——
//   一边改了另一边没改,这里立刻红。
//
// ★ 更有意思的是:这个失配之所以能潜伏这么久,是因为【判据自带的验证脚本
//   一直绿】—— verify_soak6_liveness.ps1 喂给判据的是"发出=123"(没空格),
//   那是它【自己想象的格式】。一个拿自己当参照的测试,证明不了它和现实对得上。
//   所以那边现在也改成用真日志抄下来的黄金样本了。
//
// ★ 负向对照(必须做,否则这个测试可能是坏的)
//   把 PollScheduler::formatStatsLine 里的 "%1" 改成 "%1 "(等号后加空格),
//   或者把 long_run.ps1 的正则改回 "发出=(\\d+)",本文件必须【变红】。
//   详见提交记录里的对照表。
//
// 关联:PollScheduler::formatStatsLine / PollScheduler::onStatsTick
//      scripts/long_run.ps1 的 $rePollStats
//      scripts/verify_soak6_liveness.ps1 的 $goldenRealLine
// ============================================================================

#include <QtTest>
#include <QFile>
#include <QRegularExpression>
#include <QStringList>
#include <QTextStream>

#include "PollScheduler.h"
#include "VariableManager.h"
#include "Variable.h"
#include "drivers/IPollDriver.h"

using namespace qwhmi::drivers;

// ---------------------------------------------------------------------------
// ★ 从 long_run.ps1 里把 $rePollStats 原样抠出来
//
//   为什么【读文件】而不是在本文件里抄一份:
//     抄的那份迟早跟本体对不上,而对不上的时候【没人会发现】——
//     2026-10-21 踩的就是这个(判据的正则和程序的输出对不上,
//     而判据自己的验证脚本因为喂的是想象中的格式,一直绿)。
//     读文件 = 两边【物理上】就是同一份,漂不了。
QString harnessPollStatsRegex() {
    const QString path = QStringLiteral(HMI_SRC_DIR_STRING "/../scripts/long_run.ps1");
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }
    const QString text = QString::fromUtf8(f.readAll());
    f.close();

    // 形如:  $rePollStats = '轮询统计:发出=\s*(\d+)...'
    //   抓的是【单引号里的那段】—— PowerShell 的单引号字符串是字面量,
    //   里面的 \s \d 原样就是正则源码,不需要再转义。
    QRegularExpression re(
        QStringLiteral("(?m)^\\$rePollStats\\s*=\\s*'(.*)'\\s*$"));
    const auto m = re.match(text);
    if (!m.hasMatch()) {
        return QString();
    }
    return m.captured(1);
}

// ---------------------------------------------------------------------------
// ★ 黄金样本:从【真日志】里原样抄下来的那一行。
//
//   出处(2026-10-21 16:06:32,自建从站 127.0.0.1:1502 那一轮):
//     soaks\20261021_160531\appdata\QianWei\HMI\logs\hmi_20261021_160532.log
//       2026-10-21 16:06:32.216 [INFO ] hmi.poll - [PollScheduler] 轮询统计:发出= 271  成功= 271  失败= 0  在途= 0  PLC= 1  变量= 3
//
//   ★ 抄的时候不许"顺手整理"成没有空格的版本 ——
//     整理过的样本就又变回"自己想象的格式"了。
//   这份样本是【旧二进制】打出来的形态(QDebug 的空格),
//   判据必须对【它】也有效:那台机器上还挂着一个旧二进制在跑。
const QString kGoldenRealLine =
    QStringLiteral("[PollScheduler] 轮询统计:发出= 271  成功= 271  失败= 0  在途= 0  PLC= 1  变量= 3");

// ---------------------------------------------------------------------------
// 一个最小 mock driver:接得上、回得了包。
//   目的只是让调度器【真的轮询一次】,好让统计行里的数字不是 0 ——
//   全是 0 的话,"格式对不对"和"数字有没有真的传出去"就分不开了。
class CountingDriver : public IPollDriver {
    Q_OBJECT
public:
    explicit CountingDriver(QObject* parent = nullptr) : IPollDriver(parent) {}

    void connectTo(const QString& host, quint16 port) override {
        connectedFlag = true;
        emit connected();
    }
    void disconnect() override { connectedFlag = false; emit disconnected(); }
    bool isConnected() const override { return connectedFlag; }
    int pendingCount() const override { return 0; }

    bool sendReadRequest(const ReadRequest& req) override {
        QVector<quint16> regs(req.count, 7);
        emit readFinished(req.requestId, regs, true);
        return true;
    }
    bool sendWriteRequest(RegisterArea, int, int,
                           const QVector<quint16>&, quint32) override { return true; }
    QString protocolName() const override { return QStringLiteral("counting"); }

    bool connectedFlag = false;
};

// ---------------------------------------------------------------------------
// 抓日志:装一个自己的 message handler,把程序【真的打出来的那一行】截下来。
namespace {
QStringList g_captured;

void captureHandler(QtMsgType type, const QMessageLogContext& ctx, const QString& msg) {
    Q_UNUSED(type);
    Q_UNUSED(ctx);
    g_captured << msg;
}
} // namespace

class tst_PollStatsFormat : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();

    // ---- 契约本身 ----
    void formatIsExactlyTheDocumentedContract();
    void formatHasNoSpaceAfterEquals();

    // ---- 程序 ↔ 判据:两边对得上吗 ----
    void harnessRegexMatchesTheRealEmittedLine();
    void harnessRegexMatchesTheGoldenOldBinaryLine();
    void harnessRegexExtractsTheRightNumbers();

    // ---- 失配要【响】 ----
    void driftedFormatDoesNotMatch();

private:
    // 真的起一个调度器、真的轮询一次、真的把程序打出来的那一行截回来
    static QString emitRealStatsLine();
};

void tst_PollStatsFormat::initTestCase() {
    const QString re = harnessPollStatsRegex();
    QVERIFY2(!re.isEmpty(),
             qPrintable(QStringLiteral(
                 "从 scripts/long_run.ps1 里读不到 $rePollStats —— "
                 "判据改名了?或者本测试的路径宏不对?读的是:%1")
                            .arg(QStringLiteral(HMI_SRC_DIR_STRING "/../scripts/long_run.ps1"))));
}

// ---------------------------------------------------------------------------
// 契约:程序打出来的行必须和它自己注释里写的一字不差。
//   这一条钉的是【文档 = 实现】。QDebug 插空格那件事就是从这儿漏过去的:
//   注释写的是 "发出=123",程序打的是 "发出= 123",而没有任何东西检查过。
void tst_PollStatsFormat::formatIsExactlyTheDocumentedContract() {
    const QString line = PollScheduler::formatStatsLine(271, 271, 0, 0, 1, 3);
    QCOMPARE(line, QStringLiteral("[PollScheduler] 轮询统计:发出=271 成功=271 失败=0 在途=0 PLC=1 变量=3"));
}

// ---------------------------------------------------------------------------
// ★ 单独钉"等号后没有空格" —— 因为【那一个空格】就是 2026-10-21 那次
//   失配的全部原因。别的都还好,栽就栽在它上面。
void tst_PollStatsFormat::formatHasNoSpaceAfterEquals() {
    const QString line = PollScheduler::formatStatsLine(1, 2, 3, 4, 5, 6);
    for (const QString& key : {QStringLiteral("发出"), QStringLiteral("成功"),
                               QStringLiteral("失败"), QStringLiteral("在途"),
                               QStringLiteral("PLC"), QStringLiteral("变量")}) {
        const int at = line.indexOf(key + QStringLiteral("="));
        QVERIFY2(at >= 0, qPrintable(QStringLiteral("行里没有 %1= 这个字段:%2")
                                         .arg(key, line)));
        const QChar after = line.at(at + key.size() + 1);
        QVERIFY2(after.isDigit(),
                 qPrintable(QStringLiteral(
                     "★ %1= 后面不是数字,是 '%2' —— 整行:%3\n"
                     "  这正是 2026-10-21 那次判据失配的形态:"
                     "QDebug 在相邻两项之间插了一个空格。")
                     .arg(key, QString(after), line)));
    }
}

// ---------------------------------------------------------------------------
// ★★ 核心:判据的正则(从 long_run.ps1 里读的)能不能匹配程序【真的打出来的那一行】。
//   这一条走的是完整链路:真调度器 → 真轮询 → 真计数器 → 真日志行 → 判据的正则。
//   不是"拿纯函数的返回值去正则一下"—— 那样测不到 onStatsTick 本身。
void tst_PollStatsFormat::harnessRegexMatchesTheRealEmittedLine() {
    const QString emitted = emitRealStatsLine();
    QVERIFY2(!emitted.isEmpty(), "没抓到程序打出来的统计行");

    const QRegularExpression re(harnessPollStatsRegex());
    QVERIFY2(re.isValid(), qPrintable(QStringLiteral("判据的正则本身是坏的:%1")
                                          .arg(re.errorString())));
    const auto m = re.match(emitted);
    QVERIFY2(m.hasMatch(),
             qPrintable(QStringLiteral(
                 "★ 判据的正则匹配不上程序【真的打出来的那一行】。\n"
                 "  打出来的: %1\n"
                 "  正则    : %2\n"
                 "  —— 这一条红,意味着长跑的启动自检会一直等不到统计行,"
                 "判成\"没资格跑\",而程序其实一直在正常轮询。")
                 .arg(emitted, harnessPollStatsRegex())));
}

// ---------------------------------------------------------------------------
// 判据对【旧二进制】也必须有效 —— 那台机器上还挂着一个 2026-10-21 之前
//   编译的 hmi.exe 在跑,它的统计行是带空格的。
void tst_PollStatsFormat::harnessRegexMatchesTheGoldenOldBinaryLine() {
    const QRegularExpression re(harnessPollStatsRegex());
    const auto m = re.match(kGoldenRealLine);
    QVERIFY2(m.hasMatch(),
             qPrintable(QStringLiteral(
                 "★ 判据的正则匹配不上【真日志抄下来的黄金样本】。\n"
                 "  样本: %1\n"
                 "  正则: %2")
                 .arg(kGoldenRealLine, harnessPollStatsRegex())));
}

// ---------------------------------------------------------------------------
// 光"匹配得上"不够 —— 得把数字【取对】。取错了数字,判据会安静地算错账。
void tst_PollStatsFormat::harnessRegexExtractsTheRightNumbers() {
    const QRegularExpression re(harnessPollStatsRegex());
    const auto m = re.match(kGoldenRealLine);
    QVERIFY(m.hasMatch());
    QCOMPARE(m.captured(1).toInt(), 271);   // 发出
    QCOMPARE(m.captured(2).toInt(), 271);   // 成功
    QCOMPARE(m.captured(3).toInt(), 0);     // 失败
    QCOMPARE(m.captured(4).toInt(), 0);     // 在途
    QCOMPARE(m.captured(5).toInt(), 1);     // PLC
}

// ---------------------------------------------------------------------------
// 失配要【响】:格式漂了(改了分隔符/改了字段名),正则必须匹配不上。
//   匹配不上 → 判据判 UNKNOWN("这一轮没有结论")→ 至少不会拿着默认值乱判。
//   这一条钉的是"响"这个性质本身。
void tst_PollStatsFormat::driftedFormatDoesNotMatch() {
    const QRegularExpression re(harnessPollStatsRegex());
    // 把 "=" 改成 ":" —— 一个看起来很小、实际上会让判据彻底失配的改动
    const QString drifted = QStringLiteral("[PollScheduler] 轮询统计:发出: 271  成功: 271  失败: 0  在途: 0  PLC: 1");
    QVERIFY2(!re.match(drifted).hasMatch(),
             "★ 格式漂了,正则却还匹配 —— 判据会拿着漂掉之后的格式硬判,"
             "失配时【不响】,于是漂移能潜伏很久(2026-10-21 就是这么潜伏的)");
}

// ---------------------------------------------------------------------------
// ★ 起一个真的调度器,真的轮一次,真的把程序打出来的那一行截回来。
//
//   为什么不用纯函数的返回值:
//     纯函数只覆盖 formatStatsLine。onStatsTick 才是【真正打日志的那一步】——
//     而 2026-10-21 出事的就是那一步(QDebug 插的空格)。
//     截真日志才能覆盖"整行拼成字符串再输出"这个做法有没有被改回去。
QString tst_PollStatsFormat::emitRealStatsLine() {
    VariableManager vm;
    auto* driver = new CountingDriver();
    driver->connectTo(QStringLiteral("127.0.0.1"), 1502);

    auto* v = new Variable(QStringLiteral("Speed"), quint16(100),
                           Variable::DataType::Int16,
                           Variable::Access::ReadOnly, &vm);
    v->setAlwaysPoll(true);
    vm.addVariable(v);

    PollScheduler sched(&vm, driver);

    g_captured.clear();
    QtMessageHandler prev = qInstallMessageHandler(captureHandler);

    // 真的轮一轮(100ms 一次 tick,等 250ms 足够)
    sched.setAllActive();
    sched.start();
    QTest::qWait(250);
    sched.stop();

    // ★ 直接触发那【私有槽】。它是 private slot,但 moc 把它登记进了元对象,
    //   所以能按名字 invoke —— 这正是"程序真正打日志的那一步"。
    const bool invoked = QMetaObject::invokeMethod(&sched, "onStatsTick",
                                                   Qt::DirectConnection);
    qInstallMessageHandler(prev);

    if (!invoked) {
        return QString();
    }
    for (const QString& m : g_captured) {
        if (m.contains(QStringLiteral("轮询统计"))) {
            return m;
        }
    }
    return QString();
}

QTEST_MAIN(tst_PollStatsFormat)
#include "tst_PollStatsFormat.moc"
