// ============================================================================
// 阶段 B · B2 · 登录对话框
//
// 文件职责:已有用户时启动弹,验证用户名 + 密码。失败 N 次锁定 M 分钟。
//
// 行为:
//   - 模态 QDialog
//   - 用户名 + 密码 + OK/Cancel
//   - 失败 3 次 → 锁定 5 分钟(剩余时间显示,输入禁用)
//   - 锁定时间到 → 自动解锁
//   - 成功后通过 currentUser() 拿到已登录用户
//
// 为什么锁定逻辑放对话框内部(不放 UserManager):
//   - B2 阶段最小可用;锁定是 UI 关注点(弹窗禁用输入 + 倒计时)
//   - 后续若需要全局锁定(多处共享),再升级到 UserManager.lockedUntil()
//
// 关联:UserManager.h, User.h, PasswordHasher.h
// ============================================================================

#pragma once

#include "auth/User.h"

#include <QDialog>
#include <QDateTime>
#include <QString>

class QLineEdit;
class QLabel;
class QPushButton;
class QTimer;

namespace qwhmi::auth {

class UserManager;

class LoginDialog : public QDialog {
    Q_OBJECT
public:
    explicit LoginDialog(UserManager& mgr, QWidget* parent = nullptr);
    ~LoginDialog() override;

    // exec() 返回:
    //   QDialog::Accepted → 登录成功,currentUser() 返已登录用户
    //   QDialog::Rejected → 用户取消(包含锁定期间关对话框)
    int exec() override;

    // 已登录用户(仅 exec() == Accepted 时有意义)
    const User& currentUser() const { return loggedInUser_; }

    // 锁定配置(可调整,后续如要外部配)
    static constexpr int kMaxAttempts = 3;
    static constexpr int kLockSeconds = 5 * 60;  // 5 分钟

private slots:
    void onAccept();
    void validateInputs();
    void tickLockCountdown();

private:
    void startLock(int seconds);
    bool isLocked() const;
    void applyLockUI(bool locked, int remainSec = 0);

    UserManager&  mgr_;
    QLineEdit*    nameEdit_     = nullptr;
    QLineEdit*    passwordEdit_ = nullptr;
    QLabel*       errorLabel_   = nullptr;
    QLabel*       lockLabel_    = nullptr;   // 倒计时显示
    QPushButton*  okBtn_        = nullptr;

    int           failedAttempts_ = 0;
    QDateTime     lockUntil_;    // 锁定截止时间
    QTimer*       lockTimer_    = nullptr;
    User          loggedInUser_;
};

} // namespace qwhmi::auth