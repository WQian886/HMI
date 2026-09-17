// ============================================================================
// 阶段 B · B3 · Permission 实现
//
// 角色 → 权限映射表:
//   Admin    : 全 6 个(含 ManageUsers)
//   Engineer : 除 ManageUsers 外 5 个
//   Operator : WriteVariable + AcknowledgeAlarm(2 个)
//   Viewer   : 0 个(只读)
//
// 关联:Permission.h
// ============================================================================

#include "auth/Permission.h"

#include <QHash>

namespace qwhmi::auth {

Q_LOGGING_CATEGORY(hmiPerm, "hmi.perm")

// ----- 静态映射表 -----

// ----- 静态映射表 -----
namespace {
const QHash<Role, QList<Permission>>& permissionMap() {
    static const QHash<Role, QList<Permission>> m = {
        // Admin:全 6 个
        { Role::Admin, {
            Permission::WriteVariable,
            Permission::AcknowledgeAlarm,
            Permission::EditRecipe,
            Permission::ExportReport,
            Permission::ManageUsers,
            Permission::EditProject,
        }},
        // Engineer:除 ManageUsers 外 5 个
        { Role::Engineer, {
            Permission::WriteVariable,
            Permission::AcknowledgeAlarm,
            Permission::EditRecipe,
            Permission::ExportReport,
            Permission::EditProject,
        }},
        // Operator:写值 + 确认报警 2 个
        { Role::Operator, {
            Permission::WriteVariable,
            Permission::AcknowledgeAlarm,
        }},
        // Viewer:0 个
        { Role::Viewer, {} },
    };
    return m;
}
}

QList<Permission> rolePermissions(Role r) {
    return permissionMap().value(r);
}

bool checkPermission(Role r, Permission p) {
    return permissionMap().value(r).contains(p);
}

QString permissionToString(Permission p) {
    switch (p) {
    case Permission::WriteVariable:    return QStringLiteral("WriteVariable");
    case Permission::AcknowledgeAlarm: return QStringLiteral("AcknowledgeAlarm");
    case Permission::EditRecipe:       return QStringLiteral("EditRecipe");
    case Permission::ExportReport:     return QStringLiteral("ExportReport");
    case Permission::ManageUsers:      return QStringLiteral("ManageUsers");
    case Permission::EditProject:      return QStringLiteral("EditProject");
    }
    return QStringLiteral("?");
}

Permission permissionFromString(const QString& s) {
    const QString k = s.trimmed();
    if (k == "WriteVariable")     return Permission::WriteVariable;
    if (k == "AcknowledgeAlarm")  return Permission::AcknowledgeAlarm;
    if (k == "EditRecipe")        return Permission::EditRecipe;
    if (k == "ExportReport")      return Permission::ExportReport;
    if (k == "ManageUsers")       return Permission::ManageUsers;
    if (k == "EditProject")       return Permission::EditProject;
    return Permission::WriteVariable;  // 默认最低权限
}

} // namespace qwhmi::auth