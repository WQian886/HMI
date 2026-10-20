// ============================================================================
// tst_CommTracePanel —— 调试区第三页「通讯监视」的【形状】
//
// 【这个测试在防什么】
//   「通讯监视」页是 2026-10-20 新加的第三页。数据层在
//   tst_CommTraceModel 里测了,本文件测的是【这一页在 .ui 里的形状】
//   和【hmi.cpp 里怎么接线】——
//   数据对了但页面长歪了(表格不固定高度、按钮只有 30px、页签没名字),
//   在现场看到的是"这页弄得挺随便",而不是"这个功能有问题"。
//
// 【为什么读 .ui / hmi.cpp 文本,不建出控件量】
//   同 tst_ModbusPanel / tst_MenuBar / tst_Theme 的老问题:
//   hmi_core 不编 hmi.cpp,链不到 ui_*.h,建不出主窗口那一排控件。
//   另一个更硬的理由:.ui 写错【属性】是静默的 ——
//   比如 minimumSize 少一层 <size>,uic 不报错,那一行被丢掉,
//   控件照建照显示,只是矮一截。读文本能把"写了但写歪了"变成"查得到"。
//
// 【三条决定,写下来就一定会被下一个人改坏】
//   ① 【表格固定 160px】(min == max)
//     只写 minimumSize 的话,表格的 sizeHint(约 192)会赢,
//     面板高度就随帧数变 —— 帧一多,整个调试区往下长,
//     底下的实时数据区被推走。用户会以为"程序卡了"。
//
//   ② 【底下一行也是 40px】
//     和前两页(手动发送 / Modbus)的 40px 同一节奏。
//     三页在同一个页签控件里,用户来回切,一眼就会比对。
//
//   ③ 【调试区高度在运行期算,不写死在 .ui】
//     页高取决于 qss 里的页签条高度和控件高度,写死就与主题脱节 ——
//     改了 qss 之后这里对不上,表现是面板多一截黑。
//     所以 .ui 里 debugTabs 只有 minimumSize,没有 maximumSize;
//     固定高度由 hmi.cpp 的 syncTabHeight 在切页时算。
//
// 【诚实的边界】
//   ✓ 能抓:第三页在不在 / 有没有标题 / 挂没挂在 debugTabs 里 /
//           表格高度钉没钉住 / 底下一行齐不齐 / 暂停按钮是不是可勾选 /
//           统计标签初始文案会不会骗人 / 有没有偷塞 inline 样式表 /
//           高度是不是被写死在 .ui 里 / 信号有没有接上 /
//           枚举属性是不是 uic 认的写法(见下面 noEnumPropertyUsesTheBrokenInlineForm)
//   ✗ 不能抓:"好不好看"。行着色还没做(见 hmi.cpp 里的说明),
//     配色那一半必须人眼看。
//
// 【负向对照 —— 五轮全部实跑,数字是跑出来的不是推出来的】
//   绿基线:15 passed / 0 failed
//   ① 删掉表格的 maximumSize(只留 minimumSize)
//        → 红 1:frameTable_isPinnedToOneFixedHeight
//          绿 14
//   ② 把调试区的固定高度写进 .ui(给 debugTabs 加 maximumSize 240)
//        → 红 1:debugTabsHeight_isNotHardcodedInUi
//          绿 14
//   ③ 把 hmi.cpp 里"切页重算高度"的 connect 删掉
//        → 红 1:debugTabsHeight_isNotHardcodedInUi
//          绿 14
//   ④ 把 hmi.cpp 里 ModbusMaster::frameTraced 的 connect 整块删掉
//        → 红 1:frameTraced_isHookedToTheModel
//          绿 14
//   ⑤ 把 selectionBehavior 改回 uic 不认的"类型 + 行尾值"写法
//        → 红 1:noEnumPropertyUsesTheBrokenInlineForm
//          绿 14
//
// 【这批为什么不用重编】本测试是在【运行期读磁盘上的 .ui / hmi.cpp 文本】,
//   所以改文件 → 跑 exe 就能出红,不用动 ninja。
//   代价是:它验的是"文件里写对了",不是"uic 编译出来对了" ——
//   .ui 写坏成 uic 不认的样子,这一层抓不到(那种由主程序编译抓)。
//   还原验证用的是 SHA256 比对,不是"看着像回去了"。
//
// 【★ 断言一律跳过 XML 注释】
//   uic 不读注释,断言也必须不读 ——
//   不然一条"说明某个写法为什么不能用"的注释,会把断言自己逮住。
//   见 stripXmlComments()。
//
// 关联:hmi.ui tabCommTrace / hmi.cpp buildCommTracePanel() + buildDebugPanel()
//      / ModbusMaster.h frameTraced 信号 / src/comm/CommTraceModel.h
// ============================================================================

