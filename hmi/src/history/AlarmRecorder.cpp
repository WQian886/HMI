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

namespace {

// ============================================================================
// ★ F2(2026-10-10):alarm_history 的列清单 —— schema 的唯一真相源
//
//   CREATE TABLE 由它拼出来,老库升级也逐列拿它比对。两处共用一份,
//   才不会出现"建表写死了 10 列、迁移路径却按 9 列补"这种静默漂移。
//
//   【为什么不用 PRAGMA user_version】
//     user_version 是【文件级】唯一整数,而 AlarmRecorder 和 HistoryRecorder
//     打开的是同一个 history.db(hmi.cpp:531)。只要 HistoryRecorder 以后也
//     要记版本号,两个类就会互相覆盖对方的整数 —— 谁后 open 谁赢,
//     升级顺序一变就错,而且不报任何错。
//     探测式(PRAGMA table_info 看列在不在)没有这个问题:
//     它问的是"表实际长什么样",不是"文件自称第几版"。
//     这也是 HistoryRecorder D.2 的既有做法(is_heartbeat),沿用它。
//
//   【为什么是列清单而不是纯 PRAGMA 循环】
//     纯循环只能补"已声明的列",新加一列时人容易忘了在两处都登记。
//     清单是单点:加列只改这一处,建表和升级同时跟上。
// ============================================================================
struct ColumnSpec {
    const char* name;
    const char* decl;
};

//   name/level 可空:AlarmManager 已经从 activeAlarms_ erase 后才 emit 信号
//   → 找不到完整 Alarm 数据时,只写 id + action + action_at(其他列 NULL)
const ColumnSpec kColumns[] = {
    { "id",             "INTEGER PRIMARY KEY AUTOINCREMENT" },
    { "alarm_id",       "INTEGER NOT NULL"   },  // Alarm.id_,跨行关联同一条报警
    { "name",           "TEXT"               },  // 报警名("温度高"),可空(erase 后)
    { "level",          "INTEGER"            },  // AlarmLevel enum,同上可空
    { "action",         "INTEGER NOT NULL"   },  // 0=触发 1=确认 2=恢复 3=清除
    { "action_at",      "INTEGER NOT NULL"   },  // Unix 毫秒
    { "variable_name",  "TEXT"               },  // "plc1.Temperature"
    { "trigger_value",  "TEXT"               },  // 触发值快照
    { "threshold",      "TEXT"               },  // 阈值描述
    { "user",           "TEXT"               },  // 确认人(预留;v1 默认空)
    // ★ F6(2026-10-10):区域。加这一行就同时完成两件事 ——
    //   新库建表带 area 列;老库走 addColumnIfMissing 补上。
    //   老行的 area 是 NULL → 读回来是空串 → 归入"未分配"桶。
    //   【为什么不写 NOT NULL DEFAULT ''】SQLite 的 ALTER TABLE ADD COLUMN
    //   不允许带非空默认值,强行写会在老库上直接报错。加可空列是正确的。
    { "area",           "TEXT"               },  // 现场区域("一号泵房"),可空 = 未分配
};

} // namespace

// ★ 加一列时,记得同时做两件事:
//   1. 在上面 kColumns 里登记(否则新库建不出来)
//   2. 在 insertAction / query 里用上(否则加了也没人读)

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

    // ---- 1. 建表(列定义来自 kColumns,见文件头) ------------------------
    //   新库走这条;老库 CREATE TABLE IF NOT EXISTS 是空操作,交给第 3 步。
    QStringList colDefs;
    for (const auto& c : kColumns) {
        colDefs << QStringLiteral("    %1 %2").arg(QLatin1String(c.name),
                                                      QLatin1String(c.decl));
    }
    const QString createAlarmHistory =
        QStringLiteral("CREATE TABLE IF NOT EXISTS alarm_history (\n%1\n)")
            .arg(colDefs.join(QStringLiteral(",\n")));
    if (!q.exec(createAlarmHistory)) {
        qCritical() << "[AlarmRecorder] CREATE alarm_history 失败:" << q.lastError().text();
        return false;
    }

    // ---- 2. 索引 -------------------------------------------------------
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

    // ---- 3. ★ 老库补列(2026-10-10 新增) --------------------------------
    //   上面的 CREATE TABLE IF NOT EXISTS 对已存在的表【什么也不做】,
    //   所以老库少了列,它还是少 —— 后面 INSERT 那列直接 "no such column" 报错,
    //   而且是在用户现场第一次写报警时才炸。逐列探测补齐。
    for (const auto& c : kColumns) {
        if (!addColumnIfMissing(QStringLiteral("alarm_history"),
                                QLatin1String(c.name), QLatin1String(c.decl))) {
            return false;
        }
    }
    return true;
}

