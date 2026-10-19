// 阶段 3.3 + ★ C.5(2026-09-18):PollScheduler 多 PLC 调度实现
// 对应 PollScheduler.h
//
// C.5 关键变化:
//   - 单 IPollDriver* 改成 QHash<QString, IPollDriver*>(按 plcId)
//   - 每个 driver 独立 tick + 独立 readFinished
//   - 1 driver = 1 PLC,故障隔离(教训 1.5):plc1 挂不影响 plc2/plc3
//   - 单 driver 构造兼容老工程:默认所有变量 plcId="plc1"
//
// ★ 2b(2026-10-19)改了两个真 bug。两者都跟"多 PLC"有关,但【不修它们
//   多 PLC 根本跑不起来】,所以它们排在所有新代码之前:
//
//   BUG-1  addDriver 收不到回包 —— 多 driver 骨架(C.5)自己就是坏的
//          readFinished 的 connect 只写在 start() 里,addDriver 只插哈希。
//          而 hmi.cpp 是先 start() 再 loadFromFile(),所以运行期加进来的
//          driver 永远没有回包。改法:挂接收敛到 hookDriver(),由 running_ 决定时机。
//
//   BUG-2  地址不连续的变量永久卡住 —— 这是现在就存在的活 bug,与多 PLC 无关
//          onTick 的合并循环遇地址不连续会连发多帧,但单 in-flight 防御只在
//          循环前判一次。第 2 帧起被 ModbusMaster 拒收(只 errorOccurred,
//          不发 readFinished),而 pending_[reqId] 已登记 → 这批变量再也不轮询。
//          改法:检查 sendReadRequest 返回值,失败回滚 inFlight_ 和 pending_。

#include "PollScheduler.h"
#include "Variable.h"
#include "VariableManager.h"
#include "drivers/IPollDriver.h"
#include "drivers/IPlcDriverProvider.h"  // ★ 2b:slaveId 从这里问(见 setSlaveIdProvider)
#include "logging/Logging.h"
#include <QDateTime>
#include <QVariant>
#include <algorithm>
#include <cstring>  // memcpy (Float register 组合用)
// ★ 2026-10-10:用 hmiAlarm 分类打日志,不用裸 qDebug。
//   跟 VariableManager.cpp / ActionManager.cpp 同一套分类,方便现场
//   用 QT_LOGGING_RULES 单独调高报警相关日志。
#include "logging/Logging.h"

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
    connectVariableManager();
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
    connectVariableManager();
}

// ★ 2026-10-10:连 variableReplaced —— 本类第一次订阅这个信号。
//
//   【之前没有,为什么现在要】
//   本类把【裸 Variable*】存进四个容器:nextPollAtMs_(key)、inFlight_(key)、
//   activeVariables_(key)、pending_(value 列表)。而 QObject 析构不通知任何人。
//   于是"读请求在途 + 用户删掉该变量"→ onDriverReadFinished 拿到悬空指针,
//   v->setQuality(...) / v->address() 直接 use-after-free。
//
//   【为什么这不是臆想出来的场景 —— 以及我一开始写错了】
//   原来这里写的是"用户在界面上删变量是配置对话框里的常规操作"。
//   【那句话是错的,已更正】:全仓查过,hmi.ui 里根本没有删变量的入口,
//   removeVariable() 也只有测试在调。照那句话去找触发路径是找不到的。
//
//     真正在生产里踩中的是【另一条路,而且更普通】:
//     用户点【打开工程】换工程 → Project::loadFromFile 第 4 步
//     → varMgr_->clear() 把【全部】变量 delete 掉,换成新工程的。
//     而这发生在【轮询正在跑】的时候 —— nextPollAtMs_ 里每个变量都是 key,
//     每一个都当场变成悬空 key。
//   叠加"轮询 100ms 一次、Modbus 往返几毫秒到几百毫秒,请求在途是常态",
//   就是一次迟早会发生的崩溃。第二次及以后的换工程必然命中
//   (第一次加载时 start() 还没跑,容器是空的)。
void PollScheduler::connectVariableManager() {
    if (!varMgr_) return;
    connect(varMgr_, &VariableManager::variableReplaced,
            this, &PollScheduler::onVariableReplaced);
}