#include <QtTest>
#include <QFile>
#include <QRegularExpression>
#include <QStringList>

// 去掉 C++ 注释再查字符串。
// ★ 为什么必须去:hmi.cpp 的 buildCommTracePanel() 注释里大段讲着
//   "行着色还没做""为什么不塞颜色"这些事,不去注释的话,
//   注释里提到的词会让断言变成永远绿 —— 而它看起来是在测东西的。
//   仓库里 tst_MenuBar / tst_UIPermission / tst_Theme / tst_ModbusPanel
//   用的是同一招,原因也一样:注释是给人读的,不该被当成代码证据。
static QString codeOnly(const QString& text) {
    QString out = text;
    out.remove(QRegularExpression(QStringLiteral("/\\*.*?\\*/"),
                                  QRegularExpression::DotMatchesEverythingOption));
    out.remove(QRegularExpression(QStringLiteral("//[^\n]*")));
    return out;
}

// ★ 去掉 XML 注释再查。
//   uic 【不读注释】,所以注释里写什么都不影响生成的代码 ——
//   断言也必须这样,否则:
//     ① 一条注释里贴了 .ui 片段(比如说明某个写法为什么不能用),
//        就会让"这个写法不能用"的断言自己把自己逮住;
//     ② 注释里出现 </widget> 会打乱按开闭标签数配平的页深度计算。
//   同 codeOnly() 对 C++ 注释做的事,理由一模一样:
//   注释是给人读的,不该被当成代码证据。
static QString stripXmlComments(const QString& xml) {
    QString out = xml;
    out.remove(QRegularExpression(QStringLiteral("<!--.*?-->"),
                                  QRegularExpression::DotMatchesEverythingOption));
    return out;
}

class tst_CommTracePanel : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();

    // ---- .ui 的形状 ----
    void thirdPage_existsAndHasTitle();
    void thirdPage_livesInsideDebugTabs();
    void frameTable_isPinnedToOneFixedHeight();
    void bottomRow_isSameHeightAsTheOtherRows();
    void pauseButton_isCheckable();
    void statsLabel_doesNotLieBeforeTheFirstFrame();
    void noInlineStyleSheetInThirdPage();
    void pageMargins_areZero();
    void noEnumPropertyUsesTheBrokenInlineForm();

    // ---- hmi.cpp 的接线 ----
    void buildCommTracePanel_hasNoQMessageBox();
    void buildCommTracePanel_wiresModelToView();
    void frameTraced_isHookedToTheModel();
    void debugTabsHeight_isNotHardcodedInUi();

private:
    QString m_ui;     // hmi.ui 全文
    QString m_code;   // hmi.cpp 全文(已去注释)

    // 某个控件在 .ui 里的那一段(从 name="X" 到下一个 <widget 或 </layout>)
    QString widgetXml(const QString& name) const;
    // 某个控件的祖先链(由外到内,不含它自己);每项带 "L:"布局 / "W:"控件 前缀
    QStringList enclosingChain(const QString& name) const;
    // 某一段 XML 里 <property name="minimumSize"> 的 height
    int minHeightIn(const QString& xml) const;
    // 某一段 XML 里 <property name="maximumSize"> 的 height
    int maxHeightIn(const QString& xml) const;
};

