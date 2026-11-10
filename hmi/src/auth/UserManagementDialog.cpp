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
    enableBtn_  = new QPushButton(tr("启用选中"), this);   // ★ 2026-11-10 (G-30)
    refreshBtn_ = new QPushButton(tr("刷新"), this);
    closeBtn_   = new QPushButton(tr("关闭"), this);

    setupTable();
    setupButtons();

    auto* btnRow = new QHBoxLayout;
    btnRow->addWidget(infoLabel_);
    btnRow->addStretch();
    btnRow->addWidget(addBtn_);
    btnRow->addWidget(deleteBtn_);
    btnRow->addWidget(enableBtn_);
    btnRow->addWidget(refreshBtn_);
    btnRow->addWidget(closeBtn_);

    auto* root = new QVBoxLayout(this);
    root->addWidget(table_);
    root->addLayout(btnRow);

    connect(addBtn_,     &QPushButton::clicked, this, &UserManagementDialog::onAddUser);
    connect(deleteBtn_,  &QPushButton::clicked, this, &UserManagementDialog::onDeleteUser);
    connect(enableBtn_,  &QPushButton::clicked, this, &UserManagementDialog::onEnableUser);
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
    // ★ 2026-11-10 (G-30):禁用是【可逆】的了 —— 原来只能禁不能启,
    //   而确认框还写着"(可后续用密码恢复)"(密码根本救不回来)。
    enableBtn_->setToolTip(tr("重新启用被禁用的用户(disabled=false)"));
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
    const QString actor = CurrentUser::instance().name();

    // ★ 2026-11-10 (G-30):判据搬到公共层(UserManager::canDisableUser)。
    //   原来这里自己判,而且是拿【显示文本】比角色列的"管理员"和禁用列的"是"——
    //   换个语言/改个显示名就失效;绕过这个对话框的调用更是一路畅通。
    //   现在这里只负责把 reason 弹出来。
    QString reason;
    if (!mgr_.canDisableUser(name, actor, &reason)) {
        QMessageBox::warning(this, tr("不能禁用"), reason);
        return;
    }

    // ★ 2026-11-10 (G-30):这句话以前是假的 —— "(可后续用密码恢复)"。
    //   密码根本救不回来:authenticate() 对 disabled 用户一律拒绝,
    //   而"启用"入口那时候还不存在 ⇒ 账号被禁就等于报废。
    //   现在有【启用选中】了,这句话才是真的。
    if (QMessageBox::question(this, tr("确认禁用"),
                                 tr("确定禁用用户 '%1' 吗?\n(禁用后此账号无法登录;需要恢复请用【启用选中】)").arg(name))
        != QMessageBox::Yes) {
        return;
    }
    if (!mgr_.removeUser(name, actor)) {
        QMessageBox::warning(this, tr("失败"), tr("禁用失败,看日志"));
        return;
    }
    refreshTable();
}

// ★ 2026-11-10 (G-30):启用 —— removeUser 的逆操作。
//   原来"禁用"是单向的:被禁用的账号永远回不来(没有启用按钮,
//   UserManager 也没有对应的 API),而确认框还在骗人说"可后续用密码恢复"。
void UserManagementDialog::onEnableUser() {
    const int row = table_->currentRow();
    if (row < 0) {
        QMessageBox::information(this, tr("未选择"), tr("请先在表格里选中一行"));
        return;
    }
    const QString name = table_->item(row, kColName)->text();
    const User* u = mgr_.findUser(name);
    if (u == nullptr) {
        QMessageBox::warning(this, tr("失败"), tr("用户不存在,点【刷新】再试"));
        return;
    }
    if (!u->disabled) {
        QMessageBox::information(this, tr("已经是启用状态"),
                                 tr("用户 '%1' 本来就是启用的").arg(name));
        return;
    }
    if (QMessageBox::question(this, tr("确认启用"),
                                 tr("确定重新启用用户 '%1' 吗?").arg(name))
        != QMessageBox::Yes) {
        return;
    }
    if (!mgr_.enableUser(name)) {
        QMessageBox::warning(this, tr("失败"), tr("启用失败,看日志"));
        return;
    }
    refreshTable();
}

} // namespace qwhmi::auth