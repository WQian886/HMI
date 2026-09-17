// 阶段 3.3:PollScheduler 实现
// 对应 PollScheduler.h

#include "PollScheduler.h"
#include "Variable.h"
#include "VariableManager.h"
#include "drivers/IPollDriver.h"
#include <QDateTime>
#include <QVariant>
#include <algorithm>
#include <cstring>  // memcpy (Float register 组合用)

PollScheduler::PollScheduler(VariableManager* mgr, qwhmi::drivers::IPollDriver* driver, QObject* parent)
    : QObject(parent), varMgr_(mgr), driver_(driver)
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
    // ★ C.1:接 driver 的 readFinished(协议无关信号:QVector<quint16>)
    connect(driver_, &qwhmi::drivers::IPollDriver::readFinished,
            this, &PollScheduler::onDriverReadFinished);
    timer_.start();
}

void PollScheduler::stop() {
    timer_.stop();
    disconnect(driver_, &qwhmi::drivers::IPollDriver::readFinished,
               this, &PollScheduler::onDriverReadFinished);
    nextPollAtMs_.clear();
    pending_.clear();
}

// ★ BUGFIX(2026-07-28):重连时调用,把所有 nextPollAtMs 强制置 0
//   解决"断连瞬间 in-flight 的 Variable 永久卡 -1,重连后永远不读"的 bug
// ★ IMPROVE-A7(2026-07-29):inFlight_ 独立 set 一起清(替原 -1 magic number)
void PollScheduler::resetAll() {
    qDebug() << "[PollScheduler] resetAll() — 清 in-flight 残留";
    for (auto it = nextPollAtMs_.begin(); it != nextPollAtMs_.end(); ++it) {
        it.value() = 0;  // 0 = 立即可发
    }
    inFlight_.clear();   // ★ IMPROVE-A7:清 in-flight 集合
    pending_.clear();     // 顺手清掉 pending reqId(旧的不会影响新的,清掉更干净)
}

// ★ 阶段 6.x.1(2026-08-11 Wqian 拍板)分级轮询策略 C —— UI 注册接口实现
//   背景:UI 屏幕切换时调 add/removeActiveVariable(未来多屏幕)
//   单窗口场景:hmi 构造后调 setAllActive(),所有 var 默认激活
//   onTick 中检查:不在 activeVariables_ 且 alwaysPoll=false → 跳过这个 var
void PollScheduler::addActiveVariable(Variable* v) {
    if (v && !activeVariables_.contains(v)) {
        activeVariables_.insert(v);
        qDebug() << "[PollScheduler] addActiveVariable:" << v->name()
                 << " active.size=" << activeVariables_.size();
    }
}

void PollScheduler::removeActiveVariable(Variable* v) {
    if (v && activeVariables_.remove(v)) {
        qDebug() << "[PollScheduler] removeActiveVariable:" << v->name();
    }
}

void PollScheduler::setAllActive() {
    // 单窗口场景初始化用:把所有 var 都加入激活集合
    activeVariables_.clear();
    for (Variable* v : varMgr_->allVariables()) {
        activeVariables_.insert(v);
    }
    qDebug() << "[PollScheduler] setAllActive():" << activeVariables_.size() << "vars";
}

void PollScheduler::clearActiveVariables() {
    activeVariables_.clear();
    qDebug() << "[PollScheduler] clearActiveVariables()";
}