void PollScheduler::onVariableReplaced(Variable* oldVar, Variable* newVar) {
    if (!oldVar) return;

    // ① nextPollAtMs_:它【就是 key】—— 悬空 key留在 QHash 里,后续任何
    //   哈希运算(insert / operator[] / 遍历 rehash)都会解引用已释放内存。
    //   所以这不是"读到脏数据",是"容器本身坏掉"。
    nextPollAtMs_.remove(oldVar);

    // ② inFlight_:同样是 key,同上
    inFlight_.remove(oldVar);

    // ③ activeVariables_:UI 切屏幕时调 add/removeActiveVariable 建的集合,也是 key
    activeVariables_.remove(oldVar);
    //   newVar 非空(同名替换)时把它接回去 —— 否则替换一个正在显示的变量,
    //   UI 上的控件会【永久停止刷新】,而界面上看不出任何异常。
    //   这是替换路径和删除路径的唯一区别,漏了它就是引入一个新 bug。
    if (newVar) activeVariables_.insert(newVar);

    // ④ pending_:reqId → 变量列表。删掉的是列表【里的一个元素】,
    //   reqId 本身还得留着 —— 回包到达时 onDriverReadFinished 会 take 它。
    //   ★ 这里只能"换/删元素",不能 erase 整个 reqId:
    //     erase 了的话 onDriverReadFinished 拿到空 batch 直接 return,
    //     而 TCP 层那一帧已经收下、没法退回去了。
    for (auto it = pending_.begin(); it != pending_.end(); ++it) {
        QList<Variable*>& batch = it.value();
        for (int i = 0; i < batch.size(); ++i) {
            if (batch.at(i) != oldVar) continue;
            if (newVar) {
                batch[i] = newVar;   // 同名替换:换个指针,继续等这个变量
            } else {
                batch.removeAt(i);   // 删除:这一项已经不存在了
                --i;                  // ★ removeAt 让后续元素左移,不减 i 会跳过下一个
            }
        }
    }

    qCDebug(hmiAlarm) << "[PollScheduler] 变量被"
                      << (newVar ? QStringLiteral("替换") : QStringLiteral("删除"))
                      << ",已清理" << (newVar ? 3 : 4) << "个容器中的指针";
}

// ★ C.5:运行时添加 driver
// ★ 2b(2026-10-19):补上 running_ 判断 + 同 id 覆盖时摘旧连接。
//
//   【改之前的实际行为,不是风险,是现状】
//     addDriver 只往哈希里塞指针,【不接 readFinished】——
//     那个 connect 只写在 start() 里(见下方 start)。
//     而 hmi.cpp 的顺序是 pollScheduler_->start()(:412)在
//     project_->loadFromFile()(:640)【之前】。
//     于是运行期打开工程/应用配置加进来的 driver,永远收不到任何回包。
//     现象:配置里有 plc2、变量也绑了 plc2,就是不出数,而且日志一声不吭。
void PollScheduler::addDriver(const QString& plcId, qwhmi::drivers::IPollDriver* driver) {
    if (plcId.isEmpty() || !driver) return;
    // ★ 同 id 二次添加:先把【旧 driver】的连接摘干净。
    //   不摘的话旧 driver 会继续把回包打进同一个 onDriverReadFinished,
    //   而 requestId 是 PollScheduler 全局唯一的 —— 现象是
    //   "明明已经切到新 PLC 了,偶尔还会读到旧 PLC 的值"。
    //   ★ 注意只摘不 delete:所有权在 PlcConnectionManager,本类只借引用。
    if (drivers_.contains(plcId)) {
        unhookDriver(plcId);
    }
    drivers_.insert(plcId, driver);
    if (!legacyDriver_) legacyDriver_ = driver;
    if (running_) hookDriver(plcId);   // 运行期加的也要接上 —— 这就是 2b 补的那一刀
}

