// ============================================================================
// 阶段 B · B1.2 · 用户管理器
//
// 文件职责:CRUD + 认证 + signals 钩子(给 B5 审计用)。
//
// 为什么 QObject + signals:B5 操作审计要监听 userAdded / authenticationFailed
//   等事件,在 B1.2 阶段先发信号,后续 B5 阶段接 AuditLog 即可。
//
// 设计要点:
//   - 容器 QList<User>(< 1000 用户够用,B1.3 持久化再换 SQLite)
//   - 单线程(按现状.md 第七节约定,无 QThread)
//   - 锁定逻辑留给 B2(失败 N 次锁 M 分钟)
//
// 关联:docs/优化与改善计划.html "B1",User.h, PasswordHasher.h
// ============================================================================

#pragma once

#include "auth/Role.h"
#include "auth/User.h"

#include <QObject>
#include <QList>
#include <QString>

namespace qwhmi::auth {

class UserManager : public QObject {
    Q_OBJECT
public:
    explicit UserManager(QObject* parent = nullptr);
    ~UserManager() override;

    // ===== CRUD =====

    // 添加用户。返回 true 成功;false 表示名字已存在 / 密码空 / 名字空
    bool addUser(const QString& name, const QString& plainPassword, Role role);

    // 删除用户(软删除,disabled=true)。找不到返 false
    bool removeUser(const QString& name);

    // 改密码。验证旧密码通过后才改。返回 true 成功
    bool changePassword(const QString& name, const QString& oldPlain, const QString& newPlain);

    // ===== 认证 =====

    // 验证 name + plainPassword。失败原因(用户不存在/密码错/被禁用)不暴露,
    // 只返 bool(防用户名枚举)。失败也 emit authenticationFailed 供审计用
    bool authenticate(const QString& name, const QString& plainPassword);

    // ===== 查询 =====

    // 找用户指针(只读语义)。找不到返 nullptr
    const User* findUser(const QString& name) const;

    // 是否存在任何用户(给 main 集成 B1.3 用:首启空 → 引导建 admin)
    bool hasUsers() const;

    // 当前所有用户列表(按名字排序,方便 UI 显示)
    QList<User> listUsers() const;

    // 用户总数
    int count() const { return users_.size(); }

    // 内部方法:把"已 hash 的 User"加进列表(仅给 UserStore load 用)
    // 业务代码不要调这个,直接走 addUser(name, plain, role)
    void appendLoadedUser(User u);

signals:
    // 添加成功(参数:用户名)
    void userAdded(const QString& name);
    // 删除成功
    void userRemoved(const QString& name);
    // 密码改了
    void passwordChanged(const QString& name);
    // 认证失败(参数:用户名,审计用,失败原因不传避免泄漏)
    void authenticationFailed(const QString& name);

private:
    // 名字 → 用户列表索引的内部查找
    int indexOf(const QString& name) const;

    QList<User> users_;
};

} // namespace qwhmi::auth