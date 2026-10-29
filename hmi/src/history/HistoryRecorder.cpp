// ============================================================================
// 阶段 D · D.1 · HistoryRecorder 实现
// 对应 HistoryRecorder.h
// ============================================================================

#include "history/HistoryRecorder.h"
#include "../../Variable.h"
#include "../../VariableManager.h"
#include "util/Paths.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTimer>          // ★ D.2:心跳 timer
#include <QVariant>

namespace qwhmi::history {

HistoryRecorder::HistoryRecorder(QObject* parent)
    : QObject(parent)
{
}

HistoryRecorder::~HistoryRecorder() {
    close();
}

// ★ D.1.2:打开数据库 + 创建 schema
bool HistoryRecorder::open(const QString& dbPath) {
    if (db_) {
        qWarning() << "[HistoryRecorder] 已打开,先 close";
        return false;
    }
    if (dbPath.isEmpty()) {
        qCritical() << "[HistoryRecorder] dbPath 为空";
        return false;
    }

    // 确保目录存在
    QDir().mkpath(QFileInfo(dbPath).absolutePath());

    // ★ 用静态函数获取 connection,避免 QSqlDatabase 临时对象 bug
    if (QSqlDatabase::contains("hmi_history")) {
        QSqlDatabase::removeDatabase("hmi_history");
    }
    db_ = new QSqlDatabase(QSqlDatabase::addDatabase("QSQLITE", "hmi_history"));
    db_->setDatabaseName(dbPath);
    const bool ok = db_->open();
    if (!ok) {
        qCritical() << "[HistoryRecorder] 打开失败:" << db_->lastError().text()
                    << "path=" << dbPath;
        delete db_;
        db_ = nullptr;
        QSqlDatabase::removeDatabase("hmi_history");
        return false;
    }

    if (!ensureSchema()) {
        close();
        return false;
    }

    qInfo() << "[HistoryRecorder] opened:" << dbPath;
    return true;
}

void HistoryRecorder::close() {
    if (db_) {
        db_->close();
        delete db_;
        db_ = nullptr;
        QSqlDatabase::removeDatabase("hmi_history");
        qInfo() << "[HistoryRecorder] closed";
    }
}

bool HistoryRecorder::isOpen() const {
    return db_ && db_->isOpen();
}

// ★ D.1.2:创建表 + 索引
bool HistoryRecorder::ensureSchema() {
    if (!db_) return false;
    QSqlQuery q(*db_);

    // history 表(★ D.2:加 is_heartbeat INTEGER DEFAULT 0 列)
    const QString createHistory = R"(
        CREATE TABLE IF NOT EXISTS history (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            variable_name TEXT NOT NULL,
            timestamp INTEGER NOT NULL,    -- Unix 毫秒
            value REAL NOT NULL,
            quality INTEGER NOT NULL,       -- 0=Good, 1=Bad, 2=Stale(跟 Variable::Quality 枚举一致)
            is_heartbeat INTEGER DEFAULT 0 -- ★ D.2:1=心跳(60s 一次确认数据流活着),0=变化检测
        )
    )";
    if (!q.exec(createHistory)) {
        qCritical() << "[HistoryRecorder] CREATE history 失败:" << q.lastError().text();
        return false;
    }

    // 索引(query 用)
    const QString createIndex = R"(
        CREATE INDEX IF NOT EXISTS idx_history_var_time
        ON history(variable_name, timestamp)
    )";
    if (!q.exec(createIndex)) {
        qCritical() << "[HistoryRecorder] CREATE INDEX 失败:" << q.lastError().text();
        return false;
    }

    // ★ D.2:对 D.1 时期生成的旧 DB(无 is_heartbeat 列)做轻量 ALTER
    if (!migrateAddHeartbeatColumn()) {
        return false;
    }

    return true;
}

// ★ D.2:对 D.1 时期生成的旧 DB 做 ALTER TABLE 兼容升级
//   用 PRAGMA table_info 探测列存在性,缺则 ALTER TABLE history ADD COLUMN is_heartbeat INTEGER DEFAULT 0;
//   幂等 — 已存在列则跳过。
bool HistoryRecorder::migrateAddHeartbeatColumn() {
    if (!db_) return false;
    QSqlQuery q(*db_);

    // 探测 is_heartbeat 列是否存在
    if (!q.exec("PRAGMA table_info(history)")) {
        qCritical() << "[HistoryRecorder] PRAGMA table_info 失败:" << q.lastError().text();
        return false;
    }
    bool columnExists = false;
    while (q.next()) {
        if (q.value(1).toString() == QLatin1String("is_heartbeat")) {
            columnExists = true;
            break;
        }
    }
    if (columnExists) {
        return true;   // 已存在 — 无需迁移
    }

    // 旧 DB → ALTER TABLE 加列(SQLite 加列支持 DEFAULT 0,不需要重写表)
    QSqlQuery alter(*db_);
    if (!alter.exec("ALTER TABLE history ADD COLUMN is_heartbeat INTEGER DEFAULT 0")) {
        qCritical() << "[HistoryRecorder] ALTER TABLE 加 is_heartbeat 失败:" << alter.lastError().text();
        return false;
    }
    qInfo() << "[HistoryRecorder] 已升级旧 DB schema,新增 is_heartbeat 列";
    return true;
}

