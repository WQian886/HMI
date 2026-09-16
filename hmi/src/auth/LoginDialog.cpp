// ============================================================================
// 阶段 B · B2 · LoginDialog 实现
//
// 关联:LoginDialog.h
// ============================================================================

#include "auth/LoginDialog.h"
#include "auth/UserManager.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#include "logging/Logging.h"

namespace qwhmi::auth {

LoginDialog::LoginDialog(UserManager& mgr, QWidget* parent)
    : QDialog(parent), mgr_(mgr) {
    setWindowTitle(tr("登录 HMI"));
    setModal(true);
    setMinimumWidth(320);

    // ===== 控件 =====
    auto* nameLabel = new QLabel(tr("用户名:"), this);
    auto* pwLabel   = new QLabel(tr("密码:"), this);
    nameEdit_     = new QLineEdit(this);
    passwordEdit_ = new QLineEdit(this);
    passwordEdit_->setEchoMode(QLineEdit::Password);
    errorLabel_ = new QLabel(this);
    errorLabel_->setStyleSheet("color: red;");
    errorLabel_->setWordWrap(true);
    errorLabel_->setMinimumHeight(20);
    lockLabel_ = new QLabel(this);
    lockLabel_->setStyleSheet("color: darkred; font-weight: bold;");
    lockLabel_->setVisible(false);

    okBtn_       = new QPushButton(tr("登录"), this);
    auto* cancelBtn = new QPushButton(tr("取消"), this);

    // ===== 布局 =====
    auto* grid = new QGridLayout;
    grid->addWidget(nameLabel, 0, 0);
    grid->addWidget(nameEdit_, 0, 1);
    grid->addWidget(pwLabel,   1, 0);
    grid->addWidget(passwordEdit_, 1, 1);

    auto* btnRow = new QHBoxLayout;
    btnRow->addStretch();
    btnRow->addWidget(cancelBtn);
    btnRow->addWidget(okBtn_);

    auto* root = new QVBoxLayout(this);
    root->addLayout(grid);
    root->addWidget(lockLabel_);
    root->addWidget(errorLabel_);
    root->addLayout(btnRow);

    okBtn_->setDefault(true);

    // ===== timer(每秒刷新倒计时) =====
    lockTimer_ = new QTimer(this);
    lockTimer_->setInterval(1000);

    // ===== 信号 =====
    connect(okBtn_, &QPushButton::clicked, this, &LoginDialog::onAccept);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(nameEdit_, &QLineEdit::textChanged, this, &LoginDialog::validateInputs);
    connect(passwordEdit_, &QLineEdit::textChanged, this, &LoginDialog::validateInputs);
    connect(lockTimer_, &QTimer::timeout, this, &LoginDialog::tickLockCountdown);

    validateInputs();
}

LoginDialog::~LoginDialog() = default;

void LoginDialog::validateInputs() {
    errorLabel_->clear();
    okBtn_->setEnabled(!nameEdit_->text().trimmed().isEmpty()
                       && !passwordEdit_->text().isEmpty()
                       && !isLocked());
}

bool LoginDialog::isLocked() const {
    return lockUntil_.isValid() && QDateTime::currentDateTime() < lockUntil_;
}

void LoginDialog::startLock(int seconds) {
    lockUntil_ = QDateTime::currentDateTime().addSecs(seconds);
    lockTimer_->start();
    applyLockUI(true, seconds);
    qCWarning(hmiApp) << "[Login] 触发锁定:" << seconds << "秒(失败" << failedAttempts_ << "次)";
}

void LoginDialog::applyLockUI(bool locked, int remainSec) {
    nameEdit_->setEnabled(!locked);
    passwordEdit_->setEnabled(!locked);
    okBtn_->setEnabled(!locked && !nameEdit_->text().trimmed().isEmpty() && !passwordEdit_->text().isEmpty());
    if (locked && remainSec > 0) {
        lockLabel_->setText(tr("⏳ 锁定中,剩余 %1 秒(失败过多)").arg(remainSec));
        lockLabel_->setVisible(true);
    } else {
        lockLabel_->setVisible(false);
        lockLabel_->clear();
    }
}

void LoginDialog::tickLockCountdown() {
    const qint64 remainMs = QDateTime::currentDateTime().msecsTo(lockUntil_);
    if (remainMs <= 0) {
        lockTimer_->stop();
        lockUntil_ = QDateTime();  // 标记无效
        failedAttempts_ = 0;
        applyLockUI(false);
        qCInfo(hmiApp) << "[Login] 锁定解除";
        return;
    }
    applyLockUI(true, static_cast<int>(remainMs / 1000));
}

void LoginDialog::onAccept() {
    const QString name = nameEdit_->text().trimmed();
    const QString pw   = passwordEdit_->text();

    if (!mgr_.authenticate(name, pw)) {
        ++failedAttempts_;
        errorLabel_->setText(tr("用户名或密码错误(还剩 %1 次尝试)").arg(kMaxAttempts - failedAttempts_));
        passwordEdit_->clear();
        if (failedAttempts_ >= kMaxAttempts) {
            startLock(kLockSeconds);
        } else {
            validateInputs();
        }
        return;
    }

    // 成功 → 找回来拿到完整 User 对象
    const User* u = mgr_.findUser(name);
    if (u == nullptr) {
        // 理论不可能(authenticate 通过必然有用户)
        QMessageBox::critical(this, tr("内部错误"), tr("认证成功但用户记录缺失"));
        return;
    }
    loggedInUser_ = *u;
    failedAttempts_ = 0;
    qCInfo(hmiApp) << "[Login] 登录成功:" << loggedInUser_.name << "role=" << roleToString(loggedInUser_.role);
    accept();
}

int LoginDialog::exec() {
    nameEdit_->setFocus();
    // 如果上一次有遗留锁定,先应用
    if (isLocked()) {
        const qint64 remainMs = QDateTime::currentDateTime().msecsTo(lockUntil_);
        startLock(static_cast<int>(remainMs / 1000));
    } else {
        // 重置上次状态
        failedAttempts_ = 0;
        applyLockUI(false);
    }
    return QDialog::exec();
}

} // namespace qwhmi::auth