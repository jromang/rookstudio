// Ported from cbvault 0.1.6, MIT License, Copyright (c) 2026 cbvault Contributors (cbvault-format: cbh/, game/,
// tables.rs; cbvault-chess: decode.rs, pieces.rs, start.rs). cbvault's readers of this format are themselves ported
// from oschess-cb-bridge (MIT License, Copyright (c) 2026 the oschess-cb-bridge contributors).
//
// The older ChessBase format. Its files share a name:
// - .cbh: a 46-byte header, then a 46-byte record per game or text (big-endian): what it is (bit 0 a game or a text,
//   bit 1 a text, bit 7 deleted), where its moves are in .cbg and its annotations in .cba, its players, tournament,
//   annotator and source by their number in the name files, its date, result, round, ratings, ECO and medals;
// - .cbg: per game, a byte of flags (its low six bits the encoding of its moves, bit 6 a start position follows, bit 7
//   a text), its size in three bytes, the 28 bytes of its start position when it has one (8 more naming the castling
//   squares of a Chess960 game), then its moves;
// - .cba: per annotated game, a 14-byte head (the game's number, 01 00 0e 0e, the annotations' count plus one, the
//   record's size), then each annotation: the move it belongs to (three bytes, -1 the game), its type, its size, its
//   data;
// - .cbp, .cbt, .cbc, .cbs, .cbe: players, tournaments, annotators, sources and teams, a small little-endian header
//   then records of fixed size, a name's number its record's place;
// - .cbj, when there is one: a longer record per game, its teams' numbers, and its moves' and annotations' places in
//   64 bits for files past 4 GiB.
//
// Moves are coded by the piece that moves and how, "the second rook, three squares up", the bytes translated through
// the table of the game's encoding, shifted by the number of moves decoded before: the walk keeps the lists of each
// side's pieces, numbered in the order a scan of the start position finds them. A variation is announced before the
// move it is an alternative to; the walk goes on to the end of the line, then takes up the alternative met last.

#include <algorithm>
#include <array>
#include <cstring>

#include <tour/chess_text.hpp>

#include "chessbase_detail.hpp"

