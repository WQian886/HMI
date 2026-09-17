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

class QObject;

namespace qwhmi::drivers {

class IPollDriver;

class DriverFactory {
public:
    // 根据协议名创建 driver
    //   "modbus" → ModbusDriver(IPollDriver*)
    //   "s7"     → C.2 实现(S7Driver),C.1 返回 nullptr + 错误日志
    //   "eip"    → C.3 实现(EipDriver),C.1 返回 nullptr + 错误日志
    // 失败(协议名不识别)返回 nullptr
    static IPollDriver* create(const QString& protocolName, QObject* parent = nullptr);

    // 是否支持某协议(给 UI 下拉框过滤用)
    static bool isSupported(const QString& protocolName);
};

} // namespace qwhmi::drivers
