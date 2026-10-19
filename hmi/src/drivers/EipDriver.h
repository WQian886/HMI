// ============================================================================
// 阶段 C · C.3 · EIP(EtherNet/IP)协议 Driver(包装 libplctag LGPL)
//
// 文件职责:实现 IPollDriver 接口,内部 hold 一个 libplctag tag handle,
//   把通用 IPollDriver 调用映射到 libplctag C++ 函数(plc_tag_create/read/write/status)。
//
// 关键设计(对应教训 + 规划 C.3 节):
//   - plc_tag_t(实际是 int)用 void* 包,避免头文件污染(libplctag 是 C++ 库)
//   - QLibrary 动态加载 + 函数指针解析,LGPL 动态链接合规
//   - 1 driver = 1 PLC(CIP connection per driver,教训 1.5)
//   - libplctag 是异步 API → C.3.4 用 QEventLoop 转同步(规划 C.3 第 307 行 + 5.3 节)
//   - 4 种 RegisterArea → 主要 HoldingReg(DINT[]),其他 3 种 EIP 支持有限(规划 C.3 第 302 行)
//   - Qt 抽象类三件套:default 构造 + QObject* parent 构造 + 空 .cpp(教训 1.3/1.4)
//
// 关联:IPollDriver.h, DriverFactory.h, ModbusDriver.h, S7Driver.h
// 依赖:libplctag LGPL-2.1(https://github.com/libplctag/libplctag) 放 hmi/3rdparty/libplctag/
// ============================================================================

#pragma once

#include "drivers/IPollDriver.h"

#include <QLibrary>
#include <QString>
#include <QTimer>

class QEventLoop;

namespace qwhmi::drivers {

class EipDriver : public IPollDriver {
    Q_OBJECT
public:
    // Qt 抽象类三件套 — Q_OBJECT + virtual 析构 → 必须显式 default + parent 构造
    explicit EipDriver(QObject* parent = nullptr);
    ~EipDriver() override;

    // ===== IPollDriver 接口实现 =====
    void connectTo(const QString& host, quint16 port) override;
    void disconnect() override;
    bool isConnected() const override;
    int pendingCount() const override;
    // ★ 2b(2026-10-19):返回 bool(是否被接收),理由见 IPollDriver.h 同名函数注释
    bool sendReadRequest(const ReadRequest& req) override;
    bool sendWriteRequest(RegisterArea area, int slaveId, int addr,
                          const QVector<quint16>& values, quint32 requestId) override;
    QString protocolName() const override { return QStringLiteral("eip"); }

    // ===== C.3 测试钩子(给 tst_EipDriver 用,Mock libplctag)=====
    int   libplctagTag() const { return tag_; }   // plc_tag_t 是 int,不是指针
    int   timeoutMs() const { return timeoutMs_; }

private:
    // ===== libplctag 函数指针类型定义 =====
    //   libplctag 是 C++ 库,API 是 C++ 函数;QLibrary::resolve 拿函数指针
    //   plc_tag_t = int32_t,但我们用 void* 避免头污染
    using PlcTagCreateFunc  = int  (*)(const char*, int, const char*);
    using PlcTagDestroyFunc = void (*)(int);
    using PlcTagReadFunc    = int  (*)(int, int);
    using PlcTagWriteFunc   = int  (*)(int, int);
    using PlcTagStatusFunc  = int  (*)(int);
    using PlcTagGetIntAttrFunc = int (*)(int, const char*, int*);
    using PlcTagSetIntAttrFunc = int (*)(int, const char*, int);
    using PlcTagAbortFunc   = void (*)(int);

    // ===== libplctag 函数指针(动态解析)=====
    PlcTagCreateFunc       pPlcTagCreate       = nullptr;
    PlcTagDestroyFunc      pPlcTagDestroy      = nullptr;
    PlcTagReadFunc         pPlcTagRead         = nullptr;
    PlcTagWriteFunc        pPlcTagWrite        = nullptr;
    PlcTagStatusFunc       pPlcTagStatus       = nullptr;
    PlcTagGetIntAttrFunc   pPlcTagGetIntAttr   = nullptr;
    PlcTagSetIntAttrFunc   pPlcTagSetIntAttr   = nullptr;
    PlcTagAbortFunc        pPlcTagAbort        = nullptr;

    // ===== 内部状态 =====
    QLibrary libplctagLib_;   // 动态加载 libplctag.dll
    int      tag_ = 0;         // plc_tag_t(0 表示未创建)
    QString  host_;
    quint16  port_ = 44818;    // EIP 默认端口(TCP/44818)
    int      timeoutMs_ = 5000; // 异步操作超时

    // ===== 辅助方法 =====
    bool loadLibplctagLibrary();      // 加载 DLL + resolve 函数指针
    void unloadLibplctagLibrary();

    // CIP 标签名构造(规划 C.3 第 306 行)
    //   addr 参数:用作 tag_name 索引(简化映射,C.3.4 真接 Logix 时细化)
    //   返回值示例:"N7:0" / "MyTag"
    static QString buildTagName(RegisterArea area, int slaveId, int addr);

    // 异步轮询辅助(规划 C.3 第 5.3 节 QEventLoop 模式)
    bool waitForTagCompletion(int timeoutMs);
};

} // namespace qwhmi::drivers
