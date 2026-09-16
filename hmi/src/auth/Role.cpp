// ============================================================================
// 阶段 B · B1 · Role 实现
//
// 字符串互转用 Qt 全局函数 QStringLiteral,中文名走 tr() 后续可 i18n。
// 关联:Role.h
// ============================================================================

#include "auth/Role.h"

namespace qwhmi::auth {

QString roleToString(Role r) {
    switch (r) {
    case Role::Admin:    return QStringLiteral("admin");
    case Role::Engineer: return QStringLiteral("engineer");
    case Role::Operator: return QStringLiteral("operator");
    case Role::Viewer:   return QStringLiteral("viewer");
    }
    return QStringLiteral("viewer");
}

Role roleFromString(const QString& s) {
    const QString k = s.trimmed().toLower();
    if (k == QLatin1String("admin"))    return Role::Admin;
    if (k == QLatin1String("engineer")) return Role::Engineer;
    if (k == QLatin1String("operator")) return Role::Operator;
    if (k == QLatin1String("viewer"))   return Role::Viewer;
    return Role::Viewer;  // 默认最低权限
}

QString roleDisplayName(Role r) {
    switch (r) {
    case Role::Admin:    return QStringLiteral("管理员");
    case Role::Engineer: return QStringLiteral("工程师");
    case Role::Operator: return QStringLiteral("操作员");
    case Role::Viewer:   return QStringLiteral("观察员");
    }
    return QStringLiteral("观察员");
}

bool roleCanEditProject(Role r) {
    return r == Role::Admin || r == Role::Engineer;
}

bool roleCanWriteValue(Role r) {
    // Viewer 不能写值,其他都能
    return r != Role::Viewer;
}

} // namespace qwhmi::auth