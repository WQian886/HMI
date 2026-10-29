// ============================================================================
// tst_Paths.cpp —— 应用数据目录(HMI_DATA_DIR 覆盖)的钉子测试
//
// 这个测试为什么存在
// ------------------
// 2026-10-21 的现场:一次 24h 长跑要跟"已经在跑的另一个实例"并存。
// 两个实例必须各写各的 —— 日志、history.db(SQLite)、审计日志、崩溃标记,
// 任意一个共用都会互相写坏(实测:第二个实例一开就把同名的
// audit-2026-10-21.log 截断了,在跑的那一轮的审计记录当场没了)。
//
// 第一反应是"改 LOCALAPPDATA 环境变量就行"——
// 【那是错的】:Qt 在 Windows 上用 SHGetKnownFolderPath 解析 LocalAppData,
// 那个 API 认的是"当前登录用户的真实路径",不认环境变量。
// 实测把 LOCALAPPDATA 指到别处再起一个实例,它照样写进真实的
// %LOCALAPPDATA%\QianWei\HMI\。⇒ 只能让应用自己认一个开关。
//
// 于是有了 HMI_DATA_DIR。这个文件钉住三件事:
//   1. 没设这个变量时,路径跟改动前【逐字节相同】(不许偷偷换地方)
//   2. 设了它,【所有】用数据目录的地方都跟着走 —— 不是只有日志跟着走
//   3. 设成空串,当没设(空串会算成"根目录",那是灾难性的默认)
//
// ★ 第 2 条是本文件的核心。只测 util::appDataDir() 本身是【弱】的:
//   真正的风险是"某个调用方忘了跟着走" ——
//   那种漏网的调用方在【没设覆盖】时完全看不出来,
//   一直到两个实例真打起来才出事。
//   所以下面逐个点名:users.json / 审计日志 / 崩溃 dump / history.db / 日志目录。
// ============================================================================

#include <QtTest>
#include <QDate>
#include <QDir>
#include <QFile>
#include <QStandardPaths>

#include "util/Paths.h"

#include "auth/AuditLog.h"
#include "auth/UserStore.h"
#include "crash/CrashHandler.h"
#include "history/HistoryRecorder.h"
#include "logging/Logging.h"

using namespace qwhmi;

namespace {

// 本轮用的私有目录。放在临时目录里,理由和 tst_CrashHandler 一样:
// 这类测试【绝不能】碰到真实的 %LOCALAPPDATA%\QianWei\HMI\。
QString privateDir() {
    return QDir::tempPath() + QStringLiteral("/tst_Paths/instance");
}

// 默认路径(没设 HMI_DATA_DIR 时应该算出来的那个)
QString defaultDir() {
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
         + QStringLiteral("/") + QString::fromLatin1(util::kOrgName)
         + QStringLiteral("/") + QString::fromLatin1(util::kAppName);
}

} // namespace

class TestPaths : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    // ★ 1. 默认路径没变
    void defaultIsUnchanged();
    void defaultIsIndependentOfOrgAppMetadata();

    // ★ 2. 覆盖生效
    void overrideRedirectsHelper();
    void emptyOverrideFallsBackToDefault();
    void overrideIsIndependentOfOrgAppMetadata();

    // ★ 3. 所有用数据目录的地方都跟着走
    void usersJsonFollowsTheOverride();
    void auditLogFollowsTheOverride();
    void crashDumpFollowsTheOverride();
    void historyDbFollowsTheOverride();
    void logDirectoryFollowsTheOverride();
};

void TestPaths::init() {
    // 每条用例之前都把覆盖开关清干净。
    // 为什么 per-test 清:一条用例中途失败,不能让覆盖开关残留到后面 ——
    // "一个用例失败、后面全体受害"的连乘,测试里最常见的一种假绿。
    qunsetenv("HMI_DATA_DIR");
    qunsetenv("HMI_CRASH_DIR");
}

void TestPaths::cleanup() {
    qunsetenv("HMI_DATA_DIR");
    qunsetenv("HMI_CRASH_DIR");
}

// --------------------------------------------------------------------
void TestPaths::defaultIsUnchanged() {
    // 没设 HMI_DATA_DIR ⇒ 路径必须跟改动前完全一样。
    // 这一条是"覆盖开关【不改变默认行为】"的保证 ——
    // 现场的 %LOCALAPPDATA%\QianWei\HMI\ 还在用,不许被这个改动挪走。
    QCOMPARE(util::appDataDir(), defaultDir());
}

