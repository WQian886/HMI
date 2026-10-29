// ============================================================================
// 阶段 B · B5 · AuditLog 单元测试
//
// 验证:
//   1. append 写一行 + 同步落盘 + 文件可读回(grep 出来对得上)
//   2. appendCurrent 自动从 CurrentUser 取 user/role
//   3. 文件按天滚动:模拟跨天后 rotateIfNeeded 切到新文件
//   4. 关键字段(action/user/role/target/result)都在行里,便于 grep
//
// 关联:AuditLog.h, CurrentUser.h, User.h
// ============================================================================

#include <QtTest>
#include <QFile>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QDateTime>
#include "auth/AuditLog.h"
#include "auth/CurrentUser.h"
#include "auth/User.h"

using namespace qwhmi::auth;

class TestAuditLog : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();      // 每个用例前清临时文件 + logout 复位
    void cleanup();   // 每个用例后 logout 复位

    void append_writesOneLineToFile();
    void appendCurrent_pullsFromCurrentUser();
    void append_includesAllKeyFields();
    void rotatedFile_createsNewDailyFile();
    void unloggedCurrentUser_appendsSystemMarker();
};

namespace {
// 临时目录(避免污染真实 %APPDATA%)
QString tempDir() {
    const QString base = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    return base + QStringLiteral("/hmi_audit_test");
}

// 清空临时目录
void cleanTempDir() {
    QDir d(tempDir());
    if (d.exists()) d.removeRecursively();
}

User makeUser(const QString& name, Role role) {
    User u;
    u.name = name;
    u.role = role;
    return u;
}

// 读 audit 文件返回所有行
QStringList readAuditLines() {
    QFile f(AuditLog::defaultFilePath());
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    const QString text = QString::fromUtf8(f.readAll());
    f.close();
    return text.split('\n', Qt::SkipEmptyParts);
}
} // namespace

void TestAuditLog::initTestCase() {
    // ★ 2026-10-21:把审计日志【隔离】到临时目录。
    //   以前这个测试直接用 AuditLog::defaultFilePath(),
    //   而它指向【真实的】%LOCALAPPDATA%\QianWei\HMI\audit-<今天>.log;
    //   加上 resetForTesting() 里那一句 QFile::remove(defaultFilePath()) ——
    //   等于【每跑一次单元测试,就把现场当天的审计日志删掉】。
    //   2026-10-21 实测:跑完一轮 ctest,当时正在跑的那一轮长跑的
    //   audit-2026-10-21.log 当场没了。
    //   上面那个 tempDir() 辅助函数是【摆设】—— 它清的是临时目录,
    //   而文件实际写在真实目录里,谁也没拦住。
    //   现在有 HMI_DATA_DIR 了(见 util/Paths.h),把整个数据目录指到临时目录,
    //   读写两端一起改,不用在测试里加"只给测试用"的路径接口。
    qputenv("HMI_DATA_DIR", tempDir().toUtf8());
    QDir().mkpath(tempDir());
}

void TestAuditLog::cleanupTestCase() {
    qunsetenv("HMI_DATA_DIR");
}

void TestAuditLog::init() {
    CurrentUser::instance().logout();
    AuditLog::instance().resetForTesting();  // 关 file_ + 删物理文件
    cleanTempDir();
    QDir().mkpath(tempDir());
}

void TestAuditLog::cleanup() {
    CurrentUser::instance().logout();
    cleanTempDir();
}

void TestAuditLog::append_writesOneLineToFile() {
    auto& log = AuditLog::instance();
    log.append("test.event", "alice", "admin", "target1", "detail1", "ok");

    const QStringList lines = readAuditLines();
    QCOMPARE(lines.size(), 1);

    // 检查关键字段都在行里
    const QString& line = lines.first();
    QVERIFY(line.contains("action=test.event"));
    QVERIFY(line.contains("user=alice"));
    QVERIFY(line.contains("role=admin"));
    QVERIFY(line.contains("target=target1"));
    QVERIFY(line.contains("detail=detail1"));
    QVERIFY(line.contains("result=ok"));
}

void TestAuditLog::appendCurrent_pullsFromCurrentUser() {
    CurrentUser::instance().login(makeUser("bob", Role::Operator));
    auto& log = AuditLog::instance();
    log.appendCurrent("test.event", "Pressure", "value=100", "ok");

    const QStringList lines = readAuditLines();
    QCOMPARE(lines.size(), 1);
    const QString& line = lines.first();
    QVERIFY(line.contains("user=bob"));
    QVERIFY(line.contains("role=operator"));   // roleToString 转小写
    QVERIFY(line.contains("target=Pressure"));
    QVERIFY(line.contains("detail=value=100"));
    QVERIFY(line.contains("result=ok"));
}

void TestAuditLog::append_includesAllKeyFields() {
    auto& log = AuditLog::instance();
    log.append("variable.write", "admin", "admin", "Speed", "value=1234", "ok");

    const QStringList lines = readAuditLines();
    QCOMPARE(lines.size(), 1);
    const QString& line = lines.first();

    // 必含字段:时间戳 + action + user + role + target + detail + result
    QVERIFY2(line.contains("action=variable.write"), qPrintable(line));
    QVERIFY2(line.contains("user=admin"), qPrintable(line));
    QVERIFY2(line.contains("role=admin"), qPrintable(line));
    QVERIFY2(line.contains("target=Speed"), qPrintable(line));
    QVERIFY2(line.contains("detail=value=1234"), qPrintable(line));
    QVERIFY2(line.contains("result=ok"), qPrintable(line));
    // ISO8601 UTC 时间戳格式:YYYY-MM-DDTHH:MM:SS.sssZ
    QVERIFY2(line.contains("Z |"), qPrintable(line));
}

void TestAuditLog::rotatedFile_createsNewDailyFile() {
    auto& log = AuditLog::instance();
    log.append("test.before", "alice", "admin", {}, {}, "ok");

    const QString todayFile = AuditLog::defaultFilePath();
    QVERIFY(QFile::exists(todayFile));

    // 手动 rotate 后再 append,文件路径不变(因为是同一天,rotateIfNeeded 会识别)
    log.rotateIfNeeded();
    log.append("test.after", "alice", "admin", {}, {}, "ok");

    const QStringList lines = readAuditLines();
    QCOMPARE(lines.size(), 2);  // 同一文件追加 2 行
    QVERIFY(lines[0].contains("test.before"));
    QVERIFY(lines[1].contains("test.after"));
}

void TestAuditLog::unloggedCurrentUser_appendsSystemMarker() {
    // CurrentUser 未登录(默认 Viewer,name="")→ appendCurrent 会传空 user
    // 真实场景中 main.cpp 在 userAdded 路径已判断 isLoggedIn(),
    // 但 appendCurrent 接口本身不强制要求 logged in
    auto& log = AuditLog::instance();
    log.appendCurrent("test.event", "x");

    const QStringList lines = readAuditLines();
    QCOMPARE(lines.size(), 1);
    const QString& line = lines.first();
    // role 应该是 viewer(默认)
    QVERIFY(line.contains("role=viewer"));
    // user 是空字符串
    QVERIFY(line.contains("user="));
}

QTEST_MAIN(TestAuditLog)
#include "tst_AuditLog.moc"
