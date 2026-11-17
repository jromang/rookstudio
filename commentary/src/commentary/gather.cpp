#include "gather.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <type_traits>

#include "attack.hpp"
#include "cause.hpp"
#include "endgame.hpp"
#include "facts.hpp"
#include "line.hpp"
#include "material.hpp"
#include "structure.hpp"

namespace fcg::commentary {

namespace {

template <typename T>
bool is(const Fact& f) {
    return std::holds_alternative<T>(f);
}

void append(std::vector<Fact>& to, std::vector<Fact> more) {
    for (auto& f : more) to.push_back(std::move(f));
}

void append(std::vector<Fact>& to, std::optional<Fact> more) {
    if (more) to.push_back(std::move(*more));
}

// The most telling first, facts of the same weight in the order they came (Rust's stable sort_by_key).
void bySalience(std::vector<Fact>& facts) {
    std::stable_sort(facts.begin(), facts.end(),
                     [](const Fact& a, const Fact& b) { return salience(a) > salience(b); });
}

// The facts good for `side`, most telling first, each kind of fact once.
std::vector<Fact> goodFor(std::vector<Fact> facts, Color side) {
    std::vector<Fact> kept;
    for (auto& fact : facts) {
        if (beneficiary(fact) != side) continue;
        const bool known =
            std::any_of(kept.begin(), kept.end(), [&](const Fact& k) { return k.index() == fact.index(); });
        if (!known) kept.push_back(std::move(fact));
    }
    bySalience(kept);
    return kept;
}

// The zugzwang the first move of a line puts the opponent in, when the engine is there to tell: moving again, the side
// that played it would stand clearly worse than the line (`scored`) says it stands.
std::optional<Fact> zugzwangOf(const MoveContext& ctx, const Position& start, std::span<const Move> moves,
                               const EngineLine* scored) {
    if (!ctx.oracle || !scored || moves.empty()) return std::nullopt;
    // The engine asked here does not read the tables: against their certainty, its score for moving again would make a
    // zugzwang of any won ending.
    if (scored->isExact()) return std::nullopt;
    const Color side = start.turn();
    auto after       = start;
    after.playUnchecked(moves.front());
    const double now   = forSide(scored->whiteExpected(), side);
    const auto chances = [](Score score) {
        if (score.kind == Score::Kind::Cp) return expected(score.value);
        return expected(score.value > 0 ? CP_CAP : -CP_CAP);
    };
    return zugzwang(after, side, now, chances, *ctx.oracle);
}

// What only the engine's score settles about the facts of a line `side` starts: a material drop is a sacrifice only if
// the line holds, otherwise it is just losing material; checks and threats move after move are the initiative of a side
// holding the balance, and counterplay for a side standing worse; giving up material with the balance held is
// compensation.
std::vector<Fact> weighed(std::vector<Fact> facts, const Position& start, std::span<const Move> moves,
                          const EngineLine* line, Color side) {
    if (!line) {
        std::erase_if(facts, [](const Fact& f) { return is<fact::Sacrifice>(f) || is<fact::Initiative>(f); });
        return facts;
    }
    const double win = forSide(line->whiteExpected(), side);
    if (win < 50.0) std::erase_if(facts, [](const Fact& f) { return is<fact::Sacrifice>(f); });
    if (win < HOLDS) {
        for (auto& f : facts) {
            if (const auto* i = std::get_if<fact::Initiative>(&f)) f = Fact{fact::Counterplay{i->side}};
        }
    }
    if (std::none_of(facts.begin(), facts.end(), [](const Fact& f) { return is<fact::Mate>(f); }))
        append(facts, compensation(start, moves, side, win));
    return facts;
}

// How the move made the answer possible is worth telling when something comes of it: a loss for a piece left alone, a
// loss or a tactic for a line opened.
std::vector<Fact> withCauses(std::vector<Fact> punishment) {
    const bool lost   = std::any_of(punishment.begin(), punishment.end(), isConcrete);
    const bool tactic = std::any_of(punishment.begin(), punishment.end(),
                                    [](const Fact& f) { return salience(f) >= TACTIC && !isCause(f); });
    std::erase_if(punishment, [&](const Fact& f) {
        if (is<fact::Unguards>(f) || is<fact::Neglects>(f)) return !lost;
        if (is<fact::OpensLine>(f)) return !(lost || tactic);
        return false;
    });
    return punishment;
}

// The facts of a move that takes back what was just taken: taking back is no gain, and the piece taken was not left en
// prise. What the line wins beyond it still counts.
std::vector<Fact> retaken(const MoveContext& ctx, std::vector<Fact> found) {
    const Color mover = ctx.before->turn();
    if (!isRecapture(ctx.previous, ctx.played)) return found;
    const auto due = ctx.previous->capture();
    if (!due) return found;
    const auto square = ctx.played.to();
    std::erase_if(found, [&](const Fact& f) {
        const auto* h = std::get_if<fact::Hanging>(&f);
        return h && h->piece.square == square;
    });
    const auto counted = std::find_if(found.begin(), found.end(), [&](const Fact& f) {
        const auto* w = std::get_if<fact::WinsMaterial>(&f);
        return w && w->side == mover;
    });
    if (counted != found.end()) {
        const int32_t pawns = std::get<fact::WinsMaterial>(*counted).pawns;
        found.erase(counted);
        if (pawns > value(*due))
            found.push_back(Fact{fact::WinsMaterial{mover, pawns - value(*due), std::nullopt, std::nullopt}});
    }
    found.push_back(Fact{fact::Retakes{mover, square}});
    return found;
}

// What a line wins for `side`, in pawns, once its captures settle; negative when it loses.
int32_t swingOf(const Position& start, std::span<const Move> line, Color side) {
    const auto m = material(start, line);
    if (!m) return 0;
    const auto& w = std::get<fact::WinsMaterial>(*m);
    return w.side == side ? w.pawns : -w.pawns;
}

// A trade with one more thing given, of `role`'s kind, what both sides lost cancelled out; none when nothing is left
// won.
std::optional<Trade> havingGiven(Trade trade, Role role) {
    std::size_t kind = 0;
    if (role == Role::Knight || role == Role::Bishop) kind = 1;
    if (role == Role::Rook) kind = 2;
    if (role == Role::Queen) kind = 3;
    trade.gave[kind] = static_cast<uint8_t>(trade.gave[kind] + 1);
    const auto both  = std::min(trade.won[kind], trade.gave[kind]);
    trade.won[kind] -= both;
    trade.gave[kind] -= both;
    if (std::any_of(trade.won.begin(), trade.won.end(), [](uint8_t n) { return n > 0; })) return trade;
    return std::nullopt;
}

// The facts of the answer to a move `played` from `before`, with what the move itself has to do with them; `best` is
// the engine's best line there, `previous` the move before. What the move took is part of the bill, so that taking it
// back is no gain; so is what the better move would simply have taken back. A threat that stood before the move is not
// the answer's doing. And the facts say how the move made the answer possible.
std::vector<Fact> billed(const Position& before, const Move& played, std::optional<Move> previous,
                         const EngineLine* best, std::span<const Move> reply, std::vector<Fact> found) {
    const Color mover = before.turn();
    if (reply.empty()) return found;
    const auto& answer = reply.front();
    const auto& was    = before.board();
    std::erase_if(found, [&](const Fact& f) {
        const auto* t = std::get_if<fact::ThreatensCapture>(&f);
        return t && Placed::at(was, t->target.square) == t->target && see(was, t->target.square, !mover) > 0;
    });
    // A pawn the king could not catch before the move either is not running because of it.
    std::erase_if(found, [&](const Fact& f) { return is<fact::Outruns>(f) && outruns(before, answer.from()); });
    // A piece that took and is taken back was traded, not left en prise.
    if (played.isCapture() && answer.isCapture() && answer.to() == played.to()) {
        std::erase_if(found, [&](const Fact& f) {
            const auto* h = std::get_if<fact::Hanging>(&f);
            return h && h->piece.square == answer.to();
        });
    }
    // Taking back with another piece takes the same thing: nothing to add to the bill.
    const EngineLine* better = nullptr;
    if (best) {
        const std::optional<Move> first = best->moves.empty() ? std::nullopt : std::optional<Move>(best->moves.front());
        if (first != played && isRecapture(previous, first)) better = best;
    }
    const bool same =
        better && played.isCapture() && !better->moves.empty() && better->moves.front().to() == played.to();
    std::optional<Role> took;
    if (same) {
        better = nullptr;
    } else {
        took = played.capture();
    }
    const int32_t due = better ? std::max(swingOf(before, better->moves, mover), 0) : 0;
    if (better && !better->moves.empty() && due > 0 && better->moves.front().to() != played.to())
        found.push_back(Fact{fact::NotRetaken{mover, better->moves.front().to()}});
    if (took || due > 0) {
        int32_t lost = 0;
        std::optional<Trade> trade;
        std::optional<Placed> piece;
        const auto counted =
            std::find_if(found.begin(), found.end(), [](const Fact& f) { return is<fact::WinsMaterial>(f); });
        if (counted != found.end()) {
            const auto w = std::get<fact::WinsMaterial>(*counted);
            found.erase(counted);
            if (w.side != mover) {
                lost  = w.pawns;
                trade = w.trade;
                piece = w.taken;
            } else {
                lost = -w.pawns;
            }
        }
        const int32_t net = lost - (took ? value(*took) : 0) + due;
        // In what, when the count is still that of the answer's line and of what the move took.
        if (due >= 1)
            trade.reset();
        else if (took)
            trade = trade ? havingGiven(*trade, *took) : std::nullopt;
        const bool named = due == 0
                               ? (trade && trade->won[1] + trade->won[2] + trade->won[3] == 1 && trade->won[0] == 0)
                               : (piece && value(piece->role) == net);
        if (net >= 1) {
            found.push_back(Fact{fact::WinsMaterial{!mover, net, trade, named ? piece : std::nullopt}});
        } else if (net < 0) {
            found.push_back(Fact{fact::WinsMaterial{mover, -net, std::nullopt, std::nullopt}});
        }
    }
    append(found, causes(before, played, answer));
    return found;
}

// In the opening, a knight or a bishop already out is played again, without taking, while two of its side's still stand
// at home, and the better move brings one out.
std::optional<Fact> movesAgain(const MoveContext& ctx, const std::vector<Fact>& missed) {
    const Color mover                 = ctx.before->turn();
    const Role role                   = ctx.played.role();
    const std::array<Square, 4> homes = mover == Color::White
                                            ? std::array{Square::B1, Square::G1, Square::C1, Square::F1}
                                            : std::array{Square::B8, Square::G8, Square::C8, Square::F8};
    const auto& board                 = ctx.before->board();
    const auto minors                 = (board.knights() | board.bishops()) & board.byColor(mover);
    int waiting                       = 0;
    for (const auto sq : homes) waiting += minors.contains(sq);
    const bool from_home = std::find(homes.begin(), homes.end(), ctx.played.from()) != homes.end();
    const bool again = ctx.report->phase == Phase::Opening && (role == Role::Knight || role == Role::Bishop) &&
                       !ctx.played.isCapture() && !from_home && waiting >= 2 &&
                       std::any_of(missed.begin(), missed.end(), [](const Fact& f) { return is<fact::Develops>(f); });
    if (again) return Fact{fact::MovesAgain{Placed{mover, role, ctx.played.to()}}};
    return std::nullopt;
}

}  // namespace

Theory Theory::surveys(std::vector<std::tuple<Move, std::vector<uint8_t>, std::optional<std::string>>> moves,
                       const Move& played) {
    std::optional<Move> noted;
    if (std::any_of(moves.begin(), moves.end(), [&](const auto& m) { return std::get<0>(m) == played; }))
        noted = played;
    else if (!moves.empty())
        noted = std::get<0>(moves.front());
    Theory t;
    // The note is the remark on the first move named `noted`, if it has one.
    if (noted) {
        const auto it =
            std::find_if(moves.begin(), moves.end(), [&](const auto& m) { return std::get<0>(m) == *noted; });
        if (it != moves.end() && std::get<2>(*it)) t.note = std::pair{std::get<0>(*it), *std::get<2>(*it)};
    }
    for (auto& [m, nags, remark] : moves) t.moves.emplace_back(m, std::move(nags));
    t.source = Source::Surveys;
    return t;
}

Theory Theory::played(const std::vector<Move>& moves) {
    Theory t;
    for (const auto& m : moves) t.moves.emplace_back(m, std::vector<uint8_t>{});
    t.source = Source::Games;
    return t;
}

const std::vector<uint8_t>* Theory::marks(const Move& mv) const {
    for (const auto& [m, nags] : moves) {
        if (m == mv) return &nags;
    }
    return nullptr;
}

std::optional<Move> Theory::main() const {
    if (moves.empty()) return std::nullopt;
    return moves.front().first;
}

bool isCause(const Fact& f) { return is<fact::Unguards>(f) || is<fact::Neglects>(f) || is<fact::OpensLine>(f); }

bool isFeature(const Fact& f) {
    return salience(f) < NOTABLE && !(is<fact::PawnBreak>(f) || is<fact::GainsSpace>(f) || is<fact::WeakColour>(f) ||
                                      is<fact::Castles>(f) || is<fact::Develops>(f) || is<fact::Simplifies>(f) ||
                                      is<fact::Endgame>(f) || is<fact::CentreOpens>(f) || is<fact::CentreLocks>(f));
}

bool isConcrete(const Fact& f) {
    return is<fact::Mate>(f) || is<fact::ThreatensMate>(f) || is<fact::WinsMaterial>(f) || is<fact::Promotes>(f) ||
           is<fact::Race>(f) || is<fact::Trapped>(f) || is<fact::PerpetualCheck>(f) || is<fact::Stalemate>(f) ||
           is<fact::WrongBishop>(f) || is<fact::Zugzwang>(f) || is<fact::SpoilsWin>(f) || is<fact::SpoilsDraw>(f) ||
           is<fact::Tablebase>(f);
}

bool isRecapture(std::optional<Move> previous, std::optional<Move> mv) {
    return previous && mv && previous->isCapture() && mv->isCapture() && previous->to() == mv->to();
}

bool MoveFacts::missedChance() const {
    return std::none_of(punished_by.begin(), punished_by.end(), isConcrete) &&
           std::any_of(missed.begin(), missed.end(), isConcrete);
}

std::optional<Alternative> alternative(const MoveContext& ctx) {
    // That a capture has to be taken back needs no telling.
    const bool retakes = isRecapture(ctx.previous, ctx.played);
    if (!ctx.second || !ctx.report->only_move || retakes) return std::nullopt;
    if (ctx.second->moves.empty()) return std::nullopt;
    const auto& alt = ctx.second->moves.front();
    const std::span<const Move> rest(ctx.second->moves.data() + 1, ctx.second->moves.size() - 1);
    if (alt == ctx.played || rest.empty()) return std::nullopt;
    return Alternative{ctx.second, alt, rest};
}

MoveFacts gather(const MoveContext& ctx) {
    const auto& before = *ctx.before;
    const Color mover  = before.turn();
    auto after         = before;
    after.playUnchecked(ctx.played);
    MoveFacts facts;
    std::vector<Fact> exact;
    if (ctx.tables) exact = ctx.tables->moveFacts(before, ctx.played);
    const auto line_facts = [&](const Position& start, std::span<const Move> moves) {
        return lineFactsWith(start, moves, ctx.oracle);
    };
    if (!ctx.report->judgment) {
        std::vector<Move> line{ctx.played};
        if (ctx.reply) line.insert(line.end(), ctx.reply->moves.begin(), ctx.reply->moves.end());
        auto found = line_facts(before, line);
        append(found, intermezzo(ctx.previous, before, line));
        append(found, exact);
        append(found, zugzwangOf(ctx, before, line, ctx.reply));
        found = weighed(std::move(found), before, line, ctx.reply, mover);
        // A pawn already out of the king's reach before the move only runs on.
        std::erase_if(found, [&](const Fact& f) { return is<fact::Outruns>(f) && outruns(before, ctx.played.from()); });
        // The only move may be so because a piece under attack had to step away.
        if (ctx.report->only_move) append(found, saved(before, ctx.played));
        facts.achieves = goodFor(retaken(ctx, std::move(found)), mover);
        // What the next best move runs into: why this one is the only one.
        if (const auto other = alternative(ctx)) {
            auto tried = before;
            tried.playUnchecked(other->alt);
            auto f2 = line_facts(tried, other->rest);
            append(f2, intermezzo(other->alt, tried, other->rest));
            f2 = weighed(std::move(f2), tried, other->rest, other->second, !mover);
            f2 = billed(before, other->alt, ctx.previous, ctx.best, other->rest, std::move(f2));
            f2 = withCauses(goodFor(std::move(f2), !mover));
            std::erase_if(f2, [](const Fact& f) { return is<fact::Luft>(f); });
            // Worth telling when the other move loses something outright, and the move played does not simply mate.
            const bool loses =
                std::any_of(f2.begin(), f2.end(), [](const Fact& f) { return isConcrete(f) || salience(f) >= TACTIC; });
            const bool mates = !facts.achieves.empty() && is<fact::Mate>(facts.achieves.front());
            if (loses && !mates) facts.otherwise = std::move(f2);
        }
    } else {
        // Only what the engine's answer shows: a static guess could be wrong.
        std::vector<Fact> answer;
        if (ctx.reply) {
            const auto& r = *ctx.reply;
            auto found    = line_facts(after, r.moves);
            append(found, intermezzo(ctx.played, after, r.moves));
            append(found, zugzwangOf(ctx, after, r.moves, &r));
            found  = weighed(std::move(found), after, r.moves, &r, !mover);
            answer = billed(before, ctx.played, ctx.previous, ctx.best, r.moves, std::move(found));
        }
        // And the lasting damage the move does to its own side, unless the answer mates.
        const bool mates = std::any_of(answer.begin(), answer.end(), [](const Fact& f) { return is<fact::Mate>(f); });
        std::vector<Fact> damage;
        if (!mates) damage = structureChanges(before, std::span<const Move>(&ctx.played, 1));
        // A square made for the opponent's king is no punishment.
        std::vector<Fact> punishment;
        append(punishment, exact);
        append(punishment, std::move(answer));
        append(punishment, std::move(damage));
        std::erase_if(punishment, [](const Fact& f) { return is<fact::Luft>(f); });
        facts.punished_by = withCauses(goodFor(std::move(punishment), !mover));
        if (ctx.best && (ctx.best->moves.empty() || ctx.best->moves.front() != ctx.played)) {
            const auto& best = *ctx.best;
            auto found       = line_facts(before, best.moves);
            append(found, intermezzo(ctx.previous, before, best.moves));
            append(found, zugzwangOf(ctx, before, best.moves, &best));
            auto missed = weighed(std::move(found), before, best.moves, &best, mover);
            // Taking back what was just taken is not a gain.
            const std::optional<Move> first =
                best.moves.empty() ? std::nullopt : std::optional<Move>(best.moves.front());
            if (isRecapture(ctx.previous, first))
                std::erase_if(missed, [](const Fact& f) { return is<fact::WinsMaterial>(f) || is<fact::Hanging>(f); });
            // What the better move does about the answer to the move played.
            if (first && ctx.reply && !ctx.reply->moves.empty())
                append(missed, remedies(before, ctx.played, *first, ctx.reply->moves.front(), ctx.previous));
            facts.missed = goodFor(std::move(missed), mover);
        }
        if (auto again = movesAgain(ctx, facts.missed)) {
            facts.punished_by.push_back(std::move(*again));
            bySalience(facts.punished_by);
        }
        // Nothing punishes the move, and the better one wins something: a chance missed. What follows the move is then
        // only the game going on: a passing threat or a small change in the structure is not what the move is faulted
        // for.
        if (facts.missedChance()) {
            std::erase_if(facts.punished_by,
                          [](const Fact& f) { return !(salience(f) >= NOTABLE && !is<fact::ThreatensCapture>(f)); });
        }
    }
    return facts;
}

bool atOnce(const Fact& fact, const Position& after, const Move& answer) {
    const auto to     = answer.castlingSide() ? kingTo(*answer.castlingSide(), after.turn()) : answer.to();
    const auto& board = after.board();
    return std::visit(
        [&](const auto& f) -> bool {
            using T = std::decay_t<decltype(f)>;
            using namespace fact;
            if constexpr (std::is_same_v<T, Fork> || std::is_same_v<T, Skewer>) {
                return f.attacker.square == to;
            } else if constexpr (std::is_same_v<T, Pin>) {
                return f.pinner.square == to && Placed::at(board, f.pinned.square) == f.pinned;
            } else if constexpr (std::is_same_v<T, Sacrifice>) {
                return f.piece.square == to;
            } else if constexpr (std::is_same_v<T, PawnBreak> || std::is_same_v<T, GainsSpace> ||
                                 std::is_same_v<T, Breakthrough>) {
                return f.pawn.square == to;
            } else if constexpr (std::is_same_v<T, Overloaded>) {
                // The capture that asks too much of the piece.
                return answer.isCapture() && f.first.square == to && Placed::at(board, to) == f.first;
            } else if constexpr (std::is_same_v<T, Clearance>) {
                // The move that leaves the square.
                return answer.from() == f.piece.square && Placed::at(board, f.piece.square) == f.piece;
            } else if constexpr (std::is_same_v<T, Opposition> || std::is_same_v<T, Outruns> ||
                                 std::is_same_v<T, EntersSquare> || std::is_same_v<T, CutsOff> ||
                                 std::is_same_v<T, KeySquare> || std::is_same_v<T, Lucena> ||
                                 std::is_same_v<T, Philidor>) {
                // What the endgame books name is the move's own doing.
                return true;
            } else if constexpr (std::is_same_v<T, DiscoveredAttack> || std::is_same_v<T, DoubleCheck>) {
                const auto found = moveFacts(after, answer);
                return std::find(found.begin(), found.end(), fact) != found.end();
            } else {
                return false;
            }
        },
        fact);
}

}  // namespace fcg::commentary