void tst_CommTracePanel::initTestCase() {
    QFile ui(QStringLiteral("%1/hmi.ui").arg(QString::fromLatin1(HMI_SRC_DIR_STRING)));
    QVERIFY2(ui.open(QIODevice::ReadOnly | QIODevice::Text),
             qPrintable(QStringLiteral("打不开 hmi.ui:%1").arg(ui.errorString())));
    m_ui = stripXmlComments(QString::fromUtf8(ui.readAll()));
    ui.close();

    QFile cpp(QStringLiteral("%1/hmi.cpp").arg(QString::fromLatin1(HMI_SRC_DIR_STRING)));
    QVERIFY2(cpp.open(QIODevice::ReadOnly | QIODevice::Text),
             qPrintable(QStringLiteral("打不开 hmi.cpp:%1").arg(cpp.errorString())));
    m_code = codeOnly(QString::fromUtf8(cpp.readAll()));
    cpp.close();

    // 地基:读到的不能是空的,否则下面所有"查不到"都会绿成一片。
    QVERIFY2(m_ui.size() > 5000,
             qPrintable(QStringLiteral("hmi.ui 读出来短得反常(%1 字节)—— "
                                       "路径错了,而下面的断言会空洞地绿")
                            .arg(m_ui.size())));
    QVERIFY2(m_code.size() > 5000,
             qPrintable(QStringLiteral("hmi.cpp 读出来短得反常(%1 字节)").arg(m_code.size())));
}

QString tst_CommTracePanel::widgetXml(const QString& name) const {
    const int at = m_ui.indexOf(QStringLiteral("name=\"%1\"").arg(name));
    if (at < 0) return QString();
    int nextAt = m_ui.indexOf(QStringLiteral("<widget"), at + 1);
    const int endAt = m_ui.indexOf(QStringLiteral("</layout>"), at);
    if (nextAt < 0 || (endAt >= 0 && endAt < nextAt)) nextAt = (endAt < 0) ? m_ui.size() : endAt;
    return m_ui.mid(at, nextAt - at);
}

QStringList tst_CommTracePanel::enclosingChain(const QString& name) const {
    // ★ 这段跟 tst_ModbusPanel 里那份是重复的 —— 故意的。
    //   仓库里每个测试文件都是【自给自足】的(没有共享 test helper 头):
    //   一个测试文件引一个共享头,就多一处"改共享头会连带改坏别的测试"的耦合,
    //   而这种测试的价值恰恰在于它自己站在那儿、谁也不依赖。
    //   两份重复代码换一个"改一处不会动到另一处"的独立性,划算。
    //
    //   ★ 返回的每一项带 L:/W: 前缀,区分布局和控件 ——
    //     问"这页挂在谁下面"时,祖先链里一定含 centralLayout(它在最外层),
    //     有意义的是"里面有没有 W:debugTabs"。
    static const QRegularExpression re(QStringLiteral(
        "<(layout|widget)\\s+class=\"\\w+\"\\s+name=\"(\\w+)\">|</(layout|widget)>"));
    QStringList stack;
    QRegularExpressionMatchIterator it = re.globalMatch(m_ui);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        if (!m.captured(3).isEmpty()) {            // 闭合标签
            if (!stack.isEmpty()) stack.removeLast();
            continue;
        }
        if (m.captured(2) == name) return stack;    // 返回的是【祖先】,不含自己
        stack << (m.captured(1) == QLatin1String("layout")
                      ? QStringLiteral("L:") + m.captured(2)
                      : QStringLiteral("W:") + m.captured(2));
    }
    return QStringList();
}

int tst_CommTracePanel::minHeightIn(const QString& xml) const {
    static const QRegularExpression re(QStringLiteral(
        "<property name=\"minimumSize\">\\s*<size>.*?<height>(\\d+)</height>"));
    const QRegularExpressionMatch m = re.match(xml);
    return m.hasMatch() ? m.captured(1).toInt() : -1;
}

