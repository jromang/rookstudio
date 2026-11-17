#include "line.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <numeric>

#include "attack.hpp"
#include "cause.hpp"
#include "endgame.hpp"
#include "formation.hpp"
#include "material.hpp"
#include "structure.hpp"
#include "tactics.hpp"
#include "threat.hpp"

namespace fcg::commentary {

namespace {

// Tactics are looked for in the first moves of the side starting the line.
constexpr std::size_t TACTIC_MOVES = 3;
// Checks or new threats on this many moves in a row, from the first, are an initiative.
constexpr std::size_t INITIATIVE_MOVES = 3;
// Material is counted over this many plies.
constexpr std::size_t MATERIAL_PLIES = 10;
// A breakthrough has its passed pawn within this many plies.
constexpr std::size_t BREAKTHROUGH_PLIES = 7;
// A pawn this far up its file (its fifth rank) has got through.
constexpr uint32_t THROUGH = 4;
// A pawn this far up its file (its sixth rank) when the other queens was racing it.
constexpr uint32_t RACING = 5;
// What a cleared square or line brings is counted this many plies after it is used.
constexpr std::size_t CLEARED_PLIES = 3;
// The opening is over by this move: a piece leaving its first square later is not being developed.
constexpr uint32_t OPENING_MOVES = 20;

bool contains(const std::vector<Fact>& facts, const Fact& f) {
    return std::find(facts.begin(), facts.end(), f) != facts.end();
}

void append(std::vector<Fact>& to, std::vector<Fact> more) {
    for (auto& f : more) to.push_back(std::move(f));
}

void append(std::vector<Fact>& to, std::optional<Fact> more) {
    if (more) to.push_back(std::move(*more));
}

std::vector<Fact> unique(std::vector<Fact> facts) {
    std::vector<Fact> out;
    out.reserve(facts.size());
    for (auto& f : facts) {
        if (!contains(out, f)) out.push_back(std::move(f));
    }
    return out;
}

// The threats `side`'s move from `before` to `after` creates: as the engine finds them when it is asked, else as the
// detector sees them.
std::vector<Fact> newThreatsWith(const Position& before, const Position& after, Color side, const Oracle* oracle) {
    const auto old = threats(before, side);
    // The same threat whatever the move making it: a mate, or the same piece to take.
    const auto same = [](const Fact& a, const Fact& b) {
        if (std::holds_alternative<fact::ThreatensMate>(a) && std::holds_alternative<fact::ThreatensMate>(b))
            return true;
        const auto* x = std::get_if<fact::ThreatensCapture>(&a);
        const auto* y = std::get_if<fact::ThreatensCapture>(&b);
        return x && y && x->target == y->target;
    };
    std::optional<std::vector<Fact>> found;
    if (oracle) found = engineThreats(after, side, *oracle);
    if (!found) found = threats(after, side);
    std::vector<Fact> out;
    for (auto& t : *found) {
        if (std::none_of(old.begin(), old.end(), [&](const Fact& o) { return same(o, t); }))
            out.push_back(std::move(t));
    }
    return out;
}

std::vector<Fact> newThreats(const Position& before, const Position& after, Color side) {
    return newThreatsWith(before, after, side, nullptr);
}

// The positions along a line.
struct Walk {
    std::span<const Move> moves;
    // positions[i] is the position before ply i; the last one is the end of the line.
    std::vector<Position> positions;

    Walk(const Position& start, std::span<const Move> line) : moves(line) {
        positions.reserve(line.size() + 1);
        positions.push_back(start);
        for (const auto& mv : line) {
            auto next = positions.back();
            next.playUnchecked(mv);
            positions.push_back(std::move(next));
        }
    }

    [[nodiscard]] const Position& end() const { return positions.back(); }

    // Whether the side that played ply `i` later takes the piece standing on `square` after it, wherever the piece goes
    // in between when `follow` is set: a pinned queen that takes the pinner and is taken back there is still the pin
    // paying off.
    [[nodiscard]] bool takesLater(std::size_t i, Square square, bool follow) const {
        auto at = square;
        for (std::size_t j = i + 1; j < moves.size(); ++j) {
            const auto& mv = moves[j];
            if ((j - i) % 2 == 0) {
                if (mv.isCapture() && mv.to() == at) return true;
            } else if (follow && mv.from() == at) {
                at = mv.to();
            }
        }
        return false;
    }

