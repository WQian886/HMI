// ============================================================================
// 阶段 C · C.1 · Driver 工厂(协议选择 → 创建对应 driver)
//
// 文件职责:根据字符串("modbus"/"s7"/"eip")创建对应 IPollDriver 实例。
//
// 用法:
//   auto* driver = qwhmi::drivers::DriverFactory::create("modbus");
//   driver->connect("127.0.0.1", 502);
//
// 设计:工厂模式 + 字符串映射
//   - 字符串驱动:UI 下拉框 / 工程文件 / 命令行都能用
//   - 返回 IPollDriver*(抽象基类),调用方不知道具体协议
//   - C.1 阶段只实现 modbus(现有 ModbusDriver);S7/EIP 留 stub
//
// 关联:IPollDriver.h, ModbusDriver.h
// ============================================================================

#pragma once

#include <QString>
#include <QStringList>

class QObject;

namespace qwhmi::drivers {

class IPollDriver;

class DriverFactory {
public:
    // 根据协议名创建 driver
    //   "modbus" → ModbusDriver(IPollDriver*)
    //   "s7"     → S7Driver(骨架,真连接在 C.2.4)
    //   "eip"    → EipDriver(骨架,真连接在 C.3.4)
    // 失败(协议名不识别)返回 nullptr
    static IPollDriver* create(const QString& protocolName, QObject* parent = nullptr);

    // ★ 工厂认不认识这个名字(骨架也算"认识")
    //   【别拿它当"能不能用"】—— 2026-10-10 修的就是这个误解:
    //   它对 s7/eip 一直返回 true,而那两个至今是 stub。
    //   历史上 UI 读它来决定下拉框能不能选,于是 stub 被摆成了正常选项。
    //   要判断"能不能用"请用 isUsable()。
    static bool isSupported(const QString& protocolName);

    // ★ 真正可用(不是骨架才 true)。v0.2.0 只发 Modbus(Wqian 2026-09-29 拍板)。
    //   UI 置灰、启动告警一律以这个为准。
    static bool isUsable(const QString& protocolName);

    // 给 UI 显示的档期说明。isUsable() 为 true 时返回空串。
    //   用途:下拉框 tooltip —— 用户看到置灰项时,鼠标悬上去要告诉他为什么。
    static QString unavailableReason(const QString& protocolName);

    // ★ 显示名 / 协议 ID 归一。UI 显示 "EtherNet/IP"、工厂认 "ethernet_ip",
    //   不归一就对不上 —— 详见 .cpp 里那段的说明(含它会引发的下拉框弹不回问题)。
    static QString normalizeProtocolName(const QString& protocolName);
};

// 下拉框置灰项要加的后缀。只此一份,加和拆都用它。
const QString& protocolUnimplementedSuffix();

// ★ 在下拉框里找出"协议 ID == protocolId"的那一项,找不到返回 -1。
//
//   【为什么不能直接用 QComboBox::findText】
//     currentProtocol_ 存的是协议 ID("modbus"),下拉框里是显示名("Modbus"),
//     findText(str, Qt::MatchFixedString) 是大小写敏感的,查不到 → -1。
//     原来的撤回逻辑写成 findText(...) 然后 if (idx >= 0) setCurrentIndex(idx),
//     于是【撤回那几行从来没执行过】,下拉框一直停在用户刚选的那一项上,
//     而 currentProtocol_ 还是旧的 —— 界面显示的协议和实际用的对不上,且无任何日志。
//     这个 bug 一直没被发现,是因为 create() 硬编码 "modbus",
//     实际连的永远是 Modbus,界面和实际"碰巧"一致。
//
//   纯函数:不碰 QComboBox,只比字符串,所以能单测(见 tst_ProtocolComboGrey)。
int findProtocolItemIndex(const QStringList& itemTexts, const QString& protocolId);

} // namespace qwhmi::drivers
