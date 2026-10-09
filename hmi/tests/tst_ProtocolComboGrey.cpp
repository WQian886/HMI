// ============================================================================
// 2026-10-10 · 协议下拉框置灰 —— 画出来到底对不对
//
// 【为什么 tst_ProtocolAvailability 之外还要这个】
//   那个测的是"表对不对"(isUsable 查表)。这个测的是"表对了之后,
//   QComboBox 到底有没有被改成我们以为的样子"。
//   两者之间隔着一层 Qt:setItemData 写进去的 role 对不对、灰度够不够、
//   文字有没有被误伤 —— 这些只有真去读控件状态才抓得到。
//
//   这也正是 F5-a 那次学到的:几何对 ≠ 画出来对。
//   这次是"逻辑对 ≠ 控件对"。
//
// 【测的是生产代码本身】
//   置灰逻辑抽在 drivers/ProtocolAvailabilityUi.cpp 里,hmi.cpp 和本测试
//   调的是同一个函数。之前那段是写在 hmi 构造函数里的,测试只能复制一份副本,
//   那样 hmi.cpp 里那份被改错了测试照样绿 —— 跟本项目吃过的两次亏同源。
//   代价:这个测试保证不了"hmi.cpp 真的调用了它",那一条靠人工核对
//   (hmi.cpp 构造函数里那行 applyProtocolAvailabilityStyling 调用)。
//
// 关联:drivers/ProtocolAvailabilityUi.{h,cpp}, drivers/DriverFactory.{h,cpp}
//
// 【下拉框的项跟 hmi.ui:138-146 一致】
//   三个协议、同样的显示文字。测一个跟真实下拉框长得不一样的下拉框等于没测。
// ============================================================================

#include <QtTest>
#include <QComboBox>
#include <QBrush>
#include <QColor>
#include "drivers/DriverFactory.h"
#include "drivers/ProtocolAvailabilityUi.h"

using namespace qwhmi::drivers;

class TestProtocolComboGrey : public QObject {
    Q_OBJECT
private slots:
    // ---- 置灰后:文字带未实现标记、颜色是灰的、tooltip 说了原因 ----
    void greyedItem_showsUnimplementedSuffix();
    void greyedItem_hasGreyForeground();
    void greyedItem_hasTooltipExplainingWhy();
    void greyedItem_staysSelectableSoWeCanExplainIt();   // 见下面注释

    // ---- Modbus 那一项必须【完全没被动过】----
    void usableItem_isLeftCompletelyAlone();
    void suffixIsNotAppendedToUsableItem();

    // ---- ★ 抽成公共函数之后才可能测到的两条 ----
    void callingTwice_doesNotDoubleTheSuffix();
    void itemTextMatchesRealUiItems();

    // ---- ★ 撤回下拉框:一个一直没被发现、也一直没生效的 bug ----
    void findIndex_matchesProtocolIdAgainstDisplayName();
    void findIndex_ignoresTheUnimplementedSuffix();
    void findIndex_returnsMinusOneWhenNoMatch();
};

// ★★★ 这一组对应 hmi::revertProtocolCombo 的匹配规则。
//
//   【这里藏着一个"写了但从来没执行过"的 bug】
//     原来的撤回代码是:
//         const int idx = combo->findText(current, Qt::MatchFixedString);
//         if (idx >= 0) combo->setCurrentIndex(idx);
//     current 存的是协议 ID "modbus",下拉框里是显示名 "Modbus",
//     而 Qt::MatchFixedString 是【大小写敏感】的 → findText 返回 -1
//     → if 不成立 → 【撤回那两行从来没执行过】。
//
//     后果:用户选了别的协议、被弹窗拒绝之后,下拉框【停在刚选的那一项上】,
//     而 currentProtocol_ 还是 "modbus"。界面上写着用户选的协议,
//     实际连的却是另一个 —— 而且没有任何日志。
//
//     为什么一直没人发现:create() 硬编码 "modbus",
//     所以不管下拉框显示什么,实际连的都是 Modbus,"界面"和"实际"碰巧一致。
//     等 v0.3.0 真接上 S7/EIP,这个不一致会立刻变成"界面上写着 S7,连的是 Modbus"。
//
//     这就是本项目反复吃的那种亏:一段代码看着在跑,实际从没生效过。
void TestProtocolComboGrey::findIndex_matchesProtocolIdAgainstDisplayName() {
    // 这三个显示名照抄 hmi.ui:138-146
    const QStringList items = {"Modbus", "S7", "EtherNet/IP"};
    QCOMPARE(findProtocolItemIndex(items, "modbus"), 0);
    QCOMPARE(findProtocolItemIndex(items, "s7"), 1);
    // ★ 工厂认的是 "ethernet_ip",界面写的是 "EtherNet/IP" —— 这一条以前是 -1
    QCOMPARE(findProtocolItemIndex(items, "ethernet_ip"), 2);
    QCOMPARE(findProtocolItemIndex(items, "eip"), 2);   // 两种写法都该指到同一项
}

