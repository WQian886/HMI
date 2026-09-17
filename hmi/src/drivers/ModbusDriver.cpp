// ============================================================================
// 阶段 C · C.1 · ModbusDriver 实现
//
// 关联:ModbusDriver.h
//
// 实现要点:
//   - 构造 new ModbusMaster(parent)
//   - connect 内部把 IPollDriver 信号 → ModbusMaster 信号
//   - sendReadRequest 把通用 ReadRequest 转成 QModbusDataUnit → ModbusMaster
//   - readFinished 从 QModbusDataUnit → QVector<quint16>(协议无关化)
//   - sendWriteRequest 同上(转译 RegisterArea)
// ============================================================================

#include "drivers/ModbusDriver.h"
#include "../../ModbusMaster.h"  // ModbusMaster.h 在 hmi/ 根,本 cpp 在 src/drivers/,用 ../ 相对路径

#include <QModbusDataUnit>

namespace qwhmi::drivers {

ModbusDriver::ModbusDriver(QObject* parent)
    : IPollDriver(parent)
    , inner_(new ModbusMaster(this))   // driver 接管生命周期
{
    // ★ C.1 关键:把 ModbusMaster 的 Modbus-特定信号转成 IPollDriver 通用信号
    //   - readFinished: QModbusDataUnit → QVector<quint16>
    //   - writeFinished: 透传(bool)
    //   - connected/disconnected/errorOccurred: 透传
    connect(inner_, &ModbusMaster::readFinished,
            this, &ModbusDriver::onModbusReadFinished);
    connect(inner_, &ModbusMaster::writeFinished,
            this, &ModbusDriver::onModbusWriteFinished);
    connect(inner_, &ModbusMaster::connected,
            this, &IPollDriver::connected);
    connect(inner_, &ModbusMaster::disconnected,
            this, &IPollDriver::disconnected);
    connect(inner_, &ModbusMaster::errorOccurred,
            this, &IPollDriver::errorOccurred);
}

ModbusDriver::~ModbusDriver() = default;

void ModbusDriver::connectTo(const QString& host, quint16 port) {
    // IPollDriver 接口:connectTo 是异步的(不会等连接成功)
    // 真实连接成功通过 connected() 信号通知
    inner_->connectTo(host, port);
}

void ModbusDriver::disconnect() {
    inner_->disconnect();   // ★ C.1:ModbusMaster 新加 disconnect 方法,内部 tcp_->close()
}

bool ModbusDriver::isConnected() const {
    return inner_->isConnected();
}

int ModbusDriver::pendingCount() const {
    return inner_->pendingCount();
}

// ★ C.1 关键:RegisterArea → QModbusDataUnit::RegisterType 映射
//   HoldingReg ↔ HoldingRegisters
//   InputReg ↔ InputRegisters
//   Coils ↔ Coils
//   DiscreteInput ↔ DiscreteInputs
int ModbusDriver::areaToModbusType(RegisterArea area) {
    switch (area) {
        case RegisterArea::HoldingReg:    return QModbusDataUnit::HoldingRegisters;
        case RegisterArea::InputReg:      return QModbusDataUnit::InputRegisters;
        case RegisterArea::Coils:         return QModbusDataUnit::Coils;
        case RegisterArea::DiscreteInput: return QModbusDataUnit::DiscreteInputs;
    }
    return QModbusDataUnit::HoldingRegisters;   // fallback
}

void ModbusDriver::sendReadRequest(const ReadRequest& req) {
    QModbusDataUnit::RegisterType type =
        static_cast<QModbusDataUnit::RegisterType>(areaToModbusType(req.area));
    QModbusDataUnit unit(type, static_cast<quint16>(req.addr), static_cast<quint16>(req.count));
    inner_->sendReadRequest(unit, req.slaveId, req.requestId);
}

bool ModbusDriver::sendWriteRequest(RegisterArea area, int slaveId, int addr,
                                     const QVector<quint16>& values, quint32 requestId) {
    QModbusDataUnit::RegisterType type =
        static_cast<QModbusDataUnit::RegisterType>(areaToModbusType(area));
    QModbusDataUnit unit(type, static_cast<quint16>(addr), values);
    QModbusReply* reply = inner_->sendWriteRequest(unit, slaveId, requestId);
    return reply != nullptr;   // 跟 ModbusMaster 旧 sendWriteRequest 行为一致
}

// ★ C.1 关键转译:QModbusDataUnit → QVector<quint16>
//   PollScheduler 不再需要 include QModbusDataUnit,只看到 raw 16-bit 数组
void ModbusDriver::onModbusReadFinished(quint32 requestId, QModbusDataUnit unit, bool success) {
    QVector<quint16> registers;
    if (success) {
        registers.reserve(unit.valueCount());
        for (int i = 0; i < unit.valueCount(); ++i) {
            registers.append(unit.value(i));
        }
    }
    emit readFinished(requestId, registers, success);
}

void ModbusDriver::onModbusWriteFinished(quint32 requestId, bool success) {
    emit writeFinished(requestId, success);
}

} // namespace qwhmi::drivers