int tst_CommTracePanel::maxHeightIn(const QString& xml) const {
    static const QRegularExpression re(QStringLiteral(
        "<property name=\"maximumSize\">\\s*<size>.*?<height>(\\d+)</height>"));
    const QRegularExpressionMatch m = re.match(xml);
    return m.hasMatch() ? m.captured(1).toInt() : -1;
}

// -----------------------------------------------------------------------
//  .ui 的形状

void tst_CommTracePanel::thirdPage_existsAndHasTitle() {
    QVERIFY2(m_ui.contains(QStringLiteral("<widget class=\"QWidget\" name=\"tabCommTrace\">")),
             "hmi.ui 里没有第三页 tabCommTrace —— 通讯帧监视器不见了。\n"
             "  它的价值在于:现场能拿到的只有「Modbus 错误:…」一个弹窗,几秒就没了。\n"
             "  而「是哪一帧错的」「错之前那几帧正常吗」「往返多久」从来没被留下来过。");

    // 页签上必须有名字 —— 页签上写"1""2""3"而没有名字,
    // 用户根本不知道哪一页是哪一页。
    QVERIFY2(m_ui.contains(QStringLiteral("<attribute name=\"title\"><string>通讯监视</string>")),
             "第三页没有标题「通讯监视」");

    // 三页都得在(手动发送 / Modbus / 通讯监视)
    static const QRegularExpression re(QStringLiteral(
        "<widget class=\"QWidget\" name=\"(tabManualSend|tabModbus|tabCommTrace)\">"));
    QStringList pages;
    QRegularExpressionMatchIterator it = re.globalMatch(m_ui);
    while (it.hasNext()) pages << it.next().captured(1);
    QCOMPARE(pages.size(), 3);
}

void tst_CommTracePanel::thirdPage_livesInsideDebugTabs() {
    const QStringList chain = enclosingChain(QStringLiteral("tabCommTrace"));
    QVERIFY2(!chain.isEmpty(),
             "hmi.ui 里找不到 tabCommTrace —— "
             "查不到不等于没问题,先确认名字没被改掉再往下看");
    QVERIFY2(chain.contains(QStringLiteral("W:debugTabs")),
             qPrintable(QStringLiteral(
                 "第三页不在 debugTabs 里(祖先链:%1)——\n"
                 "  页建好了却不在那个页签控件里,界面上哪儿都不显示。\n"
                 "  比\"叠在一起\"更难发现,因为界面看着\"干净\"。")
                 .arg(chain.join(QStringLiteral(" < ")))));

    // 它必须是 debugTabs 的【直属】孩子 —— 中间夹一层就说明归属又乱了。
    //   祖先链由外到内排,里面既有布局也有控件:
    //   最近的【控件】祖先才是"直属上级"，
    //   最近的那一项(centralLayout)是个布局,不是上级控件。
    QString nearestWidget;
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        if (it->startsWith(QLatin1String("W:"))) { nearestWidget = *it; break; }
    }
    QVERIFY2(nearestWidget == QLatin1String("W:debugTabs"),
             qPrintable(QStringLiteral(
                 "第三页最近的控件祖先是 %1,不是 debugTabs(祖先链:%2)—— "
                 "直属关系散了,以后谁在中间再插一层都不会有人发现")
                 .arg(nearestWidget, chain.join(QStringLiteral(" < ")))));
}

void tst_CommTracePanel::frameTable_isPinnedToOneFixedHeight() {
    const QString xml = widgetXml(QStringLiteral("commTraceView"));
    QVERIFY2(!xml.isEmpty(), "hmi.ui 里找不到 commTraceView(帧列表表格)");

    const int minH = minHeightIn(xml);
    const int maxH = maxHeightIn(xml);
    QCOMPARE(minH, 160);
    // ★ min == max,不是 min <= max。
    //   只写 minimumSize 的话,表格的 sizeHint(约 192)会赢,
    //   面板高度随帧数变 —— 帧一多,整个调试区往下长,底下的实时数据区被推走。
    QCOMPARE(maxH, 160);

    // 负向对照 ①(实跑):删掉表格的 maximumSize(只留 minimumSize)
    //   → 红 1:frameTable_isPinnedToOneFixedHeight
    //     绿 13
}

