#pragma once

// The piece letters of each language of the interface (#66, mockup M13, board 7, point 21): the notation the interface
// writes in them (French R D T F C, German K D T L S, Russian Кр Ф Л С К...), and a move typed in them, or in English
// letters, which are always read too. A table in code keyed by the language, as chess players write the pieces, not
// messages for the translators. The PGN, and anything a program reads, keep English SAN; the assistant's sentences keep
// the letters of their own language (commentary::PieceLetters).

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <chess.hpp>

#include "i18n.hpp"

namespace fcg {

// The letters of the king, the queen, the rook, the bishop and the knight in `language`, in that order: English's for
// Chinese, which has none in use.
[[nodiscard]] const std::array<std::string_view, 5>& pieceLetters(i18n::Language language);

// A SAN in the letters of `language`: its piece and the piece it promotes to, its files, castling and signs as they are
// ("Nf3" is "Sf3" in German and "Кf3" in Russian, "Kxe2" "Крxe2"; "e8=Q+" is "e8=D+" in French).
[[nodiscard]] std::string sanIn(std::string_view san, i18n::Language language);

// What was typed read as an English SAN: the letters of `language` as the pieces they name, the longest first ("Кр" the
// king, "К" the knight), a '0' as castling's 'O'.
[[nodiscard]] std::string englishTyped(std::string_view typed, i18n::Language language);

// A legal move a text typed may be.
struct TypedMove {
    chess::Move move;
    std::string san;    // in English letters
    std::string shown;  // in the letters of the language
    // Found by English letters that name another piece in the language ("R" the rook in English, the king in French):
    // the piece they name, to say so ("rook (English)").
    std::optional<chess::PieceType> english;
};

// The legal moves of `board` that `typed` begins, in the order they are offered: those it begins in the letters of
// `language` first, the moves of the piece whose letter it is before those whose longer letter it begins ("К" the
// knight's moves, then the king's, "Кр"); then those it begins in English letters only. Within each, the moves that go
// furthest forward first ("Sg" offers Sg5 before Sg1), a queen first among promotions. A '0' typed for castling's 'O',
// and the symbols typed after a move (! ?), are read as they are meant.
[[nodiscard]] std::vector<TypedMove> typedMoves(const chess::Board& board, std::string_view typed,
                                                i18n::Language language);

// Whether a move typed in `language` can begin with the character `c`: a file, castling's O (or 0), the first character
// of a piece letter of the language, or an English piece letter.
[[nodiscard]] bool beginsMove(char32_t c, i18n::Language language);

}  // namespace fcg
