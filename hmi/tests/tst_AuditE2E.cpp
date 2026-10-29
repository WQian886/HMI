// ============================================================================
// 阶段 B · B5 · AuditLog 端到端集成测试
//
// 为什么需要这个测试:
//   tst_AuditLog 只测 AuditLog 类本身(append/appendCurrent/rotate)。
//   本测试模拟 main.cpp 的 audit 接线 + 真实调 UserManager/CurrentUser 的
//   业务流程,验证:信号链 → audit 行 → 文件 三段都通。
//   等价覆盖真实 GUI 流程(Wqian 不用操作 UI 也能拿到确定性答案)。
//
// 覆盖事件:
//   1. 首启 AdminSetupDialog → user.add (system/system)
//   2. 登录成功 → auth.login (currentUser)
//   3. 注销 → auth.logout (system/system)
//   4. 登录失败 → auth.fail (anonymous/anonymous)
//   5. 已登录后增删改 user → user.add/remove/passwordChange (currentUser)
//   6. 完整流程:admin 建 → 登录 → 建 viewer → 切 viewer → 失败登录 → 注销
//
// 注意:本测试**复制了 main.cpp 的 audit 接线 lambda**,改 main.cpp 时同步改这里
//   (简单做法,避免抽象过度)。每个 lambda 加注释指向 main.cpp 行号。
//
// 关联:AuditLog.h, UserManager.h, CurrentUser.h, UserStore.h
// ============================================================================

#include <QtTest>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QFile>
#include <QDir>
#include "auth/AuditLog.h"
#include "auth/CurrentUser.h"
#include "auth/UserManager.h"
#include "auth/UserStore.h"
#include "auth/User.h"
#include "auth/Role.h"

using namespace qwhmi::auth;

class TestAuditE2E : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();      // 每个用例前 logout + 重置 audit
    void cleanup();

    // 单事件验证
    void adminSetup_writesUserAddAsSystem();
    void login_writesAuthLogin();
    void logout_writesAuthLogout();
    void loginFail_writesAuthFail();
    void userAddAfterLogin_writesAsCurrentUser();
    void userRemove_writesAsCurrentUser();
    void passwordChange_writesAsCurrentUser();

    // 完整流程:8 步操作 → 8+ 行 audit
    void fullFlow_createsCompleteAuditTrail();
};

namespace {
// 跟 main.cpp 一样的 audit 接线(Wqian 改 main.cpp 时同步改这里)
void wireUserManagerAudit(UserManager& mgr) {
    QObject::connect(&mgr, &UserManager::userAdded, &mgr,
        [&mgr](const QString& name) {
            UserStore::save(mgr);
            const auto* u = mgr.findUser(name);
            const QString role = u ? roleToString(u->role) : QStringLiteral("unknown");
            auto& audit = AuditLog::instance();
            if (CurrentUser::instance().isLoggedIn()) {
                audit.appendCurrent("user.add", name, QStringLiteral("role=%1").arg(role));
            } else {
                audit.append("user.add", "system", "system", name, QStringLiteral("role=%1").arg(role));
            }
        });
    QObject::connect(&mgr, &UserManager::userRemoved, &mgr,
        [&mgr](const QString& name) {
            UserStore::save(mgr);
            AuditLog::instance().appendCurrent("user.remove", name);
        });
    QObject::connect(&mgr, &UserManager::passwordChanged, &mgr,
        [&mgr](const QString& name) {
            UserStore::save(mgr);
            AuditLog::instance().appendCurrent("user.passwordChange", name);
        });
    QObject::connect(&mgr, &UserManager::authenticationFailed, &mgr,
        [](const QString& name) {
            AuditLog::instance().append("auth.fail", "anonymous", "anonymous",
                                        name, {}, QStringLiteral("fail"));
        });
}

void wireCurrentUserAudit() {
    QObject::connect(&CurrentUser::instance(), &CurrentUser::userLoggedIn,
        [](const QString& name) {
            AuditLog::instance().append("auth.login", name,
                roleToString(CurrentUser::instance().role()));
        });
    QObject::connect(&CurrentUser::instance(), &CurrentUser::userLoggedOut, []() {
        AuditLog::instance().append("auth.logout", "system", "system");
    });
}

// 临时目录(避免污染真实 %APPDATA%)
QString tempDir() {
    return QStandardPaths::writableLocation(QStandardPaths::TempLocation)
           + QStringLiteral("/hmi_audit_e2e_test");
}
void cleanTempDir() {
    QDir d(tempDir());
    if (d.exists()) d.removeRecursively();
}

// 读 audit 文件返回所有行
QStringList readAuditLines() {
    QFile f(AuditLog::defaultFilePath());
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    const QString text = QString::fromUtf8(f.readAll());
    f.close();
    return text.split('\n', Qt::SkipEmptyParts);
}

// 找含特定 action 的第一行
QString findLine(const QStringList& lines, const QString& action) {
    for (const QString& line : lines) {
        if (line.contains(QStringLiteral("action=%1").arg(action))) return line;
    }
    return {};
}

User makeUser(const QString& name, Role role) {
    User u;
    u.name = name;
    u.role = role;
    return u;
}
} // namespace