void PollScheduler::onTick() {
    // 2026-07-21 防御:driver 未连就别 poll,避免连环弹错
    if (!driver_->isConnected()) return;

    // ★ BUGFIX(2026-07-28):跟 WriteScheduler 对齐,加 in-flight 防御
    //
    //   【背景】Modbus 协议从站串行响应,master 不能同时问 1 又问 2
    //     1 个 driver(单 in-flight)→ 防御必加
    //     多个 driver(可并行,未来 S7) → 这层防御无影响
    //
    //   【修法】onTick 顶部检查 pendingCount,有未完成的就静默 return,
    //     等下一帧(100ms)再发
    //
    //   【取舍】静默 return 会让 PollScheduler 100ms 内少读一次(最多 1 次),
    //     对工业 HMI 1 秒轮询周期影响可忽略;比弹窗骚扰操作员好得多
    if (driver_->pendingCount() > 0) return;

    qint64 now = QDateTime::currentMSecsSinceEpoch();

    // 1. 找所有"到期"的 Variable
    // inFlight_.contains(v) → 请求已发,等 reply,跳过
    // nextPollAtMs[v] == 0   → 第一次启动,立即发
    // nextPollAtMs[v] < now → 已到期,再发
    QList<Variable*> due;
    for (Variable* v : varMgr_->allVariables()) {
        // ★ 阶段 6.x.1(2026-08-11 Wqian 拍板)分级轮询 —— 不在 activeVariables_ 且 alwaysPoll=false → 跳过
        //   alwaysPoll=true 的变量(报警关键)不受 activeVariables_ 限制 —— 报警不漏
        if (!v->alwaysPoll() && !activeVariables_.contains(v)) continue;
        if (inFlight_.contains(v)) continue;  // ★ IMPROVE-A7:in-flight 用 QSet 替 -1
        qint64 next = nextPollAtMs_.value(v, 0);
        if (now >= next) {
            due.append(v);
            inFlight_.insert(v);  // ★ IMPROVE-A7:标记 in-flight
        }
    }
    if (due.isEmpty()) return;

    // 2. 按 address 排序,便于合并连续区间
    std::sort(due.begin(), due.end(), [](Variable* a, Variable* b) {
        return a->address() < b->address();
    });

    // 3. 合并连续区间 → 1 次 driver 读请求
    while (!due.isEmpty()) {
        Variable* first = due.takeFirst();
        QList<Variable*> batch{first};
        int count = 1;
        while (!due.isEmpty()
               && due.first()->address() == first->address() + count) {
            batch.append(due.takeFirst());
            count++;
        }

        // 4. 发读请求(通用 IPollDriver 接口,不再依赖 QModbusDataUnit)
        // ★ IMPROVE-A2(2026-07-29):++ 前缀 + 跳过 0
        quint32 reqId = ++nextReqId_;
        if (reqId == 0) reqId = ++nextReqId_;
        pending_[reqId] = batch;

        qwhmi::drivers::IPollDriver::ReadRequest req;
        req.area      = qwhmi::drivers::RegisterArea::HoldingReg;
        req.slaveId   = 1;   // slaveId 暂时固定 1,阶段 6 工程文件再做成可配
        req.addr      = first->address();
        req.count     = count;
        req.requestId = reqId;
        driver_->sendReadRequest(req);
    }
}

// ★ C.1:信号签名从 QModbusDataUnit → QVector<quint16>
//   PollScheduler 不再依赖 Modbus-特定类型,任何 IPollDriver 都触发得了
void PollScheduler::onDriverReadFinished(quint32 requestId, QVector<quint16> registers, bool success) {
    QList<Variable*> batch = pending_.take(requestId);
    if (batch.isEmpty()) return;

    qint64 now = QDateTime::currentMSecsSinceEpoch();

    // ★ IMPROVE-A7(2026-07-29):reply 到了,清 in-flight set(无论成功失败都清)
    for (Variable* v : batch) {
        inFlight_.remove(v);
    }

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
        if (regIdx >= registers.size()) {
            // 数据不够(不应该发生,但防御一下)
            v->setQuality(Variable::Quality::Bad);
            nextPollAtMs_[v] = now + v->pollIntervalMs();
            continue;
        }

        quint16 raw = registers[regIdx];
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
                if (regIdx + 1 < registers.size()) {
                    quint32 composed = (static_cast<quint32>(registers[regIdx + 1]) << 16) | raw;
                    val = QVariant::fromValue<qint32>(static_cast<qint32>(composed));
                    regIdx++;
                } else {
                    val = QVariant();
                }
                break;
            }
            case Variable::DataType::Float: {
                // IEEE 754 float 占 2 个 register
                if (regIdx + 1 < registers.size()) {
                    quint32 bits = (static_cast<quint32>(registers[regIdx + 1]) << 16) | raw;
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
        // ★ IMPROVE-A6(2026-07-29):PLC 读路径不查范围(原始寄存器值可能超工程值)
        //   返 NoChange 时也继续推进 quality=Good + nextPollAt(只是没 emit valueChanged)
        v->setValue(val, false);
        v->setQuality(Variable::Quality::Good);
        // ★ 阶段 5.3+ 修 Bug #3(2026-08-05):emit 原始 register 给 AlarmChecker
        //   emit 在 setValue 之后,raw 还是原始 register(不是过滤后的 val)
        emit rawRead(v, raw);
        nextPollAtMs_[v] = now + v->pollIntervalMs();
        regIdx++;
    }
}
