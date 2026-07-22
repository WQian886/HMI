// 阶段 3.3:PollScheduler 实现
// 对应 PollScheduler.h

#include "PollScheduler.h"
#include "Variable.h"
#include "VariableManager.h"
#include "ModbusMaster.h"
#include <QDateTime>
#include <QVariant>
#include <algorithm>
#include <cstring>  // memcpy

PollScheduler::PollScheduler(VariableManager* mgr, ModbusMaster* modbus, QObject* parent)
    : QObject(parent), varMgr_(mgr), modbus_(modbus)
{
    timer_.setInterval(100);  // 100ms tick 一次
    connect(&timer_, &QTimer::timeout, this, &PollScheduler::onTick);
}

void PollScheduler::start() {
    if (timer_.isActive()) return;
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    // 初始化:所有 Variable "立即可发"(nextPollAtMs = 0 < now)
    for (Variable* v : varMgr_->allVariables()) {
        nextPollAtMs_[v] = 0;
    }
    // 接 modbus reply
    connect(modbus_, &ModbusMaster::readFinished, this, &PollScheduler::onModbusReply);
    timer_.start();
}

void PollScheduler::stop() {
    timer_.stop();
    disconnect(modbus_, &ModbusMaster::readFinished, this, &PollScheduler::onModbusReply);
    nextPollAtMs_.clear();
    pending_.clear();
}

void PollScheduler::onTick() {
    // 2026-07-21 防御:modbus 未连就别 poll,避免连环弹错
    if (!modbus_->isConnected()) return;

    qint64 now = QDateTime::currentMSecsSinceEpoch();

    // 1. 找所有"到期"的 Variable
    // nextPollAtMs[v] == 0   → 第一次启动,立即发
    // nextPollAtMs[v] < now → 已到期,再发
    // nextPollAtMs[v] == -1  → in-flight,跳过(等 reply 回调重置)
    QList<Variable*> due;
    for (Variable* v : varMgr_->allVariables()) {
        qint64 next = nextPollAtMs_.value(v, 0);
        if (next != -1 && now >= next) {
            due.append(v);
            nextPollAtMs_[v] = -1;  // 标记 in-flight
        }
    }
    if (due.isEmpty()) return;

    // 2. 按 address 排序,便于合并连续区间
    std::sort(due.begin(), due.end(), [](Variable* a, Variable* b) {
        return a->address() < b->address();
    });

    // 3. 合并连续区间 → 1 次 Modbus 读请求
    while (!due.isEmpty()) {
        Variable* first = due.takeFirst();
        QList<Variable*> batch{first};
        int count = 1;
        while (!due.isEmpty()
               && due.first()->address() == first->address() + count) {
            batch.append(due.takeFirst());
            count++;
        }

        // 4. 发读请求(holding register,从 first->address() 起 count 个)
        int reqId = nextReqId_++;
        pending_[reqId] = batch;
        modbus_->sendReadRequest(
            QModbusDataUnit(QModbusDataUnit::HoldingRegisters, first->address(), count),
            1,  // slaveId 暂时固定 1,阶段 6 工程文件再做成可配
            reqId);
    }
}

void PollScheduler::onModbusReply(int requestId, QModbusDataUnit unit, bool success) {
    QList<Variable*> batch = pending_.take(requestId);
    if (batch.isEmpty()) return;

    qint64 now = QDateTime::currentMSecsSinceEpoch();

    if (!success) {
        // 失败:全部标 Bad,等下一个 poll interval 再试
        for (Variable* v : batch) {
            v->setQuality(Variable::Quality::Bad);
            nextPollAtMs_[v] = now + v->pollIntervalMs();
        }
        return;
    }

    // 成功:按合并顺序分发到各 Variable
    int regIdx = 0;
    for (Variable* v : batch) {
        if (regIdx >= unit.valueCount()) {
            // 数据不够(不应该发生,但防御一下)
            v->setQuality(Variable::Quality::Bad);
            nextPollAtMs_[v] = now + v->pollIntervalMs();
            continue;
        }

        quint16 raw = unit.value(regIdx);
        QVariant val;
        switch (v->dataType()) {
            case Variable::DataType::Bool:
                val = QVariant(static_cast<bool>(raw & 0x01));
                break;
            case Variable::DataType::Int16:
                val = QVariant::fromValue<qint16>(static_cast<qint16>(raw));
                break;
            case Variable::DataType::UInt16:
                val = QVariant::fromValue<quint16>(static_cast<quint16>(raw));
                break;
            case Variable::DataType::Int32: {
                // 32-bit 占 2 个 register,低地址在前(大端)
                if (regIdx + 1 < unit.valueCount()) {
                    quint32 composed = (static_cast<quint32>(unit.value(regIdx + 1)) << 16) | raw;
                    val = QVariant::fromValue<qint32>(static_cast<qint32>(composed));
                    regIdx++;
                } else {
                    val = QVariant();
                }
                break;
            }
            case Variable::DataType::Float: {
                // IEEE 754 float 占 2 个 register
                if (regIdx + 1 < unit.valueCount()) {
                    quint32 bits = (static_cast<quint32>(unit.value(regIdx + 1)) << 16) | raw;
                    float f;
                    std::memcpy(&f, &bits, sizeof(float));
                    val = QVariant::fromValue<float>(f);
                    regIdx++;
                } else {
                    val = QVariant();
                }
                break;
            }
        }
        v->setValue(val);
        v->setQuality(Variable::Quality::Good);
        nextPollAtMs_[v] = now + v->pollIntervalMs();
        regIdx++;
    }
}