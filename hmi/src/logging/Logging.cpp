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
#include <QScopeGuard>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>

#include <QAtomicInt>

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
    // ★★★ G-19(2026-10-30):日志文件打不开的状态位 ★★★
    //   打不开的时候【不能】每条消息都重试一次 ——
    //   一次 mkdir + 一次 open 失败,每条日志一遍,CPU 和 stderr 都会被刷爆。
    //   一旦坏掉就记在这里,等【换日】或【换配置】才再给一次机会。
    bool     fileBroken    = false;
    // ★ G-19:观测用 —— messageHandler 被【重入】的次数。正常情况恒为 0。
    //   原子量:重入的那次调用是【在持锁状态下】进来的,拿不到锁。
    //   ★ 实测(Qt 6.8.3):Qt 不会重入自定义 handler,这个计数正常恒为 0。
    //     它是【哨兵】:哪天 Qt 改了语义(或者我们自己在 handler 里调了
    //     会打日志的东西),这里会先红,而不是等到现场死锁。
    QAtomicInt reentryCount{0};
    // ★ G-19:尝试打开日志文件的次数。日志打不开时它必须停在 1
    //   (不能每条消息都重试一次)。
    QAtomicInt openAttempts{0};
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

// ★★★ G-19(2026-10-30):这个函数【不要】用 qWarning/qDebug 报错 ★★★
//
//   【先说清楚:这里曾经被误判过一次】
//     最初的结论是 qWarning 会重入 messageHandler,于是二次 lock 同一把
//     非递归 QMutex,同线程死锁,整个进程永久挂死 —— 并按这个结论修了半天。
//     ★ 实测推翻了它 ★(探针见 tst_QtReentryProbe.cpp):
//       Qt 6.8.3 【不会】重入自定义消息处理器 ——
//       在 handler 内部再打一条日志,Qt 会把它交给【默认 handler】,
//       自定义 handler 一次都不会被重入(实测 entered=1 / reentered=0)。
//     ⇒ 那条死锁链【不成立】,本条降级为 P2(见下)。
//
//   【那为什么这里还是不用 qWarning】
//     ① 格式:handler 内部打的日志走的是 Qt 的【默认 handler】,
//        出来是【裸文本】:没有时间戳、没有级别、没有类别,
//        跟别的日志行长得完全不一样。现场看到一条没前缀的行,
//        根本猜不出它是哪儿来的、什么级别。
//     ② 失控:它【不受本模块的级别过滤控制】
//        (默认 handler 不看 categoryLevels,也不看 defaultLevel)。
//     自己用 fprintf 打:格式统一,而且能【只报一次】。
//
//   【这条真正的缺陷是什么(P2)】
//     日志目录打不开时(磁盘满 / 无写权限 / hmi.json 里 directory 配错),
//     原来每【一条】消息都会:mkpath 一次 + open 一次(失败)+ 报错一次。
//     ⇒ stderr 被刷爆、CPU 白烧,而日志文件一个字也没写进去。
//     修法见下面 messageHandler 里的 fileBroken。
//
//   【负向对照】
//     把 fileBroken 那道闸去掉 ⇒ 每条消息都重试 ⇒ openAttempts 从 1 变成 N
//     ⇒ 测试立刻红(见 tst_LoggingFileUnavailable.cpp)。
QFile* openNewLogFile() {
    g_state.openAttempts.fetchAndAddOrdered(1);
    const QString dir = logDirectory();
    QDir().mkpath(dir);
    const QString stamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    const QString path  = QString("%1/%2_%3.log").arg(dir, kLogFilePrefix, stamp);
    auto* f = new QFile(path);
    if (!f->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        // ★ 这里【不能】用 qWarning —— 理由见上面那段注释(格式失控 + 不受过滤控制)。
        fprintf(stderr, "Logging: failed to open %s: %s\n",
                qUtf8Printable(path), qUtf8Printable(f->errorString()));
        fflush(stderr);
        delete f;
        return nullptr;
    }
    return f;
}

