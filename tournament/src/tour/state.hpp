#pragma once

// The live state of a tournament: settings, schedule and results, rebuilt from its folder when it is opened and
// kept on disk as games end. The folder holds:
//
//   tournament.json      the settings and participants, written once at creation
//   journal.jsonl        everything that happened since (see journal.hpp)
//   <name>.pgn            move comments in the cutechess / fastchess style
//   <name>-chessbase.pgn  [%eval] [%clk] [%emt] comments
//
// The PGN files are named after the tournament when it is created (spec.pgn_file, spec.pgn_chessbase_file); those
// of the tournaments of 0.4.12 and earlier are games.pgn and games-chessbase.pgn. Both are written in the order
// games end, and the journal finds every game by its place in them. The PGN file of the user's (spec.pgn_out) gets
// them in schedule order instead: see pgn_out.hpp. The names of the files, and of the journal's events, are in
// journal_events.hpp.
//
// A finished game is written to both PGN files first, then to the journal, each synced to disk. The PGN files are
// written without the state's lock, so the journal may list games out of their order there; it points to each by
// its place. Text no journal line points to is never read, and not left for other PGN viewers either: a game an
// edit dropped while it was written is blanked (overwritten with blank lines, the other games keep their places),
// and when a tournament is opened, the text a crash left before its journal line is cut past the last journaled
// game and blanked between them. Games that were running are simply played again, with the opening their
// encounter started with. Resuming is therefore exact at any concurrency.
//
// One TournamentState of one program at a time has a folder open for writing (tournament.lock, see files.hpp);
// others can open it read-only.

#include <atomic>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <game/book/opening.hpp>
#include <matchmaking/sprt/sprt.hpp>
#include <matchmaking/stats.hpp>
#include <types/engine_config.hpp>
#include <types/match_data.hpp>

#include "edits.hpp"
#include "files.hpp"
#include "game_result.hpp"
#include "journal.hpp"
#include "model.hpp"
#include "openings.hpp"
#include "pgn_file.hpp"
#include "pgn_out.hpp"
#include "rounds.hpp"
#include "schedule.hpp"
#include "start_failures.hpp"
#include "unbalanced.hpp"

namespace fcg::tour {

struct JournalReader;  // journal_events.hpp

// One game handed to the runner.
struct GameTask {
    Encounter encounter;
    int game = 0;  // 0: encounter.first has White, 1: encounter.second has
    fastchess::book::Opening opening;
    fastchess::EngineConfiguration white;
    fastchess::EngineConfiguration black;

    // 0-based game number in the tournament, stable across resumes.
    [[nodiscard]] uint64_t number() const noexcept { return uint64_t{encounter.index} * 2 + game; }
};

// The statistics of one complete encounter (its two games), from participant `pov`'s point of view, pentanomial
// counts included.
[[nodiscard]] fastchess::Stats encounterStats(const GameResult& a, const GameResult& b, int pov);

// What complete() recorded.
struct Recorded {
    GameResult result;
    // Set when the game completed its encounter: statistics of the two participants after it, from the point of
    // view of the lower id.
    std::optional<fastchess::Stats> pair;
    // False when the game was not recorded: already played, dropped, or its encounter changed while it was played.
    bool kept = true;
};

// Where a game of the schedule stands.
enum class GameStatus { Pending, Running, Done, Dropped };

struct ScheduleRow {
    Encounter encounter;
    GameStatus status[2] = {GameStatus::Pending, GameStatus::Pending};
};

class TournamentState {
   public:
    // Creates the folder (which must not hold a tournament yet) and its tournament.json. Throws on error.
    static std::unique_ptr<TournamentState> create(const std::filesystem::path& dir, TournamentSpec spec);

    // Opens an existing tournament. Throws, with a readable message, when the folder is not a tournament or its
    // files cannot be read, and FolderBusy when another program (or TournamentState) has it open for writing.
    // `read_only` writes nothing, takes no lock and makes no edit: to show a tournament another program plays.
    static std::unique_ptr<TournamentState> open(const std::filesystem::path& dir, bool read_only = false);

    [[nodiscard]] static bool isTournament(const std::filesystem::path& dir);

