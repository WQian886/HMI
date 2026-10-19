// ============================================================================
// 阶段 C · C.1 · Modbus 协议 Driver(包装现有 ModbusMaster)
//
// 文件职责:实现 IPollDriver 接口,内部 hold 一个 ModbusMaster*,
//   把通用 IPollDriver 调用映射到 ModbusMaster 的 QModbusDataUnit 接口。
//
// 关键约束:
//   - 100% 行为兼容:现有所有 PollScheduler 行为不变
//   - 内部不破坏 ModbusMaster 的单 in-flight 协议特性
//   - readFinished 信号从 QModbusDataUnit → QVector<quint16> 转译
//     (PollScheduler 不再需要 include QModbusDataUnit)
//
// 关联:IPollDriver.h, ModbusMaster.h
// ============================================================================

#pragma once

#include "drivers/IPollDriver.h"

#include <QModbusDataUnit>  // ★ C.1:onModbusReadFinished 签名用 QModbusDataUnit

class ModbusMaster;

namespace qwhmi::drivers {

class ModbusDriver : public IPollDriver {
    Q_OBJECT
public:
    explicit ModbusDriver(QObject* parent = nullptr);
    ~ModbusDriver() override;

    // IPollDriver 接口实现
    void connectTo(const QString& host, quint16 port) override;
    void disconnect() override;
    bool isConnected() const override;
    int pendingCount() const override;
    // ★ 2b(2026-10-19):返回 bool(是否被接收),理由见 IPollDriver.h 同名函数注释
    bool sendReadRequest(const ReadRequest& req) override;
    bool sendWriteRequest(RegisterArea area, int slaveId, int addr,
                          const QVector<quint16>& values, quint32 requestId) override;
    QString protocolName() const override { return QStringLiteral("modbus"); }

    // ★ C.1:内部访问,供测试 + PollScheduler 用
    ModbusMaster* inner() { return inner_; }

private slots:
    // ★ C.1 关键:把 ModbusMaster 的信号转成 IPollDriver 通用信号
    void onModbusReadFinished(quint32 requestId, QModbusDataUnit unit, bool success);
    void onModbusWriteFinished(quint32 requestId, bool success);

private:
    // ★ C.1 关键:RegisterArea → QModbusDataUnit::RegisterType 的映射
    static int areaToModbusType(RegisterArea area);

    ModbusMaster* inner_ = nullptr;
};

} // namespace qwhmi::drivers
