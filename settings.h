#ifndef SETTINGS_H
#define SETTINGS_H

#include <QByteArray>
#include <QString>

namespace Settings {

auto tmutilPath() -> QString;
auto tmutilStatInterval() -> int;
auto tmutilDestInterval() -> int;
auto mainWindowGeometry() -> QByteArray;
auto mainWindowState() -> QByteArray;
auto sudoPath() -> QString;

void setTmutilPath(const QString& value);
void setTmutilStatInterval(int value);
void setTmutilDestInterval(int value);
void setMainWindowGeometry(const QByteArray& value);
void setMainWindowState(const QByteArray& value);
void setSudoPath(const QString& value);

}

#endif // SETTINGS_H
