#include "seconds.h"

auto toString(std::chrono::seconds value)
    -> QString
{
    const auto seconds = value.count() % 60;
    const auto totalMinutes = value.count() / 60;
    const auto minutes = totalMinutes % 60;
    const auto hours = totalMinutes / 60;
    constexpr auto base = 10;
    constexpr auto fillChar = QChar('0');
    return QString("%1:%2:%3")
        .arg(hours, 1, base, fillChar)
        .arg(minutes, 2, base, fillChar)
        .arg(seconds, 2, base, fillChar);
}
