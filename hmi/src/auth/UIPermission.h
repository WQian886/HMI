// ============================================================================
// 阶段 B · B4 · UI 权限绑定(画面级权限)
//
// 文件职责:把 QPushButton / QAction 的 setEnabled 绑到 Permission,让"无权限的
//   控件直接是灰的",而不是点了才被拒(B3 兜底)。
//
// 行为:
//   - bindButtonEnable / bindActionEnable:注册一个"控件 ↔ 权限"绑定
//     · 立即按当前 CurrentUser.role 应用一次
//     · CurrentUser 切换(userLoggedIn/userLoggedOut)时自动重算
//   - applyAllBindings:手动触发一次全部应用(通常不需要,signal 已自动)
//
// 设计要点:
//   - 控件必须存活到 main 结束(本项目里按钮是 hmi 窗口的子对象,符合)
//   - 重复注册同一控件 → 覆盖(以最后一次为准)
//   - 控件 == nullptr → no-op(防御)
//   - 内部用 QHash<QObject*, Entry> 注册表 + 函数局部 static 标志保证
//     CurrentUser signal 只 connect 一次
//
// 与 B3 的关系:
//   - B3 拦截"动作发生瞬间"(Binding.cpp:148 / AlarmManager.cpp:35)
//   - B4 拦截"动作发生前"(UI 灰)
//   - B3 是兜底,B4 是 UX,两者并存
//
// 关联:Permission.h, CurrentUser.h
// ============================================================================

#pragma once

#include "auth/Permission.h"

class QAbstractButton;
class QAction;

namespace qwhmi::auth {

// 把按钮的 enabled 绑到某 Permission
//   - 立即按当前 CurrentUser.role 应用一次
//   - CurrentUser 切换时自动重算
//   - btn 必须存活到 main 结束
//   - 重复注册 → 覆盖
void bindButtonEnable(QAbstractButton* btn, Permission p);

// QAction 版(给托盘菜单项 / 工具栏按钮用,比如"用户管理"绑 ManageUsers)
//   - QAction 跟 QAbstractButton 都继承 QObject,各自有 setEnabled
//   - 行为同 bindButtonEnable
void bindActionEnable(QAction* act, Permission p);

// 手动触发一次全部应用
//   - 通常不需要:首次 bind 时已应用,CurrentUser 切换时 signal 自动触发
//   - 给极端场景用(测试 / 启动时序异常 / 调 Permission 后热重算)
void applyAllBindings();

} // namespace qwhmi::auth
