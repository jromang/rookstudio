#pragma once

// The words of a PGN text, one after the other, without copying it: what the game reader (pgn_reader.hpp) and the
// database import read. Takes what programs and sites write besides the standard: a byte order mark, CRLF, moves
// glued to their number ("1.e4"), glyphs glued to their move ("e4!?"), evaluation signs written as text ("+-"),
// castling with zeros, null moves (-- and Z0), escape lines (% in the first column).

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace fcg::study {

enum class PgnToken : uint8_t {
    End,             // the end of the text
    Tag,             // [Name "value"]: the name in text, the value as written (its escapes kept) in value
    MoveNumber,      // 12. or 12...
    San,             // a move as written, its glyphs taken off (in value), or a null move (-- or Z0)
    Nag,             // $12, a glyph standing alone (!?), or an evaluation sign written as text (+-): in nag
    Comment,         // {...} or ;... to the end of the line: the text inside, in text
    VariationStart,  // (
    VariationEnd,    // )
    Result,          // 1-0, 0-1, 1/2-1/2 (or ½-½), *
};

struct PgnLexeme {
    PgnToken kind = PgnToken::End;
    std::string_view text;   // the word, or a tag's name, or a comment's text
    std::string_view value;  // a tag's value as written; the glyphs glued to a move
    int nag        = 0;
    std::size_t at = 0;  // where the word starts in the text
};

class PgnLexer {
   public:
    explicit PgnLexer(std::string_view text);

    PgnLexeme next();
    // Where the next word would start: the end of the last one read.
    [[nodiscard]] std::size_t offset() const noexcept { return pos_; }

   private:
    void skipSpace();

    std::string_view s_;
    std::size_t pos_ = 0;
    bool line_start_ = true;
};

// The NAG of a glyph or of an evaluation sign written as text: "!" 1, "?" 2, "!!" 3, "??" 4, "!?" 5, "?!" 6, "="
// 10, "∞" 13, "+=" 14, "=+" 15, "+/-" or "±" 16, "-/+" or "∓" 17, "+-" 18, "-+" 19, "N" 146; 0 for any other word.
[[nodiscard]] int glyphNag(std::string_view word);
// The glyph written for NAGs 1 to 6, empty for any other.
[[nodiscard]] std::string_view nagGlyph(int nag);

}  // namespace fcg::study
