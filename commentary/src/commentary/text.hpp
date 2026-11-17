#pragma once

// Text as Rust's str and char see it, for what the phrasebooks and the models write: UTF-8 code points, their case,
// whitespace, and the splits and trims the checker reads a comment with. Case and letter classes cover ASCII, Latin-1
// and Latin Extended-A, the letters of the languages the commentary speaks; other letters keep their case.

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fcg::commentary::text {

// One code point of UTF-8 text from byte `at`, and its length; a byte that starts none stands for itself.
[[nodiscard]] std::pair<char32_t, std::size_t> decode(std::string_view s, std::size_t at = 0) noexcept;
void append(std::string& out, char32_t c);
[[nodiscard]] std::string encode(char32_t c);

[[nodiscard]] char32_t upper(char32_t c) noexcept;
[[nodiscard]] char32_t lower(char32_t c) noexcept;
[[nodiscard]] bool isLowercase(char32_t c) noexcept;
[[nodiscard]] bool isUppercase(char32_t c) noexcept;
[[nodiscard]] bool isAlphabetic(char32_t c) noexcept;
[[nodiscard]] bool isNumeric(char32_t c) noexcept;
[[nodiscard]] inline bool isAlphanumeric(char32_t c) noexcept { return isAlphabetic(c) || isNumeric(c); }
// char::is_whitespace.
[[nodiscard]] bool isWhitespace(char32_t c) noexcept;

// str::to_lowercase.
[[nodiscard]] std::string toLower(std::string_view s);
// The number of code points: chars().count().
[[nodiscard]] std::size_t length(std::string_view s) noexcept;
// Whether `set`, in UTF-8, holds the code point `c`: "…".contains(c).
[[nodiscard]] bool holds(std::string_view set, char32_t c) noexcept;

// str::split with a test on code points: the pieces between the code points it accepts, empty ones kept.
template <typename Separator>
[[nodiscard]] std::vector<std::string_view> split(std::string_view s, Separator separator) {
    std::vector<std::string_view> out;
    std::size_t start = 0;
    for (std::size_t at = 0; at < s.size();) {
        const auto [c, n] = decode(s, at);
        if (separator(c)) {
            out.push_back(s.substr(start, at - start));
            start = at + n;
        }
        at += n;
    }
    out.push_back(s.substr(start));
    return out;
}
// str::split_whitespace: the pieces between whitespace, empty ones left out.
[[nodiscard]] std::vector<std::string_view> splitWhitespace(std::string_view s);
// str::trim.
[[nodiscard]] std::string_view trim(std::string_view s) noexcept;
// trim_matches and trim_end_matches with the code points of `set`.
[[nodiscard]] std::string_view trimMatches(std::string_view s, std::string_view set) noexcept;
[[nodiscard]] std::string_view trimEndMatches(std::string_view s, std::string_view set) noexcept;
// The byte where the last code point of `s` starts; 0 for an empty `s`.
[[nodiscard]] std::size_t lastStart(std::string_view s) noexcept;

}  // namespace fcg::commentary::text
