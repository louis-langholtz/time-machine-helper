#ifndef SECONDS_H
#define SECONDS_H

#include <chrono>

#include <QMetaType>
#include <QString>

Q_DECLARE_METATYPE(std::chrono::seconds); // NOLINT(modernize-use-trailing-return-type)

auto toString(std::chrono::seconds value) -> QString;

#endif // SECONDS_H