namespace fcg::study::chessbase {

namespace {

constexpr std::size_t RECORD              = 46;
constexpr uint64_t MIN_FILE_HEADER        = 10;
constexpr std::size_t MAX_ANNOTATIONS     = 16 << 20;
constexpr std::size_t MAX_VARIATION_DEPTH = 1024;
constexpr int32_t ENTITY_MAGIC            = 1234567890;
constexpr int32_t ENTITY_DELETED          = -999;
constexpr uint32_t NO_ENTITY              = 0xFFFFFFFF;

// The byte translation tables of the move encodings, each a permutation of 0-255.
constexpr std::array<uint8_t, 256> MODE_0 = {
    0xa2, 0x95, 0x43, 0xf5, 0xc1, 0x3d, 0x4a, 0x6c, 0x53, 0x83, 0xcc, 0x7c, 0xff, 0xae, 0x68, 0xad, 0xd1, 0x92, 0x8b,
    0x8d, 0x35, 0x81, 0x5e, 0x74, 0x26, 0x8e, 0xab, 0xca, 0xfd, 0x9a, 0xf3, 0xa0, 0xa5, 0x15, 0xfc, 0xb1, 0x1e, 0xed,
    0x30, 0xea, 0x22, 0xeb, 0xa7, 0xcd, 0x4e, 0x6f, 0x2e, 0x24, 0x32, 0x94, 0x41, 0x8c, 0x6e, 0x58, 0x82, 0x50, 0xbb,
    0x02, 0x8a, 0xd8, 0xfa, 0x60, 0xde, 0x52, 0xba, 0x46, 0xac, 0x29, 0x9d, 0xd7, 0xdf, 0x08, 0x21, 0x01, 0x66, 0xa3,
    0xf1, 0x19, 0x27, 0xb5, 0x91, 0xd5, 0x42, 0x0e, 0xb4, 0x4c, 0xd9, 0x18, 0x5f, 0xbc, 0x25, 0xa6, 0x96, 0x04, 0x56,
    0x6a, 0xaa, 0x33, 0x1c, 0x2b, 0x73, 0xf0, 0xdd, 0xa4, 0x37, 0xd3, 0xc5, 0x10, 0xbf, 0x5a, 0x23, 0x34, 0x75, 0x5b,
    0xb8, 0x55, 0xd2, 0x6b, 0x09, 0x3a, 0x57, 0x12, 0xb3, 0x77, 0x48, 0x85, 0x9b, 0x0f, 0x9e, 0xc7, 0xc8, 0xa1, 0x7f,
    0x7a, 0xc0, 0xbd, 0x31, 0x6d, 0xf6, 0x3e, 0xc3, 0x11, 0x71, 0xce, 0x7d, 0xda, 0xa8, 0x54, 0x90, 0x97, 0x1f, 0x44,
    0x40, 0x16, 0xc9, 0xe3, 0x2c, 0xcb, 0x84, 0xec, 0x9f, 0x3f, 0x5c, 0xe6, 0x76, 0x0b, 0x3c, 0x20, 0xb7, 0x36, 0x00,
    0xdc, 0xe7, 0xf9, 0x4f, 0xf7, 0xaf, 0x06, 0x07, 0xe0, 0x1a, 0x0a, 0xa9, 0x4b, 0x0c, 0xd6, 0x63, 0x87, 0x89, 0x1d,
    0x13, 0x1b, 0xe4, 0x70, 0x05, 0x47, 0x67, 0x7b, 0x2f, 0xee, 0xe2, 0xe8, 0x98, 0x0d, 0xef, 0xcf, 0xc4, 0xf4, 0xfb,
    0xb0, 0x17, 0x99, 0x64, 0xf2, 0xd4, 0x2a, 0x03, 0x4d, 0x78, 0xc6, 0xfe, 0x65, 0x86, 0x88, 0x79, 0x45, 0x3b, 0xe5,
    0x49, 0x8f, 0x2d, 0xb9, 0xbe, 0x62, 0x93, 0x14, 0xe9, 0xd0, 0x38, 0x9c, 0xb2, 0xc2, 0x59, 0x5d, 0xb6, 0x72, 0x51,
    0xf8, 0x28, 0x7e, 0x61, 0x39, 0xe1, 0xdb, 0x69, 0x80,
};
constexpr std::array<uint8_t, 256> MODE_4 = {
    0x58, 0xa3, 0xee, 0x72, 0x47, 0xb9, 0x1f, 0xa0, 0x26, 0xfd, 0xdc, 0x6d, 0xf1, 0xe3, 0xcb, 0xc5, 0xe2, 0xd0, 0x29,
    0xe7, 0x89, 0xe1, 0xae, 0x7b, 0x34, 0x41, 0x35, 0x4b, 0xa5, 0x7c, 0xf6, 0xe5, 0xdb, 0x8c, 0x5f, 0x37, 0x05, 0xbb,
    0x32, 0x54, 0x4c, 0xef, 0x5d, 0x2a, 0x7f, 0xd3, 0x81, 0x86, 0xdf, 0x18, 0xc3, 0x44, 0x16, 0xf7, 0x85, 0x0f, 0x4d,
    0x30, 0x79, 0x14, 0x45, 0xaa, 0xa2, 0x6c, 0x15, 0x92, 0xa6, 0x62, 0x48, 0x19, 0xe9, 0x3c, 0x38, 0x94, 0x28, 0xa8,
    0x66, 0x09, 0x03, 0x63, 0x20, 0x7d, 0x36, 0xd7, 0x2e, 0x98, 0xec, 0xda, 0x01, 0x13, 0x24, 0x73, 0xcd, 0x9e, 0xcf,
    0xd9, 0xd5, 0x68, 0x8b, 0xd2, 0xab, 0x5b, 0x69, 0xb0, 0x27, 0xfc, 0x31, 0x1b, 0x53, 0x7e, 0x65, 0x76, 0x55, 0x75,
    0xaf, 0x0a, 0xcc, 0xc7, 0xc4, 0xb2, 0xb5, 0x22, 0xe8, 0xa7, 0xbe, 0xf5, 0x00, 0x5a, 0x21, 0xd1, 0x74, 0x8e, 0xde,
    0x9c, 0xe4, 0x42, 0x2b, 0xb1, 0x9b, 0x93, 0xc1, 0x78, 0x70, 0xd6, 0xb6, 0xad, 0x4e, 0x6a, 0x64, 0x7a, 0x57, 0x49,
    0x11, 0x9d, 0x12, 0xc0, 0x3a, 0x2f, 0xe6, 0x84, 0xc6, 0x07, 0xce, 0x08, 0x82, 0x04, 0x90, 0xb8, 0x87, 0xca, 0x6e,
    0x1e, 0x40, 0x1a, 0x50, 0xb4, 0x10, 0xbc, 0x43, 0x8a, 0x71, 0x0b, 0x8d, 0x5c, 0x3b, 0x3f, 0x88, 0xba, 0xf4, 0xf3,
    0x61, 0x1d, 0xf9, 0xfe, 0x25, 0x51, 0x0d, 0xeb, 0xc8, 0x59, 0x17, 0x46, 0xa4, 0xf2, 0x56, 0xb7, 0x02, 0x2d, 0x33,
    0x9f, 0xd8, 0x83, 0xfb, 0xbf, 0xea, 0xed, 0xac, 0x60, 0x67, 0x39, 0xdd, 0x96, 0x6b, 0xa9, 0xc2, 0x77, 0x0c, 0x0e,
    0x91, 0xfa, 0x99, 0x9a, 0xe0, 0xa1, 0xf0, 0x5e, 0xc9, 0x06, 0x4f, 0xbd, 0x52, 0x80, 0xd4, 0xf8, 0x97, 0x23, 0x3e,
    0x1c, 0x2c, 0x95, 0x4a, 0x8f, 0x3d, 0x6f, 0xff, 0xb3,
};
constexpr std::array<uint8_t, 256> MODE_5 = {
    0xe0, 0x1b, 0x97, 0x86, 0xa2, 0x48, 0xf0, 0x23, 0x01, 0x37, 0xfa, 0xeb, 0x50, 0x76, 0x5d, 0xcd, 0x80, 0x1a, 0x31,
    0x5c, 0xff, 0xbc, 0x78, 0xda, 0xe5, 0xa3, 0x75, 0xb3, 0x71, 0xe1, 0xa1, 0x0e, 0x56, 0x41, 0x3f, 0xe3, 0x0d, 0xc4,
    0x25, 0xe6, 0x88, 0x89, 0x70, 0xdc, 0x99, 0xa6, 0x0b, 0x9a, 0x14, 0x2c, 0x57, 0x9e, 0x69, 0x7a, 0xdf, 0x55, 0xad,
    0x73, 0x7c, 0xc5, 0x03, 0x72, 0xf5, 0x45, 0x62, 0x8e, 0xaf, 0x5f, 0xcc, 0xc0, 0x5e, 0xf3, 0x18, 0x13, 0xb5, 0x38,
    0x09, 0x28, 0x3c, 0xbd, 0x10, 0x8b, 0xe9, 0xdd, 0x8c, 0x94, 0x08, 0x81, 0xa7, 0xfe, 0x24, 0x79, 0x44, 0x2b, 0xc1,
    0x36, 0x32, 0x7f, 0x67, 0xe4, 0xfd, 0x49, 0x4b, 0x21, 0xa9, 0x5b, 0x77, 0x1f, 0x1c, 0xf6, 0x40, 0x65, 0x3d, 0xf9,
    0x5a, 0xf8, 0x47, 0x26, 0x53, 0xd4, 0x20, 0xd2, 0xd8, 0x4f, 0x27, 0xae, 0xfb, 0xb6, 0x9b, 0x74, 0xb8, 0x42, 0xfc,
    0x16, 0x6f, 0xd7, 0x9f, 0xb9, 0x7b, 0x60, 0xde, 0xc2, 0x46, 0x06, 0x9c, 0xd3, 0x2a, 0xc7, 0x05, 0x85, 0x3e, 0xb1,
    0x00, 0x15, 0xd1, 0x96, 0xce, 0x90, 0x95, 0x11, 0x33, 0x8f, 0xc6, 0x82, 0x84, 0xb7, 0x39, 0xa0, 0xe8, 0x34, 0x1e,
    0x7d, 0xa8, 0xd0, 0xea, 0xe2, 0xef, 0x4c, 0xc3, 0x58, 0x4e, 0x02, 0x1d, 0xb2, 0x7e, 0x98, 0x2d, 0x07, 0xd5, 0xca,
    0x68, 0xcb, 0x30, 0xb4, 0xac, 0x83, 0x63, 0xec, 0x87, 0xee, 0x2e, 0xbe, 0xcf, 0x6d, 0x4d, 0x8a, 0xd6, 0x04, 0xb0,
    0x35, 0xc8, 0xc9, 0x12, 0x17, 0x61, 0x8d, 0x64, 0x0f, 0xa5, 0x54, 0x29, 0x3a, 0x4a, 0x6b, 0xbb, 0x2f, 0xd9, 0x3b,
    0x93, 0x6a, 0xaa, 0xa4, 0x22, 0xf4, 0x19, 0x43, 0x6e, 0xf1, 0xf7, 0xdb, 0x92, 0xed, 0x66, 0x59, 0x0a, 0xbf, 0x6c,
    0x51, 0x91, 0x52, 0xf2, 0x0c, 0x9d, 0xab, 0xba, 0xe7,
};
constexpr std::array<uint8_t, 256> MODE_10 = {
    0x38, 0x50, 0x64, 0xcc, 0x9f, 0x56, 0x6f, 0x73, 0x4d, 0x41, 0x66, 0xa2, 0xc6, 0xb9, 0x96, 0x47, 0xee, 0xcf, 0xb1,
    0x6d, 0x48, 0x4f, 0x44, 0x20, 0x15, 0xbe, 0x82, 0x09, 0x34, 0x99, 0xeb, 0x1a, 0x33, 0x7c, 0xab, 0x59, 0x00, 0x22,
    0x24, 0xb0, 0xb4, 0xfa, 0x85, 0xdd, 0x57, 0xf3, 0xd9, 0xd2, 0x8a, 0x8d, 0x70, 0xb5, 0x6e, 0x43, 0xa7, 0x0b, 0x27,
    0x08, 0xea, 0xb3, 0x94, 0x1c, 0x63, 0x1b, 0x2b, 0xc5, 0x0c, 0xd4, 0x3f, 0x37, 0xe8, 0xde, 0xa6, 0x23, 0xef, 0x06,
    0xfd, 0x3e, 0xc1, 0x40, 0x32, 0xdf, 0x2d, 0xc8, 0xd0, 0x0f, 0xf1, 0xe2, 0x7a, 0x95, 0xec, 0x3c, 0xca, 0x49, 0x11,
    0xe0, 0x25, 0xd8, 0x30, 0xfe, 0x2e, 0xba, 0xdc, 0xaa, 0x53, 0x4c, 0xd1, 0x5e, 0x67, 0x8b, 0x0d, 0x9e, 0xa9, 0xbf,
    0x05, 0x78, 0x35, 0xc4, 0xb6, 0xa4, 0x4b, 0xd6, 0x04, 0x52, 0xa8, 0x76, 0x2c, 0xbd, 0xae, 0x39, 0xb2, 0xe3, 0x03,
    0x81, 0x17, 0x9a, 0x3d, 0x68, 0xc0, 0x65, 0x92, 0xc9, 0xf7, 0x77, 0x16, 0xff, 0xf5, 0xe1, 0x1e, 0x36, 0xbb, 0x86,
    0x8e, 0xf9, 0x9d, 0xd5, 0x6b, 0xf4, 0x74, 0x80, 0x2f, 0x7b, 0x14, 0x97, 0x29, 0xe5, 0x4a, 0x02, 0x93, 0x5c, 0xcb,
    0xa1, 0x58, 0x79, 0xf8, 0x5f, 0x26, 0x28, 0x8f, 0x7e, 0xa5, 0xed, 0xd3, 0x89, 0xc7, 0x88, 0x9b, 0x54, 0x87, 0x60,
    0xda, 0xf0, 0x45, 0x3a, 0xc2, 0x84, 0x90, 0x21, 0xf6, 0xf2, 0x5a, 0x2a, 0x55, 0x18, 0x61, 0x01, 0xfb, 0xe6, 0x8c,
    0x6a, 0x12, 0xb8, 0x62, 0x83, 0x98, 0x10, 0x71, 0x5b, 0xe4, 0xad, 0xdb, 0x42, 0x0a, 0xcd, 0x46, 0xd7, 0x72, 0x07,
    0xce, 0x19, 0xb7, 0x3b, 0x31, 0x9c, 0x51, 0xc3, 0x69, 0xa3, 0x91, 0xe9, 0x0e, 0x1f, 0xfc, 0x75, 0x6c, 0x1d, 0x7d,
    0xe7, 0xaf, 0x5d, 0xa0, 0xbc, 0x13, 0x7f, 0xac, 0x4e,
};

// Squares are numbered file by file here (a1 0, a2 1 ... h8 63); chess.hpp's rank by rank.
int fromCb(int cb) { return (cb % 8) * 8 + cb / 8; }
int toCb(int square) { return (square % 8) * 8 + square / 8; }

std::string at(const std::string& file, uint64_t offset) { return file + " at byte " + std::to_string(offset); }

// ---------------------------------------------------------------------------------------------------------------
// The piece lists of the move encoding: queens, rooks, bishops and knights of each side, in the order a scan of the
// start position finds them; a piece taken makes the ones after it move down; a pawn keeps its number all game.
// ---------------------------------------------------------------------------------------------------------------

constexpr int QUEEN = 0, ROOK = 1, BISHOP = 2, KNIGHT = 3;

int kindIndex(chess::PieceType type) {
    switch (static_cast<int>(type)) {
        case 4:
            return QUEEN;
        case 3:
            return ROOK;
        case 2:
            return BISHOP;
        case 1:
            return KNIGHT;
        default:
            return -1;
    }
}

struct Pieces {
    struct Kind {
        std::array<uint8_t, 10> squares{};
        uint8_t count = 0;
        [[nodiscard]] int find(int cb) const {
            for (int i = 0; i < count; ++i)
                if (squares[i] == cb) return i;
            return -1;
        }
        bool push(int cb) {
            if (count == squares.size()) return false;
            squares[count++] = static_cast<uint8_t>(cb);
            return true;
        }
        bool remove(int cb) {
            const int i = find(cb);
            if (i < 0) return false;
            std::copy(squares.begin() + i + 1, squares.begin() + count, squares.begin() + i);
            --count;
            return true;
        }
        bool relocate(int from, int to) {
            const int i = find(from);
            if (i < 0) return false;
            squares[i] = static_cast<uint8_t>(to);
            return true;
        }
    };
    std::array<std::array<Kind, 4>, 2> kinds{};  // [White, Black][queens, rooks, bishops, knights]
    std::array<std::array<int8_t, 8>, 2> pawns{};

