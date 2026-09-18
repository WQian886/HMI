// ============================================================================
// 阶段 C · C.2 · S7 协议 Driver(包装 Snap7 LGPL 动态库)
//
// 文件职责:实现 IPollDriver 接口,内部 hold 一个 Snap7 S7Client 句柄,
//   把通用 IPollDriver 调用映射到 Snap7 C 函数(Cli_Connect / Cli_ReadArea 等)。
//
// 关键设计(对应教训 + 规划 C.2 节):
//   - S7Client* 用 void* 包,避免头文件污染(Snap7 是 C 库,头文件含 C 语法)
//   - QLibrary 动态加载 + 函数指针解析,LGPL 动态链接合规
//   - 1 driver = 1 PLC(教训 1.5:Snap7 单 client 只能连 1 台)
//   - 4 种 RegisterArea → Snap7 area 编号(S7AreaDB / PE / PA / MK / CT / TM)
//   - 字节序:Modbus 大端 / S7 大端一致,但 PLC 内部按字节序存,需 byte-swap
//   - Qt 抽象类三件套:default 构造 + QObject* parent 构造 + 空 .cpp(教训 1.3/1.4)
//
// 关联:IPollDriver.h, DriverFactory.h, ModbusDriver.h
// 依赖:Snap7 LGPL(http://snap7.sourceforge.net/) 放 hmi/3rdparty/snap7/
// ============================================================================

#pragma once

#include "drivers/IPollDriver.h"

#include <QLibrary>   // 动态加载 snap7.dll
#include <QString>

namespace qwhmi::drivers {

class S7Driver : public IPollDriver {
    Q_OBJECT
public:
    // Qt 抽象类三件套 — Q_OBJECT + virtual 析构 → 必须显式 default + parent 构造
    explicit S7Driver(QObject* parent = nullptr);
    ~S7Driver() override;

    // ===== IPollDriver 接口实现 =====
    void connectTo(const QString& host, quint16 port) override;
    void disconnect() override;
    bool isConnected() const override;
    int pendingCount() const override;
    void sendReadRequest(const ReadRequest& req) override;
    bool sendWriteRequest(RegisterArea area, int slaveId, int addr,
                          const QVector<quint16>& values, quint32 requestId) override;
    QString protocolName() const override { return QStringLiteral("s7"); }

    // ===== C.2 测试钩子(给 tst_S7Driver 用,Mock Snap7)=====
    //   返回内部 Snap7 client 句柄,只读
    void* snap7Client() const { return client_; }

private:
    // ===== Snap7 函数指针类型定义 =====
    //   Snap7 是 C 库,所有 API 是 C 函数;我们用函数指针 + QLibrary::resolve 拿
    using Cli_CreateFunc  = void* (*)();
    using Cli_DestroyFunc = void  (*)(void*);
    using Cli_ConnectFunc = int   (*)(void*, const char*, int, int);
    using Cli_DisconnectFunc = int (*)(void*);
    using Cli_ReadAreaFunc = int  (*)(void*, int, int, int, int, int, void*);
    using Cli_WriteAreaFunc = int (*)(void*, int, int, int, int, int, void*);
    using Cli_GetConnectedFunc = int (*)(void*);

    // ===== Snap7 函数指针(动态解析)=====
    Cli_CreateFunc       pCli_Create       = nullptr;
    Cli_DestroyFunc      pCli_Destroy      = nullptr;
    Cli_ConnectFunc      pCli_Connect      = nullptr;
    Cli_DisconnectFunc   pCli_Disconnect   = nullptr;
    Cli_ReadAreaFunc     pCli_ReadArea     = nullptr;
    Cli_WriteAreaFunc    pCli_WriteArea    = nullptr;
    Cli_GetConnectedFunc pCli_GetConnected = nullptr;

    // ===== 内部状态 =====
    QLibrary snap7Lib_;     // 动态加载 snap7.dll
    void*    client_ = nullptr;  // S7Client*(void* 避免头污染)
    QString  host_;          // 保存 host 给断线重连
    quint16  port_ = 102;    // S7 默认端口 ISO-TSAP

    // ===== 辅助方法 =====
    bool loadSnap7Library();      // 加载 DLL + resolve 函数指针
    void unloadSnap7Library();    // 卸载

    // RegisterArea → Snap7 area number(S7AreaDB=0x84 / PE=0x81 / PA=0x82 / MK=0x83)
    static int areaToSnap7Area(RegisterArea area);

    // 字节序转换(quint16 host byte order ↔ S7 big-endian)
    static quint16 byteSwap16(quint16 v);
    static void   byteSwapBuffer(quint16* buf, int count);
};

} // namespace qwhmi::drivers
