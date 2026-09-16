// ============================================================================
// 阶段 B · B1.3 · UserStore 单元测试
//
// 验证:
//   1. JSON 序列化往返(单个用户、多个用户、空列表)
//   2. 异常 JSON:解析失败返 false / schema_version 不匹配
//   3. save/load 端到端:写到 temp文件 → load 回 mgr → 用户数据一致
//   4. 文件不存在:load 返 0 + 不崩
//
// 关联:UserStore.h
// ============================================================================

#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include "auth/UserStore.h"
#include "auth/UserManager.h"

using namespace qwhmi::auth;

class TestUserStore : public QObject {
    Q_OBJECT
private slots:
    void roundTrip_singleUser();
    void roundTrip_multipleUsers();
    void roundTrip_emptyList();
    void fromJsonString_badJsonReturnsFalse();
    void fromJsonString_wrongSchemaReturnsFalse();
    void fromJsonString_skipsIncompleteRecords();
    void save_load_endToEnd();
    void load_fileNotFoundReturnsZero();
    void appendLoadedUser_skipsDuplicate();
};

void TestUserStore::roundTrip_singleUser() {
    UserManager m;
    QVERIFY(m.addUser("alice", "hunter2", Role::Engineer));
    const QString json = UserStore::toJsonString(m.listUsers());
    QVERIFY(json.contains("alice"));
    QVERIFY(json.contains("engineer"));

    UserManager m2;
    bool ok = false;
    QList<User> users = UserStore::fromJsonString(json, &ok);
    QVERIFY(ok);
    QCOMPARE(users.size(), 1);
    QCOMPARE(users[0].name, QStringLiteral("alice"));
    QCOMPARE(users[0].role, Role::Engineer);
    QVERIFY(!users[0].passwordHash.isEmpty());
    QVERIFY(!users[0].salt.isEmpty());
}

void TestUserStore::roundTrip_multipleUsers() {
    UserManager m;
    m.addUser("alice",   "p1", Role::Admin);
    m.addUser("bob",     "p2", Role::Operator);
    m.addUser("charlie", "p3", Role::Viewer);
    m.removeUser("bob");  // 软删除

    UserManager m2;
    bool ok = false;
    QList<User> users = UserStore::fromJsonString(UserStore::toJsonString(m.listUsers()), &ok);
    QVERIFY(ok);
    QCOMPARE(users.size(), 3);
    // bob 还在但 disabled=true
    bool foundBob = false;
    for (const User& u : users) {
        if (u.name == "bob") {
            foundBob = true;
            QVERIFY(u.disabled);
        }
    }
    QVERIFY(foundBob);
}

void TestUserStore::roundTrip_emptyList() {
    UserManager m;
    const QString json = UserStore::toJsonString(m.listUsers());
    QVERIFY(json.contains("users"));
    bool ok = false;
    QList<User> users = UserStore::fromJsonString(json, &ok);
    QVERIFY(ok);
    QCOMPARE(users.size(), 0);
}

void TestUserStore::fromJsonString_badJsonReturnsFalse() {
    bool ok = true;
    QList<User> users = UserStore::fromJsonString("{not valid json", &ok);
    QVERIFY(!ok);
    QCOMPARE(users.size(), 0);
}

void TestUserStore::fromJsonString_wrongSchemaReturnsFalse() {
    bool ok = true;
    QList<User> users = UserStore::fromJsonString(R"({"schema_version": 99, "users": []})", &ok);
    QVERIFY(!ok);
    QCOMPARE(users.size(), 0);
}

void TestUserStore::fromJsonString_skipsIncompleteRecords() {
    // name 为空 → 跳过;其他字段填全
    const QString json = R"(
        {
          "schema_version": 1,
          "users": [
            {"name": "",      "role": "admin", "passwordHash": "h", "salt": "s", "createdAt": "", "disabled": false},
            {"name": "good",  "role": "admin", "passwordHash": "h", "salt": "s", "createdAt": "", "disabled": false}
          ]
        }
    )";
    bool ok = false;
    QList<User> users = UserStore::fromJsonString(json, &ok);
    QVERIFY(ok);
    QCOMPARE(users.size(), 1);   // 只有 good 通过
    QCOMPARE(users[0].name, QStringLiteral("good"));
}

void TestUserStore::save_load_endToEnd() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("users.json");

    UserManager m1;
    QVERIFY(m1.addUser("alice", "hunter2", Role::Engineer));
    QVERIFY(UserStore::save(m1, path));

    UserManager m2;
    const int loaded = UserStore::load(m2, path);
    QCOMPARE(loaded, 1);
    QCOMPARE(m2.count(), 1);
    const User* u = m2.findUser("alice");
    QVERIFY(u != nullptr);
    QCOMPARE(u->role, Role::Engineer);
    // 关键:能 authenticate(load 回来的 hash 必须能 verify 原始密码)
    QVERIFY(m2.authenticate("alice", "hunter2"));
}

void TestUserStore::load_fileNotFoundReturnsZero() {
    UserManager m;
    const int loaded = UserStore::load(m, "/path/that/does/not/exist/users.json");
    QCOMPARE(loaded, 0);
    QCOMPARE(m.count(), 0);
}

void TestUserStore::appendLoadedUser_skipsDuplicate() {
    UserManager m;
    m.addUser("alice", "p1", Role::Admin);
    // 模拟 JSON 已存在 alice,再次 load
    User u;
    u.name         = "alice";
    u.role         = Role::Viewer;
    u.passwordHash = "x";
    u.salt         = "y";
    m.appendLoadedUser(u);  // 应该被 skip
    QCOMPARE(m.count(), 1);
    QCOMPARE(m.findUser("alice")->role, Role::Admin);  // 原 admin 不被覆盖
}

QTEST_MAIN(TestUserStore)
#include "tst_UserStore.moc"