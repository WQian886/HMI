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
    //
    // ★ 2026-11-10 (G-30)契约变严了 —— 四种情况返 false:
    //   1. 用户不存在
    //   2. actor 就是它自己(actor 传了才查这一条)
    //   3. 它已经是禁用状态(没东西可禁,不做无意义的审计噪音)
    //   4. ★ 它是【最后一个 enabled Admin】——
    //      禁掉之后这个系统里就没有人能管用户了(用户管理入口只对 Admin 开),
    //      而"启用"入口(G-30 新加的 enableUser)就是唯一的回头路。
    //   第 4 条以前只写在 UserManagementDialog 里(而且是拿【显示文本】
    //   比角色名),任何绕过那个对话框的调用都拦不住 ⇒ 判据搬进公共层。
    bool removeUser(const QString& name, const QString& actor = QString());

    // 能不能禁用 name 这个用户?
    //   actor = 当前登录用户的名字(空串 = 不查"禁自己")
    //   reason(可选)= 不通过时填人话原因,调用方可以直接弹给用户看
    // 返回 true 表示"可以禁"。
    // ★ 这是【判据】本身 —— 对话框只负责把 reason 弹出来,不再自己判。
    bool canDisableUser(const QString& name, const QString& actor,
                        QString* reason = nullptr) const;

    // 重新启用一个被禁用的用户(removeUser 的逆操作)。
    //   用户不存在 → false;本来就是启用状态(没动它)→ false;
    //   成功启用 → true,并 emit userEnabled(供存盘 + 审计用)。
    // ★ 为什么要有它:原来"禁用"是【单向】的 —— 被禁用的账号永远回不来,
    //   而确认框还在跟用户说"(可后续用密码恢复)"(密码根本救不回来:
    //   authenticate() 对 disabled 用户一律拒绝)。详见 docs/现状.md 第七节 G-30。
    bool enableUser(const QString& name);

    // 当前有多少个【enabled 的 Admin】(判"最后一个 Admin"用)
    int enabledAdminCount() const;

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
    // ★ 2026-11-10 (G-30):重新启用成功(main.cpp 拿它触发存盘 + 审计)
    void userEnabled(const QString& name);
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