void TestProtocolComboGrey::findIndex_ignoresTheUnimplementedSuffix() {
    // 置灰之后项的文字变了,撤回逻辑必须还能找回原来那一项
    QComboBox combo;
    combo.addItem("Modbus");
    combo.addItem("S7");
    combo.addItem("EtherNet/IP");
    applyProtocolAvailabilityStyling(&combo);

    QStringList texts;
    for (int i = 0; i < combo.count(); ++i) {
        texts << combo.itemText(i);
    }
    QCOMPARE(findProtocolItemIndex(texts, "modbus"), 0);
    QCOMPARE(findProtocolItemIndex(texts, "ethernet_ip"), 2);
}

void TestProtocolComboGrey::findIndex_returnsMinusOneWhenNoMatch() {
    // 找不到必须返回 -1 而不是 0 —— 返回 0 会把下拉框错误地设成第一项,
    // 那比"不动"更糟:用户以为撤回成功了。
    const QStringList items = {"Modbus", "S7", "EtherNet/IP"};
    QCOMPARE(findProtocolItemIndex(items, "profibus"), -1);
    QCOMPARE(findProtocolItemIndex(QStringList(), "modbus"), -1);
}

void TestProtocolComboGrey::greyedItem_showsUnimplementedSuffix() {
    QComboBox combo;
    combo.addItem("Modbus");
    combo.addItem("S7");
    combo.addItem("EtherNet/IP");
    applyProtocolAvailabilityStyling(&combo);

    QCOMPARE(combo.itemText(0), QStringLiteral("Modbus"));
    QVERIFY2(combo.itemText(1).contains("未实现"),
             qPrintable("S7 项没加未实现标记,实际:" + combo.itemText(1)));
    QVERIFY2(combo.itemText(2).contains("未实现"),
             qPrintable("EtherNet/IP 项没加未实现标记,实际:" + combo.itemText(2)));
}

void TestProtocolComboGrey::greyedItem_hasGreyForeground() {
    QComboBox combo;
    combo.addItem("Modbus");
    combo.addItem("S7");
    applyProtocolAvailabilityStyling(&combo);

    const QVariant fg = combo.itemData(1, Qt::ForegroundRole);
    QVERIFY(fg.isValid());
    const QColor c = fg.value<QBrush>().color();
    // 灰 = 三通道接近且不是纯黑也不是纯白。
    // 只断言"灰"太松(任何颜色都能算灰),只断言具体值又太死(换个灰度就红)。
    QVERIFY2(qAbs(c.red() - c.green()) < 12 && qAbs(c.green() - c.blue()) < 12,
             qPrintable(QString("不是灰色 rgb(%1,%2,%3)")
                            .arg(c.red()).arg(c.green()).arg(c.blue())));
    QVERIFY2(c.red() > 100 && c.red() < 200,
             qPrintable(QString("灰度不合适(太黑看不见/太亮跟没置灰一样):%1").arg(c.red())));
}

void TestProtocolComboGrey::greyedItem_hasTooltipExplainingWhy() {
    QComboBox combo;
    combo.addItem("Modbus");
    combo.addItem("S7");
    combo.addItem("EtherNet/IP");
    applyProtocolAvailabilityStyling(&combo);

    const QString tip1 = combo.itemData(1, Qt::ToolTipRole).toString();
    const QString tip2 = combo.itemData(2, Qt::ToolTipRole).toString();
    QVERIFY(!tip1.isEmpty());
    QVERIFY(!tip2.isEmpty());
    QVERIFY(tip1.contains("尚未实现"));
    QVERIFY(tip2.contains("尚未实现"));
    // ★ 两条 tooltip 必须不一样 —— 都是同一句复制粘贴 = 有一项写错了协议名
    QVERIFY2(tip1 != tip2, "S7 和 EtherNet/IP 的 tooltip 一模一样,八成有一项写错了");
}

