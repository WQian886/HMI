// ============================================================================
// tst_ModbusPanel —— 主画面调试面板里 Modbus 那一行的【形状】
//
// 【这个测试在防什么】
//   用户报"modbus 的读写和选择功能码的按钮还是不太好看"。
//   查下来的原因不是配色,是【这一行的七个控件有七种高度】:
//     按 qss 的 min-height + padding 逐个算(每一项都在 .ui 里可核):
//       QLineEdit    padding 4+4 + 边框 1+1 + 行高 17         ≈ 27px
//                    —— 三个输入框只写了 maximumSize,maximumSize 是【上限】,
//                       不占满就不生效,所以它们是 27px 而不是 40px
//       QComboBox    min-height 20 + padding 4+4 + 边框 1+1   = 30px
//       QPushButton  min-height 20 + padding 6+6 + 边框 1+1   = 34px
//       QLabel       跟着字号走                                ≈ 20px
//   于是渲染出来是 27/30/30/27/27/34/20。
//   而它正上方那一行 tcpSendLayout 是 40/40 —— 同一个折叠面板里,
//   两行连彼此都对不齐。这就是"看着拼凑"的来源。
//
//   ★ 这个形状【不可能】靠肉眼 review 长期守住:
//     上面那个"只写了 maximumSize"的错误就是活例子 —— 它不报错、不难看出一丁点,
//     一行 <property> 就把一个控件矮了 13px。下一个人加一个控件同样会忘。
//     所以把它写成断言。
//
// 【为什么读 .ui 文本而不是建出控件量】
//   同 tst_MenuBar / tst_Theme 的老问题:hmi_core 不编 hmi.cpp,
//   链不到 ui_*.h,建不出主窗口那一排控件。
//   另一个更硬的理由:.ui 写错【属性】是静默的 ——
//     比如 minimumSize 少一层 <size>,uic 不报错,那一行被丢掉,
//     而控件照建、照显示、只是矮一截。
//   读文本能把"写了但写歪了"变成"查得到"。
//
// 【诚实的边界】
//   ✓ 能抓:高度不齐 / inline 样式表 / 少标签 / qss 里少了 fieldLabel 规则
//           / 调试区不是两页 / 两行又被挂回 centralLayout / 运行期又搬布局
//   ✗ 不能抓:"好不好看"。深色配色的取舍,自动测试证明不了,
//     那一半必须人眼看 —— 所以每次改完都要启动一次。
//
// 【P1-e 那一段:一个真 bug,不是审美】
//   用户报"浮现在另外连接信息的上面"。截图确认:面板一展开,
//   Modbus 那一行压在【启动/停止/复位】那一行上面,而面板自己的槽位是一块全黑空白。
//   成因在 hmi.cpp 的 buildDebugPanel():P1-c 用 centralLayout->takeAt()
//   把两个 QLayoutItem 摘给一个新面板 —— 摘出来的布局【不会重新激活】,
//   保留着在 centralLayout 里的旧几何,于是照旧画在老位置上。
//   改法:两页(debugTabs)直接写进 .ui 由 uic 生成,代码里不再搬布局;
//   buildDebugPanel() 只插一个【新建的】开关按钮。
//   本文件下面三条(debugArea_hasExactlyTwoPages /
//   neitherDebugRow_hangsDirectlyOffCentralLayout /
//   buildDebugPanel_doesNotMoveLayoutsAtRuntime)钉的就是这件事。
//
// 【负向对照 —— 七轮全部实跑,数字是跑出来的不是推出来的】
//   ① 删掉 modbusRwCombo 的 minimumSize   → 红 1 绿 6(点名 modbusRwCombo 没写)
//   ② 给 modbusResultLabel 加回 color:blue → 红 1 绿 6
//   ③ 删掉"地址"那个字段名标签              → 红 2 绿 5
//      (两条同时红:标签总数那条也一起数,不是只有专门管标签那条)
//   ④ 把 QLabel#fieldLabel 那条规则删成空 {}→ 红 1 绿 6,
//      tst_Theme 也红 1(它的表里补进了这条选择器)
//   ⑤ 把 modbusLayout 从页里提出来直挂       → 红 2 绿 8
//      (两条同时红:页数那条和"直挂主布局"那条 —— 前者因为页被拆没了,
//       后者因为最近布局祖先变成了 centralLayout)
//   ⑥ 把 hmi.cpp 的 takeAt 那行加回来        → 红 1 绿 9
//   ⑦ 删掉 Modbus 页的 title                 → 红 1 绿 9
//
//   ★ ⑥ 顺带证明 codeOnly() 是有用的:buildDebugPanel() 的注释里
//     大段讲着 takeAt 为什么不安全,不去注释的话断言会永远红 ——
//     那种红比绿更坏,它看起来像"这个测试在盯着这件事"。
//
//   ★ ④ 那一轮第一次跑时 tst_Theme 是【绿的】—— 它压根不知道这条规则存在。
//     所以顺手把 QLabel#fieldLabel 加进了 tst_Theme 的 kRequiredRules:
//     一个选择器该被几个测试盯着,取决于删它会让界面坏成什么样,不是取决于谁先写。
//
// 关联:hmi.ui modbusLayout + debugTabs / hmi.cpp buildDebugPanel()
//      / theme/dark_industrial.qss
// ============================================================================

