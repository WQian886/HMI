#include "hmi.h"
#include <QtWidgets/QApplication>
#include <QCoreApplication>  // ★ IMPROVE-B1(2026-07-29):setOrganizationName/setApplicationName
#include <QTime>              // ★ IMPROVE-B2(2026-08-11 Wqian 拍板):qDebug 时间戳
#include <cstdio>             // ★ IMPROVE-B2(2026-08-11 Wqian 拍板):fprintf
#include <QFile>              // ★ IMPROVE-B3(2026-08-11 Wqian 拍板):hmi.log 持久化
#include <functional>         // ★ IMPROVE-B3-fix:std::function 桥接

// ★ IMPROVE-B3-fix:全局 std::function 桥接
//   【背景】qInstallMessageHandler 接收 QtMessageHandler(函数指针),有 capture lambda 不能隐式转
//   【改法】用 std::function g_logCallback 桥接:handler 是无 capture lambda(转函数指针 OK),里面调 g_logCallback 间接携 logFile
static std::function<void(const QString&)> g_logCallback;

int main(int argc, char *argv[])
{
    // ★ IMPROVE-B1(2026-07-29):阶段 6 工程文件 QSettings 前置
    //   没设的话 QSettings 默认存到注册表 HKCU\Software\HMI 等,跨平台 path 混乱
    //   设在 QApplication 之前,QSettings 自动用 C:/Users/<user>/AppData/Roaming/QianWei/HMI/
    QCoreApplication::setOrganizationName("QianWei");
    QCoreApplication::setApplicationName("HMI");

    QApplication app(argc, argv);

    // ★ IMPROVE-B3(2026-08-11 Wqian 拍板):hmi.log 持久化(纯文本追加模式)
    //   【背景】stderr 只在黑框,程序关了就没了;log 文件可以事后分析(grep / tail)
    //   【改法】QFile logFile + open(Append|Text) + g_logCallback 桥接(handler 间接携 logFile)
    //   【取舍】多线程 / 磁盘满 / 大文件 简单方案不处理(工业 HMI 量不大,逛常没事)
    QFile logFile(QCoreApplication::applicationDirPath() + "/hmi.log");
    logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
    g_logCallback = [&logFile](const QString& line) { logFile.write(line.toUtf8()); };

    // ★ IMPROVE-B2 更新:handler 是无 capture lambda → 隐式转 QtMessageHandler 函数指针
    qInstallMessageHandler([](QtMsgType, const QMessageLogContext&, const QString& msg) {
        const QString line = QString("[%1] %2\n")
            .arg(QTime::currentTime().toString("hh:mm:ss.zzz"), msg);
        fprintf(stderr, "%s", line.toLocal8Bit().constData());  // 黑框
        if (g_logCallback) g_logCallback(line);                  // 文件(绕过 std::function)
    });

    hmi window;
    window.show();
    return app.exec();
}
