// ============================================================================
// 阶段 C · C.1 · DriverFactory 实现
//
// 关联:DriverFactory.h
// ============================================================================

#include "drivers/DriverFactory.h"
#include "drivers/IPollDriver.h"
#include "drivers/ModbusDriver.h"
#include "drivers/S7Driver.h"     // C.2 S7 协议适配器(2026-09-18)
#include "drivers/EipDriver.h"    // C.3 EIP 协议适配器(2026-09-18)

#include <QObject>
#include <QLoggingCategory>
#include <QStringList>

Q_LOGGING_CATEGORY(hmiDriver, "hmi.driver")

namespace qwhmi::drivers {

namespace {

// ★ 下拉框里给置灰项加的后缀。ProtocolAvailabilityUi 那边用同一个常量
//   加后缀,这边用同一个常量拆后缀 —— 放在一处,两边不会走偏。
//   【为什么不干脆不改文字】只靠灰色太隐蔽,用户看不出这一项跟别的有什么不同。
//
// ★ 这个后缀是 UTF-8 中文,不能用 QLatin1String / fromLatin1 拼 ——
//   那两个按"一个字节一个字符"解释,中文会被拆成乱码。
//   第一版就是这么写的,下拉框里真的显示成了 "S7(??a?????°)",
//   被 tst_ProtocolComboGrey 抓到。带中文的界面文字值得有一条测试盯着。
const QString& unimplementedSuffix() {
    static const QString sfx = QStringLiteral("(未实现)");
    return sfx;
}

// 拆掉装饰后缀,还原成协议名本身。半角/全角括号都认。
QString stripDisplaySuffix(const QString& name) {
    static const QStringList suffixes = {
        QStringLiteral("(未实现)"), QStringLiteral("（未实现）"),
    };
    QString out = name.trimmed();
    for (const QString& sfx : suffixes) {
        while (out.endsWith(sfx)) {   // while:防重复调用叠成 "S7(未实现)(未实现)"
            out.chop(sfx.size());
            out = out.trimmed();
        }
    }
    return out;
}

} // namespace

// 给 ProtocolAvailabilityUi.cpp 用 —— 后缀只此一份
const QString& protocolUnimplementedSuffix() { return unimplementedSuffix(); }

int findProtocolItemIndex(const QStringList& itemTexts, const QString& protocolId) {
    const QString canonical = DriverFactory::normalizeProtocolName(protocolId);
    for (int i = 0; i < itemTexts.size(); ++i) {
        if (DriverFactory::normalizeProtocolName(itemTexts.at(i)) == canonical) {
            return i;
        }
    }
    return -1;
}

IPollDriver* DriverFactory::create(const QString& protocolName, QObject* parent) {
    const QString p = normalizeProtocolName(protocolName);

    if (p == "modbus" || p == "modbus_tcp") {
        qCInfo(hmiDriver) << "DriverFactory:创建 ModbusDriver";
        return new ModbusDriver(parent);
    }
    // C.2 启用:S7 协议(Snap7 适配器,2026-09-18 落地骨架,真连接在 C.2.4 实现)
    if (p == "s7") {
        qCInfo(hmiDriver) << "DriverFactory:创建 S7Driver(Snap7 适配器,C.2 骨架)";
        return new S7Driver(parent);
    }
    // C.3 启用:EIP 协议(libplctag 适配器,2026-09-18 落地骨架,真连接在 C.3.4 实现)
    if (p == "eip" || p == "ethernet_ip") {
        qCInfo(hmiDriver) << "DriverFactory:创建 EipDriver(libplctag 适配器,C.3 骨架)";
        return new EipDriver(parent);
    }

    qCWarning(hmiDriver) << "DriverFactory:不支持的协议:" << protocolName
                          << "(已支持 modbus/s7/eip)";
    return nullptr;
}

// ★ normalizeProtocolName:把【显示名】和【协议 ID】归一到同一个词。
//
//   【为什么需要它 —— 这是被测试逼出来的,不是我设计出来的】
//     hmi.ui 里的下拉项显示 "EtherNet/IP",而工厂认的是 "eip" / "ethernet_ip"。
//     显示名小写化是 "ethernet/ip",两边对不上:
//       isSupported("EtherNet/IP") → false
//     更要命的是 onProtocolComboChanged 撤回下拉框时用的是
//     findText(current, Qt::MatchFixedString) —— current 存的是协议 ID,
//     拿 ID 去显示名里找,找不到就 idx<0,【下拉框不会弹回去】,
//     用户会看到一个"切不过去、也弹不回来"的死界面。
//
//     这个坑之前没被发现,是因为 hmi.cpp:196 的 DriverFactory::create 是
//     硬编码 "modbus",S7/EIP 分支从来没真的跑起来过。
//     等 v0.3.0 真接上 S7/EIP,这个 bug 才会第一次显形 —— 那时候再查就晚了。
QString DriverFactory::normalizeProtocolName(const QString& protocolName) {
    QString p = stripDisplaySuffix(protocolName).trimmed().toLower();
    // EtherNet/IP 的各种写法 → 统一到 "eip"(短的那个,和 S7 一样是缩写)
    if (p == "ethernet/ip" || p == "ethernet-ip" || p == "ethernetip"
        || p == "ethernet_ip") {
        return QStringLiteral("eip");
    }
    if (p == "modbus/tcp" || p == "modbus-tcp" || p == "modbus_ip") {
        return QStringLiteral("modbus_tcp");
    }
    return p;
}

bool DriverFactory::isSupported(const QString& protocolName) {
    const QString p = normalizeProtocolName(protocolName);
    return (p == "modbus" || p == "modbus_tcp"
            || p == "s7"
            || p == "eip" || p == "ethernet_ip"
            );
}

// ★ isUsable:与 isSupported 的区别就是本文件 2026-10-10 那次修复的全部内容。
//
//   【为什么必须分成两个】
//     isSupported 回答"工厂认不认识这个名字" —— s7/eip 有骨架,认识。
//     isUsable    回答"真能连上吗"           —— s7/eip 至今是 qWarning+emit errorOccurred。
//   以前 UI 拿 isSupported 当"能不能选"用,于是两个 stub 被当成正常协议摆在下拉框里,
//   用户选下去只会看到"协议不兼容,不能切换" —— 界面从头到尾没说过它们没实现。
//   这是典型的"界面在撒谎",跟 AlarmHistoryView 注释里记的是同一类问题。
bool DriverFactory::isUsable(const QString& protocolName) {
    const QString p = normalizeProtocolName(protocolName);
    // v0.2.0 只发 Modbus —— Wqian 2026-09-29 拍板,S7/EIP 推 v0.3.0。
    // 真接的时候这里加一行,UI 那边不用动(它是查表置灰的)。
    return (p == "modbus" || p == "modbus_tcp");
}

QString DriverFactory::unavailableReason(const QString& protocolName) {
    if (isUsable(protocolName)) {
        return QString();
    }
    const QString p = normalizeProtocolName(protocolName);
    if (p == "s7") {
        return QStringLiteral("S7 尚未实现:Snap7 适配器目前只有骨架,"
                              "真连接排期 v0.3.0。");
    }
    if (p == "eip" || p == "ethernet_ip") {
        return QStringLiteral("EtherNet/IP 尚未实现:libplctag 适配器目前只有骨架,"
                              "真连接排期 v0.3.0。");
    }
    return QStringLiteral("未知协议:%1").arg(stripDisplaySuffix(protocolName));
}

} // namespace qwhmi::drivers