// ★ D.1.2:接 VariableManager 数据源
//   VariableManager 没有 valueChanged 总信号 — 我们遍历所有 Variable,
//   connect 它们的 valueChanged 信号;variableReplaced 触发"换绑"(旧断开,新连接)
void HistoryRecorder::setSource(VariableManager* varMgr) {
    if (!varMgr) return;
    varMgr_ = varMgr;

    // 监听"变量替换"信号,自动换绑
    connect(varMgr_, &VariableManager::variableReplaced,
            this, [this](Variable* oldVar, Variable* newVar) {
                if (oldVar) disconnect(oldVar, nullptr, this, nullptr);
                if (newVar) {
                    connect(newVar, &Variable::valueChanged,
                            this, &HistoryRecorder::onVariableChanged);
                }
            });

    // 遍历现有 Variable 全部 connect
    for (Variable* v : varMgr->allVariables()) {
        connect(v, &Variable::valueChanged,
                this, &HistoryRecorder::onVariableChanged);
    }
    qInfo() << "[HistoryRecorder] 绑定 VariableManager 数据源,共"
            << varMgr->allVariables().size() << "个变量";

    // ★ D.2:首次绑定 → 启动心跳 timer(允许重复 setSource 时不重启)
    if (!heartbeatTimer_) {
        heartbeatTimer_ = new QTimer(this);
        // ★ CoarseTimer 精度 5%(50ms 最小)— 兼顾生产 60s(±3s)+ 测试 200ms(可命中);
        //   不用 VeryCoarseTimer 是因为它会被四舍五入到 1000ms,测试心跳永远触发不了。
        heartbeatTimer_->setTimerType(Qt::CoarseTimer);
        connect(heartbeatTimer_, &QTimer::timeout,
                this, &HistoryRecorder::onHeartbeatTick);
    }
    if (heartbeatIntervalMs_ > 0 && !heartbeatTimer_->isActive()) {
        heartbeatTimer_->start(heartbeatIntervalMs_);
        qInfo() << "[HistoryRecorder] 心跳 timer 已启动,间隔" << heartbeatIntervalMs_ << "ms";
    }
}

// ★ D.2:start / stop / isRunning 控制心跳 timer
void HistoryRecorder::start() {
    if (!heartbeatTimer_) {
        heartbeatTimer_ = new QTimer(this);
        heartbeatTimer_->setTimerType(Qt::CoarseTimer);
        connect(heartbeatTimer_, &QTimer::timeout,
                this, &HistoryRecorder::onHeartbeatTick);
    }
    if (heartbeatIntervalMs_ > 0) {
        heartbeatTimer_->start(heartbeatIntervalMs_);
        qInfo() << "[HistoryRecorder] 心跳 timer start," << heartbeatIntervalMs_ << "ms";
    }
}

void HistoryRecorder::stop() {
    if (heartbeatTimer_ && heartbeatTimer_->isActive()) {
        heartbeatTimer_->stop();
        qInfo() << "[HistoryRecorder] 心跳 timer stop";
    }
}

bool HistoryRecorder::isRunning() const {
    return heartbeatTimer_ && heartbeatTimer_->isActive();
}

// ★ D.2:心跳 tick — 遍历所有 Variable 各写一行 is_heartbeat=1
//   关键设计:心跳是"确认数据流还活着"的标志,即使值不变也写;
//   D.2 Trend 画曲线时不必区分 is_heartbeat(两种都画),但做"健康度检查"时可只查心跳行。
//
//   ★★ 2026-10-21:读这张表的人一定要知道的一件事 ★★
//   quality=0 是 Good,但它【也是 Variable 的初始值】——
//   一个从来没被读过的变量,quality 也是 0。
//   所以"表里全是 quality=0"【不能】证明"读到了数据",
//   它只证明"没人把质量标成 Bad"。
//   想证明"真的读到了",只能看:值会不会变(而且要跟现场对得上),
//   或者看 PollScheduler 的"成功读计数"在不在涨。
//   —— 2026-10-21 查"16.5 小时零轮询流量"时,这条注释的方向是反的
//     (写着 0=Bad 1=Good),害我把 15024 行 Good 读成"读成功了"。
void HistoryRecorder::onHeartbeatTick() {
    if (!isOpen() || !varMgr_) return;

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    int written = 0;
    for (Variable* v : varMgr_->allVariables()) {
        if (!v) continue;
        const QString varName = v->plcId() + "." + v->name();  // "plc1.Speed"

        QSqlQuery q(*db_);
        q.prepare(R"(
            INSERT INTO history (variable_name, timestamp, value, quality, is_heartbeat)
            VALUES (?, ?, ?, ?, 1)
        )");
        q.addBindValue(varName);
        q.addBindValue(now);
        q.addBindValue(v->value().toDouble());
        q.addBindValue(static_cast<int>(v->quality()));

        if (q.exec()) {
            ++written;
        } else {
            qWarning() << "[HistoryRecorder] 心跳 INSERT 失败:" << q.lastError().text()
                       << "var=" << varName;
        }
    }
    if (written > 0) {
        qInfo() << "[HistoryRecorder] 心跳写了" << written << "行";
    }
}

