// ============================================================================
// 阶段 B · B4 · UI 权限绑定单元测试
//
// 验证:
//   1. Viewer 登录 → 所有绑定的按钮 / Action 都灰
//   2. Admin 登录 → 所有绑定的按钮 / Action 都亮
//   3. Operator 登录 → WriteVariable 绑的亮,EditProject 绑的灰
//   4. 切换用户 → 已注册的按钮自动重算(CurrentUser signal 钩生效)
//   5. 重复注册同一控件 → 覆盖(以最后一次为准)
//   6. nullptr 控件 → no-op,不崩
//
// 关联:UIPermission.h, CurrentUser.h, Permission.h
// ============================================================================

#include <QtTest>
#include <QSignalSpy>
#include <QPushButton>
#include <QAction>
#include "auth/UIPermission.h"
#include "auth/CurrentUser.h"
#include "auth/User.h"

using namespace qwhmi::auth;

class TestUIPermission : public QObject {
    Q_OBJECT
private slots:
    void init();      // 每个用例前 logout 复位 + 清空注册表(进程内静态)
    void cleanup();   // 每个用例后 logout 复位

    // 基本角色 × 权限矩阵
    void viewer_disablesAllButtons();
    void admin_enablesAllButtons();
    void operator_writeYesEditNo();

    // QAction 版(托盘菜单)
    void bindActionEnable_adminEnables();

    // 自动重算(CurrentUser 信号钩)
    void switchingUser_recomputesButtons();
    void logout_disablesAllButtons();

    // 边界
    void nullptrButton_isNoOp();
    void nullptrAction_isNoOp();
    void reRegister_overwritesPreviousBinding();

    // 信号触发链路(调试用)
    void applyAllBindings_triggersOnUserLoggedIn();
};

namespace {
// 构造测试用 User(只填 name + role,其他字段空 — authenticate 不用,UIPermission 也不用)
User makeUser(const QString& name, Role role) {
    User u;
    u.name = name;
    u.role = role;
    return u;
}
} // namespace

void TestUIPermission::init() {
    CurrentUser::instance().logout();
}

void TestUIPermission::cleanup() {
    CurrentUser::instance().logout();
}

void TestUIPermission::viewer_disablesAllButtons() {
    CurrentUser::instance().login(makeUser("viewer1", Role::Viewer));

    QPushButton writeBtn;
    QPushButton editBtn;
    QPushButton userBtn;
    bindButtonEnable(&writeBtn, Permission::WriteVariable);
    bindButtonEnable(&editBtn,  Permission::EditProject);
    bindButtonEnable(&userBtn,  Permission::ManageUsers);

    QVERIFY(!writeBtn.isEnabled());
    QVERIFY(!editBtn.isEnabled());
    QVERIFY(!userBtn.isEnabled());
}

void TestUIPermission::admin_enablesAllButtons() {
    CurrentUser::instance().login(makeUser("admin1", Role::Admin));

    QPushButton writeBtn;
    QPushButton editBtn;
    QPushButton userBtn;
    bindButtonEnable(&writeBtn, Permission::WriteVariable);
    bindButtonEnable(&editBtn,  Permission::EditProject);
    bindButtonEnable(&userBtn,  Permission::ManageUsers);

    QVERIFY(writeBtn.isEnabled());
    QVERIFY(editBtn.isEnabled());
    QVERIFY(userBtn.isEnabled());
}

void TestUIPermission::operator_writeYesEditNo() {
    CurrentUser::instance().login(makeUser("op1", Role::Operator));

    QPushButton writeBtn;
    QPushButton editBtn;
    QPushButton userBtn;
    bindButtonEnable(&writeBtn, Permission::WriteVariable);
    bindButtonEnable(&editBtn,  Permission::EditProject);
    bindButtonEnable(&userBtn,  Permission::ManageUsers);

    QVERIFY(writeBtn.isEnabled());    // Operator 有 WriteVariable
    QVERIFY(!editBtn.isEnabled());    // Operator 无 EditProject
    QVERIFY(!userBtn.isEnabled());    // Operator 无 ManageUsers
}

