// ============================================================================
// 阶段 A · A3 日志系统 · 头文件
//
// 文件职责:声明 8 个 QLoggingCategory + 日志配置结构 + 加载/应用 API。
//
// 为什么需要:Qt 的 qDebug 默认全打 stderr,生产环境要按模块分类、
//   运行时调级别、自动滚动文件、保留 30 天。
//
// 在程序中的位置:
//   - main.cpp 启动时:loadConfig() + applyConfig() + registerCategories()
//   - 业务代码:qCDebug(hmiVar) << "..." 等按需用 8 个类别
//   - 配置文件:%APPDATA%/HMI/hmi.json(可由客户改)
//
// 关键 API:registerCategories() / loadConfig() / applyConfig(cfg) / logDirectory()
// 8 个类别:app / comm / var / alarm / poll / write / ui / script
// 详见:docs/阶段A_工程化基线.md "A3 日志系统" 节
// ============================================================================

#pragma once

#include <QLoggingCategory>
#include <QHash>
#include <QString>

namespace qwhmi::logging {

constexpr qint64 kMaxFileBytesDefault = 100 * 1024 * 1024;  // 100 MB
constexpr int    kRetentionDaysDefault = 30;
constexpr const char* kConfigFileName = "hmi.json";
constexpr const char* kLogSubDir      = "logs";
constexpr const char* kLogFilePrefix  = "hmi";

struct Config {
    QtMsgType  defaultLevel = QtInfoMsg;
    QHash<QString, QtMsgType> categoryLevels;
    qint64     maxFileBytes = kMaxFileBytesDefault;
    int        retentionDays = kRetentionDaysDefault;
    QString    directory;
};

void registerCategories();
Config loadConfig();
void applyConfig(const Config& cfg);
QString logDirectory();

} // namespace qwhmi::logging

Q_DECLARE_LOGGING_CATEGORY(hmiApp)
Q_DECLARE_LOGGING_CATEGORY(hmiComm)
Q_DECLARE_LOGGING_CATEGORY(hmiVar)
Q_DECLARE_LOGGING_CATEGORY(hmiAlarm)
Q_DECLARE_LOGGING_CATEGORY(hmiPoll)
Q_DECLARE_LOGGING_CATEGORY(hmiWrite)
Q_DECLARE_LOGGING_CATEGORY(hmiUi)
Q_DECLARE_LOGGING_CATEGORY(hmiScript)
