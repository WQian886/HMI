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

bool UserManager::removeUser(const QString& name) {
    const int idx = indexOf(name);
    if (idx < 0) {
        return false;
    }
    const QString trimmed = users_[idx].name;
    // 软删除:标记 disabled 而不是真 erase(审计 + 历史)
    users_[idx].disabled = true;
    qCInfo(hmiApp) << "[UserMgr] 用户已禁用(软删除):" << trimmed;
    emit userRemoved(trimmed);
    return true;
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