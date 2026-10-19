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
// ============================================================================

#include "ui/MenuBuilder.h"

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

// 加一个菜单项并 connect。返回是否 connect 成功。
//   ★ 返回值必须一路传出去 —— connect 失败是静默的,
//     而"菜单项点了没反应"正是这类 bug 最典型的表现。
//
//   ★★ 发射端也必须写成 SIGNAL(triggered()),不能用 &QAction::triggered。
//      成员函数指针和字符串槽【不能混用】 —— 混用连编译都过不去,
//      而这个编译错误恰好帮了我们:改成两边都用字符串之后,
//      编译期不再校验槽名,拼错的全部转成运行期静默失败,
//      所以才必须有下面这套"接不住就喊出来"的机制。
bool addItem(QMenu* menu, const QString& text,
             QObject* receiver, const char* slot, QStringList* fail) {
    QAction* act = menu->addAction(text);
    if (!act) { reportFailure(fail, menu, text, slot); return false; }
    if (!QObject::connect(act, SIGNAL(triggered()), receiver, slot)) {
        reportFailure(fail, menu, text, slot);
        return false;
    }
    return true;
}

// 加一个带快捷键的菜单项
bool addItem(QMenu* menu, const QString& text, const QKeySequence& keys,
             QObject* receiver, const char* slot, QStringList* fail) {
    QAction* act = menu->addAction(text);
    if (!act) { reportFailure(fail, menu, text, slot); return false; }
    act->setShortcut(keys);
    if (!QObject::connect(act, SIGNAL(triggered()), receiver, slot)) {
        reportFailure(fail, menu, text, slot);
        return false;
    }
    return true;
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

    // ---- 工程 ----
    //   【为什么没有"新建工程"】程序现在没这个功能。
    //   不写灰的占位项 —— 灰项会让人反复点,不如不出现。
    //   【为什么末尾也没有留一个分隔符】留一个"给以后的新建工程用"的空分隔符
    //   是给自己看的,QMenu 会自动隐藏末尾的分隔符 —— 界面上根本看不见。
    //   看不见的东西留着,只会在将来有人以为"这儿本来就该有个功能"时被当成理由。
    QMenu* mProject = bar->addMenu(QStringLiteral("工程(&P)"));
    addItem(mProject, QStringLiteral("打开工程(&O)..."),
            QKeySequence::Open, receiver, SLOT(onOpenClicked()), &failures);
    addItem(mProject, QStringLiteral("保存工程(&S)..."),
            QKeySequence::Save, receiver, SLOT(onSaveClicked()), &failures);

    // ---- 运行(放第一个:现场用得最多)----
    QMenu* mRun = bar->addMenu(QStringLiteral("运行(&R)"));
    addItem(mRun, QStringLiteral("启动(&S)"),
            receiver, SLOT(onStartClicked()), &failures);
    addItem(mRun, QStringLiteral("停止(&T)"),
            receiver, SLOT(onStopClicked()), &failures);
    addItem(mRun, QStringLiteral("复位(&R)"),
            receiver, SLOT(onResetClicked()), &failures);
    mRun->addSeparator();
    addItem(mRun, QStringLiteral("连接 PLC(&C)"),
            receiver, SLOT(onConnectClicked()), &failures);
    addItem(mRun, QStringLiteral("断开(&D)"),
            receiver, SLOT(onDisconnectClicked()), &failures);

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
    addItem(mPlc, QStringLiteral("PLC 配置(&C)..."),
            receiver, SLOT(onPlcConfigClicked()), &failures);

    // ---- 画面 ----
    QMenu* mScreen = bar->addMenu(QStringLiteral("画面(&V)"));
    addItem(mScreen, QStringLiteral("编辑器(&E)..."),
            receiver, SLOT(onEditorClicked()), &failures);
    addItem(mScreen, QStringLiteral("运行时画面(&R)..."),
            receiver, SLOT(onRuntimeCanvasClicked()), &failures);
    addItem(mScreen, QStringLiteral("演示画布(&D)..."),
            receiver, SLOT(onDemoCanvasClicked()), &failures);

    // ---- 报警 ----
    QMenu* mAlarm = bar->addMenu(QStringLiteral("报警(&A)"));
    addItem(mAlarm, QStringLiteral("报警配置(&C)..."),
            receiver, SLOT(onAlarmConfigClicked()), &failures);
    addItem(mAlarm, QStringLiteral("报警声音(&S)..."),
            receiver, SLOT(onAlarmSoundSettings()), &failures);

    // ---- 数据 ----
    QMenu* mData = bar->addMenu(QStringLiteral("数据(&D)"));
    addItem(mData, QStringLiteral("导出变量表(&V)..."),
            receiver, SLOT(onExportVariablesClicked()), &failures);
    addItem(mData, QStringLiteral("导出报警表(&A)..."),
            receiver, SLOT(onExportAlarmsClicked()), &failures);

    // ---- 帮助 ----
    // ★ 放的不是装饰性"关于",而是【程序当前的能力边界】。
    //   理由:用户在菜单里点了个没反应的项,最该立刻知道的是"这功能到底有没有"。
    //   藏起来只会被反复试;写出来还省得每次来问。
    QMenu* mHelp = bar->addMenu(QStringLiteral("帮助(&H)"));
    addItem(mHelp, QStringLiteral("当前支持的功能(&F)"),
            receiver, SLOT(onShowCapabilities()), &failures);

    return failures;
}

} // namespace qwhmi::ui
