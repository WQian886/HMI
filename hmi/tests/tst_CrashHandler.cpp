// ============================================================================
// tst_CrashHandler —— 崩溃 dump 的【消费端】
//
// 这个文件为什么存在
// ------------------
// 2026-10-14 实测发现:crash/pending.txt 停在 2026-09-16 的 0xC0000005,
// 28 天没被任何一次启动消费掉。查下来是两个叠在一起的问题:
//
//   1)【路径错位】dump 路径来自 QStandardPaths::AppLocalDataLocation,
//      也就是 AppData/Local/<org>/<app>。而 main() 的启动顺序是
//        install() → hasPendingDump() → ... → 第 4 步才 setOrganizationName
//      于是【崩溃发生时】写 dump 的那次,org/app 已经设好了,写到
//        AppData/Local/QianWei/HMI/crash/
//      而【启动时】找 marker 的那次,org/app 还没设,去看的是
//        AppData/Local/crash/            ← 这个空壳从 2026-09-08 就在了
//      写的人和读的人看两个不同的目录。marker 写得出来,永远读不到。
//
//   2)【QMessageBox 建在 QApplication 之前】就算路径对了,
//      那个弹框也是用 QWidget 但当时还没有 QApplication 实例,Qt 会直接 abort。
//
// 本文件守的是【生产者与消费者必须落在同一个目录】这个不变量。
// 这不是"顺手测一下"——这条路径此前【零测试覆盖】,所以上面那个 bug
// 才能安静存在 28 天、全程不报错。
// ============================================================================

#include "crash/CrashHandler.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTest>
#include <QTextStream>

using namespace qwhmi;

namespace {

// 测试专用的 org/app 名 —— 换一个名字,QStandardPaths 就会把目录整个换掉,
// 于是测试绝不会碰到现场那套 %LOCALAPPDATA%\QianWei\HMI\。
// ★ 这比给 CrashHandler 加一个"仅测试用"的目录覆盖接口要好:
//   生产代码里不留一条"只为了测试才存在"的路径。
const QString kTestOrg = QStringLiteral("HmiCrashTest");
const QString kTestApp = QStringLiteral("HmiCrashTestApp");

// 测试专用目录。必须用 HMI_CRASH_DIR 覆盖 ——
// takePendingDump() 是【会删 marker 的】,而 dumpDirectory() 修好之后
// 是个纯函数、不再理会 org/app,测试再想靠改 org/app 改道就没门了。
// 不隔离的话,跑一次测试就把现场那个真实的 pending.txt 删掉了。
QString testDumpDir() {
    return QDir::tempPath() + QStringLiteral("/tst_CrashHandler/crash");
}

void enterTestMetadata() {
    QCoreApplication::setOrganizationName(kTestOrg);
    QCoreApplication::setApplicationName(kTestApp);
    qputenv("HMI_CRASH_DIR", testDumpDir().toUtf8());
    // ★ 2026-10-21:把数据目录覆盖也清掉。
    //   dumpDirectory() 现在走 util::appDataDir()(它认 HMI_DATA_DIR),
    //   环境里要是残留着这个变量,下面那条"默认路径"的断言就不是在验默认路径了。
    qunsetenv("HMI_DATA_DIR");
}

QString markerPath() {
    return crash::dumpDirectory() + QStringLiteral("/pending.txt");
}

// 按 CrashHandler 写出的格式造一个 marker(UTF-8,key=value 每行一条)
void writeMarker(const QString& dump, const QString& ts, const QString& code) {
    QDir().mkpath(crash::dumpDirectory());
    QFile f(markerPath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        return;
    QTextStream ts_out(&f);
    ts_out.setEncoding(QStringConverter::Utf8);
    ts_out << "path=" << dump << "\n"
           << "timestamp=" << ts << "\n"
           << "exception_code=" << code << "\n";
}

void removeMarker() { QFile::remove(markerPath()); }

} // namespace

class TestCrashHandler : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanupTestCase();

    // ★ 本文件最重要的一条。它在修复【之前】是红的。
    void dumpDirectoryIsIndependentOfMetadataOrder();

    void pendingDumpRoundTrip();
    void overrideRedirectsDirectory();
    void noMarkerMeansNothingPending();
    void malformedMarkerIsHandledNotCrashed();
    void takeRemovesMarkerEvenWhenContentIsGarbage();
};