void TestUIPermission::bindActionEnable_adminEnables() {
    CurrentUser::instance().login(makeUser("admin1", Role::Admin));

    QAction userMgrAct("用户管理");
    bindActionEnable(&userMgrAct, Permission::ManageUsers);
    QVERIFY(userMgrAct.isEnabled());

    // 切到 Engineer(无 ManageUsers)→ action 应自动变灰
    CurrentUser::instance().login(makeUser("eng1", Role::Engineer));
    QVERIFY(!userMgrAct.isEnabled());
}

void TestUIPermission::switchingUser_recomputesButtons() {
    QPushButton btn;
    bindButtonEnable(&btn, Permission::WriteVariable);

    CurrentUser::instance().login(makeUser("admin1", Role::Admin));
    QVERIFY(btn.isEnabled());

    // 切换到 Viewer(无 WriteVariable)→ signal hook 应触发 → 按钮变灰
    CurrentUser::instance().login(makeUser("viewer1", Role::Viewer));
    QVERIFY(!btn.isEnabled());

    // 再切回 Admin → 应该再亮
    CurrentUser::instance().login(makeUser("admin1", Role::Admin));
    QVERIFY(btn.isEnabled());
}

void TestUIPermission::logout_disablesAllButtons() {
    CurrentUser::instance().login(makeUser("admin1", Role::Admin));

    QPushButton btn;
    bindButtonEnable(&btn, Permission::WriteVariable);
    QVERIFY(btn.isEnabled());

    // 注销 → role 退回 Viewer(默认安全值)→ signal hook 触发 → 按钮变灰
    CurrentUser::instance().logout();
    QVERIFY(!btn.isEnabled());
}

void TestUIPermission::nullptrButton_isNoOp() {
    bindButtonEnable(nullptr, Permission::WriteVariable);
    // 没崩 = 通过
    QVERIFY(true);
}

void TestUIPermission::nullptrAction_isNoOp() {
    bindActionEnable(nullptr, Permission::ManageUsers);
    QVERIFY(true);
}

void TestUIPermission::reRegister_overwritesPreviousBinding() {
    QPushButton btn;
    CurrentUser::instance().login(makeUser("admin1", Role::Admin));

    // 第 1 次:绑 WriteVariable(Admin 有,亮)
    bindButtonEnable(&btn, Permission::WriteVariable);
    QVERIFY(btn.isEnabled());

    // 第 2 次:绑 ManageUsers(Admin 有,还是亮)— 仅验证不崩,覆盖逻辑难直接验证
    bindButtonEnable(&btn, Permission::ManageUsers);
    QVERIFY(btn.isEnabled());

    // 切到 Operator(无 ManageUsers)→ 应变灰(说明第 2 次生效,不是第 1 次)
    CurrentUser::instance().login(makeUser("op1", Role::Operator));
    QVERIFY(!btn.isEnabled());  // ManageUsers 没权限 → 灰(说明覆盖成功)
}

void TestUIPermission::applyAllBindings_triggersOnUserLoggedIn() {
    QPushButton btn;
    bindButtonEnable(&btn, Permission::ManageUsers);
    CurrentUser::instance().login(makeUser("viewer1", Role::Viewer));
    QVERIFY(!btn.isEnabled());

    // spy 信号 → 触发 applyAllBindings → 按钮变亮
    QSignalSpy spy(&CurrentUser::instance(), &CurrentUser::userLoggedIn);
    CurrentUser::instance().login(makeUser("admin1", Role::Admin));
    QCOMPARE(spy.count(), 1);
    QVERIFY(btn.isEnabled());  // signal hook 触发后已自动重算
}

QTEST_MAIN(TestUIPermission)
#include "tst_UIPermission.moc"
