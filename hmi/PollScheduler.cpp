// 阶段 3.3 + ★ C.5(2026-09-18):PollScheduler 多 PLC 调度实现
// 对应 PollScheduler.h
//
// C.5 关键变化:
//   - 单 IPollDriver* 改成 QHash<QString, IPollDriver*>(按 plcId)
//   - 每个 driver 独立 tick + 独立 readFinished
//   - 1 driver = 1 PLC,故障隔离(教训 1.5):plc1 挂不影响 plc2/plc3
//   - 单 driver 构造兼容老工程:默认所有变量 plcId="plc1"

#include "PollScheduler.h"
#include "Variable.h"
#include "VariableManager.h"
#include "drivers/IPollDriver.h"
#include <QDateTime>
#include <QVariant>
#include <algorithm>
#include <cstring>  // memcpy (Float register 组合用)

// ★ C.5:单 driver 构造 → 包成 QHash{"plc1": driver}(老工程兼容)
PollScheduler::PollScheduler(VariableManager* mgr,
                             qwhmi::drivers::IPollDriver* driver,
                             QObject* parent)
    : QObject(parent), varMgr_(mgr), legacyDriver_(driver)
{
    if (driver) {
        drivers_.insert(QStringLiteral("plc1"), driver);
    }
    timer_.setInterval(100);  // 100ms tick 一次
    connect(&timer_, &QTimer::timeout, this, &PollScheduler::onTick);
}

// ★ C.5:多 driver 构造
PollScheduler::PollScheduler(VariableManager* mgr,
                             const QHash<QString, qwhmi::drivers::IPollDriver*>& drivers,
                             QObject* parent)
    : QObject(parent), varMgr_(mgr), drivers_(drivers)
{
    if (!drivers_.isEmpty()) {
        legacyDriver_ = drivers_.values().first();  // 兼容指针指向第一个(QHash::first() 不存在)
    }
    timer_.setInterval(100);
    connect(&timer_, &QTimer::timeout, this, &PollScheduler::onTick);
}

// ★ C.5:运行时添加 driver
void PollScheduler::addDriver(const QString& plcId, qwhmi::drivers::IPollDriver* driver) {
    if (plcId.isEmpty() || !driver) return;
    drivers_.insert(plcId, driver);
    if (!legacyDriver_) legacyDriver_ = driver;
    // 启动定时器如果还没启(start() 由调用方决定)
}

// ★ C.5:运行时移除 driver
void PollScheduler::removeDriver(const QString& plcId) {
    drivers_.remove(plcId);
    if (legacyDriver_ && !drivers_.isEmpty()) {
        legacyDriver_ = drivers_.values().first();
    } else if (drivers_.isEmpty()) {
        legacyDriver_ = nullptr;
    }
}

void PollScheduler::start() {
    if (timer_.isActive()) return;
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    // 初始化:所有 Variable "立即可发"(nextPollAtMs = 0 < now)
    for (Variable* v : varMgr_->allVariables()) {
        nextPollAtMs_[v] = 0;
    }
    // ★ C.5:接每个 driver 的 readFinished(协议无关),包 lambda 捕获 plcId
    //   故障隔离:1 个 driver disconnect 不影响其他 driver 的 reply 处理
    for (auto it = drivers_.begin(); it != drivers_.end(); ++it) {
        const QString plcId = it.key();
        qwhmi::drivers::IPollDriver* drv = it.value();
        connect(drv, &qwhmi::drivers::IPollDriver::readFinished,
                this, [this, plcId](quint32 reqId, QVector<quint16> regs, bool success) {
                    onDriverReadFinished(plcId, reqId, regs, success);
                });
    }
    timer_.start();
}