void TestAuditE2E::init() {
    CurrentUser::instance().logout();
    AuditLog::instance().resetForTesting();
    cleanTempDir();
    QDir().mkpath(tempDir());
}

void TestAuditE2E::initTestCase() {
    // ★ 2026-10-21:把审计日志【隔离】到临时目录 —— 理由同 tst_AuditLog。
    //   AuditLog::defaultFilePath() 指向真实的 %LOCALAPPDATA%\QianWei\HMI\,
    //   而 resetForTesting() 里有一句 QFile::remove(defaultFilePath())
    //   ⇒ 每跑一次单元测试,现场当天的审计日志就没了。
    //   2026-10-21 实测踩到过:跑完 ctest,正在跑的那一轮长跑的
    //   audit-2026-10-21.log 当场消失。
    qputenv("HMI_DATA_DIR", tempDir().toUtf8());
    QDir().mkpath(tempDir());
}

void TestAuditE2E::cleanupTestCase() {
    qunsetenv("HMI_DATA_DIR");
}

void TestAuditE2E::cleanup() {
    CurrentUser::instance().logout();
    cleanTempDir();
}

void TestAuditE2E::adminSetup_writesUserAddAsSystem() {
    UserManager mgr;
    wireUserManagerAudit(mgr);

    // 模拟首启:CurrentUser 未登录时 addUser
    QVERIFY(mgr.addUser("admin", "pass123", Role::Admin));

    const QStringList lines = readAuditLines();
    QVERIFY(lines.size() >= 1);
    const QString line = findLine(lines, "user.add");
    QVERIFY2(line.contains("user=system"), qPrintable(line));      // ← 首启 system/system
    QVERIFY2(line.contains("role=system"), qPrintable(line));
    QVERIFY2(line.contains("target=admin"), qPrintable(line));
    QVERIFY2(line.contains("detail=role=admin"), qPrintable(line));
}

void TestAuditE2E::login_writesAuthLogin() {
    UserManager mgr;
    wireUserManagerAudit(mgr);
    wireCurrentUserAudit();
    mgr.addUser("admin", "pass", Role::Admin);

    AuditLog::instance().resetForTesting();   // 清掉 user.add 的噪音

    CurrentUser::instance().login(makeUser("admin", Role::Admin));

    const QStringList lines = readAuditLines();
    const QString line = findLine(lines, "auth.login");
    QVERIFY2(line.contains("user=admin"), qPrintable(line));
    QVERIFY2(line.contains("role=admin"), qPrintable(line));
    QVERIFY2(line.contains("result=ok"), qPrintable(line));
}

void TestAuditE2E::logout_writesAuthLogout() {
    UserManager mgr;
    wireUserManagerAudit(mgr);
    wireCurrentUserAudit();
    mgr.addUser("admin", "pass", Role::Admin);
    CurrentUser::instance().login(makeUser("admin", Role::Admin));

    AuditLog::instance().resetForTesting();

    CurrentUser::instance().logout();

    const QStringList lines = readAuditLines();
    const QString line = findLine(lines, "auth.logout");
    QVERIFY2(line.contains("action=auth.logout"), qPrintable(line));
    QVERIFY2(line.contains("user=system"), qPrintable(line));   // 注销时 user/role 已清
}

void TestAuditE2E::loginFail_writesAuthFail() {
    UserManager mgr;
    wireUserManagerAudit(mgr);
    wireCurrentUserAudit();
    mgr.addUser("admin", "pass", Role::Admin);

    AuditLog::instance().resetForTesting();

    QVERIFY(!mgr.authenticate("admin", "wrongpass"));
    QVERIFY(!mgr.authenticate("admin", "wrongpass2"));
    QVERIFY(!mgr.authenticate("admin", "wrongpass3"));

    const QStringList lines = readAuditLines();
    const int failCount = lines.filter("action=auth.fail").size();
    QCOMPARE(failCount, 3);
    QVERIFY(lines.first().contains("user=anonymous"));
    QVERIFY(lines.first().contains("target=admin"));
    QVERIFY(lines.first().contains("result=fail"));
}

