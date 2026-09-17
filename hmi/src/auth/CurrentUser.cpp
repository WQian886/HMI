// ============================================================================
// 阶段 B · B2.2 · CurrentUser 实现
//
// 关联:CurrentUser.h
// ============================================================================

#include "auth/CurrentUser.h"

#include "logging/Logging.h"

namespace qwhmi::auth {

// Q_GLOBAL_STATIC 保证线程安全 + 静态析构顺序
// (Qt 6 内置,不需要手写 pthread_once)
#define Q_GLOBAL_STATIC_WITH_DECL(name, ctor) \
    static QGlobalStatic<name> name;

CurrentUser& CurrentUser::instance() {
    static CurrentUser inst;
    return inst;
}

CurrentUser* CurrentUser::instancePtr() {
    return &instance();
}

CurrentUser::CurrentUser(QObject* parent) : QObject(parent) {}

void CurrentUser::login(const User& u) {
    const bool wasLoggedIn = loggedIn_;
    name_     = u.name;
    role_     = u.role;
    loggedIn_ = true;
    qCInfo(hmiApp) << "[CurrentUser] 登录:" << name_ << "role=" << roleToString(role_);
    emit userLoggedIn(name_);
    if (!wasLoggedIn) {
        // 首次登录也发一次(让监听者能区分"切换 vs 首次")
    }
}

void CurrentUser::logout() {
    if (!loggedIn_) return;
    qCInfo(hmiApp) << "[CurrentUser] 登出:" << name_;
    name_.clear();
    role_     = Role::Viewer;
    loggedIn_ = false;
    emit userLoggedOut();
}

} // namespace qwhmi::auth