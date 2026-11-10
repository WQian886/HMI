// ============================================================================
// 阶段 B · B1.2 · UserManager 单元测试
//
// 验证:
//   1. CRUD:addUser 成功 / 名字重复拒 / 空密码拒 / 空名字拒
//   2. removeUser:成功软删除(disabled=true) / 不存在返 false
//      ★ 2026-11-10 (G-30):契约变严 —— 禁自己 / 已禁用 / 最后一个 enabled Admin 都拒
//   3. changePassword:旧密码错拒 / 旧密码对接受
//   4. authenticate:正确 / 错误密码 / 禁用用户 / 不存在
//   5. hasUsers / listUsers / count
//   6. signals:userAdded / userRemoved / passwordChanged / authenticationFailed 都触发
//   7. ★ G-30:canDisableUser(判据在公共层)/ enableUser(禁用的逆操作)/
//      enabledAdminCount / userEnabled 信号
//
// ★ 关于"改了这几条既有用例":
//   2026-11-10 (G-30)把"不能禁用最后一个 enabled Admin"这条护栏
//   从 UserManagementDialog 搬进了公共层(UserManager::removeUser)。
//   护栏本身不是新加的(对话框里本来就有),所以【没有少任何能力】,
//   只是判据换了地方 —— 而以前那个地方还有两个毛病:
//     · 它是拿【显示文本】比角色名("管理员")的,换个语言就失效;
//     · 绕过对话框的调用一个都拦不住。
//   于是"只建一个 Admin"的老用例(alice 既是唯一用户又是唯一 Admin)
//   现在会撞上护栏 —— 那不是用例写错了,是【现实里就不该允许】。
//   所以这些用例改成先摆好接手人(第二个 Admin),再停用旧的。
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
    // ★ 2026-11-10 (G-30):护栏搬进公共层之后的行为
    void removeUser_refusesLastEnabledAdmin();
    void removeUser_refusesSelf();
    void removeUser_refusesAlreadyDisabled();
    void canDisableUser_judgesInSharedLayer();
    void enabledAdminCount_countsOnlyEnabledAdmins();
    void enableUser_restoresAccess();
    void enableUser_notFoundOrAlreadyEnabled();
    void signals_userEnabled();
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
    // ★ G-30:公共层拒绝"禁用最后一个 enabled Admin" ⇒ 先摆好接手人
    m.addUser("bob", "p", Role::Admin);
    QVERIFY(m.removeUser("alice"));
    const User* u = m.findUser("alice");
    QVERIFY(u != nullptr);   // 用户仍存在(软删除)
    QVERIFY(u->disabled);    // 但被标成禁用
    QVERIFY(m.hasUsers());   // bob 还在 ⇒ 还有能登录的人
}

void TestUserManager::removeUser_notFoundReturnsFalse() {
    UserManager m;
    QVERIFY(!m.removeUser("ghost"));
}

// ★ 2026-11-10 (G-30):判据搬进公共层的第一条 ——
//   "不能禁用最后一个 enabled Admin"。
//   这条以前只写在 UserManagementDialog 里,公共层一点都不知道。
void TestUserManager::removeUser_refusesLastEnabledAdmin() {
    UserManager m;
    m.addUser("alice", "p", Role::Admin);
    QVERIFY(!m.removeUser("alice"));      // 拒
    const User* u = m.findUser("alice");
    QVERIFY(u != nullptr);
    QVERIFY(!u->disabled);                // 而且没被动过
    QVERIFY(m.hasUsers());

    // 有第二个 Admin 就禁得动
    m.addUser("bob", "p", Role::Admin);
    QVERIFY(m.removeUser("alice"));
    QVERIFY(m.findUser("alice")->disabled);

    // bob 现在是最后一个 enabled Admin ⇒ 又禁不动了
    QVERIFY(!m.removeUser("bob"));
    QVERIFY(!m.findUser("bob")->disabled);
}

void TestUserManager::removeUser_refusesSelf() {
    UserManager m;
    m.addUser("alice", "p", Role::Admin);
    m.addUser("bob", "p", Role::Admin);
    QVERIFY(!m.removeUser("alice", "alice"));   // actor 就是它自己
    QVERIFY(!m.findUser("alice")->disabled);
    // actor 传别人就禁得动
    QVERIFY(m.removeUser("alice", "bob"));
    QVERIFY(m.findUser("alice")->disabled);
}

void TestUserManager::removeUser_refusesAlreadyDisabled() {
    UserManager m;
    m.addUser("alice", "p", Role::Admin);
    m.addUser("bob", "p", Role::Admin);
    QVERIFY(m.removeUser("alice"));
    QSignalSpy spy(&m, &UserManager::userRemoved);
    QVERIFY(!m.removeUser("alice"));     // 没什么可禁的
    QCOMPARE(spy.count(), 0);             // 也不该再记一条审计
}

