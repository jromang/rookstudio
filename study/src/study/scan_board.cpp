#include "scan_board.hpp"

#include <array>
#include <cstdlib>

#include <tour/chess_text.hpp>

#include "zobrist.hpp"

namespace fcg::study {

namespace {

inline int lowest(uint64_t b) noexcept { return __builtin_ctzll(b); }
inline uint64_t bit(int square) noexcept { return uint64_t{1} << square; }
inline int fileOf(int square) noexcept { return square & 7; }
inline int rankOf(int square) noexcept { return square >> 3; }
inline int kindOf(uint8_t piece) noexcept { return (piece - 1) % 6; }
inline int sideOf(uint8_t piece) noexcept { return (piece - 1) / 6; }
// Polyglot's numbering: black pawn 0, white pawn 1, black knight 2, … white king 11.
inline int polyglotPiece(int side, int kind) noexcept { return 2 * kind + (side == WHITE_SIDE ? 1 : 0); }

inline uint64_t rookAttacks(int square, uint64_t occupied) noexcept {
    return chess::attacks::rook(chess::Square(square), chess::Bitboard(occupied)).getBits();
}
inline uint64_t bishopAttacks(int square, uint64_t occupied) noexcept {
    return chess::attacks::bishop(chess::Square(square), chess::Bitboard(occupied)).getBits();
}
inline uint64_t knightAttacks(int square) noexcept { return chess::attacks::knight(chess::Square(square)).getBits(); }
inline uint64_t kingAttacks(int square) noexcept { return chess::attacks::king(chess::Square(square)).getBits(); }
// The squares a pawn of `side` on `square` attacks.
inline uint64_t pawnAttacks(int side, int square) noexcept {
    return chess::attacks::pawn(side == WHITE_SIDE ? chess::Color::WHITE : chess::Color::BLACK, chess::Square(square))
        .getBits();
}

constexpr std::array<std::array<int, 2>, 8> KING_STEPS{
    {{1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}, {0, -1}, {1, -1}}};
constexpr std::array<std::array<int, 2>, 8> KNIGHT_JUMPS{
    {{1, 2}, {2, 1}, {2, -1}, {1, -2}, {-1, -2}, {-2, -1}, {-2, 1}, {-1, 2}}};
constexpr std::array<uint8_t, 4> PROMOTIONS{QUEEN, ROOK, BISHOP, KNIGHT};

inline int promotionIndex(int kind) noexcept {
    switch (kind) {
        case ROOK:
            return 1;
        case BISHOP:
            return 2;
        case KNIGHT:
            return 3;
        default:
            return 0;
    }
}
// The 7 other files (or ranks) of a line, numbered 0 to 6.
inline int otherIndex(int own, int target) noexcept { return target < own ? target : target - 1; }
inline int otherValue(int own, int index) noexcept { return index < own ? index : index + 1; }

int stepIndex(const std::array<std::array<int, 2>, 8>& steps, int df, int dr) noexcept {
    for (int i = 0; i < 8; ++i) {
        if (steps[i][0] == df && steps[i][1] == dr) return i;
    }
    return 0;
}

int rookCode(int from, int to) noexcept {
    if (rankOf(from) == rankOf(to)) return otherIndex(fileOf(from), fileOf(to));
    return 7 + otherIndex(rankOf(from), rankOf(to));
}
int bishopCode(int from, int to) noexcept {
    const int df = fileOf(to) - fileOf(from), dr = rankOf(to) - rankOf(from);
    return (dr == df ? 0 : 7) + otherIndex(fileOf(from), fileOf(to));
}
int rookTarget(int from, int code) noexcept {
    if (code < 7) return rankOf(from) * 8 + otherValue(fileOf(from), code);
    return otherValue(rankOf(from), code - 7) * 8 + fileOf(from);
}
int bishopTarget(int from, int code) noexcept {
    const int file = otherValue(fileOf(from), code % 7);
    const int df   = file - fileOf(from);
    const int rank = rankOf(from) + (code < 7 ? df : -df);
    return rank * 8 + file;
}

// The squares from a to b on one rank, both included.
uint64_t rankSpan(int a, int b) noexcept {
    uint64_t out = 0;
    for (int s = std::min(a, b); s <= std::max(a, b); ++s) out |= bit(s);
    return out;
}

}  // namespace

bool ScanBoard::setFen(std::string_view fen, bool chess960) {
    if (!tour::readableFen(fen)) return false;
    chess::Board board;
    board.set960(chess960);
    board.setFen(fen);
    if (!tour::legalPosition(board)) return false;
    return set(board);
}

bool ScanBoard::set(const chess::Board& board) {
    *this = ScanBoard{};
    for (int square = 0; square < 64; ++square) {
        const auto piece = board.at(chess::Square(square));
        if (piece == chess::Piece::NONE) continue;
        const int kind = static_cast<int>(piece.type());
        const int side = piece.color() == chess::Color::WHITE ? WHITE_SIDE : BLACK_SIDE;
        put(square, side, kind);
    }
    if (bb_[0][KING] == 0 || bb_[1][KING] == 0) return false;
    stm_               = board.sideToMove() == chess::Color::WHITE ? WHITE_SIDE : BLACK_SIDE;
    const auto rights  = board.castlingRights();
    using CastlingSide = chess::Board::CastlingRights::Side;
    for (int side = 0; side < 2; ++side) {
        const auto color = side == WHITE_SIDE ? chess::Color::WHITE : chess::Color::BLACK;
        const int rank   = side == WHITE_SIDE ? 0 : 7;
        for (int s = 0; s < 2; ++s) {
            const auto which = s == 0 ? CastlingSide::KING_SIDE : CastlingSide::QUEEN_SIDE;
            if (rights.has(color, which))
                rook_[side][s] = static_cast<int8_t>(rank * 8 + static_cast<int>(rights.getRookFile(color, which)));
        }
    }
    const auto ep = board.enpassantSq();
    ep_           = ep == chess::Square::NO_SQ ? -1 : static_cast<int8_t>(ep.index());
    halfmove_     = static_cast<int>(board.halfMoveClock());
    fullmove_     = static_cast<int>(board.fullMoveNumber());
    // The key from nothing: the pieces were hashed by put().
    key_ ^= castlingKey() ^ epKey();
    if (stm_ == WHITE_SIDE) key_ ^= zobrist::whiteToMove();
    return true;
}

void ScanBoard::put(int square, int side, int kind) noexcept {
    board_[square] = static_cast<uint8_t>(1 + kind + 6 * side);
    bb_[side][kind] |= bit(square);
    occ_[side] |= bit(square);
    if (kind == KING) king_[side] = static_cast<uint8_t>(square);
    key_ ^= zobrist::piece(polyglotPiece(side, kind), square);
}

void ScanBoard::remove(int square) noexcept {
    const auto piece = board_[square];
    const int kind = kindOf(piece), side = sideOf(piece);
    board_[square] = 0;
    bb_[side][kind] &= ~bit(square);
    occ_[side] &= ~bit(square);
    key_ ^= zobrist::piece(polyglotPiece(side, kind), square);
}

uint64_t ScanBoard::castlingKey() const noexcept {
    uint64_t key = 0;
    for (int side = 0; side < 2; ++side) {
        for (int s = 0; s < 2; ++s) {
            if (rook_[side][s] >= 0) key ^= zobrist::castling(2 * side + s);
        }
    }
    return key;
}

// Polyglot hashes the file of a pawn that just went two squares when a pawn of the side to move stands beside it.
uint64_t ScanBoard::epKey() const noexcept {
    if (ep_ < 0) return 0;
    const int pawn  = ep_ + (stm_ == WHITE_SIDE ? -8 : 8);  // the pawn that went two squares
    uint64_t beside = 0;
    if (fileOf(pawn) > 0) beside |= bit(pawn - 1);
    if (fileOf(pawn) < 7) beside |= bit(pawn + 1);
    return (beside & bb_[stm_][PAWN]) ? zobrist::enPassant(fileOf(ep_)) : 0;
}

int ScanBoard::codeSize(int square) const noexcept {
    switch (kindOf(board_[square])) {
        case PAWN: {
            const int rank = stm_ == WHITE_SIDE ? rankOf(square) : 7 - rankOf(square);
            return rank == 6 ? 12 : 4;
        }
        case KNIGHT:
            return 8;
        case BISHOP:
        case ROOK:
            return 14;
        case QUEEN:
            return 28;
        default:
            return 10;
    }
}

int ScanBoard::codeCount() const noexcept {
    int count = 1;  // the null move
    for (uint64_t own = occ_[stm_]; own; own &= own - 1) count += codeSize(lowest(own));
    return count;
}

int ScanBoard::encode(const CodedMove& move) const noexcept {
    if (move.flags & MOVE_NULL) return codeCount() - 1;
    int base = 0;
    for (uint64_t own = occ_[stm_]; own; own &= own - 1) {
        const int square = lowest(own);
        if (square != move.from) {
            base += codeSize(square);
            continue;
        }
        const int df = fileOf(move.to) - fileOf(move.from), dr = rankOf(move.to) - rankOf(move.from);
        switch (kindOf(board_[square])) {
            case KING:
                if (move.flags & MOVE_SHORT_CASTLE) return base + 8;
                if (move.flags & MOVE_LONG_CASTLE) return base + 9;
                return base + stepIndex(KING_STEPS, df, dr);
            case KNIGHT:
                return base + stepIndex(KNIGHT_JUMPS, df, dr);
            case PAWN: {
                const int direction = df == 0 ? 0 : (df < 0 ? 1 : 2);
                if (codeSize(square) == 12) return base + direction * 4 + promotionIndex(move.promotion);
                if (direction == 0) return base + (std::abs(dr) == 2 ? 1 : 0);
                return base + direction + 1;
            }
            case ROOK:
                return base + rookCode(move.from, move.to);
            case BISHOP:
                return base + bishopCode(move.from, move.to);
            default:  // a queen
                if (df == 0 || dr == 0) return base + rookCode(move.from, move.to);
                return base + 14 + bishopCode(move.from, move.to);
        }
    }
    return -1;
}

CodedMove ScanBoard::decode(int code) const noexcept {
    for (uint64_t own = occ_[stm_]; own; own &= own - 1) {
        const int square = lowest(own);
        const int size   = codeSize(square);
        if (code >= size) {
            code -= size;
            continue;
        }
        CodedMove move;
        move.from = static_cast<uint8_t>(square);
        int to    = square;
        switch (kindOf(board_[square])) {
            case KING:
                if (code >= 8) {
                    const int s = code - 8;
                    move.to     = static_cast<uint8_t>(rook_[stm_][s] < 0 ? square : rook_[stm_][s]);
                    move.flags  = s == 0 ? MOVE_SHORT_CASTLE : MOVE_LONG_CASTLE;
                    return move;
                }
                to = square + KING_STEPS[code][0] + 8 * KING_STEPS[code][1];
                break;
            case KNIGHT:
                to = square + KNIGHT_JUMPS[code][0] + 8 * KNIGHT_JUMPS[code][1];
                break;
            case PAWN: {
                const int forward = stm_ == WHITE_SIDE ? 8 : -8;
                int direction     = 0;
                if (size == 12) {
                    direction      = code / 4;
                    move.promotion = PROMOTIONS[code % 4];
                    to             = square + forward;
                } else if (code <= 1) {
                    to = square + forward * (code + 1);
                } else {
                    direction = code - 1;
                    to        = square + forward;
                }
                if (direction == 1) to -= 1;
                if (direction == 2) to += 1;
                if (direction != 0 && to == ep_ && board_[to] == 0) move.flags = MOVE_EN_PASSANT;
                break;
            }
            case ROOK:
                to = rookTarget(square, code);
                break;
            case BISHOP:
                to = bishopTarget(square, code);
                break;
            default:
                to = code < 14 ? rookTarget(square, code) : bishopTarget(square, code - 14);
                break;
        }
        move.to = static_cast<uint8_t>(to & 63);
        return move;
    }
    CodedMove null;
    null.flags = MOVE_NULL;
    return null;
}

void ScanBoard::play(const CodedMove& move) noexcept {
    key_ ^= castlingKey() ^ epKey();
    const int side = stm_, other = stm_ ^ 1;
    if (move.flags & MOVE_NULL) {
        ep_ = -1;
        ++halfmove_;
    } else if (move.flags & (MOVE_SHORT_CASTLE | MOVE_LONG_CASTLE)) {
        const bool short_side = move.flags & MOVE_SHORT_CASTLE;
        const int rank        = side == WHITE_SIDE ? 0 : 7;
        remove(move.from);
        remove(move.to);
        put(rank * 8 + (short_side ? 6 : 2), side, KING);
        put(rank * 8 + (short_side ? 5 : 3), side, ROOK);
        rook_[side][0] = rook_[side][1] = -1;
        ep_                             = -1;
        ++halfmove_;
    } else {
        const int kind = kindOf(board_[move.from]);
        bool capture   = false;
        if (move.flags & MOVE_EN_PASSANT) {
            remove(move.to + (side == WHITE_SIDE ? -8 : 8));
            capture = true;
        } else if (board_[move.to] != 0) {
            for (auto& rook : rook_[other]) {
                if (rook == move.to) rook = -1;
            }
            remove(move.to);
            capture = true;
        }
        remove(move.from);
        put(move.to, side, move.promotion != NO_KIND ? move.promotion : kind);
        if (kind == KING) rook_[side][0] = rook_[side][1] = -1;
        if (kind == ROOK) {
            for (auto& rook : rook_[side]) {
                if (rook == move.from) rook = -1;
            }
        }
        ep_       = kind == PAWN && std::abs(move.to - move.from) == 16 ? static_cast<int8_t>((move.from + move.to) / 2)
                                                                        : int8_t{-1};
        halfmove_ = kind == PAWN || capture ? 0 : halfmove_ + 1;
    }
    if (side == BLACK_SIDE) ++fullmove_;
    stm_ = static_cast<uint8_t>(other);
    key_ ^= zobrist::whiteToMove();
    key_ ^= castlingKey() ^ epKey();
}

bool ScanBoard::attacked(int square, int by, uint64_t occupied, uint64_t removed) const noexcept {
    if (pawnAttacks(by ^ 1, square) & bb_[by][PAWN] & ~removed) return true;
    if (knightAttacks(square) & bb_[by][KNIGHT] & ~removed) return true;
    if (kingAttacks(square) & bb_[by][KING]) return true;
    if (bishopAttacks(square, occupied) & (bb_[by][BISHOP] | bb_[by][QUEEN]) & ~removed) return true;
    return (rookAttacks(square, occupied) & (bb_[by][ROOK] | bb_[by][QUEEN]) & ~removed) != 0;
}

bool ScanBoard::inCheck() const noexcept { return attacked(king_[stm_], stm_ ^ 1, occupied()); }

bool ScanBoard::legal(const CodedMove& move) const noexcept {
    uint64_t occupied = (this->occupied() & ~bit(move.from)) | bit(move.to);
    uint64_t removed  = bit(move.to);
    if (move.flags & MOVE_EN_PASSANT) {
        const int taken = move.to + (stm_ == WHITE_SIDE ? -8 : 8);
        occupied &= ~bit(taken);
        removed |= bit(taken);
    }
    const int king = kindOf(board_[move.from]) == KING ? move.to : king_[stm_];
    return !attacked(king, stm_ ^ 1, occupied, removed);
}

bool ScanBoard::castlingLegal(bool short_side) const noexcept {
    const int rook = rook_[stm_][short_side ? 0 : 1];
    if (rook < 0) return false;
    const int king    = king_[stm_];
    const int rank    = stm_ == WHITE_SIDE ? 0 : 7;
    const int king_to = rank * 8 + (short_side ? 6 : 2), rook_to = rank * 8 + (short_side ? 5 : 3);
    const uint64_t others = occupied() & ~bit(king) & ~bit(rook);
    if ((rankSpan(king, king_to) | rankSpan(rook, rook_to)) & others) return false;
    for (uint64_t path = rankSpan(king, king_to); path; path &= path - 1) {
        if (attacked(lowest(path), stm_ ^ 1, others | bit(rook_to))) return false;
    }
    return true;
}

std::optional<CodedMove> ScanBoard::parseSan(std::string_view san) const {
    while (!san.empty() && (san.back() == '+' || san.back() == '#' || san.back() == '!' || san.back() == '?'))
        san.remove_suffix(1);
    if (san.empty()) return std::nullopt;
    if (san == "--" || san == "Z0") {
        CodedMove null;
        null.flags = MOVE_NULL;
        return null;
    }
    if (san == "O-O" || san == "0-0" || san == "O-O-O" || san == "0-0-0") {
        const bool short_side = san.size() == 3;
        if (!castlingLegal(short_side)) return std::nullopt;
        CodedMove move;
        move.from  = king_[stm_];
        move.to    = static_cast<uint8_t>(rook_[stm_][short_side ? 0 : 1]);
        move.flags = short_side ? MOVE_SHORT_CASTLE : MOVE_LONG_CASTLE;
        return move;
    }

    int kind = PAWN;
    switch (san.front()) {
        case 'N':
            kind = KNIGHT;
            break;
        case 'B':
            kind = BISHOP;
            break;
        case 'R':
            kind = ROOK;
            break;
        case 'Q':
            kind = QUEEN;
            break;
        case 'K':
            kind = KING;
            break;
        default:
            break;
    }
    if (kind != PAWN || san.front() == 'P') san.remove_prefix(1);

    // A promotion: "=Q", or the piece's letter right after the square.
    uint8_t promotion = NO_KIND;
    if (kind == PAWN && !san.empty()) {
        const char last = san.back();
        const int p     = last == 'Q' ? QUEEN : last == 'R' ? ROOK : last == 'B' ? BISHOP : last == 'N' ? KNIGHT : -1;
        if (p >= 0) {
            promotion = static_cast<uint8_t>(p);
            san.remove_suffix(1);
            if (!san.empty() && san.back() == '=') san.remove_suffix(1);
        }
    }
    if (san.size() < 2) return std::nullopt;
    const char tf = san[san.size() - 2], tr = san[san.size() - 1];
    if (tf < 'a' || tf > 'h' || tr < '1' || tr > '8') return std::nullopt;
    const int to = (tr - '1') * 8 + (tf - 'a');
    san.remove_suffix(2);
    int from_file = -1, from_rank = -1;
    bool capture = false;
    for (const char c : san) {
        if (c >= 'a' && c <= 'h')
            from_file = c - 'a';
        else if (c >= '1' && c <= '8')
            from_rank = c - '1';
        else if (c == 'x' || c == ':')
            capture = true;
        else if (c != '-')
            return std::nullopt;
    }
    if (board_[to] != 0 && sideOf(board_[to]) == stm_) return std::nullopt;

    uint64_t candidates = 0;
    const uint64_t own  = bb_[stm_][kind];
    if (kind == PAWN) {
        const int forward = stm_ == WHITE_SIDE ? 8 : -8;
        if (from_file < 0 || from_file == fileOf(to)) {
            // A step forward, one or two squares.
            if (board_[to] != 0) return std::nullopt;
            const int one = to - forward;
            if (one >= 0 && one < 64 && (own & bit(one))) {
                candidates = bit(one);
            } else if (one >= 0 && one < 64 && board_[one] == 0) {
                const int two   = one - forward;
                const int start = stm_ == WHITE_SIDE ? 1 : 6;
                if (two >= 0 && two < 64 && rankOf(two) == start && (own & bit(two))) candidates = bit(two);
            }
        } else {
            const int from = to - forward + (from_file - fileOf(to));
            if (std::abs(from_file - fileOf(to)) != 1 || from < 0 || from >= 64 || !(own & bit(from)))
                return std::nullopt;
            if (board_[to] == 0 && to != ep_) return std::nullopt;
            candidates = bit(from);
        }
        const int last = stm_ == WHITE_SIDE ? 7 : 0;
        if ((rankOf(to) == last) != (promotion != NO_KIND)) return std::nullopt;
    } else {
        if (promotion != NO_KIND) return std::nullopt;
        uint64_t reach = 0;
        switch (kind) {
            case KNIGHT:
                reach = knightAttacks(to);
                break;
            case BISHOP:
                reach = bishopAttacks(to, occupied());
                break;
            case ROOK:
                reach = rookAttacks(to, occupied());
                break;
            case QUEEN:
                reach = bishopAttacks(to, occupied()) | rookAttacks(to, occupied());
                break;
            default:
                reach = kingAttacks(to);
                break;
        }
        candidates = reach & own;
        for (uint64_t c = candidates; c; c &= c - 1) {
            const int square = lowest(c);
            if ((from_file >= 0 && fileOf(square) != from_file) || (from_rank >= 0 && rankOf(square) != from_rank))
                candidates &= ~bit(square);
        }
    }
    (void)capture;

    std::optional<CodedMove> found;
    for (uint64_t c = candidates; c; c &= c - 1) {
        CodedMove move;
        move.from      = static_cast<uint8_t>(lowest(c));
        move.to        = static_cast<uint8_t>(to);
        move.promotion = promotion;
        if (kind == PAWN && to == ep_ && board_[to] == 0 && fileOf(move.from) != fileOf(to))
            move.flags = MOVE_EN_PASSANT;
        if (!legal(move)) continue;
        if (found) return std::nullopt;  // ambiguous
        found = move;
    }
    return found;
}

}  // namespace fcg::study
