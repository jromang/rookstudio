#pragma once

// The rounds of a Swiss or a knockout, paired as the results come (pairing.hpp makes the pairings): the round being
// played, the byes received, a knockout's brackets, and once a round is over, the journal events that pair the next
// one or give its tied matches another pair of games. The TournamentState journals the events, then applies them as it
// applies those of its journal when the tournament is opened, so that a resume replays the same rounds.

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <vector>

#include <json.hpp>

#include "game_result.hpp"
#include "model.hpp"
#include "schedule.hpp"

namespace fcg::tour {

// It is the TournamentState's: every call is made with the tournament's lock held, which guards what it holds and what
// it reads of the tournament.
class Rounds {
   public:
    // What it reads of the tournament: the TournamentState's members.
    struct Source {
        const TournamentSpec& spec;
        const Schedule& schedule;
        const std::vector<GameResult>& results;  // voided ones included
        const std::set<uint64_t>& done;          // the numbers of the games played,
        const std::set<uint64_t>& dropped;       // and of those that will not be
    };

    explicit Rounds(Source source) : source_(source) {}
    Rounds(const Rounds&)            = delete;
    Rounds& operator=(const Rounds&) = delete;

    // The encounters a round event of the journal ("round_paired", "tiebreak_added") adds to `schedule`, the
    // tournament's or a copy of it (TournamentState::replay()).
    static void addEncounters(const nlohmann::json& event, const TournamentSpec& spec, Schedule& schedule);

    // A round event, journaled and its encounters added: a round paired is the one played now, with its byes and its
    // bracket.
    void apply(const nlohmann::json& event);

    // The events due once the round being played is over (every game of it played or dropped): another pair of games
    // for each tied match of a knockout's round, all at once, or else the next round. None while the round goes on,
    // after a Swiss' last round, or once a knockout's final is won.
    [[nodiscard]] std::vector<nlohmann::json> next();

    [[nodiscard]] uint32_t round() const noexcept { return round_; }  // the last round paired, 0 before the first
    [[nodiscard]] const std::map<int, int>& byes() const noexcept { return byes_; }  // by participant id
    // A knockout's: the places of each round in bracket order (a pair, or a single player with a bye).
    [[nodiscard]] const std::map<uint32_t, std::vector<std::vector<int>>>& brackets() const noexcept {
        return brackets_;
    }

    // A knockout, the place of `a` and `b` in the round being played: whether all its games are played (or dropped),
    // and who goes through once they are (nullopt while level: another tiebreak is due).
    [[nodiscard]] bool placeOver(int a, int b) const;
    [[nodiscard]] std::optional<int> placeWinner(int a, int b) const;

   private:
    [[nodiscard]] double pairPoints(int a, int b, uint32_t round) const;  // a's points against b in a round
    [[nodiscard]] int tiebreaks(int a, int b) const;  // the tiebreak pairs of games of their place in the round

    Source source_;
    std::map<int, int> byes_;
    std::map<uint32_t, std::vector<std::vector<int>>> brackets_;
    uint32_t round_ = 0;
};

}  // namespace fcg::tour
