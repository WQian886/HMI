// ============================================================================
// 阶段 B · B2.4 · 新增用户对话框
//
// 文件职责:Admin 在 UserManagementDialog 里点"新增" → 弹这个子对话框。
//
// 边界:B2.4 不做"重置他人密码"和"改角色"——留后续。
//
// 关联:UserManagementDialog.h, UserManager.h, PasswordHasher.h
// ============================================================================

#pragma once

#include "auth/Role.h"

#include <QDialog>
#include <QString>

class QLineEdit;
class QComboBox;
class QLabel;
class QPushButton;

namespace qwhmi::auth {

class UserManager;

class NewUserDialog : public QDialog {
    Q_OBJECT
public:
    // 返回的 role 由调用方从 mgr.addUser 决定;这里只负责输入校验
    explicit NewUserDialog(UserManager& mgr, QWidget* parent = nullptr);
    ~NewUserDialog() override;

    // exec() == Accepted 后,以下 getter 可读(给 caller 调 mgr.addUser)
    QString newName() const;
    QString newPassword() const;
    Role    newRole() const;

private slots:
    void onAccept();
    void validateInputs();

private:
    UserManager&  mgr_;
    QLineEdit*    nameEdit_     = nullptr;
    QLineEdit*    passwordEdit_ = nullptr;
    QComboBox*    roleCombo_    = nullptr;
    QLabel*       errorLabel_   = nullptr;
    QPushButton*  okBtn_        = nullptr;

    QString acceptedName_;
    QString acceptedPassword_;
    Role    acceptedRole_ = Role::Viewer;
};

} // namespace qwhmi::auth