// --------------------------------------------------------------------
void TestCrashHandler::initTestCase() {
    enterTestMetadata();
    // 确认覆盖开关确实生效了。万一没生效,后面的用例会对着
    // 真实的 %LOCALAPPDATA%\QianWei\HMI\crash\ 跑 ——
    // 其中 takePendingDump() 会把现场那个 28 天的 marker 删掉。
    // 这条护栏在开发中真的红过一次(修完 dumpDirectory 之后),不是摆设。
    QVERIFY2(crash::dumpDirectory() == testDumpDir(),
             qPrintable(QStringLiteral("测试目录没隔离! 会动到现场数据,实际指向: %1")
                            .arg(crash::dumpDirectory())));
    // 现场那个目录必须【不存在】被创建
    QVERIFY(!QFile::exists(crash::dumpDirectory() + QStringLiteral("/pending.txt")));
}

void TestCrashHandler::init() {
    // 每条用例之前都重新锁一次隔离。
    // 为什么不能只在 initTestCase 锁一次:dumpDirectoryIsIndependentOfMetadataOrder
    // 会临时 qunsetenv 去验默认路径 —— 那条用例中途若断言失败,
    // 覆盖开关就再也不会被设回来,后面所有用例都会对着【真实目录】跑。
    // 这种"一个用例失败、后面全体受害"的连坐,必须用 per-test 钩子挡住。
    enterTestMetadata();
    if (crash::dumpDirectory() != testDumpDir()) {
        qFatal("测试隔离失效,拒绝运行。当前 dump 目录: %s",
               qPrintable(crash::dumpDirectory()));
    }
}

void TestCrashHandler::cleanupTestCase() {
    // ★ 这里曾经写过一句 QDir(crash::dumpDirectory()).removeRecursively()。
    //
    //   代价是实打实的:2026-10-14,上一轮 initTestCase 的隔离护栏判红了,
    //   而 Qt 【不会】因为 initTestCase 失败就跳过 cleanupTestCase ——
    //   于是这一句对着真实的 %LOCALAPPDATA%\QianWei\HMI\crash\ 执行,
    //   把现场的 pending.txt 和那个 366 KB 的 dump 递归删掉了。
    //
    //   教训:护栏只保护它自己所在的函数,不够。
    //   测试不配拥有"递归删除任意路径"的能力,所以这句整句删掉 ——
    //   测试本来就只写一个 marker 文件,删掉它就够了,
    //   临时目录里剩个空壳没有任何危害。
    const QString dir = crash::dumpDirectory();
    // 仍然要删这一个文件,但删之前确认自己在临时目录里。
    // 用 if 而不是 QVERIFY:不依赖 Qt 对清理钩子断言失败的行为语义。
    if (!dir.startsWith(QDir::tempPath())) {
        qWarning("拒绝清理非临时目录: %s", qPrintable(dir));
        return;
    }
    QFile::remove(dir + QStringLiteral("/pending.txt"));
}

// --------------------------------------------------------------------
void TestCrashHandler::dumpDirectoryIsIndependentOfMetadataOrder() {
    // ★ 先撤掉覆盖开关,否则两边都返回同一个覆盖值,这条测试就成了
    //   什么都没验的恒真断言。要验的是【默认路径】怎么算出来的。
    qunsetenv("HMI_CRASH_DIR");

    // 模拟【崩溃发生的那一瞬间】:org/app 已经设好了
    enterTestMetadata();
    qunsetenv("HMI_CRASH_DIR");
    const QString whenCrashHappens = crash::dumpDirectory();

    // 模拟【启动早期】:org/app 还没设 —— main() 的旧顺序就是这样
    QCoreApplication::setOrganizationName(QString());
    QCoreApplication::setApplicationName(QString());
    const QString atStartupEarly = crash::dumpDirectory();

    // 默认路径必须落在 <LOCALAPPDATA>/QianWei/HMI/crash —— 与现场已有 dump 同址
    const QString expected =
        QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
        + QStringLiteral("/QianWei/HMI/crash");
    QCOMPARE(atStartupEarly, expected);
    QCOMPARE(whenCrashHappens, expected);

    enterTestMetadata();   // 还原覆盖开关,别影响后面的用例

    // ★ 核心不变量:不管调用方什么时候设 org/app,dump 都必须落到同一处。
    //   修复之前这两个值不同(一边 QianWei/HMI/crash,一边 Local/crash),
    //   于是 marker 写得出来、读不到,崩溃上报静默失效 28 天。
    QCOMPARE(atStartupEarly, whenCrashHappens);
}