#include <QtTest>
#include <QFile>
#include <QRegularExpression>
#include <QStringList>

// 去掉 C++ 注释再查字符串。
// ★ 为什么必须去:buildDebugPanel() 的注释里【大段】讲着 takeAt 为什么会留旧几何
//   (那正是这个测试要防的东西)。不去注释的话,注释里提到的 takeAt
//   会让断言变成永远绿 —— 而它看起来是在测东西的。
//   仓库里 tst_MenuBar / tst_UIPermission / tst_Theme 用的是同一招,
//   原因也一样:注释是给人读的,不该被当成代码证据。
static QString codeOnly(const QString& text) {
    QString out = text;
    out.remove(QRegularExpression(QStringLiteral("/\\*.*?\\*/"),
                                  QRegularExpression::DotMatchesEverythingOption));
    out.remove(QRegularExpression(QStringLiteral("//[^\n]*")));
    return out;
}

class tst_ModbusPanel : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();

    // ★★ 主体:这一行所有【非标签】控件的高度必须一致。
    //   高度不齐是这一行"看着拼凑"的唯一来源,也是最容易被下一次改动破坏的。
    void everyControlInModbusRow_hasTheSameHeight();

    // ★ 不许有 inline 样式表 —— 有一处就有一处失效的 qss。
    //   真实事故:modbusResultLabel 上那行 color: blue
    //   在深底上对比度只有 1.60:1,而这张 qss 的文件头写着"正文至少 7:1"。
    //   而且它不是死代码(updateModbusLabel 只 setText,不碰样式),是真在显示的。
    void noInlineStyleSheetInModbusRow();

    // ★ 每个输入框前面都要有【看得见的】字段名。
    //   原来三个输入框只有 placeholder,而它们都有默认值(1 / 0 / 1),
    //   placeholder 只在框空的时候才显示 —— 于是那一行从头到尾没有一个字
    //   说明那几个框是干什么的。
    void everyInputHasAVisibleFieldLabel();

    // ★ 另一页那一行也要一致 —— 两行是同一套工具的两种用法,会互相被比较。
    void theOtherPage_hasTheSameHeight();

    // ★★ 调试区必须正好两页,且两行各自待在自己那一页里。
    //   P1-e 的起因就是这两行的归属被搞乱了。
    void debugArea_hasExactlyTwoPages();

    // ★★ 这两行不许再直接挂在 centralLayout 上。
    //   2026-10-20 的真 bug 就是这个:P1-c 用 takeAt() 把它们摘给一个新面板,
    //   摘出来的布局不会重新激活、保留着旧几何,面板一展开
    //   Modbus 那行就压在启动/停止/复位那一行上面(实测截图)。
    //   现在两页在 .ui 里由 uic 生成,布局归属是声明式的。
    void neitherDebugRow_hangsDirectlyOffCentralLayout();

    // ★★ hmi.cpp 的 buildDebugPanel() 不许再搬布局。
    //   上面那条测的是 .ui;这条测的是代码 —— 因为即使 .ui 写对了,
    //   只要运行期又 takeAt 一遍,叠在一起的现象就会原样回来。
    void buildDebugPanel_doesNotMoveLayoutsAtRuntime();

    // ★ 标签那条 qss 规则必须在。
    //   少了它,五个标签会和输入框里的值同色同字号,
    //   用户得先读一遍才知道哪个字是标签、哪个字是值。
    void fieldLabels_haveTheirOwnThemeRule();

