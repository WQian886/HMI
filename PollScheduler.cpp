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

// ★ BUGFIX(2026-07-28):重连时调用,把所有 nextPollAtMs 强制置 0
//   解决"断连瞬间 in-flight 的 Variable 永久卡 -1,重连后永远不读"的 bug
void PollScheduler::resetAll() {
    qDebug() << "[PollScheduler] resetAll() — 清 in-flight 残留";
    for (auto it = nextPollAtMs_.begin(); it != nextPollAtMs_.end(); ++it) {
        it.value() = 0;  // 0 = 立即可发
    }
    pending_.clear();     // 顺手清掉 pending reqId(旧的不会影响新的,清掉更干净)
}

void PollScheduler::onTick() {
    // 2026-07-21 防御:modbus 未连就别 poll,避免连环弹错
    if (!modbus_->isConnected()) return;

    // ★ BUGFIX(2026-07-28):跟 WriteScheduler 对齐,加 modbus in-flight 防御
    //
    //   【背景】ModbusMaster 2026-07-09 加了「并发请求限流」机制:
    //     同一时刻 ModbusMaster::pending_ 只能有 1 个未完成请求
    //     (Modbus 协议从站串行响应,master 不能同时问 1 又问 2)
    //
    //   【Bug 现象】本函数 onTick() 100ms 一次,直接发读请求;但 WriteScheduler
    //     也 100ms 一次发写请求,二者偶尔撞车:一个发完没回,另一个接着发,
    //     第二个被 ModbusMaster 拒,emit errorOccurred → hmi.cpp 弹窗
    //     「Modbus 错误 / 上一帧未完成,稍后再发(pending=1)」
    //
    //   【修法】onTick 顶部加 pending 检查,有未完成的就静默 return,
    //     等下一帧(100ms)再发,不会撞 WriteScheduler
    //
    //   【参照】WriteScheduler::onTick (WriteScheduler.cpp:41) 已经有同样防御:
    //     if (modbus_->pendingCount() > 0) return;
    //
    //   【取舍】静默 return 会让 PollScheduler 100ms 内少读一次(最多 1 次),
    //     对工业 HMI 1 秒轮询周期影响可忽略;比弹窗骚扰操作员好得多
    if (modbus_->pendingCount() > 0) return;

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
        v->setValue(val, false);  // 阶段 4.x 范围检查(2026-07-28):PLC 读路径不查范围(原始寄存器值可能超工程值)
        v->setQuality(Variable::Quality::Good);
        nextPollAtMs_[v] = now + v->pollIntervalMs();
        regIdx++;
    }
}