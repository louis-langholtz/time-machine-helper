#include <QSettings>

#include "settings.h"

namespace Settings {
namespace {

constexpr auto tmutilPathKey = "tmutilPath";
constexpr auto tmutilStatTimeKey = "tmutilStatusInterval";
constexpr auto tmutilDestTimeKey = "tmutilDestinationsInterval";
constexpr auto mainWindowGeomKey = "mainWindowGeomtry";
constexpr auto mainWindowStateKey = "mainWindowState";

constexpr auto defaultTmutilPath = "/usr/bin/tmutil";
constexpr auto defaultTmutilStatTime = 1000;
constexpr auto defaultTmutilDestTime = 2500;

auto settings() -> QSettings &
{
    static QSettings the;
    return the;
}

}

auto tmutilPath() -> QString
{
    return settings().value(tmutilPathKey,
                            QString(defaultTmutilPath)).toString();
}

auto tmutilStatInterval() -> int
{
    return settings().value(tmutilStatTimeKey,
                            defaultTmutilStatTime).toInt();
}

auto tmutilDestInterval() -> int
{
    return settings().value(tmutilDestTimeKey,
                            defaultTmutilDestTime).toInt();
}

auto mainWindowGeometry() -> QByteArray
{
    return settings().value(mainWindowGeomKey).toByteArray();
}

auto mainWindowState() -> QByteArray
{
    return settings().value(mainWindowStateKey).toByteArray();
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

void setMainWindowGeometry(const QByteArray &value)
{
    settings().setValue(mainWindowGeomKey, value);
}

void setMainWindowState(const QByteArray &value)
{
    settings().setValue(mainWindowStateKey, value);
}

}
