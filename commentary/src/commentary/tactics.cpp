#include "tactics.hpp"

#include <algorithm>

#include "attack.hpp"
#include "material.hpp"

namespace fcg::commentary {

namespace {

bool isSlider(Role role) noexcept { return role == Role::Bishop || role == Role::Rook || role == Role::Queen; }

bool defended(const Board& board, Square square) {
    const auto color = board.colorAt(square);
    return color && board.attacksTo(square, *color, board.occupied()).any();
}

// Worth attacking for the piece on `from`: the king, a piece worth more, or an undefended one that does not defend
// itself by attacking the attacker.
bool worthHitting(const Board& board, Square target, Square from) {
    const auto attacker = board.roleAt(from);
    if (!attacker) return false;
    const auto role = board.roleAt(target);
    if (role == Role::King) return true;
    if (!role || role == Role::Pawn) return false;
    return value(*role) > value(*attacker) || (!defended(board, target) && !board.attacksFrom(target).contains(from));
}

// The piece on `square` shields `king` from an enemy slider.
bool isPinned(const Board& board, Square square, Square king) {
    const auto color = board.colorAt(square);
    if (!color) return false;
    const auto without = board.occupied() ^ Bitboard::from(square);
    for (const auto s : board.byColor(!*color) & board.sliders()) {
        const auto role = board.roleAt(s).value_or(Role::Pawn);
        const auto ray  = attacks::of({!*color, role}, s, without);
        if (ray.contains(king) && attacks::between(s, king).contains(square)) return true;
    }
    return false;
}

Board without(const Board& board, Square square) {
    Board b = board;
    b.removePieceAt(square);
    return b;
}

}  // namespace

bool inBadSpot(const Board& board, Square square) {
    const auto piece = Placed::at(board, square);
    if (!piece) return false;
    const auto attackers = board.attacksTo(square, !piece->color, board.occupied());
    bool cheaper         = false;
    for (const auto a : attackers) {
        const auto r = board.roleAt(a);
        if (r && r != Role::King && value(*r) < value(piece->role)) cheaper = true;
    }
    return attackers.any() && (!defended(board, square) || cheaper);
}

std::optional<Fact> fork(const Board& board, Square square) {
    const auto attacker = Placed::at(board, square);
    if (!attacker || attacker->role == Role::King) return std::nullopt;
    if (inBadSpot(board, square)) return std::nullopt;
    std::vector<Placed> targets;
    for (const auto t : board.attacksFrom(square) & board.byColor(!attacker->color)) {
        if (!worthHitting(board, t, square)) continue;
        if (const auto p = Placed::at(board, t)) targets.push_back(*p);
    }
    if (targets.size() < 2) return std::nullopt;
    return Fact{fact::Fork{*attacker, std::move(targets)}};
}

std::vector<Fact> pins(const Board& board, Color color) {
    const auto king = board.kingOf(!color);
    if (!king) return {};
    const auto occupied = board.occupied();
    std::vector<Fact> found;
    for (const auto s : board.byColor(color) & board.sliders()) {
        const auto pinner = Placed::at(board, s);
        if (!pinner) continue;
        if (!attacks::of({color, pinner->role}, s, Bitboard()).contains(*king)) continue;
        const auto blockers = attacks::between(s, *king) & occupied;
        if (blockers.count() != 1) continue;
        const auto p      = *blockers.first();
        const auto pinned = Placed::at(board, p), behind = Placed::at(board, *king);
        if (!pinned || !behind || pinned->color == color) continue;
        // It would be lost: attacked by something cheaper, or simply undefended.
        const bool cannot_escape = inBadSpot(board, p);
        // It cannot take a piece it attacks, worth more than itself or undefended.
        const auto line     = attacks::ray(s, *king);
        bool blocked_attack = false;
        for (const auto t : board.attacksFrom(p) & board.byColor(color) & ~line) {
            const auto r = board.roleAt(t);
            if ((r && value(*r) > value(pinned->role)) || !defended(board, t)) {
                blocked_attack = true;
                break;
            }
        }
        if (cannot_escape || blocked_attack) found.push_back(Fact{fact::Pin{*pinner, *pinned, *behind}});
    }
    return found;
}

std::optional<Fact> skewer(const Board& board, Square square) {
    const auto attacker = Placed::at(board, square);
    if (!attacker || !isSlider(attacker->role)) return std::nullopt;
    const auto occupied = board.occupied();
    const auto enemies  = board.byColor(!attacker->color);
    for (const auto f : board.attacksFrom(square) & enemies) {
        const auto front = Placed::at(board, f);
        if (!front) continue;
        for (const auto b : enemies) {
            // `b` is the first piece beyond `front` on the attacker's line.
            if ((attacks::between(square, b) & occupied) != Bitboard::from(f)) continue;
            const auto behind = Placed::at(board, b);
            if (!behind) continue;
            const bool front_worth_more = front->role == Role::King || value(front->role) > value(behind->role);
            // Once `front` steps aside, taking `behind` must pay.
            if (front_worth_more && behind->role != Role::Pawn && see(without(board, f), b, attacker->color) > 0)
                return Fact{fact::Skewer{*attacker, *front, *behind}};
        }
    }
    return std::nullopt;
}

std::optional<std::pair<Placed, Placed>> shielded(const Board& board, Square square) {
    const auto color = board.colorAt(square);
    if (!color) return std::nullopt;
    const auto occupied = board.occupied();
    const auto opened   = without(board, square);
    for (const auto s : board.byColor(!*color) & board.sliders()) {
        if (!board.attacksFrom(s).contains(square)) continue;
        const auto pinner = Placed::at(board, s);
        if (!pinner) continue;
        const auto beyond = opened.attacksFrom(s) & board.byColor(*color) & ~board.attacksFrom(s);
        std::optional<Square> found;
        for (const auto b : beyond) {
            if ((attacks::between(s, b) & occupied) == Bitboard::from(square)) {
                found = b;
                break;
            }
        }
        if (!found) continue;
        const auto behind = Placed::at(board, *found);
        if (!behind) continue;
        const bool falls =
            behind->role == Role::King || (behind->role != Role::Pawn && see(opened, *found, !*color) > 0);
        if (falls) return std::pair{*pinner, *behind};
    }
    return std::nullopt;
}

std::vector<Fact> discovered(const Board& board, Square from, Square to, Color color) {
    std::vector<Fact> found;
    for (const auto s : board.byColor(color) & board.sliders()) {
        if (s == to) continue;
        const auto attacker = Placed::at(board, s);
        if (!attacker) continue;
        const auto reach = attacks::of({color, attacker->role}, s, board.occupied());
        for (const auto t : reach& board.byColor(!color)) {
            if (!attacks::between(s, t).contains(from)) continue;
            // A discovered attack needs only a target worth more, or one that can be won.
            const auto role = board.roleAt(t);
            const bool worthwhile =
                role == Role::King || (role && (value(*role) > value(attacker->role) || see(board, t, color) > 0));
            if (worthwhile) {
                if (const auto target = Placed::at(board, t))
                    found.push_back(Fact{fact::DiscoveredAttack{*attacker, *target}});
            }
        }
    }
    return found;
}

std::vector<Placed> trapped(const Position& pos, Color color) {
    std::optional<Position> swapped;
    if (pos.turn() != color) {
        swapped = pos.swappedTurn();
        if (!swapped) return {};
    }
    const Position& own_turn = swapped ? *swapped : pos;
    if (own_turn.isCheck()) return {};
    const auto& board = own_turn.board();
    const auto king   = board.kingOf(color);
    const auto moves  = own_turn.legalMoves();
    std::vector<Placed> out;
    for (const auto sq : board.byColor(color) & ~board.pawns() & ~board.kings()) {
        if (!inBadSpot(board, sq)) continue;
        if (king && isPinned(board, sq, *king)) continue;
        const auto role = board.roleAt(sq).value_or(Role::Pawn);
        bool nowhere    = true;
        for (const auto& m : moves) {
            if (m.from() != sq) continue;
            if (m.capture() && value(*m.capture()) >= value(role)) {
                nowhere = false;
                break;
            }
            auto after = own_turn;
            after.playUnchecked(m);
            if (!inBadSpot(after.board(), m.to())) {
                nowhere = false;
                break;
            }
        }
        if (!nowhere) continue;
        if (const auto p = Placed::at(board, sq)) out.push_back(*p);
    }
    return out;
}

std::optional<MatePattern> matePattern(const Position& pos) {
    if (!pos.isCheckmate()) return std::nullopt;
    const auto& board  = pos.board();
    const Color loser  = pos.turn();
    const auto king    = board.kingOf(loser);
    const auto checker = pos.checkers().first();
    if (!king || !checker) return std::nullopt;
    const auto checker_role = board.roleAt(*checker);
    if (!checker_role) return std::nullopt;
    const auto own    = board.byColor(loser);
    const auto around = attacks::king(*king);
    if (checker_role == Role::Knight && (around & ~own).isEmpty()) return MatePattern::Smothered;
    const Rank back  = loser == Color::White ? Rank::First : Rank::Eighth;
    const Rank front = loser == Color::White ? Rank::Second : Rank::Seventh;
    // Walled in by its own pieces, on squares the mating side does not even need to cover.
    const auto wall = around & Bitboard::rank(front);
    bool blocked_in = (wall & ~own).isEmpty();
    for (const auto sq : wall) {
        if (attackedBy(board, sq, !loser)) blocked_in = false;
    }
    if (rankOf(*king) == back && rankOf(*checker) == back &&
        (checker_role == Role::Rook || checker_role == Role::Queen) && blocked_in)
        return MatePattern::BackRank;
    return std::nullopt;
}

}  // namespace fcg::commentary
