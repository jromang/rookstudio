#include "oracle.hpp"

#include <algorithm>

#include "material.hpp"

namespace fcg::commentary {

namespace {

// Plies of a threat's line its material is counted over, as for any line.
constexpr std::size_t THREAT_PLIES = 10;
// A mate further away than this is a winning attack, not a threat of mate.
constexpr int32_t MATE_THREAT_MOVES = 3;

// Kinds of material as Trade counts them: pawns, pieces, rooks, queens.
std::size_t kind(Role role) noexcept {
    switch (role) {
        case Role::Pawn:
            return 0;
        case Role::Knight:
        case Role::Bishop:
            return 1;
        case Role::Rook:
            return 2;
        default:
            return 3;
    }
}

// The piece `side`, starting `line`, comes out of it with: the first it takes of the most valuable kind it ends up
// ahead in, what is traded on the way left aside. None when it ends up ahead in pawns only, as when the line is a race
// to promote.
std::optional<Placed> wonPiece(const Position& start, std::span<const Move> line, Color side) {
    std::optional<std::size_t> wanted;
    if (const auto t = trade(start, line, side)) {
        for (std::size_t k = 3; k >= 1; --k) {
            if (t->won[k] > 0) {
                wanted = k;
                break;
            }
        }
        if (!wanted) return std::nullopt;
    }
    // The captures do not settle (no trade): the first piece taken.
    auto pos = start;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const auto& mv = line[i];
        if (i % 2 == 0 && mv.isCapture() && !mv.isEnPassant()) {
            const auto taken = Placed::at(pos.board(), mv.to());
            if (taken && taken->role != Role::Pawn && (!wanted || kind(taken->role) == *wanted)) return taken;
        }
        pos.playUnchecked(mv);
    }
    return std::nullopt;
}

}  // namespace

void RecordedOracle::answer(const Position& pos, std::vector<std::string> only, std::vector<std::string> line,
                            Score score) {
    answer(pos.fen(), std::move(only), std::move(line), score);
}

void RecordedOracle::answer(const std::string& fen, std::vector<std::string> only, std::vector<std::string> line,
                            Score score) {
    std::sort(only.begin(), only.end());
    answers_[{fen, std::move(only)}] = {std::move(line), score};
}

std::optional<Searched> RecordedOracle::search(const Position& pos, std::span<const Move> only) const {
    std::vector<std::string> key;
    for (const auto& m : only) key.push_back(uci(m));
    std::sort(key.begin(), key.end());
    const auto it = answers_.find({pos.fen(), key});
    if (it == answers_.end()) return std::nullopt;
    Searched found;
    found.score = it->second.second;
    auto p      = pos;
    for (const auto& text : it->second.first) {
        const auto m = p.parseUci(text);
        if (!m) return std::nullopt;
        p.playUnchecked(*m);
        found.moves.push_back(*m);
    }
    return found;
}

std::optional<Searched> movingAgain(const Position& pos, Color side, const Oracle& oracle) {
    if (pos.turn() == side) return std::nullopt;
    const auto again = pos.swappedTurn();
    if (!again) return std::nullopt;
    return oracle.search(*again, {});
}

std::optional<std::vector<Fact>> engineThreats(const Position& pos, Color side, const Oracle& oracle) {
    const auto found = movingAgain(pos, side, oracle);
    if (!found || found->moves.empty()) return std::nullopt;
    const auto first = found->moves.front();
    if (found->score.kind == Score::Kind::Mate && found->score.value >= 1 && found->score.value <= MATE_THREAT_MOVES)
        return std::vector<Fact>{Fact{fact::ThreatensMate{side, first}}};
    const auto again = pos.swappedTurn();
    if (!again) return std::nullopt;
    const std::span<const Move> counted(found->moves.data(), std::min(found->moves.size(), THREAT_PLIES));
    if (settledSwing(*again, counted, side) < 2) return std::vector<Fact>{};
    std::vector<Fact> out;
    if (const auto target = wonPiece(*again, counted, side))
        out.push_back(Fact{fact::ThreatensCapture{side, first, *target}});
    return out;
}

bool escapes(const Position& pos, Placed piece, const Oracle& oracle) {
    if (pos.turn() != piece.color) return false;
    std::vector<Move> own;
    for (const auto& m : pos.legalMoves()) {
        if (m.from() == piece.square) own.push_back(m);
    }
    if (own.empty()) return false;
    const auto found = oracle.search(pos, own);
    if (!found || found->moves.empty()) return false;
    auto at = found->moves.front().to();
    for (std::size_t j = 1; j < found->moves.size(); ++j) {
        const auto& mv = found->moves[j];
        if (j % 2 == 1) {
            if (mv.isCapture() && mv.to() == at) return false;
        } else if (mv.from() == at) {
            at = mv.to();
        }
    }
    return true;
}

std::optional<Fact> zugzwang(const Position& pos, Color side, double now, const std::function<double(Score)>& chances,
                             const Oracle& oracle) {
    const auto found = movingAgain(pos, side, oracle);
    if (!found) return std::nullopt;
    if (now - chances(found->score) >= ZUGZWANG_POINTS) return Fact{fact::Zugzwang{!side}};
    return std::nullopt;
}

}  // namespace fcg::commentary
