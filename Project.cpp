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

    // schema_version(Q1-Q5 拍板的硬性字段,加载时检查)
    root["schema_version"] = "1.0";

    // project_name / 时间戳(暂时硬编码,6.3 加用户输入)
    root["project_name"] = "HMI Demo Project";
    QString nowIso = QDateTime::currentDateTime().toString(Qt::ISODate);
    root["created_at"]   = nowIso;
    root["updated_at"]   = nowIso;

    // ===== communications(Q1-A:全存)=====
    {
        QJsonObject comm;
        comm["protocol"] = "modbus_tcp";   // 当前只支持 modbus_tcp
        if (tcp_) {
            comm["host"] = tcp_->host();
            comm["port"] = tcp_->port();
        } else {
            comm["host"] = "127.0.0.1";     // 没 tcp_ 时 fallback
            comm["port"] = 502;
        }
        QJsonObject modbus;
        // ★ TODO:以后从 ModbusMaster / PollScheduler 拿真值,现在用代码默认(Q1 拍板"全存"先落实字段)
        // ★ 6.2 优化(2026-08-07):从 TcpClient 常量拿,不硬编码(Q1-A 全存 落实)
        modbus["timeout_ms"]       = TcpClient::kConnectTimeoutMs;   // 3000ms
        modbus["max_retries"]      = TcpClient::kMaxRetries;          // 5 次
        // TODO:poll_interval_ms 应从 PollScheduler::kPollIntervalMs 拿真值(目前临时硬编码 100ms)
        modbus["poll_interval_ms"] = 100;
        comm["modbus"] = modbus;
        root["communications"] = comm;
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
            QString varName = m.variable ? m.variable->name() : QString();
            actionsArr.append(JsonSerializer::toActionMappingJson(m.alarmName, varName, m.value));
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
//   【参照】Wqian 2026-08-07 阶段 6.3 开干;inventory/HMI_阶段6_schema_design.md
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

    // 3. 校验 schema_version(只接受 1.x)
    QString schemaVer = root["schema_version"].toString();
    if (schemaVer.isEmpty() || !schemaVer.startsWith("1.")) {
        emit loadError(QString("schema_version \"%1\" 不支持,需要 1.x").arg(schemaVer));
        return false;
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
        // action 可以引用还没存在的 alarm(只是永远不触发),不强制检查
        actionMgr_->addMapping(alarmName, var, writeValue);
    }

    // 8. 通讯字段应用(★ 6.5 收尾:communications.host/port 真正应用到 TcpClient)
    //   【背景】6.3 拍板保留字段、6.4 拍板不应用,问题是切工程后内存 = 新 JSON 但 TCP 还连着旧 IP
    //   【改法】loadFromFile 调 tcp_->connectTo(host, port) 触发重连,emit loaded 让 hmi 同步 modbus
    //   ★ 6.5 收尾(2026-08-08 BugFix #3):host/port 跟当前相同 → 跳过重连,避免状态闪烁 / in-flight UAF
    //   【取舍】正在通讯时 loadFromFile 会短暂断连,但用户主动打开工程 = 期望切换,合理
    //   【参照】Wqian 2026-08-08 review 6.4 修复 #2
    QJsonObject comm = root["communications"].toObject();
    QString host = comm["host"].toString();
    int port = comm["port"].toInt();
    if (tcp_ && !host.isEmpty() && port > 0 && port < 65536) {
        if (tcp_->host() == host && tcp_->port() == static_cast<quint16>(port)) {
            qDebug() << "[Project] 通讯参数已应用(无变化,跳过重连):" << host << ":" << port;
        } else {
            tcp_->connectTo(host, static_cast<quint16>(port));
            qDebug() << "[Project] 通讯参数已应用(切换):" << host << ":" << port;
        }
    }

    qDebug() << "[Project] loaded from" << path;
    emit loaded(path);   // ★ 6.3:load 成功(hmi 收到后重绑 UI)
    return true;
}