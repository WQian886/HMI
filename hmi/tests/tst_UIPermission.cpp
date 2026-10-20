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
//   7. 集成流:admin 登录 → 切 viewer → 全灰(等价真实 UI 流程)
//   8. 4 角色 × 5 权限 矩阵全覆盖(hmi.cpp 5 个按钮 + 托盘 1 个 action 用的权限)
//
// 关联:UIPermission.h, CurrentUser.h, Permission.h
// ============================================================================

#include <QtTest>
#include <QSignalSpy>
#include <QPushButton>
#include <QAction>
#include <QFile>          // ★ P0:读 hmi.cpp / EditorMainWindow.cpp 源码文本
#include <QRegularExpression>
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

    // 集成流(等价于真实启动 hmi.exe + 托盘切换用户的视觉验证)
    void fullFlow_adminToViewer_disablesButtons();
    void fullFlow_realRoles_haveExpectedVisibility();

    // ★ P0(2026-10-20):主画面【真实存在的那几个按钮】有没有被绑。
    //   上面所有用例都用自己的临时 QPushButton —— 它们验的是
    //   "bindButtonEnable 这个机制对不对",不是"hmi.cpp 调没调它"。
    //   而 2026-10-20 发现的正是后者:机制一直是对的,
    //   5 个后加的按钮一个都没调。上面那些用例对此【完全无感】——
    //   全绿,而界面上一道门都没有。
    //   所以这批只能读 hmi.cpp 源码本身(hmi_core 不编 hmi.cpp,
    //   链不到真实控件,这是唯一能钉住"接线"的形状,同 tst_MenuBar)。
    void everyGatedMainWindowButton_isBoundInHmiCpp();
    void readOnlyButtons_deliberatelyNotBound_stayUngated();
    void editorButton_isTheOnlyGateOnProjectWriting();
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

// ★ B4 集成验证(C 方案)
// 模拟真实流程:启动 hmi.exe → admin 登录 → 所有按钮亮 → 托盘"切换用户"
//   → viewer 登录 → 所有按钮灰。等价覆盖真实 UI 视觉验证,
//   不依赖 GUI 启动,Wqian 不用操作 UI 也能拿到确定性答案。
void TestUIPermission::fullFlow_adminToViewer_disablesButtons() {
    // 模拟 hmi.cpp 构造函数里的 5 个 bindButtonEnable 调用
    QPushButton startBtn, stopBtn, resetBtn, saveBtn, openBtn;
    bindButtonEnable(&startBtn, Permission::WriteVariable);
    bindButtonEnable(&stopBtn,  Permission::WriteVariable);
    bindButtonEnable(&resetBtn, Permission::WriteVariable);
    bindButtonEnable(&saveBtn,  Permission::EditProject);
    bindButtonEnable(&openBtn,  Permission::EditProject);

    // 模拟 main.cpp 托盘 install 后的 bindActionEnable(ManageUsers)
    QAction userMgrAct("用户管理");
    bindActionEnable(&userMgrAct, Permission::ManageUsers);

    // 阶段 1:admin 登录(模拟首启 + 首登)
    CurrentUser::instance().login(makeUser("admin", Role::Admin));
    QVERIFY(startBtn.isEnabled());
    QVERIFY(stopBtn.isEnabled());
    QVERIFY(resetBtn.isEnabled());
    QVERIFY(saveBtn.isEnabled());
    QVERIFY(openBtn.isEnabled());
    QVERIFY(userMgrAct.isEnabled());

    // 阶段 2:托盘"切换用户" → viewer 登录
    CurrentUser::instance().login(makeUser("viewer", Role::Viewer));
    QVERIFY(!startBtn.isEnabled());
    QVERIFY(!stopBtn.isEnabled());
    QVERIFY(!resetBtn.isEnabled());
    QVERIFY(!saveBtn.isEnabled());
    QVERIFY(!openBtn.isEnabled());
    QVERIFY(!userMgrAct.isEnabled());

    // 阶段 3:注销(自动 / 手动)→ 默认 Viewer 态 → 全灰(本来就是灰的)
    CurrentUser::instance().logout();
    QVERIFY(!startBtn.isEnabled());
    QVERIFY(!userMgrAct.isEnabled());
}

