// ============================================================================
// tst_Theme —— 深色工业风样式表:它到底有没有被编译进去、有没有被加载
//
// 【这个测试在防什么具体事故】
//   qss 是【静默失败】的东西。样式表路径写错、qrc 没登记、忘了重新 RCC ——
//   这三种情况的表现【完全一样】:程序照跑、编译照绿、测试照全绿,
//   而界面退回 Qt 默认灰白皮。用户看到的是"我改了主题但没反应",
//   查起来却要翻 qrc、翻 CMakeLists、翻构建产物。
//   ★ 这跟 2026-10-20 那次菜单事故是同一个形状:东西看着配好了,
//     但真正让它生效的那根线没接上,而没有任何一层会报错。
//
// 【为什么这个测试"必须存在"而不是靠看图】
//   唯一能证明"主题生效了"的方法是启动程序看界面,那是人做的动作,
//   下次谁改了 qrc 就没人记得做。而这里能把那条线【钉住】:
//   qrc 里有这个文件 → 资源里能读出来 → 内容非空且含关键选择器。
//
// 【覆盖的边界 —— 说清楚免得误以为它包打天下】
//   ✓ qss 进了 qrc、RCC 编进去了、内容读得到、关键规则都在
//   ✓ 配色对比度够不够(状态徽章那 4 档)
//   ✓ 徽章字号没被"好心搬进 qss"(搬进去会静默失效)
//   ✗ 界面【好不好看】—— 那是审美,测试测不了也不该测
//   ✗ 每一条 qss 规则是否被 Qt 正确解析 —— Qt 对无法识别的属性
//     【静默忽略】,所以"没报错"不等于"生效了"。
//     本测试只能保证"该在的东西在",不能保证"Qt 一定照做了"。
//     要确认最终效果,仍然得人眼看一眼。
//
// 【负向对照 —— 实跑四轮,下面是真实结果,不是"应该会红"】
//
//   ① CMakeLists 去掉 CMAKE_AUTORCC ON
//      预期:initTestCase 红(读不到资源)
//      实际:【红】—— tst_Theme 读不到 :/hmi/theme/dark_industrial.qss。
//      ★ 这轮不是我编的对照,是我自己真踩的坑:qrc 列在源文件里
//        而 AUTORCC 没开时,CMake【静默忽略】它 —— 配置日志一行警告都没有。
//        根因是 tests/CMakeLists.txt 有 CMAKE_AUTOMOC ON 却漏了
//        CMAKE_AUTORCC ON(主工程第 8 行是有的)。
//
//   ② 把 qrc 里的文件名改成一个不存在的
//      预期:initTestCase 红
//      实际:【构建就断了,根本没到测试】
//        ninja: error: '.../theme/does_not_exist.qss', needed by
//        'tst_Theme_autogen/.../qrc_hmi.cpp', missing and no known rule to make it
//      ★ 与预期不同,如实记下来:RCC 对"文件不存在"是【硬失败】,比测试红更早。
//        所以 initTestCase 那条断言防的不是这一种 ——
//        它防的是"编进去了但资源路径写错"和"忘了重新 RCC"这两种,
//        那两种不会断构建,只会安静地读不到。
//
//   ③ 把 qss 里的 QPushButton / QPushButton:hover / [primary] 三段删掉
//      预期:allCriticalSelectorsArePresent 红
//      实际:【正是那条红】—— 7 passed, 1 failed,失败点 tst_Theme.cpp:159
//        "样式表少了这些选择器,对应界面会退回系统默认",其余 5 条全绿。
//        ★ 这轮顺带撞出另一个同形状的坑,一并记在这里:
//          做对照时我用 Copy-Item 还原文件,而 Copy-Item 会【保留备份的旧时间戳】,
//          于是下一轮 ninja 报 "no work to do"、跑的是上上轮的 exe,
//          我差点拿旧 exe 的绿灯当结果。还原文件后必须重新 touch,
//          或者干脆先删掉 exe 再构建 —— 这是本仓库第 5 次栽在"跑的是旧二进制"上。
//
//   ④ 把 statusBadgeStyle() 的红档字色改回 #ffffff(初版就是这么写的)
//      预期:statusBadgeColorsAreLegibleOnDarkBackground 红
//      实际:【正是那条红】—— 3.35:1 < 4.5。灰档白字同样不过(4.31:1)。
//      ★ 这轮也是真踩的:不是编出来给人看的对照,是初版配色当场被拦下。
//        修法不是"把阈值调低迁就配色",而是四档字色统一成近黑 #0d1117,
//        顺带让灰档从 #6e7b8b 提到 #7d8b9c —— 改的是配色,不是尺子。
//
// 关联:theme/dark_industrial.qss / hmi.qrc / hmi.cpp(statusBadgeStyle) / main.cpp(加载处)
// ============================================================================

