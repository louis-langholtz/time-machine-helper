#include <filesystem>

#include <QApplication>

#include "mainwindow.h"

int main(int argc, char *argv[])
{
    QCoreApplication::setOrganizationName("louis-langholtz");
    QCoreApplication::setOrganizationDomain("louis-langholtz.github.io");
    QCoreApplication::setApplicationName("Time Machine Helper");

    QApplication a(argc, argv);

    // Allow some standard library types as QVariant...
    qRegisterMetaType<std::filesystem::path>();
    qRegisterMetaType<std::filesystem::file_status>();
    qRegisterMetaType<std::error_code>();

    MainWindow w;
    w.show();

    return a.exec();
}
