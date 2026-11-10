// ============================================================================
// 阶段 B · B1.2 · UserManager 实现
//
// 关联:UserManager.h
// ============================================================================

#include "auth/UserManager.h"
#include "auth/PasswordHasher.h"

#include <QDateTime>
#include <QHash>
#include <algorithm>

#include "logging/Logging.h"

namespace qwhmi::auth {

UserManager::UserManager(QObject* parent) : QObject(parent) {}
UserManager::~UserManager() = default;

void UserManager::appendLoadedUser(User u) {
    // 仅给 UserStore::load 用。重复名字自动 skip(不覆盖现有用户)
    if (indexOf(u.name) >= 0) {
        qCWarning(hmiApp) << "[UserMgr] appendLoadedUser 跳过重复:" << u.name;
        return;
    }
    users_.append(std::move(u));
    // 不 emit userAdded(这是 load,不是 add)
}

int UserManager::indexOf(const QString& name) const {
    for (int i = 0; i < users_.size(); ++i) {
        if (User::namesEqual(users_[i].name, name)) {
            return i;
        }
    }
    return -1;
}

bool UserManager::addUser(const QString& name, const QString& plainPassword, Role role) {
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty() || plainPassword.isEmpty()) {
        qCWarning(hmiApp) << "[UserMgr] addUser 失败:名字或密码为空";
        return false;
    }
    if (indexOf(trimmed) >= 0) {
        qCWarning(hmiApp) << "[UserMgr] addUser 失败:用户已存在" << trimmed;
        return false;
    }
    User u;
    u.name        = trimmed;
    u.role        = role;
    u.salt        = generateSalt();
    u.passwordHash = hashPassword(plainPassword, u.salt);
    if (u.passwordHash.isEmpty()) {
        qCCritical(hmiApp) << "[UserMgr] addUser 失败:hash 出错(内部 bug)";
        return false;
    }
    u.createdAt   = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    u.disabled    = false;
    users_.append(u);
    qCInfo(hmiApp) << "[UserMgr] 用户已添加:" << trimmed << "role=" << roleToString(role);
    emit userAdded(trimmed);
    return true;
}

bool UserManager::removeUser(const QString& name, const QString& actor) {
    const int idx = indexOf(name);
    if (idx < 0) {
        qCWarning(hmiApp) << "[UserMgr] removeUser 失败:用户不存在" << name;
        return false;
    }
    const QString trimmed = users_[idx].name;

    // ★ 2026-11-10 (G-30):护栏搬进公共层。
    //   原来这三条全在 UserManagementDialog 里(而且是拿【显示文本】比角色名),
    //   绕过那个对话框的调用一个都拦不住 ⇒ 判据必须在公共层。
    //   1) 不能禁自己
    if (!actor.isEmpty() && User::namesEqual(trimmed, actor)) {
        qCWarning(hmiApp) << "[UserMgr] removeUser 拒绝:不能禁用当前登录的用户" << trimmed;
        return false;
    }
    //   2) 已经是禁用状态 —— 没东西可禁(不做无意义的 userRemoved 审计噪音)
    if (users_[idx].disabled) {
        qCWarning(hmiApp) << "[UserMgr] removeUser 拒绝:已经是禁用状态" << trimmed;
        return false;
    }
    //   3) 最后一个 enabled Admin —— 禁掉之后没人能管用户了
    if (users_[idx].role == Role::Admin && enabledAdminCount() <= 1) {
        qCWarning(hmiApp) << "[UserMgr] removeUser 拒绝:这是最后一个 enabled Admin" << trimmed;
        return false;
    }

    // 软删除:标记 disabled 而不是真 erase(审计 + 历史)
    users_[idx].disabled = true;
    qCInfo(hmiApp) << "[UserMgr] 用户已禁用(软删除):" << trimmed;
    emit userRemoved(trimmed);
    return true;
}

