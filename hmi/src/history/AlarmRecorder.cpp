// ============================================================================
// 阶段 D · D.3 · AlarmRecorder 实现
// 对应 AlarmRecorder.h
// ============================================================================

#include "history/AlarmRecorder.h"
#include "../../Alarm.h"
#include "../../AlarmManager.h"

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

namespace qwhmi::history {

AlarmRecorder::AlarmRecorder(QObject* parent)
    : QObject(parent)
{
}

AlarmRecorder::~AlarmRecorder() {
    close();
}

bool AlarmRecorder::open(const QString& dbPath) {
    if (db_) {
        qWarning() << "[AlarmRecorder] 已打开,先 close";
        return false;
    }
    if (dbPath.isEmpty()) {
        qCritical() << "[AlarmRecorder] dbPath 为空";
        return false;
    }

    QDir().mkpath(QFileInfo(dbPath).absolutePath());

    if (QSqlDatabase::contains("hmi_alarm")) {
        QSqlDatabase::removeDatabase("hmi_alarm");
    }
    db_ = new QSqlDatabase(QSqlDatabase::addDatabase("QSQLITE", "hmi_alarm"));
    db_->setDatabaseName(dbPath);
    if (!db_->open()) {
        qCritical() << "[AlarmRecorder] 打开失败:" << db_->lastError().text()
                    << "path=" << dbPath;
        delete db_;
        db_ = nullptr;
        QSqlDatabase::removeDatabase("hmi_alarm");
        return false;
    }

    if (!ensureSchema()) {
        close();
        return false;
    }

    qInfo() << "[AlarmRecorder] opened:" << dbPath;
    return true;
}

void AlarmRecorder::close() {
    if (db_) {
        db_->close();
        delete db_;
        db_ = nullptr;
        QSqlDatabase::removeDatabase("hmi_alarm");
        qInfo() << "[AlarmRecorder] closed";
    }
}

bool AlarmRecorder::isOpen() const {
    return db_ && db_->isOpen();
}

bool AlarmRecorder::ensureSchema() {
    if (!db_) return false;
    QSqlQuery q(*db_);

    // ★ D.3:alarm_history 表 — 1 行 = 1 个状态变更(4 个 action 枚举)
    //   name/level 可空:AlarmManager 已经从 activeAlarms_ erase 后才 emit 信号
    //   → 找不到完整 Alarm 数据时,只写 id + action + action_at(其他列 NULL)
    const QString createAlarmHistory = R"(
        CREATE TABLE IF NOT EXISTS alarm_history (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            alarm_id INTEGER NOT NULL,      -- Alarm.id_,跨行关联同一条报警
            name TEXT,                      -- 报警名("温度高"),可空(AlarmManager erase 后)
            level INTEGER,                  -- AlarmLevel enum,同上可空
            action INTEGER NOT NULL,        -- 0=triggered,1=acknowledged,2=recovered,3=cleared
            action_at INTEGER NOT NULL,     -- Unix 毫秒
            variable_name TEXT,             -- 关联变量名("plc1.Temperature")
            trigger_value TEXT,             -- 触发值快照
            threshold TEXT,                 -- 阈值描述
            user TEXT                       -- 确认人(预留;v1 默认空)
        )
    )";
    if (!q.exec(createAlarmHistory)) {
        qCritical() << "[AlarmRecorder] CREATE alarm_history 失败:" << q.lastError().text();
        return false;
    }

    // 索引:按时间窗查询
    if (!q.exec("CREATE INDEX IF NOT EXISTS idx_alarm_time ON alarm_history(action_at)")) {
        qCritical() << "[AlarmRecorder] CREATE INDEX 失败:" << q.lastError().text();
        return false;
    }
    // 索引:按 alarm_id 查同一条报警的全生命周期
    if (!q.exec("CREATE INDEX IF NOT EXISTS idx_alarm_id ON alarm_history(alarm_id)")) {
        qCritical() << "[AlarmRecorder] CREATE INDEX alarm_id 失败:" << q.lastError().text();
        return false;
    }
    return true;
}

bool AlarmRecorder::insertAction(qint64 alarmId, const QString& name, int level,
                                 int action, const QDateTime& at,
                                 const QString& varName, const QString& triggerValue,
                                 const QString& threshold, const QString& user) {
    if (!isOpen()) return false;
    QSqlQuery q(*db_);
    q.prepare(R"(
        INSERT INTO alarm_history
            (alarm_id, name, level, action, action_at, variable_name, trigger_value, threshold, user)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
    )");
    q.addBindValue(alarmId);
    q.addBindValue(name);
    q.addBindValue(level);
    q.addBindValue(action);
    q.addBindValue(at.toMSecsSinceEpoch());
    q.addBindValue(varName);
    q.addBindValue(triggerValue);
    q.addBindValue(threshold);
    q.addBindValue(user);
    if (!q.exec()) {
        qWarning() << "[AlarmRecorder] INSERT 失败:" << q.lastError().text();
        return false;
    }
    return true;
}

