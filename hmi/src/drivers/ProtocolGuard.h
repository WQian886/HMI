// ============================================================================
// 阶段 C · C.4 · 协议切换 UX 守门人(教训 1.5 跨协议切工程弹错误)
//
// 文件职责:提供纯逻辑函数 isCrossProtocolChange(expected, newProtocol),
//   让 GUI 层(hmi.cpp 的 protocolComboBox 槽)和单元测试(tst_ProtocolSwitch)
//   都能复用,避免把 UX 锁写死在 GUI 里导致无法单测。
//
// 设计:
//   - 纯函数,无 Qt 依赖(方便单测)
//   - 大小写不敏感("Modbus" / "modbus" / "MODBUS" 等价)
//   - 同协议(同字符串)返回 false(允许切换,只换 IP/Port)
//   - 跨协议返回 true(UI 应弹错误 + audit 记录)
//
// 关联:hmi.cpp::onProtocolComboChanged, tst_ProtocolSwitch, 规划 C.4 第 316-348 行
// ============================================================================

#pragma once

#include <QString>

namespace qwhmi::drivers {

inline bool isCrossProtocolChange(const QString& currentProtocol, const QString& newProtocol)
{
    return currentProtocol.trimmed().toLower() != newProtocol.trimmed().toLower();
}

} // namespace qwhmi::drivers
