// ============================================================================
// tst_MenuBar —— 菜单栏:每个菜单项必须连到【它自己那个名字】的槽
//
// 【这个测试在防什么具体事故】
//   菜单项写错了不会崩、不会报错,也不会打日志 —— QObject::connect 拼错槽名
//   只是返回 false,菜单项照样出现在菜单栏里,点下去【什么都不会发生】。
//   最恶劣的一种是【连错而非连不上】:
//     点"打开工程"弹出"保存工程"的对话框,用户照着填,
//     当前配置就被覆盖到一个他不相关的文件上,而且全程看不出任何异常。
//
// 【为什么这份测试不是"把生产代码抄一遍"】
//   菜单逻辑本来写在 hmi.cpp 里,而 hmi.cpp 不在 hmi_core 里 —— 测试根本链不到,
//   唯一的测法就是把代码【复制一份】进测试文件。那样测的是副本:
//   生产那份被改错了(少加一项、连错槽),测试照样全绿。
//   现在菜单搬到了 src/ui/MenuBuilder.cpp,hmi.cpp 和本测试调的是同一个函数。
//
// 【本测试能覆盖的边界 —— 说清楚免得误以为它包打天下】
//   ✓ 菜单结构、每一项触发后落到哪个槽、快捷键、重复搭建不叠加、
//     "接不上"这件事能不能被【报告出来】、
//     以及(2026-10-20 起)菜单槽在 hmi.h 里有没有落在 moc 认的 slots: 段
//   ✗ 槽本身的行为(那是各功能自己的测试管的)
//   ✗ 槽的实现体是不是空的 —— "接上了但没做事"是另一类问题,这里管不着。
//   ★ 关于"hmi 那个类里到底有没有这些槽":
//     链接这一层仍然链不到(测试编不出 ui_hmi.h)。
//     但【槽名是否落在 moc 会收录的段里】是能静态查的 ——
//       hmiHeader_menuSlots_areDeclaredInsideASlotsSection 就是干这个的,
//       2026-10-20 那次事故正是它原先覆盖不到的形状。
//     运行时这一层仍由生产代码兜底:
//       buildApplicationMenu 把没连上的项【返回出来】,
//       hmi::buildMenus() 每次启动逐条 qCWarning。
//       本文件的 missingSlot_isReportedNotSwallowed 验证的正是那条通道。
//
// 【负向对照 —— 实测两轮,把 bug 原样装回去跑】
//   ① 把"打开工程"的槽从 onOpenClicked 换成 onSaveClicked(连错,不是连不上)
//      → 红 2:everyAction_reachesItsOwnSlot(直接指名"打开工程"落到了 onSaveClicked)
//              buildTwice_doesNotDuplicateMenus(那一项又触发了一次)
//        绿 6 —— 其中 actionCount 绿是对的:项数一个字没多一个字没少。
//   ② 注释掉 bar->clear()(重复搭建会叠出两层菜单)
//      → 红 1:buildTwice_doesNotDuplicateMenus(实际 12 个顶层菜单)
//        绿 7
// ★ 两轮打的是两个不同的方向:① 是"接错人",② 是"接多了"。
//   只做一轮的话,另一侧照样可以坏着而测试全绿 ——
//   而"点打开工程弹出保存框"正是这个菜单最恶劣的一类 bug:
//     用户照着填,当前配置被覆盖到不相关的文件上,而全程看不出任何异常。
//
// 【本文件自己踩过的坑(记下来是因为它比测试本身更值钱)】
//   第一版把 FakeReceiver 的槽写成普通 public 成员函数,不在 slots: 里。
//   moc 不给普通成员函数生成元方法 → 15 个 connect 全部静默失败,
//   Qt 只在 stderr 打了 15 行 "No such slot" 就放过,程序照跑、菜单照建、
//   用户照着点、什么都不会发生。
//   这正是字符串槽名最真实的样子,现在被 slots: 的强制要求挡在编译期之后、
//   测试之前。FakeReceiver 的注释里留着这段。
//
// 【2026-10-20:同一个坑在生产代码里又踩了一次 —— 这次 10 条测试没抓住】
//   用户报"菜单栏的这些按钮按了没效果"。查下来 17 个菜单项里 10 个是哑的,
//   根因和上面一模一样但发生在真的 hmi 类上:那 10 个槽声明在普通 private: 段,
//   moc 不收录 → MenuBuilder 的字符串 connect 静默返回 false。
//   而本文件当时 10 条【全绿】—— 因为它们全跑在 FakeReceiver 上,
//   而 FakeReceiver 的槽是本文件自己声明在 public slots: 里的,它必然是对的。
//     · 按钮没事 —— hmi.cpp 用成员函数指针 connect,编译期解析,不过 moc
//     · 菜单是哑的 —— MenuBuilder 用字符串 connect,查 moc 元方法表
//   同一个槽、两种 connect 写法,只有一种能工作。这就是"按钮好好的、菜单却是哑的"。
//   → 新增 hmiHeader_menuSlots_areDeclaredInsideASlotsSection(),见下方。
//
// 【负向对照 —— 2026-10-20 新增那条,实跑两轮】
//   ① 把 onExportVariablesClicked 挪回普通 private: 段(= 把 bug 原样装回去)
//      → 红 1:hmiHeader_menuSlots_areDeclaredInsideASlotsSection
//              (指名 onExportVariablesClicked / 第 232 行 / "private:" 段)
//        绿 10 —— 其中 everyAction_reachesItsOwnSlot 绿得【理直气壮】:
//                  它跑在 FakeReceiver 上,压根看不见 hmi.h 的改动。
//   ② 把 MenuBuilder.cpp 里的槽名改成 onExportVariablesRenamedByMistake(拼错)
//      → 红 3:everyAction_reachesItsOwnSlot(触发后 fired 为空)
//              healthyReceiver_reportsNoFailures(报告了一条失败)
//              hmiHeader_menuSlots_areDeclaredInsideASlotsSection
//              (指名该名字在 hmi.h 里【根本没有这个声明】)
//        绿 8
// ★ 两轮打的是两个方向:① 是"槽写对了位置但位置不对",② 是"槽名压根对不上"。
//   只做一轮的话,另一侧能坏着而新测试照样绿 —— 而 ② 恰好是老测试的地盘,
//   ① 才是这次事故真正的形状。
//
// 【负向对照 —— 2026-10-20 P1-a 新增那三条,实跑两轮】
//   ① 把"画面 → 编辑器"的权限从 EditProject 改成 std::nullopt
//      (= P1-a 没做时菜单的样子:菜单里没有门)
//      → 红 2:menuItems_areGatedByRole(点名"编辑器"这一项)
//              editorItem_isTheOnlyGateOnProjectWriting
//        绿 13 —— ★ 其中 everyMenuItem_declaresAPermission 绿得【理直气壮】:
//          它只查"表态了没有",而 std::nullopt【就是一种表态】。
//          这正是把"表态"和"表态对不对"拆成两条的理由。
//   ② 有一项绕过 addItem,直接写 mScreen->addAction(...) + 手写 connect
//      (= addItem 那个"perm 无默认值"的编译期强制,唯一的缺口)
//      → 红 2:everyMenuItem_declaresAPermission(指名"菜单项只能从 addItem 出")
//              missingSlot_isReportedNotSwallowed
//                (★ 意料之外的收获:手写的 connect 失败【不进 failures 列表】,
//                 于是 16 变成 15 —— 少了一个本该被喊出来的静默失败)
//        绿 13
// ★ 两轮打的是两个方向:① 是"表态了但表错了",② 是"根本没过表态这一关"。
//   只做一轮的话,另一侧能坏着而三条测试照样绿 ——
//   而 ② 恰好是 addItem 那个设计的全部意义所在(不给绕过它的机会)。
//
// 关联:hmi.cpp buildMenus() / src/ui/MenuBuilder.cpp / hmi.h(访问说明符段)
// ============================================================================

#include <QtTest>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QKeySequence>
#include <QRegularExpression>   // 菜单文字里带 & 助记符,比对前要剥掉
#include <QFile>                // ★ 2026-10-20:现读 MenuBuilder.cpp / hmi.h 源码
#include <QStringList>
#include <QVector>

#include "ui/MenuBuilder.h"
#include "auth/CurrentUser.h"
#include "auth/Permission.h"
#include "auth/User.h"