void AlarmRecorder::setSource(AlarmManager* mgr) {
    if (!mgr) return;
    mgr_ = mgr;
    connect(mgr_, &AlarmManager::alarmTriggered,
            this, &AlarmRecorder::onAlarmTriggered);
    connect(mgr_, &AlarmManager::alarmAcknowledged,
            this, &AlarmRecorder::onAlarmAcknowledged);
    connect(mgr_, &AlarmManager::alarmRecovered,
            this, &AlarmRecorder::onAlarmRecovered);
    connect(mgr_, &AlarmManager::allCleared,
            this, &AlarmRecorder::onAllCleared);
    qInfo() << "[AlarmRecorder] 绑定 AlarmManager 数据源";
}

void AlarmRecorder::onAlarmTriggered(const Alarm& alarm) {
    insertAction(alarm.id_, alarm.name, static_cast<int>(alarm.level),
                 /*action=*/0, alarm.triggeredAt,
                 alarm.variableName, alarm.triggerValue, alarm.threshold);
}

void AlarmRecorder::onAlarmAcknowledged(qint64 id) {
    // ★ 主动从 activeAlarms_ 找最新一份完整数据(level/name 等)
    if (!mgr_) return;
    const auto alarms = mgr_->activeAlarms();
    for (const Alarm& a : alarms) {
        if (a.id_ == id) {
            // ★ 防御:Alarm::acknowledgedAt 默认 invalid(没经 AlarmManager::acknowledge),
            //   invalid QDateTime→toMSecsSinceEpoch() = 0 → 写入"1970-01-01"行,
            //   后续按时间窗 query 找不到。无效就用 currentDateTime 兜底。
            const QDateTime at = a.acknowledgedAt.isValid()
                                 ? a.acknowledgedAt : QDateTime::currentDateTime();
            insertAction(id, a.name, static_cast<int>(a.level),
                         /*action=*/1, at,
                         a.variableName, a.triggerValue, a.threshold);
            return;
        }
    }
    // 找不到(已 cleared 或 recovered)→ 写一行只带 id,其他留空
    insertAction(id, {}, 0, /*action=*/1, QDateTime::currentDateTime());
}

void AlarmRecorder::onAlarmRecovered(qint64 id) {
    if (!mgr_) return;
    const auto alarms = mgr_->activeAlarms();
    for (const Alarm& a : alarms) {
        if (a.id_ == id) {
            const QDateTime at = a.recoveredAt.isValid()
                                 ? a.recoveredAt : QDateTime::currentDateTime();
            insertAction(id, a.name, static_cast<int>(a.level),
                         /*action=*/2, at,
                         a.variableName, a.triggerValue, a.threshold);
            return;
        }
    }
    insertAction(id, {}, 0, /*action=*/2, QDateTime::currentDateTime());
}

void AlarmRecorder::onAllCleared() {
    // ★ 复位 / clearAll → 写一行 action=3 标记"工程师手动清"
    insertAction(0, QStringLiteral("(all cleared)"), 0,
                 /*action=*/3, QDateTime::currentDateTime());
}

QVector<AlarmRecorder::Record> AlarmRecorder::query(qint64 fromMs, qint64 toMs,
                                                   int minLevel) {
    QVector<Record> result;
    if (!isOpen()) return result;

    QSqlQuery q(*db_);
    q.prepare(R"(
        SELECT alarm_id, name, level, action, action_at,
               variable_name, trigger_value, threshold, user
        FROM alarm_history
        WHERE action_at >= ? AND action_at <= ? AND level >= ?
        ORDER BY action_at ASC
    )");
    q.addBindValue(fromMs);
    q.addBindValue(toMs);
    q.addBindValue(minLevel);
    if (!q.exec()) {
        qWarning() << "[AlarmRecorder] query 失败:" << q.lastError().text();
        return result;
    }

    while (q.next()) {
        Record r;
        r.alarmId      = q.value(0).toLongLong();
        r.name         = q.value(1).toString();
        r.level        = q.value(2).toInt();
        r.action       = q.value(3).toInt();
        r.at           = QDateTime::fromMSecsSinceEpoch(q.value(4).toLongLong());
        r.variableName = q.value(5).toString();
        r.triggerValue = q.value(6).toString();
        r.threshold    = q.value(7).toString();
        r.user         = q.value(8).toString();
        result.append(r);
    }
    return result;
}

int AlarmRecorder::totalCount() const {
    if (!isOpen()) return 0;
    QSqlQuery q(*db_);
    if (!q.exec("SELECT COUNT(*) FROM alarm_history")) return 0;
    if (q.next()) return q.value(0).toInt();
    return 0;
}

} // namespace qwhmi::history