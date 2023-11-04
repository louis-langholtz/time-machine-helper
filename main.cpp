#include "mainwindow.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QCoreApplication::setOrganizationName("louis-langholtz");
    QCoreApplication::setOrganizationDomain("louis-langholtz.github.io");
    QCoreApplication::setApplicationName("Time Machine Helper");

    QApplication a(argc, argv);
    MainWindow w;
    w.show();

    return a.exec();
}
