// ============================================================================
// 阶段 B · B3 · 权限枚举 + 角色 → 权限映射
//
// 文件职责:6 个权限枚举 + checkPermission(role, perm) + rolePermissions(role) 静态表。
//
// 为什么单独成文件(不嵌 User.h):
//   - 权限 vs 角色 是两个概念:角色是身份标签,权限是行为白名单
//   - 后续 B4 画面级权限会基于 Permission(requiredPermission 字段)
//   - B5 审计需要枚举所有权限用于日志分类
//
// 设计原则:
//   - 权限枚举尽量"扁平"(6 个够用,不引入"资源 / 范围"等复杂模型)
//   - 检查失败一律 false + 调日志,B5 审计会接 checkFailed signal
//
// 关联:Role.h, UserManager.h
// ============================================================================

#pragma once

#include "auth/Role.h"

#include <QLoggingCategory>

namespace qwhmi::auth {

Q_DECLARE_LOGGING_CATEGORY(hmiPerm)

// 6 个核心权限(HTML v0.2.0 B3 + 商业补充)
enum class Permission {
    WriteVariable     = 0,    // 写变量值(Binding::bindEdit / WriteScheduler)
    AcknowledgeAlarm  = 1,    // 确认报警(AlarmManager::acknowledge)
    EditRecipe        = 2,    // 改配方(H 阶段:占位预留)
    ExportReport      = 3,    // 导出报表(H 阶段:占位预留)
    ManageUsers       = 4,    // 用户管理(UserManagementDialog)
    EditProject       = 5,    // 编辑工程文件(D 阶段:占位预留)
};

// 角色 → 该角色拥有的所有权限
QList<Permission> rolePermissions(Role r);

// 单点检查:某角色是否有某权限
bool checkPermission(Role r, Permission p);

// 字符串互转(UI / JSON / 日志 用)
QString permissionToString(Permission p);
Permission permissionFromString(const QString& s);

} // namespace qwhmi::auth

Q_DECLARE_METATYPE(qwhmi::auth::Permission)