// ============================================================================
// 阶段 A · 集成版 main.cpp
//
// 文件职责:整合所有 A 阶段子系统,定义程序启动顺序(顺序很重要)。
//
// 启动顺序(WHY THIS ORDER):
//   1. crash::install()             —— 第一行,捕获未来任何崩溃
//   2. crash::hasPendingDump()      —— 检测上次崩溃,弹对话框(用户先看)
//   3. instance::tryBecomePrimary() —— 单实例,失败就退出(已把参数传给主实例)
//   4. QCoreApplication 元数据       —— org/app 名(QSettings / QStandardPaths 用)
//   5. QApplication + 命令行解析    —— 必须有 QApplication 才能用 QCommandLineParser
//   6. --install-autostart/--remove-autostart 自启快捷命令(不进入主循环)
//   7. logging 配置加载 + registerCategories() —— 装消息处理器
//   8. 主窗口 + 托盘 + 信号连接
//   9. 根据 --minimize 决定是否显示主窗口
//  10. app.exec() 进入事件循环
//
// 关键命令行参数:
//   --minimize          启动后最小化到托盘
//   --project <path>    打开工程文件(阶段 B 接入 UserManager 后实现)
//   --install-autostart 注册 HKCU 开机自启(--minimize)并退出
//   --remove-autostart  注销开机自启并退出
//
// 关于 lambda:连接数少,lambda 比槽函数直观。window/instance/app 都在 main 栈上,
//   main() 结束才析构,lambda 捕获引用安全。
// ============================================================================

#include "hmi.h"
#include "logging/Logging.h"
#include "crash/CrashHandler.h"
#include "instance/SingleInstance.h"
#include "tray/SystemTray.h"
#include "auth/AdminSetupDialog.h"
#include "auth/LoginDialog.h"
#include "auth/UserManager.h"
#include "auth/UserStore.h"
#include "auth/CurrentUser.h"
#include "auth/InactivityWatcher.h"

#include <QApplication>
#include <QGuiApplication>
#include <QCommandLineParser>
#include <QCommandLineOption>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>
#include <windows.h>
#include <exception>  // ★ 改善清单 #21(2026-09-15):try-catch 需要 std::exception