private:
    QString m_ui;      // hmi.ui 全文
    QString m_qss;     // dark_industrial.qss 全文

    // 取出某个布局的整段 XML(<layout ... name="X"> 到它的 </layout>)
    QString layoutXml(const QString& layoutName) const;
    // 该布局段里所有 <widget class="..." name="..."> 的 (class, name)
    QList<QPair<QString, QString>> widgetsIn(const QString& layoutXml) const;
    // 某个布局/控件的祖先链(由外到内,不含它自己);每项带 "L:"布局 / "W:"控件 前缀
    QStringList enclosingChain(const QString& name) const;
};

void tst_ModbusPanel::initTestCase() {
    QFile ui(QStringLiteral("%1/hmi.ui").arg(QString::fromLatin1(HMI_SRC_DIR_STRING)));
    QVERIFY2(ui.open(QIODevice::ReadOnly | QIODevice::Text),
             qPrintable(QStringLiteral("打不开 hmi.ui:%1").arg(ui.errorString())));
    m_ui = QString::fromUtf8(ui.readAll());
    ui.close();

    QFile qss(QStringLiteral("%1/theme/dark_industrial.qss")
                  .arg(QString::fromLatin1(HMI_SRC_DIR_STRING)));
    QVERIFY2(qss.open(QIODevice::ReadOnly | QIODevice::Text),
             qPrintable(QStringLiteral("打不开 dark_industrial.qss:%1").arg(qss.errorString())));
    m_qss = QString::fromUtf8(qss.readAll());
    qss.close();

    // 地基:读到的不能是空的,否则下面所有"查不到"都会绿成一片。
    QVERIFY2(m_ui.size() > 5000,
             qPrintable(QStringLiteral("hmi.ui 读出来短得反常(%1 字节)—— "
                                       "路径错了,而下面的断言会空洞地绿")
                            .arg(m_ui.size())));
    QVERIFY2(m_qss.size() > 1000,
             qPrintable(QStringLiteral("样式表读出来短得反常(%1 字节)").arg(m_qss.size())));
}

QString tst_ModbusPanel::layoutXml(const QString& layoutName) const {
    // ★ 不要把 class 写死成 QHBoxLayout。
    //   第一版就是这么写的,结果 centralLayout 是 QVBoxLayout,
    //   查它的时候返回空字符串,而"两行有没有挂回主布局"那条断言
    //   恰好就是查 centralLayout —— 它于是恒绿,测了个寂寞。
    //   教训和 tst_Theme 那次"contains 抓不住空 {}"是同一个:
    //   查不到 ≠ 没问题,查不到这件事本身必须先排除掉。
    const QRegularExpression re(
        QStringLiteral("<layout class=\"\\w+\" name=\"%1\">").arg(layoutName));
    const int at = m_ui.indexOf(re);
    if (at < 0) return QString();
    const int end = m_ui.indexOf(QStringLiteral("</layout>"), at);
    if (end < 0) return QString();
    return m_ui.mid(at, end - at);
}