// ★ 2b(2026-10-19):readFinished 挂接的唯一入口。
//   【第一行那个无条件 disconnect 是防御性的,不是当前可达的行为】
//   实测记录(2026-10-19):把它注释掉,22 条用例【全绿】—— 没有任何用例能证伪它。
//   原因是今天每一个调用方都已经先摘干净了:
//     · addDriver  → 同 id 时先 unhookDriver
//     · start()    → 有 if (timer_.isActive()) return; 挡着,而且 stop() 已全摘
//   所以它防的是"将来有人新加一个调用方,忘了先摘"。
//   留着是因为成本只有一行、而漏掉的代价是连接泄漏(数据层静默 no-op,
//   不会有人发现)。但不要以为它被测试守着 —— 它没有。
void PollScheduler::hookDriver(const QString& plcId) {
    auto* d = drivers_.value(plcId, nullptr);
    if (!d) return;
    QObject::disconnect(d, &qwhmi::drivers::IPollDriver::readFinished, this, nullptr);
    connect(d, &qwhmi::drivers::IPollDriver::readFinished,
            this, [this, plcId](quint32 reqId, QVector<quint16> regs, bool success) {
                onDriverReadFinished(plcId, reqId, regs, success);
            });
    hooked_.insert(plcId);
}

void PollScheduler::unhookDriver(const QString& plcId) {
    auto* d = drivers_.value(plcId, nullptr);
    if (d) {
        QObject::disconnect(d, &qwhmi::drivers::IPollDriver::readFinished, this, nullptr);
    }
    hooked_.remove(plcId);
}

// ★ C.5:运行时移除 driver
void PollScheduler::removeDriver(const QString& plcId) {
    unhookDriver(plcId);   // ★ 2b:摘哈希之前先摘连接,否则 driver 析构后 lambda 还挂着
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
    // ★ 2b:改走 hookDriver —— running_ 必须【先置真】,否则下面的
    //   "只在 running_ 时挂接"会把这一轮全跳掉。
    running_ = true;
    for (auto it = drivers_.begin(); it != drivers_.end(); ++it) {
        hookDriver(it.key());
    }
    timer_.start();
}

void PollScheduler::stop() {
    timer_.stop();
    // ★ 2b:走 unhookDriver,顺带把 hooked_ 清空、running_ 落回 false。
    //   少了 running_ = false 这行,stop() 之后再 addDriver 会把连接接回来,
    //   stop() 就失去意义了(tst_IPollDriver 里约 7 条用例把它当拆解用)。
    running_ = false;
    for (auto it = drivers_.begin(); it != drivers_.end(); ++it) {
        unhookDriver(it.key());
    }
    hooked_.clear();
    nextPollAtMs_.clear();
    pending_.clear();
    pendingPlcId_.clear();
}

// ★ BUGFIX(2026-07-28):重连时调用,把所有 nextPollAtMs 强制置 0
//   解决"断连瞬间 in-flight 的 Variable 永久卡 -1,重连后永远不读"的 bug
// ★ IMPROVE-A7(2026-07-29):inFlight_ 独立 set 一起清(替原 -1 magic number)
void PollScheduler::resetAll() {
    qCDebug(hmiPoll) << "[PollScheduler] resetAll() — 清 in-flight 残留";
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
        qCDebug(hmiPoll) << "[PollScheduler] addActiveVariable:" << v->name()
                 << " active.size=" << activeVariables_.size();
    }
}

void PollScheduler::removeActiveVariable(Variable* v) {
    if (v && activeVariables_.remove(v)) {
        qCDebug(hmiPoll) << "[PollScheduler] removeActiveVariable:" << v->name();
    }
}

void PollScheduler::setAllActive() {
    // 单窗口场景初始化用:把所有 var 都加入激活集合
    activeVariables_.clear();
    for (Variable* v : varMgr_->allVariables()) {
        activeVariables_.insert(v);
    }
    qCDebug(hmiPoll) << "[PollScheduler] setAllActive():" << activeVariables_.size() << "vars";
}