// ★ F2(2026-10-10):探测式加列 —— 缺才补,幂等。
//   为什么是"探测"而不是"记版本号":user_version 是文件级唯一整数,而
//   AlarmRecorder 与 HistoryRecorder 共用同一个 history.db(hmi.cpp:531),
//   两个类各自记版本会互相覆盖。详见文件头 kColumns 的注释。
bool AlarmRecorder::addColumnIfMissing(const QString& table, const QString& column,
                                       const QString& decl) {
    if (!db_) return false;

    {
        QSqlQuery info(*db_);
        if (!info.exec(QStringLiteral("PRAGMA table_info(%1)").arg(table))) {
            qCritical() << "[AlarmRecorder] PRAGMA table_info 失败:" << info.lastError().text();
            return false;
        }
        while (info.next()) {
            if (info.value(1).toString() == column) {
                return true;   // 已存在 —— 无需迁移
            }
        }
    }

    // 表不存在时 addColumn 会失败;那种情况不该走到这(createSchema 刚建过),
    // 真发生了说明上面的 CREATE 失败了,报出来比静默跳过强。
    QSqlQuery alter(*db_);
    if (!alter.exec(QStringLiteral("ALTER TABLE %1 ADD COLUMN %2 %3")
                        .arg(table, column, decl))) {
        qCritical() << "[AlarmRecorder] ALTER TABLE 加列失败:" << alter.lastError().text()
                    << table << column;
        return false;
    }
    qInfo() << "[AlarmRecorder] 旧库升级:新增列" << table << column;
    return true;
}

bool AlarmRecorder::insertAction(qint64 alarmId, const QString& name, int level,
                                 int action, const QDateTime& at,
                                 const QString& varName, const QString& triggerValue,
                                 const QString& threshold, const QString& user,
                                 const QString& area) {
    if (!isOpen()) return false;
    QSqlQuery q(*db_);
    q.prepare(R"(
        INSERT INTO alarm_history
            (alarm_id, name, level, action, action_at, variable_name,
             trigger_value, threshold, user, area)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
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
    // ★ F6:区域。最后一个,默认空 —— 现有 4 个调用点(acknowledged / recovered /
    //   allCleared / triggered)里,只有 triggered 拿得到完整 Alarm,其余几个
    //   兜底行本来就只有 id + 时间,区域理应同样空着。
    q.addBindValue(area);
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
                 alarm.variableName, alarm.triggerValue, alarm.threshold,
                 /*user=*/{}, alarm.area);
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
                         a.variableName, a.triggerValue, a.threshold,
                         /*user=*/{}, a.area);
            return;
        }
    }
    // 找不到(已 cleared 或 recovered)→ 写一行只带 id,其他留空
    insertAction(id, {}, 0, /*action=*/1, QDateTime::currentDateTime());
}

// ★ F1(2026-09-30)重写:直接用信号带来的 Alarm,不再回头遍历 activeAlarms()。
//
// 【原来错在哪】AlarmManager::onCheckerRecovered 的顺序是 erase → emit,
// 恢复的记录在信号发出前就已经没了。而这里靠遍历 mgr_->activeAlarms()
// 找完整数据 —— 必然找不到,走到末尾兜底:
//     insertAction(id, {}, 0, action=2, now())
// 结果:历史表里每一条"恢复"行的 name/level/variable/value/threshold 全空。
// 事故追溯查出来是一行只有 id 和时间的孤儿。
//
// 【证据】tst_AlarmRecorder::recoveredRow_actuallyCarriesNameAndLevel(红→绿)
//
// 【为什么原来那种"找不到就写空行"的兜底要保留】
//   理论上信号带了内容就不该有找不到的情况。但兜底还留着,因为
//   历史表是"宁可有残缺的一行,也不能整条丢失"—— 写空行至少还知道
//   "这条 id 在这个时刻恢复过"。真出现了要 qWarning,不能静默。
void AlarmRecorder::onAlarmRecovered(qint64 id, const Alarm& recovered) {
    if (recovered.name.isEmpty()) {
        // 信号带了内容却还是空的 —— 说明有别的路径在发这个信号。
        // 静默写空行会把这个"应该有内容却没有"藏起来,和原来那个 bug 同一形状。
        // 用本文件其余地方一致的 qWarning(不引 auth 的日志 category,
        // 那是为了 alarm.ack 审计才有的,这里不是审计事件)。
        qWarning().noquote()
            << QStringLiteral("[AlarmRecorder] 恢复信号没带内容(alarmId=%1),将写残缺行。"
                              "这不该发生,查一下是谁发的 alarmRecovered。").arg(id);
    }
    const QDateTime at = recovered.recoveredAt.isValid()
                         ? recovered.recoveredAt : QDateTime::currentDateTime();
    insertAction(id, recovered.name, static_cast<int>(recovered.level),
                 /*action=*/2, at,
                 recovered.variableName, recovered.triggerValue, recovered.threshold,
                 /*user=*/{}, recovered.area);
}

