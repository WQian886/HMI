// ============================================================================
// 阶段 A · 集成版 main.cpp
//
// 文件职责:整合所有 A 阶段子系统,定义程序启动顺序(顺序很重要)。
//
// 启动顺序(WHY THIS ORDER):
//   1. QCoreApplication 元数据        —— org/app 名
//   2. crash::install()               —— 尽早捕获未来任何崩溃
//   3. instance::tryBecomePrimary()   —— 单实例,失败就退出(已把参数传给主实例)
//   4. QApplication + 命令行解析      —— 必须有 QApplication 才能用 QCommandLineParser
//   5. --install-autostart/--remove-autostart 自启快捷命令(不进入主循环)
//   6. logging 配置加载 + registerCategories() —— 装消息处理器
//   7. crash::hasPendingDump() → 弹"上次异常退出"对话框
//   8. 主窗口 + 托盘 + 信号连接
//   9. 根据 --minimize 决定是否显示主窗口
//  10. app.exec() 进入事件循环
//
// ★ 2026-10-14 调整了两处,都是被实测逼出来的,别改回去:
//
//   (a) 元数据从第 4 步提到【第 1 步】
//       原本 dump 目录靠 QStandardPaths::AppLocalDataLocation 算,含 <org>/<app>。
//       结果:崩溃瞬间(org/app 已设)写到 QianWei/HMI/crash/,而启动早期
//       (还没设)去 AppData/Local/crash/ 找 —— 写一个目录、读另一个目录。
//       pending.txt 因此静默失效 28 天,全程零报错。
//       现在 CrashHandler::dumpDirectory() 本身已是纯函数(见 CrashHandler.h),
//       顺序不再决定路径;这里提前设元数据,是为了让日志/历史库的路径
//       从一开始就正确,并顺手废掉 2026-09-08 就建起来的那个空壳目录。
//
//   (b) "上次异常退出"弹框从第 2 步挪到第 7 步
//       QMessageBox 是 QWidget,在 QApplication 之前构造会直接 abort ——
//       旧顺序下就算路径对了,弹框本身也会把进程打死。
//       挪到 SingleInstance 之后还有第二个好处:第二实例不该把
//       主实例该报的崩溃信息吃掉。
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
#include "util/Paths.h"
#include "tray/SystemTray.h"
#include "auth/AdminSetupDialog.h"
#include "auth/LoginDialog.h"
#include "auth/ChangePasswordDialog.h"
#include "auth/UserManagementDialog.h"
#include "auth/UserManager.h"
#include "auth/UserStore.h"
#include "auth/CurrentUser.h"
#include "auth/InactivityWatcher.h"
#include "auth/UIPermission.h"  // ★ B4(2026-09-17):托盘"用户管理"绑 ManageUsers
#include "auth/AuditLog.h"      // ★ B5(2026-09-17):操作审计日志

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

    // 1. App 元数据 —— 【刻意放在最前面】
    //    日志、历史库、以及崩溃 dump 的目录都由 org/app 推导。
    //    放在后面的话,崩溃处理看到的是另一个目录(2026-10-14 修的就是这个)。
    //    这几个 setter 是静态的,不需要 QCoreApplication 实例,放最前面没有代价。
    //    名字取自 util::kOrgName / kAppName —— 全项目唯一出处,不许各写各的。
    //    ★ 2026-10-21:从 crash/ 搬到 util/Paths.h —— 数据目录也要用这两个值。
    QCoreApplication::setOrganizationName(QString::fromLatin1(util::kOrgName));
    QCoreApplication::setOrganizationDomain(QStringLiteral("qianwei.com"));
    QCoreApplication::setApplicationName(QString::fromLatin1(util::kAppName));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    // 2. 崩溃处理
    crash::install();
    qCInfo(hmiApp) << "[main-early] crash::install OK";

    // 3. 单实例检测
    //   ★ 2026-10-21:实例键改成从环境变量 HMI_INSTANCE_KEY 取(见 SingleInstance.h)。
    //     动机:长跑时机器上可能已经有另一个 hmi.exe 在跑,单实例锁用固定键,
    //     第二个实例一启动就退出 —— 长跑脚本只能"拒绝开跑"。
    //     键可换之后,长跑实例和已有实例各走各的,谁也不动谁。
    //     不给这个变量 = 行为和以前一模一样。
    instance::SingleInstance instance;
    const QString instanceKey = instance::SingleInstance::keyFromEnvironment();
    qCInfo(hmiApp) << "[main-early] tryBecomePrimary start, key=" << instanceKey;
    if (!instance.tryBecomePrimary(instanceKey)) {
        QStringList args;
        for (int i = 0; i < argc; ++i) {
            args << QString::fromLocal8Bit(argv[i]);
        }
        instance.sendMessageToPrimary(args);
        qCInfo(hmiApp) << "[main-early] sender path, returning 0";
        return 0;
    }
    qCInfo(hmiApp) << "[main-early] become primary OK";

    QApplication app(argc, argv);
    qCInfo(hmiApp) << "[main-early] QApplication created";
    // ★ D.1 fix (2026-09-18):显式加 Qt plugins 路径,避免 SQLite driver 找不到(开发环境)
    QCoreApplication::addLibraryPath(QStringLiteral("D:/Qt/6.8.3/msvc2022_64/plugins"));
    qInfo() << "[main] Qt plugins path:" << QCoreApplication::libraryPaths();
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
    // ★ 2026-10-14 新增:长跑验收专用旁路(24h soak)。
    //   【为什么需要】见下面 :265 那段 InactivityWatcher 的注释 ——
    //   24h 长跑卡在两道人工操作上,这个开关把它们一次性拆掉。
    //   【安全边界】它只是"跳过交互",不是"跳过认证":
    //   仍然要求 users.json 里【已经存在】用户,并调用同一个
    //   UserManager::authenticate 去核密码 —— 密码从命令行给,
    //   见 :252 那段的处理。没有任何一条路径能凭空造出一个用户。
    QCommandLineOption soakOpt(QStringLiteral("soak"),
        QObject::tr("长跑验收模式:用已存在用户自动登录并关闭自动注销(见 scripts/long_run.ps1)"));
    QCommandLineOption soakUserOpt(QStringLiteral("soak-user"),
        QObject::tr("配合 --soak 使用的用户名"), QStringLiteral("name"));
    QCommandLineOption soakPassOpt(QStringLiteral("soak-pass"),
        QObject::tr("配合 --soak 使用的密码"), QStringLiteral("pwd"));
    parser.addOption(minimizeOpt);
    parser.addOption(projectOpt);
    parser.addOption(installOpt);
    parser.addOption(removeOpt);
    parser.addOption(soakOpt);
    parser.addOption(soakUserOpt);
    parser.addOption(soakPassOpt);
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

    // ★★ 2026-10-20:加载深色工业风样式表。
    //
    //   【位置是被实测逼出来的,挪过一次】
    //   初版把它放在 QApplication 之后、日志配置之前。功能是对的,
    //   但那句 qCInfo/qCWarning 【永远进不了日志文件】——
    //   因为 qInstallMessageHandler 是在 applyConfig() 里装的,
    //   那时还没装,消息只走 stderr。
    //   而"主题没加载上"这件事恰恰是【最需要留痕】的:
    //   它不报错、不崩,界面默默退回灰白皮,几个月后没人想得起来为什么。
    //   一个记不进日志的告警等于没有告警。
    //   所以现在挪到 applyConfig/registerCategories 之后。
    //
    //   【仍然必须早于任何控件 —— 这条没变】
    //   qss 是【构造时】套上去的快照,已经建好的控件不会被后来的
    //   setStyleSheet 追改。放到登录框之后就变成
    //   "登录框深色、主窗口浅色"的分裂界面。
    //   查过这两点之间没有任何控件构造(QMessageBox 最早出现在
    //   下面的崩溃检查处,单实例锁也不建控件),所以这个位置同时满足
    //   两个要求:日志已就绪,且控件一个都还没建。
    //
    //   【为什么走 qrc 而不是相对路径读盘】
    //   ":/hmi/theme/dark_industrial.qss" 走的是编译进 exe 的资源:
    //     · 不受工作目录影响(Qt 里"换个目录就读不到"是最难查的一类 bug)
    //     · 打包/换机器不用记得把 qss 一起拷
    //     · 和 hmi.vcxproj 那条构建轨读到的是同一份文件
    {
        QFile qss(QStringLiteral(":/hmi/theme/dark_industrial.qss"));
        if (qss.open(QIODevice::ReadOnly | QIODevice::Text)) {
            const QString sheet = QString::fromUtf8(qss.readAll());
            qss.close();
            app.setStyleSheet(sheet);
            qCInfo(hmiApp) << "[theme] 深色工业风已加载,字符数=" << sheet.size();
        } else {
            // ★ 不设默认样式表就悄悄跑 —— 那就是"界面丑但没人知道为什么"。
            qCWarning(hmiApp)
                << "[theme] ★ 样式表加载失败,界面会退回 Qt 默认皮肤。"
                   "检查 hmi.qrc 里有没有 theme/dark_industrial.qss,"
                   "以及编译时有没有带上 hmi.qrc。";
        }
    }

    // 7. 检查上次是否有崩溃 dump
    //    ★ 位置是挪过来的,两处都是实测逼出来的:
    //      · 必须在 QApplication 之后 —— QMessageBox 是 QWidget,
    //        没有 QApplication 实例就构造会直接 abort。旧顺序下就算
    //        路径对了,弹框自己也会把进程打死。
    //      · 必须在 SingleInstance 之后 —— 否则第二个实例启动时会把
    //        主实例该报的崩溃信息吃掉,用户永远看不到。
    //    放在日志就绪之后,用户点"打开目录"万一出问题也有日志可查。
    if (crash::hasPendingDump()) {
        const auto info = crash::takePendingDump();
        qCWarning(hmiApp) << "[main] 发现上次崩溃 dump:"
                          << info.timestamp << info.exceptionCode << info.path;
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

    // ★ B2.2(2026-09-17):UserManager 提到 main 局部(从原 B1.3 嵌套块挪出)
    //   理由:signals 自动存盘 / CurrentUser.login / InactivityWatcher 都要活到 main 退出
    auth::UserManager userMgr;

    // ★ B5(2026-09-17):UserManager signals → UserStore::save + AuditLog 集中触发
    //   save:已有,自动持久化 users.json
    //   audit:新增,把"谁在什么时间做了什么"留痕(给事故复盘 + 合规审计)
    QObject::connect(&userMgr, &auth::UserManager::userAdded, &app,
        [&userMgr](const QString& name) {
            auth::UserStore::save(userMgr);
            const auto* u = userMgr.findUser(name);
            const QString role = u ? roleToString(u->role) : QStringLiteral("unknown");
            auto& audit = auth::AuditLog::instance();
            // 未登录 = 首启 AdminSetupDialog 触发(系统事件),已登录 = 用户管理增删(操作事件)
            if (auth::CurrentUser::instance().isLoggedIn()) {
                audit.appendCurrent("user.add", name, QStringLiteral("role=%1").arg(role));
            } else {
                audit.append("user.add", "system", "system", name, QStringLiteral("role=%1").arg(role));
            }
        });
    QObject::connect(&userMgr, &auth::UserManager::userRemoved, &app,
        [&userMgr](const QString& name) {
            auth::UserStore::save(userMgr);
            auth::AuditLog::instance().appendCurrent("user.remove", name);
        });
    QObject::connect(&userMgr, &auth::UserManager::passwordChanged, &app,
        [&userMgr](const QString& name) {
            auth::UserStore::save(userMgr);
            auth::AuditLog::instance().appendCurrent("user.passwordChange", name);
        });
    // ★ B5:认证失败(独立 connect,因为 authenticationFailed 不需要 save)
    QObject::connect(&userMgr, &auth::UserManager::authenticationFailed, &app,
        [](const QString& name) {
            auth::AuditLog::instance().append("auth.fail", "anonymous", "anonymous",
                                             name, {}, QStringLiteral("fail"));
        });

    // ★ B5:CurrentUser login/logout 全局审计(覆盖首启 / 托盘切用户 / 自动注销 3 条路径)
    QObject::connect(&auth::CurrentUser::instance(), &auth::CurrentUser::userLoggedIn, &app,
        [](const QString& name) {
            auth::AuditLog::instance().append("auth.login", name,
                roleToString(auth::CurrentUser::instance().role()));
        });
    QObject::connect(&auth::CurrentUser::instance(), &auth::CurrentUser::userLoggedOut, &app,
        []() {
            // 注销时 name/role 已清,传 system 占位(actor 是"上次登录的用户"语义模糊,不写)
            auth::AuditLog::instance().append("auth.logout", "system", "system");
        });

    // ★ B1.3:加载 users.json + 首启引导
    //   soakMode 在这里先定下来:下面登录分支和自动注销两处都要读它。
    const bool soakMode = parser.isSet(soakOpt);
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
        //
        // ★ 2026-10-14 新增 --soak 分支(24h 长跑验收专用)。
        //   【为什么】正常路径的 LoginDialog::exec() 是模态阻塞的,
        //   长跑脚本没法点它 —— 前三次 soak 因此全判 UNKNOWN。
        //   而 InactivityWatcher 5 分钟无操作还会再弹一次,
        //   就算人工登录了,24h 里也要人工点 288 次。
        //
        //   【安全边界 —— 这段是重点,别当成后门】
        //   1. 必须【已经存在用户】才走这条路:这个分支在 else 里,
        //      users.json 为空时上面 :245 的 AdminSetupDialog 分支照旧,
        //      --soak 不会凭空造出一个用户。
        //   2. 仍然调 UserManager::authenticate 核密码 —— 密码错直接退出,
        //      认证逻辑跟人工登录是【同一段代码】,没有旁路。
        //   3. 走的是同一个 CurrentUser::login,权限体系照常生效:
        //      Viewer 登录后 start/stop 照样是灰的。
        //   唯一被跳过的只有"那个 QDialog"本身。
        if (soakMode) {
            const QString soakName = parser.value(soakUserOpt);
            const QString soakPass = parser.value(soakPassOpt);
            if (soakName.isEmpty() || soakPass.isEmpty()) {
                qCCritical(hmiApp) << "[main] --soak 必须同时给 --soak-user 和 --soak-pass";
                return 1;
            }
            const auth::User* soakUser = nullptr;
            if (userMgr.authenticate(soakName, soakPass)) {
                // authenticate 成功后 findUser 必然能查到(UserManager 内部
                // 就是拿这个名字查的同一张表)。查不到说明两者实现分叉了,
                // 那宁可退出也不能登录一个空用户 —— 所以这里判空而不是解引用。
                soakUser = userMgr.findUser(soakName);
            }
            if (soakUser == nullptr) {
                // ★ 这里绝不能降级成"随便挑一个用户登录"——
                //   那才是真后门。认证失败就是失败,退出。
                //   authenticate 返 bool 是刻意设计(防用户名枚举,
                //   UserManager.h:47-48),所以这里不能靠它的返回值区分
                //   "用户不存在"和"密码错",统一报"认证失败"。
                qCCritical(hmiApp) << "[main] --soak 认证失败:" << soakName << " 拒绝启动";
                return 1;
            }
            auth::CurrentUser::instance().login(*soakUser);
            qCInfo(hmiApp) << "[main] --soak 自动登录成功:" << soakUser->name
                           << "role=" << roleToString(soakUser->role);
        } else {
            auth::LoginDialog loginDlg(userMgr);
            if (loginDlg.exec() != QDialog::Accepted) {
                qCWarning(hmiApp) << "[main] 用户取消登录,拒绝启动";
                return 1;
            }
            auth::CurrentUser::instance().login(loginDlg.currentUser());
            qCInfo(hmiApp) << "[main] 登录成功:" << loginDlg.currentUser().name
                           << "role=" << roleToString(loginDlg.currentUser().role);
        }
    }

    // ★ B2.2:自动注销监听器(install 到 qApp,5 分钟无操作 → 重新弹 LoginDialog)
    //
    // ★ 2026-10-14:24h 长跑必须关掉它。这不是"优化",是【长跑能不能跑完】
    //   的决定性条件 —— 长跑期间没人碰鼠标键盘,5 分钟后这里必然触发,
    //   然后弹一个模态 LoginDialog 把主流程重新卡住,轮询/报警全停。
    //   表现和"程序崩了"几乎一样,但它会周期性地反复发生 ——
    //   资源曲线会呈现"冻结-恢复-冻结"的锯齿,而根因是人机界面,
    //   不是被测代码。前三次 soak 全 UNKNOWN 卡在登录框,这一条是第二道门。
    //   只在 --soak 下关;正常运行的自动注销是安全特性,一个字都不改。
    auth::InactivityWatcher inactivityWatcher;
    if (soakMode) {
        // ★ 注意这里【只跳过 installTo + start,不动 connect 本身】——
        //   留着那条 connect 是有意的:它捕获的 &inactivityWatcher 是个栈对象,
        //   它的生命周期到 main 结束,连接不会悬空。真正的开关在下面。
        qCInfo(hmiApp) << "[main] --soak 模式:自动注销保持关闭(长跑期间无人工操作)";
    }
    if (!soakMode) {
        inactivityWatcher.installTo(&app);
    }
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
    // ★ 2026-10-21:--soak 模式禁止弹模态对话框
    //   【必须在构造窗口【之前】设】
    //     构造 hmi 时它自己会 loadFromFile(default.json);
    //     那一步失败会 emit loadError → 弹 QMessageBox →
    //     长跑脚本没有人去点它 → 程序【永久卡在那个框上】。
    //     从外面看:进程活着、日志停长、内存不涨 —— 全是"正常"的假象。
    //     这跟登录框、自动注销是同一类东西:人机界面挡住机器。
    if (soakMode) {
        hmi::setModalAlertsEnabledGlobally(false);
        qCInfo(hmiApp) << "[main] --soak 模式:禁止弹模态对话框(失败只记日志)";
    }
    hmi window;
    tray::SystemTray trayIcon(&window);

    // 启动 inactivity 监听(必须在主窗口 show 前 install + start)
    // ★ --soak 下不启动:InactivityWatcher::start() 自己会查 installed_,
    //   没 installTo 时它会 qCWarning 后直接返回 —— 但那行警告在长跑日志里
    //   看着像故障,所以这里直接跳过,连警告都不产生。
    if (!soakMode) {
        inactivityWatcher.start();
    }

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

    // ★ B2.3(2026-09-17):托盘"切换用户" → logout + 重新弹 LoginDialog(in-place)
    QObject::connect(&trayIcon, &tray::SystemTray::logoutRequested,
                     &app, [&userMgr, &inactivityWatcher, &window]() {
        qCInfo(hmiApp) << "[main] 手动注销触发";
        auth::CurrentUser::instance().logout();
        inactivityWatcher.stop();
        auth::LoginDialog loginDlg(userMgr);
        if (loginDlg.exec() != QDialog::Accepted) {
            qCWarning(hmiApp) << "[main] 手动注销后用户拒绝登录,退出";
            QCoreApplication::exit(1);
            return;
        }
        auth::CurrentUser::instance().login(loginDlg.currentUser());
        qCInfo(hmiApp) << "[main] 手动注销后重新登录:" << loginDlg.currentUser().name;
        inactivityWatcher.start();
        window.show();   // 切换用户后让主窗口重新可见(可能之前已隐藏)
    });

    // ★ B2.3:托盘"修改密码" → 弹 ChangePasswordDialog
    QObject::connect(&trayIcon, &tray::SystemTray::changePasswordRequested,
                     &app, [&userMgr]() {
        qCInfo(hmiApp) << "[main] 修改密码触发";
        auth::ChangePasswordDialog dlg(userMgr);
        dlg.exec();   // 模态,关闭不退出
    });

    // ★ B2.4:托盘"用户管理" → 弹 UserManagementDialog(Admin 才能进)
    QObject::connect(&trayIcon, &tray::SystemTray::userManagementRequested,
                     &app, [&userMgr]() {
        qCInfo(hmiApp) << "[main] 用户管理触发";
        auth::UserManagementDialog dlg(userMgr);
        dlg.exec();   // exec() 内部检查 role == Admin,不是 Admin 直接 reject
    });
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
        // ★ B4(2026-09-17):托盘"用户管理"菜单项绑 ManageUsers(只有 Admin 能进)
        qwhmi::auth::bindActionEnable(trayIcon.userManagementAction(),
                                       qwhmi::auth::Permission::ManageUsers);
    }

    if (parser.isSet(projectOpt)) {
        const QString path = parser.value(projectOpt);
        qCInfo(hmiApp) << "启动参数 --project =" << path;
        // ★ 2026-10-21:真的去打开它(以前这里只打了一行日志,什么都没做)
        //   【为什么失败就退出,而不是"回退到 default.json 继续跑"】
        //     回退 = 长跑脚本以为在跑那份工程,实际跑的是另一份 ——
        //     一整天的数据测的是一个根本没打开的东西,而且【看不出来】。
        //     宁可当场退出(退出码 2),让 harness 立刻知道"没验成"。
        if (!window.loadProjectAtStartup(path)) {
            qCCritical(hmiApp) << "[main] --project 工程加载失败,拒绝启动:" << path;
            return 2;
        }
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