    static std::optional<Pieces> scan(const chess::Board& board) {
        Pieces p;
        for (auto& side : p.pawns) side.fill(-1);
        std::array<int, 2> next{};
        for (int cb = 0; cb < 64; ++cb) {
            const auto piece = board.at(chess::Square(fromCb(cb)));
            if (piece == chess::Piece::NONE) continue;
            const int side = piece.color() == chess::Color::WHITE ? 0 : 1;
            if (piece.type() == chess::PieceType::PAWN) {
                if (next[side] == 8) return std::nullopt;
                p.pawns[side][next[side]++] = static_cast<int8_t>(cb);
            } else if (const int k = kindIndex(piece.type()); k >= 0 && !p.kinds[side][k].push(cb)) {
                return std::nullopt;
            }
        }
        return p;
    }

    // `move`, played by the side to move of `before`: false when the lists do not match the board.
    bool update(const chess::Board& before, chess::Move move) {
        const int me = before.sideToMove() == chess::Color::WHITE ? 0 : 1, them = 1 - me;
        const int from = move.from().index(), to = move.to().index();
        const auto moving = before.at(move.from()).type();
        if (move.typeOf() == chess::Move::CASTLING) {
            // The king onto its own rook: the rook lands beside the king's square.
            const int rank   = to / 8;
            const int rookTo = rank * 8 + (to % 8 > from % 8 ? 5 : 3);
            return kinds[me][ROOK].relocate(toCb(to), toCb(rookTo));
        }
        const auto target           = before.at(move.to());
        chess::PieceType taken_type = chess::PieceType::NONE;
        int taken_at                = -1;
        if (target != chess::Piece::NONE && target.color() != before.sideToMove()) {
            taken_type = target.type();
            taken_at   = toCb(to);
        } else if (moving == chess::PieceType::PAWN && from % 8 != to % 8) {
            taken_type = chess::PieceType::PAWN;
            taken_at   = toCb((from / 8) * 8 + to % 8);
        }
        if (taken_at >= 0) {
            bool removed = false;
            if (const int k = kindIndex(taken_type); k >= 0) {
                removed = kinds[them][k].remove(taken_at);
            } else if (taken_type == chess::PieceType::PAWN) {
                for (auto& s : pawns[them])
                    if (s == taken_at) {
                        s       = -1;
                        removed = true;
                        break;
                    }
            }
            if (!removed && taken_type != chess::PieceType::KING) return false;
        }
        if (moving == chess::PieceType::PAWN) {
            for (auto& s : pawns[me]) {
                if (s != toCb(from)) continue;
                if (move.typeOf() == chess::Move::PROMOTION) {
                    s = -1;
                    return kinds[me][kindIndex(move.promotionType())].push(toCb(to));
                }
                s = static_cast<int8_t>(toCb(to));
                return true;
            }
            return false;
        }
        if (const int k = kindIndex(moving); k >= 0) return kinds[me][k].relocate(toCb(from), toCb(to));
        return moving == chess::PieceType::KING;
    }
};

// ---------------------------------------------------------------------------------------------------------------
// Where a game starts: the 28 bytes of a set-up position (a marker; the side to move in bit 4 and the en passant file
// plus one in the low bits; the castling rights in four bits, White's queen side first; the move number; then the board
// file by file from a1, an empty square one 0 bit, a piece a 1 bit, its colour's bit and three bits of its kind), and
// the 8 bytes of a Chess960 game: the kings' squares, the castling rooks' and its position's number.
// ---------------------------------------------------------------------------------------------------------------

struct Setup {
    std::array<char, 64> board{};  // FEN letters by square (rank * 8 + file), 0 empty
    bool black       = false;
    int ep_file      = -1;
    uint8_t castling = 0;  // bits: White's queen side, White's king side, Black's queen side, Black's king side
    int move_number  = 1;
    bool chess960    = false;
    std::array<int, 2> kings{-1, -1};          // files the 8 bytes name
    std::array<int, 4> rooks{-1, -1, -1, -1};  // in the order of the castling bits
};

int castlingBit(bool white, bool king_side) { return (white ? 0 : 2) + (king_side ? 1 : 0); }

// The file of the rook a castling right needs, -1 when the position cannot hold the right.
int rookFile(const Setup& s, bool white, bool king_side) {
    const int back  = white ? 0 : 7;
    const char king = white ? 'K' : 'k', rook = white ? 'R' : 'r';
    int king_file = -1;
    for (int f = 0; f < 8; ++f)
        if (s.board[back * 8 + f] == king) king_file = f;
    if (king_file < 0) return -1;
    const int side = white ? 0 : 1;
    if (s.kings[side] >= 0 && s.kings[side] != king_file) return -1;
    const auto wing = [&](int f) { return s.board[back * 8 + f] == rook && (f > king_file) == king_side; };
    const int named = s.rooks[castlingBit(white, king_side)];
    if (named >= 0 && wing(named)) return named;
    if (s.chess960) {
        int found = -1;
        for (int f = 0; f < 8; ++f)
            if (wing(f) && (found < 0 || king_side)) found = f;
        return found;
    }
    const int home = king_side ? 7 : 0;
    return king_file == 4 && wing(home) ? home : -1;
}

// The start section; none when its board holds a piece of an unknown kind.
std::optional<Setup> decodeStart(std::span<const uint8_t> start, std::span<const uint8_t> extra) {
    Setup s;
    std::size_t pos = 0;
    const auto bit  = [&]() -> int {
        const std::size_t byte = 4 + pos / 8;
        if (byte >= start.size()) return -1;
        const int b = (start[byte] >> (7 - pos % 8)) & 1;
        ++pos;
        return b;
    };
    static constexpr char LETTERS[] = " KQNBRP";
    for (int cb = 0; cb < 64; ++cb) {
        const int occupied = bit();
        if (occupied < 0) return std::nullopt;
        if (occupied == 0) continue;
        const int colour = bit(), b2 = bit(), b1 = bit(), b0 = bit();
        if (colour < 0 || b0 < 0) return std::nullopt;
        const int code = b2 << 2 | b1 << 1 | b0;
        if (code < 1 || code > 6) return std::nullopt;
        s.board[fromCb(cb)] = static_cast<char>(LETTERS[code] + (colour ? 32 : 0));
    }
    s.black       = start[1] & 0x10;
    const int ep  = start[1] & 0x0F;
    s.ep_file     = ep >= 1 && ep <= 8 ? ep - 1 : -1;
    s.castling    = start[2] & 0x0F;
    s.move_number = start[3];
    if (!extra.empty()) {
        // An untouched Chess960 start is its number's placement, its rooks those of the placement.
        s.chess960           = true;
        const int n          = static_cast<int>(be16(extra.data() + 6));
        const bool untouched = !s.black && s.castling == 0x0F && ep == 0 && s.move_number <= 1;
        if (untouched && n < 960 && chess960Placement(n) == s.board) return s;
        const auto file = [&](int i, int rank) { return extra[i] < 64 && extra[i] % 8 == rank ? extra[i] / 8 : -1; };
        s.kings         = {file(0, 0), file(1, 7)};
        s.rooks         = {file(3, 0), file(2, 0), file(5, 7), file(4, 7)};
    }
    return s;
}

std::string fenOf(const Setup& s) {
    std::string fen;
    for (int rank = 7; rank >= 0; --rank) {
        int empty = 0;
        for (int f = 0; f < 8; ++f) {
            const char c = s.board[rank * 8 + f];
            if (!c) {
                ++empty;
                continue;
            }
            if (empty) fen += static_cast<char>('0' + empty);
            empty = 0;
            fen += c;
        }
        if (empty) fen += static_cast<char>('0' + empty);
        if (rank) fen += '/';
    }
    fen += s.black ? " b " : " w ";
    std::string castling;
    for (const bool white : {true, false})
        for (const bool king_side : {true, false}) {
            if (!(s.castling & (1 << castlingBit(white, king_side)))) continue;
            const int file = rookFile(s, white, king_side);
            if (file < 0) continue;
            const char letter = s.chess960 ? static_cast<char>('A' + file) : (king_side ? 'K' : 'Q');
            castling += white ? letter : static_cast<char>(letter + 32);
        }
    fen += castling.empty() ? "-" : castling;
    // En passant only where a pawn of the side to move can take the pawn that went two squares.
    std::string ep = "-";
    if (s.ep_file >= 0) {
        const int pawn_rank = s.black ? 3 : 4;
        const char theirs = s.black ? 'P' : 'p', ours = s.black ? 'p' : 'P';
        const auto has = [&](int f, char c) { return f >= 0 && f < 8 && s.board[pawn_rank * 8 + f] == c; };
        if (has(s.ep_file, theirs) && (has(s.ep_file - 1, ours) || has(s.ep_file + 1, ours)))
            ep = std::string{static_cast<char>('a' + s.ep_file), static_cast<char>(s.black ? '3' : '6')};
    }
    fen += " " + ep + " 0 " + std::to_string(std::max(1, s.move_number));
    return fen;
}

// ---------------------------------------------------------------------------------------------------------------
// The name files
// ---------------------------------------------------------------------------------------------------------------

class EntityFile {
   public:
    EntityFile(std::unique_ptr<Source> source, std::size_t min_data) : source_(std::move(source)) {
        const auto& name = source_->name();
        if (source_->size() < 28) throw Damage(name + ": shorter than its header");
        const auto h = source_->read(0, 28);
        if (static_cast<int32_t>(le32(h.data() + 8)) != ENTITY_MAGIC) throw Damage(name + ": not a file of names");
        const auto data  = static_cast<int32_t>(le32(h.data() + 12));
        const auto extra = static_cast<int32_t>(le32(h.data() + 24));
        if (data < static_cast<int32_t>(min_data) || data > (64 << 10))
            throw Damage(name + ": records of " + std::to_string(data) + " bytes");
        if (extra != 0 && extra != 4) throw Damage(name + ": a header of an unknown size");
        header_ = 28 + static_cast<uint64_t>(extra);
        record_ = 9 + static_cast<uint64_t>(data);
        count_  = source_->size() > header_ ? (source_->size() - header_) / record_ : 0;
    }

