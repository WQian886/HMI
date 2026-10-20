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
//   ✗ 不能抓:"好不好看"。42 种深色配色的取舍,自动测试证明不了,
//     那一半必须人眼看 —— 所以每次改完都要启动一次。
//
// 【负向对照 —— 四轮全部实跑,数字是跑出来的不是推出来的】
//   ① 删掉 modbusRwCombo 的 minimumSize   → 红 1 绿 6(点名 modbusRwCombo 没写)
//   ② 给 modbusResultLabel 加回 color:blue → 红 1 绿 6
//   ③ 删掉"地址"那个字段名标签              → 红 2 绿 5
//      (两条同时红:标签总数那条也一起数,不是只有专门管标签那条)
//   ④ 把 QLabel#fieldLabel 那条规则删成空 {}→ 红 1 绿 6,
//      tst_Theme 也红 1(它的表里补进了这条选择器)
//
//   ★ ④ 那一轮第一次跑时 tst_Theme 是【绿的】—— 它压根不知道这条规则存在。
//     所以顺手把 QLabel#fieldLabel 加进了 tst_Theme 的 kRequiredRules:
//     一个选择器该被几个测试盯着,取决于删它会让界面坏成什么样,不是取决于谁先写。
//
// 关联:hmi.ui modbusLayout / hmi.cpp buildDebugPanel() / theme/dark_industrial.qss
// ============================================================================

#include <QtTest>
#include <QFile>
#include <QRegularExpression>
#include <QStringList>

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

    // ★ 正上方那行也要一致 —— 两行在同一个折叠面板里,会互相被比较。
    void theRowAboveIt_hasTheSameHeight();

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
    const QString open = QStringLiteral("<layout class=\"QHBoxLayout\" name=\"%1\">").arg(layoutName);
    const int at = m_ui.indexOf(open);
    if (at < 0) return QString();
    const int end = m_ui.indexOf(QStringLiteral("</layout>"), at);
    if (end < 0) return QString();
    return m_ui.mid(at, end - at);
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

void tst_ModbusPanel::theRowAboveIt_hasTheSameHeight() {
    const QString xml = layoutXml(QStringLiteral("tcpSendLayout"));
    QVERIFY2(!xml.isEmpty(), "hmi.ui 里找不到 tcpSendLayout");

    const auto widgets = widgetsIn(xml);
    QVERIFY2(widgets.size() == 2,
             qPrintable(QStringLiteral("tcpSendLayout 里应有 2 个控件,实际 %1 个")
                 .arg(widgets.size())));

    // 这一行本来就是对的(两个都写着 40x40)。
    // 把它一起钉住,是因为上面那行改坏之后,
    // "下面对齐上面那行"这句话就没有基准了 —— 两行一起歪是可能的。
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
                 "Modbus 上面那行(手动发送)也歪了 —— 它是 Modbus 那行的对齐基准:\n  %1")
                 .arg(bad.join(QStringLiteral("\n  ")))));
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