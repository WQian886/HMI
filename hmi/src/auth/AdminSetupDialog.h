// ============================================================================
// 阶段 B · B1.3 · 首次启动引导对话框
//
// 文件职责:首启无用户时弹,让用户建第一个 admin 账号。
//
// 为什么独立一个对话框(不直接做完整 LoginDialog):
//   - B1.3 只关心"有没有人"
//   - B2 阶段才做"已有用户 → 登录界面 + 失败锁定"
//   - 这个 dialog B2 可能会复用其布局思路(账号 + 密码 + 确认),所以命名 AdminSetupDialog
//
// 行为:
//   - 模态 QDialog,首启时 exec()
//   - 必填用户名 + 密码 + 确认密码
//   - 密码 ≥ 6 字符(简单防线,后续 B6 加密阶段会升级)
//   - 用户名去前后空格,大小写不敏感查重
//   - 取消/关闭 = 拒绝建账号,main 走"无用户强制退出"路径
//
// 关联:UserManager.h,PasswordHasher.h
// ============================================================================

#pragma once

#include <QDialog>
#include <QString>

class QLineEdit;
class QLabel;
class QPushButton;

namespace qwhmi::auth {

class UserManager;

class AdminSetupDialog : public QDialog {
    Q_OBJECT
public:
    explicit AdminSetupDialog(UserManager& mgr, QWidget* parent = nullptr);
    ~AdminSetupDialog() override;

    // exec() 返回 QDialog::Accepted(已建账号)或 QDialog::Rejected(取消)
    // 已建账号可通过 mgr.findUser("xxx") 找到
    int exec() override;

private slots:
    void onAccept();
    void validateInputs();

private:
    UserManager&  mgr_;
    QLineEdit*    nameEdit_      = nullptr;
    QLineEdit*    passwordEdit_  = nullptr;
    QLineEdit*    confirmEdit_   = nullptr;
    QLabel*       errorLabel_    = nullptr;
    QPushButton*  okBtn_         = nullptr;
};

} // namespace qwhmi::auth