#include <QtTest>
#include <QFile>
#include <QString>
#include <QColor>
#include <cmath>

namespace {

// 样式表在资源里的路径 —— 和 main.cpp 里那个字符串必须一致。
//   写死而不是从 main.cpp 抄:抄一份就变成"测副本",
//   main.cpp 改了这个测试还照绿,而那正是要防的。
constexpr const char* kThemeResPath = ":/hmi/theme/dark_industrial.qss";

// 缺了就等于界面塌了一半 —— 每个都有它非在不可的理由。
struct RequiredRule {
    const char* selector;
    const char* why;
};

const RequiredRule kRequiredRules[] = {
    {"QPushButton",      "主画面 11 个按钮,没有一个统一样式就没法看"},
    {"QPushButton:hover","只有默认态的话,用户不知道鼠标指到哪个了"},
    {"QLineEdit",        "8 个输入框(连 PLC 那一片)全是系统默认白底"},
    {"QTableWidget",     "主画面 2 张表 + 5 个对话框里的表"},
    {"QHeaderView::section", "深色表格里表头不做对比,整张表会糊成一块"},
    {"QMenuBar",         "7 个顶级菜单,是这个界面唯一的导航"},
    {"QMenu::item",      "菜单项本身;漏了会退回系统灰白"},
    {"QComboBox",        "3 个下拉框(协议选择等)"},
    {"QTabWidget::pane", "主画面有 QTabWidget"},
    {"QStatusBar",       "状态栏是操作员看运行结果的常驻位置"},
    {"QGroupBox",        "对话框里大量用分组框"},
    {"QMessageBox",      "权限不足/删除确认 —— 操作员最常看到的弹框"},
    {"QScrollBar",       "深底上默认滚动条和内容糊在一起"},
};

} // namespace

class tst_Theme : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();

    // ★ qss 必须真的编进二进制 —— 这一条是本文件的主体。
    //   路径错 / qrc 没登记 / 没重新 RCC,都会在这里红。
    void themeIsCompiledIntoTheBinary();

    // ★ 内容不能是空的,也不能是"文件在但内容被清空"。
    //   这两种情况上面那条测不出来(读到了就是读到了)。
    void themeIsNotEmpty();

    // ★ 关键选择器一个都不能少 —— 每个少一条,界面就塌一块。
    void allCriticalSelectorsArePresent();

    // ★ 主界面和对话框共用一张表,所以字号必须统一在样式表里,
    //   而不是散在各处 —— 散了就迟早有两处不一样。
    void fontSizeIsDefinedOnceInTheSheet();

    // ★ 状态徽章的字号必须【留在 hmi.cpp 里,不能搬进 qss】。
    //   这条看着像在管一件没发生的事,其实是在钉一条容易"好心办坏事"的边界。
    void badgeFontSizesStayInCodeNotInTheSheet();

    // 状态徽章那 4 档的配色必须够暗/够亮 —— 深色主题下
    //   "能看见"和"看得清"是两件事,这里测的是后者。
    void statusBadgeColorsAreLegibleOnDarkBackground();

    // ★ 加载点的位置。它同时被两个要求夹住,动一下就会坏一边:
    //     必须在 logging::applyConfig() 之后 —— 否则那句
    //     "样式表加载失败"永远进不了日志文件,而这恰恰是最需要留痕的告警;
    //     又必须在任何控件创建之前 —— 否则界面会分裂成两套配色。
    void loadPointSitsAfterLoggingAndBeforeAnyWidget();

