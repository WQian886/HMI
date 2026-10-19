// ============================================================================
// 阶段 C · C.2 · S7 协议 Driver 实现(Snap7 LGPL 包装层)
//
// 当前状态(C.2.1 骨架):
//   - 所有 IPollDriver 接口已最小实现(返回 false / 0),后续 C.2.2-2.5 填充
//   - Snap7 DLL 通过 QLibrary::load 动态加载;失败时不崩(返回 false)
//   - 字节序 / 4 area 映射辅助函数已就位,等待 connectTo/sendReadRequest 实现
//
// 设计要点(对应教训 + 规划 C.2 节):
//   - QLibrary 动态加载:LGPL 合规硬要求(规划 C.2 第 449-450 行)
//   - 1 driver = 1 PLC(教训 1.5)
//   - 错误处理:Snap7 错误码 → IPollDriver::errorOccurred(教训 1.6)
//   - 字节序:Modbus 大端 / S7 大端一致,但 PLC 内部按字存(规划 C.2 第 264 行)
//
// 后续任务:
//   - C.2.2:补 QLibrary::resolve 函数指针逻辑
//   - C.2.4:connectTo / disconnect 真连 S7-PLCSIM
//   - C.2.5:sendReadRequest / sendWriteRequest 真读写
//   - C.2.6:错误码映射
//
// 关联:S7Driver.h, IPollDriver.h, ModbusDriver.cpp(对照风格)
// ============================================================================

#include "drivers/S7Driver.h"

#include <QDebug>

namespace qwhmi::drivers {

// ============================================================================
// 构造 / 析构
// ============================================================================

S7Driver::S7Driver(QObject* parent)
    : IPollDriver(parent)
{
    // C.2.2 TODO: 构造时尝试 loadSnap7Library()(但失败不应崩,留给 connectTo 再试)
    //   暂时不加载,因为很多环境(如开发机)可能没装 Snap7
}

S7Driver::~S7Driver()
{
    if (client_) {
        if (pCli_Destroy) {
            pCli_Destroy(client_);
        }
        client_ = nullptr;
    }
    unloadSnap7Library();
}

// ============================================================================
// IPollDriver 接口 - C.2.1 骨架(后续小步填充)
// ============================================================================

void S7Driver::connectTo(const QString& host, quint16 port)
{
    host_ = host;
    port_ = port;

    // C.2.4 TODO: 真 Snap7 连接逻辑
    //   1. 加载 DLL(如果还没)
    //   2. Cli_Create 创建 client
    //   3. Cli_Connect(host, port, rack, slot) — S7 用 ISO-TSAP 102
    //   4. 成功 → emit connected(); 失败 → emit errorOccurred()
    qWarning() << "[S7Driver] connectTo C.2.1 stub:" << host << ":" << port
               << "(真连接在 C.2.4 实现)";
    emit errorOccurred(QStringLiteral("S7Driver.connectTo: C.2.1 stub - 真连接在 C.2.4 实现"));
}

void S7Driver::disconnect()
{
    if (client_ && pCli_Disconnect) {
        pCli_Disconnect(client_);
    }
    emit disconnected();
}

bool S7Driver::isConnected() const
{
    if (!client_ || !pCli_GetConnected) return false;
    return pCli_GetConnected(client_) == 1;
}

int S7Driver::pendingCount() const
{
    // C.2.5 TODO:Snap7 支持多 in-flight(协议层允许多请求并行)
    //   简单实现:先固定返回 0,后续用 std::deque<requestId> 跟踪
    return 0;
}

// ★ 2b(2026-10-19):返回 bool。stub 会立刻 emit readFinished(false),
//   即"接收了请求、失败通过信号报",所以返回 true 而不是 false。
//   返回 false 的语义是"拒绝,不会有 readFinished" —— 两者不能混,
//   否则 PollScheduler 会去等一个永远不来的 reply。
bool S7Driver::sendReadRequest(const ReadRequest& req)
{
    // C.2.5 TODO:Cli_ReadArea(area, dbNumber, start, amount, wordLen, buffer)
    Q_UNUSED(req);
    qWarning() << "[S7Driver] sendReadRequest C.2.1 stub";
    emit readFinished(req.requestId, QVector<quint16>{}, false);
    return true;
}

bool S7Driver::sendWriteRequest(RegisterArea area, int slaveId, int addr,
                                const QVector<quint16>& values, quint32 requestId)
{
    // C.2.5 TODO:Cli_WriteArea(area, dbNumber, start, amount, wordLen, buffer)
    Q_UNUSED(area);
    Q_UNUSED(slaveId);
    Q_UNUSED(addr);
    Q_UNUSED(values);
    qWarning() << "[S7Driver] sendWriteRequest C.2.1 stub";
    emit writeFinished(requestId, false);
    return false;
}

// ============================================================================
// Snap7 库加载/卸载(C.2.1 占位,C.2.2 完善)
// ============================================================================

bool S7Driver::loadSnap7Library()
{
    // C.2.2 TODO: 实际 QLibrary::resolve 函数指针
    //   1. snap7Lib_.setFileName("snap7") 或 "snap7.dll"
    //   2. snap7Lib_.load()
    //   3. resolve 7 个函数指针(Cli_Create/Destroy/Connect/Disconnect/ReadArea/WriteArea/GetConnected)
    //   4. 任一失败 → emit errorOccurred + return false
    qWarning() << "[S7Driver] loadSnap7Library C.2.1 stub";
    return false;
}

void S7Driver::unloadSnap7Library()
{
    if (snap7Lib_.isLoaded()) {
        snap7Lib_.unload();
    }
    pCli_Create       = nullptr;
    pCli_Destroy      = nullptr;
    pCli_Connect      = nullptr;
    pCli_Disconnect   = nullptr;
    pCli_ReadArea     = nullptr;
    pCli_WriteArea    = nullptr;
    pCli_GetConnected = nullptr;
}

// ============================================================================
// 辅助方法
// ============================================================================

int S7Driver::areaToSnap7Area(RegisterArea area)
{
    // Snap7 area codes:
    //   S7AreaPE = 0x81   Process Inputs(MODBUS InputReg)
    //   S7AreaPA = 0x82   Process Outputs(MODBUS HoldingReg / Coils)
    //   S7AreaMK = 0x83   Merkers(MODBUS HoldingReg 映射)
    //   S7AreaDB = 0x84   Data Blocks(MODBUS HoldingReg 通用)
    //   S7AreaCT = 0x1C   Counters
    //   S7AreaTM = 0x1D   Timers
    switch (area) {
        case RegisterArea::InputReg:      return 0x81;  // PE
        case RegisterArea::HoldingReg:    return 0x84;  // DB(默认 DB)
        case RegisterArea::Coils:         return 0x82;  // PA
        case RegisterArea::DiscreteInput: return 0x81;  // PE
    }
    return 0x84;  // 默认 DB
}

quint16 S7Driver::byteSwap16(quint16 v)
{
    return quint16((v >> 8) | (v << 8));
}

void S7Driver::byteSwapBuffer(quint16* buf, int count)
{
    for (int i = 0; i < count; ++i) {
        buf[i] = byteSwap16(buf[i]);
    }
}

} // namespace qwhmi::drivers
