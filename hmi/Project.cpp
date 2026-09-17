// 阶段 6.2(2026-08-07):Project 类实现

#include "Project.h"
#include "VariableManager.h"
#include "Variable.h"
#include "AlarmManager.h"
#include "Alarm.h"
#include "ActionManager.h"
#include "ThresholdAlarmChecker.h"   // ★ 6.3 需要:loadFromFile 构造 Checker
#include "ThresholdAlarmChecker.h"   // ★ 6.2 需要:dynamic_cast 用
#include "TcpClient.h"
#include "JsonSerializer.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QSaveFile>
#include <QDebug>

Project::Project(VariableManager* varMgr,
                 AlarmManager* alarmMgr,
                 ActionManager* actionMgr,
                 PollScheduler* pollScheduler,
                 TcpClient* tcp,
                 QObject* parent)
    : QObject(parent),
      varMgr_(varMgr),
      alarmMgr_(alarmMgr),
      actionMgr_(actionMgr),
      pollScheduler_(pollScheduler),
      tcp_(tcp) {}

Project::~Project() = default;

// ===== saveToFile 主实现 =====
bool Project::saveToFile(const QString& path) {
    if (!varMgr_ || !alarmMgr_ || !actionMgr_) {
        emit loadError("管理器指针为空,saveToFile 中止");
        return false;
    }

    // ★ IMPROVE-6.2(2026-08-07):构造顶层 JSON
    QJsonObject root;

    // ★ C.0(2026-09-17):schema_version 从 "1.0"(字符串)→ 2(数字)
    //   原因:C.5 多 PLC 准备 + 字符串版本号不好比较
    root["schema_version"] = kCurrentSchemaVersion;  // 2

    // project_name / 时间戳(暂时硬编码,6.3 加用户输入)
    root["project_name"] = "HMI Demo Project";
    QString nowIso = QDateTime::currentDateTime().toString(Qt::ISODate);
    root["created_at"]   = nowIso;
    root["updated_at"]   = nowIso;

    // ★ C.0(2026-09-17):plcs 顶级数组(替代原 communications 单 PLC)
    //   - 现在 C 阶段只有 modbus_tcp,但 schema 提前留多 PLC 位置
    //   - 通讯参数从 tcp_ 拿;C.1 协议抽象层完成后改成 IPollDriver
    {
        QJsonArray plcsArr;
        QJsonObject plc;
        plc["id"] = "plc1";                        // ★ C.5 多 PLC 时改成实际 ID
        plc["protocol"] = "modbus_tcp";             // 当前只支持 modbus_tcp(C.1 后会扩展)
        if (tcp_) {
            plc["host"] = tcp_->host();
            plc["port"] = tcp_->port();
        } else {
            plc["host"] = "127.0.0.1";              // 没 tcp_ 时 fallback
            plc["port"] = 502;
        }
        QJsonObject modbus;
        modbus["timeout_ms"]       = TcpClient::kConnectTimeoutMs;
        modbus["max_retries"]      = TcpClient::kMaxRetries;
        modbus["poll_interval_ms"] = 100;
        plc["modbus"] = modbus;
        plcsArr.append(plc);
        root["plcs"] = plcsArr;
    }

    // ===== variables(遍历 varMgr_)=====
    {
        QJsonArray variablesArr;
        for (Variable* v : varMgr_->allVariables()) {
            if (v) variablesArr.append(JsonSerializer::toJson(*v));
        }
        root["variables"] = variablesArr;
    }

    // ===== alarms(遍历 alarmMgr_->checkers(),从 ThresholdAlarmChecker 拿 CheckerConfig)=====
    {
        QJsonArray alarmsArr;
        for (AlarmChecker* c : alarmMgr_->checkers()) {
            // ★ 6.2 阶段只处理 ThresholdAlarmChecker(目前唯一具体 Checker 子类)
            //   5.x 以后加新 Checker 子类,要扩展这里或加 virtual getter
            auto* tc = dynamic_cast<ThresholdAlarmChecker*>(c);
            if (!tc) continue;
            QJsonObject alarmObj = JsonSerializer::toJson(tc->config());
            alarmObj["name"]     = c->name();                    // 报警名
            alarmObj["variable"] = tc->variable() ? tc->variable()->name() : QString();
            alarmsArr.append(alarmObj);
        }
        root["alarms"] = alarmsArr;
    }

    // ===== actions(遍历 actionMgr_->mappings())=====
    {
        QJsonArray actionsArr;
        for (const ActionManager::Mapping& m : actionMgr_->mappings()) {
            // ★ E #29(2026-09-15):Mapping.variableName 直接用,不再走裸指针
            actionsArr.append(JsonSerializer::toActionMappingJson(m.alarmName, m.variableName, m.value));
        }
        root["actions"] = actionsArr;
    }

    // ===== 写文件 =====
    //   ★ 用 QSaveFile(原子写:先写临时文件,fsync,再 rename —— 防止写到一半断电)
    //   【取舍】比 QFile::write() 安全,代价多一次磁盘 IO,但工程文件值得
    QJsonDocument doc(root);
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        emit loadError(QString("打开文件失败: %1").arg(f.errorString()));
        return false;
    }
    f.write(doc.toJson(QJsonDocument::Indented));   // Indented = 人类可读
    if (!f.commit()) {   // commit() 做 rename + 失败回滚
        emit loadError(QString("写入文件失败: %1").arg(f.errorString()));
        return false;
    }

    qDebug() << "[Project] saved to" << path;
    emit saved(path);
    return true;
}