    // The data of name `id`: none past the file or for a deleted record.
    std::optional<std::span<const uint8_t>> data(uint32_t id) {
        if (id >= count_) return std::nullopt;
        const auto r = source_->read(header_ + id * record_, record_);
        if (static_cast<int32_t>(le32(r.data())) == ENTITY_DELETED) return std::nullopt;
        return r.subspan(9);
    }

   private:
    std::unique_ptr<Source> source_;
    uint64_t header_ = 0, record_ = 0, count_ = 0;
};

// ---------------------------------------------------------------------------------------------------------------
// The reader
// ---------------------------------------------------------------------------------------------------------------

const char* const KIND_NAMES[4] = {"queen", "rook", "bishop", "knight"};

uint16_t languageOf(uint8_t nation) {
    switch (nation) {
        case 0:
            return ANY_LANGUAGE;
        case 42:
            return 0;  // English
        case 53:
            return 1;  // German
        case 49:
            return 2;  // French
        case 43:
            return 3;  // Spanish
        case 70:
            return 4;  // Italian
        case 103:
            return 5;  // Dutch
        case 117:
            return 6;  // Portuguese
        case 116:
            return 12;  // Polish
        case 55:
            return 18;  // Greek
        default:
            return static_cast<uint16_t>(0x100 + nation);
    }
}

std::optional<char> colourOf(uint8_t c) {
    switch (c) {
        case 2:
            return 'G';
        case 3:
            return 'Y';
        case 4:
            return 'R';
        case 7:
            return 'B';
        default:
            return std::nullopt;
    }
}

class CbhReader final : public FormatReader {
   public:
    explicit CbhReader(std::unique_ptr<Members> members) : members_(std::move(members)) {
        const auto label = members_->label();
        const auto need  = [&](const char* ext, const char* what) {
            auto s = members_->open(ext, false);
            if (!s)
                throw std::runtime_error(label + ext + " is missing: the ChessBase database keeps " + what + " in it");
            return s;
        };
        const auto names = [&](const char* ext, const char* what) {
            auto s = members_->open(ext, true);
            if (!s)
                throw std::runtime_error(label + ext + " is missing: the ChessBase database keeps " + what + " in it");
            return s;
        };
        headers_ = need(".cbh", "its games' headers");
        moves_   = need(".cbg", "its games' moves");
        notes_   = need(".cba", "its games' annotations");
        players_.emplace(names(".cbp", "its players"), 50);
        tournaments_.emplace(names(".cbt", "its tournaments"), 0x4a);
        annotators_.emplace(names(".cbc", "its annotators"), 45);
        sources_.emplace(names(".cbs", "its sources"), 25);  // part of the format, not a tag here
        if (auto teams = members_->open(".cbe", true)) teams_.emplace(std::move(teams), 50);
        if (auto wide = members_->open(".cbj", false)) openWide(std::move(wide));

        const uint64_t size = headers_->size();
        if (size < RECORD || size % RECORD != 0)
            throw std::runtime_error(headers_->name() + ": damaged (" + std::to_string(size) +
                                     " bytes is not a header and whole records of 46 bytes)");
        const auto head = headers_->read(0, RECORD);
        if (be16(head.data() + 3) != RECORD)
            throw std::runtime_error(headers_->name() +
                                     ": not a ChessBase header file (its records are not of 46 "
                                     "bytes)");
        records_ = static_cast<uint32_t>(std::min<uint64_t>(size / RECORD - 1, UINT32_MAX));
    }

