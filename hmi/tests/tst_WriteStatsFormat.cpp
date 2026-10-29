// ============================================================================
// tst_WriteStatsFormat —— 钉住"写侧统计行"这个【两方契约】
//
// 这个文件为什么存在
// ------------------
// SOAK-W(写侧闸)是 2026-10-27 新加的判据:读侧活着不代表写能落地 ——
// BUGFIX-E1 修之前就是"读 547 次全成功、写 604 次全被拒、0 帧到达从站",
// 而当时的活体判据(SOAK-6)【只看读】⇒ 那种情况下它照样绿。
//
// 这道闸量的是【程序自己报出来的写侧统计行】,所以它【必须】和程序对得上。
// 对不上的表现和 2026-10-21 那次一模一样:【判不出】,不是【判错】——
//
//   2026-10-21:程序打的是 "发出= 271"(QDebug 在相邻两项之间插了个空格),
//   harness 的正则要 "发出=271" ⇒ 启动自检等满 120 秒一条都没匹配上,
//   判成"没资格跑" —— 而那 120 秒里程序一直在正常轮询。
//   判据坏掉的样子,和程序坏掉的样子,一模一样。
//
// ★ 为什么不只写个 C++ 测试就完事
//   因为真正要守的【不是"程序输出什么"】,是【程序的输出和判据对不对得上】。
//   单独钉程序那一侧,两边照样能各改各的、一起漂走。
//   所以这个文件【把 scripts/long_run.ps1 里的 $reWriteStats 原样读出来】,
//   再拿它去匹配程序【真的打出来的那一行】——
//   一边改了另一边没改,这里立刻红。
//
// ★ 这道闸比轮询统计那条约【更严】,为什么
//   因为写侧统计行有 17 个字段,不是 6 个。17 个字段里只要有一个名字变了
//   (或者顺序变了),harness 就会把【A 的数当成 B 的数】——
//   而恒等式是【分开算】的 ⇒ 算出来的残差是错的
//   ⇒ 判据会【看着绿、其实在量错东西】。
//   ⇒ 所以这里除了"能不能匹配上",还要钉"取出来的数【逐个】对不对得上"。
//
// ★ 负向对照(必须做,否则这个测试可能是坏的)
//   把 formatWriteStatsLine 里的 "%10" 改成 "%100"、或者把 long_run.ps1 的
//   正则里"拒收缺陷"改成"拒收未知",本文件必须【变红】。
//   详见提交记录里的对照表。
//
// 关联:ModbusMaster::formatWriteStatsLine / ModbusMaster::onWriteStatsTick
//      scripts/long_run.ps1 的 $reWriteStats
//      scripts/verify_soakw_write_side.ps1
// ============================================================================

#include <QtTest>
#include <QFile>
#include <QRegularExpression>
#include <QStringList>
#include <QTextStream>

#include "ModbusMaster.h"

// ★ ModbusMaster 在【全局命名空间】里(不在 qwhmi 下)——
//   项目根目录的头文件没套命名空间,跟 ModbusDriver 那批不一样。
//   这里不写 using 声明,免得哪天它挪进命名空间了还要改一遍。
//   (写成 using qwhmi::ModbusMaster 会直接编译不过 —— 这也是一次"契约")

// ---------------------------------------------------------------------------
// ★ 从 long_run.ps1 里把 $reWriteStats 原样抠出来
//
//   为什么【读文件】而不是在本文件里抄一份:
//     抄的那份迟早跟本体对不上,而对不上的时候【没人会发现】——
//     2026-10-21 踩的就是这个(判据的正则和程序的输出对不上,
//     而判据自己的验证脚本因为喂的是想象中的格式,一直绿)。
//     读文件 = 两边【物理上】就是同一份,漂不了。
QString harnessWriteStatsRegex() {
    const QString path = QStringLiteral(HMI_SRC_DIR_STRING "/../scripts/long_run.ps1");
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }
    const QString text = QString::fromUtf8(f.readAll());
    f.close();

    // 形如:  $reWriteStats = '写侧统计:实例=\s*(\d+)...'
    //   抓的是【单引号里的那段】—— PowerShell 的单引号字符串是字面量,
    //   里面的 \s \d 原样就是正则源码,不需要再转义。
    QRegularExpression re(
        QStringLiteral("(?m)^\\$reWriteStats\\s*=\\s*'(.*)'\\s*$"));
    const auto m = re.match(text);
    if (!m.hasMatch()) {
        return QString();
    }
    return m.captured(1);
}

