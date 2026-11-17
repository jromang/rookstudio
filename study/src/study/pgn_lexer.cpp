#include "pgn_lexer.hpp"

#include <array>
#include <utility>

namespace fcg::study {

namespace {

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v'; }
bool isDigit(char c) { return c >= '0' && c <= '9'; }
// What ends a move or another bare word.
bool isStop(char c) {
    return isSpace(c) || c == '(' || c == ')' || c == '{' || c == '}' || c == '[' || c == ']' || c == ';' || c == '$';
}

constexpr std::array<std::pair<std::string_view, int>, 22> GLYPHS{{
    {"!", 1},
    {"?", 2},
    {"!!", 3},
    {"??", 4},
    {"!?", 5},
    {"?!", 6},
    {"=", 10},
    {"\xE2\x88\x9E", 13},
    {"+=", 14},
    {"+/=", 14},
    {"=+", 15},
    {"=/+", 15},
    {"+/-", 16},
    {"\xC2\xB1", 16},
    {"-/+", 17},
    {"\xE2\x88\x93", 17},
    {"+-", 18},
    {"-+", 19},
    {"N", 146},
    {"\xE2\xA9\xB2", 14},
    {"\xE2\xA9\xB1", 15},
    {"\xE2\x96\xA1", 7},
}};

}  // namespace

int glyphNag(std::string_view word) {
    for (const auto& [glyph, nag] : GLYPHS) {
        if (glyph == word) return nag;
    }
    return 0;
}

std::string_view nagGlyph(int nag) {
    static constexpr std::array<std::string_view, 7> glyphs{"", "!", "?", "!!", "??", "!?", "?!"};
    return nag >= 1 && nag <= 6 ? glyphs[static_cast<std::size_t>(nag)] : std::string_view{};
}

PgnLexer::PgnLexer(std::string_view text) : s_(text) {
    if (s_.substr(0, 3) == "\xEF\xBB\xBF") pos_ = 3;
}

void PgnLexer::skipSpace() {
    while (pos_ < s_.size()) {
        const char c = s_[pos_];
        if (c == '\n') {
            line_start_ = true;
            ++pos_;
        } else if (isSpace(c)) {
            ++pos_;
        } else if (c == '%' && line_start_) {
            // An escape line: for another program, to be skipped whole.
            while (pos_ < s_.size() && s_[pos_] != '\n') ++pos_;
        } else {
            return;
        }
    }
}

PgnLexeme PgnLexer::next() {
    skipSpace();
    PgnLexeme out;
    out.at = pos_;
    if (pos_ >= s_.size()) return out;
    line_start_  = false;
    const char c = s_[pos_];

    if (c == '[') {
        ++pos_;
        while (pos_ < s_.size() && (s_[pos_] == ' ' || s_[pos_] == '\t')) ++pos_;
        const auto name = pos_;
        while (pos_ < s_.size() && !isSpace(s_[pos_]) && s_[pos_] != '"' && s_[pos_] != ']') ++pos_;
        out.kind = PgnToken::Tag;
        out.text = s_.substr(name, pos_ - name);
        while (pos_ < s_.size() && (s_[pos_] == ' ' || s_[pos_] == '\t')) ++pos_;
        if (pos_ < s_.size() && s_[pos_] == '"') {
            const auto value = ++pos_;
            while (pos_ < s_.size() && s_[pos_] != '"' && s_[pos_] != '\n') {
                if (s_[pos_] == '\\' && pos_ + 1 < s_.size() && s_[pos_ + 1] != '\n') ++pos_;
                ++pos_;
            }
            out.value = s_.substr(value, pos_ - value);
            if (pos_ < s_.size() && s_[pos_] == '"') ++pos_;
        }
        // To the end of the tag, or of its line when it has no end.
        while (pos_ < s_.size() && s_[pos_] != ']' && s_[pos_] != '\n') ++pos_;
        if (pos_ < s_.size() && s_[pos_] == ']') ++pos_;
        return out;
    }
    if (c == '{') {
        const auto from = ++pos_;
        while (pos_ < s_.size() && s_[pos_] != '}') ++pos_;
        out.kind = PgnToken::Comment;
        out.text = s_.substr(from, pos_ - from);
        if (pos_ < s_.size()) ++pos_;
        return out;
    }
    if (c == ';') {
        const auto from = ++pos_;
        while (pos_ < s_.size() && s_[pos_] != '\n') ++pos_;
        out.kind = PgnToken::Comment;
        out.text = s_.substr(from, pos_ - from);
        return out;
    }
    if (c == '(' || c == ')') {
        ++pos_;
        out.kind = c == '(' ? PgnToken::VariationStart : PgnToken::VariationEnd;
        out.text = s_.substr(out.at, 1);
        return out;
    }
    if (c == '$') {
        const auto from = ++pos_;
        int nag         = 0;
        while (pos_ < s_.size() && isDigit(s_[pos_])) {
            if (nag < 1000) nag = nag * 10 + (s_[pos_] - '0');
            ++pos_;
        }
        out.kind = PgnToken::Nag;
        out.nag  = nag;
        out.text = s_.substr(from - 1, pos_ - from + 1);
        return out;
    }
    if (c == '}' || c == ']') {
        // A stray closing bracket: nothing, the next word.
        ++pos_;
        return next();
    }

    // A bare word: a move number, a result, a move, a glyph or a sign.
    const auto from = pos_;
    while (pos_ < s_.size() && !isStop(s_[pos_])) ++pos_;
    auto word = s_.substr(from, pos_ - from);
    out.text  = word;

    if (word == "*" || word == "1-0" || word == "0-1" || word == "1/2-1/2" || word == "\xC2\xBD-\xC2\xBD") {
        out.kind = PgnToken::Result;
        return out;
    }
    if (isDigit(word.front())) {
        std::size_t digits = 0;
        while (digits < word.size() && isDigit(word[digits])) ++digits;
        std::size_t dots = digits;
        while (dots < word.size() && word[dots] == '.') ++dots;
        if (digits == word.size() || dots > digits) {
            // "12." or "12...", maybe glued to its move: "12.e4" leaves "e4" for the next word.
            pos_     = from + dots;
            out.kind = PgnToken::MoveNumber;
            out.text = s_.substr(from, dots);
            return out;
        }
    }
    if (const int nag = glyphNag(word); nag != 0) {
        out.kind = PgnToken::Nag;
        out.nag  = nag;
        return out;
    }
    if (word == "e.p." || word == "ep") return next();  // "exd6 e.p.": the move is enough

    // A move, its glyphs taken off: "e4!?", "Nf3+?!".
    std::size_t end = word.size();
    while (end > 0 && (word[end - 1] == '!' || word[end - 1] == '?')) --end;
    out.kind  = PgnToken::San;
    out.text  = word.substr(0, end);
    out.value = word.substr(end);
    if (out.text.empty()) {
        // Only glyphs, one that is no known one ("!!!"): the first two.
        out.kind = PgnToken::Nag;
        out.nag  = glyphNag(word.substr(0, 2));
    }
    return out;
}

}  // namespace fcg::study
