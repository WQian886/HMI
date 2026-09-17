// ============================================================================
// 阶段 C · C.1 · DriverFactory 实现
//
// 关联:DriverFactory.h
// ============================================================================

#include "drivers/DriverFactory.h"
#include "drivers/IPollDriver.h"
#include "drivers/ModbusDriver.h"

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
    // C.2 / C.3 完成后启用:
    //   if (p == "s7") return new S7Driver(parent);
    //   if (p == "eip" || p == "ethernet_ip") return new EipDriver(parent);

    qCWarning(hmiDriver) << "DriverFactory:不支持的协议:" << protocolName
                          << "(C.1 阶段只支持 modbus,S7 见 C.2,EIP 见 C.3)";
    return nullptr;
}

bool DriverFactory::isSupported(const QString& protocolName) {
    const QString p = protocolName.trimmed().toLower();
    return (p == "modbus" || p == "modbus_tcp"
            // || p == "s7" || p == "eip" || p == "ethernet_ip"  // C.2/C.3 启用
            );
}

} // namespace qwhmi::drivers