    // Replays journal events on settings and schedule only, touching no file (for a summary of a tournament that
    // may be running). `spec` receives the participants as edits left them.
    struct Replayed {
        uint64_t played = 0;  // results that count
        std::optional<uint64_t> total;
        bool finished = false;
        std::string finish_reason;
        std::vector<GameResult> results;  // the games that count
        std::map<int, int> byes;          // Swiss: byes received, by participant id
        std::vector<std::vector<int>> last_bracket;  // knockout: the places of the last round paired
    };
    [[nodiscard]] static Replayed replay(const std::vector<nlohmann::json>& events, TournamentSpec& spec);

    TournamentState(const TournamentState&)            = delete;
    TournamentState& operator=(const TournamentState&) = delete;

    [[nodiscard]] const std::filesystem::path& dir() const noexcept { return dir_; }
    [[nodiscard]] bool readOnly() const noexcept { return read_only_; }
    // Lets go of the folder: its lock and its files closed, read-only from now on, as shown. The window's view of a
    // stopped tournament does so before the process that resumes it starts, which takes the folder's lock (#191).
    // Only once no runner plays the tournament: a game being recorded (complete()) or written to spec().pgn_out uses
    // the files, some of the time without the state's lock, and letGo() throws std::logic_error then, letting go of
    // nothing. A game claimed before and ending after is not recorded: complete() throws std::logic_error.
    void letGo();

    // The settings and participants, as the edits left them. spec() is for the thread that makes the edits
    // (applyEdit(), undoEdit(), follow(): the window's, or the control loop of the tournament process), and for any
    // thread while none can be made: an edit changes what it refers to, undoEdit() replaces it. Another thread (the
    // runner's, a listener it calls) takes specSnapshot(): a copy that no edit changes, shared until the next edit.
    [[nodiscard]] const TournamentSpec& spec() const noexcept { return spec_; }
    [[nodiscard]] std::shared_ptr<const TournamentSpec> specSnapshot() const;
    // Any thread: the names of the PGN files never change.
    [[nodiscard]] std::filesystem::path pgnPath() const { return pgn_path_; }
    [[nodiscard]] std::filesystem::path chessbasePgnPath() const { return chessbase_path_; }

    // Number of games the tournament plays, nullopt for a match without a game limit.
    [[nodiscard]] std::optional<uint64_t> totalGames() const;

    // --- edits (round robin and gauntlet; voiding games works for a match too) ---

    // What `edit` would change, without making it; Impact::error says why it cannot be made.
    [[nodiscard]] Impact preview(const Edit& edit) const;

    // Makes `edit`: journaled, then applied; the runner sees the new schedule at its next claim. Throws with a
    // readable message when the edit cannot be made, as preview() tells it under the same lock: a game that ends on
    // another thread meanwhile (and pairs the next round of a knockout) cannot come between.
    void applyEdit(const Edit& edit);

    // The last edit can be undone until the next game starts: nothing has been played under it yet. Games that
    // were running when it was made and finished since are kept. Journaled, so it holds after a resume. Throws
    // when it is too late. A result set by hand (but in a knockout, whose next rounds follow from the results) can
    // be undone as long as it is the last edit: undoLasts().
    [[nodiscard]] bool canUndo() const;
    [[nodiscard]] bool undoLasts() const;
    void undoEdit();

    // The whole schedule with the status of each game (empty for a match).
    [[nodiscard]] std::vector<ScheduleRow> scheduleRows() const;

    // Swiss: byes received, by participant id (a bye counts as a drawn pair of games, 1 point).
    [[nodiscard]] std::map<int, int> byes() const;

    // Knockout: the places of each round in bracket order (a pair, or a single player with a bye).
    [[nodiscard]] std::map<uint32_t, std::vector<std::vector<int>>> brackets() const;

    // Things the user should know about, found when the tournament was opened (a changed opening book...).
    [[nodiscard]] const std::vector<std::string>& warnings() const noexcept { return warnings_; }

    // --- tournament threads ---

    // The next game to play: games of encounters already started first (the second game of a pair comes right
    // after the first), then the next encounter of the schedule. The games of the participants startFailures() sets
    // aside are left for later: an encounter of theirs passed over counts as started, not played yet, so that edits
    // leave it in place and its games come first once their engine starts. nullopt when there is nothing to play now.
    // `admit`, when given, is asked about the game found (its encounter's two participants) before it is claimed:
    // false holds it back and nothing is claimed (the runner's allowance, budget.hpp). Called with the state's lock
    // held: it must not call the state.
    using Admit = std::function<bool(const Participant& first, const Participant& second)>;
    [[nodiscard]] std::optional<GameTask> claimNext(const Admit& admit = {});