// ★ G-19:关掉当前日志文件(调用方必须已持有 g_state.mutex)。
//   原来这里是三行内联代码,delete 之后 currentFile 变成悬垂指针,
//   要到下一行赋值才被覆盖 —— 这中间任何一次重入都会踩到它。
void closeCurrentFile() {
    if (g_state.currentFile) {
        g_state.currentFile->close();
        delete g_state.currentFile;
        g_state.currentFile = nullptr;      // ★ 别留悬垂指针
    }
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

    // ★★★ G-19(2026-10-30):重入保护 —— 必须在【加锁之前】判断 ★★★
    //
    //   【这道闸现在的定位:保险,不是修复】
    //     最初的结论是 qWarning 会重入本函数 ⇒ 二次 lock 同一把非递归 QMutex
    //     ⇒ 同线程死锁 ⇒ 整个进程永久挂死 —— 并按这个结论修了半天。
    //     ★ 实测推翻了它 ★(探针见 tst_QtReentryProbe.cpp):
    //       Qt 6.8.3 不会重入自定义 handler —— handler 内部再打一条日志,
    //       Qt 会把它交给【默认 handler】,本函数一次都不会被重入。
    //     ⇒ 那条死锁链不成立,这个分支在今天的 Qt 上【走不到】。
    //
    //   【为什么还留着】
    //     ① 它挡的是【将来】:哪天 Qt 改了这条语义,或者我们自己在 handler 里
    //        调了会打日志的东西,最坏结果从"永久挂死"变成"这一条被丢掉"。
    //     ② 它是【哨兵】:真发生了重入,reentryCount 会变成非 0,测试会先红,
    //        而不是等到现场才发现程序不动了。
    //
    //   【为什么必须放在锁前面】
    //     放在锁后面就晚了:重入的那次调用会先卡在 mutex 上,永远走不到判断。
    //
    //   【为什么用 thread_local 而不是普通全局】
    //     多线程下另一个线程的调用【不是】重入,它应该正常排队等锁。
    //     只有【同线程】的递归调用才是重入。
    static thread_local bool inHandler = false;
    if (inHandler) {
        g_state.reentryCount.fetchAndAddOrdered(1);
        return;      // 已经写过 stderr 了,不再碰文件、不再碰锁
    }

    QMutexLocker lock(&g_state.mutex);
    // ★ inHandler 是 static thread_local,【不能】被 lambda 按引用捕获
    //   (静态存储期的变量不需要捕获,直接引用即可)。
    //   qScopeGuard 要用 auto 声明 —— 直接写 `qScopeGuard g(...)`
    //   会被解析成【函数声明】("qScopeGuard g(lambda)"),编译报"缺少';'"。
    auto unsetInHandler = qScopeGuard([&]{ inHandler = false; });
    inHandler = true;

    const QString today = QDateTime::currentDateTime().toString("yyyy-MM-dd");
    if (today != g_state.currentDate) {
        // 跨天:关掉昨天的文件,今天重新开一个
        closeCurrentFile();
        g_state.currentBytes = 0;
        g_state.currentDate  = today;
        g_state.fileBroken   = false;      // ★ 新的一天,再给一次机会
    }
    if (!g_state.currentFile && !g_state.fileBroken) {
        // ★ 只在【还没坏】的时候才尝试打开。
        //   原来每次消息都重试一次(条件是 "!currentFile")——
        //   打不开的时候每条日志一遍 mkdir + open 失败,CPU 和 stderr 都被刷爆。
        g_state.currentFile = openNewLogFile();
        if (!g_state.currentFile) {
            g_state.fileBroken = true;
            fprintf(stderr, "Logging: 日志文件打不开,本会话【只写 stderr】"
                            "(目录=%s)。等换日或改配置后会再试一次。\n",
                    qUtf8Printable(logDirectory()));
            fflush(stderr);
        }
    }
    if (g_state.currentFile) {
        const QByteArray utf8 = line.toUtf8();
        g_state.currentBytes += g_state.currentFile->write(utf8);
        g_state.currentFile->flush();
        if (g_state.currentBytes > g_state.config.maxFileBytes) {
            closeCurrentFile();
            g_state.currentBytes = 0;
            g_state.currentFile  = openNewLogFile();
            if (!g_state.currentFile) {
                g_state.fileBroken = true;
            }
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
    // ★ G-19:换配置 = 可能换日志目录 ⇒ 之前"打不开"的状态要作废。
    //   不作废的话,用户把 hmi.json 里的 logging.directory 改对了也不会恢复。
    g_state.fileBroken = false;
}

// ★ G-19:观测口 —— 见 Logging.h 里的契约说明。
Diagnostics loggingDiagnostics() {
    Diagnostics d;
    d.reentryCount = g_state.reentryCount.loadAcquire();
    d.openAttempts = g_state.openAttempts.loadAcquire();
    return d;
}

void resetLoggingDiagnostics() {
    g_state.reentryCount.storeRelease(0);
    g_state.openAttempts.storeRelease(0);
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
