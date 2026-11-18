#pragma once

// What the databases know of a position, for the questions about it (question.hpp): the games that reached it, the
// moves they played from it and how those scored, and the strongest of those games. From each database's opening tree
// for the positions of the games' first plies; for the others, and for a database without a tree or with one older
// than its edits, from a search of the position in every game. The answers are written from these numbers alone.

#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <study/store.hpp>
#include <study/tree.hpp>

#include "notation.hpp"
#include "phrases.hpp"
#include "position.hpp"

namespace fcg::commentary {

// A database the questions look in.
struct ReferenceBase {
    std::string name;  // as the program shows it
    std::shared_ptr<const study::Snapshot> snapshot;
    std::shared_ptr<const study::Tree> tree;  // none: the position is searched for in every game
};

// A move the databases' games played from the position.
struct PlayedMove {
    Move move;
    study::TreeStats stats;
    bool operator==(const PlayedMove&) const = default;
};

// One of the games that reached the position.
struct ReferenceGame {
    std::size_t base = 0;  // its database, by its place in the list
    study::GameId id = 0;
    std::string white, black, event;
    int year      = 0;  // 0 when not known
    int white_elo = 0, black_elo = 0;
    char result                                 = '*';
    bool operator==(const ReferenceGame&) const = default;
};

struct References {
    study::TreeStats total;            // the games that reached the position, every database counted
    std::vector<PlayedMove> moves;     // the moves they played from it, the most played first
    std::vector<ReferenceGame> games;  // the strongest of them, by their players' mean rating, then the newest
    bool listed = true;                // false: too many games to look through for the strongest
};

// Positions more games than this reached are not looked through for their strongest games: they are early in the
// opening, where the moves played tell more.
inline constexpr uint64_t LISTED_GAMES = 50'000;

// What `bases` hold of `pos`, with at most `strongest` games listed.
[[nodiscard]] References references(const Position& pos, std::span<const ReferenceBase> bases,
                                    std::size_t strongest = 5);

// The answers, complete sentences. The games that reached the position: how many, how they ended, the strongest.
[[nodiscard]] std::string gamesAnswer(const References& refs, std::span<const ReferenceBase> bases, Lang lang);
// The moves they played and how each scored for the side that played it; with `asked`, that move's numbers alone.
[[nodiscard]] std::string movesAnswer(const Position& pos, const References& refs, std::span<const ReferenceBase> bases,
                                      std::optional<Move> asked, Lang lang, PieceLetters letters);
// The opening's name, from the list of named openings; none known in words too.
[[nodiscard]] std::string openingAnswer(const std::optional<std::string>& name, Lang lang);

// "12,345,678" in English, "12 345 678" in French (narrow no-break spaces).
[[nodiscard]] std::string counted(uint64_t n, Lang lang);

}  // namespace fcg::commentary