    // A claimed game that will not be finished (interrupted): it is played again later.
    void release(const GameTask& task);

    // A game of the encounter was played or is being played.
    [[nodiscard]] bool begun(uint32_t encounter) const;

    // The participants whose engine cannot start, and when they are tried again (start_failures.hpp). Thread safe.
    [[nodiscard]] StartFailures& startFailures() noexcept { return start_failures_; }
    [[nodiscard]] const StartFailures& startFailures() const noexcept { return start_failures_; }

    // Records a finished game: PGN files, then the journal. Updates the statistics and the SPRT. Throws when the
    // files cannot be written. `set`: the operator ended the game with data's result (see requestEnd()).
    Recorded complete(const GameTask& task, const fastchess::MatchData& data,
                      const std::optional<ResultSet>& set = std::nullopt);

    // The operator ends game `number` (GameTask::number()) while it is played, with `result` ("1-0", "0-1" or
    // "1/2-1/2") for `why`: asked here, then the GUI stops the engine on move, and the runner takes the request when
    // fastchess ends the game, to record it with that result (takeEnd()). False when the game is not being played,
    // or the result is no result.
    bool requestEnd(uint64_t number, const std::string& result, const std::string& why);
    // The request for game `number`, taken: (result, why), nullopt when there is none.
    std::optional<std::pair<std::string, std::string>> takeEnd(uint64_t number);

    // Is the claimed game still to be played as it was claimed: not dropped, nor its encounter changed by an edit?
    [[nodiscard]] bool wanted(const GameTask& task) const;

    // Is there anything left to claim?
    [[nodiscard]] bool hasWork() const;

    // No game of the encounter is left to start: both played, or no longer to be played.
    [[nodiscard]] bool settled(uint32_t encounter) const;

    // The engine names of the participants still playing (not withdrawn, not replaced) that have a game left: of
    // the schedule of a round robin, a gauntlet or the last round of a Swiss; in a knockout, those not knocked out
    // (a match lost ends a player's games at once); otherwise any, whose next rounds are not known yet.
    [[nodiscard]] std::set<std::string> enginesLeft() const;

    // Every game played, or the SPRT decided.
    [[nodiscard]] bool finished() const;
    [[nodiscard]] std::string finishReason() const;

    // --- the tournament process and the window ---

    // The tournament process tells the window each line of the journal as it is written (Journal::setListener()).
    void setJournalListener(Journal::Listener listener);

    // Read-only: the bytes of the journal this copy holds, as read when opened, then as followed.
    [[nodiscard]] uint64_t journalSize() const;

    // Read-only: a journal line the program playing the tournament wrote since the copy was opened (or since the last
    // one followed), ending at `end` in the file. False when the copy cannot follow it (an edit undone): it is to be
    // opened again. Throws on an event it does not know.
    bool follow(const nlohmann::json& event, uint64_t end);

    // Read-only: what the program playing the tournament tells of it, which its journal does not: the games being
    // played, whether its last change can be undone, why its games wait for spec().pgn_out. scheduleRows(),
    // canUndo(), undoLasts() and pgnOutError() answer with them.
    struct ReplicaStatus {
        std::set<uint64_t> running;  // game numbers
        bool can_undo   = false;
        bool undo_lasts = false;
        std::string pgn_out_error;
    };
    void setReplicaStatus(ReplicaStatus status);

    // --- any thread ---

    // Scored games, in the order they finished, voided ones included (flagged).
    [[nodiscard]] std::vector<GameResult> results() const;

    // The result that counts of game `number` (GameTask::number()), nullopt when none does. Copies only that one.
    [[nodiscard]] std::optional<GameResult> resultOf(uint64_t number) const;

    // Changes each time a game is claimed, released or recorded and at each edit, once the change is made: what the
    // views compute from the schedule and the results is computed again only when it moved.
    [[nodiscard]] uint64_t version() const noexcept { return version_.load(); }

    // Why the last write to spec().pgn_out failed, empty when it worked; the games wait for the next try.
    [[nodiscard]] std::string pgnOutError() const;