// ===== loadFromFile 主实现(6.3)=====
//   【背景】Q4-A 拍板:JSON = 唯一真理,加载后内存 = JSON(全删干净 + 重建)
//   【流程】读文件 → 解析 JSON → 校验 schema_version → 清空现有状态 → 重建 4 类 → 失败 abort
//   【取舍】失败严格:任何引用错误(variables 不存在)立即 abort,不留半成品状态
//   【参照】Wqian 2026-08-07 阶段 6.3 开干;docs/专题/阶段6_schema设计.md
bool Project::loadFromFile(const QString& path) {
    if (!varMgr_ || !alarmMgr_ || !actionMgr_ || !pollScheduler_) {
        emit loadError("管理器指针为空,loadFromFile 中止");
        return false;
    }

    // 1. 读文件
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        emit loadError(QString("打开文件失败: %1").arg(f.errorString()));
        return false;
    }
    QByteArray data = f.readAll();
    f.close();

    // 2. 解析 JSON
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError) {
        emit loadError(QString("JSON 解析失败: %1 (offset %2)").arg(err.errorString()).arg(err.offset));
        return false;
    }
    if (!doc.isObject()) {
        emit loadError("JSON 顶层不是 object");
        return false;
    }
    QJsonObject root = doc.object();

    // 3. ★ C.0(2026-09-17):schema_version 校验 + 自动迁移
    //   v1: 字符串 "1.0" → 调 migrateV1ToV2
    //   v2: 整数 2(当前版本)→ 直接通过
    //   v3+: 整数 > 2 → 拒绝(新版本,旧 hmi 不支持)
    //   缺字段: 默认当 v1 处理(migrate)
    QJsonValue schemaVal = root["schema_version"];
    int schemaVerInt = 0;
    if (schemaVal.isString()) {
        // 老格式:"1.0" 字符串
        QString s = schemaVal.toString();
        if (s.startsWith("1.")) schemaVerInt = 1;
    } else if (schemaVal.isDouble()) {
        // 新格式:整数(实际是 int,QJsonValue 用 double 存)
        schemaVerInt = schemaVal.toInt();
    }
    // 没有 schema_version 字段也当 v1(最老的格式,默认走迁移)
    if (schemaVal.isUndefined()) schemaVerInt = 1;

    if (schemaVerInt > kCurrentSchemaVersion) {
        emit loadError(QString("schema_version %1 太新,当前 hmi 只支持到 %2")
                       .arg(schemaVerInt).arg(kCurrentSchemaVersion));
        return false;
    }
    // ★ C.0:按需调迁移函数链 v1 → v2 → ... → vN
    if (schemaVerInt < kCurrentSchemaVersion) {
        if (!migrateTo(root, schemaVerInt, kCurrentSchemaVersion)) {
            emit loadError(QString("schema 迁移失败:从 v%1 到 v%2").arg(schemaVerInt).arg(kCurrentSchemaVersion));
            return false;
        }
        qDebug() << "[Project] schema 迁移完成:v" << schemaVerInt << "→ v" << kCurrentSchemaVersion;
    }

    // 4. 清空现有状态(Q4-A 全删干净)
    varMgr_->clear();
    alarmMgr_->clearAll();
    actionMgr_->clear();

    // 5. 解析 variables(variables 必须先建好,因为 alarms/actions 引用它们)
    QJsonArray variablesArr = root["variables"].toArray();
    for (const QJsonValue& v : variablesArr) {
        Variable* var = JsonSerializer::fromJsonVariable(v.toObject(), varMgr_);
        if (!var) {
            emit loadError("variable 字段缺失或类型错误");
            return false;
        }
        if (!varMgr_->addVariable(var)) {
            delete var;   // addVariable 失败,name 重复,清理资源
            emit loadError(QString("variable \"%1\" 重复或非法").arg(var->name()));
            return false;
        }
    }

    // 6. 解析 alarms(依赖 variables 已建好)
    QJsonArray alarmsArr = root["alarms"].toArray();
    for (const QJsonValue& a : alarmsArr) {
        QJsonObject aObj = a.toObject();
        QString alarmName = aObj["name"].toString();
        QString varName   = aObj["variable"].toString();

        Variable* var = varMgr_->findVariable(varName);
        if (!var) {
            emit loadError(QString("alarm \"%1\" 引用了不存在的 variable \"%2\"").arg(alarmName).arg(varName));
            return false;
        }
        CheckerConfig cfg = JsonSerializer::checkerConfigFromJson(aObj);
        auto* checker = new ThresholdAlarmChecker(alarmName, var, pollScheduler_, cfg, alarmMgr_);
        if (!alarmMgr_->addChecker(checker)) {
            delete checker;
            emit loadError(QString("alarm \"%1\" 重复或 addChecker 失败").arg(alarmName));
            return false;
        }
    }

    // 7. 解析 actions(依赖 variables + alarms 都已建好)
    QJsonArray actionsArr = root["actions"].toArray();
    for (const QJsonValue& ac : actionsArr) {
        QJsonObject acObj = ac.toObject();
        QString alarmName = acObj["alarm_name"].toString();
        QString varName   = acObj["variable"].toString();
        QVariant writeValue = acObj["write_value"].toVariant();

        Variable* var = varMgr_->findVariable(varName);
        if (!var) {
            emit loadError(QString("action \"%1\" 引用了不存在的 variable \"%2\"").arg(alarmName).arg(varName));
            return false;
        }
        // ★ E #29(2026-09-15):addMapping 收 QString 名,直接传 varName 即可
        //   【背景】原代码需要先 findVariable 拿指针再传 — 但 Mapping 改存变量名后
        //          触发时按名查更稳,这里直接传名字更简洁(也保留上面的存在性检查)
        //   【参照】docs/改善清单.md E #29
        actionMgr_->addMapping(alarmName, varName, writeValue);
    }

    // 8. 通讯字段应用(★ C.0 改:从 plcs[0] 拿 host/port,而非旧的 communications)
    //   【背景】v1 字段名 communications,v2 改成 plcs[](多 PLC 准备)
    //   ★ C.0:migrate 已经在 step 3 把 communications 提到 plcs[0],所以这里统一读 plcs
    QJsonArray plcsArr = root["plcs"].toArray();
    if (!plcsArr.isEmpty()) {
        QJsonObject plc0 = plcsArr[0].toObject();
        QString host = plc0["host"].toString();
        int port = plc0["port"].toInt();
        if (tcp_ && !host.isEmpty() && port > 0 && port < 65536) {
            if (tcp_->host() == host && tcp_->port() == static_cast<quint16>(port)) {
                qDebug() << "[Project] 通讯参数已应用(无变化,跳过重连):" << host << ":" << port;
            } else {
                tcp_->connectTo(host, static_cast<quint16>(port));
                qDebug() << "[Project] 通讯参数已应用(切换):" << host << ":" << port;
            }
        }
    }

    qDebug() << "[Project] loaded from" << path;
    emit loaded(path);   // ★ 6.3:load 成功(hmi 收到后重绑 UI)
    return true;
}