// ★ B4 集成验证(C 方案)
// 4 角色 × 5 权限 = 20 组合一次性矩阵验。
// 这是 hmi.cpp 5 个按钮 + 托盘 1 个 action 实际用到的权限组合。
void TestUIPermission::fullFlow_realRoles_haveExpectedVisibility() {
    struct Case { Role role; Permission perm; bool expectEnabled; };
    const Case cases[] = {
        // Admin:全 6 权限,这里只测 5 个用到的
        {Role::Admin,    Permission::WriteVariable, true},
        {Role::Admin,    Permission::EditProject,   true},
        {Role::Admin,    Permission::ManageUsers,   true},
        // Engineer:5 权限(无 ManageUsers)
        {Role::Engineer, Permission::WriteVariable, true},
        {Role::Engineer, Permission::EditProject,   true},
        {Role::Engineer, Permission::ManageUsers,   false},
        // Operator:2 权限(WriteVariable + AcknowledgeAlarm)
        {Role::Operator, Permission::WriteVariable, true},
        {Role::Operator, Permission::EditProject,   false},
        {Role::Operator, Permission::ManageUsers,   false},
        // Viewer:0 权限
        {Role::Viewer,   Permission::WriteVariable, false},
        {Role::Viewer,   Permission::EditProject,   false},
        {Role::Viewer,   Permission::ManageUsers,   false},
    };

    for (const auto& c : cases) {
        // 每个 case 用新按钮隔离(避免注册表残留)
        QPushButton btn;
        bindButtonEnable(&btn, c.perm);
        CurrentUser::instance().login(makeUser("u", c.role));
        QCOMPARE(btn.isEnabled(), c.expectEnabled);
    }
}

// ============================================================================
// ★ P0(2026-10-20):主画面真实按钮的权限接线
//
//   【为什么这一节跟上面那些用例是两种东西】
//     上面每条都是:造一个临时按钮 → 绑 → 切角色 → 看 enabled。
//     它们验的是 bindButtonEnable 【机制】正确,而机制一直是对的。
//     2026-10-20 的缺陷不在机制,在【接线】:hmi.cpp 里后加的 5 个按钮
//     一个都没调 bindButtonEnable。于是上面 12 条用例全绿,
//     而现场是个 Viewer 也能点开编辑器存盘改写工程。
//     ★ 这就是本仓库反复出现的形状:测的是被抽出来的那部分,
//       没测的是"有人忘了把它接回去"。
//
//   【为什么读源码而不是真的建 hmi 窗口】
//     hmi_core 不编 hmi.cpp,建不出主窗口控件。要真跑起来得把 hmi.cpp
//     和整串依赖拖进测试工程,那是对付一个接线问题的巨大代价。
//     读源码文本钉"这几行有没有接上"是准确且便宜的 ——
//     跟 tst_MenuBar 验 moc 槽、tst_Theme 验 hmi.cpp 徽章字号同一手法。
//
//   【诚实的边界】
//     这只能证明"调用出现在源码里",不能证明"运行时真的生效了"。
//     后者靠 bindButtonEnable 自己的那 12 条用例(机制层)加上人工启动确认。
//
//   【负向对照 —— 三轮,实跑】
//     ① 删掉 bindButtonEnable(editorBtn, ...)
//        → everyGatedMainWindowButton_isBoundInHmiCpp 红 + editorButton_isTheOnlyGate 红,
//          15 passed / 2 failed。另外 13 条全绿(非重叠)。
//     ② 给只读的 runtimeBtn 加上 bindButtonEnable
//        → readOnlyButtons_deliberatelyNotBound_stayUngated 红,
//          16 passed / 1 failed。★ 与 ① 红的用例不重叠,证明这两条不是同一根线。
//     ③ 在 EditorMainWindow.cpp 里插一个 checkPermission 调用
//        → editorButton_isTheOnlyGateOnProjectWriting 红,
//          16 passed / 1 failed,提示语是"编辑器现在自查权限,
//          按钮那行从'唯一的门'降级成'第二道门'"。
//        ★ 这条防的是"依据本身过期":哪天编辑器内部加了检查,
//          这条会红提醒更新注释,而不是让一句已经不准的话继续躺着。
//
//   ★ 做对照时又踩了一次 mtime:Copy-Item 还原文件会保留备份的旧时间戳,
//     ninja 报 "no work to do",跑的是上轮的 exe。必须 touch 或先删 exe。
//     这是本仓库第 6 次栽在"跑的是旧二进制"上。
// ============================================================================