// ★ 为什么需要这个:layoutXml() 只对【叶子布局】管用 —— 它截到第一个 </layout>,
//   而 centralLayout 里还嵌着 buttonRowLayout 等,截出来的是个片段,
//   拿它做"谁挂在谁下面"的判断会得到错答案(第一版就是这么错的)。
//   要问"这个布局的祖先是谁",只能顺着开闭标签数深度。
//
//   ★ 返回的每一项带 L:/W: 前缀,区分布局和控件 ——
//     这不是讲究,是必需的:问"有没有直接挂在 centralLayout 上"时,
//     祖先链里【一定】有 centralLayout(它在最外层),
//     有意义的是"离它最近的【布局】祖先是不是 centralLayout"。
//     不区分 L/W 就只能查"链里有没有",那对每一行都成立,恒绿,测了个寂寞。
//     —— 这一条是写完第一版之后当场跑出来的:第一版就恒红,红得也很没道理。
QStringList tst_ModbusPanel::enclosingChain(const QString& name) const {
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

QList<QPair<QString, QString>> tst_ModbusPanel::widgetsIn(const QString& xml) const {
    QList<QPair<QString, QString>> out;
    static const QRegularExpression re(
        QStringLiteral("<widget\\s+class=\"(\\w+)\"\\s+name=\"(\\w+)\""));
    QRegularExpressionMatchIterator it = re.globalMatch(xml);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        out.append(qMakePair(m.captured(1), m.captured(2)));
    }
    return out;
}

// -----------------------------------------------------------------------

void tst_ModbusPanel::everyControlInModbusRow_hasTheSameHeight() {
    const QString xml = layoutXml(QStringLiteral("modbusLayout"));
    QVERIFY2(!xml.isEmpty(),
             "hmi.ui 里找不到 modbusLayout —— 调试面板那一行没了?"
             "(如果它被搬走了,请把本测试指向新的位置,别直接删掉本条)");

    const auto widgets = widgetsIn(xml);
    QVERIFY2(!widgets.isEmpty(), "modbusLayout 里一个控件都没解析到 —— "
                                  "多半是解析器写坏了,那样本条会【永远绿】");

    // 期望高度 = 40。和上面 tcpSendLayout 那行一致(它两个控件都写着 40),
    // 也和主画面连接/断开两个按钮一致。三处对齐才是"一致"。
    const int kHeight = 40;

    QStringList tooShort, labels;
    int checked = 0;
    for (const auto& w : widgets) {
        const QString cls = w.first, name = w.second;

        if (cls == QLatin1String("QLabel")) {
            // 标签靠布局垂直居中,不需要 minimumSize。
            // modbusResultLabel 是例外(它要撑开一个固定宽度的结果区),
            // 但它也不参与"这一行的输入控件"这个话题,一并跳过。
            labels << name;
            continue;
        }
        ++checked;

        // 取这个 widget 自己那一段(到下一个 <widget 或 </layout> 为止)
        const int selfAt = xml.indexOf(QStringLiteral("name=\"%1\"").arg(name));
        QVERIFY2(selfAt >= 0, name.toUtf8().constData());
        int nextAt = xml.indexOf(QStringLiteral("<widget"), selfAt + 1);
        const int endAt = xml.indexOf(QStringLiteral("</layout>"), selfAt);
        if (nextAt < 0 || (endAt >= 0 && endAt < nextAt)) nextAt = (endAt < 0) ? xml.size() : endAt;
        const QString self = xml.mid(selfAt, nextAt - selfAt);

        // minimumSize 里的 height
        static const QRegularExpression minRe(QStringLiteral(
            "<property name=\"minimumSize\">\\s*<size>.*?<height>(\\d+)</height>"));
        const QRegularExpressionMatch mm = minRe.match(self);
        if (!mm.hasMatch()) {
            tooShort << QStringLiteral("%1(%2):【没写 minimumSize】")
                            .arg(name, cls);
        } else if (mm.captured(1).toInt() != kHeight) {
            tooShort << QStringLiteral("%1(%2):最小高度 %3,应为 %4")
                            .arg(name, cls).arg(mm.captured(1)).arg(kHeight);
        }
    }

    // ★ 写死控件数 —— 不写"至少 1 个"。
    //   少了一个输入框而本条还绿,那正是它要防的事。
    //   6 个 = 从机/读写/区域/地址/数据 五个输入 + 发送按钮
    //   (第一版这里写的是 5,漏算了按钮本身 —— 跑出来红的正是这条。)
    QVERIFY2(checked == 6,
             qPrintable(QStringLiteral(
                 "modbusLayout 里的非标签控件应是 6 个"
                 "(从机/读写/区域/地址/数据 五个输入 + 发送按钮),实际 %1 个。\n"
                 "  数量变了就意味着这一行的组成变了 —— "
                 "先确认是有意为之,再回来改这个数字。").arg(checked)));
    QVERIFY2(labels.size() == 6,
             qPrintable(QStringLiteral("modbusLayout 里应有 6 个 QLabel"
                 "(5 个字段名 + 1 个结果),实际 %1 个").arg(labels.size())));

    QVERIFY2(tooShort.isEmpty(),
             qPrintable(QStringLiteral(
                 "Modbus 那一行的控件高度不齐 —— 这就是它\"看着拼凑\"的全部原因:\n  %1\n"
                 "  没写 minimumSize 的控件会退回 qss 算出来的自然高度\n"
                 "  (QComboBox 30px / QPushButton 34px / QLineEdit 27px),\n"
                 "  而这一行要的是 %2px —— 和它上面那行、以及连接/断开两个按钮对齐。")
                 .arg(tooShort.join(QStringLiteral("\n  "))).arg(kHeight)));

    // 负向对照 ①(实跑):删掉 modbusRwCombo 的 minimumSize
    //   → 红 1:everyControlInModbusRow_hasTheSameHeight(点名 modbusRwCombo 没写)
    //     绿 6
}