void AlarmRecorder::onAllCleared() {
    // ★ 复位 / clearAll → 写一行 action=3 标记"工程师手动清"
    insertAction(0, QStringLiteral("(all cleared)"), 0,
                 /*action=*/3, QDateTime::currentDateTime());
}

// ★ F6:返回【空但非 null】的串 —— 这是 query() 判定"筛未分配"的唯一依据。
//   QStringLiteral("") 在 Qt6 里构造的是非 null 的空串;QString() 是 null。
//   写反的后果是"筛未分配"退化成"不筛",界面看起来正常、结果多出来一堆 ——
//   是那种不会变红、只会让人以为程序抽了风的错。
QString AlarmRecorder::unassigned() {
    static const QString s = QStringLiteral("");
    return s;
}

QVector<AlarmRecorder::Record> AlarmRecorder::query(qint64 fromMs, qint64 toMs,
                                                   int minLevel,
                                                   const QString& area) {
    QVector<Record> result;
    if (!isOpen()) return result;

    QSqlQuery q(*db_);
    // ★ F6:area 条件是【可选的】,不是固定的一段 SQL。
    //   判据是 isNull() 不是 isEmpty():null=不筛(老调用点默认,行为逐位不变),
    //   空串=筛"未分配"。见 .h 里 unassigned() 那段注释。
    const bool filtering = !area.isNull();
    QString sql = QStringLiteral(R"(
        SELECT alarm_id, name, level, action, action_at,
               variable_name, trigger_value, threshold, user, area
        FROM alarm_history
        WHERE action_at >= ? AND action_at <= ? AND level >= ?
    )");
    if (filtering) {
        // COALESCE(area,'') 是必须的:老行 area 是 NULL,而
        //   "WHERE area = ''" 匹配不到 NULL —— 会让"未分配"这个桶永远是空的,
        //   而那恰恰是老库里【全部】的记录。那就是筛选器一开就全空。
        sql += QStringLiteral(" AND COALESCE(area, '') = ?");
    }
    sql += QStringLiteral(" ORDER BY action_at ASC");

    q.prepare(sql);
    q.addBindValue(fromMs);
    q.addBindValue(toMs);
    q.addBindValue(minLevel);
    if (filtering) {
        q.addBindValue(area);
    }
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
        r.area         = q.value(9).toString();   // NULL → 空串
        result.append(r);
    }
    return result;
}

// ★ F6:下拉框要列哪些区域。查 DISTINCT 而不是把整表拉回来在内存里去重 ——
//   表会一直长(报警历史是只增不减的),全表扫描不该由一次开下拉框触发。
QStringList AlarmRecorder::distinctAreas() {
    QStringList out;
    if (!isOpen()) return out;
    QSqlQuery q(*db_);
    if (!q.exec(QStringLiteral(
            "SELECT DISTINCT COALESCE(area, '') FROM alarm_history "
            "ORDER BY 1 ASC"))) {
        qWarning() << "[AlarmRecorder] distinctAreas 失败:" << q.lastError().text();
        return out;
    }
    while (q.next()) out << q.value(0).toString();
    return out;
}

int AlarmRecorder::totalCount() const {
    if (!isOpen()) return 0;
    QSqlQuery q(*db_);
    if (!q.exec("SELECT COUNT(*) FROM alarm_history")) return 0;
    if (q.next()) return q.value(0).toInt();
    return 0;
}

} // namespace qwhmi::history