#pragma once

// How far a game followed a database's games, and where it left them (the prototype's refdb/theory.rs): its theory,
// the moves many of the database's games played, then its novelty, the first move none had played. From the
// database's opening tree; a novelty is searched for in every game first, which finds the games that reached the
// position by other moves.

#include <cstdint>
#include <memory>
#include <optional>

#include <study/store.hpp>
#include <study/tree.hpp>

#include "review.hpp"

namespace fcg::commentary {

struct TheoryOptions {
    uint32_t book_games = 5;              // a move this many of the database's games played is theory
    std::optional<study::GameId> itself;  // the game reviewed, when the database holds it: not counted
    // The game's year: a single game of that year that played the move is taken for the game itself (another copy).
    std::optional<int> year;
    bool confirm = true;  // a novelty is searched for in every game before it is told
};

[[nodiscard]] Opening theoryOf(const study::Tree& tree, const std::shared_ptr<const study::Snapshot>& snapshot,
                               const MainLine& line, const TheoryOptions& options = {});

}  // namespace fcg::commentary
