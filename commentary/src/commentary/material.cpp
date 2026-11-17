#include "material.hpp"

#include <algorithm>
#include <array>
#include <vector>

#include "attack.hpp"

namespace fcg::commentary {

namespace {

// A line's first capture may wait this many plies: quiet moves that set it up.
constexpr std::size_t PRELUDE_PLIES = 4;
// From this ply on, captures a line ends on are left out of the count: an engine's line is less sure that far, and how
// such an exchange ends is not known.
constexpr std::size_t DEEP_PLIES = 6;

bool changes(const Move& m) noexcept { return m.isCapture() || m.isPromotion(); }

// The plies after which a line's captures have settled: the first ply, from its first capture or promotion on,
// followed by two quiet moves; no ply at all when it has neither in its first moves. A check is not quiet, nor the move
// that answers it: the combination goes on. A line that never settles and ends, deep enough, on a run of captures a
// quiet move leads to, counts up to that run: it is a new exchange, not the end of the first.
std::optional<std::size_t> settledAt(const Position& start, std::span<const Move> line) {
    const auto first_it = std::find_if(line.begin(), line.end(), changes);
    if (first_it == line.end()) return 0;
    const auto first = static_cast<std::size_t>(first_it - line.begin());
    if (first > PRELUDE_PLIES) return 0;
    auto pos = start;
    std::vector<bool> forcing;
    forcing.reserve(line.size());
    for (const auto& mv : line) {
        const bool answers_check = pos.isCheck();
        pos.playUnchecked(mv);
        forcing.push_back(changes(mv) || answers_check || pos.isCheck());
    }
    const auto quiet = [&](std::size_t i) { return i >= forcing.size() || !forcing[i]; };
    std::optional<std::size_t> settled;
    for (std::size_t i = first + 1; i < line.size(); ++i) {
        if (quiet(i) && quiet(i + 1)) {
            settled = i;
            break;
        }
    }
    std::size_t trailing = 0;
    for (auto it = line.rbegin(); it != line.rend() && changes(*it); ++it) ++trailing;
    const std::size_t run   = line.size() - trailing;
    const bool new_exchange = run >= DEEP_PLIES && run < line.size() && quiet(run - 1);
    if (settled) return settled;
    if (new_exchange) return run;
    return std::nullopt;
}

// What the side to move at the end of `line` gets by recapturing on its last capture's square, when that pays.
int32_t pendingRecapture(const Position& end, std::span<const Move> line) {
    if (line.empty() || !line.back().isCapture()) return 0;
    return see(end.board(), line.back().to(), end.turn()) / 100;
}

std::array<int32_t, 4> kinds(const Board& board, Color color) noexcept {
    const auto n = [&](Role role) { return (board.byRole(role) & board.byColor(color)).count(); };
    return {n(Role::Pawn), n(Role::Knight) + n(Role::Bishop), n(Role::Rook), n(Role::Queen)};
}

// The position once the line's captures settle, unless it stops on a recapture that pays.
std::optional<Position> settledPosition(const Position& start, std::span<const Move> line) {
    auto end           = start;
    const auto settled = settledAt(start, line).value_or(line.size());
    for (std::size_t i = 0; i < settled; ++i) end.playUnchecked(line[i]);
    if (settled < line.size() || pendingRecapture(end, line) == 0) return end;
    return std::nullopt;
}

}  // namespace

int32_t value(Role role) noexcept {
    switch (role) {
        case Role::Pawn:
            return 1;
        case Role::Knight:
        case Role::Bishop:
            return 3;
        case Role::Rook:
            return 5;
        case Role::Queen:
            return 9;
        default:
            return 0;
    }
}

int32_t balance(const Board& board, Color color) noexcept {
    const auto count = [&](Color c) {
        int32_t sum = 0;
        for (const auto r : ALL_ROLES) sum += (board.byRole(r) & board.byColor(c)).count() * value(r);
        return sum;
    };
    return count(color) - count(!color);
}

int32_t settledSwing(const Position& start, std::span<const Move> line, Color side) {
    auto pos = start;
    std::vector<int32_t> b{balance(pos.board(), side)};
    for (const auto& mv : line) {
        pos.playUnchecked(mv);
        b.push_back(balance(pos.board(), side));
    }
    if (const auto i = settledAt(start, line)) return b[*i] - b[0];
    // The recapture, if it pays, goes to the side to move.
    const int32_t pending = pendingRecapture(pos, line);
    return b[line.size()] - b[0] + (pos.turn() == side ? pending : -pending);
}

std::optional<Trade> lead(const Board& board, Color color) noexcept {
    const auto mine = kinds(board, color), theirs = kinds(board, !color);
    Trade t;
    for (std::size_t k = 0; k < 4; ++k) {
        const int32_t more = mine[k] - theirs[k];
        t.won[k]           = static_cast<uint8_t>(std::max(more, 0));
        t.gave[k]          = static_cast<uint8_t>(std::max(-more, 0));
    }
    if (t == Trade{}) return std::nullopt;
    return t;
}

std::optional<Trade> trade(const Position& start, std::span<const Move> line, Color winner) {
    const auto end = settledPosition(start, line);
    if (!end) return std::nullopt;
    const auto lost = [&](Color color) -> std::optional<std::array<uint8_t, 4>> {
        const auto before = kinds(start.board(), color), after = kinds(end->board(), color);
        std::array<uint8_t, 4> out{};
        for (std::size_t k = 0; k < 4; ++k) {
            const int32_t d = before[k] - after[k];
            if (d < 0 || d > 255) return std::nullopt;
            out[k] = static_cast<uint8_t>(d);
        }
        return out;
    };
    auto won = lost(!winner), gave = lost(winner);
    if (!won || !gave) return std::nullopt;
    for (std::size_t k = 0; k < 4; ++k) {
        const auto both = std::min((*won)[k], (*gave)[k]);
        (*won)[k] -= both;
        (*gave)[k] -= both;
    }
    return Trade{*won, *gave};
}

std::optional<Fact> compensation(const Position& start, std::span<const Move> line, Color side, double win) {
    const auto end = settledPosition(start, line);
    if (!end) return std::nullopt;
    const int32_t pawns = -balance(end->board(), side);
    if (pawns < 1 || -pawns >= balance(start.board(), side) || win < HOLDS) return std::nullopt;
    Trade deficit;
    const auto theirs = kinds(end->board(), !side), ours = kinds(end->board(), side);
    for (std::size_t k = 0; k < 4; ++k) {
        const auto n = static_cast<uint8_t>(std::abs(theirs[k] - ours[k]));
        if (theirs[k] > ours[k])
            deficit.won[k] = n;
        else
            deficit.gave[k] = n;
    }
    const Trade exchange{{0, 0, 1, 0}, {0, 1, 0, 0}};
    const bool plain = deficit.gave == std::array<uint8_t, 4>{} || deficit == exchange;
    fact::Compensation c;
    c.side  = side;
    c.pawns = pawns;
    if (plain) c.deficit = deficit;
    return Fact{c};
}

}  // namespace fcg::commentary