// ---------------------------------------------------------------------------
// 假接收者:和 hmi 一样有一组同名槽,每个槽把自己的名字记下来。
//
// ★★★ 下面这些方法【必须】写在 public slots: 里,不能只放 public 里。
//   这一点是本测试第一次运行时真的踩到的 —— 把它们放成普通成员函数之后,
//   moc 不生成对应的元方法,于是 15 个 connect 【全部静默失败】,
//   Qt 只在 stderr 打了一串 "No such slot" 就放过了。
//   放到 slots: 里之后才有元方法,字符串 connect 才找得到。
//   这正是"菜单连错槽不报错、只是点了没反应"的真实长相 ——
//   同样一份代码,放在生产 hmi 里就是用户点菜单没反应,放在测试里被我逮到了。
//
// ★ 关键:槽名必须和 MenuBuilder.cpp 里 SLOT(...) 里的字符串【一字不差】。
//   一旦生产代码改了槽名而这里没跟着改,connect 会失败 → 断言红。
//   这正是我们要的:改菜单的人一定会被这条挡住。
// ---------------------------------------------------------------------------
class FakeReceiver : public QObject {
    Q_OBJECT
public:
    QStringList fired;   // 被触发过的槽名,按触发顺序
    void clearFired() { fired.clear(); }

public slots:
    // --- 工程 ---
    void onOpenClicked()   { fired << QStringLiteral("onOpenClicked"); }
    void onSaveClicked()   { fired << QStringLiteral("onSaveClicked"); }
    // --- 运行 ---
    void onStartClicked()    { fired << QStringLiteral("onStartClicked"); }
    void onStopClicked()     { fired << QStringLiteral("onStopClicked"); }
    void onResetClicked()    { fired << QStringLiteral("onResetClicked"); }
    void onConnectClicked()  { fired << QStringLiteral("onConnectClicked"); }
    void onDisconnectClicked(){ fired << QStringLiteral("onDisconnectClicked"); }
    // --- 画面 ---
    void onEditorClicked()          { fired << QStringLiteral("onEditorClicked"); }
    void onRuntimeCanvasClicked()   { fired << QStringLiteral("onRuntimeCanvasClicked"); }
    void onDemoCanvasClicked()      { fired << QStringLiteral("onDemoCanvasClicked"); }
    // --- 报警 ---
    void onAlarmConfigClicked() { fired << QStringLiteral("onAlarmConfigClicked"); }
    void onAlarmSoundSettings() { fired << QStringLiteral("onAlarmSoundSettings"); }
    // --- PLC(2b 阶段 6)---
    void onPlcConfigClicked()  { fired << QStringLiteral("onPlcConfigClicked"); }
    // --- 数据 ---
    void onVariableConfigClicked(){ fired << QStringLiteral("onVariableConfigClicked"); }
    void onExportVariablesClicked(){ fired << QStringLiteral("onExportVariablesClicked"); }
    void onExportAlarmsClicked()  { fired << QStringLiteral("onExportAlarmsClicked"); }
    // --- 帮助 ---
    void onShowCapabilities()   { fired << QStringLiteral("onShowCapabilities"); }

    // ★ 故意加一个【没有菜单项指向它】的槽。
    //   作用:如果测试写成"触发任意一项 → fired 有一条",那种写法在
    //   "全部菜单项都连到 onShowCapabilities"时一样会绿。
    //   有了这个没人指的槽,断言必须写成"落到指定的那个名字"才拦得住。
    void onNobodyPointsHere()   { fired << QStringLiteral("onNobodyPointsHere"); }
};

namespace {

// 把 "打开工程(&O)..." 还原成 "打开工程..."
//   ★ 不能用简单的 &. 去剥 —— 那样 "工程(&P)" 会变成 "工程()",
//     括号留着,和期望值对不上(第一次跑就是这么红的)。
//     整个 "(&X)" 一起去掉才对。
QString plainText(const QString& s) {
    static const QRegularExpression re(QStringLiteral("\\(&.\\)"));
    QString out = s;
    out.remove(re);
    return out;
}

// ★ 菜单用到的 17 个槽名 —— 本文件唯一的一份"名字清单"。
//   三个地方都用它:
//     · FakeReceiver 的槽(照着它写)
//     · hmiHeader_menuSlots_... 那条(和生产 MenuBuilder.cpp 求交集校验)
//     · moc_reallyRegistersPrivateSlots 那条(断言 moc 逐个查得到)
//   【为什么它可以手写,而槽名那处不能】槽名【本来就要手写】——
//   moc_reallyRegistersPrivateSlots 需要 moc 一个"形状已知"的类,
//   而那个类的槽名只能由测试自己指定,没法从生产代码生成(moc 发生在编译期)。
//   真正的"生产槽名对不对"由 hmiHeader_... 那条现读 MenuBuilder.cpp 负责。
//   两者分工:这里验 moc 的行为,那里验生产的名字。
const QStringList kMenuSlotNames = {
    QStringLiteral("onOpenClicked"),      QStringLiteral("onSaveClicked"),
    QStringLiteral("onStartClicked"),     QStringLiteral("onStopClicked"),
    QStringLiteral("onResetClicked"),     QStringLiteral("onConnectClicked"),
    QStringLiteral("onDisconnectClicked"),
    QStringLiteral("onPlcConfigClicked"),
    QStringLiteral("onEditorClicked"),    QStringLiteral("onRuntimeCanvasClicked"),
    QStringLiteral("onDemoCanvasClicked"),
    QStringLiteral("onAlarmConfigClicked"), QStringLiteral("onAlarmSoundSettings"),
    QStringLiteral("onVariableConfigClicked"),
    QStringLiteral("onExportVariablesClicked"), QStringLiteral("onExportAlarmsClicked"),
    QStringLiteral("onShowCapabilities"),
};

// ★★ P1-a(2026-10-20):17 项的权限表 —— 【跟 MenuBuilder.cpp 的表态一一对应】。
//
//   这份表抄的是 MenuBuilder.cpp 里那些 addItem 的第 6 个参数。
//   【为什么抄一份还叫"测副本"?这里不是】:
//     被测对象不是"生产代码写了什么权限",而是"这份权限在四个角色下
//     实际产生什么 enabled 状态"。后者只能真的建出菜单、切角色、看
//     QAction::isEnabled() —— 生产代码里【没有】这个函数可以调,
//     它把权限绑上去就完事了。所以这里不是抄,是将那张表【翻译成断言】。
//   真正防"生产代码写错了"的那一半在下面那条
//   everyMenuItem_declaresAPermission(编译期已强制)+ 负向对照第 ① 轮。
//
//   【为什么绑定【不】由测试自己调 bindActionEnable】
//   那样测的就是 UIPermission 而不是菜单了:菜单漏绑一项,测试照样绿。
//   现在的形状是"菜单自己绑 → 角色自己切 → 看菜单项自己灰不灰",
//   漏绑一项这一行立刻红 —— 因为 Default 角色是 Viewer,该项该灰却亮着。
enum Gate { kUngated = 0, kWrite, kEdit, kUsers };

struct GateCase {
    const char* menu;
    const char* item;
    Gate        gate;
    const char* why;   // 为什么是这一个 —— 抄菜单的"依据"是为了逼自己再想一遍
};

const GateCase kGateCases[] = {
    // 工程
    {"工程", "打开工程...",   kEdit, "打开=把运行中的整个配置换成磁盘上那份"},
    {"工程", "保存工程...",   kEdit, "写工程文件"},
    // 运行:启停复位要操作员能按;连接/断开任何角色都要能
    {"运行", "启动",         kWrite, "现场操作员一天按几十次"},
    {"运行", "停止",         kWrite, "同上"},
    {"运行", "复位",         kWrite, "同上"},
    {"运行", "连接 PLC",     kUngated, "看现场是最基本的动作,绑了就看不见了"},
    {"运行", "断开",         kUngated, "同上"},
    // PLC
    {"PLC",  "PLC 配置...",  kEdit, "改的是现场设备的地址/协议"},
    // 画面:编辑器是改工程的唯一门;两个画布是只读
    {"画面", "编辑器...",    kEdit, "★ EditorMainWindow 内部零权限检查,这是唯一的门"},
    {"画面", "运行时画面...", kUngated, "只读,Viewer 也该看得见现场画面"},
    {"画面", "演示画布...",  kUngated, "只读;真该做的是发布时摘掉,不是灰它"},
    // 报警
    {"报警", "报警配置...",  kEdit, "对话框内自查同一个权限,这里绑一致"},
    {"报警", "报警声音...",  kEdit, "同上"},
    // 数据:导出【刻意不绑】,理由写在 MenuBuilder.cpp 那两行注释里
    {"数据", "变量配置...",  kEdit, "变量表就是工程文件本体的一部分"},
    {"数据", "导出变量表...", kUngated, "导出只读内存里的表写 CSV,不改任何东西"},
    {"数据", "导出报警表...", kUngated, "同上"},
    // 帮助
    {"帮助", "当前支持的功能", kUngated, "把'你能看说明书'也卡掉是在制造而不是在防"},
};

// 四个角色下每个 Gate 的 enabled 期望。写成表是为了让
// "Operator 到底有没有 EditProject"这类问题有个明确答案,
// 而不是每次靠人回想 Permission.cpp 的映射。
struct RoleGate { qwhmi::auth::Role role; const char* roleName; };
const RoleGate kRoles[] = {
    {qwhmi::auth::Role::Admin,    "Admin"},
    {qwhmi::auth::Role::Engineer, "Engineer"},
    {qwhmi::auth::Role::Operator, "Operator"},
    {qwhmi::auth::Role::Viewer,   "Viewer"},
};

// Gate → 该权限名(读 Permission.cpp 同一份 checkPermission,不重写映射)
qwhmi::auth::Permission permOf(Gate g) {
    switch (g) {
    case kWrite: return qwhmi::auth::Permission::WriteVariable;
    case kEdit:  return qwhmi::auth::Permission::EditProject;
    case kUsers: return qwhmi::auth::Permission::ManageUsers;
    case kUngated: break;
    }
    return qwhmi::auth::Permission::EditRecipe;   // 取一个不会被人当真的值
}

qwhmi::auth::User userWith(qwhmi::auth::Role r) {
    qwhmi::auth::User u;
    u.name = "tester";
    u.role = r;
    return u;
}

} // namespace

