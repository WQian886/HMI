// ============================================================================
// WriteScheduler 实现
// 对应 WriteScheduler.h(★ 2b 的改动说明全在头文件里)
//
// ★ 2b(2026-10-19):按 plcId 路由写请求。改之前的全代码零 plcId ——
//   给 plc2 的变量写的值从 plc1 的 socket 发出去,而且零日志。
//   见 WriteScheduler.h 顶部"改之前的真实行为"。
// ============================================================================

#include "WriteScheduler.h"
#include "Variable.h"
#include "VariableManager.h"
#include "drivers/IPollDriver.h"
#include "drivers/IPlcDriverProvider.h"
#include "logging/Logging.h"
#include <QDateTime>
#include <QDebug>
#include <QVector>
#include <algorithm> // std::sort (桶内按地址定序)
#include <cstring>   // memcpy (Float bits 转换用)

using qwhmi::drivers::IPollDriver;
using qwhmi::drivers::IPlcDriverProvider;
using qwhmi::drivers::RegisterArea;

WriteScheduler::WriteScheduler(VariableManager* mgr, IPlcDriverProvider* provider,
                               QObject* parent)
    : QObject(parent), varMgr_(mgr), provider_(provider)
{
    timer_.setInterval(100);   // 100ms tick,跟 PollScheduler 节奏对齐
    connect(&timer_, &QTimer::timeout, this, &WriteScheduler::onTick);

    // ★ 阶段 6.3(2026-08-07):监听 varMgr_->variableReplaced,更新 pending_ 里失效的 Variable*
    //   2b:pending_ 改成 QPointer 后这条不再是防 UAF 的必需品,
    //   但保留 —— "变量没了"在 reply 路径上必须是一个显式的、可读的分支。
    if (varMgr_) {
        connect(varMgr_, &VariableManager::variableReplaced,
                this, &WriteScheduler::onVariableReplaced);
    }
}

void WriteScheduler::start() {
    if (timer_.isActive()) return;
    // ★ 2b:【不再】在这里 connect(modbus_, ...) ——
    //   driver 是运行期一台一台建出来的(PlcConnectionManager),
    //   start() 时它们可能一个都还不存在。
    //   连接改由 hookDriver(),由 hmi 在 driverAdded 时调。
    timer_.start();
    qCDebug(hmiWrite) << "[WriteScheduler] 启动";
}

void WriteScheduler::stop() {
    timer_.stop();
    for (const QString& id : hooked_) {
        IPollDriver* d = provider_ ? provider_->driverFor(id) : nullptr;
        if (d) QObject::disconnect(d, &IPollDriver::writeFinished, this, nullptr);
    }
    hooked_.clear();
    pending_.clear();
    pendingPlcId_.clear();
    pendingSentValue_.clear();   // ★ BUGFIX-D1(2026-10-26):跟另外两个一起清
    inflightPerPlc_.clear();
    qCDebug(hmiWrite) << "[WriteScheduler] 停止";
}

// ============================================================================
// driver 连接管理
// ============================================================================

void WriteScheduler::hookDriver(const QString& plcId, IPollDriver* driver) {
    if (!driver) return;
    // ★ 第一行无条件 disconnect,再 connect ——
    //   同一个 driver 被连两次的话,一次 reply 会被 onWriteReply 处理两遍:
    //   第一次 take 走 pending_,第二次找不到 → 打一条"未知 reqId"的 qWarning,
    //   而真正的问题是"这个变量的 dirty 被清早了"。
    //   靠调用方约定顺序迟早会被乱序调用打破,所以在这一行拿幂等性。
    QObject::disconnect(driver, &IPollDriver::writeFinished, this, nullptr);
    connect(driver, &IPollDriver::writeFinished, this, &WriteScheduler::onWriteReply);
    hooked_.insert(plcId);
}

void WriteScheduler::unhookDriver(const QString& plcId) {
    IPollDriver* d = provider_ ? provider_->driverFor(plcId) : nullptr;
    if (d) QObject::disconnect(d, &IPollDriver::writeFinished, this, nullptr);
    hooked_.remove(plcId);
}

// ============================================================================
// 请求构造
// ============================================================================