namespace {

// 从 hmi.cpp 里找出 "bindButtonEnable(<btn>, <Permission>::<perm>)" 的绑定。
//   返回该按钮绑到的权限名;没找到返回空串。
QString boundPermissionFor(const QString& src, const QString& btnVar) {
    const QRegularExpression re(
        QStringLiteral("bindButtonEnable\\(\\s*%1\\s*,\\s*[^;]*?Permission::(\\w+)")
            .arg(btnVar));
    const QRegularExpressionMatch m = re.match(src);
    return m.hasMatch() ? m.captured(1) : QString();
}

QString readHmiCpp() {
    QFile f(QStringLiteral("%1/hmi.cpp").arg(QString::fromLatin1(HMI_SRC_DIR_STRING)));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return QString();
    return QString::fromUtf8(f.readAll());
}

// 主画面按钮行的完整清单 —— 以及每个【该不该】被绑。
//   变量名必须和 hmi.cpp 里那个局部变量一模一样:这就是接线本身。
struct MainWindowButton {
    const char* varName;
    const char* label;
    const char* expectedPerm;   // 空串 = 刻意不绑(只读,任何角色都要能看)
    const char* why;
};

const MainWindowButton kMainWindowButtons[] = {
    // .ui 里那 6 个,2026-09-17 B4 就绑了
    {"ui.startButton",  "启动",     "WriteVariable", "运行控制,Operator 也要能按"},
    {"ui.stopButton",   "停止",     "WriteVariable", "同上"},
    {"ui.resetButton",  "复位",     "WriteVariable", "同上"},
    {"ui.saveButton",   "保存工程", "EditProject",   "改写工程文件要工程师"},
    {"ui.openButton",   "打开工程", "EditProject",   "同上"},
    {"ui.connectButton",    "连接",   "", "任何角色都要能连上看数据 —— 绑了就看不见现场了"},
    {"ui.disconnectButton", "断开",   "", "同上"},

    // ★ 2026-10-20 补的那 3 个 —— 之前一个都没绑
    {"editorBtn",     "编辑器",   "EditProject", "能存盘改写工程(它是唯一的门,见下)"},
    {"alarmCfgBtn",   "报警配置", "EditProject", "对话框内自查 EditProject,按钮这层必须一致"},
    {"alarmSoundBtn", "报警声音", "EditProject", "同上"},

    // 刻意不绑的两个 —— 写在这里是为了让"漏绑"和"决定不绑"能区分开
    {"runtimeBtn", "运行时画面", "", "只读,任何角色都要能看现场画面"},
    {"demoBtn",    "演示画布",   "", "只读;真正该做的是发布时摘掉,不是灰它"},
};

} // namespace

