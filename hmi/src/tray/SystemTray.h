// ============================================================================
// 阶段 A · A2 系统托盘 · 头文件
//
// 文件职责:封装 QSystemTrayIcon + 右键菜单 + 通知气泡。
//
// 为什么需要:HMI 长期运行时不应该霸占任务栏。托盘让用户随时唤起/隐藏/退出。
//
// 在程序中的位置:
//   - main.cpp:trayIcon(&window);  拿主窗口指针(显隐切换时用)
//   - 信号 → 主窗口槽:
//       showRequested → window.show()
//       hideRequested → window.hide()
//       quitRequested → app.quit()
//
// 关键 API:install() / showMessage(title, body, icon, timeoutMs)
// 信号:showRequested / hideRequested / quitRequested
// 注意:install() 失败(系统不支持托盘,如某些 Linux 桌面)返回 false
// 详见:docs/阶段A_工程化基线.md "A2 系统托盘" 节
// ============================================================================

#pragma once

#include <QObject>
#include <QSystemTrayIcon>

class QWidget;
class QMenu;

namespace qwhmi::tray {

class SystemTray : public QObject {
    Q_OBJECT
public:
    explicit SystemTray(QWidget* mainWindow, QObject* parent = nullptr);
    ~SystemTray() override;

    bool install();
    bool isVisible() const;

    void showMessage(const QString& title,
                          const QString& body,
                          QSystemTrayIcon::MessageIcon icon = QSystemTrayIcon::Information,
                          int timeoutMs = 3000);

signals:
    void showRequested();
    void hideRequested();
    void logoutRequested();       // ★ B2.3:切换用户(logout + 重新弹 LoginDialog)
    void changePasswordRequested();// ★ B2.3:弹修改密码对话框
    void quitRequested();

private slots:
    void onActivated(QSystemTrayIcon::ActivationReason r);

private:
    QWidget*        mainWindow_ = nullptr;
    QSystemTrayIcon* icon_      = nullptr;
    QMenu*          menu_       = nullptr;
};

} // namespace qwhmi::tray
