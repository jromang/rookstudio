#include "attack.hpp"

#include <algorithm>

namespace fcg::commentary {

namespace {

// `color`'s cheapest piece attacking `target`, given which squares are still occupied.
std::optional<Square> cheapestAttacker(const Board& board, Square target, Color color, Bitboard occupied) {
    const auto attackers = board.attacksTo(target, color, occupied) & occupied;
    for (const auto role : ALL_ROLES) {
        if (const auto s = (attackers & board.byRole(role)).first()) return s;
    }
    return std::nullopt;
}

}  // namespace

int32_t exchangeValue(Role role) noexcept {
    switch (role) {
        case Role::Pawn:
            return 100;
        case Role::Knight:
            return 320;
        case Role::Bishop:
            return 330;
        case Role::Rook:
            return 500;
        case Role::Queen:
            return 900;
        default:
            return 20000;
    }
}

int32_t see(const Board& board, Square target, Color side) {
    const auto victim = board.roleAt(target);
    if (!victim) return 0;
    auto occupied = board.occupied();
    std::vector<int32_t> gains;
    gains.reserve(32);
    gains.push_back(exchangeValue(*victim));
    Color color = side;
    auto from   = cheapestAttacker(board, target, color, occupied);
    if (!from) return 0;
    for (;;) {
        const auto role        = board.roleAt(*from);
        const int32_t attacker = role ? exchangeValue(*role) : 0;
        const int32_t previous = gains.back();
        // Speculative: what the opponent would gain by taking this attacker back.
        gains.push_back(attacker - previous);
        if (std::max(-previous, attacker - previous) < 0) break;
        occupied ^= Bitboard::from(*from);
        color = !color;
        from  = cheapestAttacker(board, target, color, occupied);
        if (!from) break;
    }
    // The last recapture never happens: nobody is left to make it, or it would not pay.
    gains.pop_back();
    // Each side keeps the better of stopping and going on, from the last capture back.
    while (gains.size() > 1) {
        const int32_t last = gains.back();
        gains.pop_back();
        gains.back() = -std::max(-gains.back(), last);
    }
    return std::max(gains[0], 0);
}

std::vector<Placed> hanging(const Board& board, Color color) {
    std::vector<Placed> out;
    for (const auto s : board.byColor(color) & ~board.kings()) {
        if (see(board, s, !color) > 0) {
            if (const auto p = Placed::at(board, s)) out.push_back(*p);
        }
    }
    return out;
}

bool attackedBy(const Board& board, Square square, Color color) noexcept {
    return board.attacksTo(square, color, board.occupied()).any();
}

}  // namespace fcg::commentary
