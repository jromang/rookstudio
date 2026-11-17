#include "text.hpp"

namespace fcg::commentary::text {

namespace {

// Latin Extended-A (U+0100 to U+017F): which code points are capitals. The pairs start on an even code point, then on
// an odd one from U+0139 to U+0148 and from U+0179; a few letters have no pair.
bool extendedUpper(char32_t c) noexcept {
    if (c == 0x138 || c == 0x149 || c == 0x17F) return false;  // ĸ, ŉ, ſ
    if (c == 0x178) return true;                               // Ÿ
    if ((c >= 0x139 && c <= 0x148) || (c >= 0x179 && c <= 0x17E)) return c % 2 == 1;
    return c % 2 == 0;
}

}  // namespace

std::pair<char32_t, std::size_t> decode(std::string_view s, std::size_t at) noexcept {
    const auto byte    = [&](std::size_t i) { return static_cast<unsigned char>(s[i]); };
    const auto c       = byte(at);
    const auto follows = [&](std::size_t n) {
        if (at + n >= s.size()) return false;
        for (std::size_t i = 1; i <= n; ++i) {
            if ((byte(at + i) & 0xC0) != 0x80) return false;
        }
        return true;
    };
    if (c < 0x80) return {c, 1};
    if ((c >> 5) == 6 && follows(1)) return {static_cast<char32_t>(((c & 0x1F) << 6) | (byte(at + 1) & 0x3F)), 2};
    if ((c >> 4) == 14 && follows(2))
        return {static_cast<char32_t>(((c & 0x0F) << 12) | ((byte(at + 1) & 0x3F) << 6) | (byte(at + 2) & 0x3F)), 3};
    if ((c >> 3) == 30 && follows(3))
        return {static_cast<char32_t>(((c & 0x07) << 18) | ((byte(at + 1) & 0x3F) << 12) |
                                      ((byte(at + 2) & 0x3F) << 6) | (byte(at + 3) & 0x3F)),
                4};
    return {c, 1};
}

void append(std::string& out, char32_t c) {
    if (c < 0x80) {
        out += static_cast<char>(c);
    } else if (c < 0x800) {
        out += static_cast<char>(0xC0 | (c >> 6));
        out += static_cast<char>(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
        out += static_cast<char>(0xE0 | (c >> 12));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (c >> 18));
        out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
    }
}

std::string encode(char32_t c) {
    std::string out;
    append(out, c);
    return out;
}

bool isUppercase(char32_t c) noexcept {
    if (c < 0x80) return c >= 'A' && c <= 'Z';
    if (c >= 0xC0 && c <= 0xDE) return c != 0xD7;
    if (c >= 0x100 && c <= 0x17F) return extendedUpper(c);
    return false;
}

bool isLowercase(char32_t c) noexcept {
    if (c < 0x80) return c >= 'a' && c <= 'z';
    if (c == 0xAA || c == 0xB5 || c == 0xBA) return true;  // ª, µ, º
    if (c >= 0xDF && c <= 0xFF) return c != 0xF7;
    if (c >= 0x100 && c <= 0x17F) return !extendedUpper(c);
    return false;
}

bool isAlphabetic(char32_t c) noexcept {
    if (isLowercase(c) || isUppercase(c)) return true;
    // Latin Extended-B, Greek and Coptic, Cyrillic: letters, though their case is not followed here.
    return (c >= 0x180 && c <= 0x24F) || (c >= 0x370 && c <= 0x3FF && c != 0x37E && c != 0x387) ||
           (c >= 0x400 && c <= 0x482) || (c >= 0x48A && c <= 0x52F);
}

bool isNumeric(char32_t c) noexcept {
    if (c >= '0' && c <= '9') return true;
    return c == 0xB2 || c == 0xB3 || c == 0xB9 || (c >= 0xBC && c <= 0xBE);  // ², ³, ¹, ¼, ½, ¾
}

char32_t upper(char32_t c) noexcept {
    if (c >= 'a' && c <= 'z') return c - 32;
    if (c >= 0xE0 && c <= 0xFE && c != 0xF7) return c - 0x20;
    if (c == 0xFF) return 0x178;
    if (c >= 0x100 && c <= 0x17F && c != 0x131 && c != 0x138 && c != 0x149 && c != 0x17F && !extendedUpper(c))
        return c - 1;
    return c;
}

char32_t lower(char32_t c) noexcept {
    if (c >= 'A' && c <= 'Z') return c + 32;
    if (c >= 0xC0 && c <= 0xDE && c != 0xD7) return c + 0x20;
    if (c == 0x178) return 0xFF;
    if (c >= 0x100 && c <= 0x17F && c != 0x130 && extendedUpper(c)) return c + 1;
    return c;
}

bool isWhitespace(char32_t c) noexcept {
    return (c >= 0x09 && c <= 0x0D) || c == 0x20 || c == 0x85 || c == 0xA0 || c == 0x1680 ||
           (c >= 0x2000 && c <= 0x200A) || c == 0x2028 || c == 0x2029 || c == 0x202F || c == 0x205F || c == 0x3000;
}

std::string toLower(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t at = 0; at < s.size();) {
        const auto [c, n] = decode(s, at);
        if (c < 0x80)
            out += static_cast<char>(lower(c));
        else if (const auto l = lower(c); l != c)
            append(out, l);
        else
            out.append(s.substr(at, n));
        at += n;
    }
    return out;
}

std::size_t length(std::string_view s) noexcept {
    std::size_t n = 0;
    for (std::size_t at = 0; at < s.size(); ++n) at += decode(s, at).second;
    return n;
}

bool holds(std::string_view set, char32_t c) noexcept {
    for (std::size_t at = 0; at < set.size();) {
        const auto [d, n] = decode(set, at);
        if (d == c) return true;
        at += n;
    }
    return false;
}

std::vector<std::string_view> splitWhitespace(std::string_view s) {
    std::vector<std::string_view> out;
    for (const auto piece : split(s, isWhitespace)) {
        if (!piece.empty()) out.push_back(piece);
    }
    return out;
}

std::size_t lastStart(std::string_view s) noexcept {
    if (s.empty()) return 0;
    std::size_t at = s.size() - 1;
    for (int back = 0; back < 3 && at > 0 && (static_cast<unsigned char>(s[at]) & 0xC0) == 0x80; ++back) --at;
    // A continuation byte no code point claims stands for itself, as decode() reads it.
    return at + decode(s, at).second == s.size() ? at : s.size() - 1;
}

namespace {

template <typename Test>
std::string_view trimStart(std::string_view s, Test test) noexcept {
    std::size_t at = 0;
    while (at < s.size()) {
        const auto [c, n] = decode(s, at);
        if (!test(c)) break;
        at += n;
    }
    return s.substr(at);
}

template <typename Test>
std::string_view trimEnd(std::string_view s, Test test) noexcept {
    while (!s.empty()) {
        const auto at = lastStart(s);
        if (!test(decode(s, at).first)) break;
        s = s.substr(0, at);
    }
    return s;
}

}  // namespace

std::string_view trim(std::string_view s) noexcept { return trimEnd(trimStart(s, isWhitespace), isWhitespace); }

std::string_view trimMatches(std::string_view s, std::string_view set) noexcept {
    const auto in = [&](char32_t c) { return holds(set, c); };
    return trimEnd(trimStart(s, in), in);
}

std::string_view trimEndMatches(std::string_view s, std::string_view set) noexcept {
    return trimEnd(s, [&](char32_t c) { return holds(set, c); });
}

}  // namespace fcg::commentary::text
