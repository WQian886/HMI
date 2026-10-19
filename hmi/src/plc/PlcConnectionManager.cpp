// ============================================================================
// ★ 多 PLC 2b(2026-10-19):PlcConnectionManager 实现
//
// 设计说明全在头文件里(PlcConnectionManager.h),这里只实现。
//
// 关联:PlcConnectionManager.h, IPlcDriverProvider.h, DriverFactory.h
// ============================================================================

#include "plc/PlcConnectionManager.h"

#include "drivers/DriverFactory.h"
#include "drivers/IPollDriver.h"
#include "logging/Logging.h"

#include <QJsonObject>
#include <QJsonValue>
#include <QLoggingCategory>
#include <QSet>

namespace {
// 本类的日志分类。与 DriverFactory.cpp 的 hmiDriver 分开 —— 那边是"工厂",
// 这边是"哪台 PLC 上线了",排查现场问题时要能分开过滤。
Q_LOGGING_CATEGORY(hmiPlc, "hmi.plc")

// ★ 协议 ID 的【规范写法】,给 diff 判据用。
//
//   DriverFactory::normalizeProtocolName 解决的是"显示名 ↔ 协议 ID"
//   ("EtherNet/IP" → "eip"),它【故意】不动 modbus 和 modbus_tcp 这对 ——
//   因为它在别处也接裸字符串,归一了就对不上别的调用方。
//
//   但 DriverFactory::create 对 "modbus" 和 "modbus_tcp" 建的是【同一个】
//   ModbusDriver。所以 diff 判据若照搬 normalizeProtocolName 的输出,
//   "只把写法从 modbus 换成 modbus_tcp"会被判成"变了" → 白白断线重连一次。
//   用户在 PlcConfigDialog 里换个下拉框显示名就能触发,现场毫无必要地抖一下。
//
//   【为什么不改 DriverFactory 去归一】那是全仓共享的函数,归一了会让
//   那些拿它当"原样返回"的调用方行为改变 —— 那是一次独立的、影响面更大的改动。
//   在这里补一刀,范围只限本类的 diff 判据。
QString canonicalProtocol(const QString& raw) {
    const QString p = qwhmi::drivers::DriverFactory::normalizeProtocolName(raw);
    if (p.isEmpty()) return QStringLiteral("modbus_tcp");
    if (p == QStringLiteral("modbus")) return QStringLiteral("modbus_tcp");
    return p;
}
} // namespace

using qwhmi::drivers::IPollDriver;

PlcConnectionManager::PlcConnectionManager(QObject* parent)
    : QObject(parent) {}

PlcConnectionManager::~PlcConnectionManager() {
    // 不 delete entries_ 里的 driver —— 它们 parent 的是 this,
    // QObject 析构会连带删掉。理由见头文件 ③。
    // 这里【只】把哈希清空,免得 QHash 析构时对已释放的 QObject 再做点什么。
    entries_.clear();
}

// =============================================================================
// 解析
// =============================================================================

