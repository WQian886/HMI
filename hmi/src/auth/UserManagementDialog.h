// ============================================================================
// 阶段 B · B2.4 · 用户管理对话框
//
// 文件职责:Admin 查看所有用户 + 新增 + 删除用户。
//
// 权限:只 Admin 才能打开。
//
// 行为:
//   - QTableWidget 显示所有用户(name / role / disabled / createdAt)
//   - "新增"按钮 → 弹 NewUserDialog
//   - "删除"按钮 → 软删除当前选中(disabled=true)
//   - 防止 Admin 删自己 / 删最后一个 admin
//
// 边界:B2.4 不做"恢复 disabled 用户"和"改角色"。
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

private:
    void setupTable();
    void setupButtons();

    UserManager&    mgr_;
    QTableWidget*   table_       = nullptr;
    QLabel*         infoLabel_   = nullptr;
    QPushButton*    addBtn_      = nullptr;
    QPushButton*    deleteBtn_   = nullptr;
    QPushButton*    refreshBtn_  = nullptr;
    QPushButton*    closeBtn_    = nullptr;
};

} // namespace qwhmi::auth