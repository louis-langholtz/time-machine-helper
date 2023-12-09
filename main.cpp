#include <filesystem>

#include <QApplication>

#include "mainwindow.h"
#include "seconds.h"

auto main(int argc, char *argv[]) -> int
{
    QCoreApplication::setOrganizationName("louis-langholtz");
    QCoreApplication::setOrganizationDomain("louis-langholtz.github.io");
    QCoreApplication::setApplicationName("Time Machine Helper");

    const QApplication app(argc, argv);

    // Allow some standard library types as QVariant...
    qRegisterMetaType<std::filesystem::path>();
    qRegisterMetaType<std::filesystem::file_status>();
    qRegisterMetaType<std::error_code>();
    qRegisterMetaType<std::chrono::seconds>();

    QMetaType::registerConverter<std::chrono::seconds, QString>(
        [](std::chrono::seconds value) {
            return toString(value);
        });

    MainWindow w;
    w.show();

    return QApplication::exec();
}