class tst_MenuBar : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();

    // ★★★ 核心:菜单结构对不对(6 个顶级菜单、顺序、名字)
    void menuBar_hasExpectedTopLevelMenus();

    // ★★★ 核心:每一项触发后必须落到【指定名字】的槽
    //   本测试的主体。逐项断言"落到谁",而不是"有没有落到某个槽"。
    void everyAction_reachesItsOwnSlot();

    // ★ 字符串 connect 拼错槽名是【静默失败】—— 必须逐项查 connect 结果。
    //   Qt 没有"列出所有 connect 失败项"的 API,只能由生产代码把返回值传出来断言。
    //   这里退一步做等价的兜底:若某项拼错了槽,上一条会红(它根本不会触发任何东西),
    //   本条则确保"没有多出/少掉"这类更隐蔽的错误也能被发现。
    void actionCount_matchesSlotsWired();

    // 快捷键:打开/保存工程这两个是现场最常用的,必须 Ctrl+O / Ctrl+S
    void openAndSave_haveStandardShortcuts();

    // ★ 可重复调用:生产代码里 bar->clear() 就是为了这个。
    //   不清的话测试反复调用会出现两层"工程"菜单 —— 而生产代码只调一次,
    //   这个 bug 平时不会显形,等到菜单改版(比如新增"工具"菜单)就可能要调两次。
    void buildTwice_doesNotDuplicateMenus();

    // 空指针:menuBar 为空时静默返回,不能崩。
    //   真实场景:hmi.ui 里那个 QMenuBar 被误删/被 designer 改名时。
    void nullMenuBar_isNoCrash();

    // ★★ 生产代码自己喊出"没连上"的能力 —— 这一条是替 hmi 兜底的最后一道。
    //   tst_MenuBar 前面所有用例都跑在【假接收者】上,它只能证明
    //   "接到了一个叫这个名字的槽";真跑起来 receiver 是 hmi,
    //   hmi 有没有这个槽只有 hmi 自己的元方法表知道。
    //   所以生产代码必须能把失败【返回出来】,让 hmi 启动时打一行黄字。
    //   下面用一个【故意缺槽】的接收者来验证这条通道真的通。
    void missingSlot_isReportedNotSwallowed();

    // 正常情况下返回的必须是空列表 —— 否则 hmi 每次启动都刷一屏黄字,
    // 喊了等于没喊。空列表是这条通道【可用】的前提,必须单独钉住。
    void healthyReceiver_reportsNoFailures();

    // ★★★ 2026-10-20:本文件【第一次】真能抓住"菜单点了没反应"这个 bug。
    //
    //   上面 10 条全都跑在 FakeReceiver 上,而 FakeReceiver 的槽是本文件自己
    //   声明在 public slots: 里的 —— 它【必然】有元方法。所以哪怕生产 hmi 的
    //   10 个菜单槽全写在普通 private: 里(2026-10-20 修之前的真实状态),
    //   这 10 条照样全绿。而那正是用户报的"菜单栏按了没效果":
    //     · 按钮没事   —— hmi.cpp 用成员函数指针 connect,编译期就解析了
    //     · 菜单是哑的 —— MenuBuilder 用字符串 connect,查的是 moc 元方法表
    //   同一个槽,两种 connect 写法,只有一种能工作。
    //
    //   本条直接读 hmi.h 源码,检查每个菜单槽的声明落在【哪个访问说明符段】里。
    //   moc 只为带 slots 关键字的段生成元方法 —— 这是 Qt 的硬规则,
    //   也是本次事故的唯一根因,所以值得用一条测试钉死。
    void hmiHeader_menuSlots_areDeclaredInsideASlotsSection();

    // ★★ moc 到底认不认 private slots: —— 用【真 moc 一份】的类来验,
    //   而不是靠我复述 moc 的规则。详见实现处那段注释。
    void moc_reallyRegistersPrivateSlots();

    // ★★★ P1-a(2026-10-20):菜单项的权限门控。
    //
    //   P0 只给主画面的按钮绑了权限,菜单 17 项一个都没绑。
    //   P1-b 把按钮裁掉之后这就变成一个【真漏洞】:界面上的"编辑器"灰着,
    //   而 画面→编辑器 菜单项点得开,EditorMainWindow 内部零权限检查。
    //   两个入口门禁不一致,比两个入口都没门更危险 —— 前者让人以为有门。
    //
    //   本组三条的分工:
    //     ① everyMenuItem_declaresAPermission —— 静态:17 项每一项都【表态】了
    //        (绑什么权限,或刻意不绑)。表态这件事在编译期已经强制
    //        (addItem 的 perm 参数无默认值),这条查的是"有没有人在
    //        绕过它" —— 比如手写 menu->addAction 不走 addItem。
    //     ② menuItems_areGatedByRole —— 行为:四角色 × 17 项的 enabled 全矩阵。
    //        默认角色是 Viewer,所以"漏绑一项"在这里是【亮着】而不是"灰着"],
    //        跟"绑错成更严的权限"表现相反,两种失败能分开。
    //     ③ editorItem_isTheOnlyGateOnProjectWriting —— 单项深挖:
    //        按钮 editorBtn 已被裁掉,这个菜单项现在是唯一的一道门,
    //        所以它的权限和"编辑器内部有没有自查"必须同时被钉住。
    void everyMenuItem_declaresAPermission();
    void menuItems_areGatedByRole();
    void editorItem_isTheOnlyGateOnProjectWriting();

private:
    QMenuBar m_bar;
    FakeReceiver m_recv;

    QMenu* menuNamed(const QString& visibleTextWithoutAmpersand);
    // 在指定菜单里按可见文字找 action(& 去掉后的全等匹配)
    static QAction* actionIn(QMenu* m, const QString& text);
};

// 故意【只有一部分槽】的接收者:用来模拟"hmi 里少了某个槽"。
//   注意它【不能】从 FakeReceiver 继承 —— 继承了就连上了,
//   那正是本文件第一次运行时踩的坑(方法不在 slots: 里, moc 不生成元方法)。
class HalfReceiver : public QObject {
    Q_OBJECT
public slots:
    void onOpenClicked()  {}
    // onSaveClicked / onStartClicked / ... 全部【故意不存在】
};