    // The opening each encounter started with (book position and moves), by encounter index.
    [[nodiscard]] std::map<uint32_t, fastchess::book::Opening> encounterOpenings() const;
    // The opening of each game started, by game number (GameTask::number()): its encounter's, or in a match with
    // locked colours, game 1's own (ownOpenings()).
    [[nodiscard]] std::map<uint64_t, fastchess::book::Opening> gameOpenings() const;

    // How many times a pair was voided and played again from another opening, its own found unbalanced
    // (spec().unbalanced_replace): the views show it, and know that the results changed.
    [[nodiscard]] uint64_t openingsReplaced() const;

    // Games `numbers` (GameTask::number()) lost an engine that runs on a machine whose connection broke (`machine`
    // its name): voided and played again, as a void of the Games tab would, in the journal with NETWORK_REASON, but
    // not as an edit of the user's (undo goes past it, as for the unbalanced openings). Those without a result that
    // counts are left out; false when none had one.
    bool voidCutGames(const std::vector<uint64_t>& numbers, const std::string& machine);
    // The games voided so: the views show it, and know that the results changed.
    [[nodiscard]] uint64_t networkVoids() const;

    // Statistics of `a` against `b` from a's point of view, counted on complete encounters only (both games),
    // like fastchess does with pentanomial reporting. Includes the pentanomial counts.
    [[nodiscard]] fastchess::Stats pairStats(int a, int b) const;

    // pairStats(a, b) after each encounter between them completed, in the order they completed.
    [[nodiscard]] std::vector<fastchess::Stats> pairHistory(int a, int b) const;

   private:
    TournamentState(std::filesystem::path dir, TournamentSpec spec);

    void load();  // replays the journal
    // What load() and follow() do with the journal's events, as they were journaled: openings, edits, rounds and the
    // end applied to the state. Its handlers hold `this`: called under mutex_, or by load() before another thread has
    // the state.
    [[nodiscard]] JournalReader applying();
    [[nodiscard]] Impact previewLocked(const Edit& edit) const;  // preview(), mutex_ held
    // Returns true when the result completes its encounter.
    bool apply(const GameResult& result);
    bool count(std::size_t index);                 // adds results_[index] to the statistics
    void applyEvent(const nlohmann::json& event);  // an edit event, journal replay or new; mutex_ held

    // What an edit event does to participants, schedule and results, on the given state (members or copies).
    static void effects(const nlohmann::json& event, TournamentSpec& spec, Schedule& schedule,
                        std::vector<GameResult>& results, std::set<uint64_t>& done, std::set<uint64_t>& dropped);
    void rebuildStats();
    // Swiss, knockout: the next round (or a knockout's tiebreaks) when one is due, journaled and applied.
    void pairIfReady();
    void applyRoundEvent(const nlohmann::json& event);  // round_paired, tiebreak_added; mutex_ held
    void rebuildOpen();
    [[nodiscard]] bool hasWorkLocked() const;
    [[nodiscard]] bool gamesLeft() const;  // games to claim, finished or not; mutex_ held
    [[nodiscard]] uint64_t totalLocked() const;
    void checkSprt();
    // The encounter, complete, played an unbalanced opening and the tournament replaces such openings: its games
    // are voided and played again from another opening (UNBALANCED_REASON), at most MAX_OPENING_REPLACEMENTS times.
    // True when it was. mutex_ held.
    bool replaceUnbalanced(uint32_t encounter);
    // Every game played, none running: the end is journaled. mutex_ held.
    void finishIfComplete();
    [[nodiscard]] GameTask makeTask(uint32_t encounter, int game);
    [[nodiscard]] bool done(uint32_t encounter, int game) const;
    [[nodiscard]] bool frc() const noexcept;
    // The Polyglot book a participant's own book plays from (ownBookFile()), asked once while the tournament is
    // open (again only when that file is gone); mutex_ held.
    const std::string& ownBookFileOf(const Participant& p);
    // Those of the active participants, asked when the tournament is opened and after a change, right after the
    // books were prepared: a PGN file saved again before the participant's first game does not leave its games
    // looking for a book never made. mutex_ held.
    void keepOwnBooks();
    const OpeningSource& openings();  // loads the book on first use; mutex_ held
    // The opening a new encounter starts with: the next of the book, or its round's (spec_.opening_per_round). With
    // ownOpenings(), the opening of its game `game`: two of the book for each encounter.
    fastchess::book::Opening openingFor(const Encounter& encounter, int game = 0);
    [[nodiscard]] bool openingPerRound() const noexcept;
    // A match with locked colours: each game of a pair has an opening of its own, rather than both the same with the
    // colours reversed (which would repeat a game with the colours locked). Game 1's is journaled apart, as an
    // encounter_started with "game": 1.
    [[nodiscard]] bool ownOpenings() const noexcept;
    // An encounter_started line of the journal, read when the tournament is opened or followed; mutex_ held.
    void readStarted(const nlohmann::json& e);

