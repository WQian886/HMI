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

Q_LOGGING_CATEGORY(hmiDriver, "hmi.driver")

namespace qwhmi::drivers {

IPollDriver* DriverFactory::create(const QString& protocolName, QObject* parent) {
    const QString p = protocolName.trimmed().toLower();

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

bool DriverFactory::isSupported(const QString& protocolName) {
    const QString p = protocolName.trimmed().toLower();
    return (p == "modbus" || p == "modbus_tcp"
            || p == "s7"
            || p == "eip" || p == "ethernet_ip"
            );
}

} // namespace qwhmi::drivers
