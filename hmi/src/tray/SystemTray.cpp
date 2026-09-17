// ============================================================================
// 阶段 A · A2 系统托盘 · 实现
//
// 文件职责:创建系统托盘图标 + 3 项菜单(显示/隐藏/退出)+ 信号连接。
//
// 行为细节:
//   - 单击图标(r == Trigger):窗口可见则隐藏,否则显示
//   - 右键菜单:3 项 + 分隔线
//   - QApplication::setQuitOnLastWindowClosed(false) 让关窗口不退到托盘
//     后退出只能走托盘菜单的"退出"
//
// 详见:SystemTray.h 和 docs/阶段A_工程化基线.md
// ============================================================================

#include "tray/SystemTray.h"

#include <QAction>
#include <QApplication>
#include <QSystemTrayIcon>
#include <QIcon>
#include <QMenu>
#include <QStyle>
#include <QWidget>

namespace qwhmi::tray {

SystemTray::SystemTray(QWidget* mainWindow, QObject* parent)
    : QObject(parent), mainWindow_(mainWindow) {}

SystemTray::~SystemTray() {
    if (icon_) {
        icon_->hide();
        delete icon_;
    }
    if (menu_) delete menu_;
}

bool SystemTray::install() {
    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        return false;
    }
    // ★ 修复:用 fallback 图标,避免 QApplication::windowIcon() 为空导致托盘不可见
    QIcon trayIcon = QApplication::windowIcon();
    if (trayIcon.isNull()) {
        trayIcon = QApplication::style()->standardIcon(QStyle::SP_ComputerIcon);
    }
    icon_ = new QSystemTrayIcon(trayIcon, this);
    icon_->setToolTip(QApplication::applicationName()
                      + QStringLiteral(" ")
                      + QApplication::applicationVersion());

    menu_ = new QMenu();
    auto* showAct = menu_->addAction(tr("显示主窗口"));
    auto* hideAct = menu_->addAction(tr("隐藏主窗口"));
    menu_->addSeparator();
    // ★ B2.3:切换用户 + 修改密码
    auto* logoutAct = menu_->addAction(tr("切换用户"));
    auto* changePwAct = menu_->addAction(tr("修改密码"));
    menu_->addSeparator();
    auto* quitAct = menu_->addAction(tr("退出"));

    connect(showAct, &QAction::triggered, this, &SystemTray::showRequested);
    connect(hideAct, &QAction::triggered, this, &SystemTray::hideRequested);
    connect(logoutAct, &QAction::triggered, this, &SystemTray::logoutRequested);
    connect(changePwAct, &QAction::triggered, this, &SystemTray::changePasswordRequested);
    connect(quitAct, &QAction::triggered, this, &SystemTray::quitRequested);

    icon_->setContextMenu(menu_);
    connect(icon_, &QSystemTrayIcon::activated,
            this, &SystemTray::onActivated);
    icon_->show();

    // ★ 启动通知:用户能立刻看到托盘图标在工作
    icon_->showMessage(QObject::tr("HMI 已启动"),
                             QObject::tr("程序在托盘运行。右键图标可退出。"),
                             QSystemTrayIcon::Information, 5000);

    return true;
}

void SystemTray::showMessage(const QString& title, const QString& body,
                                  QSystemTrayIcon::MessageIcon icon, int timeoutMs) {
    if (!icon_) return;
    icon_->showMessage(title, body, icon, timeoutMs);
}

bool SystemTray::isVisible() const {
    return icon_ && icon_->isVisible();
}

void SystemTray::onActivated(QSystemTrayIcon::ActivationReason r) {
    if (r == QSystemTrayIcon::Trigger) {
        if (mainWindow_ && mainWindow_->isVisible()) {
            emit hideRequested();
        } else {
            emit showRequested();
        }
    }
}

} // namespace qwhmi::tray
