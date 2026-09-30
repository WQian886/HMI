// ============================================================================
// 阶段 D · D.3 · 报警持久化(SQLite + Qt SQL)
//
// 文件职责:监听 AlarmManager 的 alarmTriggered/alarmAcknowledged/alarmRecovered/
//   allCleared 4 个信号 → INSERT 报警历史;提供 query 接口给 D.3 UI 用。
//
// 关键设计:
//   - 对称 HistoryRecorder:同一 DB 文件(history.db),新表 alarm_history
//   - 1 行 = 1 个状态变更(同一条报警会写 4 行:triggered → acked → recovered → cleared)
//   - 触发/确认/恢复/清除 4 个 action enum,完整保留事件时序
//   - 用 Alarm.id_ 跨行关联(报警生命周期)
//   - 不与 HistoryRecorder 共享 QSqlDatabase — 各开各的("hmi_alarm")
//
// 关联:D.1 HistoryRecorder,D.3 报警历史表,AlarmManager.cpp, hmi.cpp
// ============================================================================

#pragma once

#include <QObject>
#include <QString>
#include <QDateTime>
#include <QVector>
#include <QPair>

class QSqlDatabase;
struct Alarm;            // ★ forward decl 必须跟 Alarm.h 一致用 struct — 教训:
                         //   moc 看 forward decl 生成 class Alarm,跟 .cpp 里 struct Alarm
                         //   MSVC 名字修饰 V(class) ≠ U(struct) → LNK2019
class AlarmManager;

namespace qwhmi::history {

class AlarmRecorder : public QObject {
    Q_OBJECT
public:
    explicit AlarmRecorder(QObject* parent = nullptr);
    ~AlarmRecorder() override;

    // ★ D.3:打开报警历史表(同 history.db 文件,不同表 alarm_history)
    bool open(const QString& dbPath);
    void close();
    bool isOpen() const;

    // ★ D.3:接 AlarmManager → 自动 connect 4 个信号
    void setSource(AlarmManager* mgr);

    // ★ D.3:查询接口(D.3 UI 用)
    //   返回:[(timestamp, "name|level|action|variable|value|threshold"), ...]
    //   action:0=triggered, 1=acknowledged, 2=recovered, 3=cleared
    struct Record {
        qint64    alarmId = 0;
        QString   name;
        int       level  = 0;       // AlarmLevel enum 值
        int       action = 0;       // 0/1/2/3
        QDateTime at;                // 事件时间
        QString   variableName;
        QString   triggerValue;
        QString   threshold;
        QString   user;             // 谁 ack 的(预留)
    };
    QVector<Record> query(qint64 fromMs, qint64 toMs,
                          int minLevel = 0);  // minLevel=0 全收

    // ★ D.3:总报警次数(给 health 卡片用,后续 H 阶段)
    int totalCount() const;

private slots:
    // ★ D.3:接 AlarmManager 信号
    void onAlarmTriggered(const Alarm& alarm);
    void onAlarmAcknowledged(qint64 id);
    // ★ F1(2026-09-30):多带一个 Alarm —— 恢复后记录已不在 activeAlarms_ 里,
    //   只收 id 的话只能写残缺行(历史查询出来是只有 id 的孤儿)。详见 .cpp。
    void onAlarmRecovered(qint64 id, const Alarm& recovered);
    void onAllCleared();

private:
    bool ensureSchema();
    // ★ INSERT helper:写一行 action 记录
    bool insertAction(qint64 alarmId, const QString& name, int level, int action,
                      const QDateTime& at, const QString& varName = {},
                      const QString& triggerValue = {}, const QString& threshold = {},
                      const QString& user = {});

    QSqlDatabase* db_ = nullptr;
    AlarmManager* mgr_ = nullptr;
};

} // namespace qwhmi::history