    [[nodiscard]] ChessBaseFormat format() const noexcept override { return ChessBaseFormat::CBH; }
    [[nodiscard]] uint32_t records() const noexcept override { return records_; }
    [[nodiscard]] uint64_t headerBytes() const noexcept override { return headers_->size(); }
    [[nodiscard]] uint64_t headerEnd(uint32_t record) const noexcept override {
        return std::min<uint64_t>((uint64_t{record} + 1) * RECORD, headers_->size());
    }

    RecordKind read(uint32_t id, RawGame& out) override {
        std::array<uint8_t, RECORD> b{};
        const auto r = headers_->read(uint64_t{id} * RECORD, RECORD);
        std::copy(r.begin(), r.end(), b.begin());
        if (!(b[0] & 1)) return RecordKind::Unknown;
        if (b[0] & 2) return RecordKind::Text;
        if (b[0] & 0x80) return RecordKind::Deleted;
        out.order = Order::Stored;

        uint64_t moves_at = be32(b.data() + 1), notes_at = be32(b.data() + 5);
        uint32_t white_team = NO_ENTITY, black_team = NO_ENTITY;
        readWide(id, moves_at, notes_at, white_team, black_team);

        tags(b, white_team, black_team, out);
        out.builder.reset(chess::Board());
        try {
            walk(id, movesRecord(moves_at), out);
        } catch (const Damage& e) {
            out.problem = moves_->name() + ": game " + std::to_string(id) + ": " + e.what();
        }
        if (notes_at != 0) {
            try {
                annotations(id, notes_at, out);
            } catch (const Damage& e) {
                if (out.problem.empty()) out.problem = e.what();
                out.notes.clear();
            }
        }
        return RecordKind::Game;
    }

   private:
    // .cbj: a longer record per game, its teams and its places in 64 bits (used when .cbg or .cba is past 4 GiB).
    void openWide(std::unique_ptr<Source> wide) {
        if (wide->size() < 32) return;
        const auto h       = wide->read(0, 32);
        const auto version = static_cast<int32_t>(le32(h.data()));
        const auto record  = static_cast<int32_t>(le32(h.data() + 4));
        const auto count   = static_cast<int32_t>(le32(h.data() + 8));
        if (record < 8 || record > 4096 || count < 0) return;
        wide_record_ = static_cast<uint64_t>(record);
        wide_count_  = static_cast<uint32_t>(count);
        wide_long_   = version >= 6 && record >= 0x26 &&
                       (moves_->size() >= (uint64_t{1} << 32) || notes_->size() >= (uint64_t{1} << 32));
        wide_        = std::move(wide);
    }

    void readWide(uint32_t id, uint64_t& moves_at, uint64_t& notes_at, uint32_t& white_team, uint32_t& black_team) {
        if (!wide_ || id == 0 || id > wide_count_) return;
        const uint64_t at = 32 + wide_record_ * (id - 1);
        if (at + wide_record_ > wide_->size()) return;
        const auto r = wide_->read(at, static_cast<std::size_t>(wide_record_));
        white_team   = be32(r.data());
        black_team   = be32(r.data() + 4);
        if (!wide_long_) return;
        const auto moves  = static_cast<int64_t>(be64(r.data() + 0x1E));
        const auto notes  = std::max<int64_t>(0, static_cast<int64_t>(be64(r.data() + 0x0C)));
        const auto agrees = [](int64_t wide, uint64_t narrow) {
            return wide >= 0 && (static_cast<uint64_t>(wide) & 0xFFFFFFFF) == narrow;
        };
        if (!agrees(moves, moves_at) || !agrees(notes, notes_at))
            throw Damage(wide_->name() + ": game " + std::to_string(id) + ": its places disagree with the header's");
        moves_at = static_cast<uint64_t>(moves);
        notes_at = static_cast<uint64_t>(notes);
    }

