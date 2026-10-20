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
//   8. 4 角色 × 5 权限 矩阵全覆盖(hmi.cpp 剩下的按钮 + 托盘 1 个 action 用的权限)
//   9. ★ P1-b:被裁掉的按钮不许悄悄回来(读 hmi.cpp 的【代码】,注释已剥)
//
//   【★ P1-b 之后的分工】主画面的按钮只剩 5 个(启动/停止/复位 + 连接/断开),
//   其余入口都在菜单栏。菜单项 ↔ 权限的对应由 tst_MenuBar 守;
//   本文件守的是【还留在主画面上的那 5 个】,以及"被裁掉的别回来"。
//   两边合起来才是完整不变式。
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

    // ★★ P1-b(2026-10-20):被裁掉的按钮不许悄悄回来。
    //   原来这里有一条 editorButton_isTheOnlyGateOnProjectWriting,
    //   钉的是"主画面的 editorBtn 是改工程的唯一一道门"。
    //   P1-b 把 8 个按钮撤进菜单栏之后,那句话就不成立了 ——
    //   门从按钮搬到了 画面→编辑器 菜单项,那条门由 tst_MenuBar 的
    //   editorItem_isTheOnlyGateOnProjectWriting 接着钉。
    //   这里改成钉"别回来":P0 的教训是"功能会一个个加,权限会一个个漏",
    //   而"把按钮放回主画面更顺手"是最可能发生的下一次加法。
    void croppedButtons_stayCropped();
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
// ★ P0(2026-10-20)→ P1-b(2026-10-20):主画面真实按钮的权限接线
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
//   【P1-b 之后这里为什么变短了】
//     主画面的按钮从 11 个减到 5 个:撤掉的 6 个(保存/打开/编辑器/
//     报警配置/报警声音/运行时画面/演示画布)功能一个没丢,全搬进了菜单栏。
//     于是本文件要守的接线只剩 5 个;被撤掉的那 6 个入口由 tst_MenuBar
//     的 menuItems_areGatedByRole 守(菜单项 ↔ 权限的对应)。
//     两个文件各守一半,合起来才是不变式 ——
//     只有本文件时"按钮"无人管,只有 tst_MenuBar 时"新增的按钮"无人管。
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
//   【负向对照 —— P0 三轮 + P1-b 一轮,全部实跑,数字如下】
//
//   【P0 三轮(裁按钮之前,历史记录)】
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
//
//   【P1-b 一轮(2026-10-20)】在 hmi.cpp 里把编辑器按钮加回主画面 ——
//     而且是【最可能发生的那个形状】:不带 bindButtonEnable 就 new 出来。
//       → 红 1:croppedButtons_stayCropped(点名 editorBtn)
//         绿 16 —— ★ tst_MenuBar 那 15 条【全绿】:它不读 hmi.cpp 的按钮。
//           两条各守一侧(菜单项的门 / 主画面的门),互不重叠才合起来完整。
//     这一轮顺带证明了一件必须证明的事:那 16 条老用例对"按钮又回来了"
//     【完全无感】—— 它们查的是 bindButtonEnable 机制,而这次是"根本没调它"。
//     这正是本节开头写的那个形状,只不过这次发生在裁剪之后。
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

// 去掉注释再比对。
//   ★ 这一步不是讲究,是必需的:hmi.cpp 里有一整段注释【故意】列着
//     editorBtn / alarmCfgBtn / runtimeBtn 这些名字(说明它们为什么被撤掉),
//     不剥就会把"注释提到了"当成"代码还在",那条测试永远绿 ——
//     而它守的恰恰是"别回来"。
QString codeOnly(const QString& text) {
    QString out;
    out.reserve(text.size());
    bool inBlock = false;
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString& line : lines) {
        QString keep;
        int i = 0;
        while (i < line.size()) {
            if (inBlock) {
                const int end = line.indexOf(QLatin1String("*/"), i);
                if (end < 0) { i = line.size(); break; }
                inBlock = false;
                i = end + 2;
                continue;
            }
            const int b = line.indexOf(QLatin1String("/*"), i);
            const int s = line.indexOf(QLatin1String("//"), i);
            if (s >= 0 && (b < 0 || s < b)) { keep += line.mid(i, s - i); break; }
            if (b >= 0) { keep += line.mid(i, b - i); inBlock = true; i = b + 2; continue; }
            keep += line.mid(i);
            break;
        }
        out += keep;
        out += QLatin1Char('\n');
    }
    return out;
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
    // .ui 里那 3 个运行控制,2026-09-17 B4 就绑了
    {"ui.startButton",  "启动",     "WriteVariable", "运行控制,Operator 也要能按"},
    {"ui.stopButton",   "停止",     "WriteVariable", "同上"},
    {"ui.resetButton",  "复位",     "WriteVariable", "同上"},

    // 刻意不绑的两个 —— 写在这里是为了让"漏绑"和"决定不绑"能区分开
    {"ui.connectButton",    "连接",   "", "任何角色都要能连上看数据 —— 绑了就看不见现场了"},
    {"ui.disconnectButton", "断开",   "", "同上"},

    // ★★ P1-b(2026-10-20)之后清单为什么只剩这 5 个:
    //   .ui 里的 saveButton / openButton 连同代码后加的 6 个按钮
    //   (editorBtn / alarmCfgBtn / alarmSoundBtn / runtimeBtn / demoBtn)
    //   全部撤掉了 —— 功能没丢,搬进了菜单栏(见 MenuBuilder.cpp 的权限表)。
    //   那些入口的门禁由 tst_MenuBar 的 menuItems_areGatedByRole 负责,
    //   本文件只管【还留在主画面上的】这 5 个。
    //   被撤掉的那批由 croppedButtons_stayCropped 钉住"别回来"。
};

