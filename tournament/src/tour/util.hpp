#pragma once

// Small helpers that every layer uses, each written once (#117): the local time. In namespace fcg, for the window, the
// study, the commentary and the tournament layer alike.

#include <ctime>
#include <optional>

namespace fcg {

// `t` in local time, by the system's thread-safe call (localtime() shares one buffer between the threads); none when
// the system cannot say (a time out of its range).
[[nodiscard]] inline std::optional<std::tm> localTimeOf(std::time_t t) {
    std::tm tm{};
#ifdef _WIN32
    if (localtime_s(&tm, &t) != 0) return std::nullopt;
#else
    if (!localtime_r(&t, &tm)) return std::nullopt;
#endif
    return tm;
}
// The same, every field 0 when the system cannot say.
[[nodiscard]] inline std::tm localTime(std::time_t t) { return localTimeOf(t).value_or(std::tm{}); }

}  // namespace fcg
