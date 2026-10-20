// ============================================================================
// 2026-10-16 · 菜单栏实现(设计理由见 MenuBuilder.h)
//
// ★★ 关键设计:connect 用【字符串槽名】而不是成员函数指针
//
//   看着像是退步(字符串 connect 比成员函数指针弱类型、易拼错),
//   但换来的是【可测试性】:
//     测试侧可以用一个"同名槽的假接收者"跑同一份 buildApplicationMenu,
//     逐个触发菜单项,验证"点'打开工程'真的落到 onOpenClicked"。
//
//   用成员函数指针做不到这件事 —— 因为槽属于 hmi 类,而 hmi 依赖
//   uic 生成的 ui_*.h,测试链的 hmi_core 里根本没有它,连编译都过不了。
//
//   ★ 字符串 connect 拼错槽名的后果是【运行时静默失败】
//     (connect 返回 false,不报错,菜单项点了没反应)。
//     编译期帮不上忙 —— 两边都用字符串之后,moc 之外没有任何检查。
//
//   ★★ 所以本文件把每一次 connect 的失败【收集起来返回】,而不是自己吞掉。
//     为什么光在测试里断言还不够:测试用的是假接收者,槽名对不对由测试自己保证;
//     真正跑起来时接收者是 hmi,而 hmi 到底有没有 onOpenClicked 这个槽,
//     只有 hmi 自己的元方法表知道 —— 测试链不到那个类。
//     把失败列表返回给 hmi,让生产代码自己喊出来,
//     一次"槽名对不上"就从"用户点了没反应"变成"启动日志里一行黄字"。
//
// ★★ 2026-10-20(P1-a):菜单项现在也带权限了。
//   以前这 17 项一个都没绑 —— P0 只给主画面的按钮绑了。
//   主画面按钮被裁掉之后,权限整个失效:界面上"编辑器"灰着,
//   菜单里"画面 → 编辑器"却能点开,而 EditorMainWindow 内部零权限检查。
//   两个入口门禁不一致,比两个入口都没门更危险 —— 前者让人以为有门。
//   逐项依据写在 buildApplicationMenu 里那段权限表上。
//
//   ★ 为什么"表态"做成编译期强制:addItem 的 perm 参数没有默认值。
//     P0 的事故形状正是"每加一个按钮只写 tooltip,没人顺手补绑定",
//     漏一次不报错、测试也绿。把表态做成必填参数,漏绑就编不过。
// ============================================================================

#include "ui/MenuBuilder.h"

#include "auth/Permission.h"
#include "auth/UIPermission.h"

#include <QAction>
#include <QKeySequence>
#include <QMenu>
#include <QMenuBar>
#include <QObject>