int main(int argc, char *argv[])
{
    using namespace qwhmi;

    // 1. 崩溃处理
    crash::install();
    qCInfo(hmiApp) << "[main-early] crash::install OK";

    // 2. 检查上次是否有崩溃 dump
    if (crash::hasPendingDump()) {
        const auto info = crash::takePendingDump();
        QMessageBox box;
        box.setIcon(QMessageBox::Critical);
        box.setWindowTitle(QObject::tr("上次异常退出"));
        box.setText(QObject::tr(
            "HMI 上次运行异常退出。"
            "时间: %1"
            "异常: %2"
            "Dump: %3"
            "是否打开 dump 文件所在目录?")
            .arg(info.timestamp.isEmpty() ? QStringLiteral("(未知)") : info.timestamp,
                 info.exceptionCode.isEmpty() ? QStringLiteral("(未知)") : info.exceptionCode,
                 info.path.isEmpty() ? QStringLiteral("(未生成)") : info.path));
        box.setStandardButtons(QMessageBox::Open | QMessageBox::Ignore);
        box.setDefaultButton(QMessageBox::Ignore);
        if (box.exec() == QMessageBox::Open) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(
                QFileInfo(info.path).absolutePath()));
        }
    }

    // 3. 单实例检测
    instance::SingleInstance instance;
    qCInfo(hmiApp) << "[main-early] tryBecomePrimary start";
    if (!instance.tryBecomePrimary()) {
        QStringList args;
        for (int i = 0; i < argc; ++i) {
            args << QString::fromLocal8Bit(argv[i]);
        }
        instance.sendMessageToPrimary(args);
        qCInfo(hmiApp) << "[main-early] sender path, returning 0";
        return 0;
    }
    qCInfo(hmiApp) << "[main-early] become primary OK";

    // 4. App 元数据
    QCoreApplication::setOrganizationName(QStringLiteral("QianWei"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("qianwei.com"));
    QCoreApplication::setApplicationName(QStringLiteral("HMI"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    QApplication app(argc, argv);
    qCInfo(hmiApp) << "[main-early] QApplication created";
    QApplication::setQuitOnLastWindowClosed(false);

    // 5. 命令行参数
    QCommandLineParser parser;
    QCommandLineOption minimizeOpt(QStringLiteral("minimize"),
        QObject::tr("启动后最小化到托盘"));
    QCommandLineOption projectOpt(QStringLiteral("project"),
        QObject::tr("打开工程文件"), QStringLiteral("path"));
    QCommandLineOption installOpt(QStringLiteral("install-autostart"),
        QObject::tr("注册开机自启并退出"));
    QCommandLineOption removeOpt(QStringLiteral("remove-autostart"),
        QObject::tr("注销开机自启并退出"));
    parser.addOption(minimizeOpt);
    parser.addOption(projectOpt);
    parser.addOption(installOpt);
    parser.addOption(removeOpt);
    parser.addHelpOption();
    parser.addVersionOption();
    parser.process(app);

#ifdef Q_OS_WIN
    if (parser.isSet(installOpt)) {
        QSettings reg(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"),
                      QSettings::NativeFormat);
        reg.setValue(QStringLiteral("QianWeiHMI"),
                     QStringLiteral("\"") + QDir::toNativeSeparators(QCoreApplication::applicationFilePath())
                     + QStringLiteral("\" --minimize"));
        reg.sync();
        return 0;
    }
    if (parser.isSet(removeOpt)) {
        QSettings reg(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"),
                      QSettings::NativeFormat);
        reg.remove(QStringLiteral("QianWeiHMI"));
        return 0;
    }
#endif

    // 6. 日志配置
    const auto logCfg = logging::loadConfig();
    logging::applyConfig(logCfg);
    logging::registerCategories();

    qCInfo(hmiApp) << "===== HMI 启动 ====="
                   << "version=" << QCoreApplication::applicationVersion()
                   << "build=" << __DATE__ << __TIME__;

    // ★ B2.2(2026-09-17):UserManager 提到 main 局部(从原 B1.3 嵌套块挪出)
    //   理由:signals 自动存盘 / CurrentUser.login / InactivityWatcher 都要活到 main 退出
    auth::UserManager userMgr;

    // ★ B2.2:signals → UserStore::save 集中触发,任何增删改都自动存盘
    QObject::connect(&userMgr, &auth::UserManager::userAdded,    &app, [&userMgr]() { auth::UserStore::save(userMgr); });
    QObject::connect(&userMgr, &auth::UserManager::userRemoved,  &app, [&userMgr]() { auth::UserStore::save(userMgr); });
    QObject::connect(&userMgr, &auth::UserManager::passwordChanged, &app, [&userMgr]() { auth::UserStore::save(userMgr); });

    // ★ B1.3:加载 users.json + 首启引导
    const int loaded = auth::UserStore::load(userMgr);
    qCInfo(hmiApp) << "[main] userMgr 启动:load=" << loaded
                   << "hasUsers=" << (userMgr.hasUsers() ? "yes" : "no");
    if (!userMgr.hasUsers()) {
        auth::AdminSetupDialog dlg(userMgr);
        if (dlg.exec() != QDialog::Accepted) {
            qCWarning(hmiApp) << "[main] 首启未建 admin,拒绝启动";
            return 1;
        }
        // signals 已接 save,这里不用手动 save
    } else {
        // ★ B2.1:已有用户 → 必须登录;B2.2:登录成功 → CurrentUser.login
        auth::LoginDialog loginDlg(userMgr);
        if (loginDlg.exec() != QDialog::Accepted) {
            qCWarning(hmiApp) << "[main] 用户取消登录,拒绝启动";
            return 1;
        }
        auth::CurrentUser::instance().login(loginDlg.currentUser());
        qCInfo(hmiApp) << "[main] 登录成功:" << loginDlg.currentUser().name
                       << "role=" << roleToString(loginDlg.currentUser().role);
    }

    // ★ B2.2:自动注销监听器(install 到 qApp,5 分钟无操作 → 重新弹 LoginDialog)
    auth::InactivityWatcher inactivityWatcher;
    inactivityWatcher.installTo(&app);
    QObject::connect(&inactivityWatcher, &auth::InactivityWatcher::inactivityTimeout,
                     &app, [&userMgr, &inactivityWatcher]() {
        qCWarning(hmiApp) << "[main] 自动注销触发";
        auth::CurrentUser::instance().logout();
        inactivityWatcher.stop();
        // 重新弹 LoginDialog(in-place,不退出程序)
        auth::LoginDialog reLoginDlg(userMgr);
        if (reLoginDlg.exec() != QDialog::Accepted) {
            qCWarning(hmiApp) << "[main] 自动注销后用户拒绝登录,退出";
            QCoreApplication::exit(1);
            return;
        }
        auth::CurrentUser::instance().login(reLoginDlg.currentUser());
        qCInfo(hmiApp) << "[main] 自动注销后重新登录:" << reLoginDlg.currentUser().name;
        inactivityWatcher.start();   // 重新开始计时
    });

    // 7. 主窗口 + 托盘
    hmi window;
    tray::SystemTray trayIcon(&window);

    // 启动 inactivity 监听(必须在主窗口 show 前 install + start)
    inactivityWatcher.start();

    QObject::connect(&trayIcon, &tray::SystemTray::showRequested, &window, [&window]() {
        window.show();
        window.setWindowState(window.windowState() & ~Qt::WindowMinimized);
        window.activateWindow();
    });
    QObject::connect(&trayIcon, &tray::SystemTray::hideRequested, &window, [&window, &trayIcon]() {
        window.hide();
        // ★ IMPROVE-2026-09-09:避免用户以为程序死了
        //   窗口被 hide 后,Windows 下焦点保护会让用户看不到任何提示,误以为程序退出
        //   解决:hide 时弹一个5秒气泡,明确告诉用户'程序在托盘跑'
        trayIcon.showMessage(QObject::tr("HMI 仍在运行"),
                              QObject::tr("程序已隐藏到托盘。右键托盘图标可退出。"),
                              QSystemTrayIcon::Information, 5000);
    });
    QObject::connect(&trayIcon, &tray::SystemTray::quitRequested,
                     &app, &QCoreApplication::quit);
    QObject::connect(&instance, &instance::SingleInstance::messageReceived,
                     &window, [&window](const QStringList& args) {
        qCInfo(hmiApp) << "[main] messageReceived slot 触发,args=" << args;
#ifdef Q_OS_WIN
        // ★ Windows 焦点保护 workaround:TopMost 切换一下能强制到前台
        //   普通 activateWindow() 在隐藏窗口上会被 Windows 焦点保护挡
        ::SetWindowPos((HWND)window.winId(), HWND_TOPMOST, 0, 0, 0, 0,
                       SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
        ::SetWindowPos((HWND)window.winId(), HWND_NOTOPMOST, 0, 0, 0, 0,
                       SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
#endif
        window.show();
        window.setWindowState(window.windowState() & ~Qt::WindowMinimized);
        window.raise();
        window.activateWindow();
        qCInfo(hmiApp) << "[main] 窗口已激活,isVisible=" << window.isVisible();
    });

    if (!trayIcon.install()) {
        qCWarning(hmiApp) << "⚠ 托盘安装失败(系统不支持/图标问题)";
        qCWarning(hmiApp) << "  → 关窗口会退出程序,需要从命令行杀进程";
    } else {
        qCInfo(hmiApp) << "✓ 托盘已安装,关窗口不会退出";
    }

    if (parser.isSet(projectOpt)) {
        const QString path = parser.value(projectOpt);
        qCInfo(hmiApp) << "启动参数 --project =" << path
                       << "(打开工程功能在阶段 B 接入)";
    }

    if (!parser.isSet(minimizeOpt)) {
        window.show();
    } else {
        qCInfo(hmiApp) << "以最小化模式启动";
    }

    // 安全网:即使 setQuitOnLastWindowClosed(false) 失效,也别退出
    QObject::connect(&app, &QGuiApplication::lastWindowClosed,
                     &app, []() {
        qCInfo(hmiApp) << "[诊断] lastWindowClosed 信号触发,但因为 setQuitOnLastWindowClosed(false) 应该不退出";
    });

    qCInfo(hmiApp) << "进入事件循环";
    // ★ 改善清单 #21(2026-09-15):app.exec() 包 try-catch
    //   【背景】crash::install() 兜 SEH(C 结构异常/空指针段错),但 C++ 异常
    //          (std::bad_alloc / std::system_error 等)会穿透到 main → 默认 std::terminate
    //   【改法】catch 弹 QMessageBox + 返回非 0,免得直接闪退用户看不到任何信息
    //   【参照】docs/改善清单.md #21
    try {
        return app.exec();
    } catch (const std::exception& e) {
        qCCritical(hmiApp) << "未捕获 C++ 异常:" << e.what();
        QMessageBox::critical(nullptr, QObject::tr("HMI 异常"),
            QObject::tr("程序异常退出:\n%1").arg(e.what()));
        return 1;
    } catch (...) {
        qCCritical(hmiApp) << "未捕获未知异常";
        QMessageBox::critical(nullptr, QObject::tr("HMI 异常"),
            QObject::tr("程序异常退出(未知错误)"));
        return 2;
    }
}
