#pragma once

// The Zobrist keys of Polyglot opening books, the ones chess.hpp hashes positions with: a position's key here is the
// key chess::Board::hash() gives it and the one a Polyglot book is searched by.

#include <cstdint>

namespace fcg::study::zobrist {

extern const uint64_t KEYS[781];

// Polyglot's pieces: black pawn 0, white pawn 1, black knight 2, … white king 11.
[[nodiscard]] inline uint64_t piece(int polyglot_piece, int square) { return KEYS[64 * polyglot_piece + square]; }
[[nodiscard]] inline uint64_t castling(int right) { return KEYS[768 + right]; }  // 0 K, 1 Q, 2 k, 3 q
[[nodiscard]] inline uint64_t enPassant(int file) { return KEYS[772 + file]; }
[[nodiscard]] inline uint64_t whiteToMove() { return KEYS[780]; }

}  // namespace fcg::study::zobrist
