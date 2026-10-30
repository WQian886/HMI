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

// ★★★ 2026-10-26(BUGFIX-C6):严重程度排名 ★★★
//
//   【为什么单独暴露出来】
//     Qt 的 QtMsgType 枚举【不是按严重程度排的】(实测 Qt 6.8.3):
//       QtDebugMsg=0  QtWarningMsg=1  QtCriticalMsg=2  QtFatalMsg=3  QtInfoMsg=4
//     —— Info 是【后加上去的】,数值比 Fatal 还大。
//     handler 里直接比数值(`if (type < effLevel) return;`)会把
//     Warning/Critical/Fatal 【全部丢掉】—— 整个应用一条告警都写不进日志。
//     实测:24 小时长跑的两份日志共 2918 行,WARN 行数 = 0。
//
//   【契约】返回【严重程度】排名,不是 QtMsgType 数值:
//     Debug(0) < Info(1) < Warning(2) < Critical(3) < Fatal(4)
//   比较日志级别一律用这个,不要用 QtMsgType 的数值。
//
//   【为什么暴露成公开 API】
//     测试要能直接断言"排名是对的这个顺序",而不只是间接地
//     "看某行有没有出现在日志里"。间接断言有个盲区:
//     Fatal 一旦真打出来,进程就结束了 —— 那一档没法用间接法测。
int severityRank(QtMsgType t);

// ★★★ G-19(2026-10-30):日志模块的观测口 ★★★
//
//   【背景】
//     G-19:日志目录打不开时,原来每【一条】日志都会重试一次打开
//     (mkpath + open 失败 + 报错),⇒ stderr 被刷爆、CPU 白烧,
//     而日志文件一个字也没写进去。
//
//   【契约】
//     openAttempts —— 尝试打开日志文件的次数。
//       日志文件打不开时,它必须【停在 1】:不能每条消息都重试一次。
//     reentryCount —— messageHandler 被【重入】的次数。
//       ★ 实测(Qt 6.8.3):Qt 不会重入自定义 handler,这个计数正常恒为 0。
//         它是【哨兵】:哪天 Qt 改了这条语义,这里会先红。
//
//   【为什么暴露成公开 API】
//     测试要能【精确】断言"没有反复重试"。
//     只能靠"stderr 刷了多少行"来判红的话,负向对照既慢又不准。
struct Diagnostics {
    int reentryCount = 0;
    int openAttempts = 0;
};
Diagnostics loggingDiagnostics();
void resetLoggingDiagnostics();

} // namespace qwhmi::logging

Q_DECLARE_LOGGING_CATEGORY(hmiApp)
Q_DECLARE_LOGGING_CATEGORY(hmiComm)
Q_DECLARE_LOGGING_CATEGORY(hmiVar)
Q_DECLARE_LOGGING_CATEGORY(hmiAlarm)
Q_DECLARE_LOGGING_CATEGORY(hmiPoll)
Q_DECLARE_LOGGING_CATEGORY(hmiWrite)
Q_DECLARE_LOGGING_CATEGORY(hmiUi)
Q_DECLARE_LOGGING_CATEGORY(hmiScript)
