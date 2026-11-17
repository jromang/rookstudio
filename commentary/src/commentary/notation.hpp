#pragma once

// Move notation in the reader's language (the prototype's crates/game/src/notation.rs), and how a move and a line are
// printed in a comment (crates/commentary/src/lib.rs: move_text, line_text).

#include <cstdint>
#include <span>
#include <string>

#include "position.hpp"

namespace fcg::commentary {

// How pieces are written in moves.
enum class PieceLetters : uint8_t {
    French,     // R D T F C
    English,    // K Q R B N
    Figurines,  // ♚ ♛ ♜ ♝ ♞
};

[[nodiscard]] const char* pieceLetter(PieceLetters letters, Role role) noexcept;
// SAN with its "+" or "#", the pieces written with `letters`.
[[nodiscard]] std::string san(const Position& before, const Move& mv, PieceLetters letters);
// "12." before a White move; "12…" before a Black move.
[[nodiscard]] std::string moveNumber(uint32_t fullmove, Color mover);
// "23.Td1" or "23…Td1", in the reader's notation.
[[nodiscard]] std::string moveText(const Position& before, const Move& mv, PieceLetters letters);
// A line the way it is printed: "10.Rxh2 Bxh2 11.Bg5", "13…Nxd4+ 14.Ke3 Be5"; at most `plies` moves.
[[nodiscard]] std::string lineText(const Position& start, std::span<const Move> moves, std::size_t plies,
                                   PieceLetters letters);

}  // namespace fcg::commentary
