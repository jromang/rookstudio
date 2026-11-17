#include "structure.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>

#include "endgame.hpp"
#include "material.hpp"

namespace fcg::commentary {

namespace {

// How much more room than the opponent makes more space.
constexpr std::size_t SPACE_MARGIN = 3;

constexpr std::array<File, 8> ALL_FILES{File::A, File::B, File::C, File::D, File::E, File::F, File::G, File::H};

std::optional<File> fileOffset(File f, int delta) noexcept {
    const int i = static_cast<int>(f) + delta;
    if (i < 0 || i > 7) return std::nullopt;
    return static_cast<File>(i);
}

int fileIndex(Square s) noexcept { return static_cast<int>(fileOf(s)); }
int rankIndex(Square s) noexcept { return static_cast<int>(rankOf(s)); }

// `color`'s seventh rank: the second for Black.
Rank seventh(Color c) noexcept { return c == Color::White ? Rank::Seventh : Rank::Second; }
// `color`'s first rank.
Rank backRank(Color c) noexcept { return c == Color::White ? Rank::First : Rank::Eighth; }
// `color`'s fourth rank.
Rank fourth(Color c) noexcept { return c == Color::White ? Rank::Fourth : Rank::Fifth; }

Bitboard centreFiles() noexcept {
    return Bitboard::file(File::C) | Bitboard::file(File::D) | Bitboard::file(File::E) | Bitboard::file(File::F);
}

// The squares behind `square` and level with it, from `color`'s point of view.
Bitboard behindOrLevel(Square square, Color color) noexcept { return ~ahead(square, color); }
// The squares behind `square`, from `color`'s point of view.
Bitboard behind(Square square, Color color) noexcept {
    return behindOrLevel(square, color) & ~Bitboard::rank(rankOf(square));
}

// A pawn its neighbours have all passed, the square in front of it held by an enemy pawn, on a file where no enemy pawn
// stands before it: it can neither be guarded nor safely advance.
bool isBackward(const Board& board, Square square, Color color) {
    const auto own = pawns(board, color), enemy = pawns(board, !color);
    const auto neighbours = filesAround(fileOf(square)) & ~Bitboard::file(fileOf(square));
    const auto stop       = offset(square, color == Color::White ? 8 : -8);
    if (!stop) return false;
    return (neighbours & behindOrLevel(square, color) & own).isEmpty() &&
           (neighbours & ahead(square, color) & own).any() && (attacks::pawn(color, *stop) & enemy).any() &&
           (Bitboard::file(fileOf(square)) & ahead(square, color) & enemy).isEmpty();
}

// Two pawns side by side on the c and d, or d and e files, on `color`'s fourth rank, with no pawn of their own on the
// files beside them and no enemy pawn on theirs.
std::optional<Fact> hangingPawns(const Board& board, Color color) {
    const auto own = pawns(board, color), enemy = pawns(board, !color);
    const auto rank = Bitboard::rank(fourth(color));
    for (const auto left : {File::C, File::D}) {
        const auto right   = *fileOffset(left, 1);
        const auto pair    = Bitboard::file(left) | Bitboard::file(right);
        const auto beside  = (filesAround(left) | filesAround(right)) & ~pair;
        const bool abreast = (own & rank & Bitboard::file(left)).any() && (own & rank & Bitboard::file(right)).any();
        if (abreast && (own & beside).isEmpty() && (enemy & pair).isEmpty())
            return Fact{fact::HangingPawns{color, left}};
    }
    return std::nullopt;
}

// Pawns on the d and e files of `color`'s fourth rank, the opponent left with one central pawn at most.
bool hasPawnCentre(const Board& board, Color color) {
    const auto centre = Bitboard::file(File::D) | Bitboard::file(File::E);
    const auto own    = pawns(board, color) & Bitboard::rank(fourth(color));
    return (own & centre).count() == 2 && (pawns(board, !color) & centre).count() <= 1;
}

// A bishop with three of its own pawns on its colour of squares, two of them blocked by enemy pawns: shut in behind its
// own chain.
std::vector<Fact> badBishops(const Board& board, Color color) {
    const auto own = pawns(board, color), enemy = pawns(board, !color);
    Bitboard blocked;
    for (const auto sq : own) {
        const auto f = offset(sq, color == Color::White ? 8 : -8);
        if (f && enemy.contains(*f)) blocked = blocked.with(sq);
    }
    std::vector<Fact> out;
    for (const auto sq : board.bishops() & board.byColor(color)) {
        const auto colour = Bitboard::lightSquares().contains(sq) ? Bitboard::lightSquares() : Bitboard::darkSquares();
        if ((own & colour).count() >= 3 && (blocked & colour).count() >= 2)
            out.push_back(Fact{fact::BadBishop{placed(color, Role::Bishop, sq)}});
    }
    return out;
}

// In an endgame (queens off, four pieces at most besides kings and pawns), a king on its fourth rank or beyond.
std::optional<Fact> activeKing(const Board& board, Color color) {
    const auto pieces = board.occupied() & ~board.pawns() & ~board.kings();
    if (board.queens().any() || pieces.count() > 4) return std::nullopt;
    const auto king = board.kingOf(color);
    if (!king) return std::nullopt;
    const bool forward = color == Color::White ? rankIndex(*king) >= static_cast<int>(Rank::Fourth)
                                               : rankIndex(*king) <= static_cast<int>(Rank::Fifth);
    if (!forward) return std::nullopt;
    return Fact{fact::ActiveKing{placed(color, Role::King, *king)}};
}

// `color`'s pieces, its queen among them, aimed at the enemy king's square and those around it: three or more, and
// more than the defender's.
std::optional<Fact> kingAttack(const Board& board, Color color) {
    const auto king = board.kingOf(!color);
    if (!king) return std::nullopt;
    if ((board.queens() & board.byColor(color)).isEmpty()) return std::nullopt;
    const auto zone  = attacks::king(*king) | Bitboard::from(*king);
    const auto aimed = [&](Color side) {
        int n = 0;
        for (const auto sq : board.byColor(side) & ~board.pawns() & ~board.kings()) {
            if ((board.attacksFrom(sq) & zone).any()) ++n;
        }
        return n;
    };
    const int attackers = aimed(color);
    if (attackers >= 3 && attackers > aimed(!color)) return Fact{fact::KingAttack{color}};
    return std::nullopt;
}

// The moves `color`'s pieces, pawns and king aside, could make, pins and checks aside.
int mobility(const Board& board, Color color) {
    const auto own = board.byColor(color);
    int n          = 0;
    for (const auto sq : own & ~board.pawns() & ~board.kings()) n += (board.attacksFrom(sq) & ~own).count();
    return n;
}

// `color`'s pieces with half as many moves again as the opponent's, ten more at least, the material even within a pawn:
// more pieces would have more moves anyway.
std::optional<Fact> moreActive(const Board& board, Color color) {
    const int ours = mobility(board, color), theirs = mobility(board, !color);
    const bool even = std::abs(balance(board, color)) <= 1;
    if (even && 2 * ours >= 3 * theirs && ours >= theirs + 10) return Fact{fact::MoreActive{color}};
    return std::nullopt;
}

// `color`'s bishops on a long diagonal, seeing five squares of it or more.
std::vector<Fact> longDiagonals(const Board& board, Color color) {
    const std::array<Bitboard, 2> diagonals{
        Bitboard::from(Square::A1).with(Square::H8) | attacks::between(Square::A1, Square::H8),
        Bitboard::from(Square::H1).with(Square::A8) | attacks::between(Square::H1, Square::A8)};
    std::vector<Fact> out;
    for (const auto sq : board.bishops() & board.byColor(color)) {
        bool long_one = false;
        for (const auto d : diagonals) {
            if (d.contains(sq) && (board.attacksFrom(sq) & d).count() >= 5) long_one = true;
        }
        if (long_one) out.push_back(Fact{fact::LongDiagonal{placed(color, Role::Bishop, sq)}});
    }
    return out;
}

// Squares on files c to f of `color`'s third and fourth ranks, free of pawns, that no pawn of its own can guard any
// more, and that an enemy knight attacks or holds, or a bishop holds: squares for the opponent's pieces, while the
// queens are on.
std::vector<Fact> holes(const Board& board, Color color) {
    if (board.queens().isEmpty()) return {};
    const auto own     = pawns(board, color);
    const auto ranks   = color == Color::White ? Bitboard::rank(Rank::Third) | Bitboard::rank(Rank::Fourth)
                                               : Bitboard::rank(Rank::Sixth) | Bitboard::rank(Rank::Fifth);
    const auto enemy   = board.byColor(!color);
    const auto knights = board.knights() & enemy;
    const auto bishops = board.bishops() & enemy;
    std::vector<Fact> out;
    for (const auto sq : ranks& centreFiles() & ~board.pawns()) {
        const auto guards = filesAround(fileOf(sq)) & ~Bitboard::file(fileOf(sq));
        const bool used   = (knights | bishops).contains(sq) || (attacks::knight(sq) & knights).any();
        if ((guards & behind(sq, color) & own).isEmpty() && used) out.push_back(Fact{fact::Hole{color, sq}});
    }
    return out;
}

// Pieces `color` has developed: knights and bishops off their first squares, and castling.
std::pair<int, int> developed(const Board& board, Color color) {
    const bool white = color == Color::White;
    const std::array<Square, 2> knights =
        white ? std::array{Square::B1, Square::G1} : std::array{Square::B8, Square::G8};
    const std::array<Square, 2> bishops =
        white ? std::array{Square::C1, Square::F1} : std::array{Square::C8, Square::F8};
    const std::array<Square, 4> castled = white ? std::array{Square::G1, Square::C1, Square::B1, Square::H1}
                                                : std::array{Square::G8, Square::C8, Square::B8, Square::H8};
    const auto own                      = [&](Role role) { return board.byRole(role) & board.byColor(color); };
    int at_home                         = 0;
    for (const auto s : knights) at_home += own(Role::Knight).contains(s);
    for (const auto s : bishops) at_home += own(Role::Bishop).contains(s);
    const int minors  = (own(Role::Knight) | own(Role::Bishop)).count();
    const auto king   = board.kingOf(color);
    const int is_done = king && std::find(castled.begin(), castled.end(), *king) != castled.end();
    return {minors - at_home + is_done, at_home};
}

// Two pieces developed more than the opponent, who still has two at home: the opening is not over, and `color` is
// ahead in it.
bool leadsInDevelopment(const Board& board, Color color) {
    const auto [ours, mine_home] = developed(board, color);
    const auto [theirs, at_home] = developed(board, !color);
    (void)mine_home;
    return at_home >= 2 && ours >= theirs + 2;
}

// Rooks on files without pawns, and on the seventh rank when there is something to attack.
std::vector<Fact> rooks(const Board& board, Color color) {
    const auto all_pawns  = board.pawns();
    const auto enemy_king = board.kingOf(!color);
    std::vector<Fact> facts;
    for (const auto sq : board.rooks() & board.byColor(color)) {
        const auto rook = placed(color, Role::Rook, sq);
        if ((all_pawns & Bitboard::file(fileOf(sq))).isEmpty()) facts.push_back(Fact{fact::RookOnOpenFile{rook}});
        const auto rank    = seventh(color);
        const auto targets = pawns(board, !color) & Bitboard::rank(rank);
        const bool cut_off = enemy_king && rankOf(*enemy_king) == backRank(!color);
        if (rankOf(sq) == rank && (targets.any() || cut_off)) facts.push_back(Fact{fact::RookOnSeventh{rook}});
    }
    return facts;
}

// What makes `color`'s passed pawns strong: one guarded by a pawn, two side by side, one far from all the others in an
// endgame, a rook behind one.
std::vector<Fact> passers(const Board& board, Color color) {
    const auto own = pawns(board, color);
    std::vector<Square> passed;
    for (const auto sq : own) {
        if (isPassed(board, sq, color)) passed.push_back(sq);
    }
    std::vector<Fact> facts;
    for (const auto sq : passed) {
        const auto pawn    = placed(color, Role::Pawn, sq);
        const bool guarded = (attacks::pawn(!color, sq) & own).any();
        if (guarded && advance(sq, color) >= 3) facts.push_back(Fact{fact::ProtectedPasser{pawn}});
        const auto others = board.pawns() ^ Bitboard::from(sq);
        const bool near_edge =
            fileOf(sq) == File::A || fileOf(sq) == File::B || fileOf(sq) == File::G || fileOf(sq) == File::H;
        bool far = true;
        for (const auto o : others) {
            if (std::abs(fileIndex(o) - fileIndex(sq)) < 3) far = false;
        }
        if (isEndgame(board) && near_edge && others.any() && far) facts.push_back(Fact{fact::OutsidePasser{pawn}});
        // A rook of its own behind it, nothing between them.
        const auto behind_it = board.rooks() & board.byColor(color) & Bitboard::file(fileOf(sq)) & behind(sq, color);
        for (const auto r : behind_it) {
            if ((attacks::between(r, sq) & board.occupied()).isEmpty()) {
                facts.push_back(Fact{fact::RookBehindPasser{placed(color, Role::Rook, r), pawn}});
                break;
            }
        }
    }
    bool connected = false;
    for (const auto a : passed) {
        for (const auto b : passed) {
            const auto da = advance(a, color), db = advance(b, color);
            if (fileIndex(b) - fileIndex(a) == 1 && (da > db ? da - db : db - da) <= 1) connected = true;
        }
    }
    if (connected) facts.push_back(Fact{fact::ConnectedPassers{color}});
    return facts;
}

// `color`'s rooks and queens on a file without a pawn of their own, attacking the first enemy pawn on it, which no pawn
// guards.
std::vector<Fact> pressure(const Board& board, Color color) {
    const auto own = pawns(board, color), enemy = pawns(board, !color);
    const auto heavy = (board.rooks() | board.queens()) & board.byColor(color);
    std::vector<Fact> out;
    for (const auto sq : heavy) {
        if ((own & Bitboard::file(fileOf(sq))).any()) continue;
        std::optional<Square> target;
        for (const auto t : board.attacksFrom(sq) & Bitboard::file(fileOf(sq)) & enemy) {
            if (ahead(sq, color).contains(t)) {
                target = t;
                break;
            }
        }
        if (!target) continue;
        const bool guarded = (attacks::pawn(color, *target) & enemy).any();
        if (guarded) continue;
        const auto piece = Placed::at(board, sq);
        if (!piece) continue;
        out.push_back(Fact{fact::Pressure{*piece, placed(!color, Role::Pawn, *target)}});
    }
    return out;
}

// `color`'s knights and bishops standing right in front of an enemy pawn that is passed or isolated: it cannot advance,
// and the piece cannot be chased by a pawn from its file.
std::vector<Fact> blockades(const Board& board, Color color) {
    const auto enemy  = pawns(board, !color);
    const auto minors = (board.knights() | board.bishops()) & board.byColor(color);
    std::vector<Fact> out;
    for (const auto sq : enemy) {
        const auto neighbours = filesAround(fileOf(sq)) & ~Bitboard::file(fileOf(sq));
        if (!(isPassed(board, sq, !color) || (neighbours & enemy).isEmpty())) continue;
        const auto stop = offset(sq, color == Color::White ? -8 : 8);
        if (!stop || !minors.contains(*stop)) continue;
        const auto piece = Placed::at(board, *stop);
        if (!piece) continue;
        out.push_back(Fact{fact::Blockade{*piece, placed(!color, Role::Pawn, sq)}});
    }
    return out;
}

// Knights in the enemy half on squares no enemy pawn can ever attack, guarded by a pawn.
std::vector<Fact> outposts(const Board& board, Color color) {
    const auto enemy_half = color == Color::White ? Bitboard::rank(Rank::Fifth) | Bitboard::rank(Rank::Sixth)
                                                  : Bitboard::rank(Rank::Fourth) | Bitboard::rank(Rank::Third);
    std::vector<Fact> out;
    for (const auto sq : board.knights() & board.byColor(color) & enemy_half) {
        const bool guarded   = (attacks::pawn(!color, sq) & pawns(board, color)).any();
        const auto sides     = filesAround(fileOf(sq)) & ~Bitboard::file(fileOf(sq));
        const auto attackers = sides & ahead(sq, color) & pawns(board, !color);
        if (guarded && attackers.isEmpty()) out.push_back(Fact{fact::Outpost{placed(color, Role::Knight, sq)}});
    }
    return out;
}

// A king on its first two ranks, queens still on, with at most one pawn left in the three files around it just in
// front. A king that can still castle is not judged by its shelter: it has yet to choose one.
std::optional<Fact> exposedKing(const Position& pos, Color color) {
    const auto& board = pos.board();
    const auto king   = board.kingOf(color);
    if (!king) return std::nullopt;
    if (pos.castles().hasColor(color)) return std::nullopt;
    const auto home   = color == Color::White ? Bitboard::rank(Rank::First) | Bitboard::rank(Rank::Second)
                                              : Bitboard::rank(Rank::Eighth) | Bitboard::rank(Rank::Seventh);
    const auto queens = board.queens() & board.byColor(!color);
    if (!home.contains(*king) || queens.isEmpty()) return std::nullopt;
    Bitboard near;
    for (const auto s : ahead(*king, color)) {
        if (std::abs(rankIndex(s) - rankIndex(*king)) <= 2) near = near.with(s);
    }
    const auto shelter = filesAround(fileOf(*king)) & near & pawns(board, color);
    if (shelter.count() <= 1) return Fact{fact::ExposedKing{placed(color, Role::King, *king)}};
    return std::nullopt;
}

// The squares of files c to f on `color`'s second to fourth ranks where its pieces have room: free of pawns and out of
// the enemy pawns' reach, those behind a pawn of its own counted twice.
std::size_t room(const Board& board, Color color) {
    const auto own = pawns(board, color);
    Bitboard reach;
    for (const auto sq : pawns(board, !color)) reach |= attacks::pawn(!color, sq);
    std::size_t total = 0;
    for (const auto sq : centreFiles() & ~board.pawns() & ~reach) {
        const auto a = advance(sq, color);
        if (a < 1 || a > 3) continue;
        const bool sheltered = (Bitboard::file(fileOf(sq)) & ahead(sq, color) & own).any();
        total += 1 + (sheltered ? 1 : 0);
    }
    return total;
}

// Which feature a fact is, and whose: two facts of the same feature and side can be compared by number.
std::optional<std::pair<uint8_t, Color>> feature(const Fact& fact) {
    using namespace fact;
    return std::visit(
        [](const auto& f) -> std::optional<std::pair<uint8_t, Color>> {
            using T = std::decay_t<decltype(f)>;
            if constexpr (std::is_same_v<T, PassedPawn>)
                return std::pair{uint8_t{0}, f.pawn.color};
            else if constexpr (std::is_same_v<T, IsolatedPawn>)
                return std::pair{uint8_t{1}, f.pawn.color};
            else if constexpr (std::is_same_v<T, DoubledPawns>)
                return std::pair{uint8_t{2}, f.color};
            else if constexpr (std::is_same_v<T, BishopPair>)
                return std::pair{uint8_t{3}, f.color};
            else if constexpr (std::is_same_v<T, RookOnOpenFile>)
                return std::pair{uint8_t{4}, f.rook.color};
            else if constexpr (std::is_same_v<T, RookOnSeventh>)
                return std::pair{uint8_t{5}, f.rook.color};
            else if constexpr (std::is_same_v<T, Outpost>)
                return std::pair{uint8_t{6}, f.knight.color};
            else if constexpr (std::is_same_v<T, ExposedKing>)
                return std::pair{uint8_t{7}, f.king.color};
            else if constexpr (std::is_same_v<T, BackwardPawn>)
                return std::pair{uint8_t{8}, f.pawn.color};
            else if constexpr (std::is_same_v<T, HangingPawns>)
                return std::pair{uint8_t{9}, f.color};
            else if constexpr (std::is_same_v<T, PawnCentre>)
                return std::pair{uint8_t{10}, f.color};
            else if constexpr (std::is_same_v<T, BadBishop>)
                return std::pair{uint8_t{11}, f.bishop.color};
            else if constexpr (std::is_same_v<T, ActiveKing>)
                return std::pair{uint8_t{12}, f.king.color};
            else if constexpr (std::is_same_v<T, DevelopmentLead>)
                return std::pair{uint8_t{13}, f.side};
            else if constexpr (std::is_same_v<T, KingAttack>)
                return std::pair{uint8_t{14}, f.side};
            else if constexpr (std::is_same_v<T, Hole>)
                return std::pair{uint8_t{15}, f.color};
            else if constexpr (std::is_same_v<T, MoreActive>)
                return std::pair{uint8_t{16}, f.side};
            else if constexpr (std::is_same_v<T, LongDiagonal>)
                return std::pair{uint8_t{17}, f.bishop.color};
            else if constexpr (std::is_same_v<T, Pressure>)
                return std::pair{uint8_t{18}, f.piece.color};
            else if constexpr (std::is_same_v<T, ProtectedPasser>)
                return std::pair{uint8_t{19}, f.pawn.color};
            else if constexpr (std::is_same_v<T, ConnectedPassers>)
                return std::pair{uint8_t{20}, f.color};
            else if constexpr (std::is_same_v<T, OutsidePasser>)
                return std::pair{uint8_t{21}, f.pawn.color};
            else if constexpr (std::is_same_v<T, RookBehindPasser>)
                return std::pair{uint8_t{22}, f.rook.color};
            else if constexpr (std::is_same_v<T, Blockade>)
                return std::pair{uint8_t{23}, f.piece.color};
            else if constexpr (std::is_same_v<T, WrongBishop>)
                return std::pair{uint8_t{24}, f.bishop.color};
            else
                return std::nullopt;
        },
        fact);
}

}  // namespace

Bitboard filesAround(File file) noexcept {
    auto mask = Bitboard::file(file);
    if (const auto left = fileOffset(file, -1)) mask |= Bitboard::file(*left);
    if (const auto right = fileOffset(file, 1)) mask |= Bitboard::file(*right);
    return mask;
}

Bitboard ahead(Square square, Color color) noexcept {
    const int rank = rankIndex(square);
    Bitboard out;
    for (int r = 0; r < 8; ++r) {
        if (color == Color::White ? r > rank : r < rank) out |= Bitboard::rank(static_cast<Rank>(r));
    }
    return out;
}

bool isPassed(const Board& board, Square square, Color color) noexcept {
    const auto enemy_pawns = board.pawns() & board.byColor(!color);
    return (filesAround(fileOf(square)) & ahead(square, color) & enemy_pawns).isEmpty();
}

Bitboard pawns(const Board& board, Color color) noexcept { return board.pawns() & board.byColor(color); }

uint32_t advance(Square square, Color color) noexcept {
    const auto rank = static_cast<uint32_t>(rankOf(square));
    return color == Color::White ? rank : 7 - rank;
}

bool isCrowded(const Board& board) noexcept {
    return (board.occupied() & ~board.pawns() & ~board.kings()).count() >= 8;
}

bool isEndgame(const Board& board) noexcept {
    const auto pieces = board.occupied() & ~board.pawns() & ~board.kings();
    return board.queens().isEmpty() && pieces.count() <= 4;
}

std::vector<Fact> positionFacts(const Position& pos) {
    const auto& board = pos.board();
    std::vector<Fact> facts;
    const auto add = [&](std::vector<Fact> more) {
        for (auto& f : more) facts.push_back(std::move(f));
    };
    const auto addOne = [&](std::optional<Fact> f) {
        if (f) facts.push_back(std::move(*f));
    };
    for (const auto color : {Color::White, Color::Black}) {
        const auto own = pawns(board, color);
        for (const auto sq : own) {
            const auto pawn = placed(color, Role::Pawn, sq);
            if (isPassed(board, sq, color)) facts.push_back(Fact{fact::PassedPawn{pawn}});
            const auto neighbours = filesAround(fileOf(sq)) & ~Bitboard::file(fileOf(sq));
            if ((neighbours & own).isEmpty())
                facts.push_back(Fact{fact::IsolatedPawn{pawn}});
            else if (isBackward(board, sq, color))
                facts.push_back(Fact{fact::BackwardPawn{pawn}});
        }
        addOne(hangingPawns(board, color));
        if (hasPawnCentre(board, color)) facts.push_back(Fact{fact::PawnCentre{color}});
        add(badBishops(board, color));
        addOne(activeKing(board, color));
        addOne(kingAttack(board, color));
        addOne(moreActive(board, color));
        add(longDiagonals(board, color));
        add(holes(board, color));
        if (leadsInDevelopment(board, color)) facts.push_back(Fact{fact::DevelopmentLead{color}});
        for (const auto file : ALL_FILES) {
            if ((own & Bitboard::file(file)).count() >= 2) facts.push_back(Fact{fact::DoubledPawns{color, file}});
        }
        const auto bishops  = board.bishops() & board.byColor(color);
        const bool on_light = (bishops & Bitboard::lightSquares()).any();
        const bool on_dark  = (bishops & Bitboard::darkSquares()).any();
        const auto opponent = board.bishops() & board.byColor(!color);
        if (on_light && on_dark && opponent.count() < 2) facts.push_back(Fact{fact::BishopPair{color}});
        add(rooks(board, color));
        add(passers(board, color));
        add(pressure(board, color));
        add(blockades(board, color));
        add(outposts(board, color));
        addOne(exposedKing(pos, color));
        addOne(wrongBishop(board, color));
    }
    return facts;
}

std::vector<Wing> majorities(const Board& board, Color color) {
    if (pawns(board, color).count() != pawns(board, !color).count()) return {};
    const std::array<std::pair<Wing, std::array<File, 3>>, 2> wings{
        std::pair{Wing::Queenside, std::array{File::A, File::B, File::C}},
        std::pair{Wing::Kingside, std::array{File::F, File::G, File::H}}};
    std::vector<Wing> out;
    for (const auto& [wing, files] : wings) {
        Bitboard area;
        for (const auto f : files) area |= Bitboard::file(f);
        const auto own = pawns(board, color) & area, enemy = pawns(board, !color) & area;
        bool doubled = false, passed = false;
        for (const auto f : files) {
            if ((own & Bitboard::file(f)).count() >= 2) doubled = true;
        }
        for (const auto sq : own) {
            if (isPassed(board, sq, color)) passed = true;
        }
        if (own.count() >= 2 && enemy.any() && own.count() > enemy.count() && !doubled && !passed) out.push_back(wing);
    }
    return out;
}

std::optional<bool> weakColour(const Board& board, Color color) {
    if ((board.queens() & board.byColor(!color)).isEmpty() || !isCrowded(board)) return std::nullopt;
    const auto own     = pawns(board, color);
    const auto bishops = board.bishops() & board.byColor(color);
    for (const auto& [dark, squares] :
         {std::pair{true, Bitboard::darkSquares()}, std::pair{false, Bitboard::lightSquares()}}) {
        const int on_these = (own & squares).count(), on_others = (own & ~squares).count();
        const bool lopsided = on_others >= 5 && on_others >= 2 * on_these;
        const bool weak     = (bishops & squares).isEmpty() && (lopsided || (on_others == 4 && on_these == 0));
        if (weak) return dark;
    }
    return std::nullopt;
}

bool hasSpace(const Board& board, Color color) {
    if (!isCrowded(board) || board.queens().isEmpty()) return false;
    const auto central = [&](Color c) { return pawns(board, c) & centreFiles(); };
    const auto up      = [&](Color c, uint32_t steps) {
        int n = 0;
        for (const auto sq : central(c)) n += advance(sq, c) >= steps;
        return n;
    };
    int at_home = 0;
    for (const auto sq : central(!color)) at_home += advance(sq, !color) == 1;
    const bool wedge = up(color, 4) >= 1 && up(!color, 4) == 0;
    const bool front = up(color, 3) >= 2 && up(!color, 3) == 0;
    return at_home <= 2 && (wedge || front) && room(board, color) >= room(board, !color) + SPACE_MARGIN;
}

std::vector<Fact> structureChanges(const Position& start, std::span<const Move> line) {
    auto end = start;
    for (std::size_t i = 0; i < line.size() && i < SETTLING_PLIES; ++i) end.playUnchecked(line[i]);
    const auto before = positionFacts(start), after = positionFacts(end);
    std::vector<Fact> created;
    for (const auto& fact : after) {
        const auto kind = feature(fact);
        if (!kind) continue;
        const auto count = [&](const std::vector<Fact>& facts) {
            return std::count_if(facts.begin(), facts.end(), [&](const Fact& f) { return feature(f) == kind; });
        };
        const auto already = count(created);
        if (std::find(before.begin(), before.end(), fact) == before.end() && count(after) > count(before) + already)
            created.push_back(fact);
    }
    for (const auto side : {Color::White, Color::Black}) {
        bool castled = false;
        for (std::size_t i = 0; i < line.size() && i < SETTLING_PLIES; ++i) {
            if (line[i].isCastle() && ((i % 2 == 0) == (side == start.turn()))) castled = true;
        }
        const bool could = start.castles().hasColor(side);
        if (could && !end.castles().hasColor(side) && !castled) created.push_back(Fact{fact::LosesCastling{side}});
    }
    return created;
}

}  // namespace fcg::commentary