void tst_ModbusPanel::noInlineStyleSheetInModbusRow() {
    const QString xml = layoutXml(QStringLiteral("modbusLayout"));
    QVERIFY2(!xml.isEmpty(), "hmi.ui 里找不到 modbusLayout");

    // QWidget::setProperty / uic 生成的样式表在 .ui 里长这样:
    //   <property name="styleSheet"> <string>...</string> </property>
    QVERIFY2(!xml.contains(QStringLiteral("name=\"styleSheet\"")),
             "Modbus 那一行里又有 inline 样式表了 ——\n"
             "  ① 它会让 qss 对那个控件【完全失效】,"
             "改主题时它不跟着变(第二套样式并行);\n"
             "  ② 上一次这里就出过一次:modbusResultLabel 上那行 color: blue,\n"
             "     深底上对比度只有 1.60:1,而这张 qss 的文件头写着\"正文至少 7:1\"。\n"
             "  要改颜色请改 theme/dark_industrial.qss;字号同理。");

    // 负向对照 ②(实跑):给 modbusResultLabel 加回 color: blue
    //   → 红 1:noInlineStyleSheetInModbusRow
    //     绿 6
}

void tst_ModbusPanel::everyInputHasAVisibleFieldLabel() {
    const QString xml = layoutXml(QStringLiteral("modbusLayout"));
    QVERIFY2(!xml.isEmpty(), "hmi.ui 里找不到 modbusLayout");

    // 五个字段,五个标签。少一个就退回"看框里的数字猜它是干什么的"。
    static const QRegularExpression re(
        QStringLiteral("<widget\\s+class=\"QLabel\"\\s+name=\"(\\w+)\">(.*?)</widget>"),
        QRegularExpression::DotMatchesEverythingOption);

    QRegularExpressionMatchIterator it = re.globalMatch(xml);
    QStringList found;
    int withFieldObjectName = 0;
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        const QString name = m.captured(1);
        if (name == QLatin1String("modbusResultLabel")) continue;   // 结果区不是字段名
        found << name;
        if (m.captured(2).contains(QStringLiteral("fieldLabel"))) ++withFieldObjectName;
    }

    QVERIFY2(found.size() == 5,
             qPrintable(QStringLiteral(
                 "Modbus 那一行应有 5 个字段名标签(从机/读写/区域/地址/数据),实际 %1 个。\n"
                 "  为什么非要标签:三个输入框原本只有 placeholder,\n"
                 "  而它们都有默认值(1 / 0 / 1),placeholder 只在框空的时候才显示 ——\n"
                 "  于是那一行从头到尾没有一个字说明那几个框是干什么的。\n"
                 "  现在有的:%2")
                 .arg(found.size()).arg(found.join(QStringLiteral(" / ")))));

    QVERIFY2(withFieldObjectName == 5,
             qPrintable(QStringLiteral(
                 "5 个字段名标签里只有 %1 个设了 objectName=\"fieldLabel\"。\n"
                 "  没设的话它们会用 QLabel 的默认色 #dfe8f2,和输入框里的值同色 ——\n"
                 "  用户得先读一遍才知道哪个字是标签、哪个字是值,层次就没了。")
                 .arg(withFieldObjectName)));

    // 负向对照 ③(实跑):删掉"地址"那个标签
    //   → 红 2:everyInputHasAVisibleFieldLabel(实际 4 个)
    //        + everyControlInModbusRow_hasTheSameHeight(标签总数 5 ≠ 6)
    //     绿 5
}

