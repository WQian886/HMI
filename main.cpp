#include "hmi.h"
#include <QtWidgets/QApplication>
#include <QCoreApplication>  // ★ IMPROVE-B1(2026-07-29):setOrganizationName/setApplicationName
#include <QTime>              // ★ IMPROVE-B2(2026-08-11 Wqian 拍板):qDebug 时间戳
#include <cstdio>             // ★ IMPROVE-B2(2026-08-11 Wqian 拍板):fprintf

int main(int argc, char *argv[])
{
    // ★ IMPROVE-B1(2026-07-29):阶段 6 工程文件 QSettings 前置
    //   没设的话 QSettings 默认存到注册表 HKCU\Software\HMI 等,跨平台 path 混乱
    //   设在 QApplication 之前,QSettings 自动用 C:/Users/<user>/AppData/Roaming/QianWei/HMI/
    QCoreApplication::setOrganizationName("QianWei");
    QCoreApplication::setApplicationName("HMI");

    // ★ IMPROVE-B2(2026-08-11 Wqian 拍板):全局 qDebug 重定向,加毫秒级时间戳
    //   【背景】subsystem=Console 后黑框有了,但 qDebug 默认输出不带时间戳
    //   【改法】qInstallMessageHandler 拦截 → fprintf 加 [hh:mm:ss.zzz] 前缀
    //   【取舍】handler 装上之前的 qDebug 走老路(无时间戳);装上之后所有 qDebug 都带
    //   【参照】Wqian 2026-08-11 要求"黑框内容打上时间";放 main() 内避免文件作用域 most vexing parse
    qInstallMessageHandler([](QtMsgType, const QMessageLogContext&, const QString& msg) {
        fprintf(stderr, "[%s] %s\n",
                QTime::currentTime().toString("hh:mm:ss.zzz").toLocal8Bit().constData(),
                msg.toLocal8Bit().constData());
    });

    QApplication app(argc, argv);
    hmi window;
    window.show();
    return app.exec();
}
