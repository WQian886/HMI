// ============================================================================
// 阶段 B · B2.4 · NewUserDialog 实现
//
// 关联:NewUserDialog.h
// ============================================================================

#include "auth/NewUserDialog.h"
#include "auth/UserManager.h"

#include <QComboBox>
#include <QFormLayout>
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

NewUserDialog::NewUserDialog(UserManager& mgr, QWidget* parent)
    : QDialog(parent), mgr_(mgr) {
    setWindowTitle(tr("新增用户"));
    setModal(true);
    setMinimumWidth(320);

    auto* nameLabel = new QLabel(tr("用户名:"), this);
    auto* pwLabel   = new QLabel(tr("密码(≥ 6 字符):"), this);
    auto* roleLabel = new QLabel(tr("角色:"), this);
    nameEdit_     = new QLineEdit(this);
    passwordEdit_ = new QLineEdit(this);
    passwordEdit_->setEchoMode(QLineEdit::Password);
    roleCombo_    = new QComboBox(this);
    roleCombo_->addItem(roleDisplayName(Role::Admin),    QVariant::fromValue(Role::Admin));
    roleCombo_->addItem(roleDisplayName(Role::Engineer), QVariant::fromValue(Role::Engineer));
    roleCombo_->addItem(roleDisplayName(Role::Operator), QVariant::fromValue(Role::Operator));
    roleCombo_->addItem(roleDisplayName(Role::Viewer),   QVariant::fromValue(Role::Viewer));
    roleCombo_->setCurrentIndex(2);   // 默认 Operator(商业常见)
    errorLabel_ = new QLabel(this);
    errorLabel_->setStyleSheet("color: red;");
    errorLabel_->setWordWrap(true);
    errorLabel_->setMinimumHeight(20);

    okBtn_       = new QPushButton(tr("创建"), this);
    auto* cancelBtn = new QPushButton(tr("取消"), this);

    auto* grid = new QGridLayout;
    grid->addWidget(nameLabel, 0, 0);
    grid->addWidget(nameEdit_, 0, 1);
    grid->addWidget(pwLabel,   1, 0);
    grid->addWidget(passwordEdit_, 1, 1);
    grid->addWidget(roleLabel, 2, 0);
    grid->addWidget(roleCombo_, 2, 1);

    auto* btnRow = new QHBoxLayout;
    btnRow->addStretch();
    btnRow->addWidget(cancelBtn);
    btnRow->addWidget(okBtn_);

    auto* root = new QVBoxLayout(this);
    root->addLayout(grid);
    root->addWidget(errorLabel_);
    root->addLayout(btnRow);

    okBtn_->setDefault(true);

    connect(okBtn_, &QPushButton::clicked, this, &NewUserDialog::onAccept);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(nameEdit_, &QLineEdit::textChanged, this, &NewUserDialog::validateInputs);
    connect(passwordEdit_, &QLineEdit::textChanged, this, &NewUserDialog::validateInputs);

    validateInputs();
}

NewUserDialog::~NewUserDialog() = default;

QString NewUserDialog::newName() const     { return acceptedName_; }
QString NewUserDialog::newPassword() const { return acceptedPassword_; }
Role    NewUserDialog::newRole() const     { return acceptedRole_; }

void NewUserDialog::validateInputs() {
    errorLabel_->clear();
    okBtn_->setEnabled(false);
    const QString name = nameEdit_->text().trimmed();
    if (name.isEmpty()) return;
    if (mgr_.findUser(name) != nullptr) {
        errorLabel_->setText(tr("用户名已存在"));
        return;
    }
    const QString pw = passwordEdit_->text();
    if (pw.length() < kMinPasswordLength) return;
    okBtn_->setEnabled(true);
}

void NewUserDialog::onAccept() {
    const QString name = nameEdit_->text().trimmed();
    const QString pw   = passwordEdit_->text();
    const Role    role = roleCombo_->currentData().value<Role>();
    if (!mgr_.addUser(name, pw, role)) {
        errorLabel_->setText(tr("创建失败,看日志"));
        return;
    }
    acceptedName_     = name;
    acceptedPassword_ = pw;
    acceptedRole_     = role;
    qCInfo(hmiApp) << "[NewUser] 已创建用户:" << name << "role=" << roleToString(role);
    accept();
}

} // namespace qwhmi::auth