// ★ G-30:判据本身在公共层 —— reason 是能直接弹给用户看的人话
void TestUserManager::canDisableUser_judgesInSharedLayer() {
    UserManager m;
    m.addUser("alice", "p", Role::Admin);
    m.addUser("bob", "p", Role::Operator);
    QString reason;

    // 用户不存在
    QVERIFY(!m.canDisableUser("ghost", "alice", &reason));
    QVERIFY(!reason.isEmpty());

    // 禁自己
    QVERIFY(!m.canDisableUser("alice", "alice", &reason));
    QVERIFY(!reason.isEmpty());

    // 最后一个 enabled Admin
    QVERIFY(!m.canDisableUser("alice", "bob", &reason));
    QVERIFY(!reason.isEmpty());

    // 普通用户:可以禁
    QVERIFY(m.canDisableUser("bob", "alice", &reason));
    QVERIFY(reason.isEmpty());

    // 有第二个 Admin 之后,alice 也可以禁了
    m.addUser("carol", "p", Role::Admin);
    QVERIFY(m.canDisableUser("alice", "carol", &reason));
}

void TestUserManager::enabledAdminCount_countsOnlyEnabledAdmins() {
    UserManager m;
    QCOMPARE(m.enabledAdminCount(), 0);
    m.addUser("alice", "p", Role::Admin);
    m.addUser("bob", "p", Role::Admin);
    m.addUser("op", "p", Role::Operator);
    QCOMPARE(m.enabledAdminCount(), 2);
    m.addUser("carol", "p", Role::Admin);
    QCOMPARE(m.enabledAdminCount(), 3);
    QVERIFY(m.removeUser("alice"));
    QCOMPARE(m.enabledAdminCount(), 2);   // 被禁的不算
}

// ★ G-30:禁用的逆操作 —— 没有它,"禁用"就是单向的,账号等于报废
void TestUserManager::enableUser_restoresAccess() {
    UserManager m;
    m.addUser("alice", "p", Role::Admin);
    m.addUser("bob", "p", Role::Admin);
    QVERIFY(m.removeUser("alice"));
    QVERIFY(!m.authenticate("alice", "p"));      // 禁用了就登不上
    QVERIFY(m.enableUser("alice"));
    QVERIFY(!m.findUser("alice")->disabled);
    QVERIFY(m.authenticate("alice", "p"));       // ★ 启用之后又能登了
}

void TestUserManager::enableUser_notFoundOrAlreadyEnabled() {
    UserManager m;
    m.addUser("alice", "p", Role::Admin);
    QVERIFY(!m.enableUser("ghost"));   // 不存在
    QVERIFY(!m.enableUser("alice"));   // 本来就是启用的 ⇒ 没动它
}

void TestUserManager::signals_userEnabled() {
    UserManager m;
    m.addUser("alice", "p", Role::Admin);
    m.addUser("bob", "p", Role::Admin);
    QSignalSpy spy(&m, &UserManager::userEnabled);
    QVERIFY(m.removeUser("alice"));
    QCOMPARE(spy.count(), 0);
    QVERIFY(m.enableUser("alice"));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("alice"));
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
    m.addUser("bob", "p", Role::Admin);   // ★ G-30:禁掉最后一个 Admin 会被公共层拒
    QVERIFY(m.removeUser("alice"));
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
    QVERIFY(m.removeUser("bob"));    // bob 是 Operator —— 禁得
    QVERIFY(m.hasUsers());            // alice(Admin)还在
    QCOMPARE(m.count(), 2);           // count 包括 disabled
    // ★ G-30:alice 是最后一个 enabled Admin ⇒ 公共层拒绝
    QVERIFY(!m.removeUser("alice"));
    QVERIFY(m.hasUsers());
    // 全禁光(而且没有 Admin)才可能出现 hasUsers()==false
    UserManager m2;
    m2.addUser("op", "p", Role::Operator);
    m2.addUser("vi", "p", Role::Viewer);
    QVERIFY(m2.removeUser("op"));
    QVERIFY(m2.removeUser("vi"));
    QVERIFY(!m2.hasUsers());
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
    m.addUser("bob", "p", Role::Admin);     // ★ G-30
    m.authenticate("alice", "wrong");       // 密码错
    m.authenticate("ghost", "anything");     // 不存在
    m.removeUser("alice");
    m.authenticate("alice", "p");           // 已禁用
    QCOMPARE(spy.count(), 3);
}

QTEST_MAIN(TestUserManager)
#include "tst_UserManager.moc"