// --------------------------------------------------------------------
void TestCrashHandler::pendingDumpRoundTrip() {
    removeMarker();
    QVERIFY(!crash::hasPendingDump());

    writeMarker(QStringLiteral("C:/x/crash_20260916_093410_510.dmp"),
                QStringLiteral("2026-09-16T09:34:10"),
                QStringLiteral("0xC0000005"));
    QVERIFY2(crash::hasPendingDump(), "写完 marker 却查不到 —— 读写路径不一致");

    const auto info = crash::takePendingDump();
    QCOMPARE(info.path,          QStringLiteral("C:/x/crash_20260916_093410_510.dmp"));
    QCOMPARE(info.timestamp,     QStringLiteral("2026-09-16T09:34:10"));
    QCOMPARE(info.exceptionCode, QStringLiteral("0xC0000005"));

    // 消费完必须把标记清掉,否则下次启动会拿同一份旧信息反复弹框
    QVERIFY2(!crash::hasPendingDump(), "takePendingDump() 之后 marker 还在");
}

// --------------------------------------------------------------------
void TestCrashHandler::overrideRedirectsDirectory() {
    // HMI_CRASH_DIR 一旦设上,读写两端【都必须】跟着走 ——
    // 只要有一端不跟,dump 就又写到别处去了,正是这次修的那个病。
    const QString wanted = testDumpDir() + QStringLiteral("/redirected");
    qputenv("HMI_CRASH_DIR", wanted.toUtf8());
    QCOMPARE(crash::dumpDirectory(), wanted);

    QDir().mkpath(wanted);
    writeMarker(QStringLiteral("C:/x/b.dmp"),
                QStringLiteral("2026-10-14T00:00:00"),
                QStringLiteral("0xDEAD"));
    QVERIFY(crash::hasPendingDump());          // 跟到覆盖目录了吗
    QCOMPARE(crash::takePendingDump().path, QStringLiteral("C:/x/b.dmp"));
    QVERIFY(!QFile::exists(wanted + QStringLiteral("/pending.txt")));
    QDir(wanted).removeRecursively();

    // 撤掉之后必须回到默认路径,不能粘在覆盖值上
    qunsetenv("HMI_CRASH_DIR");
    QVERIFY(crash::dumpDirectory().endsWith(QStringLiteral("/QianWei/HMI/crash")));
    enterTestMetadata();   // 恢复测试隔离
}

// --------------------------------------------------------------------
void TestCrashHandler::noMarkerMeansNothingPending() {
    removeMarker();
    QVERIFY(!crash::hasPendingDump());
    const auto info = crash::takePendingDump();
    QVERIFY(info.path.isEmpty());
    QVERIFY(info.timestamp.isEmpty());
    QVERIFY(info.exceptionCode.isEmpty());
}

// --------------------------------------------------------------------
void TestCrashHandler::malformedMarkerIsHandledNotCrashed() {
    // 半路崩掉 / 磁盘写满,marker 完全可能只剩半行。
    // 这时候宁可报"内容读不出来",也不能崩 —— 崩了就等于又崩一次,
    // 而且是在"告诉用户上次崩了"这个最不该崩的时刻崩。
    //
    // ⚠ 判据更正(2026-10-14):最初这里断言 timestamp 必须为空,是【我写错了】——
    //   "timestamp=2026-0" 虽然被截断,但仍是一行合法的 key=value,
    //   解析出部分时间是【正确】行为。给用户看半个时间戳,好过什么都不显示。
    //   真正该守的是下面两条:不许崩、标记必须被清掉。
    removeMarker();
    QDir().mkpath(crash::dumpDirectory());
    {
        QFile f(markerPath());
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write("path=C:/x/a.dmp\ntimestamp=2026-0");   // 故意截断
    }
    QVERIFY(crash::hasPendingDump());

    const auto info = crash::takePendingDump();     // 关键:不许崩
    QCOMPARE(info.path, QStringLiteral("C:/x/a.dmp"));       // 完整的那行照常解析出来
    QCOMPARE(info.timestamp, QStringLiteral("2026-0"));      // 截断的那行给到能给的
    QVERIFY(!crash::hasPendingDump());
}

// --------------------------------------------------------------------
void TestCrashHandler::takeRemovesMarkerEvenWhenContentIsGarbage() {
    removeMarker();
    QDir().mkpath(crash::dumpDirectory());
    {
        QFile f(markerPath());
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write("这不是 key=value 这也不是\n\xFF\xFE binary garbage");
    }
    const auto info = crash::takePendingDump();
    QVERIFY(info.path.isEmpty());
    QVERIFY2(!crash::hasPendingDump(),
             "垃圾内容也得清掉 —— 不然每次启动都拿一份读不出来的标记弹框");
}

QTEST_MAIN(TestCrashHandler)
#include "tst_CrashHandler.moc"