    [[nodiscard]] const Move* get(std::size_t i) const { return i < moves.size() ? &moves[i] : nullptr; }
};

// Whether the piece on `from` guards `square`: attacks it, were an enemy to land there.
bool guards(const Board& board, Square from, Square square) { return board.attacksFrom(from).contains(square); }

// A king, queen or rook that takes what the side's move at ply `i` offers, and is hit on that square by the piece the
// side moves next: the king checked there, the piece taken there later.
std::optional<Fact> attraction(const Walk& walk, std::size_t i, Color side) {
    const auto &mv = walk.moves[i], &answer = walk.moves[i + 1], &next = walk.moves[i + 2];
    const auto square = landing(mv, side);
    if (answer.to() != square || !answer.isCapture()) return std::nullopt;
    const auto lured = Placed::at(walk.positions[i + 2].board(), square);
    if (!lured) return std::nullopt;
    const auto& after = walk.positions[i + 3];
    if (!guards(after.board(), landing(next, side), square)) return std::nullopt;
    bool hit = false;
    if (lured->role == Role::King)
        hit = true;  // the piece that moved checks the king where it was lured
    else if (lured->role == Role::Queen || lured->role == Role::Rook)
        hit = walk.takesLater(i, square, false);
    if (hit) return Fact{fact::Attraction{*lured}};
    return std::nullopt;
}

// The side takes at ply `i` what a pinned piece guards: the guard cannot take back, or would uncover a piece worth more
// than what it takes. Kept when the capture would not pay were the guard free, and the line does not have it take back.
std::optional<Fact> pinnedGuard(const Walk& walk, std::size_t i, Color side) {
    const auto& mv    = walk.moves[i];
    const auto& board = walk.positions[i].board();
    const auto target = mv.to();
    if (!mv.isCapture() || see(board, target, side) > 0) return std::nullopt;
    const auto takes_back = [&](Square from) {
        const auto* m = walk.get(i + 1);
        return m && m->from() == from;
    };
    for (const auto guard : board.attacksTo(target, !side, board.occupied())) {
        if (takes_back(guard)) continue;
        const auto shield = shielded(board, guard);
        if (!shield) continue;
        const auto& [pinner, behind] = *shield;
        // Taking back along the pin keeps the piece behind covered.
        if (attacks::ray(pinner.square, behind.square).contains(target)) continue;
        // Taking back would cost more than it brings.
        const bool matters = behind.role == Role::King || value(behind.role) > value(mv.role());
        const auto pinned  = Placed::at(board, guard);
        if (!pinned) continue;
        if (matters) return Fact{fact::Pin{pinner, *pinned, behind}};
    }
    return std::nullopt;
}

// The combinations a line plays against the guard of a piece or of a square: the defender taken first, or drawn away;
// a piece lured onto a square where a tactic hits it; a guard that cannot take back because it is pinned. `pays` is
// whether the line wins material or mates: a combination that wins nothing is none.
std::vector<Fact> combinations(const Walk& walk, Color side, bool pays) {
    std::vector<Fact> found;
    if (!pays) return found;
    const auto plies = walk.moves.size();
    for (std::size_t n = 0, i = 0; i < plies && n < TACTIC_MOVES; i += 2, ++n) {
        const auto& board = walk.positions[i].board();
        const auto& mv    = walk.moves[i];
        append(found, pinnedGuard(walk, i, side));
        const auto* answer_p = walk.get(i + 1);
        const auto* next_p   = walk.get(i + 2);
        if (!answer_p || !next_p) continue;
        const auto &answer = *answer_p, &next = *next_p;
        append(found, attraction(walk, i, side));
        // What the side's next move takes, which stood there all along and could not be taken before, and can now; or
        // the mate it gives.
        const auto target           = next.to();
        const bool mates            = walk.positions[i + 3].isCheckmate();
        const auto& ahead           = walk.positions[i + 2].board();
        std::optional<Placed> prize = Placed::at(board, target);
        if (prize && !(prize->color != side && next.isCapture())) prize.reset();
        if (prize && Placed::at(ahead, target) != prize) prize.reset();
        if (prize && !(see(board, target, side) <= 0 && (mates || see(ahead, target, side) > 0))) prize.reset();
        Placed guarded;
        if (prize) {
            guarded = *prize;
        } else if (mates && answer.to() != target && (mv.isCapture() || answer.isCapture())) {
            // The piece that mates, on the square it mates from: one the defender watched, until it was taken or took
            // what was offered elsewhere.
            const auto mating = Placed::at(walk.positions[i + 3].board(), target);
            if (!mating) continue;
            guarded = *mating;
        } else {
            continue;
        }
        // The defender taken first, at a price: it is taken back.
        if (mv.isCapture() && mv.to() != target && answer.to() == mv.to()) {
            const auto defender = Placed::at(board, mv.to());
            if (defender && guards(board, mv.to(), target)) {
                found.push_back(Fact{fact::RemovesDefender{*defender, guarded}});
                continue;
            }
        }
        // The defender drawn away: it takes what was offered, or must answer a check. A check that wins a piece worth
        // more than the one giving it is a fork, though.
        const bool offered = answer.to() == mv.to();
        const bool checked = prize && value(prize->role) <= value(next.role()) && walk.positions[i + 1].isCheck();
        // Taking again on the square the defender took back on is an exchange there.
        const auto from = answer.from();
        if (from != target && mv.to() != target) {
            const auto piece = Placed::at(board, from);
            if (piece && guards(board, from, target) && !guards(ahead, answer.to(), target) && (offered || checked)) {
                // Taking back what it guarded, it leaves what else it guarded: it had two things to do. Offered a piece
                // on an empty square, it is only drawn away.
                std::optional<Placed> first;
                if (offered && mv.isCapture()) first = Placed::at(board, mv.to());
                if (first)
                    found.push_back(Fact{fact::Overloaded{*piece, *first, guarded}});
                else
                    found.push_back(Fact{fact::Deflection{*piece, guarded}});
            }
        }
    }
    return found;
}

// The side's move at ply `i` leaves a square that a bishop, a rook or a queen of its own then goes to, or through,
// without taking, or that a pawn promotes on: the square or the line was cleared for it. Told when the move clears with
// tempo: it gives the piece that makes it, which is taken; or it opens a line by a trade or with a check; or it lets a
// pawn queen. A quiet move that makes room is how pieces are regrouped in any game.
std::optional<Fact> clearance(const Walk& walk, std::size_t i, Color side) {
    const auto* answer_p = walk.get(i + 1);
    const auto* next_p   = walk.get(i + 2);
    if (!answer_p || !next_p) return std::nullopt;
    const auto &mv = walk.moves[i], &answer = *answer_p, &next = *next_p;
    const auto from = mv.from(), to = landing(mv, side);
    const auto start = next.from(), goal = next.to();
    const bool slider  = next.role() == Role::Bishop || next.role() == Role::Rook || next.role() == Role::Queen;
    const bool through = slider && attacks::between(start, goal).contains(from);
    const bool uses    = next.isPromotion() ? goal == from : slider && (goal == from || through);
    if (mv.isPromotion() || mv.isCastle() || next.isCapture() || !uses) return std::nullopt;
    if (to == start || to == goal) return std::nullopt;
    // Taken where it went, where it could not stay: traded when it took as much as it is worth, given when it took less
    // or nothing. A piece that takes more than it is worth, or a pawn that takes, is after what it takes.
    const auto& after   = walk.positions[i + 1];
    const bool taken_it = answer.isCapture() && answer.to() == to && inBadSpot(after.board(), to);
    const int32_t worth = mv.capture() ? value(*mv.capture()) : 0;
    const bool given    = taken_it && worth < value(mv.role());
    // Queens for queens, rooks for rooks: an exchange is after nothing but the exchange.
    const bool swap   = mv.capture() == mv.role();
    const bool greedy = worth > value(mv.role()) || (mv.isCapture() && mv.role() == Role::Pawn);
    // A check from the piece the line opens for is a discovered check, told as that.
    const bool checks = after.isCheck() && !after.checkers().contains(start);
    // A square is left to another piece all the time: only a piece given clears one. A line is cleared by a trade too,
    // and by a check.
    const bool tempo = next.isPromotion() ? true : (through ? taken_it || checks : given);
    if (!tempo || greedy || swap || (mv.isCapture() && !taken_it)) return std::nullopt;
    // A king that steps onto the line and is checked along it opened nothing: it walked in.
    const bool walked_in = answer.role() == Role::King && walk.positions[i + 3].isCheck();
    if (walk.positions[i + 2].isCheck() || walked_in) return std::nullopt;
    const auto piece = Placed::at(walk.positions[i].board(), from);
    const auto user  = Placed::at(walk.positions[i + 2].board(), start);
    if (!piece || !user) return std::nullopt;
    std::optional<LineKind> line;
    if (through) line = lineKindOf(start, goal);
    return Fact{fact::Clearance{*piece, *user, line}};
}

// The clearances among the side's first moves, kept when the piece the way was cleared for makes something of it at
// once: a check, a promotion, or material won in the next moves.
std::vector<Fact> clearances(const Walk& walk, Color side, bool mate) {
    std::vector<Fact> out;
    for (std::size_t n = 0, i = 0; i < walk.moves.size() && n < TACTIC_MOVES; i += 2, ++n) {
        auto f = clearance(walk, i, side);
        if (!f) continue;
        const auto& after  = walk.positions[i + 3];
        const auto& later  = walk.positions[std::min(walk.moves.size(), i + 3 + CLEARED_PLIES)];
        const int32_t gain = balance(later.board(), side) - balance(after.board(), side);
        const bool pays    = mate || after.isCheck() || walk.moves[i + 2].isPromotion() || gain > 0;
        if (pays) out.push_back(std::move(*f));
    }
    return out;
}

// A pawn breakthrough: the line's first move pushes a pawn where an enemy pawn takes it, nothing taking back, and a few
// moves on its side has a passed pawn it did not have, on its fifth rank or beyond, a pawn or more down for it when the
// pawn gets there. Told in endgames, where a passed pawn is what the game is about.
std::optional<Fact> breakthrough(const Walk& walk, Color side) {
    const auto& first = walk.moves[0];
    const auto& start = walk.positions[0].board();
    const auto to     = first.to();
    const bool quiet  = first.role() == Role::Pawn && !first.isCapture() && !first.isPromotion();
    // The pawn is given: a pawn takes it, and it is not taken back.
    const auto takes = [&](const Move* m) { return m && m->isCapture() && m->to() == to; };
    const bool given = takes(walk.get(1)) && walk.moves[1].role() == Role::Pawn && !takes(walk.get(2));
    if (!quiet || !given || !isEndgame(start)) return std::nullopt;
    const auto count = [](const Board& board, Color c) { return pawns(board, c).count(); };
    // Where a pawn on `square` after `plies` plies stood when the line started.
    const auto origin = [&](Square square, std::size_t plies) -> std::optional<Square> {
        std::vector<std::size_t> own;
        for (std::size_t k = 0; k < plies; k += 2) own.push_back(k);
        std::optional<Square> at = square;
        for (auto it = own.rbegin(); it != own.rend(); ++it) {
            const auto& m = walk.moves[*it];
            if (m.to() == *at) {
                if (m.role() != Role::Pawn) return std::nullopt;
                at = m.from();
            }
        }
        return at;
    };
    // The first passed pawn that far up which was not one when the line started.
    for (std::size_t plies = 3; plies <= std::min(walk.moves.size(), BREAKTHROUGH_PLIES); ++plies) {
        bool promoted = false;
        for (std::size_t k = 0; k < plies; ++k) promoted |= walk.moves[k].isPromotion();
        if (promoted) continue;
        const auto& board = walk.positions[plies].board();
        std::optional<Square> found;
        for (const auto sq : pawns(board, side)) {
            if (!isPassed(board, sq, side) || advance(sq, side) < THROUGH) continue;
            const auto o = origin(sq, plies);
            if (!o) continue;
            if (!isPassed(start, *o, side)) {
                found = o;
                break;
            }
        }
        if (!found) continue;
        const int down = (count(start, side) - count(board, side)) - (count(start, !side) - count(board, !side));
        if (down < 1) return std::nullopt;
        const auto passer = Placed::at(start, *found);
        if (!passer) return std::nullopt;
        return Fact{fact::Breakthrough{Placed{side, Role::Pawn, to}, *passer}};
    }
    return std::nullopt;
}

// A race to promote: in an endgame, the first pawn of the line to queen, after running two steps of it or more, while a
// passed pawn of the other side ran as many and stands on its sixth rank or beyond, or queens in turn.
std::optional<Fact> race(const Walk& walk) {
    if (!isEndgame(walk.positions[0].board())) return std::nullopt;
    const auto queens_it =
        std::find_if(walk.moves.begin(), walk.moves.end(), [](const Move& m) { return m.isPromotion(); });
    if (queens_it == walk.moves.end()) return std::nullopt;
    const auto queens = static_cast<std::size_t>(queens_it - walk.moves.begin());
    const Color side  = walk.positions[queens].turn();
    // The steps a pawn standing on `square` after ply `last` took along the line, and whether it was passed when it set
    // out.
    const auto run = [&](Square square, std::size_t last) {
        const Color color   = walk.positions[last].turn();
        auto at             = square;
        std::size_t set_out = last;
        int steps           = 0;
        for (std::size_t i = last + 1; i-- > 0;) {
            if ((last - i) % 2 != 0) continue;
            const auto& m = walk.moves[i];
            if (m.to() != at || m.role() != Role::Pawn || m.isCapture()) continue;
            at      = m.from();
            set_out = i;
            ++steps;
        }
        const bool passed = isPassed(walk.positions[set_out].board(), at, color);
        return std::pair{steps, passed};
    };
    const auto [steps, passed] = run(walk.moves[queens].to(), queens);
    if (steps < 2 || !passed) return std::nullopt;
    // The other side's runner: the pawn it queens with later, or its pawn furthest up when the first one queens.
    std::optional<std::size_t> later;
    for (std::size_t k = queens + 1; k < walk.moves.size(); ++k) {
        if (walk.moves[k].isPromotion()) {
            if (walk.positions[k].turn() != side) later = k;
            break;
        }
    }
    const auto& board = walk.positions[queens + 1].board();
    std::optional<std::pair<Square, std::size_t>> rival;
    if (later) {
        rival = std::pair{walk.moves[*later].to(), *later};
    } else if (queens >= 1) {
        for (const auto sq : pawns(board, !side)) {
            if (advance(sq, !side) >= RACING && isPassed(board, sq, !side)) {
                rival = std::pair{sq, queens - 1};
                break;
            }
        }
    }
    if (!rival) return std::nullopt;
    const auto [rival_steps, rival_passed] = run(rival->first, rival->second);
    if (rival_steps >= 2 && rival_passed)
        return Fact{fact::Race{side, walk.positions[queens + 1].isCheck(), later.has_value()}};
    return std::nullopt;
}

// A knight or a bishop leaving its first square, in the opening.
std::optional<Fact> develops(const Position& before, const Move& mv, Color side) {
    if (before.fullmoves() > OPENING_MOVES) return std::nullopt;
    std::array<Square, 2> home{};
    if (mv.role() == Role::Knight)
        home = side == Color::White ? std::array{Square::B1, Square::G1} : std::array{Square::B8, Square::G8};
    else if (mv.role() == Role::Bishop)
        home = side == Color::White ? std::array{Square::C1, Square::F1} : std::array{Square::C8, Square::F8};
    else
        return std::nullopt;
    if (std::find(home.begin(), home.end(), mv.from()) == home.end()) return std::nullopt;
    return Fact{fact::Develops{Placed{side, mv.role(), mv.to()}}};
}

// The b-pawn advancing, two pawns against three on the queen's wing, where the enemy c-pawn on its third rank holds a
// d-pawn on its fourth: the minority attack.
std::optional<Fact> minorityAttack(const Position& before, const Move& mv, Color side) {
    if (mv.role() != Role::Pawn || mv.isCapture() || fileOf(mv.to()) != File::B) return std::nullopt;
    const auto& board = before.board();
    const auto wing   = Bitboard::file(File::A) | Bitboard::file(File::B) | Bitboard::file(File::C);
    const auto held   = [&](File file, uint32_t rank) {
        for (const auto sq : pawns(board, !side) & Bitboard::file(file)) {
            if (advance(sq, !side) == rank) return true;
        }
        return false;
    };
    const bool attack = advance(mv.to(), side) >= 3 && (pawns(board, side) & wing).count() == 2 &&
                        (pawns(board, !side) & wing).count() == 3 && held(File::C, 2) && held(File::D, 3);
    if (attack) return Fact{fact::MinorityAttack{side}};
    return std::nullopt;
}

// The kings on opposite wings and the queens on, the side starting the line pushes a pawn to its fourth rank or beyond
// on the enemy king's files, where it then has two that far.
std::optional<Fact> pawnStorm(const Walk& walk, Color side) {
    const auto& start   = walk.positions[0].board();
    const auto& end     = walk.positions[std::min(walk.moves.size(), SETTLING_PLIES)].board();
    const auto kingside = [](Square king) -> std::optional<bool> {
        const auto f = fileOf(king);
        if (f == File::A || f == File::B || f == File::C) return false;
        if (f == File::G || f == File::H) return true;
        return std::nullopt;
    };
    const auto own = start.kingOf(side), enemy = start.kingOf(!side);
    if (!own || !enemy) return std::nullopt;
    const auto a = kingside(*own), b = kingside(*enemy);
    if (!a || !b || *a == *b || start.queens().isEmpty()) return std::nullopt;
    const auto files = filesAround(fileOf(*enemy));
    const auto far   = [&](Square sq) { return files.contains(sq) && advance(sq, side) >= 3; };
    bool pushes      = false;
    for (std::size_t i = 0; i < walk.moves.size() && i < SETTLING_PLIES; i += 2) {
        if (walk.moves[i].role() == Role::Pawn && far(walk.moves[i].to())) pushes = true;
    }
    int stormers = 0;
    for (const auto sq : pawns(end, side)) stormers += far(sq);
    if (pushes && stormers >= 2) return Fact{fact::PawnStorm{side}};
    return std::nullopt;
}

// A pawn advancing, without taking, to strike at an enemy pawn.
std::optional<Fact> pawnBreak(const Position& before, const Move& mv, Color side) {
    if (mv.role() != Role::Pawn || mv.isCapture() || mv.isPromotion()) return std::nullopt;
    const auto& board = before.board();
    const auto target = (attacks::pawn(side, mv.to()) & pawns(board, !side)).first();
    if (!target) return std::nullopt;
    return Fact{fact::PawnBreak{Placed{side, Role::Pawn, mv.to()}, Placed{!side, Role::Pawn, *target},
                                lever(board, mv.from(), mv.to(), *target, side)}};
}

// A pawn advancing, without taking, into the enemy half, where it is not simply lost; one that attacks something is a
// break or a threat, a passed pawn is running, and with few pieces left there is room for everyone.
std::vector<Fact> gainsSpace(const Position& before, const Move& mv, Color side) {
    if (mv.role() != Role::Pawn || mv.isCapture() || mv.isPromotion()) return {};
    const auto from = mv.from(), to = mv.to();
    auto after = before;
    after.playUnchecked(mv);
    const auto& board  = after.board();
    const bool touches = (attacks::pawn(side, to) & board.byColor(!side)).any();
    const bool safe    = see(board, to, !side) <= 0;
    const auto a       = advance(to, side);
    const bool gains =
        a >= 4 && a <= 5 && !touches && isCrowded(board) && !isPassed(before.board(), from, side) && safe;
    std::vector<Fact> facts;
    // The advance to the fourth rank or beyond, on files c to f, that leaves its side with more space than the
    // opponent.
    const auto f       = fileOf(to);
    const bool central = f == File::C || f == File::D || f == File::E || f == File::F;
    const bool takes   = central && a >= 3 && safe;
    if (takes && hasSpace(board, side) && !hasSpace(before.board(), side)) facts.push_back(Fact{fact::Space{side}});
    if (gains) facts.push_back(Fact{fact::GainsSpace{Placed{side, Role::Pawn, to}}});
    return facts;
}

bool retaken(const Walk& walk) {
    const auto& first = walk.moves[0];
    const auto* m     = walk.get(1);
    return m && first.isCapture() && m->isCapture() && m->to() == first.to();
}

// The named formations the line's first move brings about, or the exchange it starts when the answer takes back, while
// there are pieces enough for their plans. An isolated queen's pawn is told as the pawn it is.
std::vector<Fact> newFormations(const Walk& walk) {
    if (!isCrowded(walk.positions[0].board())) return {};
    const auto before = formations(walk.positions[0].board());
    std::vector<Fact> out;
    for (const auto& shape : formations(walk.positions[1 + (retaken(walk) ? 1 : 0)].board())) {
        if (shape.first != Formation::Isolani && std::find(before.begin(), before.end(), shape) == before.end())
            out.push_back(Fact{fact::Formation{shape.first, shape.second}});
    }
    return out;
}

// The squares of one colour left weak in a side's camp once the line's first plies settle: its bishop of that colour
// was taken where it stood, and its pawns stand on the other. A bishop that went and traded itself was given, not
// taken.
std::vector<Fact> weakColours(const Walk& walk) {
    const auto& start   = walk.positions[0].board();
    const auto plies    = std::min(walk.moves.size(), SETTLING_PLIES);
    const auto& settled = walk.positions[plies].board();
    std::vector<Fact> out;
    for (const auto side : {Color::White, Color::Black}) {
        const auto dark = weakColour(settled, side);
        if (!dark) continue;
        const auto squares = *dark ? Bitboard::darkSquares() : Bitboard::lightSquares();
        const auto bishops = start.bishops() & start.byColor(side) & squares;
        bool taken_it      = false;
        for (std::size_t i = 0; i < plies; ++i) {
            const auto& mv   = walk.moves[i];
            const auto still = Placed::at(walk.positions[i].board(), mv.to());
            const bool there = still && still->role == Role::Bishop && still->color == side;
            if (mv.isCapture() && bishops.contains(mv.to()) && there) taken_it = true;
        }
        if (taken_it) out.push_back(Fact{fact::WeakColour{side, *dark}});
    }
    return out;
}

// The pawn majorities the line's first move brings about, or the exchange it starts when the answer takes back, once
// the queens are off: that is when a majority tells. Both sides have as many pawns before as after: a majority is a
// matter of structure, and a pawn won or won back is told as that.
std::vector<Fact> newMajorities(const Walk& walk) {
    const auto& before = walk.positions[0].board();
    const auto& after  = walk.positions[1 + (retaken(walk) ? 1 : 0)].board();
    const bool level   = pawns(before, Color::White).count() == pawns(before, Color::Black).count();
    if (after.queens().any() || !level) return {};
    std::vector<Fact> out;
    for (const auto side : {Color::White, Color::Black}) {
        const auto had = majorities(before, side);
        for (const auto wing : majorities(after, side)) {
            if (std::find(had.begin(), had.end(), wing) == had.end()) out.push_back(Fact{fact::Majority{side, wing}});
        }
    }
    return out;
}

// A pawn move in front of a king shut in on its first rank by its own men, while an enemy rook or queen bears on that
// rank: the king gets a square to step to, one no enemy attacks.
std::optional<Fact> luft(const Position& before, const Move& mv, Color side) {
    const auto& board = before.board();
    const auto king   = board.kingOf(side);
    if (!king) return std::nullopt;
    const auto from  = mv.from();
    const auto heavy = (board.rooks() | board.queens()) & board.byColor(!side);
    const auto rank  = Bitboard::rank(rankOf(*king));
    bool bears       = false;
    for (const auto sq : heavy) {
        if ((board.attacksFrom(sq) & rank).any()) bears = true;
    }
    if (mv.role() != Role::Pawn || mv.isCapture() || advance(*king, side) != 0 || !bears) return std::nullopt;
    Bitboard front;
    for (const auto sq : attacks::king(*king)) {
        if (advance(sq, side) == 1) front = front.with(sq);
    }
    const bool shut_in = (front & ~board.byColor(side)).isEmpty();
    auto after         = before;
    after.playUnchecked(mv);
    const bool safe = after.board().attacksTo(from, !side, after.board().occupied()).isEmpty();
    if (shut_in && front.contains(from) && safe) return Fact{fact::Luft{side}};
    return std::nullopt;
}

// The side starting the line checks, or makes a new threat without taking anything, with each of its first moves:
// captures are exchanges, not an initiative.
bool keepsInitiative(const Walk& walk, Color side) {
    if (walk.moves.size() <= 2 * (INITIATIVE_MOVES - 1)) return false;
    for (std::size_t k = 0; k < INITIATIVE_MOVES; ++k) {
        const auto &before = walk.positions[2 * k], &after = walk.positions[2 * k + 1];
        if (!(after.isCheck() || (!walk.moves[2 * k].isCapture() && !newThreats(before, after, side).empty())))
            return false;
    }
    return true;
}

// The side whose every move is a check between two visits of the same position.
std::optional<Color> perpetualCheck(const Walk& walk) {
    const auto same = [](const Position& a, const Position& b) {
        return a.board() == b.board() && a.turn() == b.turn() && a.castles() == b.castles();
    };
    const auto& positions = walk.positions;
    for (std::size_t i = 0; i < positions.size(); ++i) {
        for (std::size_t j = i + 4; j < positions.size(); ++j) {
            if (!same(positions[i], positions[j])) continue;
            for (std::size_t p = i; p < j; ++p) {
                const Color c = positions[p].turn();
                bool all      = true;
                for (std::size_t k = i; k < j; ++k) {
                    if (positions[k].turn() == c && !positions[k + 1].isCheck()) {
                        all = false;
                        break;
                    }
                }
                if (all) return c;
            }
            return std::nullopt;
        }
    }
    return std::nullopt;
}

// How the centre stands, by its d- and e-pawns, while the queens are on.
enum class Centre { Full, Open, Locked, Other };

Centre centre(const Board& board) {
    const auto all   = board.pawns();
    const auto files = Bitboard::file(File::D) | Bitboard::file(File::E);
    if (board.queens().isEmpty()) return Centre::Other;
    if ((all & files).count() <= 1) return Centre::Open;
    const auto white = all & board.white(), black = all & board.black();
    bool locked = true;
    for (const auto file : {File::D, File::E}) {
        bool any = false;
        for (const auto sq : white& Bitboard::file(file)) {
            const auto front = offset(sq, 8);
            if (front && black.contains(*front)) any = true;
        }
        if (!any) locked = false;
    }
    if (locked) return Centre::Locked;
    if ((all & files).count() >= 3) return Centre::Full;
    return Centre::Other;
}

// The tactics of ply `i`, kept when the line later takes one of their targets (or mates).
std::vector<Fact> cashedTactics(const Walk& walk, std::size_t i, Color side, bool mate, const Oracle* oracle) {
    const auto &before = walk.positions[i], &after = walk.positions[i + 1];
    const auto& mv = walk.moves[i];
    const auto to  = landing(mv, side);
    // A forked, pinned or trapped piece can be taken wherever it goes; the target of a discovered attack that steps
    // away has escaped it.
    const auto cashed = [&](std::span<const Square> squares) {
        return mate ||
               std::any_of(squares.begin(), squares.end(), [&](Square s) { return walk.takesLater(i, s, true); });
    };
    const auto& board = after.board();
    std::vector<Fact> found;

    if (auto f = fork(board, to)) {
        std::vector<Square> squares;
        for (const auto& t : std::get<fact::Fork>(*f).targets) squares.push_back(t.square);
        if (cashed(squares)) found.push_back(std::move(*f));
    }
    // A skewer pays when the front piece steps aside and the same slider takes the one behind.
    if (auto f = skewer(board, to)) {
        const auto& s     = std::get<fact::Skewer>(*f);
        const auto* step  = walk.get(i + 1);
        const auto* taken = walk.get(i + 2);
        if (step && step->from() == s.front.square && taken && taken->from() == to && taken->to() == s.behind.square)
            found.push_back(std::move(*f));
    }
    for (auto& f : discovered(board, mv.from(), to, side)) {
        const auto& d = std::get<fact::DiscoveredAttack>(f);
        if (d.target.role == Role::King || mate || walk.takesLater(i, d.target.square, false))
            found.push_back(std::move(f));
    }
    // A pin that matters (see pins) is the line's motif even if nothing is taken: the pinned piece cannot escape, or
    // cannot take. A piece that at once takes a pinner worth more than itself was not held: the pinner had only taken
    // something on its way.
    const auto freed = [&](const Placed& pinner, const Placed& pinned) {
        const auto* m = walk.get(i + 1);
        return value(pinner.role) > value(pinned.role) && m && m->from() == pinned.square && m->to() == pinner.square;
    };
    for (auto& f : pins(board, side)) {
        const auto& p = std::get<fact::Pin>(f);
        if (!freed(p.pinner, p.pinned)) found.push_back(std::move(f));
    }
    // A piece pinned to one worth more, attacked by another than its pinner where it cannot stay, and taken there.
    for (const auto piece : board.byColor(!side) & ~board.kings() & ~board.pawns()) {
        const auto shield = shielded(board, piece);
        if (!shield) continue;
        const auto& [pinner, behind] = *shield;
        if (behind.role == Role::King) continue;
        const auto pinned = Placed::at(board, piece);
        if (!pinned || value(behind.role) <= value(pinned->role)) continue;
        if (!inBadSpot(board, piece)) continue;
        if ((board.attacksTo(piece, side, board.occupied()) ^ Bitboard::from(pinner.square)).isEmpty()) continue;
        if (!walk.takesLater(i, piece, false)) continue;
        found.push_back(Fact{fact::Pin{pinner, *pinned, behind}});
    }
    const auto trapped_before = trapped(before, !side);
    for (const auto& piece : trapped(after, !side)) {
        const auto way_out = [&] { return oracle && escapes(after, piece, *oracle); };
        const std::array<Square, 1> square{piece.square};
        if (std::find(trapped_before.begin(), trapped_before.end(), piece) == trapped_before.end() && cashed(square) &&
            !way_out())
            found.push_back(Fact{fact::Trapped{piece}});
    }
    return found;
}

// The piece `side` gives away on purpose: after one of its next moves, among those tactics are looked for in, it is
// still two or more pawns down on the start, or one when what it gave is a piece for pawns. Promotions muddle the count
// and rule it out. A piece that was already lost when the line starts, the side in check and unable to save it, is not
// given.
std::optional<Placed> sacrificed(const Walk& walk, Color side) {
    if (std::any_of(walk.moves.begin(), walk.moves.end(), [](const Move& m) { return m.isPromotion(); }))
        return std::nullopt;
    const auto& start = walk.positions[0];
    const auto behind = [&](std::size_t i) {
        return balance(start.board(), side) - balance(walk.positions[i + 1].board(), side);
    };
    // The piece whose capture, not taken back, put `side` behind.
    std::optional<std::pair<std::size_t, Placed>> found;
    for (std::size_t i = 2; i < std::min(walk.moves.size(), 2 * TACTIC_MOVES); i += 2) {
        const std::size_t taken_at = i - 1;
        const auto piece           = Placed::at(walk.positions[taken_at].board(), walk.moves[taken_at].to());
        if (!piece) continue;
        const bool given = behind(i) >= 2 || (behind(i) >= 1 && piece->role != Role::Pawn);
        if (given && piece->color == side) {
            found = std::pair{taken_at, *piece};
            break;
        }
    }
    if (!found) return std::nullopt;
    const auto& [taken_at, piece] = *found;
    bool stood                    = true;
    for (std::size_t k = 0; k < taken_at; k += 2) {
        if (landing(walk.moves[k], side) == piece.square) stood = false;
    }
    const bool lost_already = stood && start.isCheck() && see(start.board(), piece.square, !side) > 0;
    if (lost_already) return std::nullopt;
    return piece;
}

}  // namespace

std::vector<Fact> moveFacts(const Position& before, const Move& mv) {
    const Color side = before.turn();
    auto after       = before;
    after.playUnchecked(mv);
    const auto& b = before.board();
    const auto& a = after.board();
    const auto to = landing(mv, side);
    std::vector<Fact> facts;

    if (const auto wing = mv.castlingSide()) facts.push_back(Fact{fact::Castles{side, *wing}});
    if (const auto role = mv.promotion()) facts.push_back(Fact{fact::Promotes{side, *role, mv.to()}});
    if (after.isCheckmate()) {
        facts.push_back(Fact{fact::Mate{side, 1, matePattern(after)}});
        return facts;
    }
    if (after.checkers().count() >= 2) facts.push_back(Fact{fact::DoubleCheck{side}});
    append(facts, fork(a, to));
    append(facts, skewer(a, to));
    append(facts, discovered(a, mv.from(), to, side));
    const auto old_pins = pins(b, side);
    for (auto& p : pins(a, side)) {
        if (!contains(old_pins, p)) facts.push_back(std::move(p));
    }
    // The mover's pieces it leaves en prise. The piece that just captured is only en prise if taking it back nets at
    // least a pawn more than it took: a trade is no loss.
    const int32_t took = mv.capture() ? exchangeValue(*mv.capture()) : 0;
    for (const auto& piece : hanging(a, side)) {
        if (piece.square != to || see(a, to, !side) >= took + 100) facts.push_back(Fact{fact::Hanging{piece}});
    }
    append(facts, newThreats(before, after, side));
    if (mv.role() == Role::Pawn && isPassed(a, to, side) && !isPassed(b, mv.from(), side))
        facts.push_back(Fact{fact::PassedPawn{Placed{side, Role::Pawn, to}}});
    return unique(std::move(facts));
}

std::vector<Fact> lineFacts(const Position& start, std::span<const Move> line) {
    return lineFactsWith(start, line, nullptr);
}

std::vector<Fact> lineFactsWith(const Position& start, std::span<const Move> line, const Oracle* oracle) {
    if (line.empty()) return {};
    const auto& first = line.front();
    const Color side  = start.turn();
    const Walk walk(start, line);
    const bool mate = walk.end().isCheckmate();
    std::vector<Fact> facts;

    if (const auto wing = first.castlingSide()) facts.push_back(Fact{fact::Castles{side, *wing}});
    // A promotion among the side's moves along the line.
    for (std::size_t i = 0; i < line.size(); i += 2) {
        if (const auto role = line[i].promotion()) {
            facts.push_back(Fact{fact::Promotes{side, *role, line[i].to()}});
            break;
        }
    }
    append(facts, newThreatsWith(start, walk.positions[1], side, oracle));
    append(facts, pawnBreak(start, first, side));
    append(facts, gainsSpace(start, first, side));
    append(facts, develops(start, first, side));
    append(facts, minorityAttack(start, first, side));
    append(facts, endgameFacts(start, first));
    append(facts, newFormations(walk));
    append(facts, newMajorities(walk));
    append(facts, luft(start, first, side));
    append(facts, breakthrough(walk, side));
    append(facts, race(walk));

    for (std::size_t i = 0; i < line.size(); i += 2) {
        const auto& after = walk.positions[i + 1];
        if (after.checkers().count() >= 2) facts.push_back(Fact{fact::DoubleCheck{side}});
        if (i / 2 < TACTIC_MOVES) append(facts, cashedTactics(walk, i, side, mate, oracle));
    }

    const std::span<const Move> counted = line.first(std::min(line.size(), MATERIAL_PLIES));
    const int32_t swing                 = settledSwing(start, counted, side);
    append(facts, material(start, line));
    // Taking an undefended piece and keeping it.
    if (const auto captured = first.capture(); captured && *captured != Role::Pawn) {
        if (start.board().attacksTo(first.to(), !side, start.board().occupied()).isEmpty() &&
            swing >= value(*captured) - 1) {
            if (const auto piece = Placed::at(start.board(), first.to())) facts.push_back(Fact{fact::Hanging{*piece}});
        }
    }
    if (const auto piece = sacrificed(walk, side)) facts.push_back(Fact{fact::Sacrifice{side, *piece}});
    append(facts, combinations(walk, side, swing > 0 || mate));
    append(facts, clearances(walk, side, mate));
    if (mate) {
        const Color winner   = !walk.end().turn();
        const auto plies     = static_cast<uint32_t>(line.size());
        const uint32_t moves = winner == side ? (plies + 1) / 2 : plies / 2;
        // The mate is what the line threatens and wins.
        std::erase_if(facts, [](const Fact& f) {
            return std::holds_alternative<fact::WinsMaterial>(f) || std::holds_alternative<fact::ThreatensMate>(f) ||
                   std::holds_alternative<fact::ThreatensCapture>(f);
        });
        facts.push_back(Fact{fact::Mate{winner, moves, matePattern(walk.end())}});
    }
    // A mate makes the rest of the position beside the point.
    if (!mate) {
        if (keepsInitiative(walk, side)) facts.push_back(Fact{fact::Initiative{side}});
        if (const auto checker = perpetualCheck(walk)) facts.push_back(Fact{fact::PerpetualCheck{*checker}});
        if (walk.end().isStalemate()) facts.push_back(Fact{fact::Stalemate{walk.end().turn()}});
        const auto& settled = walk.positions[std::min(line.size(), SETTLING_PLIES)];
        if (const auto e = ending(settled.board()); e && ending(start.board()) != e)
            facts.push_back(Fact{fact::Endgame{side, *e}});
        // The queens traded by a side well ahead: it simplifies.
        if (start.board().queens().count() == 2 && settled.board().queens().count() == 0) {
            for (const auto c : {Color::White, Color::Black}) {
                if (balance(start.board(), c) >= 2 && balance(settled.board(), c) >= 2)
                    facts.push_back(Fact{fact::Simplifies{c}});
            }
        }
        append(facts, pawnStorm(walk, side));
        append(facts, weakColours(walk));
        const auto was = centre(start.board()), now = centre(settled.board());
        if ((was == Centre::Full || was == Centre::Locked) && now == Centre::Open)
            facts.push_back(Fact{fact::CentreOpens{side}});
        else if (now == Centre::Locked && was != Centre::Locked)
            facts.push_back(Fact{fact::CentreLocks{side}});
        append(facts, structureChanges(start, line));
    }
    return unique(std::move(facts));
}

std::optional<Fact> material(const Position& start, std::span<const Move> line) {
    const Color side    = start.turn();
    const auto counted  = line.first(std::min(line.size(), MATERIAL_PLIES));
    const int32_t swing = settledSwing(start, counted, side);
    const Color winner  = swing > 0 ? side : !side;
    if (swing == 0) return std::nullopt;
    return Fact{
        fact::WinsMaterial{winner, std::abs(swing), trade(start, counted, winner), taken(start, counted, winner)}};
}

std::optional<Placed> taken(const Position& start, std::span<const Move> all, Color winner) {
    const auto line = all.first(std::min(all.size(), MATERIAL_PLIES));
    const auto t    = trade(start, line, winner);
    if (!t) return std::nullopt;
    std::size_t kind = 0;
    for (std::size_t k = 1; k < 4; ++k) {
        if (t->won[k] > 0) {
            kind = k;
            break;
        }
    }
    if (kind == 0) return std::nullopt;
    const bool exchange = kind == 2 && t->gave[1] > 0;
    if (t->won[1] + t->won[2] + t->won[3] != 1 || t->won[0] != 0 || exchange) return std::nullopt;
    const auto kind_of = [](Role role) -> std::size_t {
        switch (role) {
            case Role::Knight:
            case Role::Bishop:
                return 1;
            case Role::Rook:
                return 2;
            case Role::Queen:
                return 3;
            default:
                return 0;
        }
    };
    const std::size_t first = start.turn() != winner ? 1 : 0;
    auto pos                = start;
    std::vector<std::pair<std::size_t, Placed>> captures;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const auto& mv = line[i];
        if (i % 2 == first && mv.isCapture()) {
            const auto piece = Placed::at(pos.board(), mv.to());
            if (piece && kind_of(piece->role) == kind) captures.emplace_back(i, *piece);
        }
        pos.playUnchecked(mv);
    }
    if (captures.size() != 1) return std::nullopt;
    const auto [ply, piece] = captures.front();
    // Back to where the piece stood when the line started.
    auto square = piece.square;
    for (std::size_t i = ply; i-- > 0;) {
        const auto& mv = line[i];
        if (i % 2 != first && mv.to() == square) {
            if (mv.isPromotion() || mv.isCastle()) return std::nullopt;
            square = mv.from();
        }
    }
    const auto there = Placed::at(start.board(), square);
    if (there && there->role == piece.role && there->color == piece.color) return there;
    return std::nullopt;
}