bool WriteScheduler::buildWritePayload(Variable* v, RegisterArea* outArea,
                                       QVector<quint16>* outValues) {
    if (!v || !outArea || !outValues) return false;
    QVector<quint16> payload;

    switch (v->dataType()) {
        case Variable::DataType::Bool: {
            // Bool → 0x05 写单线圈
            // Modbus 规范:0x0000 = OFF,0xFF00 = ON(Qt 的 QModbusDataUnit::Coils 内部也按这俩编码)
            // ★ BUGFIX(2026-07-28):之前写 0/1 走 Qt 自动转换;严格按协议改成 0x0000/0xFF00
            *outArea = RegisterArea::Coils;
            payload = {(v->value().toBool() ? quint16(0xFF00) : quint16(0x0000))};
            break;
        }
        case Variable::DataType::Int16:
        case Variable::DataType::UInt16: {
            // 16 位 → 0x06 写单寄存器
            *outArea = RegisterArea::HoldingReg;
            payload = {static_cast<quint16>(v->value().toUInt())};
            break;
        }
        case Variable::DataType::Int32:
        case Variable::DataType::Float: {
            // 32 位占 2 reg,走 0x10 多寄存器写
            *outArea = RegisterArea::HoldingReg;
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
    *outValues = payload;
    return true;
}

// ============================================================================
// onTick —— 2b 的核心
// ============================================================================

void WriteScheduler::onTick() {
    // ★ 2b:不再有"全局未连接就不发"的早退。
    //   改之前第一行是 `if (!modbus_->isConnected()) return;` ——
    //   多 PLC 之后这个早退是错的:一台 PLC 没连上,会挡住另外两台的写。
    //   现在改成逐 PLC 判断,一台不行不影响别的。

    if (!provider_) return;   // 没装配 provider(单测/未装配),什么都不写

    // ---- 第 1 步:把 dirty 的变量按 plcId 分桶 ----
    QHash<QString, QList<Variable*>> buckets;
    if (!varMgr_) return;
    for (Variable* v : varMgr_->allVariables()) {
        if (!v) continue;
        if (!v->isDirty()) continue;                            // 只发 dirty
        if (v->access() != Variable::Access::ReadWrite) continue;// 只发 ReadWrite
        buckets[v->plcId()].append(v);
    }
    if (buckets.isEmpty()) return;

    // ---- 第 2 步:逐 PLC 发一个 ----
    //   ★ 顺序要稳定:allVariables() 回来的顺序是【QHash 的遍历顺序】,
    //     不是添加顺序 —— Qt 没给 QHash 的遍历顺序做任何承诺,换个 Qt 版本
    //     甚至换个插入历史都会变。所以下面两步排序都是必需的,不是洁癖:
    //     ① plcIds 排序 —— 否则"每台 PLC 一帧一个"的服务对象每次跑都不一样。
    //     ② 桶内按地址排序 —— 否则同一台 PLC 的写顺序是哈希序,
    //        审计日志读起来像随机,单变量时的 FIFO 也名不副实。
    QStringList plcIds = buckets.keys();
    plcIds.sort();
    for (QString& id : plcIds) {
        QList<Variable*>& bucket = buckets[id];
        std::sort(bucket.begin(), bucket.end(),
                  [](Variable* a, Variable* b) {
                      if (a->address() != b->address()) return a->address() < b->address();
                      return a->name() < b->name();   // 同地址时按名字定序,保证可重现
                  });
    }

    for (const QString& plcId : plcIds) {
        // 这台已经有在途的 → 本帧跳过(协议串行)
        if (inflightPerPlc_.value(plcId) > 0) continue;

        // ★ driverFor 拿不到 → qCWarning 并【继续下一台】
        //   绝不能 return、也绝不能"退而取第一个 driver"。
        //   前者会让一台没配的 PLC 挡住全部写入;后者会把 plc2 的值写进 plc1 ——
        //   那正是 2b 要消灭的那个静默写错设备的 bug。
        IPollDriver* driver = provider_->driverFor(plcId);
        if (!driver) {
            qCWarning(hmiWrite).noquote()
                << QStringLiteral("[WriteScheduler] plc \"%1\" 没有可用 driver,"
                                  "本帧跳过它的 %2 个待写变量(其余 PLC 不受影响)。")
                       .arg(plcId)
                       .arg(buckets.value(plcId).size());
            continue;
        }

        if (!driver->isConnected()) {
            // 没连上不是错误 —— TcpClient 自己有重连退避,连上后下一帧自然就发了。
            // 所以【不】警告:否则每次重连期间都刷屏,日志里全是噪声。
            continue;
        }

        Variable* v = buckets.value(plcId).first();
        RegisterArea area = RegisterArea::HoldingReg;
        QVector<quint16> payload;
        if (!buildWritePayload(v, &area, &payload)) continue;

        // ★ BUGFIX-D1(2026-10-26):把【发出时】的值记下来。
        //   【为什么记在这一行,而不是 sendWriteRequest 之后】
        //     buildWritePayload 刚刚读过 v->value() 并把它编进了 payload ——
        //     这一行读到的就是【真的会被发出去的那个值】。
        //     放在发送之后再取,理论上存在一个空隙:那期间本地值可能又变了
        //     (理论上不会,但那正是这条守卫要防的事,自己别踩)。
        //   【为什么必须记】
        //     审计条目以前是在 reply 时刻现取 v->value() —— 而写挂起的那 100ms 里,
        //     一次轮询回包就可能把本地值冲掉(PollScheduler 那边同一个 bug,已修),
        //     于是审计记的是【另一个值】:复盘时唯一的证据写着"写了 100",
        //     而 PLC 上跑的是 0。跟 pendingPlcId_ 同一个道理,详见 .h 里的注释。
        const QString sentValue = v->value().toString();

        // ★ slaveId 从 provider 取。改之前是字面量 1。
        const int slaveId = provider_->slaveIdFor(plcId);

        // ★ IMPROVE-A2(2026-07-29):++ 前缀 + 跳过 0。
        //   必须是【进程内全局】单调的,不能各 driver 一份 ——
        //   三个 ModbusMaster 的 nextTid_ 都从 1 开始,TID 是 driver 的编号空间,
        //   requestId 是本调度器的编号空间,两个混用会让 onWriteReply 张冠李戴。
        quint32 reqId = ++nextReqId_;
        if (reqId == 0) reqId = ++nextReqId_;

        qCDebug(hmiWrite) << "[WriteScheduler] 发写:" << v->name()
                 << "plc=" << plcId
                 << "slaveId=" << slaveId
                 << "addr=0x" << QString::number(v->address(), 16).toUpper()
                 << "value=" << sentValue
                 << "reqId=" << reqId;

        // ★ 关键顺序:【先发,后登记】。
        //   sendWriteRequest 返回 false = 请求被拒,【不会】有 writeFinished 回来。
        //   改之前是先 pending_[reqId] = v 再发 —— 被拒时那个条目就永远留在
        //   哈希里,等一个永远不会来的 reply。表现是 pending_ 越堆越大,
        //   而且一旦将来有人加"pending_ 非空就跳过"的逻辑,整条写通路会静默哑掉。
        const bool accepted = driver->sendWriteRequest(area, slaveId, v->address(), payload, reqId);
        if (!accepted) {
            qCWarning(hmiWrite).noquote()
                << QStringLiteral("[WriteScheduler] plc \"%1\" 的 driver 拒收了 %2 的写请求"
                                  "(addr=%3),不登记 pending —— dirty 保留,下一帧重试。")
                       .arg(plcId, v->name())
                       .arg(v->address());
            continue;
        }

        pending_[reqId] = v;
        pendingPlcId_[reqId] = plcId;
        pendingSentValue_[reqId] = sentValue;
        inflightPerPlc_[plcId] = inflightPerPlc_.value(plcId) + 1;
        // ★ 这里【不】 break。每台 PLC 每帧只发一个,靠的是循环结构本身:
        //   循环体是"按 plcId 走一遍",同一个 plcId 一帧只被访问一次,
        //   所以它天然只能发一个。写 break 会立刻退出【整个】 plcIds 循环,
        //   退化成"全局每帧一个" —— 正是 2b 要消灭的那个语义。
        //   (这条我写错过一次:first() 取桶首 + break 的组合看起来像"每台一个",
        //    实际只服务 plcIds 排序后的第一台,plc2/plc3 永远排在后面。)
    }
}

// ============================================================================
// reply
// ============================================================================

void WriteScheduler::onWriteReply(quint32 requestId, bool success) {
    // ★ 2b:先摘 plcId —— inflightPerPlc_ 必须无条件 -1,
    //   【不】只在找到变量时才减。否则变量被删了(QPointer 置空),
    //   那个 PLC 的在途计数就永远降不下来 → 那台 PLC 从此再也不写任何东西。
    const QString plcId = pendingPlcId_.take(requestId);
    // ★ BUGFIX-D1(2026-10-26):摘出【发出时】的值(跟 plcId 一起摘,同一个道理)。
    //   注意 take 要【无条件】做 —— 变量在途时被删了(QPointer 置空)也一样,
    //   否则这个条目会永远留在哈希里。
    const QString sentValue = pendingSentValue_.take(requestId);
    if (!plcId.isEmpty()) {
        const int n = inflightPerPlc_.value(plcId) - 1;
        if (n > 0) inflightPerPlc_[plcId] = n;
        else      inflightPerPlc_.remove(plcId);
    }

    QPointer<Variable> vp = pending_.take(requestId);
    if (vp.isNull()) {
        // ★ 两种原因都到这儿:① 未知 reqId(理论上不该发生)② 变量在途时被删了。
        //   第 ② 种是【正常】的 —— 用户换个工程,那个变量就没了,
        //   而这个请求确实已经发出去了,PLC 那边照收不误,
        //   只是我们已经没有对应的 Variable 可以标脏/清脏了。
        qCDebug(hmiWrite) << "[WriteScheduler] 收到无对应变量的 reply:"
                          << "reqId=" << requestId
                          << "plc=" << (plcId.isEmpty() ? QStringLiteral("?") : plcId);
        return;
    }
    Variable* v = vp.data();

    if (success) {
        v->clearDirty();
        v->clearWriteFail();  // ★ IMPROVE-A4(2026-07-29):成功后清失败计数
        // ★ BUGFIX-D1(2026-10-26):打【发出时】的值,不是 reply 时刻的 v->value()
        qCDebug(hmiWrite) << "[Write] OK:" << v->name() << "=" << sentValue
                          << "(plc=" << v->plcId() << ")";
    } else {
        // ★ IMPROVE-A4(2026-07-29):失败累加,达到上限报警 + 清 dirty(工业铁律)
        //   之前决策 3「失败不回滚,保留 dirty 等下一帧重试」,但 PLC 故障时会刷屏
        //   现在重试 kMaxWriteRetries 次后:清 dirty + 标 Bad + emit writeFailed(阶段 5 报警)
        v->incrementWriteFail();
        if (v->writeFailCount() >= kMaxWriteRetries) {
            v->clearDirty();
            v->setQuality(Variable::Quality::Bad);  // UI 能看 Bad 知道不可信
            QString reason = QString("重试 %1 次仍失败,已停止重发(PLC 故障?)").arg(kMaxWriteRetries);
            qWarning() << "[Write] FAIL 报警:" << v->name() << reason;
            emit writeFailed(v->name(), reason);              // ★ 老签名,下游一行不用改
            emit writeFailedOnPlc(plcId, v->name(), reason);   // ★ 2b 新增
        } else {
            qWarning() << "[Write] FAIL:" << v->name()
                       << "=" << sentValue
                       << "(plc=" << plcId << ","
                       << v->writeFailCount() << "/" << kMaxWriteRetries << " 保留 dirty)";
        }
    }

    // ★ 阶段 4.x 写审计日志(2026-07-28):追加一条 + emit(只 UI 列表,不写文件)
    WriteAuditEntry e;
    e.timestamp    = QDateTime::currentDateTime();
    e.variableName = v->name();
    // ★ BUGFIX-D1(2026-10-26):记【发出时】的值,不是 reply 时刻现取的 v->value()。
    //   写挂起的那 100ms 里轮询回包可能把本地值冲掉,
    //   那样审计记的就是一个【没发出去】的值 —— 复盘时唯一的证据是假的。
    //   sentValue 为空只可能是"这个 reqId 不是我们发的"(未知 reqId),
    //   那条路径上面已经 return 了,走不到这里。
    e.newValue     = sentValue;
    e.success      = success;
    e.address      = QString("0x%1").arg(v->address(), 4, 16, QChar('0')).toUpper();
    QString failSuffix;
    if (!success && v->writeFailCount() >= kMaxWriteRetries) failSuffix = " (报警)";
    e.resultText   = success ? QStringLiteral("OK") : (QStringLiteral("FAIL") + failSuffix);
    // ★ 2b:用【发出时记下的】plcId,不是 v->plcId()。
    //   变量在途时被改成别的 plcId 的话,后者会把这笔账记到错的机器上。
    e.plcId        = plcId;
    auditLog_.append(e);
    emit auditAppended(e);
}

// ★ 阶段 6.3(2026-08-07):loadFromFile 重建 Variable 时,自动更新 pending_ 里的 Variable* 指针
//   旧 Variable 被 delete,新 Variable 同名替换
//   ★ 2b:pending_ 的值已是 QPointer,那个指针在旧对象被 delete 时【自动置空】,
//     这里的替换是"补上信息"而不是"防止 UAF" ——
//     不补的话,一个本来能成功写完的请求会因为找不到变量而被静默丢弃。
void WriteScheduler::onVariableReplaced(Variable* oldVar, Variable* newVar) {
    for (auto it = pending_.begin(); it != pending_.end(); ++it) {
        if (it.value().data() == oldVar) {
            it.value() = newVar;
        }
    }
}