void tst_CommTracePanel::bottomRow_isSameHeightAsTheOtherRows() {
    // 底下一行:统计标签 + 暂停 + 清空。
    //   40px = 调试区既有的节奏(前两页的控件都是 40)。
    struct { const char* name; } items[] = {
        {"commTraceStatsLabel"},
        {"commTracePauseButton"},
        {"commTraceClearButton"},
    };
    QStringList bad;
    for (const auto& it : items) {
        const QString xml = widgetXml(QString::fromLatin1(it.name));
        if (xml.isEmpty()) { bad << QStringLiteral("%1:【在 .ui 里找不到】").arg(it.name); continue; }
        const int h = minHeightIn(xml);
        if (h != 40) {
            bad << QStringLiteral("%1:最小高度 %2,应为 40").arg(it.name).arg(h);
        }
    }
    QVERIFY2(bad.isEmpty(),
             qPrintable(QStringLiteral(
                 "第三页底下一行的高度不齐 ——\n  %1\n"
                 "  三页在同一个页签控件里,用户来回切一眼就会比对,\n"
                 "  其中一行矮了 6px 是当场能看出来的。")
                 .arg(bad.join(QStringLiteral("\n  ")))));
}

void tst_CommTracePanel::pauseButton_isCheckable() {
    const QString xml = widgetXml(QStringLiteral("commTracePauseButton"));
    QVERIFY2(!xml.isEmpty(), "hmi.ui 里找不到 commTracePauseButton");
    // ★ 必须 checkable —— 它是"暂停/继续"两态按钮。
    //   不是 checkable 的话勾选不了,toggled 信号不发生,
    //   采集停不下来 —— 而"停下来看清那几帧"正是这个按钮的全部意义。
    QVERIFY2(xml.contains(QStringLiteral("<property name=\"checkable\"><bool>true</bool>")),
             "暂停按钮没有 checkable —— 它勾不上,采集停不下来。\n"
             "  而这个按钮的全部意义就是:先停下来,再看清那几帧。");
    // 文字是"接下来点我会做什么"(暂停→继续),不是"我现在是什么状态"。
    QVERIFY2(xml.contains(QStringLiteral("<property name=\"text\"><string>暂停</string>")),
             "暂停按钮的初始文字不是「暂停」");
}

void tst_CommTracePanel::statsLabel_doesNotLieBeforeTheFirstFrame() {
    const QString xml = widgetXml(QStringLiteral("commTraceStatsLabel"));
    QVERIFY2(!xml.isEmpty(), "hmi.ui 里找不到 commTraceStatsLabel");

    // 初始文案必须说"尚无样本",不能写"平均 0ms" ——
    //   写 0 会被读成"实测 0ms",而真实含义是"还没测过"。
    QVERIFY2(xml.contains(QStringLiteral("尚无样本")),
             "统计标签的初始文案里没有「尚无样本」——\n"
             "  一帧都还没有的时候它必须说\"还没测过\",不能写 0\n"
             "  (0 会被读成\"实测 0ms\")。");
    QVERIFY2(xml.contains(QStringLiteral("发 0")),
             "统计标签的初始文案里没有帧数");
}

