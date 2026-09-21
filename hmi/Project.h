// 阶段 6.2(2026-08-07):Project 类
//   主存读入口:saveToFile 把当前工程状态 → JSON 文件
//   loadFromFile 是 6.3 阶段,本阶段只写空壳(返回 false)
//
//   ★ E.3(2026-09-21):JSON 存读扩展
//   - schema_version 2 → 3 + migrateV2ToV3
//   - saveToFile/loadFromFile 加 controls 参数 + controlsLoaded 信号
//   - 画布控件(ControlBase)用 propertiesToJson 反射序列化
//
//   【设计】Project 持有 4 个管理器指针 + 1 个 TcpClient 指针(通讯)
//          saveToFile 不接管生命周期,只读
//   【取舍】TcpClient 用于拿 host/port(阶段 6.2 getter 已经加好)
//          Modbus 详细参数暂时硬编码在 saveToFile 里(以后可加 ModbusMaster getter)
//   【参照】docs/专题/阶段6_schema设计.md

#pragma once

#include <QObject>
#include <QString>
#include <QList>

class QJsonObject;
class QJsonArray;

namespace qwhmi::widgets { class ControlBase; }

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
    //   - ★ E.3(2026-09-21):v3 加 screens[] 数组(画布控件)
    //   - 每次加字段时 kCurrentSchemaVersion++ + 写新的 migrateVxToVxPlus1 函数
    static constexpr int kCurrentSchemaVersion = 3;

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

    // ★ E.3:带 controls 的 saveToFile — 把画布控件也序列化到 screens[0].controls
    //   不接管 controls 生命周期(由 EditorMainWindow 拥有)
    bool saveToFile(const QString& path, const QList<qwhmi::widgets::ControlBase*>& controls);

    // 6.3 阶段用,本阶段只写空壳(返回 false)
    bool loadFromFile(const QString& path);

    // ★ C.0(2026-09-17):schema 迁移公开 API(单测和外部工具可直接用)
    //   - migrateTo:从 fromVersion 链式迁移到 toVersion(内部按需调 migrateVxToVxPlus1)
    //   - migrateV1ToV2:v1 → v2 单步迁移(单独暴露给单测)
    //   - migrateV2ToV3:v2 → v3 单步迁移(E.3 加 screens)
    //   返回 false 表示迁移失败(原 JSON 不变,调用方报 loadError)
    bool migrateTo(QJsonObject& root, int fromVersion, int toVersion);
    bool migrateV1ToV2(QJsonObject& root);
    bool migrateV2ToV3(QJsonObject& root);

    // ★ E.3:控件 JSON 序列化静态工具(供测试和 EditorMainWindow 直接调用)
    //   - controlsToJson:遍历控件列表 → JSON array(走 ControlBase::propertiesToJson 反射)
    //   - loadControlsFromJson:从 JSON array 重建控件列表(用 factory 按 type 字符串 new)
    //     factory 参数让调用方决定所有权(本函数不 delete 返回的指针)
    static QJsonArray controlsToJson(const QList<qwhmi::widgets::ControlBase*>& controls);
    // factory: 输入 typeName(如 "BarControl"),返回 new 出来的 ControlBase*(失败返回 nullptr)
    // outControls: 成功创建的控件列表(调用方负责 delete)
    // 返回成功创建的控件数
    static int loadControlsFromJson(const QJsonArray& arr,
                                    qwhmi::widgets::ControlBase* (*factory)(const QString& typeName),
                                    QList<qwhmi::widgets::ControlBase*>& outControls);

signals:
    void saved(const QString& path);
    void loaded(const QString& path);   // ★ 6.3:loadFromFile 成功(给 hmi 重绑 UI 用)
    // ★ E.3:loadFromFile 后发出,带刚加载的控件列表(EditorMainWindow 监听后建到画布)
    void controlsLoaded(const QList<qwhmi::widgets::ControlBase*>& controls);
    void loadError(const QString& reason);

private:
    // 共享 saveToFile 内部:构造 root JSON(不含 controls),返回 true/false 表示 IO 成功
    bool saveRootToFile(const QString& path, const QJsonObject& root);

    // 共享 loadFromFile 内部:读文件 + 解析 + schema 校验 + migrate + 应用 vars/alarms/actions/plcs
    // 返回 root QJsonObject(供 E.3 进一步解析 screens[0].controls);失败时 root 设为空
    bool loadRootFromFile(const QString& path, QJsonObject& outRoot);

    VariableManager* varMgr_;
    AlarmManager*    alarmMgr_;
    ActionManager*   actionMgr_;
    PollScheduler*   pollScheduler_;
    TcpClient*       tcp_;
};