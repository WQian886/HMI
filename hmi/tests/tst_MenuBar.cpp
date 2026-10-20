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

QTEST_MAIN(tst_MenuBar)
#include "tst_MenuBar.moc"
