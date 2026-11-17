#include "endgame.hpp"

#include <array>
#include <cstdlib>

#include "attack.hpp"
#include "formation.hpp"
#include "structure.hpp"

namespace fcg::commentary {

namespace {

// Kings and pawns only.
bool isPawnEnding(const Board& board) {
    return board.occupied() == (board.kings() | board.pawns()) && board.pawns().any();
}

uint32_t dist(Square a, Square b) noexcept { return static_cast<uint32_t>(distance(a, b)); }

// In a pawn ending, a king move after which the kings face each other on a file or a rank with an odd number of squares
// between them, all empty: the other king has to give way.
std::optional<Fact> opposition(const Position& after, const Move& mv) {
    const auto& board = after.board();
    const Color side  = !after.turn();
    // A king that takes a pawn is after the pawn.
    if (mv.role() != Role::King || mv.isCapture() || !isPawnEnding(board)) return std::nullopt;
    const auto own = board.kingOf(side), other = board.kingOf(!side);
    if (!own || !other) return std::nullopt;
    const uint32_t apart = dist(*own, *other);
    const auto line      = lineKindOf(*own, *other);
    if (!line) return std::nullopt;
    // From afar it is told along a file only: along a rank, kings that far apart are more often on their way elsewhere.
    const bool facing = *line == LineKind::File ? apart % 2 == 0 : *line == LineKind::Rank ? apart == 2 : false;
    const bool clear  = (attacks::between(*own, *other) & board.occupied()).isEmpty();
    if (facing && clear) return Fact{fact::Opposition{side, apart > 2}};
    return std::nullopt;
}

// The moves `pawn` needs to promote, when it is passed and nothing stands in its way.
std::optional<uint32_t> stepsToPromote(const Board& board, Square pawn, Color color) {
    const auto path = Bitboard::file(fileOf(pawn)) & ahead(pawn, color);
    if (!isPassed(board, pawn, color) || (path & board.occupied()).any()) return std::nullopt;
    // From its first square, the pawn's first step is a double one.
    const auto rank = advance(pawn, color);
    return rank == 1 ? 5u : 7 - rank;
}

Square promotionSquare(Square pawn, Color color) noexcept {
    return square(fileOf(pawn), color == Color::White ? Rank::Eighth : Rank::First);
}

// Whether the side without the pawn has only its king to stop it.
bool kingAloneDefends(const Board& board, Color color) {
    return (board.byColor(color) & ~board.pawns() & ~board.kings()).isEmpty();
}

// A pawn move after which the pawn can no longer be caught.
std::optional<Fact> runs(const Position& after, const Move& mv) {
    const Color side = !after.turn();
    if (mv.role() == Role::Pawn && !mv.isPromotion() && outruns(after, mv.to()))
        return Fact{fact::Outruns{placed(side, Role::Pawn, mv.to())}};
    return std::nullopt;
}

// In a pawn ending, a king move, without taking, into the square of an enemy passed pawn: one step later it would have
// been too late. Not told when another pawn runs anyway.
std::optional<Fact> entersSquare(const Position& before, const Position& after, const Move& mv) {
    const Color side  = before.turn();
    const auto& board = after.board();
    if (mv.role() != Role::King || mv.isCapture() || !isPawnEnding(board)) return std::nullopt;
    const auto from = mv.from(), to = mv.to();
    for (const auto p : pawns(board, !side)) {
        if (outruns(after, p)) return std::nullopt;
    }
    for (const auto pawn : pawns(board, !side)) {
        const auto steps = stepsToPromote(board, pawn, !side);
        if (!steps) continue;
        const auto goal = promotionSquare(pawn, !side);
        if (dist(from, goal) > *steps && dist(to, goal) <= *steps)
            return Fact{fact::EntersSquare{placed(side, Role::King, to), placed(!side, Role::Pawn, pawn)}};
    }
    return std::nullopt;
}

// In an endgame, a rook move, without taking, to a file or a rank that the enemy king cannot cross, a passed pawn of
// the rook's side standing beyond it: the king is cut off from the pawn.
std::optional<Fact> cutsOff(const Position& after, const Move& mv) {
    const auto& board = after.board();
    const Color side  = !after.turn();
    // A rook that takes is there for what it takes.
    if (mv.role() != Role::Rook || mv.isCapture() || !isEndgame(board)) return std::nullopt;
    const auto from = mv.from(), rook = mv.to();
    const auto king = board.kingOf(!side);
    if (!king) return std::nullopt;
    // A rook the king attacks, or that can simply be taken, cuts nothing off.
    if (dist(*king, rook) <= 1 || see(board, rook, !side) > 0) return std::nullopt;
    std::vector<Square> passed;
    for (const auto sq : pawns(board, side)) {
        if (isPassed(board, sq, side)) passed.push_back(sq);
    }
    const auto file = [](Square sq) { return static_cast<int>(fileOf(sq)); };
    const auto rank = [](Square sq) { return static_cast<int>(rankOf(sq)); };
    const auto seen = board.attacksFrom(rook);
    for (const auto line : {LineKind::File, LineKind::Rank}) {
        // Where a square stands across the line, and the line's squares next to the king.
        const auto across = [&](Square sq) { return line == LineKind::File ? file(sq) : rank(sq); };
        const auto along  = [&](Square sq) { return line == LineKind::File ? rank(sq) : file(sq); };
        const int barrier = across(rook);
        const auto beyond = [&](Square sq) { return (across(sq) - barrier) * (across(*king) - barrier) < 0; };
        bool held         = true;
        for (int i = 0; i < 64; ++i) {
            const auto sq = square(i);
            if (across(sq) == barrier && std::abs(along(sq) - along(*king)) <= 1 && !seen.contains(sq)) held = false;
        }
        const bool fresh = across(from) != barrier;
        bool any_beyond  = false;
        for (const auto p : passed) {
            if (beyond(p)) any_beyond = true;
        }
        if (across(*king) != barrier && fresh && held && any_beyond)
            return Fact{fact::CutsOff{placed(side, Role::Rook, rook), Placed{!side, Role::King, *king}, line}};
    }
    return std::nullopt;
}

// King and pawn against king: a king move onto a key square of its pawn, which the enemy king cannot simply take. A
// knight's pawn on the sixth can stalemate the king in its corner: that is no win.
std::optional<Fact> keySquare(const Position& after, const Move& mv) {
    const auto& board = after.board();
    const Color side  = !after.turn();
    if (mv.role() != Role::King || board.occupied().count() != 3) return std::nullopt;
    const auto pawn  = (board.pawns() & board.byColor(side)).first();
    const auto enemy = board.kingOf(!side);
    if (!pawn || !enemy) return std::nullopt;
    const auto from = mv.from(), to = mv.to();
    const auto keys    = keySquares(*pawn, side);
    const bool taken   = dist(*enemy, *pawn) <= 1 && dist(to, *pawn) > 1;
    const bool reached = keys.contains(to) && !keys.contains(from);
    if (reached && !taken && !after.isStalemate())
        return Fact{fact::KeySquare{placed(side, Role::King, to), placed(side, Role::Pawn, *pawn)}};
    return std::nullopt;
}

// The men of a rook ending with one pawn, when the board holds just that: the side with the pawn, the pawn, and the
// kings of the attacker and the defender.
struct RookAndPawn {
    Color side;
    Square pawn, king, enemy;
};
std::optional<RookAndPawn> rookAndPawn(const Board& board) {
    const auto pawn = board.pawns().first();
    if (!pawn) return std::nullopt;
    const auto side = board.colorAt(*pawn);
    if (!side) return std::nullopt;
    const auto rooks = [&](Color c) { return (board.rooks() & board.byColor(c)).count(); };
    const bool bare  = board.occupied().count() == 5 && board.pawns().count() == 1;
    if (!(bare && rooks(*side) == 1 && rooks(!*side) == 1)) return std::nullopt;
    const auto king = board.kingOf(*side), enemy = board.kingOf(!*side);
    if (!king || !enemy) return std::nullopt;
    return RookAndPawn{*side, *pawn, *king, *enemy};
}

// Rook and pawn against rook: the king steps in front of its pawn on the seventh rank, or the pawn joins it there, the
// enemy king two files away or more. Not for a rook's pawn.
std::optional<Fact> lucena(const Position& after, const Move& mv) {
    const auto men = rookAndPawn(after.board());
    if (!men) return std::nullopt;
    const int files    = std::abs(static_cast<int>(fileOf(men->pawn)) - static_cast<int>(fileOf(men->enemy)));
    const bool reached = men->side != after.turn() && (mv.role() == Role::King || mv.role() == Role::Pawn) &&
                         fileOf(men->pawn) != File::A && fileOf(men->pawn) != File::H &&
                         advance(men->pawn, men->side) == 6 && men->king == promotionSquare(men->pawn, men->side) &&
                         files >= 2;
    if (reached) return Fact{fact::Lucena{men->side}};
    return std::nullopt;
}

// Rook against rook and pawn: the defender's rook goes to its third rank, which neither the enemy king nor the pawn has
// reached, its own king standing by the promotion square.
std::optional<Fact> philidor(const Position& after, const Move& mv) {
    const auto& board = after.board();
    const auto men    = rookAndPawn(board);
    if (!men) return std::nullopt;
    const Color attacker = men->side;
    const Color side     = !attacker;
    const auto from = mv.from(), rook = mv.to();
    const bool held = side != after.turn() && mv.role() == Role::Rook && !mv.isCapture() && advance(rook, side) == 2 &&
                      advance(from, side) != 2 && advance(men->pawn, attacker) <= 4 &&
                      advance(men->king, attacker) <= 4 &&
                      dist(men->enemy, promotionSquare(men->pawn, attacker)) <= 1 && see(board, rook, attacker) <= 0;
    if (held) return Fact{fact::Philidor{placed(side, Role::Rook, rook)}};
    return std::nullopt;
}

}  // namespace

std::vector<Fact> endgameFacts(const Position& before, const Move& mv) {
    auto after = before;
    after.playUnchecked(mv);
    std::vector<Fact> facts;
    for (auto f : {opposition(after, mv), runs(after, mv), entersSquare(before, after, mv), cutsOff(after, mv),
                   keySquare(after, mv), lucena(after, mv), philidor(after, mv)}) {
        if (f) facts.push_back(std::move(*f));
    }
    return facts;
}

bool outruns(const Position& pos, Square pawn) {
    const auto& board = pos.board();
    const auto color  = board.colorAt(pawn);
    if (!color) return false;
    if (board.roleAt(pawn) != Role::Pawn || !kingAloneDefends(board, !*color)) return false;
    const auto steps = stepsToPromote(board, pawn, *color);
    const auto king  = board.kingOf(!*color);
    if (!steps || !king) return false;
    // A king to move is one step closer; next to a pawn nothing guards, it takes it.
    const uint32_t head_start = pos.turn() != *color ? 1 : 0;
    const bool guarded        = board.attacksTo(pawn, *color, board.occupied()).any();
    const bool taken          = head_start == 1 && dist(*king, pawn) <= 1 && !guarded;
    return !taken && dist(*king, promotionSquare(pawn, *color)) > *steps + head_start;
}

Bitboard keySquares(Square pawn, Color color) noexcept {
    const auto file = fileOf(pawn);
    if (file == File::A || file == File::H) return {};
    const uint32_t steps = advance(pawn, color);
    // Two ranks ahead of a pawn in its own half; one and two ahead of one beyond it; beside the pawn and its promotion
    // square once it is on the seventh.
    std::vector<uint32_t> ranks;
    bool own_file = false;
    if (steps >= 1 && steps <= 3) {
        ranks    = {steps + 2};
        own_file = true;
    } else if (steps == 4 || steps == 5) {
        ranks    = {steps + 1, steps + 2};
        own_file = true;
    } else if (steps == 6) {
        ranks = {6, 7};
    }
    Bitboard out;
    const int f = static_cast<int>(file);
    for (const int candidate : {f - 1, f, f + 1}) {
        if (candidate < 0 || candidate > 7 || (candidate == f && !own_file)) continue;
        for (const auto r : ranks) out = out.with(squareFor(color, static_cast<File>(candidate), r));
    }
    return out;
}

std::optional<Fact> wrongBishop(const Board& board, Color color) {
    const auto own = board.byColor(color), enemy = board.byColor(!color);
    const auto bishops   = board.bishops() & own;
    const auto own_pawns = pawns(board, color);
    const bool alone     = enemy.count() == 1 && own.count() == 2 + own_pawns.count();
    if (!alone || bishops.count() != 1 || own_pawns.isEmpty()) return std::nullopt;
    const auto bishop = *bishops.first(), first = *own_pawns.first();
    const auto file        = fileOf(first);
    const bool on_edge     = (file == File::A || file == File::H) && (own_pawns & ~Bitboard::file(file)).isEmpty();
    const auto corner      = promotionSquare(first, color);
    const bool same_colour = Bitboard::lightSquares().contains(bishop) == Bitboard::lightSquares().contains(corner);
    const auto king = board.kingOf(!color), own_king = board.kingOf(color);
    if (!king || !own_king) return std::nullopt;
    // The king is in the corner, or next to it with nothing to keep it out.
    const bool holds = *king == corner || (dist(*king, corner) <= 1 && dist(*own_king, corner) > 1);
    // The most advanced; of equals, the last (Rust's max_by_key).
    std::optional<Square> leader;
    for (const auto sq : own_pawns) {
        if (!leader || advance(sq, color) >= advance(*leader, color)) leader = sq;
    }
    if (on_edge && !same_colour && holds)
        return Fact{fact::WrongBishop{placed(color, Role::Bishop, bishop), placed(color, Role::Pawn, *leader)}};
    return std::nullopt;
}

}  // namespace fcg::commentary