bool PlcConnectionManager::parseEntry(const QJsonObject& plc, Entry* out, QString* outReason) {
    if (!out) return false;
    const auto fail = [outReason](const QString& r) {
        if (outReason) *outReason = r;
        return false;
    };

    const QString id = plc.value(QStringLiteral("id")).toString().trimmed();
    if (id.isEmpty()) {
        return fail(QStringLiteral("缺少 \"id\""));
    }
    const QString host = plc.value(QStringLiteral("host")).toString().trimmed();
    if (host.isEmpty()) {
        return fail(QStringLiteral("host 为空"));
    }
    // toInt(-1):字段不存在时 QJsonValue 是 Undefined,toInt 会返回默认值 -1。
    //   【不】用 toInt(0) —— 0 不是合法端口,而且"没填"和"填了 0"要能分开。
    const int port = plc.value(QStringLiteral("port")).toInt(-1);
    if (port <= 0 || port > 65535) {
        return fail(QStringLiteral("port 非法:%1").arg(port));
    }

    out->id   = id;
    out->host = host;
    out->port = port;
    // ★ 协议归一:文件里写 "modbus" / "modbus_tcp" / "Modbus TCP" 都指同一台。
    //   不归一的话,只把协议字段的写法改一下就会被判成"变了"→ 白白重连一次。
    out->protocol = canonicalProtocol(plc.value(QStringLiteral("protocol")).toString());

    // ★ slave_id:缺省 1。【不回 0】—— 0 是 Modbus 广播地址,理由见 IPlcDriverProvider.h。
    //   同时把越界的也按缺省处理,而不是原样传下去:unit id 256+ 发出去,
    //   从站会当非法帧丢掉,现场表现是"配了 300 却连不上",而配置看上去完全正常。
    const int slaveId = plc.value(QStringLiteral("slave_id")).toInt(1);
    if (slaveId >= 1 && slaveId <= 247) {
        out->slaveId = slaveId;
    } else {
        if (plc.contains(QStringLiteral("slave_id"))) {
            qCWarning(hmiPlc).noquote()
                << QStringLiteral("[PlcConnectionManager] plc \"%1\" 的 slave_id=%2 不在 1..247,"
                                  "已按缺省 1 处理。")
                       .arg(id)
                       .arg(slaveId);
        }
        out->slaveId = 1;
    }
    return true;
}

// =============================================================================
// diff 判据 —— 全类最容易写错的一处,改之前先读这段
// =============================================================================

bool PlcConnectionManager::sameConnection(const Entry& a, const Entry& b) {
    // ★ 只比这四个。timeout_ms / max_retries / poll_interval_ms 【刻意不参与】:
    //   TcpClient 的那些常量是 static constexpr(TcpClient.h:22-27),没有 setter,
    //   connectTo() 那一刻就定死了。现在重连也用不上新值 —— 所以只更新配置、
    //   打日志说"下次生效",假装重连只会让现场 PLC 白抖一次。
    //
    //   【判错两个方向的后果不对称,所以两个方向都有用例】
    //     永远判"没变" → 用户改了 IP,程序说已应用,现场连的还是老地址(静默错)
    //     永远判"变了" → 每次打开工程全断线重连,现场 PLC 跟着抖
    return a.host == b.host
        && a.port == b.port
        && a.protocol == b.protocol
        && a.slaveId == b.slaveId;
    return a.host == b.host
        && a.port == b.port
        && a.protocol == b.protocol
        && a.slaveId == b.slaveId;
}

// =============================================================================
// 建 / 删
// =============================================================================

IPollDriver* PlcConnectionManager::createAndConnect(Entry& e) {
    // 工厂不认识的协议返回 nullptr(比如工程文件来自将来版本,写了 "profinet")。
    //   【不】在这里 qFatal / 整个 applyConfig 中止 —— 一台填错不挡整个工程,
    //   与 Project::validatePlcEntry 的既有取舍一致。
    IPollDriver* d = qwhmi::drivers::DriverFactory::create(e.protocol, this);
    if (!d) {
        qCWarning(hmiPlc).noquote()
            << QStringLiteral("[PlcConnectionManager] plc \"%1\" 的协议 \"%2\" 无法创建 driver,"
                              "这一台不会上线(其余 PLC 不受影响)。")
                   .arg(e.id, e.protocol);
        return nullptr;
    }

    // ★ parent 必须设 —— DriverFactory::create(protocol, this) 已经传了 this,
    //   这里再写一遍是为了让所有权意图在本文件里可见(头文件 ③ 钉的那条)。
    d->setParent(this);

    connect(d, &IPollDriver::connected, this, [this, id = e.id]() {
        emit plcStateChanged(id, true);
    });
    connect(d, &IPollDriver::disconnected, this, [this, id = e.id]() {
        emit plcStateChanged(id, false);
    });

    ++connectAttempts_;
    // 异步连接,【不】等结果:现场 PLC 常常是启动后 30 秒才上电的,
    // 在这里等它就等于 applyConfig 卡住整个界面。失败走 TcpClient 的重连退避,
    // 结果由 plcStateChanged 报。
    d->connectTo(e.host, static_cast<quint16>(e.port));

    qCInfo(hmiPlc).noquote()
        << QStringLiteral("[PlcConnectionManager] plc \"%1\" 上线:%2:%3 protocol=%4 slaveId=%5")
               .arg(e.id, e.host)
               .arg(e.port)
               .arg(e.protocol)
               .arg(e.slaveId);
    return d;
}