private:
    // initTestCase 读一次,后面 4 条都用它。
    //   【为什么存成成员而不是每条各读一次】
    //   资源是【只读且一次性的】,反复读不会给出不同结果 ——
    //   但更重要的是:如果路径写错,应该【只有 initTestCase 报一次错】,
    //   而不是 5 条用例各报一次同一个错,把人淹在里面。
    QString m_sheet;
};

void tst_Theme::initTestCase() {
    QFile f(QString::fromLatin1(kThemeResPath));
    QVERIFY2(f.open(QIODevice::ReadOnly | QIODevice::Text),
             qPrintable(QStringLiteral(
                 "读不到样式表资源:%1\n"
                 "  → 检查 hmi.qrc 里有没有登记 theme/dark_industrial.qss\n"
                 "  → 检查 CMakeLists.txt 的 RESOURCES 里有没有 hmi.qrc\n"
                 "  → 改完 qrc 记得重新构建(RCC 只在 qrc 变化时才重跑)")
                 .arg(QLatin1String(kThemeResPath))));
    m_sheet = QString::fromUtf8(f.readAll());
}

void tst_Theme::themeIsCompiledIntoTheBinary() {
    // initTestCase 里已经验过"能读到",这里补一条更有用的断言:
    //   读到的是不是【我们写的那一份】,而不是 Qt 自带的某个 qss。
    //   判据是文件里那行独有的标记注释。
    QVERIFY2(m_sheet.contains(QStringLiteral("dark_industrial")),
             "读到的不是本项目那张样式表(内容里找不到 dark_industrial 标记)");
}

void tst_Theme::themeIsNotEmpty() {
    QVERIFY2(!m_sheet.trimmed().isEmpty(),
             "样式表在二进制里,但内容是空的 —— "
             "多半是文件被清空后没有重新构建");
    // 一张正常的 qss 至少几百字节。低于这个数说明被截断了
    // (编码问题 / 构建时文件正在被编辑器写)。
    QVERIFY2(m_sheet.size() > 500,
             qPrintable(QStringLiteral("样式表只有 %1 字节,像是被截断了")
                 .arg(m_sheet.size())));
}

void tst_Theme::allCriticalSelectorsArePresent() {
    QStringList missing;
    for (const RequiredRule& r : kRequiredRules) {
        if (!m_sheet.contains(QLatin1String(r.selector))) {
            missing << QStringLiteral("%1  (%2)")
                          .arg(QLatin1String(r.selector), QLatin1String(r.why));
        }
    }
    QVERIFY2(missing.isEmpty(),
             qPrintable(QStringLiteral("样式表少了这些选择器,对应界面会退回系统默认:\n  %1")
                 .arg(missing.join(QStringLiteral("\n  ")))));
}

void tst_Theme::fontSizeIsDefinedOnceInTheSheet() {
    // 字号只该在 QWidget 那一条里定一次。
    //   出现多次 = 有人在某个控件上单独又写了一遍,
    //   而那两处迟早会不一样(改了 QWidget 那条,另一处不跟)。
    const int n = m_sheet.count(QStringLiteral("font-size: 11pt"));
    QCOMPARE(n, 1);
}