    void tags(const std::array<uint8_t, RECORD>& b, uint32_t white_team, uint32_t black_team, RawGame& out) {
        std::string event, site, event_date;
        if (const auto t = tournaments_->data(be24(b.data() + 0x0F))) {
            event = nameText(t->subspan(0, 40));
            site  = nameText(t->subspan(40, 30));
            // A tournament's date with its year alone is the year of its games: an EventDate only with its month.
            const uint32_t date = le32(t->data() + 0x46);
            if ((date >> 5) & 15) event_date = dateText(date);
        }
        const auto player = [&](uint32_t id) -> std::string {
            const auto p = players_->data(id);
            if (!p) return {};
            return playerText(nameText(p->subspan(0, 30)), nameText(p->subspan(30, 20)));
        };
        addTag(out, "Event", event);
        addTag(out, "Site", site);
        addTag(out, "Date", dateText(be24(b.data() + 0x18)));
        addTag(out, "Round", roundText(b[0x1D], b[0x1E]));
        addTag(out, "White", player(be24(b.data() + 0x09)));
        addTag(out, "Black", player(be24(b.data() + 0x0C)));
        addTag(out, "Result", resultText(b[0x1B]));
        if (const uint32_t elo = be16(b.data() + 0x1F)) addTag(out, "WhiteElo", std::to_string(elo));
        if (const uint32_t elo = be16(b.data() + 0x21)) addTag(out, "BlackElo", std::to_string(elo));
        addTag(out, "ECO", ecoText(be16(b.data() + 0x23)));
        if (const auto a = annotators_->data(be24(b.data() + 0x12)))
            addTag(out, "Annotator", nameText(a->subspan(0, 45)));
        if (teams_) {
            if (const auto t = white_team != NO_ENTITY ? teams_->data(white_team) : std::nullopt)
                addTag(out, "WhiteTeam", nameText(t->subspan(0, 50)));
            if (const auto t = black_team != NO_ENTITY ? teams_->data(black_team) : std::nullopt)
                addTag(out, "BlackTeam", nameText(t->subspan(0, 50)));
        }
        addTag(out, "EventDate", event_date);
        if (const uint32_t medals = be16(b.data() + 0x25)) addTag(out, "Medals", std::to_string(medals));
    }

    std::vector<uint8_t> movesRecord(uint64_t offset) {
        if (offset < MIN_FILE_HEADER || offset + 4 > moves_->size())
            throw Damage("no move record at byte " + std::to_string(offset));
        const auto head  = moves_->read(offset, 4);
        const uint32_t n = be24(head.data() + 1);
        if (n < 4 || offset + n > moves_->size())
            throw Damage("its move record at byte " + std::to_string(offset) + " runs past the file");
        const auto r = moves_->read(offset, n);
        return {r.begin(), r.end()};
    }

    // The walk of a game's moves (decode.rs).
    struct Walker {
        Walker(TreeBuilder& t, const Pieces& p) : tree(t), pieces(p) {}
        TreeBuilder& tree;
        Pieces pieces;
        std::vector<Pieces> stack;
        const std::array<uint8_t, 256>* table = nullptr;
        bool pre = false, chess960 = false, branch_next = false, ended = false;
        uint32_t plies = 0;
        int missing    = -1;  // the castling bit a castling move lacked

        [[nodiscard]] uint8_t translate(uint8_t byte) const {
            const auto n = static_cast<uint8_t>(plies);
            return pre ? (*table)[static_cast<uint8_t>(byte - n)] : static_cast<uint8_t>((*table)[byte] - n);
        }
        [[noreturn]] void fail(const std::string& what) const {
            throw Damage("move " + std::to_string(plies + 1) + ": " + what);
        }

        void startVariation() {
            if (branch_next) fail("two variations start in a row");
            if (tree.pendingCount() >= MAX_VARIATION_DEPTH) fail("variations nested too deep");
            branch_next = true;
        }
        void endLine() {
            if (branch_next) fail("a variation starts at the end of a line");
            if (tree.endLine()) {
                pieces = stack.back();
                stack.pop_back();
            } else {
                ended = true;
            }
        }
        void play(chess::Move move) {
            if (branch_next) stack.push_back(pieces);
            if (move != chess::Move(chess::Move::NULL_MOVE) && !pieces.update(tree.board(), move))
                fail("the piece lists do not match the board");
            tree.play(move);
            ++plies;
            if (branch_next) {
                branch_next = false;
                tree.branchLast();
            }
        }
        chess::Move castle(bool king_side) {
            const auto& b    = tree.board();
            const bool white = b.sideToMove() == chess::Color::WHITE;
            const auto move  = legalMove(b, -1, -1, -1, king_side ? 1 : 2);
            if (!move) {
                using Side = chess::Board::CastlingRights::Side;
                if (!b.castlingRights().has(b.sideToMove(), king_side ? Side::KING_SIDE : Side::QUEEN_SIDE))
                    missing = castlingBit(white, king_side);
                fail(std::string("castling (") + (king_side ? "O-O" : "O-O-O") + ") that is not legal");
            }
            return *move;
        }
        chess::Move ordinary(int from, int to, int promotion) {
            const auto& b    = tree.board();
            const auto piece = b.at(chess::Square(to));
            if (piece != chess::Piece::NONE && piece.color() == b.sideToMove())
                fail("a move onto a piece of the side to move");
            const auto move = legalMove(b, from, to, promotion);
            if (!move)
                fail("a move that is not legal (" + std::string(1, static_cast<char>('a' + from % 8)) +
                     std::to_string(from / 8 + 1) + "-" + std::string(1, static_cast<char>('a' + to % 8)) +
                     std::to_string(to / 8 + 1) + ")");
            return *move;
        }
        // From a square numbered file by file, by a step of (files, ranks), each taken modulo 8.
        chess::Move step(int from, std::pair<int, int> delta) {
            const int file = (from / 8 + delta.first) & 7, rank = (from % 8 + delta.second) & 7;
            return ordinary(fromCb(from), rank * 8 + file, -1);
        }
        // A move given by its squares, in two bytes: from and to in six bits each, a promotion's piece above them.
        chess::Move bySquares(uint32_t v) {
            const int from = static_cast<int>(v & 63), to = static_cast<int>((v >> 6) & 63);
            const int from_sq = fromCb(from), to_sq = fromCb(to);
            const auto& b  = tree.board();
            const int back = b.sideToMove() == chess::Color::WHITE ? 0 : 7;
            if (from == to) {
                if ((v & 0x0FFF) == 0) return chess::Move(chess::Move::NULL_MOVE);
                if (chess960 && to_sq / 8 == back && to_sq % 8 == 6) return castle(true);
                if (chess960 && to_sq / 8 == back && to_sq % 8 == 2) return castle(false);
                fail("a move from a square to itself");
            }
            const auto piece    = b.at(chess::Square(from_sq));
            const bool own      = piece != chess::Piece::NONE && piece.color() == b.sideToMove();
            const bool castling = !chess960 && from_sq == back * 8 + 4 && to_sq / 8 == back;
            if (own && piece.type() == chess::PieceType::KING && castling && to_sq % 8 == 6) return castle(true);
            if (own && piece.type() == chess::PieceType::KING && castling && to_sq % 8 == 2) return castle(false);
            if (own && piece.type() == chess::PieceType::PAWN && (to_sq / 8 == 0 || to_sq / 8 == 7)) {
                static constexpr int PROMOTIONS[4] = {4, 3, 2, 1};  // queen, rook, bishop, knight
                return ordinary(from_sq, to_sq, PROMOTIONS[(v >> 12) & 3]);
            }
            return ordinary(from_sq, to_sq, -1);
        }
        // A one-byte code: a piece of a kind, by its number, and its move.
        chess::Move byCode(uint8_t code) {
            static constexpr std::pair<int, int> KING[8]    = {{0, 1},  {1, 1},   {1, 0},  {1, -1},
                                                               {0, -1}, {-1, -1}, {-1, 0}, {-1, 1}};
            static constexpr std::pair<int, int> KNIGHTS[8] = {{2, 1},   {1, 2},   {-1, 2}, {-2, 1},
                                                               {-2, -1}, {-1, -2}, {1, -2}, {2, -1}};
            static constexpr std::pair<int, int> Q[4]       = {{0, 1}, {1, 0}, {1, 1}, {1, -1}};
            static constexpr std::pair<int, int> R[2]       = {{0, 1}, {1, 0}};
            static constexpr std::pair<int, int> B[2]       = {{1, 1}, {1, -1}};
            const auto& b                                   = tree.board();
            const int us                                    = b.sideToMove() == chess::Color::WHITE ? 0 : 1;
            const auto line                                 = [](int k, const std::pair<int, int>* dirs) {
                const auto d = dirs[k / 7];
                const int s  = k % 7 + 1;
                return std::pair<int, int>{d.first * s, d.second * s};
            };
            int kind = 0, index = 0;
            std::pair<int, int> delta;
            if (code == 0) return chess::Move(chess::Move::NULL_MOVE);
            if (code <= 8) return step(toCb(b.kingSq(b.sideToMove()).index()), KING[code - 1]);
            if (code == 9) return castle(true);
            if (code == 10) return castle(false);
            if (code >= 111 && code <= 142) {
                const int pawn = (code - 111) / 4, how = (code - 111) % 4;
                const int f                = us == 0 ? 1 : -1;
                static constexpr int DF[4] = {0, 0, 1, -1};
                const int dr[4]            = {f, 2 * f, f, f};
                const int from             = pieces.pawns[us][pawn];
                if (from < 0) fail("no pawn number " + std::to_string(pawn + 1));
                const int to_rank = ((from % 8) + dr[how]) & 7;
                if (to_rank == 0 || to_rank == 7) fail("a promotion in a one-byte move");
                return step(from, {DF[how] * f, dr[how]});
            }
            if (code <= 38) {
                kind = QUEEN, index = 0, delta = line(code - 11, Q);
            } else if (code <= 52) {
                kind = ROOK, index = 0, delta = line(code - 39, R);
            } else if (code <= 66) {
                kind = ROOK, index = 1, delta = line(code - 53, R);
            } else if (code <= 80) {
                kind = BISHOP, index = 0, delta = line(code - 67, B);
            } else if (code <= 94) {
                kind = BISHOP, index = 1, delta = line(code - 81, B);
            } else if (code <= 102) {
                kind = KNIGHT, index = 0, delta = KNIGHTS[code - 95];
            } else if (code <= 110) {
                kind = KNIGHT, index = 1, delta = KNIGHTS[code - 103];
            } else if (code >= 143 && code <= 170) {
                kind = QUEEN, index = 1, delta = line(code - 143, Q);
            } else if (code >= 171 && code <= 198) {
                kind = QUEEN, index = 2, delta = line(code - 171, Q);
            } else if (code >= 199 && code <= 212) {
                kind = ROOK, index = 2, delta = line(code - 199, R);
            } else if (code >= 213 && code <= 226) {
                kind = BISHOP, index = 2, delta = line(code - 213, B);
            } else if (code >= 227 && code <= 234) {
                kind = KNIGHT, index = 2, delta = KNIGHTS[code - 227];
            } else {
                fail("code " + std::to_string(code) + " is not a move");
            }
            const auto& list = pieces.kinds[us][kind];
            if (index >= list.count)
                fail(std::string("no ") + KIND_NAMES[kind] + " number " + std::to_string(index + 1));
            return step(list.squares[index], delta);
        }

