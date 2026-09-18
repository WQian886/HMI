// ============================================================================
// 阶段 D · D.1 · 历史数据记录器(SQLite + Qt SQL)
//
// 文件职责:监听 VariableManager::valueChanged,把变量值 + timestamp 写入 SQLite;
//   支持采样策略(每 N 秒 / 变化超阈值 才写)+ 环形缓冲(> N 天自动删);
//   提供 query 接口给 D.2 Trend 控件用。
//
// 关键设计:
//   - Qt SQL 内置 SQLite 驱动,零外部依赖
//   - 单例模式(类似 AuditLog,B.5 已有先例)
//   - 数据库默认路径:%APPDATA%\QianWei\HMI\history.db
//   - 表 schema:history(id, variable_name, timestamp, value, quality)
//   - 索引:(variable_name, timestamp) 给 query 用
//
// 关联:D.2 Trend 控件,VariableManager.cpp
// ============================================================================

#pragma once

#include <QObject>
#include <QString>
#include <QDateTime>
#include <QHash>
#include <QPair>
#include <QVector>

class QSqlDatabase;
class Variable;
class VariableManager;

namespace qwhmi::history {

class HistoryRecorder : public QObject {
    Q_OBJECT
public:
    explicit HistoryRecorder(QObject* parent = nullptr);
    ~HistoryRecorder() override;

    // ★ D.1.2:打开/创建 SQLite 数据库(dbPath 通常 %APPDATA%\QianWei\HMI\history.db)
    bool open(const QString& dbPath);
    void close();
    bool isOpen() const;

    // ★ D.1.2:设置 VariableManager 数据源 + 自动 connect valueChanged
    void setSource(VariableManager* varMgr);

    // ★ D.1.2:采样策略
    //   - sampleIntervalMs: 距离上次写 < N ms 跳过(防高频写入,默认 1000ms)
    //   - changeThreshold: 变化 < threshold 跳过(0 = 不启用)
    void setSampleInterval(int ms) { sampleIntervalMs_ = ms; }
    void setChangeThreshold(double t) { changeThreshold_ = t; }

    // ★ D.1.2:环形缓冲保留天数(> N 天的记录自动删除,默认 30 天)
    void setRetentionDays(int days) { retentionDays_ = days; }
    void cleanupExpired();   // 手动触发清理

    // ★ D.1.2:查询接口(D.2 Trend 控件用)
    //   返回:[(timestamp, value), ...] 按时间升序
    QVector<QPair<QDateTime, double>> query(
        const QString& variableName,
        const QDateTime& from,
        const QDateTime& to);

    // 默认数据库路径
    static QString defaultDbPath();

private slots:
    // ★ D.1.2:监听 Variable.valueChanged(由 setSource 内部 connect)
    void onVariableChanged(const Variable& v);

private:
    // ★ D.1.2:初始化数据库 schema
    bool ensureSchema();

    // ★ D.1.2:采样策略判断(满足任一就写)
    bool shouldWrite(const QString& varName, double value);

    QSqlDatabase* db_ = nullptr;     // heap 持有 QSqlDatabase(QObject 生命周期复杂)
    VariableManager* varMgr_ = nullptr;

    // ★ D.1.2:采样状态
    QHash<QString, QDateTime> lastWrite_;      // varName → 上次写时间
    QHash<QString, double>    lastValue_;      // varName → 上次写值(用于 changeThreshold)

    int sampleIntervalMs_ = 1000;  // 默认 1 秒
    double changeThreshold_ = 0.0;  // 默认不启用
    int retentionDays_ = 30;        // 默认保留 30 天
};

} // namespace qwhmi::history