void PlcConnectionManager::dropEntry(const QString& plcId) {
    auto it = entries_.find(plcId);
    if (it == entries_.end()) return;

    // ★ 顺序不能变,四步各有一个非它不行的理由:
    //   ① driverAboutToBeRemoved 必须【先】发 —— PollScheduler 此刻还挂着指向
    //      这个 driver 的 lambda 连接,不发信号它就一直挂着。
    //   ② 先掐断【老 driver → 本类】的连接,再让老 driver 断线。
    //      反过来做会出这种现场事故:重连路径上先 createAndConnect 发出了
    //      driverAdded(界面:正在连接),紧接着老 driver 的 disconnected 才到
    //      (界面:已断开)—— 用户看着一台刚配好的 PLC 显示成掉线,而且不会自己恢复。
    //      老 driver 已经过期,它的状态变化【一律不该】再影响界面。
    //   ③ disconnect() 让 TcpClient 停下重连退避(deleteLater 不会,那个定时器
    //      还会活着并在半个重连周期后回来碰一个已 delete 的 socket)。
    //   ④ deleteLater 不是 delete —— 本函数返回后下游代码还在跑(比如紧接着的
    //      driverAdded),直接 delete 会让它调到已释放内存。
    emit driverAboutToBeRemoved(plcId);
    IPollDriver* d = it->driver;
    entries_.erase(it);      // 先从哈希里摘掉,后面各步就算出事也不会重复处理
    if (!d) return;

    // 注意是 QObject::disconnect(四参数静态版),不是 d->disconnect()(协议级断开)
    QObject::disconnect(d, nullptr, this, nullptr);
    d->disconnect();
    d->deleteLater();
}

// =============================================================================
// applyConfig —— 全量对齐,幂等
// =============================================================================

