#include "hmi.h"
#include <QtWidgets/QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    hmi window;
    window.show();
    return app.exec();
}