void tst_ModbusPanel::theOtherPage_hasTheSameHeight() {
    const QString xml = layoutXml(QStringLiteral("tcpSendLayout"));
    QVERIFY2(!xml.isEmpty(), "hmi.ui 里找不到 tcpSendLayout");

    const auto widgets = widgetsIn(xml);
    QVERIFY2(widgets.size() == 2,
             qPrintable(QStringLiteral("tcpSendLayout 里应有 2 个控件,实际 %1 个")
                 .arg(widgets.size())));

    // 另一页那一行本来就是对的(两个都写着 40x40)。
    // 把它一起钉住:两页在同一个 QTabWidget 里、挨着点,用户来回切时
    // 一眼就会比对 —— 其中一页矮了 6px 是当场能看出来的。
    QStringList bad;
    for (const auto& w : widgets) {
        const int selfAt = xml.indexOf(QStringLiteral("name=\"%1\"").arg(w.second));
        QVERIFY2(selfAt >= 0, w.second.toUtf8().constData());
        const int endAt = xml.indexOf(QStringLiteral("</layout>"), selfAt);
        const QString self = xml.mid(selfAt, endAt - selfAt);
        static const QRegularExpression minRe(QStringLiteral(
            "<property name=\"minimumSize\">\\s*<size>.*?<height>(\\d+)</height>"));
        const QRegularExpressionMatch mm = minRe.match(self);
        if (!mm.hasMatch() || mm.captured(1).toInt() != 40) {
            bad << QStringLiteral("%1:%2").arg(w.second,
                       mm.hasMatch() ? mm.captured(1) : QStringLiteral("没写"));
        }
    }
    QVERIFY2(bad.isEmpty(),
             qPrintable(QStringLiteral(
                 "手动发送那一页也歪了 —— 它和 Modbus 页在同一个页签控件里,\n"
                 "  用户来回切时会一眼比对出来:\n  %1")
                 .arg(bad.join(QStringLiteral("\n  ")))));
}

// -----------------------------------------------------------------------
//  以下三条是 P1-e(拆两页)带来的,起因是一个真 bug 不是审美。

