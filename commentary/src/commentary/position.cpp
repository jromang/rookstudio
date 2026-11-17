#include "position.hpp"

#include <algorithm>
#include <charconv>
#include <cstdlib>

#include <chess.hpp>

namespace fcg::commentary {

namespace {

chess::Square cs(Square s) { return chess::Square(index(s)); }
Bitboard bb(chess::Bitboard b) { return Bitboard(b.getBits()); }

// The squares strictly between two squares, and the line through them, for every pair.
struct Lines {
    std::array<std::array<uint64_t, 64>, 64> between{};
    std::array<std::array<uint64_t, 64>, 64> ray{};
    Lines() {
        for (int a = 0; a < 64; ++a) {
            for (int b = 0; b < 64; ++b) {
                if (a == b) continue;
                const int df = (b & 7) - (a & 7), dr = (b >> 3) - (a >> 3);
                if (df != 0 && dr != 0 && std::abs(df) != std::abs(dr)) continue;
                const int sf = (df > 0) - (df < 0), sr = (dr > 0) - (dr < 0);
                uint64_t inner = 0;
                for (int f = (a & 7) + sf, r = (a >> 3) + sr; f != (b & 7) || r != (b >> 3); f += sf, r += sr)
                    inner |= uint64_t{1} << (r * 8 + f);
                between[a][b] = inner;
                uint64_t line = 0;
                for (int f = a & 7, r = a >> 3; f >= 0 && f < 8 && r >= 0 && r < 8; f -= sf, r -= sr)
                    line |= uint64_t{1} << (r * 8 + f);
                for (int f = a & 7, r = a >> 3; f >= 0 && f < 8 && r >= 0 && r < 8; f += sf, r += sr)
                    line |= uint64_t{1} << (r * 8 + f);
                ray[a][b] = line;
            }
        }
    }
};

const Lines& lines() {
    static const Lines l;
    return l;
}

constexpr const char* SQUARE_NAMES[64] = {"A1", "B1", "C1", "D1", "E1", "F1", "G1", "H1", "A2", "B2", "C2", "D2", "E2",
                                          "F2", "G2", "H2", "A3", "B3", "C3", "D3", "E3", "F3", "G3", "H3", "A4", "B4",
                                          "C4", "D4", "E4", "F4", "G4", "H4", "A5", "B5", "C5", "D5", "E5", "F5", "G5",
                                          "H5", "A6", "B6", "C6", "D6", "E6", "F6", "G6", "H6", "A7", "B7", "C7", "D7",
                                          "E7", "F7", "G7", "H7", "A8", "B8", "C8", "D8", "E8", "F8", "G8", "H8"};
constexpr const char* ROLE_NAMES[6]    = {"Pawn", "Knight", "Bishop", "Rook", "Queen", "King"};
constexpr const char* RANK_NAMES[8]    = {"First", "Second", "Third", "Fourth", "Fifth", "Sixth", "Seventh", "Eighth"};
constexpr char ROLE_LETTERS[6]         = {'p', 'n', 'b', 'r', 'q', 'k'};

std::string debugRole(std::optional<Role> r) { return r ? "Some(" + debug(*r) + ")" : "None"; }

// Promotions as shakmaty lists them.
constexpr std::array<Role, 4> PROMOTIONS{Role::Queen, Role::Rook, Role::Bishop, Role::Knight};

void pushPromotions(std::vector<Move>& moves, Square from, Square to, std::optional<Role> capture) {
    for (const auto p : PROMOTIONS) moves.push_back(Move::normal(Role::Pawn, from, capture, to, p));
}

}  // namespace

int distance(Square a, Square b) noexcept {
    return std::max(std::abs(static_cast<int>(fileOf(a)) - static_cast<int>(fileOf(b))),
                    std::abs(static_cast<int>(rankOf(a)) - static_cast<int>(rankOf(b))));
}

std::optional<Square> Bitboard::first() const noexcept {
    if (!bits_) return std::nullopt;
    return square(__builtin_ctzll(bits_));
}
std::optional<Square> Bitboard::last() const noexcept {
    if (!bits_) return std::nullopt;
    return square(63 - __builtin_clzll(bits_));
}
std::optional<Square> Bitboard::singleSquare() const noexcept {
    if (!bits_ || moreThanOne()) return std::nullopt;
    return first();
}

namespace attacks {
Bitboard pawn(Color c, Square s) noexcept {
    return bb(chess::attacks::pawn(c == Color::White ? chess::Color::WHITE : chess::Color::BLACK, cs(s)));
}
Bitboard knight(Square s) noexcept { return bb(chess::attacks::knight(cs(s))); }
Bitboard bishop(Square s, Bitboard occupied) noexcept {
    return bb(chess::attacks::bishop(cs(s), chess::Bitboard(occupied.bits())));
}
Bitboard rook(Square s, Bitboard occupied) noexcept {
    return bb(chess::attacks::rook(cs(s), chess::Bitboard(occupied.bits())));
}
Bitboard queen(Square s, Bitboard occupied) noexcept { return bishop(s, occupied) | rook(s, occupied); }
Bitboard king(Square s) noexcept { return bb(chess::attacks::king(cs(s))); }
Bitboard between(Square a, Square b) noexcept { return Bitboard(lines().between[index(a)][index(b)]); }
Bitboard ray(Square a, Square b) noexcept { return Bitboard(lines().ray[index(a)][index(b)]); }
bool aligned(Square a, Square b, Square c) noexcept { return ray(a, b).contains(c); }
Bitboard of(Piece p, Square s, Bitboard occupied) noexcept {
    switch (p.role) {
        case Role::Pawn:
            return pawn(p.color, s);
        case Role::Knight:
            return knight(s);
        case Role::Bishop:
            return bishop(s, occupied);
        case Role::Rook:
            return rook(s, occupied);
        case Role::Queen:
            return queen(s, occupied);
        default:
            return king(s);
    }
}
}  // namespace attacks

Board Board::initial() noexcept {
    Board b;
    constexpr Role back[8] = {Role::Rook, Role::Knight, Role::Bishop, Role::Queen,
                              Role::King, Role::Bishop, Role::Knight, Role::Rook};
    for (int f = 0; f < 8; ++f) {
        b.setPieceAt(square(static_cast<File>(f), Rank::First), {Color::White, back[f]});
        b.setPieceAt(square(static_cast<File>(f), Rank::Second), {Color::White, Role::Pawn});
        b.setPieceAt(square(static_cast<File>(f), Rank::Seventh), {Color::Black, Role::Pawn});
        b.setPieceAt(square(static_cast<File>(f), Rank::Eighth), {Color::Black, back[f]});
    }
    return b;
}

std::optional<Role> Board::roleAt(Square s) const noexcept {
    for (int r = 0; r < 6; ++r) {
        if (role_[r].contains(s)) return static_cast<Role>(r);
    }
    return std::nullopt;
}

std::optional<Color> Board::colorAt(Square s) const noexcept {
    if (color_[0].contains(s)) return Color::White;
    if (color_[1].contains(s)) return Color::Black;
    return std::nullopt;
}

std::optional<Piece> Board::pieceAt(Square s) const noexcept {
    const auto c = colorAt(s);
    if (!c) return std::nullopt;
    return Piece{*c, *roleAt(s)};
}

std::optional<Square> Board::kingOf(Color c) const noexcept { return (kings() & byColor(c)).singleSquare(); }

Bitboard Board::attacksFrom(Square s) const noexcept {
    const auto p = pieceAt(s);
    if (!p) return {};
    switch (p->role) {
        case Role::Pawn:
            return attacks::pawn(p->color, s);
        case Role::Knight:
            return attacks::knight(s);
        case Role::Bishop:
            return attacks::bishop(s, occupied());
        case Role::Rook:
            return attacks::rook(s, occupied());
        case Role::Queen:
            return attacks::queen(s, occupied());
        default:
            return attacks::king(s);
    }
}

Bitboard Board::attacksTo(Square s, Color attacker, Bitboard occupied) const noexcept {
    return byColor(attacker) &
           ((attacks::rook(s, occupied) & rooksAndQueens()) | (attacks::bishop(s, occupied) & bishopsAndQueens()) |
            (attacks::knight(s) & knights()) | (attacks::king(s) & kings()) | (attacks::pawn(!attacker, s) & pawns()));
}

void Board::setPieceAt(Square s, Piece p) noexcept {
    removePieceAt(s);
    role_[static_cast<int>(p.role)] |= Bitboard::from(s);
    color_[static_cast<int>(p.color)] |= Bitboard::from(s);
}

std::optional<Piece> Board::removePieceAt(Square s) noexcept {
    const auto p = pieceAt(s);
    if (p) {
        role_[static_cast<int>(p->role)]   = role_[static_cast<int>(p->role)].without(s);
        color_[static_cast<int>(p->color)] = color_[static_cast<int>(p->color)].without(s);
    }
    return p;
}

Move Move::normal(Role role, Square from, std::optional<Role> capture, Square to,
                  std::optional<Role> promotion) noexcept {
    Move m;
    m.kind_      = Kind::Normal;
    m.role_      = role;
    m.from_      = from;
    m.to_        = to;
    m.capture_   = capture;
    m.promotion_ = promotion;
    return m;
}

Move Move::enPassant(Square from, Square to) noexcept {
    Move m;
    m.kind_ = Kind::EnPassant;
    m.role_ = Role::Pawn;
    m.from_ = from;
    m.to_   = to;
    return m;
}

Move Move::castle(Square king, Square rook) noexcept {
    Move m;
    m.kind_ = Kind::Castle;
    m.role_ = Role::King;
    m.from_ = king;
    m.to_   = rook;
    return m;
}

Role Move::role() const noexcept {
    switch (kind_) {
        case Kind::Castle:
            return Role::King;
        case Kind::EnPassant:
            return Role::Pawn;
        default:
            return role_;
    }
}

std::optional<Role> Move::capture() const noexcept {
    if (kind_ == Kind::EnPassant) return Role::Pawn;
    if (kind_ == Kind::Normal) return capture_;
    return std::nullopt;
}

bool Move::isZeroing() const noexcept {
    switch (kind_) {
        case Kind::Normal:
            return role_ == Role::Pawn || capture_.has_value();
        case Kind::EnPassant:
            return true;
        case Kind::Put:
            return role_ == Role::Pawn;
        default:
            return false;
    }
}

std::optional<CastlingSide> Move::castlingSide() const noexcept {
    if (kind_ != Kind::Castle) return std::nullopt;
    return index(to_) > index(from_) ? CastlingSide::KingSide : CastlingSide::QueenSide;
}

std::optional<Square> Castles::rook(Color c, CastlingSide side) const noexcept {
    return rooks_[2 * static_cast<int>(c) + static_cast<int>(side)];
}

Bitboard Castles::path(Color c, CastlingSide side) const noexcept {
    const auto r = rook(c, side);
    if (!r) return {};
    const auto king = square(File::E, relative(Rank::First, c));
    const auto kt = kingTo(side, c), rt = rookTo(side, c);
    const auto ways = attacks::between(king, kt).with(kt) | attacks::between(*r, rt).with(rt);
    return ways.without(king).without(*r);
}

void Castles::set(Color c, CastlingSide side, std::optional<Square> rook) noexcept {
    rooks_[2 * static_cast<int>(c) + static_cast<int>(side)] = rook;
}

void Castles::discardColor(Color c) noexcept {
    set(c, CastlingSide::KingSide, std::nullopt);
    set(c, CastlingSide::QueenSide, std::nullopt);
}

void Castles::discardRook(Square s) noexcept {
    for (auto& r : rooks_) {
        if (r == s) r.reset();
    }
}

std::optional<Position> Position::fromFen(std::string_view fen, std::string* error) {
    const auto fail = [&](const char* why) -> std::optional<Position> {
        if (error) *error = why;
        return std::nullopt;
    };
    std::vector<std::string_view> fields;
    for (std::size_t at = 0; at < fen.size();) {
        while (at < fen.size() && fen[at] == ' ') ++at;
        if (at >= fen.size()) break;
        const auto end = std::min(fen.find(' ', at), fen.size());
        fields.push_back(fen.substr(at, end - at));
        at = end;
    }
    if (fields.size() < 4) return fail("not a FEN");
    Position p;
    int rank = 7, file = 0;
    for (const char c : fields[0]) {
        if (c == '/') {
            if (file != 8 || rank == 0) return fail("bad board");
            --rank;
            file = 0;
        } else if (c >= '1' && c <= '8') {
            file += c - '0';
        } else {
            const char lower = static_cast<char>(c | 0x20);
            const auto it    = std::find(std::begin(ROLE_LETTERS), std::end(ROLE_LETTERS), lower);
            if (it == std::end(ROLE_LETTERS) || file > 7) return fail("bad board");
            p.board_.setPieceAt(
                square(static_cast<File>(file), static_cast<Rank>(rank)),
                {c == lower ? Color::Black : Color::White, static_cast<Role>(it - std::begin(ROLE_LETTERS))});
            ++file;
        }
        if (file > 8) return fail("bad board");
    }
    if (rank != 0 || file != 8) return fail("bad board");
    if (fields[1] == "w")
        p.turn_ = Color::White;
    else if (fields[1] == "b")
        p.turn_ = Color::Black;
    else
        return fail("bad turn");
    if (fields[2] != "-") {
        for (const char c : fields[2]) {
            const Color color       = (c >= 'a') ? Color::Black : Color::White;
            const char lower        = static_cast<char>(c | 0x20);
            const auto back         = relative(Rank::First, color);
            const CastlingSide side = lower == 'k' ? CastlingSide::KingSide : CastlingSide::QueenSide;
            if (lower != 'k' && lower != 'q') return fail("bad castling");
            const auto corner = square(side == CastlingSide::KingSide ? File::H : File::A, back);
            if (p.board_.pieceAt(corner) != Piece{color, Role::Rook} ||
                p.board_.pieceAt(square(File::E, back)) != Piece{color, Role::King})
                return fail("bad castling rights");
            p.castles_.set(color, side, corner);
        }
    }
    if (fields[3] != "-") {
        if (fields[3].size() != 2 || fields[3][0] < 'a' || fields[3][0] > 'h' ||
            (fields[3][1] != '3' && fields[3][1] != '6'))
            return fail("bad en passant square");
        const auto ep = square(static_cast<File>(fields[3][0] - 'a'), static_cast<Rank>(fields[3][1] - '1'));
        // Behind a pawn of the side not to move that could have just gone two squares.
        const auto pawn = offset(ep, p.turn_ == Color::White ? -8 : 8);
        const auto from = offset(ep, p.turn_ == Color::White ? 8 : -8);
        if (relative(rankOf(ep), p.turn_) != Rank::Sixth || !pawn ||
            p.board_.pieceAt(*pawn) != Piece{!p.turn_, Role::Pawn} || p.board_.occupied().contains(ep) ||
            (from && p.board_.occupied().contains(*from)))
            return fail("bad en passant square");
        p.ep_ = ep;
    }
    const auto number = [](std::string_view text, uint32_t& out) {
        return std::from_chars(text.data(), text.data() + text.size(), out).ec == std::errc();
    };
    if (fields.size() > 4 && !number(fields[4], p.halfmoves_)) return fail("bad halfmove clock");
    if (fields.size() > 5 && (!number(fields[5], p.fullmoves_) || p.fullmoves_ == 0)) return fail("bad move number");
    // A legal position: one king each, no pawn on the back ranks, the side not to move not in check.
    if ((p.board_.kings() & p.board_.white()).count() != 1 || (p.board_.kings() & p.board_.black()).count() != 1)
        return fail("not one king each");
    if ((p.board_.pawns() & Bitboard(Bitboard::BACKRANKS)).any()) return fail("a pawn on a back rank");
    if (p.board_.attacksTo(*p.board_.kingOf(!p.turn_), p.turn_, p.board_.occupied()).any())
        return fail("the side not to move is in check");
    return p;
}

Position Position::initial() {
    Position p;
    p.board_ = Board::initial();
    p.castles_.set(Color::White, CastlingSide::KingSide, Square::H1);
    p.castles_.set(Color::White, CastlingSide::QueenSide, Square::A1);
    p.castles_.set(Color::Black, CastlingSide::KingSide, Square::H8);
    p.castles_.set(Color::Black, CastlingSide::QueenSide, Square::A8);
    return p;
}

Bitboard Position::checkers() const noexcept {
    const auto king = board_.kingOf(turn_);
    if (!king) return {};
    return board_.attacksTo(*king, !turn_, board_.occupied());
}

void Position::generate(std::vector<Move>& moves) const {
    const auto king     = *board_.kingOf(turn_);
    const auto occupied = board_.occupied();
    // En passant first, as shakmaty does.
    if (ep_) {
        for (const auto from : board_.pawns() & us() & attacks::pawn(!turn_, *ep_))
            moves.push_back(Move::enPassant(from, *ep_));
    }
    const auto nonKing = [&](Bitboard target) {
        // Pawns: captures to the west, to the east, then single steps, then double steps.
        const int forward = turn_ == Color::White ? 8 : -8;
        const auto pawns  = our(Role::Pawn);
        for (const int side : {-1, 1}) {
            const int delta = forward + side;
            const auto file_mask =
                side == -1 ? ~Bitboard::file(File::A) : ~Bitboard::file(File::H);  // the pawns that can go that way
            const auto captures = (pawns & file_mask).shift(delta) & them() & target;
            for (const auto to : captures & ~Bitboard(Bitboard::BACKRANKS))
                moves.push_back(Move::normal(Role::Pawn, square(index(to) - delta), board_.roleAt(to), to));
            for (const auto to : captures& Bitboard(Bitboard::BACKRANKS))
                pushPromotions(moves, square(index(to) - delta), to, board_.roleAt(to));
        }
        const auto single = pawns.shift(forward) & ~occupied;
        for (const auto to : single & target & ~Bitboard(Bitboard::BACKRANKS))
            moves.push_back(Move::normal(Role::Pawn, square(index(to) - forward), std::nullopt, to));
        for (const auto to : single & target & Bitboard(Bitboard::BACKRANKS))
            pushPromotions(moves, square(index(to) - forward), to, std::nullopt);
        const auto double_rank = Bitboard::relativeRank(turn_, Rank::Fourth);
        const auto doubles     = single.shift(forward) & double_rank & ~occupied;
        for (const auto to : doubles& target)
            moves.push_back(Move::normal(Role::Pawn, square(index(to) - 2 * forward), std::nullopt, to));
        // Then knights, bishops, rooks and queens, each from a1 on, each to a1 on.
        for (const auto role : {Role::Knight, Role::Bishop, Role::Rook, Role::Queen}) {
            for (const auto from : our(role)) {
                const auto reach = role == Role::Knight   ? attacks::knight(from)
                                   : role == Role::Bishop ? attacks::bishop(from, occupied)
                                   : role == Role::Rook   ? attacks::rook(from, occupied)
                                                          : attacks::queen(from, occupied);
                for (const auto to : reach& target) moves.push_back(Move::normal(role, from, board_.roleAt(to), to));
            }
        }
    };
    const auto safeKing = [&](Bitboard target) {
        for (const auto to : attacks::king(king) & target) {
            if (board_.attacksTo(to, !turn_, occupied).isEmpty())
                moves.push_back(Move::normal(Role::King, king, board_.roleAt(to), to));
        }
    };
    const auto checking = checkers();
    if (checking.isEmpty()) {
        const auto target = ~us();
        nonKing(target);
        safeKing(target);
        for (const auto side : {CastlingSide::KingSide, CastlingSide::QueenSide}) {
            const auto rook = castles_.rook(turn_, side);
            if (!rook) continue;
            if ((castles_.path(turn_, side) & occupied).any()) continue;
            const auto kt = kingTo(side, turn_);
            const auto without =
                occupied ^ Bitboard::from(king) ^ Bitboard::from(*rook) ^ Bitboard::from(rookTo(side, turn_));
            if ((board_.attacksTo(kt, !turn_, without) & ~Bitboard::from(*rook)).any()) continue;
            bool safe = true;
            for (const auto s : attacks::between(king, kt).with(king)) {
                if (board_.attacksTo(s, !turn_, occupied ^ Bitboard::from(king)).any()) {
                    safe = false;
                    break;
                }
            }
            if (safe) moves.push_back(Move::castle(king, *rook));
        }
    } else {
        Bitboard attacked;
        for (const auto checker : checking& board_.sliders())
            attacked |= attacks::ray(checker, king) ^ Bitboard::from(checker);
        safeKing(~us() & ~attacked);
        if (const auto checker = checking.singleSquare()) nonKing(attacks::between(king, *checker).with(*checker));
    }
}

bool Position::isLegal(const Move& m) const {
    // Played on a copy: the side that moved must not be in check.
    Position after = *this;
    after.playUnchecked(m);
    const auto king = after.board_.kingOf(turn_);
    return king && after.board_.attacksTo(*king, !turn_, after.board_.occupied()).isEmpty();
}

std::vector<Move> Position::legalMoves() const {
    std::vector<Move> moves;
    moves.reserve(64);
    generate(moves);
    std::erase_if(moves, [&](const Move& m) { return !isLegal(m); });
    return moves;
}

bool Position::isCheckmate() const { return isCheck() && legalMoves().empty(); }
bool Position::isStalemate() const { return !isCheck() && legalMoves().empty(); }

bool Position::hasInsufficientMaterial(Color c) const noexcept {
    const auto& b = board_;
    if ((b.byColor(c) & (b.pawns() | b.rooksAndQueens())).any()) return false;
    if ((b.byColor(c) & b.knights()).any())
        return b.byColor(c).count() <= 2 && (b.byColor(!c) & ~b.kings() & ~b.queens()).isEmpty();
    if ((b.byColor(c) & b.bishops()).any()) {
        const bool same_color =
            (b.bishops() & Bitboard::darkSquares()).isEmpty() || (b.bishops() & Bitboard::lightSquares()).isEmpty();
        return same_color && b.knights().isEmpty() && b.pawns().isEmpty();
    }
    return true;
}

std::optional<Square> Position::epSquare() const {
    if (!ep_) return std::nullopt;
    for (const auto from : board_.pawns() & us() & attacks::pawn(!turn_, *ep_)) {
        if (isLegal(Move::enPassant(from, *ep_))) return ep_;
    }
    return std::nullopt;
}

void Position::playUnchecked(const Move& m) noexcept {
    const Color color = turn_;
    ep_.reset();
    halfmoves_ = m.isZeroing() ? 0 : halfmoves_ + 1;
    switch (m.kind()) {
        case Move::Kind::Normal: {
            const auto role = m.role();
            const auto from = m.from(), to = m.to();
            if (role == Role::Pawn && index(to) - index(from) == 16 && rankOf(from) == Rank::Second)
                ep_ = offset(from, 8);
            else if (role == Role::Pawn && index(from) - index(to) == 16 && rankOf(from) == Rank::Seventh)
                ep_ = offset(from, -8);
            if (role == Role::King)
                castles_.discardColor(color);
            else if (role == Role::Rook)
                castles_.discardRook(from);
            if (m.capture() == Role::Rook) castles_.discardRook(to);
            board_.removePieceAt(from);
            board_.setPieceAt(to, {color, m.promotion().value_or(role)});
            break;
        }
        case Move::Kind::Castle: {
            const auto king = m.from(), rook = m.to();
            const auto side = index(rook) < index(king) ? CastlingSide::QueenSide : CastlingSide::KingSide;
            board_.removePieceAt(king);
            board_.removePieceAt(rook);
            board_.setPieceAt(square(side == CastlingSide::KingSide ? File::F : File::D, rankOf(rook)),
                              {color, Role::Rook});
            board_.setPieceAt(square(side == CastlingSide::KingSide ? File::G : File::C, rankOf(king)),
                              {color, Role::King});
            castles_.discardColor(color);
            break;
        }
        case Move::Kind::EnPassant:
            board_.removePieceAt(square(fileOf(m.to()), rankOf(m.from())));
            board_.removePieceAt(m.from());
            board_.setPieceAt(m.to(), {color, Role::Pawn});
            break;
        case Move::Kind::Put:
            board_.setPieceAt(m.to(), {color, m.role()});
            break;
    }
    if (color == Color::Black) ++fullmoves_;
    turn_ = !color;
}

std::optional<Position> Position::swappedTurn() const {
    if (isCheck()) return std::nullopt;
    Position p = *this;
    p.turn_    = !turn_;
    p.ep_.reset();
    return p;
}

std::string Position::fen() const {
    std::string out;
    for (int r = 7; r >= 0; --r) {
        int empty = 0;
        for (int f = 0; f < 8; ++f) {
            const auto p = board_.pieceAt(square(static_cast<File>(f), static_cast<Rank>(r)));
            if (!p) {
                ++empty;
                continue;
            }
            if (empty) out += static_cast<char>('0' + empty);
            empty        = 0;
            const char c = ROLE_LETTERS[static_cast<int>(p->role)];
            out += p->color == Color::White ? static_cast<char>(c - 32) : c;
        }
        if (empty) out += static_cast<char>('0' + empty);
        if (r > 0) out += '/';
    }
    out += turn_ == Color::White ? " w " : " b ";
    std::string rights;
    if (castles_.has(Color::White, CastlingSide::KingSide)) rights += 'K';
    if (castles_.has(Color::White, CastlingSide::QueenSide)) rights += 'Q';
    if (castles_.has(Color::Black, CastlingSide::KingSide)) rights += 'k';
    if (castles_.has(Color::Black, CastlingSide::QueenSide)) rights += 'q';
    out += rights.empty() ? "-" : rights;
    out += ' ';
    const auto ep = epSquare();
    out += ep ? name(*ep) : "-";
    out += ' ' + std::to_string(halfmoves_) + ' ' + std::to_string(fullmoves_);
    return out;
}

std::optional<Move> Position::parseUci(std::string_view text) const {
    if (text.size() < 4 || text.size() > 5) return std::nullopt;
    const auto sq = [](char f, char r) -> std::optional<Square> {
        if (f < 'a' || f > 'h' || r < '1' || r > '8') return std::nullopt;
        return square(static_cast<File>(f - 'a'), static_cast<Rank>(r - '1'));
    };
    const auto from = sq(text[0], text[1]), to = sq(text[2], text[3]);
    if (!from || !to) return std::nullopt;
    std::optional<Role> promotion;
    if (text.size() == 5) {
        const auto it = std::find(std::begin(ROLE_LETTERS), std::end(ROLE_LETTERS), text[4]);
        if (it == std::end(ROLE_LETTERS)) return std::nullopt;
        promotion = static_cast<Role>(it - std::begin(ROLE_LETTERS));
    }
    for (const auto& m : legalMoves()) {
        if (m.from() != *from || m.promotion() != promotion) continue;
        if (m.to() == *to) return m;
        // Castling as the king's move to its square ("e1g1").
        if (m.isCastle() && kingTo(*m.castlingSide(), turn_) == *to) return m;
    }
    return std::nullopt;
}

std::string Position::san(const Move& m) const {
    if (m.isCastle()) return m.castlingSide() == CastlingSide::KingSide ? "O-O" : "O-O-O";
    std::string out;
    if (m.role() == Role::Pawn) {
        if (m.isCapture()) {
            out += letter(fileOf(m.from()));
            out += 'x';
        }
        out += name(m.to());
    } else {
        out += upperLetter(m.role());
        bool ambiguous = false, ambiguous_file = false, ambiguous_rank = false;
        for (const auto& c : legalMoves()) {
            if (c.kind() != Move::Kind::Normal || c.role() != m.role() || c.to() != m.to() ||
                c.promotion() != m.promotion() || c.from() == m.from())
                continue;
            ambiguous = true;
            if (rankOf(c.from()) == rankOf(m.from())) ambiguous_rank = true;
            if (fileOf(c.from()) == fileOf(m.from())) ambiguous_file = true;
        }
        if (ambiguous && (!ambiguous_file || ambiguous_rank)) out += letter(fileOf(m.from()));
        if (ambiguous_file) out += letter(rankOf(m.from()));
        if (m.isCapture()) out += 'x';
        out += name(m.to());
    }
    if (m.promotion()) {
        out += '=';
        out += upperLetter(*m.promotion());
    }
    return out;
}

std::string Position::sanPlus(const Move& m) const {
    auto out   = san(m);
    auto after = *this;
    after.playUnchecked(m);
    if (after.isCheckmate())
        out += '#';
    else if (after.isCheck())
        out += '+';
    return out;
}

std::string uci(const Move& m) {
    std::string out = name(m.from());
    if (m.isCastle()) {
        out += name(kingTo(*m.castlingSide(), rankOf(m.from()) == Rank::First ? Color::White : Color::Black));
        return out;
    }
    out += name(m.to());
    if (m.promotion()) out += ROLE_LETTERS[static_cast<int>(*m.promotion())];
    return out;
}

std::string debug(Color c) { return c == Color::White ? "White" : "Black"; }
std::string debug(Role r) { return ROLE_NAMES[static_cast<int>(r)]; }
std::string debug(Square s) { return SQUARE_NAMES[index(s)]; }
std::string debug(File f) { return std::string(1, static_cast<char>('A' + static_cast<int>(f))); }
std::string debug(Rank r) { return RANK_NAMES[static_cast<int>(r)]; }
std::string debug(CastlingSide s) { return s == CastlingSide::KingSide ? "KingSide" : "QueenSide"; }

std::string debug(const Move& m) {
    switch (m.kind()) {
        case Move::Kind::Normal:
            return "Normal { role: " + debug(m.role()) + ", from: " + debug(m.from()) +
                   ", capture: " + debugRole(m.capture()) + ", to: " + debug(m.to()) +
                   ", promotion: " + debugRole(m.promotion()) + " }";
        case Move::Kind::EnPassant:
            return "EnPassant { from: " + debug(m.from()) + ", to: " + debug(m.to()) + " }";
        case Move::Kind::Castle:
            return "Castle { king: " + debug(m.from()) + ", rook: " + debug(m.to()) + " }";
        default:
            return "Put { role: " + debug(m.role()) + ", to: " + debug(m.to()) + " }";
    }
}

std::string name(Square s) {
    return {static_cast<char>('a' + static_cast<int>(fileOf(s))), static_cast<char>('1' + static_cast<int>(rankOf(s)))};
}
char letter(File f) noexcept { return static_cast<char>('a' + static_cast<int>(f)); }
char letter(Rank r) noexcept { return static_cast<char>('1' + static_cast<int>(r)); }
char upperLetter(Role r) noexcept { return "PNBRQK"[static_cast<int>(r)]; }

}  // namespace fcg::commentary