        // The compact stream: one-byte codes, a two-byte move after 235, 254 a variation's start, 255 a line's end,
        // 236 nothing.
        void compact(std::span<const uint8_t> s) {
            for (std::size_t i = 0; i < s.size();) {
                const uint8_t v = translate(s[i++]);
                if (v == 236) continue;
                if (ended) break;  // bytes left after the tree's end
                if (v == 254) {
                    startVariation();
                } else if (v == 255) {
                    endLine();
                } else if (v == 235) {
                    if (i + 2 > s.size()) fail("a two-byte move runs past the record");
                    const uint32_t word = static_cast<uint32_t>(translate(s[i])) << 8 | translate(s[i + 1]);
                    i += 2;
                    play(bySquares(word));
                } else if (v > 236) {
                    fail("code " + std::to_string(v) + " is not used");
                } else {
                    play(byCode(v));
                }
            }
        }
        // The simple stream: two-byte moves, bit 15 a variation's start before it, bit 14 a line's end after it.
        void simple(std::span<const uint8_t> s) {
            std::size_t i = 0;
            for (; i + 2 <= s.size() && !ended; i += 2) {
                const uint32_t word = static_cast<uint32_t>(translate(s[i])) << 8 | translate(s[i + 1]);
                if (word & 0x8000) startVariation();
                play(bySquares(word & 0x3FFF));
                if (word & 0x4000) endLine();
            }
            if (i < s.size() && !ended) fail("a move record of an odd length");
        }
    };

    void walk(uint32_t id, const std::vector<uint8_t>& record, RawGame& out) {
        const uint8_t flags = record[0];
        std::size_t pos     = 4;
        const auto take     = [&](std::size_t n) -> std::span<const uint8_t> {
            if (pos + n > record.size()) throw Damage("its move record is cut");
            const std::span<const uint8_t> part(record.data() + pos, n);
            pos += n;
            return part;
        };
        if (flags & 0x80) throw Damage("its move record is a text's");
        std::span<const uint8_t> start, extra;
        if (flags & 0x40) start = take(28);
        const uint8_t mode = flags & 0x3F;
        if (mode == 10 || mode == 11) {
            if (start.empty()) throw Damage("a Chess960 game without its start");
            extra = take(8);
        }
        const std::span<const uint8_t> stream(record.data() + pos, record.size() - pos);

        const std::array<uint8_t, 256>* table = nullptr;
        bool pre = false, simple = false;
        switch (mode) {
            case 0:
                table = &MODE_0, pre = true;
                break;
            case 4:
                table = &MODE_4;
                break;
            case 5:
                table = &MODE_5, simple = true;
                break;
            case 10:
                table = &MODE_10;
                break;
            default:
                break;
        }

        // Where it starts.
        std::optional<Setup> setup;
        if (!start.empty()) {
            setup = decodeStart(start, extra);
            if (!setup) {
                out.builder.reset(chess::Board());
                out.problem = moves_->name() + ": game " + std::to_string(id) + ": its start position is not one";
                return;
            }
        }
        out.chess960 = setup && setup->chess960;
        if (out.chess960) addTag(out, "Variant", "Chess960");

        // A set-up game may castle without the right its start says (older databases store no rights): the right is
        // added when the king and the rook stand where it needs them.
        for (int attempt = 0;; ++attempt) {
            chess::Board board;
            board.set960(out.chess960);
            std::string fen;
            if (setup) {
                fen = fenOf(*setup);
                if (!tour::readableFen(fen)) {
                    out.builder.reset(chess::Board());
                    out.problem = moves_->name() + ": game " + std::to_string(id) + ": its start position is not one";
                    return;
                }
                board.setFen(fen);
                if (!tour::legalPosition(board)) {
                    out.builder.reset(chess::Board());
                    out.problem = moves_->name() + ": game " + std::to_string(id) + ": its start position is not legal";
                    return;
                }
                out.fen = board.getFen();
            }
            out.builder.reset(board);
            if (!table) {
                out.problem = moves_->name() + ": game " + std::to_string(id) + ": moves in encoding " +
                              std::to_string(mode) + ", which this reader does not know";
                return;
            }
            const auto pieces = Pieces::scan(board);
            if (!pieces) {
                out.problem = moves_->name() + ": game " + std::to_string(id) + ": its start has too many pieces";
                return;
            }
            Walker w(out.builder, *pieces);
            w.table    = table;
            w.pre      = pre;
            w.chess960 = out.chess960;
            try {
                if (simple)
                    w.simple(stream);
                else
                    w.compact(stream);
                if (!w.ended && !stream.empty()) w.fail("the moves' tree does not end");
                return;
            } catch (const Damage& e) {
                if (setup && w.missing >= 0 && attempt < 4 && !(setup->castling & (1 << w.missing))) {
                    setup->castling |= static_cast<uint8_t>(1 << w.missing);
                    if (rookFile(*setup, w.missing < 2, w.missing % 2 == 1) >= 0) continue;
                }
                out.problem = moves_->name() + ": game " + std::to_string(id) + ": " + e.what();
                return;
            }
        }
    }