std::optional<Fact> intermezzo(std::optional<Move> previous, const Position& start, std::span<const Move> line) {
    const Color side = start.turn();
    if (!previous || !previous->isCapture() || line.empty()) return std::nullopt;
    const auto square = previous->to();
    const auto& first = line.front();
    // Taking back was possible at once.
    bool could = false;
    for (const auto& m : start.legalMoves()) {
        if (m.isCapture() && m.to() == square) could = true;
    }
    if (!could || first.to() == square) return std::nullopt;
    auto after = start;
    after.playUnchecked(first);
    if (!after.isCheck() && !first.isCapture()) return std::nullopt;
    // The piece that took is still there when the side takes it, one move later.
    if (line.size() < 3) return std::nullopt;
    const auto& later = line[2];
    const bool stays  = line[1].from() != square;
    if (stays && later.isCapture() && later.to() == square) return Fact{fact::Intermezzo{side}};
    return std::nullopt;
}

std::optional<Ending> ending(const Board& board) {
    const auto each = [&](Bitboard pieces) { return (pieces & board.white()).any() && (pieces & board.black()).any(); };
    const auto queens = board.queens(), rooks = board.rooks(), bishops = board.bishops(), knights = board.knights();
    const auto minors = bishops | knights;
    const bool q = queens.any(), r = rooks.any(), m = minors.any();
    if (!q && !r && !m) return Ending::Pawn;
    if (!q && r && !m && each(rooks)) return Ending::Rook;
    if (q && !r && !m && each(queens)) return Ending::Queen;
    if (!q && !r && m && knights.isEmpty() && bishops.count() == 2 && each(bishops)) {
        const int light = (bishops & Bitboard::lightSquares()).count();
        return light == 1 ? Ending::OppositeBishops : Ending::Minor;
    }
    if (!q && !r && m && each(minors)) return Ending::Minor;
    if (!q) return Ending::Queenless;
    return std::nullopt;
}

}  // namespace fcg::commentary
