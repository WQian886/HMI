// ============================================================================
// 阶段 B · B3 · Permission 单元测试
//
// 验证:
//   1. 4 个角色的权限映射表正确(Admin 全 / Engineer 除 ManageUsers / Operator 2 个 / Viewer 0)
//   2. checkPermission 单点检查正确
//   3. 字符串互转 toString/fromString 往返
//
// 关联:Permission.h
// ============================================================================

#include <QtTest>
#include "auth/Permission.h"

using namespace qwhmi::auth;

class TestPermission : public QObject {
    Q_OBJECT
private slots:
    void admin_hasAllPermissions();
    void engineer_hasAllExceptManageUsers();
    void operator_hasWriteAndAck();
    void viewer_hasNoPermissions();
    void checkPermission_basic();
    void stringRoundTrip();
    void unknownStringFallsBackToSafeDefault();
};

void TestPermission::admin_hasAllPermissions() {
    const auto perms = rolePermissions(Role::Admin);
    QCOMPARE(perms.size(), 6);
    QVERIFY(perms.contains(Permission::WriteVariable));
    QVERIFY(perms.contains(Permission::AcknowledgeAlarm));
    QVERIFY(perms.contains(Permission::EditRecipe));
    QVERIFY(perms.contains(Permission::ExportReport));
    QVERIFY(perms.contains(Permission::ManageUsers));
    QVERIFY(perms.contains(Permission::EditProject));
}

void TestPermission::engineer_hasAllExceptManageUsers() {
    const auto perms = rolePermissions(Role::Engineer);
    QCOMPARE(perms.size(), 5);
    QVERIFY(perms.contains(Permission::WriteVariable));
    QVERIFY(perms.contains(Permission::AcknowledgeAlarm));
    QVERIFY(!perms.contains(Permission::ManageUsers));
}

void TestPermission::operator_hasWriteAndAck() {
    const auto perms = rolePermissions(Role::Operator);
    QCOMPARE(perms.size(), 2);
    QVERIFY(perms.contains(Permission::WriteVariable));
    QVERIFY(perms.contains(Permission::AcknowledgeAlarm));
    QVERIFY(!perms.contains(Permission::EditRecipe));
    QVERIFY(!perms.contains(Permission::ManageUsers));
}

void TestPermission::viewer_hasNoPermissions() {
    const auto perms = rolePermissions(Role::Viewer);
    QCOMPARE(perms.size(), 0);
    QVERIFY(!checkPermission(Role::Viewer, Permission::WriteVariable));
    QVERIFY(!checkPermission(Role::Viewer, Permission::AcknowledgeAlarm));
}

void TestPermission::checkPermission_basic() {
    QVERIFY(checkPermission(Role::Admin,    Permission::WriteVariable));
    QVERIFY(checkPermission(Role::Engineer, Permission::WriteVariable));
    QVERIFY(checkPermission(Role::Operator, Permission::WriteVariable));
    QVERIFY(!checkPermission(Role::Viewer,  Permission::WriteVariable));

    QVERIFY(checkPermission(Role::Admin,    Permission::ManageUsers));
    QVERIFY(!checkPermission(Role::Engineer, Permission::ManageUsers));
    QVERIFY(!checkPermission(Role::Operator, Permission::ManageUsers));
    QVERIFY(!checkPermission(Role::Viewer,  Permission::ManageUsers));
}

void TestPermission::stringRoundTrip() {
    for (Permission p : { Permission::WriteVariable, Permission::AcknowledgeAlarm,
                          Permission::EditRecipe, Permission::ExportReport,
                          Permission::ManageUsers, Permission::EditProject }) {
        QCOMPARE(permissionFromString(permissionToString(p)), p);
    }
}

void TestPermission::unknownStringFallsBackToSafeDefault() {
    // 不认识 → 默认 WriteVariable(最低权限,不暴露 EditProject 等)
    QCOMPARE(permissionFromString("unknown"), Permission::WriteVariable);
}

QTEST_MAIN(TestPermission)
#include "tst_Permission.moc"