// ★ P1-b 撤掉的标识符 —— 代码里(剥掉注释之后)不许再出现。
//   分两类,含义完全不同:
//     ui.XxxButton —— .ui 里删掉的控件。uic 会生成 ui_*.h,
//                    一旦 .ui 里还留着而代码里又用了,会静默变成"控件在但没人管"。
//     xxxBtn      —— hmi.cpp 里 new 出来的那批局部变量。
const char* const kCroppedIdentifiers[] = {
    "ui.saveButton", "ui.openButton",
    "editorBtn", "alarmCfgBtn", "alarmSoundBtn", "runtimeBtn", "demoBtn",
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

// ★★ P1-b(2026-10-20):撤掉的按钮不许悄悄回来。
//
//   【这条为什么必须存在,而不能"删了旧的就算了"】
//     P0 的教训是"每加一个功能,就多一个可能漏绑权限的地方"。
//     而"把编辑器按钮放回主画面,因为放那儿更顺手"是最可能发生的下一次加法 ——
//     尤其在按钮裁掉之后,新人接手时看到主画面空了一块,第一反应就是补回来。
//     一旦补回来而没顺手绑权限,界面就变成:
//       主画面一个【亮着】的"编辑器"按钮 + 菜单里一个【灰着的】菜单项。
//     界面上写着"你可以点开编辑器",而权限体系说"你不行"。
//     这比从来没装过门更糟 —— 它让人以为门在,于是放心地把工程交给对方。
//
//   【为什么剥掉注释再查】
//     hmi.cpp 里有一整段注释【故意】列着这些名字,说明它们为什么被撤。
//     不剥注释的话这条永远绿 —— 守着一个"其实什么也没查"的测试,
//     比没有这条更坏:它让人以为有人看着。
void TestUIPermission::croppedButtons_stayCropped() {
    const QString code = codeOnly(readHmiCpp());
    QVERIFY2(!code.trimmed().isEmpty(),
             "读不到 hmi.cpp(或剥完注释后是空的) —— 裁剪检查没法验");

    QStringList back;
    for (const char* id : kCroppedIdentifiers) {
        if (code.contains(QLatin1String(id))) back << QLatin1String(id);
    }
    QVERIFY2(back.isEmpty(),
             qPrintable(QStringLiteral(
                 "hmi.cpp 的【代码】里又出现了这些已被 P1-b 撤掉的按钮标识符:\n  %1\n"
                 "  功能没丢,它们都在菜单栏里(见 MenuBuilder.cpp)。\n"
                 "  真要放回主画面,请一并补 bindButtonEnable —— 否则会出现\n"
                 "  『主画面一个亮着的按钮 + 菜单里一个灰着的菜单项』,\n"
                 "  界面在说有门,而门在另一个入口那儿。")
                 .arg(back.join(QStringLiteral("\n  ")))));

    // 反过来:还留在主画面上的那 3 个运行控制,必须【还在】。
    //   上一条查"被撤的别回来",这条查"该留的没被误撤" ——
    //   两者失败方向相反,只做一条的话另一侧可以坏着而全绿。
    for (const MainWindowButton& b : kMainWindowButtons) {
        if (!b.expectedPerm[0]) continue;
        QVERIFY2(code.contains(QLatin1String(b.varName)),
                 qPrintable(QStringLiteral(
                     "主画面的「%1」(%2)不见了 —— P1-b 不该撤它。"
                     "撤错了就是功能直接消失,而界面上一句话都不会说。")
                     .arg(QLatin1String(b.label), QLatin1String(b.varName))));
    }
}

QTEST_MAIN(TestUIPermission)
#include "tst_UIPermission.moc"