namespace qwhmi::ui {

namespace {

// 记录一处没连上的菜单项。
//   ★ 菜单文字原样带出去(含 & 助记符)—— 日志里 "打开工程(&O)..."
//     比剥干净的 "打开工程..." 更好定位:源文件里搜 "&O" 就搜得到。
void reportFailure(QStringList* fail, const QMenu* menu,
                   const QString& text, const char* slot) {
    if (!fail) return;
    fail->append(QStringLiteral("菜单「%1」里的「%2」没能连到槽 %3")
                     .arg(menu ? menu->title() : QStringLiteral("?"),
                          text, QString::fromLatin1(slot)));
}

// 加一个菜单项并 connect,顺带按权限置灰。
//
// ★★ perm 为什么用 std::optional<Permission> 而不是"不传就是不管":
//   不传 = 无权限门槛。写成一个必填参数(无默认值),于是
//   【每加一个菜单项都必须当场表态"绑什么权限 / 刻意不绑"】——
//   忘了写,编译不过。这正是 P0 事故的形状:主画面每加一个按钮都只写了
//   tooltip,没人顺手补 bindButtonEnable,权限门控在一个个加功能的过程中被漏光。
//   把"表态"做成编译期强制,漏绑就不可能悄悄发生。
//
//   ★ connect 失败时【仍然绑权限】:绑定是给用户看的状态(灰),
//     connect 是给用户点的通路(槽)。两件事各自独立,
//     槽名写错时菜单项本来就点不动,置不置灰都不影响那个 bug 的可见性
//     —— 而它已经被 failures 列表喊出来了。
//
//   ★★ 发射端也必须写成 SIGNAL(triggered()),不能用 &QAction::triggered。
//      成员函数指针和字符串槽【不能混用】 —— 混用连编译都过不去,
//      而这个编译错误恰好帮了我们:改成两边都用字符串之后,
//      编译期不再校验槽名,拼错的全部转成运行期静默失败,
//      所以才必须有下面这套"接不住就喊出来"的机制。
QAction* addItem(QMenu* menu, const QString& text,
                 QObject* receiver, const char* slot,
                 std::optional<qwhmi::auth::Permission> perm,
                 QStringList* fail) {
    QAction* act = menu->addAction(text);
    if (!act) { reportFailure(fail, menu, text, slot); return nullptr; }
    if (perm) qwhmi::auth::bindActionEnable(act, *perm);
    if (!QObject::connect(act, SIGNAL(triggered()), receiver, slot)) {
        reportFailure(fail, menu, text, slot);
        return act;   // ★ 返回 act 而不是 nullptr —— 它确实建出来了,还给绑了权限
    }
    return act;
}

// 加一个带快捷键的菜单项
QAction* addItem(QMenu* menu, const QString& text, const QKeySequence& keys,
                 QObject* receiver, const char* slot,
                 std::optional<qwhmi::auth::Permission> perm,
                 QStringList* fail) {
    QAction* act = addItem(menu, text, receiver, slot, perm, fail);
    if (act) act->setShortcut(keys);
    return act;
}

} // namespace

QStringList buildApplicationMenu(QMenuBar* bar, QObject* receiver) {
    QStringList failures;
    if (!bar) {
        failures.append(QStringLiteral("菜单栏为空,整个菜单没有搭建(hmi.ui 里的 QMenuBar 被删了?)"));
        return failures;
    }

    // ★ 先清空 —— 让本函数【可重复调用】。
    //   构造函数里只调一次,但测试会反复调用;不清就会出现两层"工程"菜单。
    bar->clear();

    // ══════════════════════════════════════════════════════════════════
    // ★★ P1-a(2026-10-20)权限表 —— 每一项都必须【在这里表态】
    //
    //   以前这 17 项一个权限都没绑。P0 只给主画面按钮绑了,于是裁掉按钮之后
    //   权限整个失效:界面上灰着的"编辑器"让人以为Viewer碰不了,
    //   菜单里"画面 → 编辑器"却能点开 —— 而编辑器内部【零权限检查】。
    //   两个入口门禁不一致,比两个入口都没门更危险。
    //
    //   【表态为什么做成编译期强制】addItem 的 perm 参数没有默认值,
    //   加一项就必须写。P0 的形状正是"每加一个只写 tooltip,没人顺手补绑定",
    //   漏绑一次不报错、测试也绿,权限在一个个加功能的过程中被悄悄漏光。
    //
    //   【判据只有一条:这个动作会不会改变现场】
    //     会改 → EditProject      不会改 → 不绑(任何角色都要能看/能连)
    //   每一条下面写了具体依据,不是照抄同类。
    // ══════════════════════════════════════════════════════════════════
    using qwhmi::auth::Permission;

    // ---- 工程 ----
    //   【为什么没有"新建工程"】程序现在没这个功能。
    //   不写灰的占位项 —— 灰项会让人反复点,不如不出现。
    //   【为什么末尾也没有留一个分隔符】留一个"给以后的新建工程用"的空分隔符
    //   是给自己看的,QMenu 会自动隐藏末尾的分隔符 —— 界面上根本看不见。
    //   看不见的东西留着,只会在将来有人以为"这儿本来就该有个功能"时被当成理由。
    QMenu* mProject = bar->addMenu(QStringLiteral("工程(&P)"));
    // 打开 → EditProject:打开工程会把运行中的整个配置换成磁盘上那份,
    //   等价于改现场配置。P1-b 之前主画面的 openButton 绑的也是它。
    addItem(mProject, QStringLiteral("打开工程(&O)..."), QKeySequence::Open,
            receiver, SLOT(onOpenClicked()), Permission::EditProject, &failures);
    addItem(mProject, QStringLiteral("保存工程(&S)..."), QKeySequence::Save,
            receiver, SLOT(onSaveClicked()), Permission::EditProject, &failures);

    // ---- 运行(放第一个:现场用得最多)----
    QMenu* mRun = bar->addMenu(QStringLiteral("运行(&R)"));
    // 启动/停止/复位 → WriteVariable:与主画面那三个按钮同一个权限
    //   (hmi.cpp 里 bindButtonEnable(startButton/stopButton/resetButton, ...))。
    //   操作员要能按,所以不是 EditProject。
    addItem(mRun, QStringLiteral("启动(&S)"),
            receiver, SLOT(onStartClicked()), Permission::WriteVariable, &failures);
    addItem(mRun, QStringLiteral("停止(&T)"),
            receiver, SLOT(onStopClicked()), Permission::WriteVariable, &failures);
    addItem(mRun, QStringLiteral("复位(&R)"),
            receiver, SLOT(onResetClicked()), Permission::WriteVariable, &failures);
    mRun->addSeparator();
    // 连接/断开 → 【刻意不绑】:任何角色都要能连上看现场。
    //   绑了就等于把"看现场"这个最基本的动作禁掉了。
    //   同 hmi.cpp 里 connectButton/disconnectButton 不绑的理由。
    addItem(mRun, QStringLiteral("连接 PLC(&C)"),
            receiver, SLOT(onConnectClicked()), std::nullopt, &failures);
    addItem(mRun, QStringLiteral("断开(&D)"),
            receiver, SLOT(onDisconnectClicked()), std::nullopt, &failures);

    // ---- PLC(2b 阶段 6)----
    //   ★ 放独立顶级菜单,不塞进"运行"或"工程"。理由:
    //   它既不是"运行控制"(启动/停止那种一次性动作),
    //   也不是"文件操作"(打开/保存工程)。
    //   它是【改现场设备的配置】—— 塞进"运行"会让人以为点它就能开始通讯,
    //   塞进"工程"会让人以为它只写文件(而它一半的职责是立即重连)。
    //   混进任何一个都是拿现成的分类掩盖真实的差别。
    //   【为什么不给快捷键】现场改 PLC 配置是低频动作,给快捷键等于
    //   占掉一个用户更常用的键位;而且它有 EditProject 权限门槛,
    //   按错一次弹一个"权限不足"框很烦。
    QMenu* mPlc = bar->addMenu(QStringLiteral("PLC(&L)"));
    // PLC 配置 → EditProject:改的是现场设备的地址/协议。
    //   PlcConfigDialog.cpp:757 内部 exec() 自查同一个权限,这里绑一致。
    addItem(mPlc, QStringLiteral("PLC 配置(&C)..."),
            receiver, SLOT(onPlcConfigClicked()), Permission::EditProject, &failures);

    // ---- 画面 ----
    QMenu* mScreen = bar->addMenu(QStringLiteral("画面(&V)"));
    // 编辑器 → EditProject,而且这条是【唯一的门】:
    //   EditorMainWindow 内部零权限检查,它直接 project_->saveToFile()。
    //   P1-b 之前唯一的门是主画面那个 editorBtn 按钮;按钮被裁掉之后,
    //   门就只剩这个菜单项了 —— 漏绑它 = 权限在主画面整个消失。
    //   (tst_MenuBar 有一条 editorItem_isTheOnlyGateOnProjectWriting 钉住这件事)
    addItem(mScreen, QStringLiteral("编辑器(&E)..."),
            receiver, SLOT(onEditorClicked()), Permission::EditProject, &failures);
    // 运行时画面/演示画布 → 【刻意不绑】:只读,不写任何东西。
    //   绑了就把"看现场"禁掉了 —— 同 P0 里那两个按钮的处置。
    addItem(mScreen, QStringLiteral("运行时画面(&R)..."),
            receiver, SLOT(onRuntimeCanvasClicked()), std::nullopt, &failures);
    addItem(mScreen, QStringLiteral("演示画布(&D)..."),
            receiver, SLOT(onDemoCanvasClicked()), std::nullopt, &failures);

    // ---- 报警 ----
    QMenu* mAlarm = bar->addMenu(QStringLiteral("报警(&A)"));
    // 报警配置 → EditProject:AlarmConfigDialog.cpp:1353 自查同一个权限。
    //   报警阈值/等级/屏蔽是现场设备的一部分,不是个人偏好。
    addItem(mAlarm, QStringLiteral("报警配置(&C)..."),
            receiver, SLOT(onAlarmConfigClicked()), Permission::EditProject, &failures);
    // 报警声音 → EditProject:AlarmSoundDialog.cpp:281 同上。
    addItem(mAlarm, QStringLiteral("报警声音(&S)..."),
            receiver, SLOT(onAlarmSoundSettings()), Permission::EditProject, &failures);

    // ---- 数据 ----
    QMenu* mData = bar->addMenu(QStringLiteral("数据(&D)"));
    // ★★ 2026-10-19:变量配置放在"导出变量表"【正上面】,不是另起一个菜单。
    //   为什么挨着放:这一对是同一个对象的两个方向 ——
    //   "导出变量表"是把变量表【拿出来看】,"变量配置"是把它【改掉】。
    //   用户找"变量在哪儿"时,这两个是最可能的目标,分开放等于让人找两遍。
    // 变量配置 → EditProject:VariableConfigDialog.cpp:706 自查同一个权限。
    //   变量表就是工程文件本体的一部分,改它等于改工程。
    addItem(mData, QStringLiteral("变量配置(&C)..."),
            receiver, SLOT(onVariableConfigClicked()), Permission::EditProject, &failures);
    // ★ 导出两项【刻意不绑】—— 这一条我犹豫过,如实写清取舍:
    //   Permission::ExportReport 确实存在,而且 Admin/Engineer 有、Operator/Viewer 没有。
    //   不绑的理由:导出【不改工程】,它只读一份已经在内存里的表,写一个 CSV 到
    //     用户自己选的位置。主画面那两个导出按钮(P0 时)也是不绑的 ——
    //     绑了菜单不绑按钮,两边又不一致了。
    //   哪天真要卡导出,改这一行就行(Permission.cpp 的表也已经有这个权限了)。
    addItem(mData, QStringLiteral("导出变量表(&V)..."),
            receiver, SLOT(onExportVariablesClicked()), std::nullopt, &failures);
    addItem(mData, QStringLiteral("导出报警表(&A)..."),
            receiver, SLOT(onExportAlarmsClicked()), std::nullopt, &failures);

    // ---- 帮助 ----
    // ★ 放的不是装饰性"关于",而是【程序当前的能力边界】。
    //   理由:用户在菜单里点了个没反应的项,最该立刻知道的是"这功能到底有没有"。
    //   藏起来只会被反复试;写出来还省得每次来问。
    QMenu* mHelp = bar->addMenu(QStringLiteral("帮助(&H)"));
    // 帮助 → 【刻意不绑】:任何人都有权知道这程序能干什么。
    //   把"你能看说明书"也按角色卡掉,是在制造而不是在防。
    addItem(mHelp, QStringLiteral("当前支持的功能(&F)"),
            receiver, SLOT(onShowCapabilities()), std::nullopt, &failures);

    return failures;
}

} // namespace qwhmi::ui
