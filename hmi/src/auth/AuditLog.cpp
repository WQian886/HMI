// ============================================================================
// 阶段 B · B5 · 操作审计日志(实现)
//
// 关联:AuditLog.h
//
// 实现要点:
//   - 单例(Q_GLOBAL_STATIC 模式:函数局部 static + 引用)
//   - 跨天滚动:每次 append 都调 rotateIfNeeded → 检查日期变化 → 切文件
//   - 落盘:append 时 file_->write + flush(同步落盘,崩了不丢最后几行)
//   - 双写:qCInfo(hmiAudit) + 文件,主日志(滚动)+ audit.log(按天)各一份
//
// 路径:%LOCALAPPDATA%/QianWei/HMI/audit-YYYY-MM-DD.log
//   跟 users.json 同目录,ApplicationName/OrgName 由 main.cpp 提前设置好
// ============================================================================

#include "auth/AuditLog.h"
#include "auth/CurrentUser.h"
#include "auth/Role.h"

#include "logging/Logging.h"
#include "util/Paths.h"

#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QStandardPaths>

namespace qwhmi::auth {

Q_LOGGING_CATEGORY(hmiAudit, "hmi.audit")

AuditLog& AuditLog::instance() {
    static AuditLog inst;
    return inst;
}

AuditLog::AuditLog(QObject* parent) : QObject(parent) {
    // 构造时打开今日文件,失败 qCWarning 不崩
    openForToday();
}

AuditLog::~AuditLog() {
    closeFile();
}

QString AuditLog::defaultFilePath() {
    // ★ 2026-10-21:走 util::appDataDir() —— 见 util/Paths.h。
    //   这一条尤其重要:审计日志按天命名,两个实例写同一份会互相截断。
    const QString dir = qwhmi::util::appDataDir();
    const QString today = QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd"));
    return QStringLiteral("%1/audit-%2.log").arg(dir, today);
}

void AuditLog::openForToday() {
    closeFile();
    const QString today = QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd"));
    currentDate_ = today;

    const QString path = defaultFilePath();
    QDir().mkpath(QFileInfo(path).absolutePath());

    file_ = new QFile(path, this);
    // Append 模式:启动时不覆盖老文件,新事件追加
    if (!file_->open(QIODevice::Append | QIODevice::Text)) {
        qCWarning(hmiAudit) << "audit log open failed:" << path << file_->errorString();
        delete file_;
        file_ = nullptr;
        return;
    }
    qCInfo(hmiAudit) << "audit log opened:" << path;
}

void AuditLog::closeFile() {
    if (!file_) return;
    file_->close();
    delete file_;
    file_ = nullptr;
}

void AuditLog::rotateIfNeeded() {
    const QString today = QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd"));
    if (today != currentDate_) {
        qCInfo(hmiAudit) << "audit log rotate:" << currentDate_ << "->" << today;
        openForToday();
    }
}

void AuditLog::resetForTesting() {
    closeFile();
    currentDate_.clear();
    QFile::remove(defaultFilePath());
}

void AuditLog::append(const QString& action,
                      const QString& user,
                      const QString& role,
                      const QString& target,
                      const QString& detail,
                      const QString& result) {
    // 简单锁:多线程并发 append 时不交叉写(实际单线程,但 qDebug 也可能异步)
    static QMutex s_mutex;
    QMutexLocker lock(&s_mutex);

    rotateIfNeeded();

    // ISO8601 with ms,UTC(给跨时区事故复盘用)
    const QString ts = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);

    // 拼一行:key=value 格式,便于 grep / awk
    //   例:2026-09-17T10:23:45.123Z | action=variable.write user=admin role=admin target=Speed detail=value=1234 result=ok
    QString line = QStringLiteral("%1 | action=%2 user=%3 role=%4")
                       .arg(ts, action, user, role);
    if (!target.isEmpty()) {
        line += QStringLiteral(" target=%1").arg(target);
    }
    if (!detail.isEmpty()) {
        line += QStringLiteral(" detail=%1").arg(detail);
    }
    line += QStringLiteral(" result=%1").arg(result);

    // 1) 走 logging category(主日志滚动文件能搜)
    qCInfo(hmiAudit).noquote() << line;

    // 2) 同步落盘到 audit-YYYY-MM-DD.log
    if (file_) {
        file_->write(line.toUtf8());
        file_->write("\n");
        file_->flush();
    }
}

void AuditLog::appendCurrent(const QString& action,
                             const QString& target,
                             const QString& detail,
                             const QString& result) {
    const QString user = CurrentUser::instance().name();
    const QString role = roleToString(CurrentUser::instance().role());
    append(action, user, role, target, detail, result);
}

} // namespace qwhmi::auth
