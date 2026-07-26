// 阶段 4.3.0:WriteScheduler 实现
// 对应 WriteScheduler.h

#include "WriteScheduler.h"
#include "Variable.h"
#include "VariableManager.h"
#include "ModbusMaster.h"
#include <QModbusDataUnit>
#include <QDateTime>
#include <QDebug>
#include <QVector>
#include <cstring>   // memcpy (Float bits 转换用)

WriteScheduler::WriteScheduler(VariableManager* mgr, ModbusMaster* modbus, QObject* parent)
    : QObject(parent), varMgr_(mgr), modbus_(modbus)
{
    timer_.setInterval(100);   // 100ms tick,跟 PollScheduler 节奏对齐
    connect(&timer_, &QTimer::timeout, this, &WriteScheduler::onTick);
}

void WriteScheduler::start() {
    if (timer_.isActive()) return;
    connect(modbus_, &ModbusMaster::writeFinished, this, &WriteScheduler::onWriteReply);
    timer_.start();
    qDebug() << "[WriteScheduler] 启动";
}

void WriteScheduler::stop() {
    timer_.stop();
    disconnect(modbus_, &ModbusMaster::writeFinished, this, &WriteScheduler::onWriteReply);
    pending_.clear();
    qDebug() << "[WriteScheduler] 停止";
}

void WriteScheduler::onTick() {
    // 防御:Modbus 未连接就不发
    if (!modbus_->isConnected()) return;

    // 阶段 4.3.0 bugfix:避免发请求被 ModbusMaster「上一帧未完成」拒掉
    // PollScheduler 读和 WriteScheduler 写共用 ModbusMaster::pending_,错开时候才发
    if (modbus_->pendingCount() > 0) return;

    // 遍历:找一个 dirty + ReadWrite + 非 in-flight 的 Variable
    // 简化策略:每帧只发一个,避免一帧堆积 + 好调试
    for (Variable* v : varMgr_->allVariables()) {
        if (!v->isDirty()) continue;
        if (v->access() != Variable::Access::ReadWrite) continue;

        QModbusDataUnit::RegisterType type;
        QVector<quint16> payload;
        bool single = true;   // false → 走 0x10(多寄存器写)

        switch (v->dataType()) {
            case Variable::DataType::Bool: {
                // Bool → 0x05 写单线圈
                type = QModbusDataUnit::Coils;
                payload = {(v->value().toBool() ? quint16(1) : quint16(0))};
                break;
            }
            case Variable::DataType::Int16:
            case Variable::DataType::UInt16: {
                // 16 位 → 0x06 写单寄存器
                type = QModbusDataUnit::HoldingRegisters;
                quint16 raw = static_cast<quint16>(v->value().toUInt());
                payload = {raw};
                break;
            }
            case Variable::DataType::Int32:
            case Variable::DataType::Float: {
                // 32 位占 2 reg,走 0x10 多寄存器写
                type = QModbusDataUnit::HoldingRegisters;
                single = false;
                quint32 bits;
                if (v->dataType() == Variable::DataType::Float) {
                    float f = v->value().toFloat();
                    std::memcpy(&bits, &f, sizeof(float));
                } else {
                    bits = static_cast<quint32>(v->value().toInt());
                }
                payload = {static_cast<quint16>(bits & 0xFFFF),
                           static_cast<quint16>((bits >> 16) & 0xFFFF)};
                break;
            }
        }

        int reqId = nextReqId_++;
        pending_[reqId] = v;

        QModbusDataUnit unit(type, v->address(), payload);
        qDebug() << "[WriteScheduler] 发写:" << v->name()
                 << "addr=0x" << QString::number(v->address(), 16).toUpper()
                 << "value=" << v->value().toString()
                 << "reqId=" << reqId;

        modbus_->sendWriteRequest(unit, 1, reqId);   // slaveId 暂固定 1,阶段 6 可配
        break;   // ← 关键:每帧只发一个
    }
}

void WriteScheduler::onWriteReply(int requestId, bool success) {
    Variable* v = pending_.take(requestId);
    if (!v) {
        qWarning() << "[WriteScheduler] 收到未知 reqId:" << requestId;
        return;
    }

    if (success) {
        v->clearDirty();
        qDebug() << "[Write] OK:" << v->name() << "=" << v->value().toString();
    } else {
        // 决策 3:失败不回滚,保留 dirty 等下一帧重试
        qWarning() << "[Write] FAIL:" << v->name()
                   << "=" << v->value().toString() << "(dirty 保留,下帧重试)";
    }
}