bool UserManager::canDisableUser(const QString& name, const QString& actor,
                                 QString* reason) const {
    // reason 填人话原因 —— 调用方(对话框)直接弹给用户看,不用自己再翻译一遍
    const auto say = [reason](const QString& s) { if (reason) *reason = s; };

    const int idx = indexOf(name);
    if (idx < 0) {
        say(tr("用户不存在"));
        return false;
    }
    const User& u = users_[idx];
    if (!actor.isEmpty() && User::namesEqual(u.name, actor)) {
        say(tr("不能禁用当前登录的用户"));
        return false;
    }
    if (u.disabled) {
        say(tr("该用户已经是禁用状态"));
        return false;
    }
    if (u.role == Role::Admin && enabledAdminCount() <= 1) {
        say(tr("这是最后一个 enabled Admin,禁用后没人能登录(请先新建另一个 Admin)"));
        return false;
    }
    say(QString());
    return true;
}

bool UserManager::enableUser(const QString& name) {
    const int idx = indexOf(name);
    if (idx < 0) {
        qCWarning(hmiApp) << "[UserMgr] enableUser 失败:用户不存在" << name;
        return false;
    }
    if (!users_[idx].disabled) {
        qCWarning(hmiApp) << "[UserMgr] enableUser 跳过:本来就是启用状态" << users_[idx].name;
        return false;
    }
    users_[idx].disabled = false;
    qCInfo(hmiApp) << "[UserMgr] 用户已重新启用:" << users_[idx].name;
    emit userEnabled(users_[idx].name);
    return true;
}

int UserManager::enabledAdminCount() const {
    int n = 0;
    for (const User& u : users_) {
        if (u.role == Role::Admin && !u.disabled) ++n;
    }
    return n;
}

bool UserManager::changePassword(const QString& name, const QString& oldPlain, const QString& newPlain) {
    if (newPlain.isEmpty()) {
        qCWarning(hmiApp) << "[UserMgr] changePassword 失败:新密码空";
        return false;
    }
    const int idx = indexOf(name);
    if (idx < 0) {
        qCWarning(hmiApp) << "[UserMgr] changePassword 失败:用户不存在" << name;
        return false;
    }
    if (!verifyPassword(oldPlain, users_[idx].salt, users_[idx].passwordHash)) {
        qCWarning(hmiApp) << "[UserMgr] changePassword 失败:旧密码错" << users_[idx].name;
        emit authenticationFailed(users_[idx].name);  // 复用审计信号
        return false;
    }
    // 生成新 salt + 重 hash(防止同密码重用旧 salt)
    users_[idx].salt        = generateSalt();
    users_[idx].passwordHash = hashPassword(newPlain, users_[idx].salt);
    qCInfo(hmiApp) << "[UserMgr] 密码已修改:" << users_[idx].name;
    emit passwordChanged(users_[idx].name);
    return true;
}

bool UserManager::authenticate(const QString& name, const QString& plainPassword) {
    const int idx = indexOf(name);
    if (idx < 0) {
        qCWarning(hmiApp) << "[UserMgr] authenticate 失败:用户不存在";
        emit authenticationFailed(name);
        return false;
    }
    const User& u = users_[idx];
    if (u.disabled) {
        qCWarning(hmiApp) << "[UserMgr] authenticate 失败:用户被禁用" << u.name;
        emit authenticationFailed(u.name);
        return false;
    }
    if (!verifyPassword(plainPassword, u.salt, u.passwordHash)) {
        qCWarning(hmiApp) << "[UserMgr] authenticate 失败:密码错" << u.name;
        emit authenticationFailed(u.name);
        return false;
    }
    qCInfo(hmiApp) << "[UserMgr] 认证成功:" << u.name;
    return true;
}

const User* UserManager::findUser(const QString& name) const {
    const int idx = indexOf(name);
    return idx < 0 ? nullptr : &users_[idx];
}

bool UserManager::hasUsers() const {
    // 任何 enabled 用户就算存在(已 disabled 不算)
    return std::any_of(users_.cbegin(), users_.cend(),
                       [](const User& u) { return !u.disabled; });
}

QList<User> UserManager::listUsers() const {
    QList<User> list = users_;
    // 按名字升序排(忽略大小写)
    std::sort(list.begin(), list.end(),
              [](const User& a, const User& b) {
                  return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
              });
    return list;
}

} // namespace qwhmi::auth