void PollScheduler::stop() {
    timer_.stop();
    for (auto it = drivers_.begin(); it != drivers_.end(); ++it) {
        qwhmi::drivers::IPollDriver* drv = it.value();
        disconnect(drv, &qwhmi::drivers::IPollDriver::readFinished,
                   this, nullptr);  // 断开所有 lambda
    }
    nextPollAtMs_.clear();
    pending_.clear();
    pendingPlcId_.clear();
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
    pendingPlcId_.clear();
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

// ★ C.5(2026-09-18):onTick 多 driver 调度 + 故障隔离
//   关键:每个 driver 独立检查 isConnected / pendingCount
//         1 个 PLC 挂 → 只该 plc 的变量 pendingCount 累积,其他 plc 照常 poll
void PollScheduler::onTick() {
    qint64 now = QDateTime::currentMSecsSinceEpoch();

    // 1. 找所有"到期"的 Variable,并按 plcId 分组
    // inFlight_.contains(v) → 请求已发,等 reply,跳过
    // nextPollAtMs[v] == 0   → 第一次启动,立即发
    // nextPollAtMs[v] < now → 已到期,再发
    // ★ C.5:同时按 Variable.plcId() 找对应 driver
    QHash<QString, QList<Variable*>> dueByPlc;  // plcId → 该 plc 到期的变量
    for (Variable* v : varMgr_->allVariables()) {
        // ★ 阶段 6.x.1(2026-08-11 Wqian 拍板)分级轮询
        if (!v->alwaysPoll() && !activeVariables_.contains(v)) continue;
        if (inFlight_.contains(v)) continue;
        qint64 next = nextPollAtMs_.value(v, 0);
        if (now >= next) {
            dueByPlc[v->plcId()].append(v);
            inFlight_.insert(v);
        }
    }
    if (dueByPlc.isEmpty()) return;

    // 2. 对每个 plc 独立处理:找 driver + 检查连接 + 合并发请求
    for (auto it = dueByPlc.begin(); it != dueByPlc.end(); ++it) {
        const QString plcId = it.key();
        QList<Variable*> due = it.value();

        // ★ C.5:找不到 driver(变量配置错或 driver 未注册)→ 全部 Bad
        qwhmi::drivers::IPollDriver* drv = drivers_.value(plcId, nullptr);
        if (!drv) {
            qWarning() << "[PollScheduler] onTick: plcId=" << plcId
                       << "没注册 driver — 跳过" << due.size() << "vars,标 Bad";
            for (Variable* v : due) {
                v->setQuality(Variable::Quality::Bad);
                inFlight_.remove(v);
                nextPollAtMs_[v] = now + v->pollIntervalMs();
            }
            continue;
        }

        // ★ C.5 故障隔离:这个 plc 的 driver 没连 → 只跳过该 plc 的变量
        if (!drv->isConnected()) {
            for (Variable* v : due) {
                v->setQuality(Variable::Quality::Bad);
                inFlight_.remove(v);
                nextPollAtMs_[v] = now + v->pollIntervalMs();
            }
            continue;
        }

        // ★ 单 in-flight 防御(同 driver 不能并行问多个请求)
        if (drv->pendingCount() > 0) {
            for (Variable* v : due) {
                inFlight_.remove(v);  // 回滚标记,下一帧再发
                nextPollAtMs_[v] = now + 100;  // 100ms 后重试
            }
            continue;
        }

        // 3. 按 address 排序,便于合并连续区间(同 plc 内)
        std::sort(due.begin(), due.end(), [](Variable* a, Variable* b) {
            return a->address() < b->address();
        });

        // 4. 合并连续区间 → 1 次 driver 读请求
        while (!due.isEmpty()) {
            Variable* first = due.takeFirst();
            QList<Variable*> batch{first};
            int count = 1;
            while (!due.isEmpty()
                   && due.first()->address() == first->address() + count) {
                batch.append(due.takeFirst());
                count++;
            }

            // 5. 发读请求(通用 IPollDriver 接口,不再依赖 QModbusDataUnit)
            quint32 reqId = ++nextReqId_;
            if (reqId == 0) reqId = ++nextReqId_;
            pending_[reqId] = batch;
            pendingPlcId_[reqId] = plcId;  // ★ C.5:记 plcId 给 readFinished 分派

            qwhmi::drivers::IPollDriver::ReadRequest req;
            req.area      = qwhmi::drivers::RegisterArea::HoldingReg;
            req.slaveId   = 1;   // slaveId 暂时固定 1,阶段 6 工程文件再做成可配
            req.addr      = first->address();
            req.count     = count;
            req.requestId = reqId;
            drv->sendReadRequest(req);  // ★ C.5:用 per-plc driver 发,不是单 driver_
        }
    }
}

// ★ C.5(2026-09-18):readFinished 改签名带 plcId(因为可能 N 个 driver 同时发 reply)
void PollScheduler::onDriverReadFinished(const QString& plcId, quint32 requestId, QVector<quint16> registers, bool success) {
    QList<Variable*> batch = pending_.take(requestId);
    pendingPlcId_.remove(requestId);
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