void tst_Theme::badgeFontSizesStayInCodeNotInTheSheet() {
    // 【★ 这条断言初版写错了地方】
    //   我原先在这里断言"样式表里必须有 font-size: 18px / 13px",
    //   结果红了 —— 因为徽章字号【本来就不该在 qss 里】。
    //
    //   状态徽章是 hmi.cpp 每次 setStyleSheet 生成的 inline 样式表,
    //   而 Qt 的规则是:控件上一旦有 inline 样式表,qss 对它【完全失效】。
    //   所以就算把这两行搬进 qss,徽章字号也【一个字节都不会变】——
    //   搬过去只会给人"已经统一到主题文件了"的错觉,
    //   而实际效果是徽章字号悄悄退回继承值 11pt。
    //
    //   徽章字号必须比正文大:状态是操作员最先看的东西。
    //   它跟正文一起变灰就等于没有状态。所以这里改成盯住【真实所在】。
    QVERIFY2(!m_sheet.contains(QStringLiteral("font-size: 18px")),
             "★ 徽章字号被搬进 qss 了 —— inline 样式表会让它彻底失效,"
             "状态块会静默缩回 11pt,而 qss 看着一切正常。"
             "要改徽章字号请改 hmi.cpp 的 statusBadgeStyle()");
    QVERIFY2(!m_sheet.contains(QStringLiteral("font-size: 13px")),
             "同上 —— 13px 是 statusBadgeStyle() 里 small=true 的值,"
             "搬进 qss 不会生效,只会让徽章字号失控");

    // 反过来确认这两个值确实还在【该在的地方】:hmi.cpp 的源码里。
    //   读源码文本是这个仓库已有的做法(tst_MenuBar 就是这么钉住 hmi.h 的),
    //   这里同理 —— 徽章不产生独立目标,只能这么钉。
    QFile cpp(QStringLiteral("%1/hmi.cpp").arg(QString::fromLatin1(HMI_SRC_DIR_STRING)));
    QVERIFY2(cpp.open(QIODevice::ReadOnly | QIODevice::Text),
             qPrintable(QStringLiteral("打不开 hmi.cpp(徽章字号的真实所在):%1")
                 .arg(cpp.errorString())));
    const QString code = QString::fromUtf8(cpp.readAll());
    QVERIFY2(code.contains(QStringLiteral("font-size: 18px")),
             "状态徽章的大字号没了 —— 主画面最重要的两个状态块会缩成一行小字");
    QVERIFY2(code.contains(QStringLiteral("font-size: 13px")),
             "对话框里徽章的小字号没了");
}