void TestAuditE2E::userAddAfterLogin_writesAsCurrentUser() {
    UserManager mgr;
    wireUserManagerAudit(mgr);
    wireCurrentUserAudit();
    mgr.addUser("admin", "pass", Role::Admin);
    CurrentUser::instance().login(makeUser("admin", Role::Admin));

    AuditLog::instance().resetForTesting();

    // 已登录 admin 增 viewer
    mgr.addUser("viewer", "pass", Role::Viewer);

    const QStringList lines = readAuditLines();
    const QString line = findLine(lines, "user.add");
    QVERIFY2(line.contains("user=admin"), qPrintable(line));     // ← actor 是当前 admin
    QVERIFY2(line.contains("role=admin"), qPrintable(line));
    QVERIFY2(line.contains("target=viewer"), qPrintable(line));
    QVERIFY2(line.contains("detail=role=viewer"), qPrintable(line));
}

void TestAuditE2E::userRemove_writesAsCurrentUser() {
    UserManager mgr;
    wireUserManagerAudit(mgr);
    wireCurrentUserAudit();
    mgr.addUser("admin", "pass", Role::Admin);
    mgr.addUser("viewer", "pass", Role::Viewer);
    CurrentUser::instance().login(makeUser("admin", Role::Admin));

    AuditLog::instance().resetForTesting();

    QVERIFY(mgr.removeUser("viewer"));

    const QStringList lines = readAuditLines();
    const QString line = findLine(lines, "user.remove");
    QVERIFY2(line.contains("user=admin"), qPrintable(line));
    QVERIFY2(line.contains("target=viewer"), qPrintable(line));
}

void TestAuditE2E::passwordChange_writesAsCurrentUser() {
    UserManager mgr;
    wireUserManagerAudit(mgr);
    wireCurrentUserAudit();
    mgr.addUser("admin", "oldpass", Role::Admin);
    CurrentUser::instance().login(makeUser("admin", Role::Admin));

    AuditLog::instance().resetForTesting();

    QVERIFY(mgr.changePassword("admin", "oldpass", "newpass"));

    const QStringList lines = readAuditLines();
    const QString line = findLine(lines, "user.passwordChange");
    QVERIFY2(line.contains("user=admin"), qPrintable(line));
    QVERIFY2(line.contains("target=admin"), qPrintable(line));
}

void TestAuditE2E::fullFlow_createsCompleteAuditTrail() {
    UserManager mgr;
    wireUserManagerAudit(mgr);
    wireCurrentUserAudit();

    // 阶段 1:首启建 admin(未登录 → system)
    mgr.addUser("admin", "pass", Role::Admin);

    // 阶段 2:admin 登录
    CurrentUser::instance().login(makeUser("admin", Role::Admin));

    // 阶段 3:admin 增 viewer
    mgr.addUser("viewer", "pass", Role::Viewer);

    // 阶段 4:admin 切到 viewer(模拟托盘切用户)
    CurrentUser::instance().logout();
    CurrentUser::instance().login(makeUser("viewer", Role::Viewer));

    // 阶段 5:viewer 输错密码 3 次(触发登录锁定)
    mgr.authenticate("viewer", "bad1");
    mgr.authenticate("viewer", "bad2");
    mgr.authenticate("viewer", "bad3");

    // 阶段 6:viewer 注销
    CurrentUser::instance().logout();

    // 验证:audit 文件应该至少有这些 action(顺序不限)
    const QStringList lines = readAuditLines();
    QVERIFY2(!findLine(lines, "user.add").isEmpty(),       qPrintable(lines.join("\n")));
    QVERIFY2(!findLine(lines, "auth.login").isEmpty(),      qPrintable(lines.join("\n")));
    QVERIFY2(lines.filter("action=auth.fail").size() >= 3, qPrintable(lines.join("\n")));
    QVERIFY2(lines.filter("action=auth.logout").size() >= 2, qPrintable(lines.join("\n")));  // 手动 + 自动

    // 关键验证:首启 user.add 是 system,登录后 user.add 是 admin
    const QStringList userAddLines = lines.filter("action=user.add");
    QCOMPARE(userAddLines.size(), 2);
    QVERIFY(userAddLines[0].contains("user=system"));   // admin(首启)
    QVERIFY(userAddLines[1].contains("user=admin"));    // viewer(admin 加)
}

QTEST_MAIN(TestAuditE2E)
#include "tst_AuditE2E.moc"
