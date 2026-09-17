// ============================================================================
// 阶段 B · B2.4 · UserManagementDialog 实现
//
// 关联:UserManagementDialog.h
// ============================================================================

#include "auth/UserManagementDialog.h"
#include "auth/CurrentUser.h"
#include "auth/NewUserDialog.h"
#include "auth/UserManager.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include "logging/Logging.h"

namespace qwhmi::auth {

namespace {
constexpr int kColName     = 0;
constexpr int kColRole     = 1;
constexpr int kColDisabled = 2;
constexpr int kColCreated  = 3;
}

UserManagementDialog::UserManagementDialog(UserManager& mgr, QWidget* parent)
    : QDialog(parent), mgr_(mgr) {
    setWindowTitle(tr("用户管理(仅 Admin)"));
    setModal(true);
    resize(640, 360);

    table_      = new QTableWidget(this);
    infoLabel_  = new QLabel(this);
    addBtn_     = new QPushButton(tr("新增用户"), this);
    deleteBtn_  = new QPushButton(tr("禁用选中"), this);
    refreshBtn_ = new QPushButton(tr("刷新"), this);
    closeBtn_   = new QPushButton(tr("关闭"), this);

    setupTable();
    setupButtons();

    auto* btnRow = new QHBoxLayout;
    btnRow->addWidget(infoLabel_);
    btnRow->addStretch();
    btnRow->addWidget(addBtn_);
    btnRow->addWidget(deleteBtn_);
    btnRow->addWidget(refreshBtn_);
    btnRow->addWidget(closeBtn_);

    auto* root = new QVBoxLayout(this);
    root->addWidget(table_);
    root->addLayout(btnRow);

    connect(addBtn_,     &QPushButton::clicked, this, &UserManagementDialog::onAddUser);
    connect(deleteBtn_,  &QPushButton::clicked, this, &UserManagementDialog::onDeleteUser);
    connect(refreshBtn_, &QPushButton::clicked, this, &UserManagementDialog::refreshTable);
    connect(closeBtn_,   &QPushButton::clicked, this, &QDialog::accept);

    refreshTable();
}

UserManagementDialog::~UserManagementDialog() = default;

void UserManagementDialog::setupTable() {
    table_->setColumnCount(4);
    table_->setHorizontalHeaderLabels(
        QStringList() << tr("用户名") << tr("角色") << tr("已禁用") << tr("创建时间"));
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
}

void UserManagementDialog::setupButtons() {
    deleteBtn_->setToolTip(tr("禁用(disabled=true),不删除记录(审计用)"));
}

void UserManagementDialog::refreshTable() {
    const auto users = mgr_.listUsers();   // 已按名字排序
    table_->setRowCount(users.size());
    int row = 0;
    int enabledCount = 0;
    for (const User& u : users) {
        table_->setItem(row, kColName,     new QTableWidgetItem(u.name));
        table_->setItem(row, kColRole,     new QTableWidgetItem(roleDisplayName(u.role)));
        table_->setItem(row, kColDisabled, new QTableWidgetItem(u.disabled ? tr("是") : QString()));
        table_->setItem(row, kColCreated,  new QTableWidgetItem(u.createdAt));
        if (!u.disabled) ++enabledCount;
        ++row;
    }
    infoLabel_->setText(tr("共 %1 个用户(%2 启用)").arg(users.size()).arg(enabledCount));
}

int UserManagementDialog::exec() {
    if (CurrentUser::instance().role() != Role::Admin) {
        QMessageBox::warning(this, tr("权限不足"), tr("只有 Admin 才能管理用户"));
        return QDialog::Rejected;
    }
    return QDialog::exec();
}

void UserManagementDialog::onAddUser() {
    NewUserDialog dlg(mgr_, this);
    if (dlg.exec() != QDialog::Accepted) return;
    // mgr.addUser 已经在 NewUserDialog 里调了,这里只需刷新
    refreshTable();
}

void UserManagementDialog::onDeleteUser() {
    const int row = table_->currentRow();
    if (row < 0) {
        QMessageBox::information(this, tr("未选择"), tr("请先在表格里选中一行"));
        return;
    }
    const QString name = table_->item(row, kColName)->text();
    const QString currentUser = CurrentUser::instance().name();

    // 防止 1) Admin 删自己 2) 删最后一个 enabled admin
    if (name == currentUser) {
        QMessageBox::warning(this, tr("不能删自己"), tr("不能禁用当前登录的用户"));
        return;
    }
    if (table_->item(row, kColRole)->text() == roleDisplayName(Role::Admin)
        && table_->item(row, kColDisabled)->text() != tr("是")) {
        // 找所有 enabled admin 数量
        int enabledAdmins = 0;
        for (const User& u : mgr_.listUsers()) {
            if (u.role == Role::Admin && !u.disabled && u.name != name) ++enabledAdmins;
        }
        if (enabledAdmins == 0) {
            QMessageBox::warning(this, tr("最后一个 Admin"),
                                  tr("这是最后一个 enabled Admin,禁用后没人能登录"));
            return;
        }
    }

    if (QMessageBox::question(this, tr("确认禁用"),
                                 tr("确定禁用用户 '%1' 吗?\n(可后续用密码恢复)").arg(name))
        != QMessageBox::Yes) {
        return;
    }
    if (!mgr_.removeUser(name)) {
        QMessageBox::warning(this, tr("失败"), tr("禁用失败,看日志"));
        return;
    }
    refreshTable();
}

} // namespace qwhmi::auth