void tst_CommTracePanel::noInlineStyleSheetInThirdPage() {
    // 取【整页】:从页面那个 <widget> 开始,按开闭标签数深度,
    // 数到配平为止。第一版只截到第一个 </widget>(表格自己的那个),
    // 底下一行(标签/两个按钮)就落在检查范围之外了 —— 那正是最容易
    // 被人贴一行 color: 的地方。
    const QString opener = QStringLiteral("<widget class=\"QWidget\" name=\"tabCommTrace\">");
    int at = m_ui.indexOf(opener);
    QVERIFY2(at >= 0, "hmi.ui 里找不到 tabCommTrace 这一页");
    int depth = 0;
    int endAt = -1;
    int cursor = at;
    while (cursor >= 0 && cursor < m_ui.size()) {
        const int open = m_ui.indexOf(QStringLiteral("<widget"), cursor);
        const int close = m_ui.indexOf(QStringLiteral("</widget>"), cursor);
        if (close < 0) break;
        if (open >= 0 && open < close) { ++depth; cursor = open + 7; continue; }
        --depth;
        cursor = close + 9;
        if (depth == 0) { endAt = close; break; }
    }
    QVERIFY2(endAt > at, "第三页的 <widget> 开闭对不上 —— .ui 结构被改坏了");
    const QString page = m_ui.mid(at, endAt - at);

    QVERIFY2(!page.contains(QStringLiteral("name=\"styleSheet\"")),
             "第三页里又有 inline 样式表了 ——\n"
             "  ① 它会让 qss 对那个控件【完全失效】,改主题时它不跟着变\n"
             "     (两套样式并行,这是本仓库吃过亏的地方);\n"
             "  ② 上一次出过一次:modbusResultLabel 上那行 color: blue,\n"
             "     深底上对比度只有 1.60:1,而这张 qss 的文件头写着\"正文至少 7:1\"。\n"
             "  要改颜色请改 theme/dark_industrial.qss;字号同理。");
}

void tst_CommTracePanel::pageMargins_areZero() {
    // 页内边距必须 0 —— QVBoxLayout 默认 9px,
    //   9×2 会在 160 的表格高度里吃掉 18px,表格就只剩 142。
    const QString xml = widgetXml(QStringLiteral("tabCommTraceLayout"));
    QVERIFY2(!xml.isEmpty(), "hmi.ui 里找不到 tabCommTraceLayout");

    for (const char* margin : {"leftMargin", "topMargin", "rightMargin", "bottomMargin"}) {
        const QRegularExpression re(QStringLiteral(
            "<property name=\"%1\"><number>(\\d+)</number>").arg(QLatin1String(margin)));
        const QRegularExpressionMatch m = re.match(xml);
        QVERIFY2(m.hasMatch(),
                 qPrintable(QStringLiteral("tabCommTraceLayout 没写 %1 —— "
                     "QVBoxLayout 默认 9px,会在 160 的表格高度里吃掉一块")
                     .arg(QLatin1String(margin))));
        QCOMPARE(m.captured(1).toInt(), 0);
    }
}

// ★★ 这条是【2026-10-20 一次真实编译事故】留下的疤。
//   当时 .ui 里写的是:
//     <property name="selectionBehavior">
//      <enum>QAbstractItemView::SelectionBehavior</enum>SelectRows
//     </property>
//   uic 不认"类型 + 行尾值"这种写法 —— 它把【类型】当【值】照抄,生成:
//     setSelectionBehavior(QAbstractItemView::SelectionBehavior);
//   编译报 C2275("应为表达式,而不是类型")。
//   ★★ 而它是【静默的】:ninja 不会因为只改了 .ui 就重跑 uic,
//     所以这个错要等一次 -Clean 干净重建才现形 ——
//     也就是说"改完 .ui 编译是绿的"根本不能证明 .ui 是对的。
//   (另一个坑:写成两个 <enum>(类型一个、值一个)时,uic 会按【控件类】去限定,
//     生成 QTableView::SelectRows —— 能编过,但不是想要的那个类的枚举。)
//   正确写法:【单个 <enum>,值写全限定】,和本文件其它地方(如 orientation)一致。
void tst_CommTracePanel::noEnumPropertyUsesTheBrokenInlineForm() {
    // <enum>xxx</enum> 后面【紧跟着】非空白文字 = 那个坏写法。
    // 正常写法后面要么是空白+下一个标签,要么紧跟 </property>。
    static const QRegularExpression re(QStringLiteral(
        "<enum>[^<]*</enum>\\s*[^<\\s]"));
    const QRegularExpressionMatch m = re.match(m_ui);
    QVERIFY2(!m.hasMatch(),
             qPrintable(QStringLiteral(
                 "hmi.ui 里有一个枚举属性写成了 uic 不认的\"类型 + 行尾值\"写法 ——\n"
                 "  位置:%1\n"
                 "  uic 会把类型当值照抄,生成 setXxx(SomeType::SomeType),\n"
                 "  编译报 C2275。\n"
                 "  ★ 而且它【静默】:ninja 不会因为只改了 .ui 就重跑 uic,\n"
                 "    只有 -Clean 干净重建才会现形。\n"
                 "  正确写法:单个 <enum>,值写全限定(如 QAbstractItemView::SelectRows)。")
                 .arg(m.captured(0))));

    // 这两处【必须】是全限定值 —— 钉住第三页表格的两个枚举属性。
    QVERIFY2(m_ui.contains(QStringLiteral("<enum>QAbstractItemView::SelectRows</enum>")),
             "hmi.ui 里 selectionBehavior 的枚举不是全限定的 QAbstractItemView::SelectRows");
    QVERIFY2(m_ui.contains(QStringLiteral("<enum>Qt::ElideRight</enum>")),
             "hmi.ui 里 textElideMode 的枚举不是全限定的 Qt::ElideRight");
}

