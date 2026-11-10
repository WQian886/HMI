// ============================================================================
// 阶段 B · B2.4 · 用户管理对话框
//
// 文件职责:Admin 查看所有用户 + 新增 + 禁用 / 启用用户。
//
// 权限:只 Admin 才能打开。
//
// 行为:
//   - QTableWidget 显示所有用户(name / role / disabled / createdAt)
//   - "新增"按钮 → 弹 NewUserDialog
//   - "禁用选中"按钮 → 软删除当前选中(disabled=true)
//   - "启用选中"按钮 → 把当前选中重新启用(disabled=false)★ 2026-11-10 (G-30)
//
// ★ 2026-11-10 (G-30):本文件原来自己判"能不能禁" —— 而且是拿【显示文本】
//   (角色列的"管理员"、禁用列的"是")去比,换个语言/改个显示名就失效。
//   现在判据搬到公共层(UserManager::canDisableUser / removeUser),
//   这里只负责把 reason 弹出来 —— 同 tst_MenuBar / tst_UIPermission 的分工。
//
// 边界:B2.4 不做"改角色"(禁用/启用 2026-11-10 补上了)。
//
// 关联:UserManager.h, CurrentUser.h, NewUserDialog.h
// ============================================================================

#pragma once

#include <QDialog>

class QTableWidget;
class QPushButton;
class QLabel;

namespace qwhmi::auth {

class UserManager;

class UserManagementDialog : public QDialog {
    Q_OBJECT
public:
    // 要求:CurrentUser.role() == Admin(否则 exec() 拒绝)
    explicit UserManagementDialog(UserManager& mgr, QWidget* parent = nullptr);
    ~UserManagementDialog() override;

    int exec() override;

private slots:
    void refreshTable();
    void onAddUser();
    void onDeleteUser();
    void onEnableUser();   // ★ 2026-11-10 (G-30)

private:
    void setupTable();
    void setupButtons();

    UserManager&    mgr_;
    QTableWidget*   table_       = nullptr;
    QLabel*         infoLabel_   = nullptr;
    QPushButton*    addBtn_      = nullptr;
    QPushButton*    deleteBtn_   = nullptr;
    QPushButton*    enableBtn_   = nullptr;   // ★ 2026-11-10 (G-30)
    QPushButton*    refreshBtn_  = nullptr;
    QPushButton*    closeBtn_    = nullptr;
};

} // namespace qwhmi::auth