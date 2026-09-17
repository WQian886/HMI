// ============================================================================
// 阶段 B · B2.2 · CurrentUser 单元测试
//
// 关联:CurrentUser.h
// ============================================================================

#include <QtTest>
#include <QSignalSpy>
#include "auth/CurrentUser.h"

using namespace qwhmi::auth;

class TestCurrentUser : public QObject {
    Q_OBJECT

private slots:
    void init();      // 每个用例前 logout 复位
    void cleanup();   // 每个用例后 logout 复位

    void singleton_identity();
    void initialState_notLoggedIn();
    void login_setsFields();
    void login_emitsSignal();
    void logout_clearsFields();
    void logout_emitsSignal();
    void logout_idempotent();
};

void TestCurrentUser::init() {
    CurrentUser::instance().logout();
}

void TestCurrentUser::cleanup() {
    CurrentUser::instance().logout();
}

void TestCurrentUser::singleton_identity() {
    auto& a = CurrentUser::instance();
    auto& b = CurrentUser::instance();
    QVERIFY(&a == &b);
}

void TestCurrentUser::initialState_notLoggedIn() {
    auto& u = CurrentUser::instance();
    QVERIFY(!u.isLoggedIn());
    QCOMPARE(u.name(), QString());
    QCOMPARE(u.role(), Role::Viewer);
}

void TestCurrentUser::login_setsFields() {
    User u;
    u.name = "alice";
    u.role = Role::Engineer;
    CurrentUser::instance().login(u);
    QVERIFY(CurrentUser::instance().isLoggedIn());
    QCOMPARE(CurrentUser::instance().name(), QStringLiteral("alice"));
    QCOMPARE(CurrentUser::instance().role(), Role::Engineer);
}

void TestCurrentUser::login_emitsSignal() {
    User u; u.name = "alice"; u.role = Role::Engineer;
    QSignalSpy spy(&CurrentUser::instance(), &CurrentUser::userLoggedIn);
    CurrentUser::instance().login(u);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("alice"));
}

void TestCurrentUser::logout_clearsFields() {
    User u; u.name = "alice"; u.role = Role::Admin;
    CurrentUser::instance().login(u);
    CurrentUser::instance().logout();
    QVERIFY(!CurrentUser::instance().isLoggedIn());
    QCOMPARE(CurrentUser::instance().name(), QString());
    QCOMPARE(CurrentUser::instance().role(), Role::Viewer);
}

void TestCurrentUser::logout_emitsSignal() {
    User u; u.name = "alice"; u.role = Role::Admin;
    CurrentUser::instance().login(u);
    QSignalSpy spy(&CurrentUser::instance(), &CurrentUser::userLoggedOut);
    CurrentUser::instance().logout();
    QCOMPARE(spy.count(), 1);
}

void TestCurrentUser::logout_idempotent() {
    // 未登录状态下 logout 不发信号、不崩
    CurrentUser::instance().logout();   // 先确保未登录
    QSignalSpy spy(&CurrentUser::instance(), &CurrentUser::userLoggedOut);
    CurrentUser::instance().logout();   // 重复 logout
    QCOMPARE(spy.count(), 0);
}

QTEST_MAIN(TestCurrentUser)
#include "tst_CurrentUser.moc"