// =============================================================================
// ★ C.0(2026-09-17):schema 演进函数链
//
// 设计原则:
//   - 迁移在内存里做,不立即写回(用户主动 save 才落盘成新版本)
//   - 每次 schema_version++ 时,加一个 migrateVxToVxPlus1 函数 + 在 migrateTo 里注册
//   - 迁移失败不丢数据,返回 false 让 loadFromFile 报错
// =============================================================================

// v1 → v2:communications 单 PLC → plcs[0] 多 PLC 数组准备
//   - schema_version: "1.0" → 2(数字)
//   - 新增 plcs[] 顶级数组,内容从 communications 提取
//   - 删 communications 字段(冗余)
bool Project::migrateV1ToV2(QJsonObject& root) {
    qDebug() << "[Project] migrateV1ToV2 开始";

    // 1. 改 schema_version 数字
    root["schema_version"] = 2;

    // 2. 如果还没 plcs 字段,从 communications 提取
    if (!root.contains("plcs")) {
        QJsonObject comm = root["communications"].toObject();
        QJsonObject plc;
        plc["id"] = "plc1";
        plc["protocol"] = comm.value("protocol").toString("modbus_tcp");
        plc["host"] = comm.value("host").toString("127.0.0.1");
        plc["port"] = comm.value("port").toInt(502);
        // 保留 modbus 子对象(timeout/max_retries/poll_interval_ms)
        if (comm.contains("modbus")) {
            plc["modbus"] = comm["modbus"];
        } else {
            // 缺 modbus 子对象 → 写默认(避免后续 IPollDriver 找不到 timeout)
            QJsonObject modbus;
            modbus["timeout_ms"] = 3000;
            modbus["max_retries"] = 5;
            modbus["poll_interval_ms"] = 100;
            plc["modbus"] = modbus;
        }
        QJsonArray plcsArr;
        plcsArr.append(plc);
        root["plcs"] = plcsArr;
    }

    // 3. 删 communications 字段(冗余,信息已搬到 plcs[0])
    root.remove("communications");

    qDebug() << "[Project] migrateV1ToV2 完成(communications → plcs[0])";
    return true;
}

// 迁移调度器:按版本号差依次调 migrate 函数
//   设计成"链式":v1→v2→v3→v4...直到目标版本
//   未来加 v3 时,只加 migrateV2ToV3 + 在这里加一行 case
bool Project::migrateTo(QJsonObject& root, int fromVersion, int toVersion) {
    int current = fromVersion;
    while (current < toVersion) {
        switch (current) {
            case 1:
                if (!migrateV1ToV2(root)) return false;
                current = 2;
                break;
            // case 2: migrateV2ToV3(root); current = 3; break;  // 未来加
            default:
                qWarning() << "[Project] migrateTo:没有从 v" << current << "到 v" << (current+1) << "的迁移函数";
                return false;
        }
    }
    return true;
}