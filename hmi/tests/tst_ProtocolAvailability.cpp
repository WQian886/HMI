// ============================================================================
// 2026-10-10 · 协议可用性 —— 修"下拉框把没实现的协议摆成正常选项"
//
// 【这条测试是从一次代码审计里长出来的,不是先写测试再补功能】
//   审计发现:UI 的 S7 / EtherNet/IP 两项没有任何置灰或"未实现"标注,
//   而 S7Driver/EipDriver 至今是 qWarning + emit errorOccurred 的骨架。
//   用户选下去只会看到"协议不兼容,不能切换" —— 界面从没说过它们没实现。
//   Wqian 2026-09-29 已拍板:"把两个 stub 从假可用改成 UI 上明确禁用/置灰"。
//
// 【为什么这些断言长得像在测字符串】
//   因为修法就是查表:isUsable() 返回 false 的项,UI 负责置灰。
//   表错了 UI 就错,所以表本身就是被测对象。
//
// 关联:drivers/DriverFactory.{h,cpp}, hmi.cpp 构造函数的置灰循环
// ============================================================================

#include <QtTest>
#include "drivers/DriverFactory.h"

using namespace qwhmi::drivers;

class TestProtocolAvailability : public QObject {
    Q_OBJECT
private slots:
    // ---- isUsable:v0.2.0 只发 Modbus ----
    void modbus_isUsable();
    void s7_isNotUsable();
    void eip_isNotUsable();

    // ---- 大小写 / 空格:和 isSupported 保持同一套宽容度 ----
    void isUsable_isCaseAndWhitespaceTolerant();

    // ---- ★ 这条是修 UI 的核心:UI 拿到的是带后缀的显示文本 ----
    void unusableItemText_withSuffix_stillResolvesToProtocol();

    // ---- unavailableReason:置灰项的 tooltip 必须是"人话" ----
    void unavailableReason_explainsWhy_notJustThatItFails();
    void unavailableReason_isEmptyForUsableProtocols();

    // ---- ★ isSupported 和 isUsable 必须同时存在且不一致 ----
    //   把这条删掉,"两个函数慢慢长成一样"就没人拦了
    void isSupported_true_but_isUsable_false_isTheWholePoint();
};

void TestProtocolAvailability::modbus_isUsable() {
    QVERIFY(DriverFactory::isUsable("modbus"));
    QVERIFY(DriverFactory::isUsable("modbus_tcp"));
}

void TestProtocolAvailability::s7_isNotUsable() {
    // ★ 这条红了 = 有人把 S7 从置灰表里拿掉了。
    //   拿掉之前先确认 S7Driver::connectTo 是不是真实现了(现在是 qWarning + emit errorOccurred)。
    QVERIFY(!DriverFactory::isUsable("s7"));
}

void TestProtocolAvailability::eip_isNotUsable() {
    QVERIFY(!DriverFactory::isUsable("eip"));
    QVERIFY(!DriverFactory::isUsable("ethernet_ip"));
}

void TestProtocolAvailability::isUsable_isCaseAndWhitespaceTolerant() {
    // 下拉框给的是显示文本,不是规范化后的协议名
    QVERIFY(DriverFactory::isUsable("Modbus"));
    QVERIFY(DriverFactory::isUsable("MODBUS"));
    QVERIFY(DriverFactory::isUsable(" modbus "));
    QVERIFY(!DriverFactory::isUsable(" S7 "));
}

void TestProtocolAvailability::unusableItemText_withSuffix_stillResolvesToProtocol() {
    // ★ 这条对应真实路径:UI 把项显示成 "S7(未实现)" 之后,
    //   currentTextChanged 传过来的就是 "S7(未实现)",不是 "S7"。
    //   如果 isUsable 不认后缀,置灰项被选中时会被当成"未知协议",
    //   tooltip 会显示"未知协议:S7(未实现)" —— 这种话用户看不懂。
    QVERIFY(!DriverFactory::isUsable("S7(未实现)"));
    QVERIFY(!DriverFactory::isUsable("EtherNet/IP(未实现)"));

    const QString reason = DriverFactory::unavailableReason("S7(未实现)");
    QVERIFY2(!reason.contains("未知协议"),
             qPrintable("带(未实现)后缀却没认出是 S7,实际输出:" + reason));
    QVERIFY(reason.contains("S7"));
}

void TestProtocolAvailability::unavailableReason_explainsWhy_notJustThatItFails() {
    // ★ tooltip 的内容质量也是被测的。
    //   只写"不可用"等于没说 —— 用户悬停后还是不知道要等多久、为什么。
    const QString s7 = DriverFactory::unavailableReason("s7");
    QVERIFY(s7.contains("尚未实现"));
    QVERIFY(s7.contains("v0.3.0"));   // 给了档期,用户才知道不是被忘了

    const QString eip = DriverFactory::unavailableReason("eip");
    QVERIFY(eip.contains("尚未实现"));
    QVERIFY(eip.contains("v0.3.0"));
}

void TestProtocolAvailability::unavailableReason_isEmptyForUsableProtocols() {
    // 可用协议不该带 tooltip 说明 —— 否则灰项和非灰项看起来没区别,
    // 置灰就白做了
    QVERIFY(DriverFactory::unavailableReason("modbus").isEmpty());
    QVERIFY(DriverFactory::unavailableReason("Modbus").isEmpty());
}

void TestProtocolAvailability::isSupported_true_but_isUsable_false_isTheWholePoint() {
    // ★ 把两个函数的差别写死成断言。
    //   isSupported = "工厂认得这个名字"(s7/eip 有骨架,认)
    //   isUsable    = "真能连上"(s7/eip 是 stub,不能)
    //   当年 UI 拿 isSupported 当"能不能选"用,才把两个 stub 摆成了正常选项。
    //   这个用例就是防止下一次又有人混用。
    QVERIFY(DriverFactory::isSupported("s7"));
    QVERIFY(!DriverFactory::isUsable("s7"));
    QVERIFY(DriverFactory::isSupported("eip"));
    QVERIFY(!DriverFactory::isUsable("eip"));

    // Modbus 则是两个都 true —— 真正可用的协议不该有这层区别
    QVERIFY(DriverFactory::isSupported("modbus"));
    QVERIFY(DriverFactory::isUsable("modbus"));
}

QTEST_APPLESS_MAIN(TestProtocolAvailability)
#include "tst_ProtocolAvailability.moc"