void tst_ModbusPanel::debugArea_hasExactlyTwoPages() {
    const int at = m_ui.indexOf(QStringLiteral("name=\"debugTabs\""));
    QVERIFY2(at >= 0, "hmi.ui 里没有 debugTabs —— 调试区不是两页了。"
                      "用户明确要求过\"分成两页,放在不同的页面内\"。");

    // 两页:QTabWidget 直属的 QWidget 孩子
    static const QRegularExpression re(QStringLiteral(
        "<widget class=\"QWidget\" name=\"(tabManualSend|tabModbus)\">"));
    QStringList pages;
    QRegularExpressionMatchIterator it = re.globalMatch(m_ui);
    while (it.hasNext()) pages << it.next().captured(1);

    QVERIFY2(pages.size() == 2,
             qPrintable(QStringLiteral(
                 "调试区应正好 2 页(tabManualSend / tabModbus),实际找到 %1 个:%2")
                 .arg(pages.size()).arg(pages.join(QStringLiteral(" / ")))));

    // 每一页都得真的装着一行工具 —— 页建了但没放东西,
    // 表现出来是"点开那一页是空的",而从外面完全看不出。
    QVERIFY2(m_ui.indexOf(QStringLiteral("name=\"tcpSendLayout\"")) > 0, "tcpSendLayout 不见了");
    QVERIFY2(m_ui.indexOf(QStringLiteral("name=\"modbusLayout\"")) > 0, "modbusLayout 不见了");

    // 两页的标题必须都有 —— 页签上写"1" "2"而没有名字,
    // 用户根本不知道哪一页是哪一页。
    QVERIFY2(m_ui.contains(QStringLiteral("<string>手动发送</string>")),
             "第一页没有标题\"手动发送\"");
    QVERIFY2(m_ui.contains(QStringLiteral("<attribute name=\"title\"><string>Modbus</string>")),
             "第二页没有标题\"Modbus\"");
}

void tst_ModbusPanel::neitherDebugRow_hangsDirectlyOffCentralLayout() {
    // ★ 这就是 P1-c 那个 bug 的形状。
    //   原来两行是 centralLayout 的直接子布局,buildDebugPanel() 在运行期
    //   用 takeAt() 把它们摘给一个新面板 —— 摘出来的布局不会重新激活,
    //   保留着旧几何,于是面板一展开,Modbus 那行照旧画在 centralLayout
    //   里的老位置上,压在启动/停止/复位那一行上面。
    //   两页进了 .ui 之后,这两行的祖先链里不该再有 centralLayout。
    QStringList leaked;
    for (const char* name : {"tcpSendLayout", "modbusLayout"}) {
        const QString n = QString::fromLatin1(name);
        const QStringList chain = enclosingChain(n);
        QVERIFY2(!chain.isEmpty(),
                 qPrintable(QStringLiteral("hmi.ui 里找不到 %1 —— "
                     "查不到不等于没问题,先确认名字没被改掉再往下看")
                     .arg(n)));
        // 离它最近的【布局】祖先是谁 —— 那才是"直接挂在谁下面"的答案
        QString nearestLayout;
        for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
            if (it->startsWith(QLatin1String("L:"))) { nearestLayout = *it; break; }
        }
        if (nearestLayout == QLatin1String("L:centralLayout")) leaked << n;
    }
    QVERIFY2(leaked.isEmpty(),
             qPrintable(QStringLiteral(
                 "这两行还直接挂在 centralLayout 上:%1。\n"
                 "  调试区现在是 debugTabs(两页),它们该在页里,不该在主布局上。\n"
                 "  留在主布局上不会立刻看不出问题,但它们就回到 P1-c 那个\n"
                 "  「折进去但保留旧几何、叠在启动行上面」的老路上。")
                 .arg(leaked.join(QStringLiteral(" / ")))));

    // 而 debugTabs 必须【直接】挂在 centralLayout 里 —— 否则两页做好了,
    // 界面上哪儿都不显示,那等于把调试工具弄丢了(比叠在一起更难发现,
    // 因为界面看着"干净")。
    const QStringList tabsChain = enclosingChain(QStringLiteral("debugTabs"));
    QVERIFY2(!tabsChain.isEmpty(), "hmi.ui 里找不到 debugTabs");
    const QString nearest =
        tabsChain.isEmpty() ? QString() : tabsChain.last();
    QVERIFY2(nearest == QLatin1String("L:centralLayout"),
             qPrintable(QStringLiteral(
                 "debugTabs 的直接父级不是 centralLayout(祖先链:%1)—— "
                 "两页做好了但界面上不显示,那等于把调试工具弄丢了")
                 .arg(tabsChain.join(QStringLiteral(" < ")))));
}