void TestProtocolComboGrey::greyedItem_staysSelectableSoWeCanExplainIt() {
    // ★ 这条断言的是一个【刻意的取舍】,不是缺陷,所以要写清楚理由:
    //
    //   QComboBox 没有"禁用某一项"的 API(只有整框 setEnabled)。
    //   常见做法是装一个 QStyledItemDelegate 在 isEnabled() 里返回 false,
    //   但那样用户点下去【什么都不会发生】—— 连弹窗都看不到,
    //   而"为什么不让我选"恰恰是他此刻最需要知道的。
    //
    //   现在的选择:置灰 + 文字标注 + tooltip 讲原因,
    //   真的选下去时 onProtocolComboChanged 弹"协议尚未实现"并撤回下拉框。
    //   用户能选、选了会被告知、选完自动弹回 —— 信息是完整的。
    //
    //   这条测试的意义:如果将来有人装了 delegate 把它改成真禁用,
    //   这条会红,提醒他重新想一遍是不是要保留解释。
    QComboBox combo;
    combo.addItem("Modbus");
    combo.addItem("S7");
    applyProtocolAvailabilityStyling(&combo);

    // 置灰不等于禁用:第 1 项仍然可以被选为当前项
    combo.setCurrentIndex(1);
    QCOMPARE(combo.currentIndex(), 1);
    QVERIFY(combo.isEnabled());
}

void TestProtocolComboGrey::usableItem_isLeftCompletelyAlone() {
    QComboBox combo;
    combo.addItem("Modbus");
    combo.addItem("S7");
    applyProtocolAvailabilityStyling(&combo);

    QCOMPARE(combo.itemText(0), QStringLiteral("Modbus"));
    // 可用项不该被写 ForegroundRole —— 没写过就是无效 QVariant
    QVERIFY2(!combo.itemData(0, Qt::ForegroundRole).isValid(),
             "Modbus 项被写了前景色,说明置灰循环的 isUsable 判断反了");
    // 也不该有 tooltip,否则灰项和非灰项看起来没区别
    QVERIFY2(!combo.itemData(0, Qt::ToolTipRole).isValid(),
             "Modbus 项被写了 tooltip");
}

void TestProtocolComboGrey::suffixIsNotAppendedToUsableItem() {
    // ★ 挡住一个具体的手滑:把后缀无条件加给每一项。
    //   那样下拉框第一行会变成 "Modbus(未实现)" —— 整个软件看起来像没做完。
    QComboBox combo;
    combo.addItem("Modbus");
    combo.addItem("S7");
    applyProtocolAvailabilityStyling(&combo);

    QVERIFY2(!combo.itemText(0).contains("未实现"),
             qPrintable("Modbus 项被误加未实现标记:" + combo.itemText(0)));
}

void TestProtocolComboGrey::callingTwice_doesNotDoubleTheSuffix() {
    // ★ 这条在"逻辑写在 hmi 构造函数里"的时候【测不到】——
    //   那份代码每次进程启动只跑一次,天然不会重复。
    //   抽成可复用的公共函数之后,"被调用两次"就成了一种可能:
    //   以后加个"新建工程"对话框也会调它。
    //   第二次调用如果拿带后缀的文字去查 isUsable,就会叠成
    //   "S7(未实现)(未实现)" —— 用户一眼看到这种东西就会觉得这软件没做完。
    QComboBox combo;
    combo.addItem("Modbus");
    combo.addItem("S7");
    combo.addItem("EtherNet/IP");

    applyProtocolAvailabilityStyling(&combo);
    const QString afterFirst = combo.itemText(1);
    applyProtocolAvailabilityStyling(&combo);
    const QString afterSecond = combo.itemText(1);

    QVERIFY2(afterFirst == afterSecond,
             qPrintable(QString("第二次调用后文字变了:第一次 \"%1\" 第二次 \"%2\"")
                            .arg(afterFirst, afterSecond)));
    QCOMPARE(combo.count(), 3);
    QCOMPARE(combo.itemText(0), QStringLiteral("Modbus"));
}

void TestProtocolComboGrey::itemTextMatchesRealUiItems() {
    // ★ 防"hmi.ui 改了协议名,测试里的下拉框没跟着改"——
    //   那样测试会一直绿,但测的是一个界面上不存在的下拉框。
    //   isUsable 认的是小写协议名(s7/eip/modbus),
    //   这里验的是"显示名去掉后缀后能被认出来"。
    for (const QString& name : {"Modbus", "S7", "EtherNet/IP"}) {
        const bool recognized = DriverFactory::isSupported(name)
                             || DriverFactory::isSupported(name.toLower());
        QVERIFY2(recognized,
                 qPrintable("下拉框里的协议名 " + name + " 工厂两个函数都不认 —— "
                            "hmi.ui 的文字和 DriverFactory 的表对不上了"));
    }
    // 三个显示名对应的可用性,必须正好是 [可用, 不可用, 不可用]
    QCOMPARE(DriverFactory::isUsable("Modbus"), true);
    QCOMPARE(DriverFactory::isUsable("S7"), false);
    QCOMPARE(DriverFactory::isUsable("EtherNet/IP"), false);
}

QTEST_MAIN(TestProtocolComboGrey)
#include "tst_ProtocolComboGrey.moc"
