#include "fact_parts.hpp"

#include <cstdlib>

namespace fcg::commentary {

std::optional<Placed> Placed::at(const Board& board, Square square) {
    const auto piece = board.pieceAt(square);
    if (!piece) return std::nullopt;
    return Placed{piece->color, piece->role, square};
}

std::optional<LineKind> lineKindOf(Square a, Square b) noexcept {
    const int files = std::abs(static_cast<int>(fileOf(a)) - static_cast<int>(fileOf(b)));
    const int ranks = std::abs(static_cast<int>(rankOf(a)) - static_cast<int>(rankOf(b)));
    if (files == 0 && ranks == 0) return std::nullopt;
    if (files == 0) return LineKind::File;
    if (ranks == 0) return LineKind::Rank;
    if (files == ranks) return LineKind::Diagonal;
    return std::nullopt;
}

std::string debugValue(const Placed& p) {
    return "Placed { color: " + debug(p.color) + ", role: " + debug(p.role) + ", square: " + debug(p.square) + " }";
}
std::string debugValue(Color c) { return debug(c); }
std::string debugValue(Role r) { return debug(r); }
std::string debugValue(Square s) { return debug(s); }
std::string debugValue(File f) { return debug(f); }
std::string debugValue(CastlingSide s) { return debug(s); }
std::string debugValue(const Move& m) { return debug(m); }
std::string debugValue(int32_t v) { return std::to_string(v); }
std::string debugValue(uint32_t v) { return std::to_string(v); }
std::string debugValue(bool v) { return v ? "true" : "false"; }

std::string debugValue(Ending e) {
    constexpr const char* names[] = {"Queenless", "Pawn", "Rook", "Queen", "Minor", "OppositeBishops"};
    return names[static_cast<int>(e)];
}
std::string debugValue(MatePattern p) { return p == MatePattern::BackRank ? "BackRank" : "Smothered"; }
std::string debugValue(Wing w) { return w == Wing::Queenside ? "Queenside" : "Kingside"; }
std::string debugValue(LineKind k) {
    constexpr const char* names[] = {"File", "Rank", "Diagonal"};
    return names[static_cast<int>(k)];
}
std::string debugValue(Verdict v) {
    constexpr const char* names[] = {"Win", "Draw", "Loss"};
    return names[static_cast<int>(v)];
}
std::string debugValue(Formation f) {
    constexpr const char* names[] = {"Carlsbad",      "Maroczy",        "Hedgehog", "Stonewall",
                                     "KingsideChain", "QueensideChain", "Benoni",   "Isolani"};
    return names[static_cast<int>(f)];
}
std::string debugValue(const Lever& l) {
    switch (l.kind) {
        case Lever::Kind::ChainBase:
            return "ChainBase";
        case Lever::Kind::ChainHead:
            return "ChainHead";
        case Lever::Kind::Isolani:
            return "Isolani";
        default:
            return "Freeing(" + debugValue(l.formation) + ")";
    }
}
std::string debugValue(const Trade& t) {
    const auto list = [](const std::array<uint8_t, 4>& a) {
        return "[" + std::to_string(a[0]) + ", " + std::to_string(a[1]) + ", " + std::to_string(a[2]) + ", " +
               std::to_string(a[3]) + "]";
    };
    return "Trade { won: " + list(t.won) + ", gave: " + list(t.gave) + " }";
}

}  // namespace fcg::commentary