void PollScheduler::clearActiveVariables() {
    activeVariables_.clear();
    qCDebug(hmiPoll) << "[PollScheduler] clearActiveVariables()";
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
        // ★★★ 2026-10-19:派生变量【不许】进轮询。
        //   派生变量的值是本地算出来的(addDerivedVariable 把它建成
        //   address=0,靠 recomputeDerivedVariables 算),它【没有寄存器】。
        //   而上面这一圈原来没有任何过滤,于是:
        //     ① 它被排进 dueByPlc,plcId 默认 "plc1" → 向 plc1 申请读地址 0
        //     ② reply 回来,onDriverReadFinished 把【算出来的值覆盖掉】,
        //        写进 plc1 的 0 号寄存器 —— 那【是真的现场寄存器】,
        //        不是没人用的占位符
        //     ③ 画面上那个派生量从此变成 plc1 某个随机寄存器的值,
        //        而且下一次有人重算表达式时它又被改回去,来回跳
        //   这是"把计算结果和现场数据混在同一个寄存器上"——
        //   往真实设备的一个真实地址写了一个假的值。
        //   它今天没被发现,是因为派生变量在 UI 上没法建(TagImporter 能导,
        //   但没有界面),而 CSV 导进来的表达式变量恰好 alwaysPoll 默认关,
        //   逃过了这层筛选。详见 tst_PollSchedulerDerived.cpp。
        if (v->isDerived()) continue;
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
            // ★ 2b(2026-10-19):原来这里是字面量 1,注释写着"阶段 6 工程文件再做成可配"
            //   —— 也就是说这一行【从写下那天起就是一个已知错误】。工程里 slave_id 配 5,
            //   读请求照样发 Unit ID=1,打到别的从机上,读回来的值还"看着正常"。
            //   现在统一从 provider 取;没装 provider(单元测试 / 老单 driver 用法)退回 1。
            req.slaveId   = slaveIdProvider_ ? slaveIdProvider_->slaveIdFor(plcId) : 1;
            req.addr      = first->address();
            req.count     = count;
            req.requestId = reqId;
            // ★ BUGFIX-B1(2026-10-19):检查返回值,失败必须回滚。
            //
            //   【改之前的实际行为,不是风险,是现状】
            //     单 in-flight 防御在 while 循环【之前】只判了一次(:268),
            //     而这个循环遇地址不连续会连发多帧。于是第 2 帧起
            //     ModbusMaster::sendReadRequest 因 pending_ 非空拒收
            //     (ModbusMaster.cpp:233-237),【只 emit errorOccurred,
            //     不发 readFinished】。而上面 pending_[reqId] 早就登记了,
            //     于是这批变量永远等不到 reply,inFlight_ 里再也出不来。
            //
            //     现场表现:同一台 PLC 上地址连续的那几个变量有值,
            //     中间跳过几个地址的那几个永远停在旧值,而且日志里只有
            //     一句 "上一帧未完成,稍后再发" —— 看不出后果是什么。
            if (!drv->sendReadRequest(req)) {
                pending_.remove(reqId);
                pendingPlcId_.remove(reqId);
                for (Variable* v : batch) {
                    inFlight_.remove(v);              // 回滚在途标记
                    nextPollAtMs_[v] = now + 100;     // 100ms 后重试(与上面同节奏)
                }
            }
        }
    }
}

// ★ C.5(2026-09-18):readFinished 改签名带 plcId(因为可能 N 个 driver 同时发 reply)
void PollScheduler::onDriverReadFinished(const QString& plcId, quint32 requestId, QVector<quint16> registers, bool success) {
    // ★ 2b:观测计数器。重复连接时下面这行会被进两次,而数据层看不出差别,
    //   所以只能靠计数把它证明出来。见 PollScheduler.h 的 replyDispatchCount()。
    ++replyDispatchCount_;
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
