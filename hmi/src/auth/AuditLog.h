// ============================================================================
// 阶段 B · B5 · 操作审计日志
//
// 文件职责:把"用户做了什么"持久化到 %LOCALAPPDATA%/QianWei/HMI/audit-YYYY-MM-DD.log,
// 给事故复盘 + 合规审计用(工业 HMI 商业化必备)。
//
// 关键设计:
//   - 单例(AuditLog::instance()),跟 CurrentUser 同样的全局访问模式
//   - 按天滚动:文件名带日期,跨天自动切文件(rotateIfNeeded 在每次 append 调一次)
//   - append 同步落盘(file->flush()),不怕进程崩丢最后几行
//   - 同时 qCInfo(hmiAudit) 输出,主日志和 audit.log 都能看到
//
// 接入点(7 类事件):
//   1. auth.login / auth.logout       — CurrentUser signals (main.cpp)
//   2. auth.fail                      — UserManager::authenticationFailed (main.cpp)
//   3. user.add / user.remove         — UserManager::userAdded/userRemoved (main.cpp)
//   4. user.passwordChange            — UserManager::passwordChanged (main.cpp)
//   5. variable.write                 — Binding::bindEdit 写值成功 (Binding.cpp)
//   6. alarm.ack                      — AlarmManager::acknowledge 成功 (AlarmManager.cpp)
//   7. user.add (首启 AdminSetupDialog)— main.cpp 已弹 dialog,走 userAdded signal 自动捕获
//
// 用法:
//   auth::AuditLog::instance().appendCurrent("variable.write", "Speed", "value=1234", "ok");
//
// 关联:UserManager.h, CurrentUser.h, Permission.h
// ============================================================================

#pragma once

#include <QObject>
#include <QString>

class QFile;

namespace qwhmi::auth {

class AuditLog : public QObject {
    Q_OBJECT
public:
    static AuditLog& instance();

    // 通用接口:显式传 user/role
    //   action:  "auth.login" / "variable.write" / "alarm.ack" / "user.add" ...
    //   user:    谁操作的(用户名)
    //   role:    操作者角色字符串("admin" / "engineer" / ...)
    //   target:  涉及的实体(用户名 / 变量名 / 报警 ID,空=无)
    //   detail:  自由文本(值 / 错误信息 / ...)
    //   result:  "ok" / "fail" / "rejected"
    void append(const QString& action,
                const QString& user,
                const QString& role,
                const QString& target = {},
                const QString& detail = {},
                const QString& result = QStringLiteral("ok"));

    // 便捷接口:自动从 CurrentUser 拿 user/role
    //   适用于"用户在操作"的场景(写值 / 确认报警 / 用户管理)
    //   系统事件(首启建 admin)用 append 显式传 "system" / "system"
    void appendCurrent(const QString& action,
                       const QString& target = {},
                       const QString& detail = {},
                       const QString& result = QStringLiteral("ok"));

    // 默认文件路径(测试 / 调试用)
    static QString defaultFilePath();

    // 检查并按天切换文件(append 内部已自动调,通常不需要手动)
    void rotateIfNeeded();

    // 测试用:关闭文件 + 清空物理文件,下次 append 重新打开
    //   解决:单例 file_ 句柄在测试间不释放,删文件后句柄仍写已删除 inode 的问题
    void resetForTesting();

private:
    explicit AuditLog(QObject* parent = nullptr);
    ~AuditLog() override;

    AuditLog(const AuditLog&) = delete;
    AuditLog& operator=(const AuditLog&) = delete;

    void openForToday();    // 打开今日文件
    void closeFile();       // 关闭当前文件

    QString currentDate_;   // 当前打开的文件对应的日期(YYYY-MM-DD)
    QFile* file_ = nullptr; // 当前打开的 audit 文件
};

} // namespace qwhmi::auth
