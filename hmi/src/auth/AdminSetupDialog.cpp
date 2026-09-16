// ============================================================================
// 阶段 B · B1.3 · AdminSetupDialog 实现
//
// 手写布局(不用 .ui):B1.3 阶段最小版,3 行(账号/密码/确认)+ 错误提示 + OK/Cancel。
//
// 关联:AdminSetupDialog.h
// ============================================================================

#include "auth/AdminSetupDialog.h"
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

AdminSetupDialog::AdminSetupDialog(UserManager& mgr, QWidget* parent)
    : QDialog(parent), mgr_(mgr) {
    setWindowTitle(tr("首次启动 · 创建管理员账号"));
    setModal(true);
    setMinimumWidth(360);

    // ===== 控件 =====
    auto* nameLabel = new QLabel(tr("用户名:"), this);
    auto* pwLabel   = new QLabel(tr("密码(≥ 6 字符):"), this);
    auto* cfLabel   = new QLabel(tr("确认密码:"), this);
    nameEdit_     = new QLineEdit(this);
    passwordEdit_ = new QLineEdit(this);
    confirmEdit_  = new QLineEdit(this);
    passwordEdit_->setEchoMode(QLineEdit::Password);
    confirmEdit_->setEchoMode(QLineEdit::Password);
    errorLabel_ = new QLabel(this);
    errorLabel_->setStyleSheet("color: red;");
    errorLabel_->setWordWrap(true);
    errorLabel_->setMinimumHeight(20);

    okBtn_     = new QPushButton(tr("创建"), this);
    auto* cancelBtn = new QPushButton(tr("取消"), this);

    // ===== 布局 =====
    auto* grid = new QGridLayout;
    grid->addWidget(nameLabel, 0, 0);
    grid->addWidget(nameEdit_, 0, 1);
    grid->addWidget(pwLabel,   1, 0);
    grid->addWidget(passwordEdit_, 1, 1);
    grid->addWidget(cfLabel,   2, 0);
    grid->addWidget(confirmEdit_, 2, 1);

    auto* btnRow = new QHBoxLayout;
    btnRow->addStretch();
    btnRow->addWidget(cancelBtn);
    btnRow->addWidget(okBtn_);

    auto* root = new QVBoxLayout(this);
    root->addLayout(grid);
    root->addWidget(errorLabel_);
    root->addLayout(btnRow);

    okBtn_->setDefault(true);

    // ===== 信号 =====
    connect(okBtn_, &QPushButton::clicked, this, &AdminSetupDialog::onAccept);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(nameEdit_, &QLineEdit::textChanged, this, &AdminSetupDialog::validateInputs);
    connect(passwordEdit_, &QLineEdit::textChanged, this, &AdminSetupDialog::validateInputs);
    connect(confirmEdit_, &QLineEdit::textChanged, this, &AdminSetupDialog::validateInputs);

    validateInputs();  // 初始 OK 按钮禁用
}

AdminSetupDialog::~AdminSetupDialog() = default;

void AdminSetupDialog::validateInputs() {
    errorLabel_->clear();
    okBtn_->setEnabled(false);

    const QString name     = nameEdit_->text().trimmed();
    const QString pw       = passwordEdit_->text();
    const QString confirm  = confirmEdit_->text();

    if (name.isEmpty()) return;
    if (pw.length() < kMinPasswordLength) return;
    if (pw != confirm) {
        errorLabel_->setText(tr("两次密码不一致"));
        return;
    }
    if (mgr_.findUser(name) != nullptr) {
        errorLabel_->setText(tr("用户名已存在"));
        return;
    }
    okBtn_->setEnabled(true);
}

void AdminSetupDialog::onAccept() {
    const QString name = nameEdit_->text().trimmed();
    const QString pw   = passwordEdit_->text();
    if (!mgr_.addUser(name, pw, Role::Admin)) {
        QMessageBox::warning(this, tr("失败"), tr("建账号失败,看日志"));
        return;
    }
    qCInfo(hmiApp) << "[AdminSetup] 已创建首个 admin:" << name;
    accept();
}

int AdminSetupDialog::exec() {
    nameEdit_->setFocus();
    return QDialog::exec();
}

} // namespace qwhmi::auth