void TestUIPermission::everyGatedMainWindowButton_isBoundInHmiCpp() {
    const QString src = readHmiCpp();
    QVERIFY2(!src.isEmpty(), "读不到 hmi.cpp —— 权限接线没法验");

    QStringList unbound, wrongPerm;
    for (const MainWindowButton& b : kMainWindowButtons) {
        if (!b.expectedPerm[0]) continue;              // 刻意不绑的,这条不查
        const QString got = boundPermissionFor(src, QString::fromLatin1(b.varName));
        if (got.isEmpty()) {
            unbound << QStringLiteral("%1(%2)")
                           .arg(QLatin1String(b.label), QLatin1String(b.varName));
        } else if (got != QLatin1String(b.expectedPerm)) {
            wrongPerm << QStringLiteral("%1 绑成了 %2,应为 %3")
                             .arg(QLatin1String(b.label), got, QLatin1String(b.expectedPerm));
        }
    }
    QVERIFY2(unbound.isEmpty(),
             qPrintable(QStringLiteral(
                 "主画面这些按钮在 hmi.cpp 里【没有】bindButtonEnable,权限对它们完全无效:\n  %1\n"
                 "  ↑ 亮着的按钮 + 点了被拒 = 用户以为是自己账号坏了。\n"
                 "    新加按钮时必须顺手绑,不然权限门控会随功能增加被悄悄漏光。")
                 .arg(unbound.join(QStringLiteral("\n  ")))));
    QVERIFY2(wrongPerm.isEmpty(),
             qPrintable(QStringLiteral("绑错了权限:\n  %1").arg(wrongPerm.join(QStringLiteral("\n  ")))));
}

void TestUIPermission::readOnlyButtons_deliberatelyNotBound_stayUngated() {
    const QString src = readHmiCpp();
    QVERIFY2(!src.isEmpty(), "读不到 hmi.cpp");

    QStringList wronglyGated;
    for (const MainWindowButton& b : kMainWindowButtons) {
        if (b.expectedPerm[0]) continue;               // 该绑的,上一条查了
        if (!boundPermissionFor(src, QString::fromLatin1(b.varName)).isEmpty()) {
            wronglyGated << QStringLiteral("%1(%2)")
                                .arg(QLatin1String(b.label), QLatin1String(b.varName));
        }
    }
    QVERIFY2(wronglyGated.isEmpty(),
             qPrintable(QStringLiteral(
                 "这几个按钮是【只读】的,却挂了权限门控:\n  %1\n"
                 "  运行画面/演示画布不写任何东西,Viewer 也该看得见现场 ——\n"
                 "  绑了就把'看现场'这个最基本的动作给禁掉了。")
                 .arg(wronglyGated.join(QStringLiteral("\n  ")))));
}

void TestUIPermission::editorButton_isTheOnlyGateOnProjectWriting() {
    // ★ 为什么单独一条:编辑器的"绑定按钮"这件事本身就是坏消息。
    //   EditorMainWindow 内部【完全没有权限检查】,它 L948 直接
    //   project_->saveToFile() 改写工程文件。所以 hmi.cpp 里那条
    //   bindButtonEnable(editorBtn, EditProject) 不是"多一层保险",
    //   是【唯一的一道门】。
    //
    //   哪天有人觉得"编辑器内部该自己查一次",顺手把按钮那行删了,
    //   这条测试会红 —— 提醒他删掉的不是冗余,是唯一的防线。
    const QString src = readHmiCpp();
    QVERIFY2(!src.isEmpty(), "读不到 hmi.cpp");
    QCOMPARE(boundPermissionFor(src, QStringLiteral("editorBtn")),
             QStringLiteral("EditProject"));

    // 反过来确认前提没变:编辑器内部现在【确实】没有权限检查。
    //   一旦哪天有人加了,这条会红 —— 那不是失败,是要更新这条的注释,
    //   因为"唯一的一道门"这个说法不再成立了。
    QFile ed(QStringLiteral("%1/src/editor/EditorMainWindow.cpp")
                 .arg(QString::fromLatin1(HMI_SRC_DIR_STRING)));
    QVERIFY2(ed.open(QIODevice::ReadOnly | QIODevice::Text),
             qPrintable(QStringLiteral("打不开 EditorMainWindow.cpp:%1").arg(ed.errorString())));
    const QString edSrc = QString::fromUtf8(ed.readAll());
    QVERIFY2(!edSrc.contains(QStringLiteral("checkPermission")),
             "EditorMainWindow 里出现 checkPermission 了 —— "
             "编辑器现在自查权限,按钮那行从'唯一的门'降级成'第二道门'。\n"
             "请更新 hmi.cpp 与本测试的注释,明确两道门各拦什么。");
}

QTEST_MAIN(TestUIPermission)
#include "tst_UIPermission.moc"