PlcConnectionManager::ApplyResult PlcConnectionManager::applyConfig(const QJsonArray& plcs) {
    ApplyResult result;

    // ★ 先存原始数组,不重建。理由见头文件 ②。
    //   放【最前面】而不是成功之后:哪怕全部 rejected,这次提交的原文也要留下,
    //   否则调用方拿 lastConfig() 会对不上它刚传进来的东西。
    lastConfig_ = plcs;

    QSet<QString> wanted;    // 本次配置里"应该在线"的 plcId
    QSet<QString> seen;      // 本次已处理过的 plcId(查重)

    for (int i = 0; i < plcs.size(); ++i) {
        Entry e;
        QString reason;
        if (!parseEntry(plcs.at(i).toObject(), &e, &reason)) {
            // 【故意】不进 rejected:连 id 都没有,rejected 是按 id 记的,
            // 硬塞一个空串会让调用方分不清"配错了"和"配的 id 是空的"。
            qCWarning(hmiPlc).noquote()
                << QStringLiteral("[PlcConnectionManager] plcs[%1] 跳过:%2。").arg(i).arg(reason);
            continue;
        }

        // ★ 重复 id:后者不顶替前者。
        //   【为什么后者作废而不是反过来】工程文件是手写的,重复 id 更像是复制粘贴
        //   时忘了改。前面的那份通常是用户真正在用的那份,顶掉它会让正在通讯的
        //   PLC 静默换地址 —— 这是最坏的一种错法。
        if (seen.contains(e.id)) {
            qCWarning(hmiPlc).noquote()
                << QStringLiteral("[PlcConnectionManager] plcs 里 id \"%1\" 重复,"
                                  "已忽略后面那条。")
                       .arg(e.id);
            result.rejected.append(e.id);
            continue;
        }
        seen.insert(e.id);
        wanted.insert(e.id);

        Entry newEntry = e;

        auto it = entries_.find(e.id);
        if (it == entries_.end()) {
            // ---- 新增 ----
            if (IPollDriver* d = createAndConnect(newEntry)) {
                newEntry.driver = d;
                entries_.insert(e.id, newEntry);
                emit driverAdded(e.id, d);
                result.added.append(e.id);
            } else {
                result.rejected.append(e.id);
            }
            continue;
        }

        if (sameConnection(it.value(), newEntry)) {
            // ---- 没变:【一都不做】 ----
            // 这里最容易顺手写一句 it->port = e.port 之类,不要。
            //   当前四元组相同,赋值是 no-op;但哪天判据放宽了,这一句就会变成
            //   "配置被静默吞掉"。真要更新 lastConfig_,靠的是 applyConfig 开头
            //   那句 lastConfig_ = plcs,不是靠改 Entry。
            result.unchanged.append(e.id);
            continue;
        }

        // ---- 变了:拆了重建 ----
        // 先记下旧配置,用于日志说明"从什么变成什么"
        const Entry old = it.value();
        dropEntry(e.id);
        if (IPollDriver* d = createAndConnect(newEntry)) {
            newEntry.driver = d;
            entries_.insert(e.id, newEntry);
            emit driverAdded(e.id, d);
            result.reconnected.append(e.id);
            qCInfo(hmiPlc).noquote()
                << QStringLiteral("[PlcConnectionManager] plc \"%1\" 重连:%2:%3 → %4:%5"
                                  "(protocol %6 → %7,slaveId %8 → %9)")
                       .arg(e.id, old.host)
                       .arg(old.port)
                       .arg(e.host)
                       .arg(e.port)
                       .arg(old.protocol, e.protocol)
                       .arg(old.slaveId)
                       .arg(e.slaveId);
        } else {
            // 建不出来:【旧的那台也没了】。
            //   【为什么不留旧的】配置说它该连 B,留着连 A 的 driver 比什么都不做更危险 ——
            //   用户以为已经切过去了,数据其实还从老机器来。
            result.rejected.append(e.id);
        }
    }

    // ---- 本次配置里没有、但当前在线的:删掉 ----
    //   先收集再删,不能边遍历 QHash 边 erase。
    const QStringList existing = entries_.keys();
    for (const QString& id : existing) {
        if (!wanted.contains(id)) {
            dropEntry(id);
            result.removed.append(id);
            qCInfo(hmiPlc).noquote()
                << QStringLiteral("[PlcConnectionManager] plc \"%1\" 不在新配置里,已下线。").arg(id);
        }
    }

    if (result.touched() > 0) {
        emit configApplied();
    }
    // ★ 只在"真的动了哪台 PLC"时才发。
    //   幂等重入(连调两次同一份配置)第二次不发,是刻意的:界面若每次都整表刷新,
    //   用户改一个字段点"应用"会看到整张表闪一下。
    //   【所以这个信号【不】等价于"applyConfig 被调用过"】——
    //   想确认"跑了没跑、结果如何",请用返回值。同头文件 ①。

    return result;
}

// =============================================================================
// IPlcDriverProvider
// =============================================================================

IPollDriver* PlcConnectionManager::driverFor(const QString& plcId) const {
    // 找不到返回 nullptr。【调用方不许退化成"取第一个 driver"】——
    //   那正是 2b 要消灭的静默写错设备(给 plc2 的变量写进 plc1 的 socket)。
    auto it = entries_.constFind(plcId);
    return (it == entries_.constEnd()) ? nullptr : it.value().driver;
}

int PlcConnectionManager::slaveIdFor(const QString& plcId) const {
    auto it = entries_.constFind(plcId);
    if (it == entries_.constEnd()) {
        // 没这台 PLC。回 1 而不是 0:0 是广播地址,详见 IPlcDriverProvider.h。
        // 调用方应该先 driverFor() 判空,走到这里说明调用顺序有问题。
        qCDebug(hmiPlc) << "[PlcConnectionManager] slaveIdFor:未知 plcId" << plcId << ",回缺省 1";
        return 1;
    }
    return it.value().slaveId;
}

QStringList PlcConnectionManager::managedIds() const {
    QStringList ids = entries_.keys();
    ids.sort();   // 界面显示要稳定顺序,否则用户每刷新一次表格就跳行
    return ids;
}
