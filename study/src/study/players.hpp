#pragma once

// The table of names of a database (mockup M13, board 1): the spellings of one player brought together under one name,
// the games themselves untouched. It is part of the database, named in its manifest and committed as its edits are,
// so that a snapshot has the table of its games; everything that names the players reads it through the snapshot:
// the game list and its filters and sorts, the opening tree's reference games, a player's dossier (#90), a
// tournament's crosstable (#92).
//
// A player gathers name ids (the names of the White and Black columns); a name id is in one player at most. A name in
// no player is a player of its own, shown as it is written.

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fcg::study {

using GameId = uint64_t;

struct Player {
    std::string name;                 // as everything shows it: the FIDE list's "Surname, Given", or a spelling chosen
    int64_t fide = 0;                 // FIDE id, 0 when not known
    std::vector<uint32_t> spellings;  // the database's name ids it gathers, sorted
    // How they were brought together: "fide-id" (the FIDE id the games give), "list" (FIDE's rating list), "hand".
    std::string how;
    // From FIDE's rating list when matched with it, kept with the table (the list itself can be removed).
    std::string federation, title;
    int born     = 0;  // year
    int standard = 0, rapid = 0, blitz = 0;
    std::string list;  // the list's month, "2026-11"
};

class PlayerTable {
   public:
    PlayerTable() = default;
    explicit PlayerTable(std::vector<Player> players, std::vector<std::pair<GameId, GameId>> apart = {});

    [[nodiscard]] const std::vector<Player>& players() const noexcept { return players_; }
    // Pairs of games told apart by hand ("Keep both"): never offered as duplicates again. Sorted, the smaller first.
    [[nodiscard]] const std::vector<std::pair<GameId, GameId>>& apart() const noexcept { return apart_; }
    [[nodiscard]] bool keptApart(GameId a, GameId b) const;
    // The player a name id belongs to; none for a name in no player.
    [[nodiscard]] std::optional<std::size_t> playerOf(uint32_t name_id) const;
    [[nodiscard]] const Player* of(uint32_t name_id) const;
    [[nodiscard]] bool empty() const noexcept { return players_.empty() && apart_.empty(); }

    // As kept in the database's folder (players-NNNNNN.bin): "RSPLAYR1", then the pairs kept apart and the players,
    // each its numbers, its names and its spellings; quick to write and read with hundreds of thousands of players.
    [[nodiscard]] std::string bytes() const;
    // Throws std::runtime_error when the bytes are not a table.
    [[nodiscard]] static PlayerTable fromBytes(std::string_view bytes);

   private:
    void index();

    std::vector<Player> players_;
    std::vector<std::pair<GameId, GameId>> apart_;
    std::unordered_map<uint32_t, uint32_t> by_name_;  // name id -> player
};

// A player's name read for matching, as "Surname, Given" (PGN, FIDE), "Surname,G", "Surname G." or "Given Surname"
// write it: folded (lower case, the accents of Latin letters off), the surname's letters only.
struct NameParts {
    std::string surname;  // "vanderberg" for "van der Berg, Jan"
    std::string given;    // the given names' words, letters only: "jan", "j", "jan willem"
    char initial = 0;     // the given names' first letter, 0 when none
};
[[nodiscard]] NameParts nameParts(std::string_view name);
// Two given names that can be one person's: one of them not known, the same first word, or an initial and a name
// that starts with it.
[[nodiscard]] bool givenCompatible(std::string_view a, std::string_view b);
// Whether `a` is an initial (or initials) and `b` the names: "e" and "erik".
[[nodiscard]] bool givenInitialOnly(std::string_view given);
// A surname as other alphabets' transliterations write it alike: w as v, y and j as i, ph as f, ck as k, double letters
// as one ("Kowalenko" and "Kovalenko", "Dmitrij" and "Dmitry").
[[nodiscard]] std::string nameSkeleton(std::string_view letters);
// A name that says no player: empty, "?", "NN", "N.N.", "White", "Black".
[[nodiscard]] bool anonymousName(std::string_view name);

}  // namespace fcg::study
