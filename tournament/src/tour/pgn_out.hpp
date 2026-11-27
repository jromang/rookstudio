#pragma once

// The PGN file of the user's (TournamentSpec::pgn_out): a file outside the tournament's folder, which other tournaments
// may write as well, that every game of the tournament is also appended to, in the cutechess style or the ChessBase
// one (TournamentSpec::pgn_out_chessbase). The games go there in schedule order rather than in the order they end: the
// two games of an encounter side by side, the rounds in order. Each write is journaled ("pgn_out") before and after,
// so that a resume carries on, and a crash in the middle neither writes games twice nor loses them.

#include <cstdint>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <json.hpp>

#include "game_result.hpp"
#include "journal.hpp"
#include "model.hpp"
#include "pgn_file.hpp"

namespace fcg::tour {

// Writes a tournament's games to the PGN file of the user's. It is the TournamentState's: every call is made with the
// tournament's lock held, which guards what it holds and what it reads of the tournament.
class PgnOutWriter {
   public:
    // What it reads of the tournament: the TournamentState's members.
    struct Source {
        const TournamentSpec& spec;              // pgn_out, pgn_out_chessbase
        const std::vector<GameResult>& results;  // the games recorded, in the order they ended, voided ones included
        const std::set<uint64_t>& done;          // the numbers of the games played,
        const std::set<uint64_t>& dropped;       // and of those that will not be
        const bool& finished;                    // the tournament is over: every game left is written
        std::filesystem::path pgn;               // the tournament's PGN files, which the games are read back from
        std::filesystem::path chessbase;
    };

    explicit PgnOutWriter(Source source) : source_(std::move(source)) {}
    PgnOutWriter(const PgnOutWriter&)            = delete;
    PgnOutWriter& operator=(const PgnOutWriter&) = delete;

    // The tournament opened, its journal read: the games whose pgnPath() offset is in `written` are there already,
    // the others wait for their turn.
    void opened(const std::set<uint64_t>& written);

    // results[index], recorded, waits for its turn.
    void hold(std::size_t index) { held_.push_back(index); }

    // An edit changed the games to play (voided games are played again): the first game the others wait for is
    // looked for from the first one again.
    void rewind() noexcept { floor_ = 0; }

    // Appends the finished games every game before which, in the schedule, is written there or will not be played. A
    // replayed game is written when it ends; once the tournament is over, every game left. Journaled to `journal`
    // before and after each write. A failure is kept for error() and tried again next time. `lock` holds the
    // tournament's lock, and is released while the games are written: one thread writes at a time, the games that
    // become ready meanwhile written by it right after. Nothing without a journal (read-only) or a file.
    void write(std::unique_lock<std::mutex>& lock, Journal* journal);

    // When the tournament is opened: a write whose start the journal has but not its end (a crash in between). True
    // when its games are in the file (completed if the write was cut short), which is then journaled; false when they
    // are to be written again. `journaled`: the places of the games in the tournament's PGN files, by pgnPath()
    // offset.
    bool settle(const nlohmann::json& begun, const std::map<uint64_t, std::pair<PgnSpan, PgnSpan>>& journaled,
                Journal* journal);

    // Why the last write failed, empty when it worked; the games wait for the next try.
    [[nodiscard]] const std::string& error() const noexcept { return error_; }

    // A thread is writing there, without the tournament's lock.
    [[nodiscard]] bool writing() const noexcept { return writing_; }

   private:
    Source source_;
    std::vector<std::size_t> held_;  // indexes in results of the games not written there yet, voided included
    std::set<uint64_t> written_;     // numbers of the games written there
    uint64_t floor_ = 0;             // every game number below is written there, done or dropped
    std::string error_;
    bool writing_ = false;  // a thread is writing there, without the lock
    bool again_   = false;  // games may have become ready meanwhile
    // A write there journaled as begun that failed (a disk full) and may have left part of its text: the next one
    // completes it, rather than write the text again after a game cut off.
    struct Cut {
        std::vector<std::size_t> games;  // indexes in results
        nlohmann::json offsets;          // their pgnPath() offsets, as journaled
        std::string file;                // spec.pgn_out then
        uint64_t at = 0;                 // where it went
        std::string text;
    };
    std::optional<Cut> cut_;
};

}  // namespace fcg::tour
