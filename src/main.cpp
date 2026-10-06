#include <QApplication>
#include <QCoreApplication>

#include "MainWindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("LocalTools");
    QCoreApplication::setApplicationName("repo");

    MainWindow window;
    window.resize(1440, 900);
    window.show();

    return app.exec();
}