// ---------------------------------------------------------------------------
// WCAG 相对亮度与对比度 —— 和 src/alarm/AlarmRowPalette 的测试同一套算法。
//   ★ 抄算法不抄结论:那边钉的是"浅色报警行之间的对比",
//     这里钉的是"徽章文字在自己的底色上够不够读"。
//     同一套公式、不同的被测对象 —— 这不是重复。
// ---------------------------------------------------------------------------
namespace {

double relLum(const QColor& c) {
    auto f = [](int v) {
        const double x = v / 255.0;
        return x <= 0.03928 ? x / 12.92 : std::pow((x + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * f(c.red()) + 0.7152 * f(c.green()) + 0.0722 * f(c.blue());
}

double contrast(const QColor& a, const QColor& b) {
    const double la = relLum(a), lb = relLum(b);
    const double hi = qMax(la, lb), lo = qMin(la, lb);
    return (hi + 0.05) / (lo + 0.05);
}

// 与 hmi.cpp 的 statusBadgeStyle() 一一对应。
//   ★ 字色四档统一是 #0d1117,不是逐档挑的 —— 初版红/灰用白字,
//     实测只有 3.35:1 和 4.31:1,低于 4.5。改成亮底深字后四档分别是
//     7.45 / 5.45 / 7.50 / 5.65。灰档也因此从 #6e7b8b 提到 #7d8b9c
//     (暗灰配深字 4.39:1,差一点点)。改配色时这几个数字要一起重算。
struct Badge { const char* name; const char* bg; const char* fg; };

const Badge kBadges[] = {
    {"运行/已连接",  "#3fb950", "#0d1117"},
    {"连接中/复位中", "#d29922", "#0d1117"},
    {"故障/连接失败", "#f85149", "#0d1117"},
    {"已停止/已断开", "#7d8b9c", "#0d1117"},
};

} // namespace

void tst_Theme::statusBadgeColorsAreLegibleOnDarkBackground() {
    // WCAG AA 正文阈值 4.5:1。这里取 4.5 而不是 AlarmRowPalette 那种更严的
    //   标准 —— 徽章是大字号粗体(18px bold),WCAG 对此的阈值是 3:1,
    //   但现场屏反光强,留余量,所以仍然按 4.5 卡。
    for (const Badge& b : kBadges) {
        const QColor bg(QLatin1String(b.bg));
        const QColor fg(QLatin1String(b.fg));
        QVERIFY2(bg.isValid() && fg.isValid(),
                 qPrintable(QStringLiteral("%1 的色值不是合法颜色").arg(QLatin1String(b.name))));
        const double r = contrast(bg, fg);
        QVERIFY2(r >= 4.5,
                 qPrintable(QStringLiteral(
                     "%1 的文字/底色对比度只有 %2:1,低于 4.5 —— "
                     "现场强光下会看不清状态。底=%3 字=%4")
                     .arg(QLatin1String(b.name)).arg(r, 0, 'f', 2)
                     .arg(QLatin1String(b.bg), QLatin1String(b.fg))));
    }
}

void tst_Theme::loadPointSitsAfterLoggingAndBeforeAnyWidget() {
    QFile f(QStringLiteral("%1/main.cpp").arg(QString::fromLatin1(HMI_SRC_DIR_STRING)));
    QVERIFY2(f.open(QIODevice::ReadOnly | QIODevice::Text),
             qPrintable(QStringLiteral("打不开 main.cpp(加载点位置的真实在处):%1")
                 .arg(f.errorString())));
    const QString src = QString::fromUtf8(f.readAll());
    const QStringList lines = src.split(QLatin1Char('\n'));

    // 找加载点:打开那个资源的那一行。
    int iTheme = -1, iApplyConfig = -1;
    for (int i = 0; i < lines.size(); ++i) {
        if (iTheme < 0 && lines[i].contains(QStringLiteral(":/hmi/theme/dark_industrial.qss")))
            iTheme = i;
        if (iApplyConfig < 0 && lines[i].contains(QStringLiteral("logging::applyConfig")))
            iApplyConfig = i;
    }
    QVERIFY2(iTheme >= 0, "main.cpp 里找不到打开样式表资源的那一行 —— 加载点被删了?");
    QVERIFY2(iApplyConfig >= 0, "main.cpp 里找不到 logging::applyConfig");

    // ★ 前半条:必须在日志就绪之后。
    //   我第一版就栽在这 —— 加载功能是对的,但 qCWarning 只走 stderr,
    //   日志文件里一个字都没有,"主题没加载上"就此查无实据。
    QVERIFY2(iTheme > iApplyConfig,
             "★ 样式表加载点被挪到 logging::applyConfig 之前了 —— "
             "加载失败时那句告警进不了日志文件。"
             "主题没生效是不报错的静默故障,留不下痕迹就等于没发生");

    // ★ 后半条:加载点之前【不能有任何窗口被构造】。
    //
    //   方向别搞反:登录框、主窗口出现在加载点【之后】是正常的 ——
    //   那正是"主题已经套好、窗口才建出来"的正确顺序。
    //   出事的是反过来的:某个窗口先建好了,之后才 setStyleSheet,
    //   qss 追改不上已存在的控件,于是它和别的窗口分成两套配色。
    //
    //   (第一版我把方向写反了,于是 main.cpp 里合法的登录框被判成了违规 ——
    //    测试红了,但它红得有道理:我断言的东西不是我标题写的那件事。)
    int iQApp = -1;
    for (int i = 0; i < lines.size(); ++i) {
        if (iQApp < 0 && lines[i].contains(QStringLiteral("QApplication app(")))
            iQApp = i;
    }
    QVERIFY2(iQApp >= 0, "main.cpp 里找不到 QApplication 的构造");

    static const char* kWindowMarkers[] = {
        "loginDlg", "LoginDialog", "hmi window", "MainWindow", "RuntimeCanvas",
    };
    for (int i = iQApp; i < iTheme; ++i) {
        const QString trimmed = lines[i].trimmed();
        // 只认"真的在构造",不认注释里提到这些名字。
        if (trimmed.startsWith(QLatin1String("//")) || trimmed.startsWith(QLatin1String("*")))
            continue;
        for (const char* m : kWindowMarkers) {
            QVERIFY2(!trimmed.contains(QString::fromLatin1(m)),
                     qPrintable(QStringLiteral(
                         "样式表加载点(%1 行)之前,第 %2 行就构造了窗口 '%3' —— "
                         "qss 是构造时快照,追改不了已建好的控件,"
                         "那个窗口会留在旧配色上,界面分裂成两套")
                         .arg(iTheme + 1).arg(i + 1).arg(QLatin1String(m))));
        }
    }
}

QTEST_MAIN(tst_Theme)
#include "tst_Theme.moc"