// -----------------------------------------------------------------------
//  hmi.cpp 的接线
void tst_CommTracePanel::buildCommTracePanel_hasNoQMessageBox() {
    const int fn = m_code.indexOf(QStringLiteral("void hmi::buildCommTracePanel()"));
    QVERIFY2(fn >= 0, "hmi.cpp 里找不到 buildCommTracePanel()");
    const QString body = m_code.mid(fn, m_code.indexOf(QStringLiteral("\n}"), fn) - fn);

    // ★ 这个函数是【纯接线】:建模型、setModel、连三个信号。
    //   它要是弹框,说明有决定被做在了接线层 ——
    //   而"要不要弹框"这个决定该留在界面层,
    //   数据层(CommTraceModel)一行界面代码都没有,那才是能脱开界面单测的原因。
    QVERIFY2(!body.contains(QStringLiteral("QMessageBox")),
             "buildCommTracePanel() 里出现了 QMessageBox ——\n"
             "  这个函数是纯接线(建模型/setModel/连信号),不該做\"要不要问用户\"的决定。\n"
             "  真要弹框请放回界面层,数据层保持一行界面代码都没有\n"
             "  (那正是 tst_CommTraceModel 能在无界面环境里跑的原因)。");
}

void tst_CommTracePanel::buildCommTracePanel_wiresModelToView() {
    const int fn = m_code.indexOf(QStringLiteral("void hmi::buildCommTracePanel()"));
    QVERIFY2(fn >= 0, "hmi.cpp 里找不到 buildCommTracePanel()");
    const QString body = m_code.mid(fn, m_code.indexOf(QStringLiteral("\n}"), fn) - fn);

    // 四处接线,少一处就是一处功能没接上:
    //   setModel      —— 表格不认模型,表格永远是空的
    //   statsChanged  —— 统计标签不跟着变,数字停在 0
    //   setCollecting —— 暂停按钮点了不起作用
    //   clear         —— 清空按钮点了不起作用
    QVERIFY2(body.contains(QStringLiteral("setModel")),
             "buildCommTracePanel() 里没有 setModel —— 表格不认模型,永远是空的");
    QVERIFY2(body.contains(QStringLiteral("statsChanged")),
             "buildCommTracePanel() 里没有连 statsChanged —— 统计标签不会更新");
    QVERIFY2(body.contains(QStringLiteral("setCollecting")),
             "buildCommTracePanel() 里没有 setCollecting —— 暂停按钮点了不起作用");
    QVERIFY2(body.contains(QStringLiteral("clear()")),
             "buildCommTracePanel() 里没有 clear —— 清空按钮点了不起作用");
}

