// ============================================================================
// 阶段 B · B1 · 角色枚举
//
// 文件职责:定义 4 个内置角色 + 角色 <-> 字符串互转 + 权限查询。
//
// 为什么 4 个:对应 v0.2.0 HTML B1 任务里"Admin/Engineer/Operator/Viewer"。
//   - Admin      全权,包括用户管理
//   - Engineer   改工程、写值、确认报警
//   - Operator   看 + 写值 + 确认报警,不能改工程
//   - Viewer     只看,不能写
//
// 关联:docs/优化与改善计划.html "阶段 B1"
// ============================================================================

#pragma once

#include <QString>

namespace qwhmi::auth {

enum class Role {
    Admin      = 0,
    Engineer   = 1,
    Operator   = 2,
    Viewer     = 3,
};

// Role <-> QString 互转(JSON 持久化用)
QString roleToString(Role r);
Role roleFromString(const QString& s);

// 人类可读的中文名(UI 显示用)
QString roleDisplayName(Role r);

// 角色是否有"改工程"权限(简化版,后续 B3 用 Permission 枚举细化)
bool roleCanEditProject(Role r);

// 角色是否有"写值"权限
bool roleCanWriteValue(Role r);

} // namespace qwhmi::auth