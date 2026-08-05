#include "hmi.h"
#include <QtWidgets/QApplication>
#include <QCoreApplication>  // ★ IMPROVE-B1(2026-07-29):setOrganizationName/setApplicationName

int main(int argc, char *argv[])
{
    // ★ IMPROVE-B1(2026-07-29):阶段 6 工程文件 QSettings 前置
    //   没设的话 QSettings 默认存到注册表 HKCU\Software\HMI 等,跨平台 path 混乱
    //   设在 QApplication 之前,QSettings 自动用 C:/Users/<user>/AppData/Roaming/QianWei/HMI/
    QCoreApplication::setOrganizationName("QianWei");
    QCoreApplication::setApplicationName("HMI");

    QApplication app(argc, argv);
    hmi window;
    window.show();
    return app.exec();
}