void TestPaths::defaultIsIndependentOfOrgAppMetadata() {
    // ★ 默认路径不读 QCoreApplication 的 org/app 全局状态。
    //   崩溃 dump 那条路已经因为"调用方什么时候设的"漂移过一次,
    //   整整 28 天丢现场(见 crash/CrashHandler.h 顶上的注释)。
    //   数据目录是同一类问题,不能重蹈覆辙。
    const QString before = util::appDataDir();
    QCoreApplication::setOrganizationName(QString());
    QCoreApplication::setApplicationName(QString());
    QCOMPARE(util::appDataDir(), before);
    QCoreApplication::setOrganizationName(QStringLiteral("QianWei"));
    QCoreApplication::setApplicationName(QStringLiteral("HMI"));
}

// --------------------------------------------------------------------
void TestPaths::overrideRedirectsHelper() {
    const QString want = privateDir();
    qputenv("HMI_DATA_DIR", want.toUtf8());
    QCOMPARE(util::appDataDir(), QDir::toNativeSeparators(want));
}

void TestPaths::emptyOverrideFallsBackToDefault() {
    // ★ 空串必须当"没设" ——
    //   空串会算成"根目录"(C:/),那意味着日志、users.json 全写到盘根上。
    //   这是那种"配置项没填"就会踩的默认值,必须钉死。
    qputenv("HMI_DATA_DIR", QByteArray(""));
    QCOMPARE(util::appDataDir(), defaultDir());
}

void TestPaths::overrideIsIndependentOfOrgAppMetadata() {
    const QString want = privateDir();
    qputenv("HMI_DATA_DIR", want.toUtf8());
    const QString before = util::appDataDir();
    QCoreApplication::setOrganizationName(QString());
    QCoreApplication::setApplicationName(QString());
    QCOMPARE(util::appDataDir(), before);
    QCoreApplication::setOrganizationName(QStringLiteral("QianWei"));
    QCoreApplication::setApplicationName(QStringLiteral("HMI"));
}

// --------------------------------------------------------------------
void TestPaths::usersJsonFollowsTheOverride() {
    const QString want = privateDir();
    qputenv("HMI_DATA_DIR", want.toUtf8());
    QCOMPARE(auth::UserStore::defaultFilePath(),
             QDir::toNativeSeparators(want) + QStringLiteral("/users.json"));
}

void TestPaths::auditLogFollowsTheOverride() {
    // 审计日志按天命名 —— 两个实例写同一份会互相截断,
    // 这正是 2026-10-21 踩到的那一刀。
    const QString want = privateDir();
    qputenv("HMI_DATA_DIR", want.toUtf8());
    const QString today = QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd"));
    QCOMPARE(auth::AuditLog::defaultFilePath(),
             QDir::toNativeSeparators(want) + QStringLiteral("/audit-") + today
             + QStringLiteral(".log"));
}

void TestPaths::crashDumpFollowsTheOverride() {
    // 崩溃标记 pending.txt 也在数据目录里 ——
    // 一边 takePendingDump() 取走,另一边就丢现场。
    const QString want = privateDir();
    qputenv("HMI_DATA_DIR", want.toUtf8());
    QCOMPARE(crash::dumpDirectory(),
             QDir::toNativeSeparators(want) + QStringLiteral("/crash"));
}

void TestPaths::historyDbFollowsTheOverride() {
    // history.db 是 SQLite —— 两个进程同时写会互相锁死。
    const QString want = privateDir();
    qputenv("HMI_DATA_DIR", want.toUtf8());
    QCOMPARE(history::HistoryRecorder::defaultDbPath(),
             QDir::toNativeSeparators(want) + QStringLiteral("/history.db"));
}

void TestPaths::logDirectoryFollowsTheOverride() {
    const QString want = privateDir();
    qputenv("HMI_DATA_DIR", want.toUtf8());
    QCOMPARE(logging::logDirectory(),
             QDir::toNativeSeparators(want) + QStringLiteral("/logs"));
}

QTEST_MAIN(TestPaths)
#include "tst_Paths.moc"
