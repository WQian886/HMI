// ============================================================================
// 阶段 B · B1 · PasswordHasher 单元测试
//
// 验证:
//   1. generateSalt 每次返回不同的 32 字符 hex
//   2. 相同明文 + 相同 salt → 相同 hash
//   3. 相同明文 + 不同 salt → 不同 hash(防彩虹表)
//   4. verifyPassword(true) / verifyPassword(false) 正确分支
//   5. 空字符串、长度错误的 salt/hash 都不崩
//
// 关联:PasswordHasher.h
// ============================================================================

#include <QtTest>
#include "auth/PasswordHasher.h"
#include "auth/Role.h"
#include "auth/User.h"

using namespace qwhmi::auth;

class TestPasswordHasher : public QObject {
    Q_OBJECT

private slots:
    void generateSalt_uniqueAndValid();
    void hashPassword_deterministicForSameSalt();
    void hashPassword_differsAcrossSalts();
    void verifyPassword_happyPath();
    void verifyPassword_wrongPlain();
    void hashPassword_emptyPlainReturnsEmpty();
    void hashPassword_badSaltLengthReturnsEmpty();
    void verifyPassword_badHashLengthReturnsFalse();
    void role_stringRoundTrip();
    void role_canEditAndWritePredicates();
    void user_namesEqualCaseInsensitive();
};

void TestPasswordHasher::generateSalt_uniqueAndValid() {
    const QString s1 = generateSalt();
    const QString s2 = generateSalt();
    QCOMPARE(s1.length(), 32);          // 16 bytes = 32 hex chars
    QCOMPARE(s2.length(), 32);
    QVERIFY(s1 != s2);                  // 每次随机
}

void TestPasswordHasher::hashPassword_deterministicForSameSalt() {
    const QString salt = generateSalt();
    QCOMPARE(hashPassword("hunter2", salt), hashPassword("hunter2", salt));
}

void TestPasswordHasher::hashPassword_differsAcrossSalts() {
    // 不同 salt → 不同 hash(防彩虹表)
    const QString salt1 = generateSalt();
    const QString salt2 = generateSalt();
    QVERIFY(salt1 != salt2);
    const QString h1 = hashPassword("hunter2", salt1);
    const QString h2 = hashPassword("hunter2", salt2);
    QVERIFY(!h1.isEmpty());
    QVERIFY(!h2.isEmpty());
    QVERIFY(h1 != h2);
}

void TestPasswordHasher::verifyPassword_happyPath() {
    const QString salt = generateSalt();
    const QString hash = hashPassword("hunter2", salt);
    QVERIFY(verifyPassword("hunter2", salt, hash));
}

void TestPasswordHasher::verifyPassword_wrongPlain() {
    const QString salt = generateSalt();
    const QString hash = hashPassword("hunter2", salt);
    QVERIFY(!verifyPassword("hunter3", salt, hash));   // 密码错一个字符
    QVERIFY(!verifyPassword("", salt, hash));         // 空密码
    QVERIFY(!verifyPassword("Hunter2", salt, hash));  // 大小写也算错
}

void TestPasswordHasher::hashPassword_emptyPlainReturnsEmpty() {
    const QString salt = generateSalt();
    QCOMPARE(hashPassword("", salt), QString{});
}

void TestPasswordHasher::hashPassword_badSaltLengthReturnsEmpty() {
    // salt 应是 32 hex 字符(16 字节)
    QCOMPARE(hashPassword("hunter2", "tooshort"), QString{});
    QCOMPARE(hashPassword("hunter2", QString(33, 'a')), QString{});
}

void TestPasswordHasher::verifyPassword_badHashLengthReturnsFalse() {
    const QString salt = generateSalt();
    QVERIFY(!verifyPassword("hunter2", salt, "abc"));             // 太短
    QVERIFY(!verifyPassword("hunter2", salt, QString(63, 'a')));   // 不是 64
    QVERIFY(!verifyPassword("hunter2", salt, QString(65, 'a')));   // 多 1
}

void TestPasswordHasher::role_stringRoundTrip() {
    for (Role r : { Role::Admin, Role::Engineer, Role::Operator, Role::Viewer }) {
        QCOMPARE(roleFromString(roleToString(r)), r);
    }
    // 不认识的字符串默认 Viewer(最低权限,安全的失败模式)
    QCOMPARE(roleFromString("god"), Role::Viewer);
    QCOMPARE(roleFromString(""), Role::Viewer);
}

void TestPasswordHasher::role_canEditAndWritePredicates() {
    QVERIFY(roleCanEditProject(Role::Admin));
    QVERIFY(roleCanEditProject(Role::Engineer));
    QVERIFY(!roleCanEditProject(Role::Operator));
    QVERIFY(!roleCanEditProject(Role::Viewer));

    QVERIFY(roleCanWriteValue(Role::Admin));
    QVERIFY(roleCanWriteValue(Role::Engineer));
    QVERIFY(roleCanWriteValue(Role::Operator));
    QVERIFY(!roleCanWriteValue(Role::Viewer));
}

void TestPasswordHasher::user_namesEqualCaseInsensitive() {
    QVERIFY(User::namesEqual("admin", "ADMIN"));
    QVERIFY(User::namesEqual("  admin  ", "ADMIN"));     // 去前后空格
    QVERIFY(!User::namesEqual("admin", "administrator"));
    QVERIFY(!User::namesEqual("admin", ""));
}

QTEST_MAIN(TestPasswordHasher)
#include "tst_PasswordHasher.moc"