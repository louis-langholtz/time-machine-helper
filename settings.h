#ifndef SETTINGS_H
#define SETTINGS_H

#include <QByteArray>
#include <QString>

namespace Settings {

auto defaultTmutilPath() -> QString;
auto defaultSudoPath() -> QString;
auto defaultTmutilStatInterval() -> int;
auto defaultTmutilDestInterval() -> int;
auto defaultPathInfoInterval() -> int;

auto tmutilPath() -> QString;
auto sudoPath() -> QString;
auto tmutilStatInterval() -> int;
auto tmutilDestInterval() -> int;
auto pathInfoInterval() -> int;

auto mainWindowGeometry() -> QByteArray;
auto mainWindowState() -> QByteArray;
auto destinationsTableState() -> QByteArray;
auto machinesTableState() -> QByteArray;
auto volumesTableState() -> QByteArray;
auto backupsTableState() -> QByteArray;

void setTmutilPath(const QString& value);
void setTmutilStatInterval(int value);
void setTmutilDestInterval(int value);
void setSudoPath(const QString& value);
void setPathInfoInterval(int value);

void setMainWindowGeometry(const QByteArray& value);
void setMainWindowState(const QByteArray& value);
void setDestinationsTableState(const QByteArray& value);
void setMachinesTableState(const QByteArray& value);
void setVolumesTableState(const QByteArray& value);
void setBackupsTableState(const QByteArray& value);

void clear();

void removeMainWindowGeometry();
void removeMainWindowState();
void removeDestinationsTableState();
void removeMachinesTableState();
void removeVolumesTableState();
void removeBackupsTableState();

}

#endif // SETTINGS_H
