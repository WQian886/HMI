// ============================================================================
// 阶段 B · B2.2 · 当前登录用户(单例)
//
// 文件职责:全局唯一"当前登录用户",业务代码(B3+ 权限检查、B5 审计)查它。
//
// 为什么单例:
//   - HMI 单进程,HMI 一次只允许 1 个登录用户
//   - ActionManager / Binding / WriteScheduler 等各处都要查 role
//   - 全局可访问,免去到处传指针的麻烦
//
// 生命周期:跟 main 同生命周期(Q_GLOBAL_STATIC 保证析构顺序)
//
// 关联:UserManager.h, User.h
// ============================================================================

#pragma once

#include "auth/Role.h"
#include "auth/User.h"

#include <QObject>
#include <QString>

namespace qwhmi::auth {

class CurrentUser : public QObject {
    Q_OBJECT
public:
    static CurrentUser& instance();
    static CurrentUser* instancePtr();   // 给 main 之前未初始化阶段的安全检查

    // 登录(覆盖当前)
    void login(const User& u);

    // 登出(回到未登录)
    void logout();

    // 查询
    bool isLoggedIn() const { return loggedIn_; }
    const QString& name() const { return name_; }
    Role role() const { return role_; }

signals:
    void userLoggedIn(const QString& name);
    void userLoggedOut();

private:
    explicit CurrentUser(QObject* parent = nullptr);
    CurrentUser(const CurrentUser&) = delete;
    CurrentUser& operator=(const CurrentUser&) = delete;

    bool    loggedIn_ = false;
    QString name_;
    Role    role_ = Role::Viewer;
};

} // namespace qwhmi::auth