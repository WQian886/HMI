// ============================================================================
// 阶段 A · A3 日志系统 · 实现
//
// 文件职责:实现自定义 Qt 消息处理器(写文件 + stderr)+ 配置加载 + 文件滚动。
//
// 核心流程:
//   1. Qt 收到 qCDebug/qCInfo/qCWarning 等 → 调我们的 messageHandler
//   2. 检查有效级别(分类覆盖 > 默认)→ 低级别丢弃
//   3. 构造日志行:[时间戳] [级别] 类别 - 消息
//   4. 同时输出 stderr(开发期黑框)和 文件(滚动)
//   5. 跨天 或 单文件超 100MB → 切下一文件
//   6. 每天清理 30 天前的旧文件
//
// 详见:Logging.h 和 docs/阶段A_工程化基线.md
// ============================================================================

#include "logging/Logging.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QIODevice>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>

Q_LOGGING_CATEGORY(hmiApp,    "hmi.app")
Q_LOGGING_CATEGORY(hmiComm,   "hmi.comm")
Q_LOGGING_CATEGORY(hmiVar,    "hmi.var")
Q_LOGGING_CATEGORY(hmiAlarm,  "hmi.alarm")
Q_LOGGING_CATEGORY(hmiPoll,   "hmi.poll")
Q_LOGGING_CATEGORY(hmiWrite,  "hmi.write")
Q_LOGGING_CATEGORY(hmiUi,     "hmi.ui")
Q_LOGGING_CATEGORY(hmiScript, "hmi.script")

namespace qwhmi::logging {

namespace {

struct State {
    QMutex   mutex;
    Config   config;
    QFile*   currentFile   = nullptr;
    qint64   currentBytes  = 0;
    QString  currentDate;
};
State g_state;

const char* levelName(QtMsgType t) {
    switch (t) {
    case QtDebugMsg:    return "DEBUG";
    case QtInfoMsg:     return "INFO ";
    case QtWarningMsg:  return "WARN ";
    case QtCriticalMsg: return "CRIT ";
    case QtFatalMsg:    return "FATAL";
    }
    return "?    ";
}

QtMsgType parseLevel(const QString& s) {
    const QString u = s.trimmed().toUpper();
    if (u == "DEBUG")                              return QtDebugMsg;
    if (u == "INFO")                               return QtInfoMsg;
    if (u == "WARNING" || u == "WARN")             return QtWarningMsg;
    if (u == "CRITICAL" || u == "CRIT")            return QtCriticalMsg;
    if (u == "FATAL")                              return QtFatalMsg;
    return QtInfoMsg;
}

QFile* openNewLogFile() {
    const QString dir = logDirectory();
    QDir().mkpath(dir);
    const QString stamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    const QString path  = QString("%1/%2_%3.log").arg(dir, kLogFilePrefix, stamp);
    auto* f = new QFile(path);
    if (!f->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        qWarning("Logging: failed to open %s: %s",
                 qUtf8Printable(path), qUtf8Printable(f->errorString()));
        delete f;
        return nullptr;
    }
    return f;
}

void cleanupOldLogs() {
    const QString dir = logDirectory();
    if (!QDir(dir).exists()) return;
    const QStringList filters{ QString("%1_*.log").arg(kLogFilePrefix) };
    const QFileInfoList files = QDir(dir).entryInfoList(filters, QDir::Files, QDir::Time | QDir::Reversed);
    const QDateTime cutoff = QDateTime::currentDateTime().addDays(-g_state.config.retentionDays);
    for (const QFileInfo& fi : files) {
        if (fi.lastModified() < cutoff) {
            QFile::remove(fi.absoluteFilePath());
        }
    }
}

void messageHandler(QtMsgType type, const QMessageLogContext& ctx, const QString& msg) {
    QtMsgType effLevel = g_state.config.defaultLevel;
    if (ctx.category) {
        const auto it = g_state.config.categoryLevels.find(QString::fromLatin1(ctx.category));
        if (it != g_state.config.categoryLevels.end()) {
            effLevel = it.value();
        }
    }
    if (type < effLevel) return;

    const QString ts  = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz");
    const QString cat = ctx.category ? QString::fromLatin1(ctx.category) : QStringLiteral("hmi");
    const QString src = ctx.file ? QString::fromLatin1(ctx.file).section('/', -1) : QString();
    const int     ln  = ctx.line;

    QString line;
    QTextStream s(&line);
    s << ts << " [" << levelName(type) << "] " << cat << " - " << msg;
    if (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg) {
        s << "  (" << src << ":" << ln << ")";
    }
    s << '\n';

    fprintf(stderr, "%s", line.toLocal8Bit().constData());
    fflush(stderr);

    QMutexLocker lock(&g_state.mutex);
    const QString today = QDateTime::currentDateTime().toString("yyyy-MM-dd");
    if (!g_state.currentFile || today != g_state.currentDate) {
        if (g_state.currentFile) {
            g_state.currentFile->close();
            delete g_state.currentFile;
        }
        g_state.currentFile  = openNewLogFile();
        g_state.currentBytes = 0;
        g_state.currentDate  = today;
    }
    if (g_state.currentFile) {
        const QByteArray utf8 = line.toUtf8();
        g_state.currentBytes += g_state.currentFile->write(utf8);
        g_state.currentFile->flush();
        if (g_state.currentBytes > g_state.config.maxFileBytes) {
            g_state.currentFile->close();
            delete g_state.currentFile;
            g_state.currentFile  = openNewLogFile();
            g_state.currentBytes = 0;
        }
    }
}

} // namespace

void registerCategories() {
    if (auto* app = QCoreApplication::instance()) {
        auto* timer = new QTimer(app);
        timer->setInterval(24LL * 60 * 60 * 1000);
        QObject::connect(timer, &QTimer::timeout, timer, []() {
            QMutexLocker lock(&g_state.mutex);
            cleanupOldLogs();
        });
        timer->start();
    }
    qInstallMessageHandler(messageHandler);
}

QString logDirectory() {
    if (!g_state.config.directory.isEmpty()) {
        return g_state.config.directory;
    }
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    return base + QLatin1Char('/') + QLatin1String(kLogSubDir);
}

Config loadConfig() {
    Config cfg;
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QFile f(base + QLatin1Char('/') + QLatin1String(kConfigFileName));
    if (!f.exists() || !f.open(QIODevice::ReadOnly)) {
        return cfg;
    }
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    f.close();
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        return cfg;
    }
    const QJsonObject log = doc.object().value("logging").toObject();
    if (log.isEmpty()) return cfg;

    cfg.defaultLevel  = parseLevel(log.value("default_level").toString("info"));
    const QJsonObject cats = log.value("categories").toObject();
    for (auto it = cats.begin(); it != cats.end(); ++it) {
        cfg.categoryLevels.insert(it.key(), parseLevel(it.value().toString()));
    }
    cfg.maxFileBytes  = log.value("max_file_bytes").toVariant().toLongLong();
    if (cfg.maxFileBytes <= 0) cfg.maxFileBytes = kMaxFileBytesDefault;
    cfg.retentionDays = log.value("retention_days").toInt(kRetentionDaysDefault);
    cfg.directory     = log.value("directory").toString();
    return cfg;
}

void applyConfig(const Config& cfg) {
    QMutexLocker lock(&g_state.mutex);
    g_state.config = cfg;
}

} // namespace qwhmi::logging
