#include "formation.hpp"

#include <algorithm>

#include "structure.hpp"

namespace fcg::commentary {

namespace {

// Whether an enemy pawn stands right in front of `color`'s pawn on `square`.
bool blocked(const Board& board, Square square, Color color) {
    const auto front = offset(square, color == Color::White ? 8 : -8);
    return front && pawns(board, !color).contains(*front);
}

}  // namespace

Square squareFor(Color color, File file, uint32_t steps) noexcept {
    const uint32_t rank = color == Color::White ? steps : 7 - steps;
    return square(file, static_cast<Rank>(rank));
}

std::vector<std::pair<Formation, Color>> formations(const Board& board) {
    using enum File;
    std::vector<std::pair<Formation, Color>> found;
    for (const auto side : {Color::White, Color::Black}) {
        const auto at = [&](Color color, File file, uint32_t steps) {
            return pawns(board, color).contains(squareFor(color, file, steps));
        };
        const auto on = [&](Color color, File file) { return (pawns(board, color) & Bitboard::file(file)).any(); };
        // Whether `color` has a pawn on `file` at least `steps` ranks up.
        const auto up = [&](Color color, File file, uint32_t steps) {
            for (const auto sq : pawns(board, color) & Bitboard::file(file)) {
                if (advance(sq, color) >= steps) return true;
            }
            return false;
        };
        const auto me     = [&](File file, uint32_t steps) { return at(side, file, steps); };
        const auto them   = [&](File file, uint32_t steps) { return at(!side, file, steps); };
        const auto mine   = [&](File file) { return on(side, file); };
        const auto theirs = [&](File file) { return on(!side, file); };

        if (me(D, 3) && them(D, 3) && (me(E, 1) || me(E, 2)) && !mine(C) && (them(C, 1) || them(C, 2)) && !theirs(E))
            found.emplace_back(Formation::Carlsbad, side);
        const auto hedgehog = [&](Color owner) {
            for (const auto f : {A, B, D, E}) {
                if (!at(owner, f, 2)) return false;
            }
            return !on(owner, C) && at(!owner, C, 3) && at(!owner, E, 3) && !on(!owner, D);
        };
        if (hedgehog(side)) found.emplace_back(Formation::Hedgehog, side);
        if (me(C, 3) && me(E, 3) && !mine(D) && !theirs(C) && (them(D, 1) || them(D, 2)) && !up(!side, E, 3) &&
            !hedgehog(!side))
            found.emplace_back(Formation::Maroczy, side);
        if (me(D, 3) && me(E, 2) && me(F, 3) && them(D, 3)) found.emplace_back(Formation::Stonewall, side);
        if (me(D, 3) && me(E, 4) && them(D, 3) && them(E, 2)) found.emplace_back(Formation::KingsideChain, side);
        if (me(E, 3) && me(D, 4) && them(E, 3) && them(D, 2)) found.emplace_back(Formation::QueensideChain, side);
        if (me(D, 4) && mine(E) && !mine(C) && them(C, 3) && them(D, 2) && !theirs(E))
            found.emplace_back(Formation::Benoni, side);
        if (me(D, 3) && !mine(C) && !mine(E) && !theirs(D) && (theirs(C) || theirs(E)))
            found.emplace_back(Formation::Isolani, side);
    }
    return found;
}

std::optional<Lever> lever(const Board& board, Square from, Square to, Square target, Color side) {
    const auto enemy = pawns(board, !side);
    // The plans of a formation are those of a middlegame: with few pieces left, a break is told by the chain it strikes
    // at, if any.
    const bool crowded = isCrowded(board);
    // Against the pawns that hold the side in: the c4 and e4 of a bind.
    const bool binder = (fileOf(target) == File::C || fileOf(target) == File::E) && advance(target, !side) == 3;
    const auto shapes = formations(board);
    const auto has    = [&](Formation f, Color c) {
        return std::find(shapes.begin(), shapes.end(), std::pair{f, c}) != shapes.end();
    };
    if (crowded && binder && has(Formation::Maroczy, !side)) return Lever::freeing(Formation::Maroczy);
    if (crowded && binder && has(Formation::Hedgehog, side)) return Lever::freeing(Formation::Hedgehog);
    if (crowded && fileOf(from) == File::D && advance(to, side) == 4 && has(Formation::Isolani, side))
        return Lever{Lever::Kind::Isolani, Formation::Carlsbad};
    // A chain has its head in the other half of the board, on files c to f, blocked there by a pawn. The pawns the
    // target guards, and those that guard it.
    const auto is_head = [&](Square pawn) {
        const auto f = fileOf(pawn);
        return (f == File::C || f == File::D || f == File::E || f == File::F) && advance(pawn, !side) >= 4 &&
               blocked(board, pawn, !side);
    };
    const auto guards  = attacks::pawn(!side, target) & enemy;
    const bool guarded = (attacks::pawn(side, target) & enemy).any();
    for (const auto g : guards) {
        if (is_head(g)) return Lever{Lever::Kind::ChainBase, Formation::Carlsbad};
    }
    if (guarded && is_head(target)) return Lever{Lever::Kind::ChainHead, Formation::Carlsbad};
    return std::nullopt;
}

}  // namespace fcg::commentary
