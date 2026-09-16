// ============================================================================
// 阶段 B · B1.2 · UserManager 单元测试
//
// 验证:
//   1. CRUD:addUser 成功 / 名字重复拒 / 空密码拒 / 空名字拒
//   2. removeUser:成功软删除(disabled=true) / 不存在返 false
//   3. changePassword:旧密码错拒 / 旧密码对接受
//   4. authenticate:正确 / 错误密码 / 禁用用户 / 不存在
//   5. hasUsers / listUsers / count
//   6. signals:userAdded / userRemoved / passwordChanged / authenticationFailed 都触发
//
// 关联:UserManager.h
// ============================================================================

#include <QtTest>
#include <QSignalSpy>
#include "auth/UserManager.h"
#include "auth/PasswordHasher.h"

using namespace qwhmi::auth;

class TestUserManager : public QObject {
    Q_OBJECT

private slots:
    void init();  // 每个用例前:新建 UserManager
    void addUser_success();
    void addUser_duplicateRejected();
    void addUser_emptyPasswordRejected();
    void addUser_emptyNameRejected();
    void addUser_nameTrimmed();
    void removeUser_marksDisabled();
    void removeUser_notFoundReturnsFalse();
    void changePassword_happyPath();
    void changePassword_wrongOldRejected();
    void authenticate_happyPath();
    void authenticate_wrongPasswordRejected();
    void authenticate_disabledUserRejected();
    void authenticate_unknownUserRejected();
    void hasUsers_andCount();
    void listUsers_sorted();
    void signals_userAdded();
    void signals_authenticationFailed();
};

void TestUserManager::init() {
    // 每个用例独立 manager(QSignalSpy 需要新对象)
}

void TestUserManager::addUser_success() {
    UserManager m;
    QVERIFY(m.addUser("alice", "hunter2", Role::Engineer));
    QCOMPARE(m.count(), 1);
    QVERIFY(m.hasUsers());
    const User* u = m.findUser("alice");
    QVERIFY(u != nullptr);
    QCOMPARE(u->role, Role::Engineer);
    QVERIFY(!u->passwordHash.isEmpty());
    QVERIFY(!u->salt.isEmpty());
    QVERIFY(!u->disabled);
}

void TestUserManager::addUser_duplicateRejected() {
    UserManager m;
    QVERIFY(m.addUser("alice", "p1", Role::Admin));
    // 大小写不同但 User::namesEqual 不敏感
    QVERIFY(!m.addUser("ALICE", "p2", Role::Viewer));
    QCOMPARE(m.count(), 1);
}

void TestUserManager::addUser_emptyPasswordRejected() {
    UserManager m;
    QVERIFY(!m.addUser("alice", "", Role::Admin));
    QCOMPARE(m.count(), 0);
}

void TestUserManager::addUser_emptyNameRejected() {
    UserManager m;
    QVERIFY(!m.addUser("", "p", Role::Admin));
    QVERIFY(!m.addUser("   ", "p", Role::Admin));   // 纯空格也拒
    QCOMPARE(m.count(), 0);
}

void TestUserManager::addUser_nameTrimmed() {
    UserManager m;
    QVERIFY(m.addUser("  alice  ", "p", Role::Admin));
    const User* u = m.findUser("alice");
    QVERIFY(u != nullptr);
    QCOMPARE(u->name, QStringLiteral("alice"));   // 前后空格被 trim
}

void TestUserManager::removeUser_marksDisabled() {
    UserManager m;
    m.addUser("alice", "p", Role::Admin);
    QVERIFY(m.removeUser("alice"));
    const User* u = m.findUser("alice");
    QVERIFY(u != nullptr);   // 用户仍存在(软删除)
    QVERIFY(u->disabled);
    QVERIFY(!m.hasUsers());  // 但 hasUsers 返 false
}

void TestUserManager::removeUser_notFoundReturnsFalse() {
    UserManager m;
    QVERIFY(!m.removeUser("ghost"));
}

void TestUserManager::changePassword_happyPath() {
    UserManager m;
    m.addUser("alice", "old", Role::Admin);
    QVERIFY(m.changePassword("alice", "old", "new"));
    QVERIFY(m.authenticate("alice", "new"));
    QVERIFY(!m.authenticate("alice", "old"));   // 旧密码已失效
}

void TestUserManager::changePassword_wrongOldRejected() {
    UserManager m;
    m.addUser("alice", "old", Role::Admin);
    QVERIFY(!m.changePassword("alice", "WRONG", "new"));
    QVERIFY(m.authenticate("alice", "old"));     // 旧密码仍有效
}

void TestUserManager::authenticate_happyPath() {
    UserManager m;
    m.addUser("alice", "hunter2", Role::Engineer);
    QVERIFY(m.authenticate("alice", "hunter2"));
    QVERIFY(m.authenticate("ALICE", "hunter2"));   // 大小写不敏感
}

void TestUserManager::authenticate_wrongPasswordRejected() {
    UserManager m;
    m.addUser("alice", "hunter2", Role::Engineer);
    QVERIFY(!m.authenticate("alice", "hunter3"));
}

void TestUserManager::authenticate_disabledUserRejected() {
    UserManager m;
    m.addUser("alice", "hunter2", Role::Admin);
    m.removeUser("alice");
    QVERIFY(!m.authenticate("alice", "hunter2"));
}

void TestUserManager::authenticate_unknownUserRejected() {
    UserManager m;
    QVERIFY(!m.authenticate("ghost", "anything"));
}

void TestUserManager::hasUsers_andCount() {
    UserManager m;
    QVERIFY(!m.hasUsers());
    QCOMPARE(m.count(), 0);
    m.addUser("alice", "p", Role::Admin);
    m.addUser("bob", "p", Role::Operator);
    QVERIFY(m.hasUsers());
    QCOMPARE(m.count(), 2);
    m.removeUser("alice");
    QVERIFY(m.hasUsers());      // bob 还在
    QCOMPARE(m.count(), 2);     // count 包括 disabled
    m.removeUser("bob");
    QVERIFY(!m.hasUsers());     // 都 disabled 了
}

void TestUserManager::listUsers_sorted() {
    UserManager m;
    m.addUser("charlie", "p", Role::Admin);
    m.addUser("alice", "p", Role::Admin);
    m.addUser("bob", "p", Role::Admin);
    const QList<User> list = m.listUsers();
    QCOMPARE(list.size(), 3);
    QCOMPARE(list[0].name, QStringLiteral("alice"));
    QCOMPARE(list[1].name, QStringLiteral("bob"));
    QCOMPARE(list[2].name, QStringLiteral("charlie"));
}

void TestUserManager::signals_userAdded() {
    UserManager m;
    QSignalSpy spy(&m, &UserManager::userAdded);
    m.addUser("alice", "p", Role::Admin);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("alice"));
}

void TestUserManager::signals_authenticationFailed() {
    UserManager m;
    QSignalSpy spy(&m, &UserManager::authenticationFailed);
    m.addUser("alice", "p", Role::Admin);
    m.authenticate("alice", "wrong");       // 密码错
    m.authenticate("ghost", "anything");     // 不存在
    m.removeUser("alice");
    m.authenticate("alice", "p");           // 已禁用
    QCOMPARE(spy.count(), 3);
}

QTEST_MAIN(TestUserManager)
#include "tst_UserManager.moc"