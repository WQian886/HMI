// 阶段 6.2(2026-08-07):Project 类实现

#include "Project.h"
#include "VariableManager.h"
#include "Variable.h"
#include "AlarmManager.h"
#include "Alarm.h"
#include "ActionManager.h"
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
                 TcpClient* tcp,
                 QObject* parent)
    : QObject(parent),
      varMgr_(varMgr),
      alarmMgr_(alarmMgr),
      actionMgr_(actionMgr),
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

// ===== loadFromFile 空壳(6.3 阶段实现)=====
bool Project::loadFromFile(const QString& path) {
    Q_UNUSED(path);
    emit loadError("loadFromFile 6.3 阶段实现,目前未支持");
    return false;
}