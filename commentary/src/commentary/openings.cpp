#include "openings.hpp"

#include <cstddef>
#include <functional>
#include <string_view>
#include <unordered_map>

#include "text.hpp"

namespace fcg::assets {
extern const unsigned char lichess_openings[];
extern const std::size_t lichess_openings_size;
}  // namespace fcg::assets

namespace fcg::commentary {

namespace {

// A position as the table tells positions apart: the pieces, the side to move, the castling rights, and the en passant
// square when a capture there is legal (the prototype's Zobrist key, EnPassantMode::Legal).
struct Key {
    Board board;
    Color turn = Color::White;
    Castles castles;
    std::optional<Square> ep;

    explicit Key(const Position& pos)
        : board(pos.board()), turn(pos.turn()), castles(pos.castles()), ep(pos.epSquare()) {}
    bool operator==(const Key&) const = default;
};

struct KeyHash {
    std::size_t operator()(const Key& k) const noexcept {
        uint64_t h     = k.turn == Color::White ? 0x9E3779B97F4A7C15ull : 0xC2B2AE3D27D4EB4Full;
        const auto mix = [&](uint64_t v) { h ^= v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2); };
        for (const auto role : {Role::Pawn, Role::Knight, Role::Bishop, Role::Rook, Role::Queen, Role::King})
            mix(k.board.byRole(role).bits());
        mix(k.board.byColor(Color::White).bits());
        mix(k.ep ? static_cast<uint64_t>(index(*k.ep)) + 1 : 0);
        for (const auto c : {Color::White, Color::Black}) {
            mix(k.castles.has(c, CastlingSide::KingSide));
            mix(k.castles.has(c, CastlingSide::QueenSide));
        }
        return static_cast<std::size_t>(h);
    }
};

// The named positions, each with the first name the table gives it.
const std::unordered_map<Key, std::string_view, KeyHash>& names() {
    static const auto table = [] {
        std::unordered_map<Key, std::string_view, KeyHash> out;
        const std::string_view all(reinterpret_cast<const char*>(assets::lichess_openings),
                                   assets::lichess_openings_size);
        bool header = true;
        for (std::size_t at = 0; at < all.size();) {
            auto end = all.find('\n', at);
            if (end == std::string_view::npos) end = all.size();
            auto line = all.substr(at, end - at);
            at        = end + 1;
            if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
            if (header) {
                header = false;
                continue;
            }
            // eco, name, pgn
            const auto first  = line.find('\t');
            const auto second = first == std::string_view::npos ? first : line.find('\t', first + 1);
            if (second == std::string_view::npos) continue;
            const auto name = line.substr(first + 1, second - first - 1);
            auto pgn        = line.substr(second + 1);
            if (const auto tab = pgn.find('\t'); tab != std::string_view::npos) pgn = pgn.substr(0, tab);
            auto pos      = Position::initial();
            bool replayed = true;
            for (const auto token : text::splitWhitespace(pgn)) {
                if (token.ends_with('.')) continue;
                const auto mv = pos.parseSan(token);
                if (!mv) {
                    replayed = false;
                    break;
                }
                pos.playUnchecked(*mv);
            }
            if (replayed) out.try_emplace(Key(pos), name);
        }
        return out;
    }();
    return table;
}

}  // namespace

std::optional<std::string> openingName(std::span<const Move> moves) {
    const auto& table = names();
    auto pos          = Position::initial();
    std::optional<std::string_view> found;
    for (const auto& mv : moves) {
        pos.playUnchecked(mv);
        if (const auto it = table.find(Key(pos)); it != table.end()) found = it->second;
    }
    if (!found) return std::nullopt;
    return std::string(*found);
}

}  // namespace fcg::commentary