// ---------------------------------------------------------------------------
// ★ 黄金样本:程序【真的打出来的那一行】。
//
//   ★ 这份样本的来历必须是【真跑出来的】,不是手写的 ——
//     手写的样本就是"自己想象的格式",2026-10-21 栽的就是这个。
//     下面这一行跟 ModbusMaster::formatWriteStatsLine 的输出【逐字一致】
//     (用一个【非全零】的样本,免得"全零也匹配"这种假绿混过去)。
const QString kGoldenRealLine = QStringLiteral(
    "[ModbusMaster] 写侧统计:实例=1 尝试=26 接受=26 拒收=0 拒收未连=0 拒收非法=0 "
    "拒收队列满=0 拒收发送=0 拒收缺陷=0 排队=3 排队发出=3 排队作废=0 已发出=26 "
    "成功=26 失败=0 在队=0 在途写=0");

// ---------------------------------------------------------------------------
// 造一份"什么值都有"的统计快照 —— 每个字段都给【不同的】数,
//   这样"取错字段"一定看得出来(取错的话至少有一个对不上)。
//
//   ★ 为什么要每个字段都给不同的数:
//     如果全给 0 或者全给同一个数,字段顺序错了也可能照样通过。
//     "取错了但看不出来"是这种契约测试最常见的假绿。
ModbusMaster::WritePathStats sampleStats() {
    ModbusMaster::WritePathStats s;   // NOLINT(modernize-use-using) — 全局命名空间的类
    s.instance        = 1;
    s.attempts        = 101;   // 尝试
    s.accepted        = 102;   // 接受
    s.rejected        = 103;   // 拒收
    s.rejNotConnected = 104;   // 拒收未连
    s.rejInvalid      = 105;   // 拒收非法
    s.rejQueueFull    = 106;   // 拒收队列满
    s.rejSendFailed   = 107;   // 拒收发送
    s.rejDefect       = 108;   // 拒收缺陷
    s.queued          = 109;   // 排队
    s.queuedSent      = 110;   // 排队发出
    s.queuedVoided    = 111;   // 排队作废
    s.sent            = 112;   // 已发出
    s.ok              = 113;   // 成功
    s.fail            = 114;   // 失败
    s.inQueue         = 115;   // 在队
    s.inflightWrites  = 116;   // 在途写
    return s;
}

class tst_WriteStatsFormat : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();

    // ---- 契约本身 ----
    void formatIsExactlyTheDocumentedContract();
    void formatHasNoSpaceAfterEquals();

    // ---- 程序 ↔ 判据:两边对得上吗 ----
    void harnessRegexMatchesTheRealEmittedLine();
    void harnessRegexMatchesTheGoldenRealLine();
    void harnessRegexExtractsTheRightNumbers();

    // ---- 失配要【响】 ----
    void driftedFormatDoesNotMatch();
    void renamedFieldDoesNotMatch();

private:
    static QString emitRealStatsLine();
};

void tst_WriteStatsFormat::initTestCase() {
    const QString re = harnessWriteStatsRegex();
    QVERIFY2(!re.isEmpty(),
             qPrintable(QStringLiteral(
                 "从 scripts/long_run.ps1 里读不到 $reWriteStats —— "
                 "判据改名了?或者本测试的路径宏不对?读的是:%1")
                            .arg(QStringLiteral(HMI_SRC_DIR_STRING "/../scripts/long_run.ps1"))));
}

// ---------------------------------------------------------------------------
// 契约:程序打出来的行必须和它自己注释里写的一字不差。
//   这一条钉的是【文档 = 实现】。
void tst_WriteStatsFormat::formatIsExactlyTheDocumentedContract() {
    const QString line = ModbusMaster::formatWriteStatsLine(sampleStats());
    QCOMPARE(line, QStringLiteral(
        "[ModbusMaster] 写侧统计:实例=1 尝试=101 接受=102 拒收=103 "
        "拒收未连=104 拒收非法=105 拒收队列满=106 拒收发送=107 拒收缺陷=108 "
        "排队=109 排队发出=110 排队作废=111 已发出=112 成功=113 失败=114 "
        "在队=115 在途写=116"));
}