// ★ D.1.2:采样策略判断
bool HistoryRecorder::shouldWrite(const QString& varName, double value) {
    const QDateTime now = QDateTime::currentDateTime();
    const QDateTime last = lastWrite_.value(varName);
    const double lastV = lastValue_.value(varName);

    // 条件 1:距离上次写 < sampleIntervalMs → 跳过
    if (last.isValid() && last.msecsTo(now) < sampleIntervalMs_) {
        return false;
    }

    // 条件 2:变化超阈值(只有启用 changeThreshold_ > 0 才检查)
    if (changeThreshold_ > 0.0 && qAbs(value - lastV) < changeThreshold_) {
        return false;
    }

    return true;
}

// ★ D.1.2:valueChanged 槽(★ D.2:写 is_heartbeat=0 — 区别于心跳行)
void HistoryRecorder::onVariableChanged(const Variable& v) {
    if (!isOpen()) return;

    const QString varName = v.plcId() + "." + v.name();  // "plc1.Speed"
    const double value = v.value().toDouble();

    if (!shouldWrite(varName, value)) return;

    QSqlQuery q(*db_);
    q.prepare(R"(
        INSERT INTO history (variable_name, timestamp, value, quality, is_heartbeat)
        VALUES (?, ?, ?, ?, 0)
    )");
    q.addBindValue(varName);
    q.addBindValue(QDateTime::currentMSecsSinceEpoch());
    q.addBindValue(value);
    q.addBindValue(static_cast<int>(v.quality()));

    if (!q.exec()) {
        qWarning() << "[HistoryRecorder] INSERT 失败:" << q.lastError().text()
                   << "var=" << varName << "value=" << value;
        return;
    }

    // 更新采样状态
    lastWrite_[varName] = QDateTime::currentDateTime();
    lastValue_[varName] = value;
}

// ★ D.1.2:环形缓冲清理
void HistoryRecorder::cleanupExpired() {
    if (!isOpen()) return;
    const qint64 cutoffMs = QDateTime::currentMSecsSinceEpoch()
                            - static_cast<qint64>(retentionDays_) * 24 * 3600 * 1000;
    QSqlQuery q(*db_);
    q.prepare("DELETE FROM history WHERE timestamp < ?");
    q.addBindValue(cutoffMs);
    if (!q.exec()) {
        qWarning() << "[HistoryRecorder] cleanup 失败:" << q.lastError().text();
        return;
    }
    const int affected = q.numRowsAffected();
    if (affected > 0) {
        qInfo() << "[HistoryRecorder] cleanup 删除" << affected << "条过期记录(> "
                << retentionDays_ << "天)";
    }
}

// ★ D.1.2:查询接口(D.2 Trend 控件用)
QVector<QPair<QDateTime, double>> HistoryRecorder::query(
    const QString& variableName,
    const QDateTime& from,
    const QDateTime& to)
{
    QVector<QPair<QDateTime, double>> result;
    if (!isOpen()) return result;

    QSqlQuery q(*db_);
    q.prepare(R"(
        SELECT timestamp, value FROM history
        WHERE variable_name = ? AND timestamp >= ? AND timestamp <= ?
        ORDER BY timestamp ASC
    )");
    q.addBindValue(variableName);
    q.addBindValue(from.toMSecsSinceEpoch());
    q.addBindValue(to.toMSecsSinceEpoch());

    if (!q.exec()) {
        qWarning() << "[HistoryRecorder] query 失败:" << q.lastError().text();
        return result;
    }

    while (q.next()) {
        const qint64 ts = q.value(0).toLongLong();
        const double val = q.value(1).toDouble();
        result.append({QDateTime::fromMSecsSinceEpoch(ts), val});
    }
    return result;
}

// ★ D.1.2:默认路径 %APPDATA%\QianWei\HMI\history.db
QString HistoryRecorder::defaultDbPath() {
    // ★ 2026-10-21:改走 util::appDataDir() —— 跟 users.json / audit log / 日志
    //   同一个目录(以前是各算各的,靠"碰巧算出同一个值"才凑到一起),
    //   而且支持 HMI_DATA_DIR 覆盖。
    //   这一条是【隔离的关键】:history.db 是 SQLite,两个实例同时写会互相锁死。
    const QString base = qwhmi::util::appDataDir();
    return base + "/history.db";
}

} // namespace qwhmi::history