void tst_MenuBar::initTestCase() {
    // ★★★ P1-a(2026-10-20):先以 Admin 登录,再建菜单。
    //
    //   【为什么必须先登录】CurrentUser 未登录时 role 是 Viewer,
    //   而 Viewer 一项权限都没有 —— 菜单一旦建好,17 项里有 10 项是灰的。
    //   而上面那 11 条老用例里有两条是:
    //       QVERIFY2(a->isEnabled(), "菜单项不该是禁用的");
    //       a->trigger();
    //   灰着的 QAction trigger() 【什么都不会触发】(Qt 自己吞掉),
    //   于是这两条会以"触发后 fired 为空"的形式红掉 ——
    //   而那条红跟菜单接得对不对【毫无关系】,纯粹是角色不对。
    //   那正是本文件开头警告的"报错误导人":一个红指向槽名,一个红指向登录态,
    //   混在一起排查会先怀疑错的那一半。
    //
    //   【为什么是 Admin 而不是"没有角色"]没有角色就是 Viewer,没有中间态。
    //   Admin 拥有全部 6 项权限,17 项全亮,老用例回到它们本来的样子。
    //
    //   ★ 下面 menuItems_areGatedByRole 会依次登录四个角色,并以 Viewer 收尾;
    //     它之后的用例都自带 login(),不依赖这里留下的角色。
    qwhmi::auth::CurrentUser::instance().login(userWith(qwhmi::auth::Role::Admin));
    qwhmi::ui::buildApplicationMenu(&m_bar, &m_recv);
}

// QMenuBar 的 action 列表里,顶级菜单的 text 形如 "工程(&P)"
QMenu* tst_MenuBar::menuNamed(const QString& visibleTextWithoutAmpersand) {
    const QList<QAction*> top = m_bar.actions();
    for (QAction* a : top) {
        if (!a->menu()) continue;
        if (plainText(a->text()) == visibleTextWithoutAmpersand) return a->menu();
    }
    return nullptr;
}