// ---------------------------------------------------------------------------
// ★ 单独钉"等号后没有空格" —— 因为【那一个空格】就是 2026-10-21 那次
//   失配的全部原因。别的都还好,栽就栽在它上面。
//
//   ★ 这条对写侧【更严】:整行有 17 个字段,漏掉一个"="后面的空格,
//     harness 的正则会【匹配不上整行】—— 不是匹配错,是整行都不认。
void tst_WriteStatsFormat::formatHasNoSpaceAfterEquals() {
    const QString line = ModbusMaster::formatWriteStatsLine(sampleStats());
    const QStringList keys = {
        QStringLiteral("实例"),   QStringLiteral("尝试"),   QStringLiteral("接受"),
        QStringLiteral("拒收"),   QStringLiteral("拒收未连"), QStringLiteral("拒收非法"),
        QStringLiteral("拒收队列满"), QStringLiteral("拒收发送"), QStringLiteral("拒收缺陷"),
        QStringLiteral("排队"),   QStringLiteral("排队发出"), QStringLiteral("排队作废"),
        QStringLiteral("已发出"), QStringLiteral("成功"),   QStringLiteral("失败"),
        QStringLiteral("在队"),   QStringLiteral("在途写")
    };
    QCOMPARE(keys.size(), 17);
    for (const QString& key : keys) {
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
//   这一条走的是完整链路:真 ModbusMaster → 真统计行 → 判据的正则。
//   不是"拿纯函数的返回值去正则一下"—— 那样测不到 onWriteStatsTick 本身
//   (那正是 2026-10-21 出事的那一步)。
void tst_WriteStatsFormat::harnessRegexMatchesTheRealEmittedLine() {
    const QString emitted = emitRealStatsLine();
    QVERIFY2(!emitted.isEmpty(), "没抓到程序打出来的写侧统计行");

    const QRegularExpression re(harnessWriteStatsRegex());
    QVERIFY2(re.isValid(), qPrintable(QStringLiteral("判据的正则本身是坏的:%1")
                                          .arg(re.errorString())));
    const auto m = re.match(emitted);
    QVERIFY2(m.hasMatch(),
             qPrintable(QStringLiteral(
                 "★ 判据的正则匹配不上程序【真的打出来的那一行】。\n"
                 "  打出来的: %1\n"
                 "  正则    : %2\n"
                 "  —— 这一条红,意味着长跑的写侧闸【永远看不见证据】,"
                 "于是它会判 UNKNOWN(\"看不见证据 ≠ 没问题\"),"
                 "而程序其实一直在正常写。")
                 .arg(emitted, harnessWriteStatsRegex())));
}

// ---------------------------------------------------------------------------
// 判据对【黄金样本】(真跑出来的那一行)必须有效。
void tst_WriteStatsFormat::harnessRegexMatchesTheGoldenRealLine() {
    const QRegularExpression re(harnessWriteStatsRegex());
    const auto m = re.match(kGoldenRealLine);
    QVERIFY2(m.hasMatch(),
             qPrintable(QStringLiteral(
                 "★ 判据的正则匹配不上【真日志抄下来的黄金样本】。\n"
                 "  样本: %1\n"
                 "  正则: %2")
                 .arg(kGoldenRealLine, harnessWriteStatsRegex())));
}

// ---------------------------------------------------------------------------
// ★★ 光"匹配得上"不够 —— 得把数字【逐个取对】。
//
//   ★ 这是写侧统计比轮询统计【更要紧】的一条:
//     17 个字段,判据会把它们【分开算】(恒等式①用已发出/成功/失败/在途写,
//     恒等式②用排队/排队发出/排队作废/在队)。
//     ⇒ 如果取错了一格,恒等式必然残差 ⇒ 判据【看着绿、其实在量错东西】。
//     ⇒ 所以这里【逐个】比,而不是"抽两个看看"。
void tst_WriteStatsFormat::harnessRegexExtractsTheRightNumbers() {
    const QRegularExpression re(harnessWriteStatsRegex());
    const auto m = re.match(kGoldenRealLine);
    QVERIFY(m.hasMatch());

    // 捕获组顺序 = 字段顺序(程序侧 formatWriteStatsLine 的 %1..%17)
    QCOMPARE(m.captured(1).toInt(), 1);    // 实例
    QCOMPARE(m.captured(2).toInt(), 26);   // 尝试
    QCOMPARE(m.captured(3).toInt(), 26);   // 接受
    QCOMPARE(m.captured(4).toInt(), 0);    // 拒收
    QCOMPARE(m.captured(5).toInt(), 0);    // 拒收未连
    QCOMPARE(m.captured(6).toInt(), 0);    // 拒收非法
    QCOMPARE(m.captured(7).toInt(), 0);    // 拒收队列满
    QCOMPARE(m.captured(8).toInt(), 0);    // 拒收发送
    QCOMPARE(m.captured(9).toInt(), 0);    // 拒收缺陷
    QCOMPARE(m.captured(10).toInt(), 3);   // 排队
    QCOMPARE(m.captured(11).toInt(), 3);   // 排队发出
    QCOMPARE(m.captured(12).toInt(), 0);   // 排队作废
    QCOMPARE(m.captured(13).toInt(), 26);  // 已发出
    QCOMPARE(m.captured(14).toInt(), 26);  // 成功
    QCOMPARE(m.captured(15).toInt(), 0);   // 失败
    QCOMPARE(m.captured(16).toInt(), 0);   // 在队
    QCOMPARE(m.captured(17).toInt(), 0);   // 在途写
}

// ---------------------------------------------------------------------------
// ★★ 失配要【响】:格式漂了(改了分隔符),正则必须匹配不上。
//   匹配不上 → 判据判 UNKNOWN("这一轮没有结论")→ 至少不会拿着默认值乱判。
//   这一条钉的是"响"这个性质本身。
void tst_WriteStatsFormat::driftedFormatDoesNotMatch() {
    const QRegularExpression re(harnessWriteStatsRegex());
    // 把 "=" 改成 ":" —— 一个看起来很小、实际上会让判据彻底失配的改动
    const QString drifted = QStringLiteral(
        "[ModbusMaster] 写侧统计:实例:1 尝试:26 接受:26 拒收:0 拒收未连:0 拒收非法:0 "
        "拒收队列满:0 拒收发送:0 拒收缺陷:0 排队:3 排队发出:3 排队作废:0 已发出:26 "
        "成功:26 失败:0 在队:0 在途写:0");
    QVERIFY2(!re.match(drifted).hasMatch(),
             "★ 格式漂了,正则却还匹配 —— 判据会拿着漂掉之后的格式硬判,"
             "失配时【不响】,于是漂移能潜伏很久(2026-10-21 就是这么潜伏的)");
}

// ---------------------------------------------------------------------------
// ★★ 字段【改名】也必须响 —— 而且这一条比"改分隔符"更要命。
//
//   为什么:
//     改分隔符(等号→冒号)⇒ 整行匹配不上 ⇒ 判据判 UNKNOWN ⇒ 【响】。
//     改字段名(拒收缺陷→拒收未知)⇒ 整行也匹配不上 ⇒ 同样响。
//     但如果改的是【顺序】或者【多了一个字段】,就可能【只匹配一半】——
//     那时候 harness 会把后面字段的数【整体前移一格】,
//     而恒等式是分开算的 ⇒ 算出来的残差是错的 ⇒ 【不响、且判错】。
//
//   ⇒ 所以这条钉的是"字段名对不上就整行不认"这个性质。
void tst_WriteStatsFormat::renamedFieldDoesNotMatch() {
    const QRegularExpression re(harnessWriteStatsRegex());
    // 把"拒收缺陷"改名成"拒收未知" —— 语义完全一样,字段名不一样
    const QString renamed = QStringLiteral(
        "[ModbusMaster] 写侧统计:实例=1 尝试=26 接受=26 拒收=0 拒收未连=0 拒收非法=0 "
        "拒收队列满=0 拒收发送=0 拒收未知=0 排队=3 排队发出=3 排队作废=0 已发出=26 "
        "成功=26 失败=0 在队=0 在途写=0");
    QVERIFY2(!re.match(renamed).hasMatch(),
             "★ 字段名漂了,正则却还匹配 —— harness 会把后面的字段整体错位一格,"
             "而恒等式是分开算的 ⇒ 判据会【看着绿、其实在量错东西】。"
             "这种错不会自己暴露。");
}

// ---------------------------------------------------------------------------
// ★ 起一个真的 ModbusMaster,真的把程序打出来的那一行截回来。
//
//   为什么不用纯函数的返回值:
//     纯函数只覆盖 formatWriteStatsLine。onWriteStatsTick 才是【真正打日志的那一步】——
//     而 2026-10-21 出事的就是那一步(QDebug 插的空格)。
//     截真日志才能覆盖"整行拼成字符串再输出"这个做法有没有被改回去。
QString tst_WriteStatsFormat::emitRealStatsLine() {
    ModbusMaster master;

    // ★ 用【静态】数组捕获 —— 消息处理器是个普通函数指针,
    //   捕不到外层的局部变量。静态变量不需要捕获,lambda 能直接用。
    static QStringList g_captured;
    g_captured.clear();
    QtMessageHandler prev = qInstallMessageHandler(
        [](QtMsgType type, const QMessageLogContext& ctx, const QString& msg) {
            Q_UNUSED(type); Q_UNUSED(ctx);
            g_captured << msg;
        });

    // ★ 直接触发那【私有槽】。它是 private slot,但 moc 把它登记进了元对象,
    //   所以能按名字 invoke —— 这正是"程序真正打日志的那一步"。
    const bool invoked = QMetaObject::invokeMethod(&master, "onWriteStatsTick",
                                                   Qt::DirectConnection);
    qInstallMessageHandler(prev);

    if (!invoked) {
        return QString();
    }
    for (const QString& m : g_captured) {
        if (m.contains(QStringLiteral("写侧统计"))) {
            return m;
        }
    }
    return QString();
}

QTEST_MAIN(tst_WriteStatsFormat)
#include "tst_WriteStatsFormat.moc"
