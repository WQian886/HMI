// 阶段 6.2(2026-08-07):Project 类
//   主存读入口:saveToFile 把当前工程状态 → JSON 文件
//   loadFromFile 是 6.3 阶段,本阶段只写空壳(返回 false)
//
//   【设计】Project 持有 4 个管理器指针 + 1 个 TcpClient 指针(通讯)
//          saveToFile 不接管生命周期,只读
//   【取舍】TcpClient 用于拿 host/port(阶段 6.2 getter 已经加好)
//          Modbus 详细参数暂时硬编码在 saveToFile 里(以后可加 ModbusMaster getter)
//   【参照】docs/专题/阶段6_schema设计.md

#pragma once

#include <QObject>
#include <QString>

class QJsonObject;

class VariableManager;
class AlarmManager;
class ActionManager;
class PollScheduler;   // ★ 6.3 新增:ThresholdAlarmChecker 构造需要 PollScheduler*
class TcpClient;

class Project : public QObject {
    Q_OBJECT
public:
    // ★ C.0(2026-09-17):schema_version 演进框架
    //   - 老文件 schema_version="1.0"(字符串)
    //   - v2 改成整数(2)+ 顶级 plcs[] 数组(替代原 communications 单 PLC)
    //   - 每次加字段时 kCurrentSchemaVersion++ + 写新的 migrateVxToVxPlus1 函数
    static constexpr int kCurrentSchemaVersion = 2;

    explicit Project(VariableManager* varMgr,
                     AlarmManager* alarmMgr,
                     ActionManager* actionMgr,
                     PollScheduler* pollScheduler,   // ★ 6.3 新增
                     TcpClient* tcp,
                     QObject* parent = nullptr);
    ~Project() override;

    // 主入口:把当前工程状态写到 JSON 文件
    // 成功返回 true;失败(写文件失败 / JSON 构造失败)返回 false + emit loadError
    bool saveToFile(const QString& path);

    // 6.3 阶段用,本阶段只写空壳(返回 false)
    bool loadFromFile(const QString& path);

    // ★ C.0(2026-09-17):schema 迁移公开 API(单测和外部工具可直接用)
    //   - migrateTo:从 fromVersion 链式迁移到 toVersion(内部按需调 migrateVxToVxPlus1)
    //   - migrateV1ToV2:v1 → v2 单步迁移(单独暴露给单测)
    //   返回 false 表示迁移失败(原 JSON 不变,调用方报 loadError)
    bool migrateTo(QJsonObject& root, int fromVersion, int toVersion);
    bool migrateV1ToV2(QJsonObject& root);

signals:
    void saved(const QString& path);
    void loaded(const QString& path);   // ★ 6.3:loadFromFile 成功(给 hmi 重绑 UI 用)
    void loadError(const QString& reason);

private:
    // bool migrateV2ToV3(QJsonObject& root);  // 未来 C.1/C.5 加新字段时启用

    VariableManager* varMgr_;
    AlarmManager*    alarmMgr_;
    ActionManager*   actionMgr_;
    PollScheduler*   pollScheduler_;
    TcpClient*       tcp_;
};