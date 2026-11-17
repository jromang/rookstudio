#pragma once

// What the facts are made of, as the Rust prototype had them (crates/facts): a piece where it stands, what a trade
// gives, the named pawn formations and breaks, the tables' verdict; with Rust's debug text of each, which the reference
// briefs quote.

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "position.hpp"

namespace fcg::commentary {

// A piece on a square.
struct Placed {
    Color color   = Color::White;
    Role role     = Role::Pawn;
    Square square = Square::A1;
    [[nodiscard]] static std::optional<Placed> at(const Board& board, Square square);
    bool operator==(const Placed&) const = default;
};

// The kind of endgame a line goes into.
enum class Ending : uint8_t {
    Queenless,        // the queens are off, rooks or minor pieces still on
    Pawn,             // kings and pawns
    Rook,             // rooks and pawns
    Queen,            // queens and pawns
    Minor,            // bishops and knights
    OppositeBishops,  // one bishop each, on squares of different colours
};

enum class MatePattern : uint8_t {
    BackRank,   // the king, walled in by its own pawns, is mated along its first rank
    Smothered,  // a knight mates a king surrounded by its own pieces
};

// A wing of the board: files a to c, or f to h.
enum class Wing : uint8_t { Queenside, Kingside };

// The kind of line a piece moves along.
enum class LineKind : uint8_t { File, Rank, Diagonal };
// The line two squares share, when they share one.
[[nodiscard]] std::optional<LineKind> lineKindOf(Square a, Square b) noexcept;

enum class Verdict : uint8_t { Win, Draw, Loss };
[[nodiscard]] constexpr Verdict flip(Verdict v) noexcept {
    return v == Verdict::Win ? Verdict::Loss : v == Verdict::Loss ? Verdict::Win : Verdict::Draw;
}

// The pawn formations the books name.
enum class Formation : uint8_t {
    Carlsbad,        // the d-pawns face each other; the owner's minority attack on the queen's wing
    Maroczy,         // pawns on c4 and e4, no d-pawn, against a d-pawn kept back and no c-pawn: the bind
    Hedgehog,        // pawns on a3, b3, d3 and e3, no c-pawn, against enemy pawns on their c4 and e4
    Stonewall,       // pawns on d4, e3 and f4, the d-file blocked
    KingsideChain,   // pawns on d4 and e5 against d5 and e6: the chain points to the king's wing
    QueensideChain,  // pawns on e4 and d5 against e5 and d6: the chain points to the queen's wing
    Benoni,          // a pawn on d5 and an e-pawn, no c-pawn, against c5 and d6 and no e-pawn
    Isolani,         // a d-pawn on d4 with no pawn of its own beside it, and no enemy d-pawn
};
inline constexpr std::array<Formation, 8> ALL_FORMATIONS{
    Formation::Carlsbad,      Formation::Maroczy,        Formation::Hedgehog, Formation::Stonewall,
    Formation::KingsideChain, Formation::QueensideChain, Formation::Benoni,   Formation::Isolani};

// What a pawn break means in the structure, when the books have a name for it.
struct Lever {
    enum class Kind : uint8_t {
        ChainBase,  // the pawn struck guards the head of a locked chain
        ChainHead,  // the pawn struck is the head of a locked chain, guarded by a pawn behind it
        Isolani,    // the pawn advancing is an isolated queen's pawn
        Freeing,    // the break strikes at the c4 or e4 pawn of a Maroczy bind, or around a hedgehog
    };
    Kind kind           = Kind::ChainBase;
    Formation formation = Formation::Carlsbad;  // Freeing: the formation it frees from; Carlsbad otherwise
    [[nodiscard]] static Lever freeing(Formation f) noexcept { return {Kind::Freeing, f}; }
    bool operator==(const Lever&) const = default;
};

// What a side has more of, and the opponent has more of, by kind: pawns, minor pieces, rooks, queens.
struct Trade {
    std::array<uint8_t, 4> won{};
    std::array<uint8_t, 4> gave{};
    bool operator==(const Trade&) const = default;
};

// Rust's debug text, for the facts' fields.
[[nodiscard]] std::string debugValue(const Placed& p);
[[nodiscard]] std::string debugValue(Color c);
[[nodiscard]] std::string debugValue(Role r);
[[nodiscard]] std::string debugValue(Square s);
[[nodiscard]] std::string debugValue(File f);
[[nodiscard]] std::string debugValue(CastlingSide s);
[[nodiscard]] std::string debugValue(const Move& m);
[[nodiscard]] std::string debugValue(int32_t v);
[[nodiscard]] std::string debugValue(uint32_t v);
[[nodiscard]] std::string debugValue(bool v);
[[nodiscard]] std::string debugValue(Ending e);
[[nodiscard]] std::string debugValue(MatePattern p);
[[nodiscard]] std::string debugValue(Wing w);
[[nodiscard]] std::string debugValue(LineKind k);
[[nodiscard]] std::string debugValue(Verdict v);
[[nodiscard]] std::string debugValue(Formation f);
[[nodiscard]] std::string debugValue(const Lever& l);
[[nodiscard]] std::string debugValue(const Trade& t);

template <typename T>
[[nodiscard]] std::string debugValue(const std::optional<T>& v) {
    return v ? "Some(" + debugValue(*v) + ")" : std::string("None");
}

template <typename T>
[[nodiscard]] std::string debugValue(const std::vector<T>& v) {
    std::string out = "[";
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (i) out += ", ";
        out += debugValue(v[i]);
    }
    return out + "]";
}

}  // namespace fcg::commentary
