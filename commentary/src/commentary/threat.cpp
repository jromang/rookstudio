#include "threat.hpp"

#include "attack.hpp"

namespace fcg::commentary {

namespace {

// Captures gaining less than this (in centipawns) are not worth calling a threat.
constexpr int32_t MIN_GAIN = 200;

}  // namespace

std::vector<Fact> threats(const Position& pos, Color side) {
    // In check, the side not to move cannot be given the move: the check is the threat.
    std::optional<Position> swapped;
    if (pos.turn() != side) {
        swapped = pos.swappedTurn();
        if (!swapped) return {};
    }
    const Position& own_turn = swapped ? *swapped : pos;
    const auto moves         = own_turn.legalMoves();
    for (const auto& m : moves) {
        auto after = own_turn;
        after.playUnchecked(m);
        if (after.isCheckmate()) return {Fact{fact::ThreatensMate{side, m}}};
    }
    const auto& board = own_turn.board();
    // The cheapest capturer of the most profitable target; of equals, the last (Rust's max_by_key).
    const Move* best = nullptr;
    std::pair<int32_t, int32_t> best_key{};
    for (const auto& m : moves) {
        if (!m.isCapture() || m.capture() == Role::King) continue;
        const int32_t gain = see(board, m.to(), side);
        if (gain < MIN_GAIN) continue;
        const std::pair<int32_t, int32_t> key{gain, -static_cast<int32_t>(m.role())};
        if (!best || key >= best_key) {
            best     = &m;
            best_key = key;
        }
    }
    if (!best) return {};
    const auto target = Placed::at(board, best->to());
    if (!target) return {};
    return {Fact{fact::ThreatensCapture{side, *best, *target}}};
}

}  // namespace fcg::commentary