void tst_CommTracePanel::frameTraced_isHookedToTheModel() {
    // ★ 这条是【整个功能的命脉】:帧从哪儿来?
    //   只有 ModbusMaster 同时握有请求帧字节 + 响应帧字节 + 请求发出的时刻,
    //   所以「往返耗时」只有它算得出来(TcpClient 那层连"什么是一帧"都不知道)。
    //   接线的 lambda 在 hmi.cpp 的 driverAdded 段里,不在 buildCommTracePanel() 里。
    QVERIFY2(m_code.contains(QStringLiteral("ModbusMaster::frameTraced")),
             "hmi.cpp 里没有 connect ModbusMaster::frameTraced —— "
             "帧监视器收不到任何帧,表格永远是空的。\n"
             "  这是整个功能的命脉:只有那一层算得出往返耗时。");

    // 而信号本身必须真的存在(改个名不会有人发现,只会让功能悄悄死掉)
    QFile mm(QStringLiteral("%1/ModbusMaster.h").arg(QString::fromLatin1(HMI_SRC_DIR_STRING)));
    QVERIFY2(mm.open(QIODevice::ReadOnly | QIODevice::Text),
             qPrintable(QStringLiteral("打不开 ModbusMaster.h:%1").arg(mm.errorString())));
    const QString master = codeOnly(QString::fromUtf8(mm.readAll()));
    mm.close();
    QVERIFY2(master.contains(QStringLiteral("void frameTraced(")),
             "ModbusMaster.h 里没有 frameTraced 信号 —— "
             "hmi.cpp 那边接的是一个不存在的东西(编译会挂,但值得钉住这个名字)。");
}

void tst_CommTracePanel::debugTabsHeight_isNotHardcodedInUi() {
    // ① .ui 里 debugTabs 不能有 maximumSize(高度写死 = 与主题脱节)
    const QString xml = widgetXml(QStringLiteral("debugTabs"));
    QVERIFY2(!xml.isEmpty(), "hmi.ui 里找不到 debugTabs");
    QVERIFY2(maxHeightIn(xml) < 0,
             "debugTabs 在 .ui 里被写死了 maximumSize ——\n"
             "  调试区总高 = 页签条 + 当前页高,而页高取决于 qss 里的页签条高度和控件高度。\n"
             "  写死就跟主题脱节:改了 qss 之后这里对不上,\n"
             "  表现是面板多一截黑(前两页下面各空一块)。\n"
             "  高度由 hmi.cpp 的 syncTabHeight 在运行期算。");

    // ② 运行期确实在算
    QVERIFY2(m_code.contains(QStringLiteral("syncTabHeight")),
             "hmi.cpp 里没有 syncTabHeight —— 调试区高度没人在算了。\n"
             "  少了它,加一页高的第三页会把整个调试区撑高,前两页下面各空一块黑。");
    QVERIFY2(m_code.contains(QStringLiteral("setFixedHeight")),
             "hmi.cpp 里没有 setFixedHeight —— syncTabHeight 没在真正定高度");

    // ③ 切页时要重算
    QVERIFY2(m_code.contains(QStringLiteral("currentChanged")),
             "hmi.cpp 里没连 currentChanged —— 切到第三页时高度不会重算,面板会空一块。");

    // 负向对照 ②(实跑):给 .ui 里的 debugTabs 加 maximumSize 240
    //   → 红 1:debugTabsHeight_isNotHardcodedInUi
    //     绿 13
    // 负向对照 ③(实跑):删掉 hmi.cpp 里 currentChanged 的 connect
    //   → 红 1:debugTabsHeight_isNotHardcodedInUi
    //     绿 13
}

// ★ 用 GUILESS:这个测试只读 .ui / .cpp 文本,不建任何控件,
//   所以不需要 QApplication —— 也就意味着在【没有显示设备】的机器上也能跑。
QTEST_GUILESS_MAIN(tst_CommTracePanel)
#include "tst_CommTracePanel.moc"
