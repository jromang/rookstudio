#pragma once

// A tournament's crosstable (#92, mockup M13, board 5) behind its views (view_crosstable.cpp): the settings each
// tournament keeps in the profile (its system and tie-breaks chosen, its results set by hand), the words and cells the
// views show, a followed broadcast's crosstable made from the games RookStudio follows and the rounds it kept in the
// "Broadcasts" database, Ctrl+K's "Crosstable: …", and the games a crosstable misses looked for in the user's other
// databases. The computations are study/crosstable.hpp's. Nothing here leaves the machine.

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <study/crosstable.hpp>
#include <study/store.hpp>

#include "lichess_broadcasts.hpp"

namespace fcg {

// What a tournament's crosstable keeps: its system and tie-breaks when chosen, its results set by hand.
struct CrosstableSettings {
    std::optional<study::SystemChoice> system;
    std::optional<std::vector<study::Tiebreak>> order;
    std::vector<study::HandResult> hand;
    // Where those results come from: "lichess-trf", the TRF file of a Swiss kept from Lichess (#95: its byes, late
    // joins and absences); empty: the user.
    std::string hand_from;
    [[nodiscard]] bool empty() const { return !system && !order && hand.empty(); }
    bool operator==(const CrosstableSettings&) const = default;
};

// Where a tournament's settings are kept: its database's folder and its key; a broadcast's, "lichess:" and its
// tournament's id.
[[nodiscard]] std::string crosstablePlace(const std::filesystem::path& database, const std::string& tournament);
[[nodiscard]] std::string broadcastPlace(const std::string& tour_id);

// The crosstables' settings in the profile (crosstables.json), written at each change.
class CrosstableStore {
   public:
    explicit CrosstableStore(std::filesystem::path profile);
    [[nodiscard]] CrosstableSettings get(const std::string& place) const;
    void set(const std::string& place, const CrosstableSettings& settings);  // empty settings: removed

   private:
    void save() const;
    std::filesystem::path file_;
    std::map<std::string, CrosstableSettings> settings_;
};

// ---------------------------------------------------------------------------------------------------------------------
// The words of the views

// "round robin", "double round robin", "round robin, 4 cycles", "Swiss", "team event", "knockout".
[[nodiscard]] std::string systemName(const study::SystemChoice& system);
// "Sonneborn-Berger", "Buchholz cut 1"; what a heading shows ("SB", "BH-C1"); what it counts.
[[nodiscard]] const char* tiebreakName(study::Tiebreak t);
[[nodiscard]] const char* tiebreakHeading(study::Tiebreak t);
[[nodiscard]] const char* tiebreakHelp(study::Tiebreak t);
// A tie-break's value: halves with ½ ("45½"), other fractions with two decimals ("12.25"), ratings whole; with
// `decimals` (a column that has quarters), two decimals whatever the value ("112.50").
[[nodiscard]] std::string tiebreakValue(study::Tiebreak t, double value, bool decimals = false);
// Points: "5½", "½", "0"; "5½?" when results are not known.
[[nodiscard]] std::string pointsText(double points, int unknown = 0);
// A rating change: "+11.2", "−3.1", "±0.0".
[[nodiscard]] std::string changeText(double change);

// A cell: in a round robin's ("1", "½", "0", "+" and "−" forfeits, "?" a game missing, "●" being played, "·" to come);
// in a Swiss's, the opponent's start number, the colour and the result ("4b½", "7w+"), "bye", "–" a round without a
// game, "?" not known.
[[nodiscard]] std::string crossCellText(const study::Crosstable& table, const study::CrossEntry& entry, bool swiss);

// ---------------------------------------------------------------------------------------------------------------------
// A followed broadcast's crosstable

struct LiveCrosstable {
    std::vector<study::CrossPlayer> players;
    std::vector<study::CrossGame> games;  // `source` 1: a followed game (`ref` its place in `urls`), 0: a saved one
    std::vector<std::string> urls;        // the followed games' addresses
    std::vector<std::string> pgns;        // and their text, as Lichess last sent it
    std::string event;                    // their event, as its games write it
    int saved = 0;                        // games taken from the "Broadcasts" database
};

// The crosstable of a broadcast's followed games (those being played among them), with the games of its rounds kept
// in `saved` (the "Broadcasts" database: `earlier` its tournament's games) for the rounds that no longer come.
[[nodiscard]] LiveCrosstable liveCrosstableInput(const std::vector<lichess::BroadcastGame>& followed,
                                                 const study::Snapshot* saved, std::span<const study::GameId> earlier);

// ---------------------------------------------------------------------------------------------------------------------
// Ctrl+K

struct CrosstableName {
    std::filesystem::path folder;
    std::string database;
    std::string key;
    std::string label;  // "Autumn Open 2026 · Lyon"
};
struct TournamentsOf {
    std::filesystem::path folder;
    std::string database;
    std::shared_ptr<const study::Snapshot> snapshot;
    std::shared_ptr<const study::TournamentList> list;
};
// The tournaments whose event or site hold each of `words` (letter case and accents aside), the highest mean Elo first;
// at most `most`.
[[nodiscard]] std::vector<CrosstableName> crosstableNames(const std::vector<TournamentsOf>& lists,
                                                          const std::string& words, std::size_t most);
// A tournament's label: its event as written, and its site.
[[nodiscard]] std::string tournamentLabel(const study::Snapshot& snapshot, const study::TournamentEntry& entry);

// ---------------------------------------------------------------------------------------------------------------------
// The games missing, looked for elsewhere

struct MissingAsked {
    std::string white, black;  // the players as the crosstable names them
    int round = 0;             // 0 not known
    int year  = 0;
};
struct FoundMissing {
    std::size_t asked = 0;  // in what was asked
    std::filesystem::path folder;
    std::string database;
    study::GameId id = 0;
    std::string round;  // as that database writes it
    std::string white, black;
    char result = '*';
};
struct OtherDatabase {
    std::filesystem::path folder;
    std::string name;
    std::shared_ptr<const study::Snapshot> snapshot;
};
// The games of `others` between the players of each game asked (names that can be theirs: the same surname, a given
// name that can be theirs), of its year, its round when told and theirs agree.
[[nodiscard]] std::vector<FoundMissing> lookForMissing(const std::vector<MissingAsked>& asked,
                                                       const std::vector<OtherDatabase>& others,
                                                       const std::atomic<bool>& cancel);
// A game found, made a game of the tournament: its event, site, round and players written as the tournament's.
[[nodiscard]] study::ImportedGame missingGameCopy(study::ImportedGame game, const std::string& event,
                                                  const std::string& site, int round, const std::string& white,
                                                  const std::string& black);

}  // namespace fcg
