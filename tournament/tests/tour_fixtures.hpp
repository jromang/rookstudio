#pragma once

// Tournaments of the tests, played without engines: a spec made in a line, the games fastchess would report, played as
// the state hands them out. For the tests of the tournament layer and of those above it.

#include <doctest/doctest.hpp>

#include <concepts>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

#include <tour/model.hpp>
#include <tour/state.hpp>

namespace fcg::test {

// An engine's configuration: its name alone.
inline fastchess::EngineConfiguration engineNamed(std::string name) {
    fastchess::EngineConfiguration e;
    e.name = std::move(name);
    return e;
}

// A tournament's spec as a test needs it: SpecBuilder(tour::Format::RoundRobin).players(4).cycles(2).
class SpecBuilder {
   public:
    explicit SpecBuilder(tour::Format format) { spec_.format = format; }

    // `count` participants, ids 0, 1, 2..., named A, B, C... or, given a prefix, the prefix and the id (P0, P1...).
    SpecBuilder& players(int count, std::string_view prefix = {}) {
        for (int i = 0; i < count; ++i) {
            tour::Participant p;
            p.id     = i;
            p.engine = engineNamed(prefix.empty() ? std::string(1, static_cast<char>('A' + i))
                                                  : std::string(prefix) + std::to_string(i));
            spec_.participants.push_back(std::move(p));
        }
        return *this;
    }
    SpecBuilder& name(std::string name) {
        spec_.name = std::move(name);
        return *this;
    }
    SpecBuilder& cycles(int cycles) {
        spec_.cycles = cycles;
        return *this;
    }
    SpecBuilder& maxGames(uint64_t games) {
        spec_.max_games = games;
        return *this;
    }

    operator tour::TournamentSpec() const { return spec_; }

   private:
    tour::TournamentSpec spec_;
};

// A round robin of `players` participants named A, B, C..., `cycles` times over.
inline tour::TournamentSpec roundRobin(int players, int cycles = 1) {
    return SpecBuilder(tour::Format::RoundRobin).players(players).cycles(cycles);
}

// A finished game as fastchess reports it, without moves: `result` White's, "1-0", "0-1" or "1/2-1/2".
inline fastchess::MatchData fakeGame(const tour::GameTask& task, std::string_view result) {
    using chess::GameResult;
    fastchess::MatchData data(chess::constants::STARTPOS);
    const auto white = result == "1-0" ? GameResult::WIN : result == "0-1" ? GameResult::LOSE : GameResult::DRAW;
    const auto black = result == "1-0" ? GameResult::LOSE : result == "0-1" ? GameResult::WIN : GameResult::DRAW;
    data.players     = {fastchess::MatchData::PlayerInfo(task.white, white),
                        fastchess::MatchData::PlayerInfo(task.black, black)};
    data.termination = fastchess::MatchTermination::NORMAL;
    data.duration    = "00:00:01";
    return data;
}

// What a game ends in, given its task and the games played before it: "1-0", "0-1" or "1/2-1/2".
template <typename F>
concept ResultOf = std::invocable<F&, const tour::GameTask&, int>;

// Plays the games `state` hands out, `count` at most (all of them when negative), each one ending in what `result`
// says of it: how many were played.
template <ResultOf Result>
int play(tour::TournamentState& state, int count, Result&& result) {
    int played = 0;
    while (count < 0 || played < count) {
        const auto task = state.claimNext();
        if (!task) break;
        state.complete(*task, fakeGame(*task, result(*task, played)));
        REQUIRE(++played < 100000);  // a schedule that would never end
    }
    return played;
}

// The same, every game ending in `result`.
inline int play(tour::TournamentState& state, int count, std::string_view result = "1-0") {
    return play(state, count, [result](const tour::GameTask&, int) { return result; });
}

// Every game the state hands out until it has none left: how many there were.
template <ResultOf Result>
int playAll(tour::TournamentState& state, Result&& result) {
    return play(state, -1, std::forward<Result>(result));
}
inline int playAll(tour::TournamentState& state, std::string_view result = "1-0") { return play(state, -1, result); }

}  // namespace fcg::test
