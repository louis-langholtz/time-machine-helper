#include <QSettings>

#include "settings.h"

namespace Settings {
namespace {

constexpr auto tmutilPathKey = "tmutilPath";
constexpr auto tmutilStatTimeKey = "tmutilStatusInterval";
constexpr auto tmutilDestTimeKey = "tmutilDestinationsInterval";
constexpr auto sudoPathKey = "sudoPath";
constexpr auto pathInfoTimeKey = "pathInfoInterval";
constexpr auto mainWindowGeomKey = "mainWindowGeomtry";
constexpr auto mainWindowStateKey = "mainWindowState";
constexpr auto centralWidgetStateKey = "centralWidgetState";
constexpr auto destinationsTableStateKey = "destinationsTableState";
constexpr auto machinesTableStateKey = "machinesTableState";
constexpr auto volumesTableStateKey = "volumesTableState";
constexpr auto backupsTableStateKey = "backupsTableState";

auto settings() -> QSettings &
{
    static QSettings the;
    return the;
}

}

auto defaultTmutilPath() -> QString
{
    static const auto value = QString{"/usr/bin/tmutil"};
    return value;
}

auto defaultSudoPath() -> QString
{
    static const auto value = QString{"/usr/bin/sudo"};
    return value;
}

auto defaultTmutilStatInterval() -> int
{
    static constexpr auto value = 1000;
    return value;
}

auto defaultTmutilDestInterval() -> int
{
    static constexpr auto value = 2500;
    return value;
}

auto defaultPathInfoInterval() -> int
{
    static constexpr auto value = 10000;
    return value;
}

auto tmutilPath() -> QString
{
    return settings()
        .value(tmutilPathKey,
               QVariant::fromValue(defaultTmutilPath()))
        .toString();
}

auto sudoPath() -> QString
{
    return settings()
        .value(sudoPathKey,
               QVariant::fromValue(defaultSudoPath()))
        .toString();
}

auto tmutilStatInterval() -> int
{
    return settings()
        .value(tmutilStatTimeKey,
               QVariant::fromValue(defaultTmutilStatInterval()))
        .toInt();
}

auto tmutilDestInterval() -> int
{
    return settings()
        .value(tmutilDestTimeKey,
               QVariant::fromValue(defaultTmutilDestInterval()))
        .toInt();
}

auto pathInfoInterval() -> int
{
    return settings()
        .value(pathInfoTimeKey,
               QVariant::fromValue(defaultPathInfoInterval()))
        .toInt();
}

auto mainWindowGeometry() -> QByteArray
{
    return settings().value(mainWindowGeomKey).toByteArray();
}

auto mainWindowState() -> QByteArray
{
    return settings().value(mainWindowStateKey).toByteArray();
}

auto centralWidgetState() -> QByteArray
{
    return settings().value(centralWidgetStateKey).toByteArray();
}

auto destinationsTableState() -> QByteArray
{
    return settings().value(destinationsTableStateKey).toByteArray();
}

auto machinesTableState() -> QByteArray
{
    return settings().value(machinesTableStateKey).toByteArray();
}

auto volumesTableState() -> QByteArray
{
    return settings().value(volumesTableStateKey).toByteArray();
}

auto backupsTableState() -> QByteArray
{
    return settings().value(backupsTableStateKey).toByteArray();
}

void setTmutilPath(const QString& value)
{
    settings().setValue(tmutilPathKey, value);
}

void setTmutilStatInterval(int value)
{
    settings().setValue(tmutilStatTimeKey, value);
}

void setTmutilDestInterval(int value)
{
    settings().setValue(tmutilDestTimeKey, value);
}

void setSudoPath(const QString &value)
{
    settings().setValue(sudoPathKey, value);
}

void setPathInfoInterval(int value)
{
    settings().setValue(pathInfoTimeKey, value);
}

void setMainWindowGeometry(const QByteArray &value)
{
    settings().setValue(mainWindowGeomKey, value);
}

void setMainWindowState(const QByteArray &value)
{
    settings().setValue(mainWindowStateKey, value);
}

void setCentralWidgetState(const QByteArray &value)
{
    settings().setValue(centralWidgetStateKey, value);
}

void setDestinationsTableState(const QByteArray &value)
{
    settings().setValue(destinationsTableStateKey, value);
}

void setMachinesTableState(const QByteArray &value)
{
    settings().setValue(machinesTableStateKey, value);
}

void setVolumesTableState(const QByteArray &value)
{
    settings().setValue(volumesTableStateKey, value);
}

void setBackupsTableState(const QByteArray &value)
{
    settings().setValue(backupsTableStateKey, value);
}

void clear()
{
    settings().clear();
}

void removeMainWindowGeometry()
{
    settings().remove(mainWindowGeomKey);
}

void removeMainWindowState()
{
    settings().remove(mainWindowStateKey);
}

void removeDestinationsTableState()
{
    settings().remove(destinationsTableStateKey);
}

void removeMachinesTableState()
{
    settings().remove(machinesTableStateKey);
}

void removeVolumesTableState()
{
    settings().remove(volumesTableStateKey);
}

void removeBackupsTableState()
{
    settings().remove(backupsTableStateKey);
}

}
