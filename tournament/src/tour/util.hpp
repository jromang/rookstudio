#pragma once

// Small helpers that every layer uses, each written once (#117): text in lower case, FNV-1a, the clock in seconds, the
// local time. In namespace fcg, for the window, the study, the commentary and the tournament layer alike.

#include <chrono>
#include <cstdint>
#include <ctime>
#include <optional>
#include <string>
#include <string_view>

namespace fcg {

// `text` in lower case, ASCII's letters only: other bytes, UTF-8's among them, stay as they are.
[[nodiscard]] inline std::string lower(std::string_view text) {
    std::string out(text);
    for (auto& c : out)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return out;
}

// FNV-1a, 64 bits, over `bytes`, from `h`: FNV's offset basis, or the hash of what came before them.
inline constexpr uint64_t FNV_OFFSET = 0xcbf29ce484222325ULL;
[[nodiscard]] constexpr uint64_t fnv1a(std::string_view bytes, uint64_t h = FNV_OFFSET) noexcept {
    for (const unsigned char c : bytes) h = (h ^ c) * 0x100000001b3ULL;
    return h;
}

// Seconds since the epoch (1970-01-01, UTC).
[[nodiscard]] inline int64_t nowSeconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

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