void tst_ModbusPanel::buildDebugPanel_doesNotMoveLayoutsAtRuntime() {
    QFile f(QStringLiteral("%1/hmi.cpp").arg(QString::fromLatin1(HMI_SRC_DIR_STRING)));
    QVERIFY2(f.open(QIODevice::ReadOnly | QIODevice::Text),
             qPrintable(QStringLiteral("打不开 hmi.cpp:%1").arg(f.errorString())));
    const QString code = codeOnly(QString::fromUtf8(f.readAll()));
    f.close();

    const int fn = code.indexOf(QStringLiteral("void hmi::buildDebugPanel()"));
    QVERIFY2(fn >= 0, "hmi.cpp 里找不到 buildDebugPanel() 的定义");
    // 只看这个函数体,别把文件里别处的 takeAt 也算进来
    const QString body = code.mid(fn, code.indexOf(QStringLiteral("\n}"), fn) - fn);

    // ★ 上一条测的是 .ui,这条测的是代码 ——
    //   因为即使 .ui 写对了,只要运行期又 takeAt 一遍,
    //   叠在一起的现象就会原样回来,而 .ui 那时看不出任何毛病。
    QVERIFY2(!body.contains(QStringLiteral("takeAt")),
             "buildDebugPanel() 里又出现 takeAt —— 这正是 2026-10-20 那个叠位的成因:\n"
             "  takeAt() 摘出来的 QLayoutItem 交给新布局后【不会重新激活】,\n"
             "  它保留着在 centralLayout 里的旧几何,面板一展开就照旧画在老位置上。\n"
             "  两页已经在 .ui 里(debugTabs),这里只需要插一个【新建的】开关按钮,\n"
             "  插新控件进布局是安全的 —— 危险的只有\"摘走已有的 item\"。");
    QVERIFY2(!body.contains(QStringLiteral("addItem")),
             "buildDebugPanel() 里又出现 addItem —— 同上,是在往运行期拼布局。"
             "页的归属该由 .ui 声明,不由代码拼。");

    // 反过来:它必须【确实】在插开关按钮,否则折叠功能整个没了。
    QVERIFY2(body.contains(QStringLiteral("insertWidget")),
             "buildDebugPanel() 里没有 insertWidget —— 调试区的展开/收起开关没了。"
             "两页虽然建好了,但用户没法把它们藏起来。");
}

void tst_ModbusPanel::fieldLabels_haveTheirOwnThemeRule() {
    QVERIFY2(m_qss.contains(QStringLiteral("QLabel#fieldLabel")),
             "样式表里少了 QLabel#fieldLabel 那条规则 ——\n"
             "  五个字段名会退回 QLabel 默认的 #dfe8f2,和输入框里的值同色同字号,\n"
             "  用户得先读一遍才知道哪个字是标签、哪个字是值。\n"
             "  补一条次要文字色 #9fb0c4 的即可(标签是框架,值才是内容)。");

    // 这条规则必须真的带着次要色,而不是只有个空壳 ——
    // 理由同 tst_Theme 那次加固:删规则时最自然的手滑是留下一个空的 {}。
    const int at = m_qss.indexOf(QStringLiteral("QLabel#fieldLabel"));
    QVERIFY(at >= 0);
    const int open = m_qss.indexOf(QLatin1Char('{'), at);
    const int close = m_qss.indexOf(QLatin1Char('}'), open);
    QVERIFY2(open > 0 && close > open, "QLabel#fieldLabel 那条规则没有 {} 包裹");
    QVERIFY2(m_qss.mid(open, close - open).contains(QStringLiteral("#9fb0c4")),
             "QLabel#fieldLabel 那条规则里没有次要文字色 #9fb0c4 —— "
             "它要么是空的,要么被别人改成了正文色(层次就没了)");

    // 负向对照 ④(实跑):把这条规则删成 QLabel#fieldLabel { }(空壳)
    //   → 红 1:fieldLabels_haveTheirOwnThemeRule
    //     绿 6
    //     tst_Theme::allCriticalSelectorsArePresent 同轮也红 1
    //   ★ 头一条 contains(选择器) 抓不住这个 —— 名字还在,只是里面空了。
    //     这是删样式规则时最自然的手滑,所以必须查内容。
}

QTEST_MAIN(tst_ModbusPanel)
#include "tst_ModbusPanel.moc"