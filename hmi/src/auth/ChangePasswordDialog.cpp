// ============================================================================
// 阶段 B · B2.3 · ChangePasswordDialog 实现
//
// 关联:ChangePasswordDialog.h
// ============================================================================

#include "auth/ChangePasswordDialog.h"
#include "auth/CurrentUser.h"
#include "auth/UserManager.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include "logging/Logging.h"

namespace qwhmi::auth {

namespace {
constexpr int kMinPasswordLength = 6;
}

ChangePasswordDialog::ChangePasswordDialog(UserManager& mgr, QWidget* parent)
    : QDialog(parent), mgr_(mgr) {
    setWindowTitle(tr("修改密码"));
    setModal(true);
    setMinimumWidth(320);

    auto* oldLabel = new QLabel(tr("当前密码:"), this);
    auto* newLabel = new QLabel(tr("新密码(≥ 6 字符):"), this);
    auto* cfLabel  = new QLabel(tr("确认新密码:"), this);
    oldPwEdit_   = new QLineEdit(this);
    newPwEdit_   = new QLineEdit(this);
    confirmEdit_ = new QLineEdit(this);
    oldPwEdit_->setEchoMode(QLineEdit::Password);
    newPwEdit_->setEchoMode(QLineEdit::Password);
    confirmEdit_->setEchoMode(QLineEdit::Password);
    errorLabel_ = new QLabel(this);
    errorLabel_->setStyleSheet("color: red;");
    errorLabel_->setWordWrap(true);
    errorLabel_->setMinimumHeight(20);

    okBtn_       = new QPushButton(tr("确认修改"), this);
    auto* cancelBtn = new QPushButton(tr("取消"), this);

    auto* grid = new QGridLayout;
    grid->addWidget(oldLabel,    0, 0);
    grid->addWidget(oldPwEdit_,  0, 1);
    grid->addWidget(newLabel,    1, 0);
    grid->addWidget(newPwEdit_,  1, 1);
    grid->addWidget(cfLabel,     2, 0);
    grid->addWidget(confirmEdit_,2, 1);

    auto* btnRow = new QHBoxLayout;
    btnRow->addStretch();
    btnRow->addWidget(cancelBtn);
    btnRow->addWidget(okBtn_);

    auto* root = new QVBoxLayout(this);
    root->addLayout(grid);
    root->addWidget(errorLabel_);
    root->addLayout(btnRow);

    okBtn_->setDefault(true);

    connect(okBtn_, &QPushButton::clicked, this, &ChangePasswordDialog::onAccept);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(oldPwEdit_, &QLineEdit::textChanged, this, &ChangePasswordDialog::validateInputs);
    connect(newPwEdit_, &QLineEdit::textChanged, this, &ChangePasswordDialog::validateInputs);
    connect(confirmEdit_, &QLineEdit::textChanged, this, &ChangePasswordDialog::validateInputs);

    validateInputs();
}

ChangePasswordDialog::~ChangePasswordDialog() = default;

void ChangePasswordDialog::validateInputs() {
    errorLabel_->clear();
    okBtn_->setEnabled(false);

    const QString oldPw  = oldPwEdit_->text();
    const QString newPw  = newPwEdit_->text();
    const QString cf     = confirmEdit_->text();

    if (oldPw.isEmpty() || newPw.isEmpty() || cf.isEmpty()) return;
    if (newPw.length() < kMinPasswordLength) {
        errorLabel_->setText(tr("新密码至少 %1 字符").arg(kMinPasswordLength));
        return;
    }
    if (newPw != cf) {
        errorLabel_->setText(tr("两次新密码不一致"));
        return;
    }
    if (oldPw == newPw) {
        errorLabel_->setText(tr("新密码不能与旧密码相同"));
        return;
    }
    okBtn_->setEnabled(true);
}

void ChangePasswordDialog::onAccept() {
    const QString name = CurrentUser::instance().name();
    if (name.isEmpty() || !CurrentUser::instance().isLoggedIn()) {
        QMessageBox::warning(this, tr("未登录"), tr("当前未登录,无法改密"));
        return;
    }
    const QString oldPw = oldPwEdit_->text();
    const QString newPw = newPwEdit_->text();
    if (!mgr_.changePassword(name, oldPw, newPw)) {
        errorLabel_->setText(tr("改密失败:旧密码错或参数非法"));
        oldPwEdit_->clear();
        oldPwEdit_->setFocus();
        return;
    }
    qCInfo(hmiApp) << "[ChangePwd] 用户" << name << "改密成功";
    accept();
}

int ChangePasswordDialog::exec() {
    // 防御性检查:必须登录才能改密
    if (!CurrentUser::instance().isLoggedIn()) {
        QMessageBox::warning(this, tr("未登录"), tr("必须先登录才能改密码"));
        return QDialog::Rejected;
    }
    oldPwEdit_->setFocus();
    return QDialog::exec();
}

} // namespace qwhmi::auth