// ============================================================================
// 阶段 B · B2.3 · 修改自己密码的对话框
//
// 文件职责:让当前登录用户改自己的密码(验旧 + 新密码 ≥ 6 字符 + 确认)。
//
// 边界:B2.3 阶段不做 admin 重置他人密码(留到后续阶段或单)。
//
// 关联:UserManager.h, CurrentUser.h, PasswordHasher.h
// ============================================================================

#pragma once

#include <QDialog>

class QLineEdit;
class QLabel;
class QPushButton;

namespace qwhmi::auth {

class UserManager;

class ChangePasswordDialog : public QDialog {
    Q_OBJECT
public:
    // mgr:用于调 changePassword(name, oldPlain, newPlain)
    // 要求:调用前 CurrentUser.isLoggedIn() 必须 true(否则对话框禁用所有输入)
    explicit ChangePasswordDialog(UserManager& mgr, QWidget* parent = nullptr);
    ~ChangePasswordDialog() override;

    int exec() override;

private slots:
    void onAccept();
    void validateInputs();

private:
    UserManager&  mgr_;
    QLineEdit*    oldPwEdit_     = nullptr;
    QLineEdit*    newPwEdit_     = nullptr;
    QLineEdit*    confirmEdit_   = nullptr;
    QLabel*       errorLabel_    = nullptr;
    QPushButton*  okBtn_         = nullptr;
};

} // namespace qwhmi::auth