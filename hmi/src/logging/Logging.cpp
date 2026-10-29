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

#include "util/Paths.h"

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

// ★★★ 2026-10-26(BUGFIX-C6):严重程度排名 —— 【不要直接比 QtMsgType 的数值】★★★
//
//   【实测出来的真 bug(Qt 6.8.3)】
//     QtMsgType 的枚举值【不是按严重程度排的】:
//       QtDebugMsg=0  QtWarningMsg=1  QtCriticalMsg=2  QtFatalMsg=3  QtInfoMsg=4
//     —— Info 是【后加上去的】,数值比 Fatal 还大。
//     于是 handler 里 `if (type < effLevel) return;` 这种直接比数值是错的:
//     默认级别(=QtInfoMsg=4)会把 Warning(1)/Critical(2)/Fatal(3)
//     【全部丢掉】。
//
//   【后果:整个应用一条告警都写不进日志】
//     实测:24 小时长跑的两份日志共 2918 行,WARN 行数 = 0 ——
//     连启动期那条"[协议可用性] S7 尚未实现"这种固定会出现的告警都没有。
//     而【联动失败】的告警恰恰在这一档:
//       "报警响了、界面正常、审计记着已执行、PLC 一个字节没收到",
//       现场能看到的线索被滤掉了 —— 只能靠猜。
//     这也是本次负向对照里"变异后日志一声不吭"的真正原因:
//     告警【发出去了】,被【过滤掉了】。
//
//   【改法】
//     比【严重程度排名】,不比 QtMsgType 数值。
//     排名的定义就是本项目日志里显示的那一档(DEBUG/INFO/WARN/CRIT/FATAL)。
//
//   【为什么不用 Qt::LoggingSeverity / qInstallMessageHandler 的别的机制】
//     Qt 自己的 QLoggingCategory 也有同样的问题(它的 minLevel 比的是数值),
//     那是 Qt 的事;本项目自己的 handler 自己比,就得自己比对。
//
// ★ 本函数的定义在【文件末尾、匿名 namespace 之外】——
//   它要能被测试直接断言,所以不能藏在匿名 namespace 里。

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
    // ★ 2026-10-26:比【严重程度排名】,不是比 QtMsgType 数值。
    //   QtMsgType 的数值不是按严重程度排的(QtInfoMsg=4 比 QtFatalMsg=3 还大),
    //   直接比会把 Warning/Critical/Fatal 全部丢掉 —— 详见 severityRank()。
    if (severityRank(type) < severityRank(effLevel)) return;

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
    // ★ 2026-10-21:改走 util::appDataDir() —— 全应用同一个数据目录,
    //   而且支持 HMI_DATA_DIR 覆盖(长跑实例要与正在跑的实例隔离,见 util/Paths.h)。
    const QString base = qwhmi::util::appDataDir();
    return base + QLatin1Char('/') + QLatin1String(kLogSubDir);
}

Config loadConfig() {
    Config cfg;
    const QString base = qwhmi::util::appDataDir();
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

// ★★★ 2026-10-26:严重程度排名(公开 API,见 Logging.h 的说明)★★★
//
//   ★ 【实测(Qt 6.8.3)】QtMsgType 的枚举值【不是按严重程度排的】:
//       QtDebugMsg=0  QtWarningMsg=1  QtCriticalMsg=2(=QtSystemMsg)  QtFatalMsg=3  QtInfoMsg=4
//     —— Info 是后加的,数值比 Fatal 还大。
//     所以直接比 QtMsgType 数值会把告警全丢掉(默认级别=Info=4)。
//
//   【为什么放在匿名 namespace 之外】
//     测试要能直接断言"排名顺序是对的",而不只是间接看某行有没有落盘。
//
//   【QtSystemMsg 的处理】
//     它的枚举值【跟 QtCriticalMsg 一样】(都是 2)—— 写两个 case 会编译报错
//     (error C2196: case 值"2"已使用)。所以只能留一个。
//     留着 Critical、去掉 System:两者数值相同,switch 本来也只会走同一个分支。
int severityRank(QtMsgType t) {
    switch (t) {
    case QtDebugMsg:    return 0;
    case QtInfoMsg:     return 1;
    case QtWarningMsg:  return 2;
    case QtCriticalMsg: return 3;   // QtSystemMsg 数值相同(=2),见上面的说明
    case QtFatalMsg:    return 4;
    }
    return 1;   // 未知级别按 Info 处理 —— 【宁可多写,不可静默丢掉告警】
}

} // namespace qwhmi::logging