    // What the last edit changed, to undo it (see canUndo()).
    struct UndoPoint {
        TournamentSpec spec;
        Schedule schedule;
        std::vector<GameResult> results;  // the results then (an edit changes whether they count, and how)
        std::set<uint64_t> done;
        std::set<uint64_t> dropped;
        uint64_t claims = 0;
        // A result set by hand changes nothing else: it is undone exactly however many games were played since.
        bool results_only = false;
    };

    std::filesystem::path dir_;
    std::atomic<bool> read_only_{false};
    std::unique_ptr<FolderLock> lock_;
    TournamentSpec spec_;  // changed under mutex_, on the thread that makes the edits (see spec())
    // specSnapshot()'s copy, made when asked and dropped whenever spec_ changes; mutex_.
    mutable std::shared_ptr<const TournamentSpec> spec_snapshot_;
    std::filesystem::path pgn_path_;  // pgnPath(), chessbasePgnPath()
    std::filesystem::path chessbase_path_;
    Schedule schedule_;
    std::unique_ptr<OpeningSource> openings_;  // loaded when a new encounter first needs it
    std::map<std::pair<std::string, int>, std::string> own_book_files_;  // (book, depth) -> its Polyglot book
    std::vector<std::string> warnings_;

    mutable std::mutex mutex_;

    std::unique_ptr<Journal> journal_;
    std::unique_ptr<PgnFile> pgn_;
    std::unique_ptr<PgnFile> pgn_chessbase_;
    // Threads in complete() that use the files, some of the time without mutex_ (letGo() refuses meanwhile); counted up
    // under mutex_.
    std::atomic<int> recording_{0};

    std::vector<GameResult> results_;
    std::set<uint64_t> done_;                               // game numbers
    std::map<uint32_t, fastchess::book::Opening> started_;  // encounter -> opening it started with
    std::map<uint32_t, fastchess::book::Opening> second_;   // ownOpenings(): encounter -> the opening of its game 1
    uint64_t replaced_ = 0;                                 // openings replaced as unbalanced (replaceUnbalanced())
    uint64_t network_voids_ = 0;                            // games voided for a broken connection (voidCutGames())
    std::map<uint32_t, int> replacements_;                  // encounter -> how many times its opening was
    // spec_.opening_per_round: (cycle, round) -> the opening of its encounters.
    std::map<std::pair<uint32_t, uint32_t>, fastchess::book::Opening> round_openings_;
    std::map<uint32_t, std::size_t> half_;                   // encounter -> index in results_ of its first game
    std::map<std::pair<int, int>, fastchess::Stats> pairs_;  // (lower id, higher id), lower id's point of view

    std::set<uint64_t> dropped_;   // games that will not be played (withdrawn participant, voided without replay)
    std::set<uint64_t> running_;   // claimed by the runner, not finished
    uint32_t next_encounter_ = 0;  // first encounter never started
    std::deque<std::pair<uint32_t, int>> open_;  // games of started encounters still to play

    std::optional<UndoPoint> undo_;
    uint64_t claims_ = 0;  // games claimed since the tournament was opened

    uint64_t journal_size_ = 0;  // read-only: see journalSize()
    ReplicaStatus replica_;      // read-only: see setReplicaStatus()

    StartFailures start_failures_;
    std::map<uint64_t, std::pair<std::string, std::string>> end_requests_;  // requestEnd(): number -> (result, why)

    // version(): bumped when a change is over, the mutex released (declared before the lock_guard).
    std::atomic<uint64_t> version_{0};
    struct Bump {
        std::atomic<uint64_t>& version;
        ~Bump() { ++version; }
    };

    fastchess::SPRT sprt_;
    bool finished_ = false;
    std::string finish_reason_;

    // spec_.pgn_out's, and the rounds of a Swiss or a knockout: they read the members above (declared before them),
    // under mutex_.
    PgnOutWriter pgn_out_;
    Rounds rounds_;
};

}  // namespace fcg::tour