    // The game's annotations, checked whole before any is taken.
    void annotations(uint32_t id, uint64_t offset, RawGame& out) {
        const auto& name = notes_->name();
        if (offset < MIN_FILE_HEADER || offset + 14 > notes_->size())
            throw Damage(at(name, offset) + ": no annotation record");
        const auto head     = notes_->read(offset, 14);
        const uint32_t size = be32(head.data() + 0x0A);
        if (size < 14 || size > MAX_ANNOTATIONS || offset + size > notes_->size())
            throw Damage(at(name, offset) + ": an annotation record that runs past the file");
        const auto span = notes_->read(offset, size);
        const std::vector<uint8_t> r(span.begin(), span.end());
        if (be24(r.data()) != id) throw Damage(at(name, offset) + ": the annotation record of another game");
        if (r[3] != 1 || r[4] != 0 || r[5] != 0x0E || r[6] != 0x0E)
            throw Damage(at(name, offset) + ": not an annotation record");
        uint32_t count = 0;
        std::vector<Note> notes;
        for (std::size_t at_ = 14; at_ < r.size(); ++count) {
            if (r.size() - at_ < 6)
                throw Damage(at(name, offset + at_) + ": an annotation's head runs past its record");
            const uint32_t raw   = be24(r.data() + at_);
            const auto position  = static_cast<int32_t>(raw << 8) >> 8;
            const uint8_t type   = r[at_ + 3];
            const std::size_t sz = be16(r.data() + at_ + 4);
            if (sz < 6 || sz > r.size() - at_) throw Damage(at(name, offset + at_) + ": an annotation of a wrong size");
            if (position < -1) throw Damage(at(name, offset + at_) + ": an annotation at no move");
            notes.push_back(note(name, offset + at_, position, type, std::span(r.data() + at_ + 6, sz - 6)));
            at_ += sz;
        }
        if (be24(r.data() + 7) != count + 1)
            throw Damage(at(name, offset) + ": an annotation count that disagrees with its record");
        out.notes = std::move(notes);
    }

    static Note note(const std::string& name, uint64_t where, int32_t position, uint8_t type,
                     std::span<const uint8_t> d) {
        Note n;
        n.position     = position;
        n.kind         = noteKind(type);
        const auto bad = [&](const char* what) { return Damage(at(name, where) + ": " + what); };
        switch (n.kind) {
            case NoteKind::Text:
            case NoteKind::TextBefore: {
                if (d.size() < 2) throw bad("a text without its language");
                n.language = languageOf(d[1]);
                n.text     = commentText(d.subspan(2), n.language != ANY_LANGUAGE);
                break;
            }
            case NoteKind::Symbols:
                if (d.empty() || d.size() > 3) throw bad("symbols of a wrong size");
                // As PGN writes them: the prefix ("better is"), the move's, the position's.
                n.nags = {d.size() > 2 ? d[2] : uint8_t{0}, d[0], d.size() > 1 ? d[1] : uint8_t{0}};
                break;
            case NoteKind::Squares:
            case NoteKind::Arrows: {
                const std::size_t item = n.kind == NoteKind::Squares ? 2 : 3;
                if (d.size() % item) throw bad("coloured squares or arrows of a wrong size");
                for (std::size_t i = 0; i < d.size(); i += item) {
                    for (std::size_t k = 1; k < item; ++k)
                        if (d[i + k] < 1 || d[i + k] > 64) throw bad("a coloured square off the board");
                    const auto colour = colourOf(d[i]);
                    if (!colour) continue;
                    const auto square = [&](uint8_t v) { return static_cast<int8_t>(fromCb(v - 1)); };
                    n.marks.push_back(
                        BoardMark{*colour, square(d[i + 1]), item == 3 ? square(d[i + 2]) : static_cast<int8_t>(-1)});
                }
                break;
            }
            case NoteKind::TimeSpent:
            case NoteKind::ClockWhite:
            case NoteKind::ClockBlack:
                // Hours, minutes, seconds, hundredths.
                if (d.size() != 4 || d[1] >= 60 || d[2] >= 60) {
                    n.decoded = false;
                } else {
                    n.value = ((d[0] * 60LL + d[1]) * 60 + d[2]) * 1000 + d[3] * 10LL;
                }
                break;
            case NoteKind::Evaluation:
                // Three little-endian shorts: the value, its kind (0 centipawns, 1 moves to mate), the depth.
                if (d.size() == 6 && (le16(d.data() + 2) == 0 || le16(d.data() + 2) == 1)) {
                    n.value = static_cast<int16_t>(le16(d.data()));
                    n.mate  = le16(d.data() + 2) == 1;
                    n.depth = static_cast<int16_t>(le16(d.data() + 4));
                    if (n.mate && n.value == 0) n.decoded = false;
                } else {
                    n.decoded = false;
                }
                break;
            case NoteKind::Medal:
                if (d.size() == 4)
                    n.value = be32(d.data());
                else
                    n.decoded = false;
                break;
            case NoteKind::Evaluations: {
                // A big-endian count, then per position of the main line its value, depth and flag (0 centipawns, 1 a
                // mate in plies, ff none), stored big-endian in four bytes.
                if (d.size() < 2 || d.size() != 2 + 4 * be16(d.data())) {
                    n.decoded = false;
                    break;
                }
                for (std::size_t i = 2; i < d.size(); i += 4) {
                    const uint8_t flag = d[i];
                    const int depth    = d[i + 1];
                    const auto value   = static_cast<int16_t>(be16(d.data() + i + 2));
                    if (flag == 0)
                        n.profile.emplace_back(value, depth);
                    else if (flag == 1 && value != 0)
                        n.profile.emplace_back(
                            static_cast<int16_t>(value > 0 ? 30000 - (value + 1) / 2 : -30000 + (-value + 1) / 2),
                            depth);
                    else
                        n.profile.emplace_back(INT16_MIN, 0);
                }
                break;
            }
            case NoteKind::TimeControl:
            case NoteKind::Correspondence:
                n.decoded = false;  // their layouts in this format are not known
                break;
            default:
                break;
        }
        return n;
    }

    std::unique_ptr<Members> members_;
    std::unique_ptr<Source> headers_, moves_, notes_, wide_;
    std::optional<EntityFile> players_, tournaments_, annotators_, sources_, teams_;
    uint32_t records_     = 0;
    uint64_t wide_record_ = 0;
    uint32_t wide_count_  = 0;
    bool wide_long_       = false;
};

}  // namespace

std::unique_ptr<FormatReader> cbhReader(std::unique_ptr<Members> members) {
    try {
        return std::make_unique<CbhReader>(std::move(members));
    } catch (const Damage& e) {
        throw std::runtime_error(e.what());
    }
}

}  // namespace fcg::study::chessbase