QAction* tst_MenuBar::actionIn(QMenu* m, const QString& text) {
    if (!m) return nullptr;
    for (QAction* a : m->actions()) {
        if (plainText(a->text()) == text) return a;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------

void tst_MenuBar::menuBar_hasExpectedTopLevelMenus() {
    QStringList names;
    for (QAction* a : m_bar.actions()) {
        names << plainText(a->text());
    }

    // ★ 顺序也是断言的一部分 —— "运行"放第二个是有意的(现场用得最多),
    //   排到工程后面会让操作员每次先扫一眼用不上的项。
    //   "PLC" 排在"运行"之后、"画面"之前:它比画面更接近现场操作
    //   (改 IP 是工程师到场做的事),但比启动/停止低频。
    const QStringList expected = {
        QStringLiteral("工程"), QStringLiteral("运行"), QStringLiteral("PLC"),
        QStringLiteral("画面"), QStringLiteral("报警"),
        QStringLiteral("数据"), QStringLiteral("帮助")
    };
    QCOMPARE(names, expected);

    // 每一项都必须真的是菜单(带下拉),不是点了没反应的空 action
    for (QAction* a : m_bar.actions()) {
        QVERIFY2(a->menu() != nullptr,
                 qPrintable(QStringLiteral("顶级项「%1」没有下拉菜单").arg(a->text())));
        QVERIFY2(a->menu()->title() == a->text(),
                 "下拉菜单的标题必须和菜单栏上的文字一致");
    }
}

void tst_MenuBar::everyAction_reachesItsOwnSlot() {
    // ★★★ 本文件的核心。
    //   表格式断言:可见文字 → 必须落到的槽名。
    //   加一行菜单 = 加一行表;改一行 connect = 当场红。
    struct Case { const char* menu; const char* item; const char* slot; };
    static const Case kCases[] = {
        {"工程", "打开工程...",   "onOpenClicked"},
        {"工程", "保存工程...",   "onSaveClicked"},
        {"运行", "启动",         "onStartClicked"},
        {"运行", "停止",         "onStopClicked"},
        {"运行", "复位",         "onResetClicked"},
        {"运行", "连接 PLC",     "onConnectClicked"},
        {"运行", "断开",         "onDisconnectClicked"},
        {"PLC",  "PLC 配置...",  "onPlcConfigClicked"},
        {"画面", "编辑器...",    "onEditorClicked"},
        {"画面", "运行时画面...", "onRuntimeCanvasClicked"},
        {"画面", "演示画布...",  "onDemoCanvasClicked"},
        {"报警", "报警配置...",  "onAlarmConfigClicked"},
        {"报警", "报警声音...",  "onAlarmSoundSettings"},
        {"数据", "变量配置...",  "onVariableConfigClicked"},
        {"数据", "导出变量表...", "onExportVariablesClicked"},
        {"数据", "导出报警表...", "onExportAlarmsClicked"},
        {"帮助", "当前支持的功能", "onShowCapabilities"},
    };

    for (const Case& c : kCases) {
        QMenu* m = menuNamed(QString::fromUtf8(c.menu));
        QVERIFY2(m, qPrintable(QStringLiteral("没有顶级菜单「%1」").arg(QLatin1String(c.menu))));

        QAction* a = actionIn(m, QString::fromUtf8(c.item));
        QVERIFY2(a, qPrintable(QStringLiteral("菜单「%1」里没有「%2」")
                     .arg(QLatin1String(c.menu), QLatin1String(c.item))));
        QVERIFY2(a->isEnabled(), "菜单项不该是禁用的");

        // ★ 触发前先清空,再触发,再【只看这一条】——
        //   这样"这项连到了别人的槽"会被精确指认,而不是笼统的"内容不对"。
        m_recv.clearFired();
        a->trigger();
        QCOMPARE(m_recv.fired.size(), 1);
        QCOMPARE(m_recv.fired.at(0), QString::fromUtf8(c.slot));
    }

    // 顺便确认没人指向那个故意留着的诱饵槽
    QVERIFY(!m_recv.fired.contains(QStringLiteral("onNobodyPointsHere")));
}

void tst_MenuBar::actionCount_matchesSlotsWired() {
    // 每一项都必须真的有 receiver —— connect 失败时 QAction 仍在菜单里,
    // 数量对得上却点不动。上一条 everyAction_reachesItsOwnSlot 靠"触发后
    // fired 为空"抓到了这种情况;本条防的是另一头:多一项没人认领的孤立 action。
    struct Count { const char* menu; int expected; };
    // 工程 = 2 项(没有多余的分隔符);运行 = 5 项 + 1 分隔符;其余各项全列
    static const Count kCounts[] = {
        {"工程", 2}, {"运行", 6}, {"PLC", 1}, {"画面", 3},
        {"报警", 2}, {"数据", 3}, {"帮助", 1}
    };
    int total = 0;
    for (const Count& c : kCounts) {
        QMenu* m = menuNamed(QString::fromUtf8(c.menu));
        QVERIFY2(m, c.menu);
        const int n = m->actions().size();
        QVERIFY2(n == c.expected,
                 qPrintable(QStringLiteral("菜单「%1」应有 %2 个 action(含分隔符),实际 %3")
                     .arg(QLatin1String(c.menu)).arg(c.expected).arg(n)));
        total += n;
    }
    QCOMPARE(m_bar.actions().size(), 7);
    QVERIFY(total > 0);

    // ★★ 全菜单扫一遍:不许出现"新建工程"。
    //   程序现在【没有】这个功能;写个灰的占位项会让人反复点,不如不出现。
    //   这条钉住的是"不出现",而不是"出现时是灰的" —— 后者会诱使人去实现它。
    //   写成扫全部菜单而不是只看工程那一个:万一以后有人在别的菜单里
    //   也塞一个"新建",这条一样能逮到。
    const QList<QAction*> top = m_bar.actions();
    for (QAction* a : top) {
        if (!a->menu()) continue;
        for (QAction* sub : a->menu()->actions()) {
            QVERIFY2(!plainText(sub->text()).contains(QStringLiteral("新建")),
                     qPrintable(QStringLiteral("菜单里出现了还没有实现的「%1」")
                         .arg(plainText(sub->text()))));
            // 末尾不留空分隔符 —— QMenu 会自动隐藏它,留着只是骗自己
            QVERIFY2(!(sub == a->menu()->actions().last() && sub->isSeparator()),
                     qPrintable(QStringLiteral("菜单「%1」末尾多了一个空分隔符")
                         .arg(plainText(a->text()))));
        }
    }
}

void tst_MenuBar::openAndSave_haveStandardShortcuts() {
    // 现场最常用的两个必须能用键盘 —— 戴手套的时候鼠标不好点。
    QMenu* m = menuNamed(QStringLiteral("工程"));
    QVERIFY(m);
    QAction* open = actionIn(m, QStringLiteral("打开工程..."));
    QAction* save = actionIn(m, QStringLiteral("保存工程..."));
    QVERIFY(open);
    QVERIFY(save);
    QCOMPARE(open->shortcut(), QKeySequence(QKeySequence::Open));
    QCOMPARE(save->shortcut(), QKeySequence(QKeySequence::Save));
}

void tst_MenuBar::buildTwice_doesNotDuplicateMenus() {
    qwhmi::ui::buildApplicationMenu(&m_bar, &m_recv);
    QCOMPARE(m_bar.actions().size(), 7);

    // 顶层菜单【数量】对了还不够:如果没 clear,会出现 12 个("工程"出现两次)。
    // 而"两处都叫工程"这个状态肉眼很难在测试报告里看出来,所以显式查名字。
    int gongcheng = 0;
    for (QAction* a : m_bar.actions()) {
        if (a->text().startsWith(QStringLiteral("工程"))) ++gongcheng;
    }
    QCOMPARE(gongcheng, 1);

    // 重建之后 connect 仍然是好的(不是"旧 action 还在、新 action 没连")
    QAction* open = actionIn(menuNamed(QStringLiteral("工程")), QStringLiteral("打开工程..."));
    QVERIFY(open);
    m_recv.clearFired();
    open->trigger();
    QCOMPARE(m_recv.fired, QStringList{QStringLiteral("onOpenClicked")});
}

void tst_MenuBar::nullMenuBar_isNoCrash() {
    // 传 nullptr 必须【返回一条失败】而不是崩,更不能安静地返回空列表。
    //   为什么要有这条:hmi.ui 里那个 QMenuBar 是设计器放上去的,
    //   将来有人把它删了/改名了,menuBar() 就返回 nullptr ——
    //   没有这条的话,构造函数里直接段错误,用户看到的是"程序双击就闪退",
    //   查半天却跟菜单没关系。
    //   而返回空列表更糟:那样 hmi 会以为菜单搭好了,一个菜单项都没有,
    //   用户看到的是"菜单栏空的",既不崩也不报错。
    const QStringList failed = qwhmi::ui::buildApplicationMenu(nullptr, &m_recv);
    QCOMPARE(failed.size(), 1);
    QVERIFY2(failed.at(0).contains(QStringLiteral("菜单栏")),
             qPrintable(QStringLiteral("失败原因要说清楚,实际:%1").arg(failed.at(0))));
}

void tst_MenuBar::missingSlot_isReportedNotSwallowed() {
    // ★★★ 替 hmi 兜底的最后一道。
    //   HalfReceiver 只有 onOpenClicked 一个槽,其余 16 个 connect 必然失败 ——
    //   这正是"hmi 里少了个槽"在真实运行时的样子。
    //   改之前这些失败被 addItem 静默吞掉,菜单照样建出来,
    //   用户点那 16 个菜单项【什么都不会发生】,而程序一声不吭。
    HalfReceiver half;
    const QStringList failed = qwhmi::ui::buildApplicationMenu(&m_bar, &half);

    // 17 个菜单项,只有 1 个能连上 → 16 个必须被报出来,一个都不能少
    //   ★ 加了"变量配置"之后从 15 变成 16。这是【故意】写死数字而不是
    //     "总数 - 1"的原因:后者在有人加了菜单项却忘了接槽时会【照样绿】,
    //     而那正是这个用例存在的意义 —— 那条静默失败必须被抓住。
    QCOMPARE(failed.size(), 16);

    // 报出来的内容必须【指得出是哪个菜单的哪一项、想连的是哪个槽】
    //   —— 只报一个数字("14 个没连上")的话,排查的人还得自己去猜是哪里错了。
    QVERIFY2(failed.at(0).contains(QStringLiteral("保存工程")),
             qPrintable(QStringLiteral("失败条目里应有菜单项名字,实际:%1").arg(failed.at(0))));
    QVERIFY2(failed.at(0).contains(QStringLiteral("onSaveClicked")),
             qPrintable(QStringLiteral("失败条目里应有槽名,实际:%1").arg(failed.at(0))));
    QVERIFY2(failed.at(0).contains(QStringLiteral("工程")),
             qPrintable(QStringLiteral("失败条目里应有所属菜单,实际:%1").arg(failed.at(0))));

    // 没报出来的那个确实接上了 —— 反过来验证没有【多报】
    for (const QString& f : failed) {
        QVERIFY2(!f.contains(QStringLiteral("打开工程")),
                 qPrintable(QStringLiteral("onOpenClicked 是有的,不该报:%1").arg(f)));
    }

    // ★ 菜单【照样建出来了】。
    //   这是刻意的:一个槽名写错,不该让整个菜单栏消失 ——
    //   否则用户看到的是"程序连启动都出不来",而真实情况只是少了一个功能。
    QCOMPARE(m_bar.actions().size(), 7);
}

void tst_MenuBar::healthyReceiver_reportsNoFailures() {
    // ★ 空列表是这条通道【可用】的前提,必须单独钉住。
    //   如果 buildApplicationMenu 永远返回非空,那 hmi 每次启动都刷一屏黄字,
    //   喊了等于没喊 —— 几个月后没人看日志了,真出事那天也一样没人看。
    qwhmi::ui::buildApplicationMenu(&m_bar, &m_recv);
    const QStringList failed = qwhmi::ui::buildApplicationMenu(&m_bar, &m_recv);
    QVERIFY2(failed.isEmpty(),
             qPrintable(QStringLiteral("槽齐全时不该报任何失败,实际报了:%1")
                 .arg(failed.join(QStringLiteral(" / ")))));
}

// ---------------------------------------------------------------------------
// ★★★ 2026-10-20 新增:唯一能抓住"菜单点了没反应"的一条
//
// 【事故经过】用户报"菜单栏的这些按钮按了没效果"。
//   查下来 17 个菜单项里有 10 个是哑的,而且:
//     · 编译零报错(字符串 connect 编译期不校验槽名)
//     · 运行零日志(QObject::connect 只往 stderr 打一行 "No such slot",
//       而正式版 GUI 程序没有控制台,那行字根本没人看得见)
//     · tst_MenuBar 10 条全绿(它跑在 FakeReceiver 上,那个类必然是对的)
//   只有一处能看出来:moc 生成的文件里没有那些槽。
//
// 【为什么 moc 只认 slots: 段】
//   moc 是文本处理器。它扫描类体,遇到 public:/private:/protected: 会记下
//   "当前访问权限",但【只有遇到带 slots 关键字的说明符】
//   才会把后面的方法登记成元方法。普通 private: 里的方法对 moc 不存在。
//   对比:
//     private:        void onFoo();   → moc 不生成 → 字符串 connect 静默失败
//     private slots:  void onFoo();   → moc 生成    → 字符串 connect 成功
//   而成员函数指针 connect 两种都能用 —— 它在编译期就解析了,根本不过 moc。
//   所以"按钮好好的、菜单却是哑的"完全可能同时发生。
//
// 【本测试怎么查】
//   读 hmi.h 源码,按行跟踪当前所处的访问说明符段,
//   然后要求每个菜单槽的声明落在【带 slots 关键字】的那一段里。
//   这是对 moc 规则的直接翻译 —— moc 自己就是这么扫的。
//
// 【为什么不直接查 moc 生成的文件】
//   那条路更贴近运行时,但 moc 产物路径随构建系统变
//   (CMake/Ninja 落在 build-menu/hmi_autogen/<hash>/moc_hmi.cpp,
//    VS 落在 x64\Debug\hmi\moc_hmi.cpp,hash 还每次都不一样),
//   两条构建轨都得写一遍、还得处理"产物还没生成"的情况。
//   查源码只有一处真相,而且失败信息能直接指到源码行。
//
// 【边界 —— 说清楚免得误以为它包打天下】
//   ✓ 能抓:菜单槽被声明在非 slots: 段 / 被删掉 / 被改名
//   ✗ 不能抓:hmi.cpp 里槽的实现体为空、或实现体自己报错弹框 ——
//     那是"槽接上了但没做事",和这次的静默 connect 失败是另一类。
// ---------------------------------------------------------------------------

// 一个访问说明符段。moc 只对 isSlots 为真的段生成元方法。
namespace {

struct Section {
    QString access;   // "public" / "private" / "protected"
    bool    isSlots;  // 是否带 slots 关键字
    int     lineNo;   // 该说明符所在行(1 起)
};

// 逐行扫 hmi.h,返回每一行所处的访问说明符段(按段起点索引)。
//   ★ 只处理【类体顶层】的说明符 —— hmi.h 里嵌了一个 struct PendingModbusRequest,
//     它内部若出现 "public:" 也不该影响主类的段跟踪。这里用大括号深度守住:
//     深度 1 才是类体本身,深度 >= 2 是嵌套类型。
QVector<Section> scanSections(const QString& text) {
    QVector<Section> out;
    int depth = 0;
    // 当前段,先用文件第一个说明符之前的状态占位
    Section cur{QStringLiteral("?"), false, 0};

    const QStringList lines = text.split(QLatin1Char('\n'));
    for (int i = 0; i < lines.size(); ++i) {
        const QString& raw = lines.at(i);

        // 去掉行注释,免得把注释里的 "public:" 当成真说明符
        QString code = raw;
        const int cmt = code.indexOf(QLatin1String("//"));
        if (cmt >= 0) code = code.left(cmt);

        // 只在【类体顶层】认访问说明符
        if (depth == 1) {
            static const QRegularExpression re(
                QStringLiteral("^\\s*(public|private|protected)\\s*(slots)?\\s*:"));
            const QRegularExpressionMatch m = re.match(code);
            if (m.hasMatch()) {
                cur.access  = m.captured(1);
                cur.isSlots = !m.captured(2).isNull();
                cur.lineNo  = i + 1;
                out.append(cur);
            }
        }

        // 更新大括号深度(字符串字面量里的括号极少,不为它单开解析)
        for (const QChar ch : code) {
            if (ch == QLatin1Char('{')) ++depth;
            else if (ch == QLatin1Char('}')) --depth;
        }
    }
    return out;
}

} // namespace

void tst_MenuBar::hmiHeader_menuSlots_areDeclaredInsideASlotsSection() {
    // ---- 1. 菜单用到的槽名,从【生产的 MenuBuilder 源码】里现读 ----
    //   不在本文件另抄一份 —— 抄一份就又变成"测副本",改生产代码时测试照样绿,
    //   那正是本文件开头警告过的事。
    const QString mbPath = QStringLiteral(MENU_SRC_DIR_STRING "/ui/MenuBuilder.cpp");
    QFile mb(mbPath);
    QVERIFY2(mb.open(QIODevice::ReadOnly | QIODevice::Text),
             qPrintable(QStringLiteral("打不开 %1(菜单槽名从这儿现读)").arg(mbPath)));
    const QString mbText = QString::fromUtf8(mb.readAll());
    mb.close();

    static const QRegularExpression slotRe(
        QStringLiteral("SLOT\\(\\s*(\\w+)\\s*\\(\\s*\\)\\s*\\)"));
    QStringList menuSlots;
    QRegularExpressionMatchIterator it = slotRe.globalMatch(mbText);
    while (it.hasNext()) menuSlots << it.next().captured(1);
    menuSlots.removeDuplicates();

    // 17 个是当前的实数(工程2+运行5+PLC1+画面3+报警2+数据3+帮助1)。
    //   ★ 写死数字而不是"至少 1 个" —— 后者在解析器写坏时会红不了,
    //     而这条测试的价值全在"它真的在读那份文件"上。
    QCOMPARE(menuSlots.size(), 17);

    // ---- 2. 读 hmi.h,扫出所有访问说明符段 ----
    const QString hmiHeader = QStringLiteral(HMI_SRC_DIR_STRING "/hmi.h");
    QFile f(hmiHeader);
    QVERIFY2(f.open(QIODevice::ReadOnly | QIODevice::Text),
             qPrintable(QStringLiteral("打不开 %1").arg(hmiHeader)));
    const QString text = QString::fromUtf8(f.readAll());
    f.close();

    const QVector<Section> sections = scanSections(text);
    QVERIFY2(!sections.isEmpty(),
             "hmi.h 里一个访问说明符都没扫到 —— 大概率是类体深度判断写错了,"
             "那样这条测试会【永远绿】,等于没有");

    // ---- 3. 逐个槽查:声明在不在?在哪个段? ----
    QStringList problems;
    const QStringList lines = text.split(QLatin1Char('\n'));

    for (const QString& slot : menuSlots) {
        // 找声明行:行首到 '(' 之间的类型片段 + 槽名 + '('
        //   slot 本身就来自 \w+ 的捕获,不含正则元字符,直接拼进去即可。
        const QRegularExpression declRe(
            QStringLiteral("^\\s*(?:virtual\\s+)?[\\w:<>\\*&\\s]*?\\b%1\\s*\\(").arg(slot));

        int declLine = -1;
        bool declInSlots = false;
        QString declAccess;

        int lineNo = 0;
        for (const QString& raw : lines) {
            ++lineNo;
            // 剔掉行注释 —— hmi.h 的注释里满是"void onFoo();"这类字样
            QString code = raw;
            const int cmt = code.indexOf(QLatin1String("//"));
            if (cmt >= 0) code = code.left(cmt);
            if (!declRe.match(code).hasMatch()) continue;

            // 这一行属于哪个段?倒序找【段起点 <= 本行】的最后一个
            for (int k = sections.size() - 1; k >= 0; --k) {
                if (sections.at(k).lineNo <= lineNo) {
                    declInSlots = sections.at(k).isSlots;
                    declAccess  = sections.at(k).access;
                    break;
                }
            }
            declLine = lineNo;
            break;   // 只认第一次声明(重定义编译器会抓,不必在这里管)
        }

        if (declLine < 0) {
            problems << QStringLiteral("%1:hmi.h 里【根本没有这个声明】—— 菜单会报 No such slot")
                            .arg(slot);
        } else if (!declInSlots) {
            problems << QStringLiteral(
                          "%1:声明在 hmi.h 第 %2 行的 \"%3:\" 段里,"
                          "必须挪进 \"%3 slots:\" 段 —— moc 不为普通私有函数生成元方法,"
                          "菜单项点了会【静默无反应】")
                          .arg(slot).arg(declLine).arg(declAccess);
        }
    }

    QVERIFY2(problems.isEmpty(),
             qPrintable(QStringLiteral("以下菜单槽不会被 moc 收录(菜单点了没反应):\n  %1")
                 .arg(problems.join(QStringLiteral("\n  ")))));
}

// ---------------------------------------------------------------------------
// ★★ 2026-10-20 加固:上一条查的是【我读源码得出的结论】,这条查 moc 的【真实产物】。
//
// 【为什么上一条不够】它复刻的是 moc 的规则,不是 moc 本身。
//   复刻和原件可能不一致,而这次事故的教训恰恰是"我以为我懂了"——
//   真正的判决者是 moc_hmi.cpp 里那张 qt_static_metacall 分发表:
//   字符串 connect 查的就是那张表,表里没有 = 点了没反应。
//   所以最硬的证据不是"它在 slots: 段里",而是"case N 真的出现在产物里"。
//
// 【为什么不能直接查 moc_hmi.cpp】它在构建目录里,而构建目录名随构建系统变:
//   CMake/Ninja → build*/hmi_autogen/<8位哈希>/moc_hmi.cpp(哈希每次都不一样)
//   VS         → <OutDir>/qt/moc/moc_hmi.cpp
//   两条轨、两个路径、还可能"产物还没生成"。写死一条 = 另一种单点故障。
//
// 【本条的取舍】不去找产物,而是让【本测试自己 moc 一份】:
//   本文件已经 include 了 MenuBuilder.h,而 hmi.h 依赖 uic 生成的 ui_*.h ——
//   hmi 编不了,那就在本文件里放一个【形状和 hmi 一模一样的小类】,
//   把 17 个菜单槽声明在 private slots: 里(和 hmi 修好之后一样),
//   然后断言 QObject::staticMetaObject.indexOfSlot() 能【逐个查到】。
//
//   ★ 这测的不是"hmi.h 写对了吗"(那是上一条干的),而是:
//     【moc 对这种写法到底会不会生成元方法】—— 也就是这条不变量的物理基础。
//   两者合起来才闭环:
//     上一条 保证 hmi.h 的声明位置对
//     本条   保证 moc 认这种位置
//     任一条单独成立都不够,缺另一条就还是哑菜单。
//
// 【负向对照 —— 实跑,数字如实记】
//   把 ClassUnderTest 的 17 个槽挪回普通 private: →
//     红 1:moc_reallyRegistersPrivateSlots
//         (一次列全 17 个名字:onOpenClicked … onShowCapabilities)
//     绿 11 —— 其中 hmiHeader_menuSlots_areDeclaredInsideASlotsSection
//          绿得【天经地义】:它查的是 hmi.h,根本不看这个类。
// ★ 这正是两条都需要存在的证明:各红各的,互不重叠。
//   只有上一条 → moc 哪天改规则,测试照绿;只有本条 → hmi.h 挪错段,测试照绿。
// ---------------------------------------------------------------------------

// ★ 和 hmi 形状对齐的替身:同一批菜单槽,同样声明在 private slots: 里。
//   【为什么它不能继承 FakeReceiver】继承了就连上了 ——
//   那正是本文件第一次运行时踩的坑(测试替身天然"正确",看不见真 bug)。
class ClassUnderTest : public QObject {
    Q_OBJECT
private slots:
    void onOpenClicked()  {}
    void onSaveClicked()  {}
    void onStartClicked()   {}
    void onStopClicked()    {}
    void onResetClicked()   {}
    void onConnectClicked() {}
    void onDisconnectClicked() {}
    void onPlcConfigClicked() {}
    void onEditorClicked() {}
    void onRuntimeCanvasClicked() {}
    void onDemoCanvasClicked() {}
    void onAlarmConfigClicked() {}
    void onAlarmSoundSettings() {}
    void onVariableConfigClicked() {}
    void onExportVariablesClicked() {}
    void onExportAlarmsClicked() {}
    void onShowCapabilities() {}

    // ★ 故意放在【普通 private:】里的一个 —— 它必须查不到。
    //   这一条是本用例的阴性对照:如果它也能查到,
    //   说明下面的 indexOfSlot 根本没在查元方法表(或者查错了对象),
    //   那"17 个全查到"就是【空洞地绿】。
private:
    void onThisOneMustNotBeARealSlot() {}
};

void tst_MenuBar::moc_reallyRegistersPrivateSlots() {
    // staticMetaObject 本身就是指针(staticMetaObject 声明为 const QMetaObject*),
    //   别再取地址 —— 那是第一次编译报 C2440 的原因。
    const QMetaObject* mo = &ClassUnderTest::staticMetaObject;

    // ★ 先证伪:普通 private: 里的方法【必须】查不到。
    //   这一条是整个用例的地基 —— 它保证下面那些"查到了"不是假阳性。
    QVERIFY2(mo->indexOfSlot("onThisOneMustNotBeARealSlot()") == -1,
             "普通 private: 里的方法居然能被 indexOfSlot 查到 —— "
             "那么 moc 不生成元方法这条前提就不成立,本用例失去意义");

    // 再证真:17 个菜单槽必须全部能被查到。
    //   ★ 逐个断言而不是只比数量 —— 少一个和全错,两种失败要能分开。
    QStringList missing;
    for (const QString& slot : kMenuSlotNames) {
        const QByteArray sig = (slot + QStringLiteral("()")).toLatin1();
        if (mo->indexOfSlot(sig) == -1) missing << slot;
    }
    QVERIFY2(missing.isEmpty(),
             qPrintable(QStringLiteral("这些槽 moc 没生成元方法(菜单会点了没反应): %1")
                 .arg(missing.join(QStringLiteral(", ")))));
}

// ---------------------------------------------------------------------------
// ★★★ P1-a(2026-10-20):菜单项的权限门控
//
// 【为什么这是 P1-b 的【前置条件】而不是配套收拾】
//   P0 只给主画面那几个按钮绑了权限,菜单 17 项一个都没绑。
//   单看今天:只是"菜单宽进"了一点,不算漏洞 ——
//   因为主画面还摆着按钮,能用的人本来就能用。
//   P1-b 把那 8 个按钮裁掉之后,形状变了:
//     界面上"编辑器"灰着(按钮还在、只是被权限门控挡住),
//     而 画面 → 编辑器 菜单项【点得开】——
//     EditorMainWindow 内部零权限检查,它直接 project_->saveToFile()。
//   也就是说:【一个 Viewer 能改工程并写盘】。
//   而"灰着的按钮"比"根本没有按钮"更危险 ——
//   它让人相信"编辑器对我关着",从而放心地把工程文件交给对方,
//   而实际上对方从菜单里就进去了。
//   两个入口门禁不一致,比两个入口都没门更糟。
//
// 【为什么"绑权限"这件事本身还要再钉一层】
//   P0 的事故形状不是"忘了设计",是"加功能时漏了一步":
//   主画面每加一个按钮都写了 tooltip,没人顺手补 bindButtonEnable,
//   而漏一次【不报错、测试也绿】。17 项如果靠人自觉,下次加第 18 项就会重演。
//   所以生产代码把表态做成编译期强制(addItem 的 perm 参数无默认值),
//   而下面 ① 查的是"有没有人绕过 addItem 手写 menu->addAction"——
//   那条路绕过编译期强制,是这条防线唯一的缺口。
// ---------------------------------------------------------------------------

namespace {

// 去掉注释再比对 —— hmi.cpp / MenuBuilder.cpp 的注释里满是
// "editorBtn / saveButton"这类名字(说明它们为什么被删),
// 不去掉就会把"注释提到了"当成"代码还在",测试永远绿。
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

int countOf(const QString& text, const QString& needle) {
    int n = 0, i = 0;
    while ((i = text.indexOf(needle, i)) >= 0) { ++n; i += needle.size(); }
    return n;
}

} // namespace

// ★★ P1-a ①:静态 —— 17 项每一项都【表态】了
void tst_MenuBar::everyMenuItem_declaresAPermission() {
    const QString path = QStringLiteral(MENU_SRC_DIR_STRING "/ui/MenuBuilder.cpp");
    QFile f(path);
    QVERIFY2(f.open(QIODevice::ReadOnly | QIODevice::Text),
             qPrintable(QStringLiteral("打不开 %1").arg(path)));
    const QString all = QString::fromUtf8(f.readAll());
    f.close();

    // 只取 buildApplicationMenu 这个函数体 —— addItem 的两个重载定义在它前面,
    // 一起数会把"定义"数进来。
    const int fnAt = all.indexOf(QStringLiteral("QStringList buildApplicationMenu"));
    QVERIFY2(fnAt >= 0, "MenuBuilder.cpp 里找不到 buildApplicationMenu 的定义");
    const QString body = all.mid(fnAt);

    // ---- 1. 菜单项【只能】从 addItem 出 ----
    //   addItem 的 perm 参数没有默认值(生产代码的编译期强制),
    //   这条是那道强制的唯一缺口:menu->addAction(...) 完全绕过它。
    //   ★ 只在 body 里数 —— addItem 自己的实现里有 menu->addAction(text),
    //     那是【建出 action】的必要一步,不是绕过表态。
    const int directAdds = countOf(body, QStringLiteral("->addAction("));
    QCOMPARE(directAdds, 0);

    // ---- 2. 每一项都必须写了权限,或写了"刻意不绑" ----
    //   逐条语句查,而不是只查全文含不含 std::nullopt ——
    //   后者在【漏了一项】时照样绿(别的项写了 nullopt 就搜得到)。
    const QStringList stmts = body.split(QLatin1Char(';'));
    QStringList problems;
    int itemCount = 0;
    for (const QString& raw : stmts) {
        const QString s = raw.simplified();
        if (!s.contains(QStringLiteral("addItem("))) continue;
        ++itemCount;
        const bool stated = s.contains(QStringLiteral("Permission::"))
                         || s.contains(QStringLiteral("std::nullopt"));
        if (!stated) {
            problems << QStringLiteral("这一项没有表态: %1")
                        .arg(s.left(120).append(QStringLiteral(" ...")));
        }
    }

    // 17 项(工程2+运行5+PLC1+画面3+报警2+数据3+帮助1)。
    //   ★ 写死而不是"至少 1 个":解析器写坏时后者红不了,
    //     而这条测试的全部价值就在"它真的读到了那份文件"。
    QCOMPARE(itemCount, 17);
    QVERIFY2(problems.isEmpty(),
             qPrintable(QStringLiteral(
                 "这些菜单项没表态权限(加一项却忘了写【绑什么 / 刻意不绑】,"
                 "就等于绕过编译期强制,门禁又漏了):\n  %1")
                 .arg(problems.join(QStringLiteral("\n  ")))));
}

void tst_MenuBar::menuItems_areGatedByRole() {
    // ★★ 行为:四角色 × 17 项的 enabled 全矩阵。
    //
    //   【为什么默认角色是关键】CurrentUser 未登录时 role 是 Viewer,
    //   而 Viewer 一项权限都没有。所以"菜单漏绑一项"在这里表现为
    //   【该项亮着】—— 与"绑错成更严的权限"(该项该亮却灰着)方向相反,
    //   两种失败能被这两类断言分开,不会互相掩盖。
    //
    //   【为什么逐项查,不按菜单查】一份表漏一项时按菜单查会整块跳过,
    //   报错还指出的是菜单名而不是项名,排查要多翻一次。
    QStringList problems;
    for (const RoleGate& r : kRoles) {
        qwhmi::auth::CurrentUser::instance().login(userWith(r.role));

        for (const GateCase& c : kGateCases) {
            QMenu* m = menuNamed(QString::fromUtf8(c.menu));
            QVERIFY2(m, qPrintable(QStringLiteral("没有顶级菜单「%1」").arg(QLatin1String(c.menu))));
            QAction* a = actionIn(m, QString::fromUtf8(c.item));
            QVERIFY2(a, qPrintable(QStringLiteral("菜单「%1」里没有「%2」")
                         .arg(QLatin1String(c.menu), QLatin1String(c.item))));

            // 期望值直接问生产的 checkPermission,不在本文件重写角色→权限映射 ——
            // 那样这张表就会和 Permission.cpp 分叉,而分叉的方向恰好是
            // "测试说该能、生产说不能"这种最难查的错。
            const bool expect = c.gate == kUngated
                || qwhmi::auth::checkPermission(r.role, permOf(c.gate));

            const QString where = QStringLiteral("%1(角色 %2)")
                .arg(QString::fromUtf8(c.item), QLatin1String(r.roleName));
            if (a->isEnabled() != expect) {
                problems += QStringLiteral(
                    "%1 应%2,实际%3 —— 依据:%4")
                    .arg(where,
                         expect ? QStringLiteral("亮着") : QStringLiteral("灰着"),
                         a->isEnabled() ? QStringLiteral("亮着") : QStringLiteral("灰着"),
                         QString::fromUtf8(c.why));
            }
        }
    }
    QVERIFY2(problems.isEmpty(),
             qPrintable(QStringLiteral("菜单门控矩阵对不上:\n  %1")
                 .arg(problems.join(QStringLiteral("\n  ")))));
}

// ★★★ P1-a ③:编辑器菜单项现在是"改工程"的【唯一一道门】
void tst_MenuBar::editorItem_isTheOnlyGateOnProjectWriting() {
    QMenu* screen = menuNamed(QStringLiteral("画面"));
    QVERIFY(screen);
    QAction* editor = actionIn(screen, QStringLiteral("编辑器..."));
    QVERIFY(editor);

    // ---- 1. 行为:Viewer / Operator 灰,Engineer / Admin 亮 ----
    //   两个方向都要查:只查"Viewer 灰"会放过"绑成了 ManageUsers"这种
    //   过度收紧 —— 结果是【工程师也进不去编辑器】,功能没了但测试全绿。
    struct Want { qwhmi::auth::Role role; const char* name; bool enabled; };
    const Want kWant[] = {
        {qwhmi::auth::Role::Admin,    "Admin",    true},
        {qwhmi::auth::Role::Engineer, "Engineer", true},
        {qwhmi::auth::Role::Operator, "Operator", false},
        {qwhmi::auth::Role::Viewer,   "Viewer",   false},
    };
    for (const Want& w : kWant) {
        qwhmi::auth::CurrentUser::instance().login(userWith(w.role));
        QCOMPARE(editor->isEnabled(), w.enabled);
        if (!w.enabled) continue;
        // ★ 顺带钉住"亮着的时候真的点得动"——
        //   权限门控只是把 enabled 设成 true,连接断了照样点不动,
        //   而"菜单项亮着但点了没反应"正是本文件开头写的那类最恶劣 bug。
        m_recv.clearFired();
        editor->trigger();
        QCOMPARE(m_recv.fired, QStringList{QStringLiteral("onEditorClicked")});
    }

    // ---- 2. 静态:这道门【外面】不能再有第二个入口 ----
    //   P1-b 把主画面的 editorBtn 撤了。万一将来有人觉得"放在主画面更顺手"
    //   又加回来,而那个新按钮漏绑权限 —— 那就等于门又漏了一道,
    //   而界面上的样子是"一个灰按钮 + 一个能点的菜单",没人看得出门在哪。
    //   所以这里把"主画面【代码里】不再有编辑器按钮"钉住。
    //   ★ 先剥注释:hmi.cpp 里那段注释【故意】提到了 editorBtn(说明为什么删),
    //     不剥就会把"注释提到了"当成"代码还在",这条永远绿。
    const QString hmiCppPath = QStringLiteral(HMI_SRC_DIR_STRING "/hmi.cpp");
    QFile hf(hmiCppPath);
    QVERIFY2(hf.open(QIODevice::ReadOnly | QIODevice::Text),
             qPrintable(QStringLiteral("打不开 %1").arg(hmiCppPath)));
    const QString hmiCode = codeOnly(QString::fromUtf8(hf.readAll()));
    hf.close();
    QVERIFY2(!hmiCode.contains(QStringLiteral("editorBtn")),
             "hmi.cpp 里【代码中】又出现了 editorBtn —— P1-b 已经把这个按钮撤掉了。"
             "再放回来之前先想清楚:它绑权限了吗?菜单项是唯一一道门,"
             "多一个入口就多一处可能没门的地方;要放回主画面,请连 bindButtonEnable 一起加。");

    // ---- 3. 静态:编辑器内部确实没有第二道自查 ----
    //   这不是"要求它别有自查"(那反而是坏事),而是把"菜单项是唯一的门"
    //   这个前提【如实写下来】:哪天有人给 EditorMainWindow 加了 checkPermission,
    //   这条会红,提示他更新注释和第 1 步的说明 —— 因为那时这句话已经不成立了,
    //   而过期的"安全说明"比没有说明更坏。
    const QString edPath = QStringLiteral(HMI_SRC_DIR_STRING "/src/editor/EditorMainWindow.cpp");
    QFile ef(edPath);
    QVERIFY2(ef.open(QIODevice::ReadOnly | QIODevice::Text),
             qPrintable(QStringLiteral("打不开 %1").arg(edPath)));
    const QString edText = QString::fromUtf8(ef.readAll());
    ef.close();
    const bool editorSelfChecks = edText.contains(QStringLiteral("checkPermission"));
    if (editorSelfChecks) {
        qInfo("EditorMainWindow 里已经出现 checkPermission —— "
              "『编辑器菜单项是唯一一道门』这句话已过期,MenuBuilder.cpp 的注释也要一起改。");
    }
    // 无条件成立的那一半:菜单项【必须】拦住 Viewer,与内部有没有自查无关。
    qwhmi::auth::CurrentUser::instance().login(userWith(qwhmi::auth::Role::Viewer));
    QVERIFY2(!editor->isEnabled(),
             "Viewer 能点开编辑器,而 EditorMainWindow 内部零权限检查 —— "
             "Viewer 可以改工程并写盘");
}

QTEST_MAIN(tst_MenuBar)
#include "tst_MenuBar.moc"
