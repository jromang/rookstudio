#pragma once

// Your mistakes as exercises, and guessing the moves of a game (#81, mockup M12, board 2; docs/design/mistakes.md).
//
// Your games are those you played under one of your names (Settings › Your names, and the linked Lichess account's).
// The moves their reviews marked ? and ?? on your side, where the review's best move is clear, become positions to find
// better in: one card a position, scheduled by FSRS as the repertoires' training schedules its positions, in the day's
// review (review_queue.hpp) as a kind of its own. An answer is right when it is the review's best move, or a move the
// review's engine at the review's settings (#61) finds as good. Your games without a review are reviewed in the
// background first, as the Board page reviews a game.
//
// Kept in the profile: mistakes/exercises.json (the exercises, each with its card, and the reviewed games they come
// from), mistakes/answers.jsonl (a line per answer), guess/games.jsonl (a line per game guessed). Nothing leaves the
// machine.

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <future>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <chess.hpp>
#include <commentary/phrases.hpp>
#include <study/fsrs.hpp>
#include <study/store.hpp>
#include <study/training.hpp>
#include <types/score.hpp>

#include "analysis.hpp"
#include "game_analysis.hpp"
#include "pgn_store.hpp"

namespace fcg {

// ---------------------------------------------------------------------------
// Your names

// A name's words as they are compared: letters in lower case without their accents, split at anything but a letter or a
// figure ("Tester, Sam-Éric": tester, sam, eric).
[[nodiscard]] std::vector<std::string> nameWords(std::string_view name);
// Whether a game's player is the user: every word of one of their names is a word of the player's ("Tester" is
// "Tester, S."; "Sam Tester" is "Tester, Sam-Eric"; "Test" is no one).
[[nodiscard]] bool isUserName(std::string_view player, const std::vector<std::string>& names);
// The side the user played, from the game's players: none when neither, or both, are theirs.
[[nodiscard]] std::optional<chess::Color> userSide(std::string_view white, std::string_view black,
                                                   const std::vector<std::string>& names);

// ---------------------------------------------------------------------------
// The exercises

struct MistakeExercise {
    uint64_t key = 0;  // the position before the mistake (Polyglot): one card a position
    // The game: its start and its moves up to the position (UCI), as the review searched them; its review's key
    // (reviewKey()); its tags; the database it was found in (its name, folder, game number).
    std::string start_fen;
    std::vector<std::string> moves;
    std::string game_key;
    std::string white, black, event, site, date, time_control;
    std::string database;
    std::filesystem::path folder;
    uint64_t game_id     = 0;
    uint32_t packed_date = 0;  // the game's date (study::packDate()), to order them
    // The mistake: the move played (UCI), ? or ??, its kind ("To work on"'s topic key, "fork"; empty for none).
    std::string played;
    Judgement judgement = Judgement::Mistake;
    std::string kind;
    // The review: its engine, its limit (nodes per position, else a time), when it was kept; its first line at the
    // position (the best move, its score and WDL for the side to move, its moves) and after the move played (the
    // opponent's).
    std::string engine;
    uint64_t nodes      = 0;
    int64_t movetime_ms = 0;
    int64_t reviewed    = 0;  // seconds since the epoch
    std::string best, line;
    std::optional<fastchess::Score> score;
    std::optional<std::array<int64_t, 3>> wdl;
    std::string reply;
    std::optional<fastchess::Score> after;
    std::optional<std::array<int64_t, 3>> after_wdl;
    // The review's comment on the mistake, held to the boards (#64), in the commentary's language then ("en", "fr").
    std::string comment, comment_lang;
    // Its card.
    study::fsrs::Card card;
    int64_t since = 0;  // its first answer, seconds since the epoch; 0 before
    // A key moment of a game's guided visit answered wrong (#94): kept whatever the scans of the user's games find.
    bool visit = false;

    [[nodiscard]] chess::Board board() const;         // the position
    [[nodiscard]] chess::Color side() const;          // the user's: the side to move there
    [[nodiscard]] AnalysisPosition position() const;  // as the review searched it
    [[nodiscard]] SearchLimit limit() const;          // the review's: its nodes, else its time
    [[nodiscard]] PositionEval eval() const;          // the review's at the position
};

// What makes a mistake an exercise (docs/design/mistakes.md): a move of the user's the review judged ? or ??, where its
// search went deep enough to name a best move (its third pass at least: 320,000 nodes, or 320 ms a position), the best
// move known and not the one played, and the game not decided anyway: the best move keeps the user in the game (an
// expected score of 10 % at least, the review's "lost anyway") and the move played did not keep a win (at most 90 %).
inline constexpr uint64_t EXERCISE_NODES       = 320000;
inline constexpr int64_t EXERCISE_MOVETIME_MS  = 320;
inline constexpr double EXERCISE_LOST_ANYWAY   = 10.0;
inline constexpr double EXERCISE_STILL_WINNING = 90.0;

// The exercises of a game the user played as `side`, by its judging review: in the game's order, without their place
// (database, folder, number). `lang` writes the review's comments on the mistakes.
[[nodiscard]] std::vector<MistakeExercise> exercisesOf(const ReplayGame& game, const GameReview& review,
                                                       chess::Color side, commentary::Lang lang);
// What tells a review from another for the exercises it gives: its engine, passes, limit and positions.
[[nodiscard]] std::string reviewSignature(const GameReview& review);
// A kind of exercise ("To work on"'s topic key) in English, to be translated (N_); the key itself when unknown.
[[nodiscard]] const char* mistakeKindName(std::string_view key);
// The Lichess puzzle theme that trains a kind ("fork": "fork", "en_prise": "hangingPiece"); empty for none.
[[nodiscard]] std::string_view mistakeKindTheme(std::string_view key);
// The packed date (study::packDate()) `days` days before `now` (seconds since the epoch), in local time.
[[nodiscard]] uint32_t packedDaysBefore(int64_t now, int days);

// ---------------------------------------------------------------------------
// The answers

// An answer judged the way the review judges a move: the review's evaluation of the position against its engine's
// search, at the review's settings, of the position after the answer. Right when it loses 0.3 pawn at most against the
// review's best (the review's centipawns), or when the review would not mark it and it keeps a win (90 % or more).
inline constexpr int ANSWER_CP      = 30;
inline constexpr double ANSWER_WINS = 90.0;
struct AnswerCheck {
    bool right    = false;
    bool judged   = false;  // both positions had something to say
    int cp_loss   = 0;
    double before = 50.0, after = 50.0;  // the user's expected score with the review's best, with the answer
    std::optional<Judgement> judgement;  // what the review would have marked the answer
};
[[nodiscard]] AnswerCheck checkAnswer(const MistakeExercise& exercise, const chess::Move& move,
                                      const PositionEval& searched);
// The position after `move` ends the game: its score without an engine (endScore()), none when it goes on.
[[nodiscard]] std::optional<PositionEval> endedAfter(const MistakeExercise& exercise, const chess::Move& move);

// ---------------------------------------------------------------------------
// What the profile keeps

// A reviewed game of the user's: its review's signature when its exercises were found, and the positions they are.
struct ScannedGame {
    std::string review;
    std::vector<uint64_t> keys;
};

// The card of an exercise no game gives any longer (a name taken away, a database removed, a review changed): kept, so
// that the exercise comes back with it when a game gives it again (nothing is lost by a gesture undone).
struct KeptCard {
    study::fsrs::Card card;
    int64_t since = 0;
};

struct MistakeRecord {
    std::map<uint64_t, MistakeExercise> exercises;  // by position
    std::map<std::string, ScannedGame> games;       // by review key
    std::map<uint64_t, KeptCard> kept;              // by position: the cards answered of exercises gone
    int64_t answer_ms = 0;                          // how long an answer takes, on average; 0 before any
    // An exercise gone: its card kept when it was ever answered.
    void keep(const MistakeExercise& exercise);
    // A card kept given back to an exercise that comes back; false when none is kept for it.
    bool giveBack(MistakeExercise& exercise);
};
[[nodiscard]] MistakeRecord readMistakeRecord(const std::filesystem::path& folder);
bool writeMistakeRecord(const std::filesystem::path& folder, const MistakeRecord& record);
// An answer, at the end of answers.jsonl: the position, when, its grade, how long it took, the move played ("" for
// "I don't know").
void appendMistakeAnswer(const std::filesystem::path& folder, uint64_t key, int64_t at, study::fsrs::Rating rating,
                         int64_t ms, const std::string& played);

// The day's counts of a set of exercises: those due (reviewed before, due by the end of the day), new (never answered),
// today's new ones (the day's limit less those begun today), known (due after today); of the known, due by the end of
// tomorrow and of the third day.
struct MistakeCounts {
    std::size_t exercises = 0, games = 0, due = 0, fresh = 0, today = 0, known = 0, tomorrow = 0, three_days = 0;
};
[[nodiscard]] MistakeCounts mistakeCounts(const std::vector<const MistakeExercise*>& exercises,
                                          const study::TrainingDay& day, std::size_t new_per_day);
// A session's exercises, by position: those due unless `with_due` is false, the longest overdue first; then `fresh`
// new ones, the latest games first, the blunders before the mistakes.
[[nodiscard]] std::vector<uint64_t> mistakeOrder(const std::vector<const MistakeExercise*>& exercises,
                                                 const study::TrainingDay& day, std::size_t fresh, bool with_due);

// ---------------------------------------------------------------------------
// The user's games, looked for

// A database the user's games are looked for in: its name, folder and snapshot (held while the scan reads it).
struct MistakeSource {
    std::string name;
    std::filesystem::path folder;
    std::shared_ptr<const study::Snapshot> snapshot;
};

// A game of the user's without a review, among the latest: where it is and the game itself, to be reviewed.
struct UnreviewedGame {
    std::string database;
    std::filesystem::path folder;
    uint64_t id = 0;
    ReplayGame game;
    chess::Color side = chess::Color::WHITE;
    uint32_t date     = 0;
};

struct MistakeScan {
    MistakeRecord record;      // the exercises found, the cards of those found before kept
    std::size_t games    = 0;  // the user's games found (a game in two databases once)
    std::size_t reviewed = 0;  // of them, with a review
    // Of the `recent` latest of them, those without a review, the latest first.
    std::vector<UnreviewedGame> unreviewed;
    bool cancelled = false;
};
// The user's games in `sources` under `names`, their reviews in `analyses` (the profile's analyses/), each game's
// exercises found again when its review changed since `before`, else taken from it. The latest `recent` games tell
// which wait for a review.
[[nodiscard]] MistakeScan scanMistakes(const std::vector<MistakeSource>& sources, const std::vector<std::string>& names,
                                       const std::filesystem::path& analyses, const MistakeRecord& before,
                                       std::size_t recent, commentary::Lang lang,
                                       const std::atomic<bool>* cancel = nullptr);
// A game's exercises put in a record in place of those it gave before (a game reviewed in the background), their
// cards kept, those of the exercises gone kept apart: `game` its review key, `exercises` as exercisesOf() gives them,
// with their place.
void putExercises(MistakeRecord& record, const std::string& game, const std::string& signature,
                  std::vector<MistakeExercise> exercises);

// ---------------------------------------------------------------------------
// Guessing the moves

// A guess scored as mockup M12 shows it: the game's move 3 points; else, by the engine's evaluations after each move,
// for the side that played: as good (0.2 pawn worse at most, or better) 2, playable (0.5 pawn worse at most) 1, else 0.
inline constexpr double GUESS_AS_GOOD  = 0.2;
inline constexpr double GUESS_PLAYABLE = 0.5;
[[nodiscard]] int guessPoints(bool same, double game_pawns, double guess_pawns);
// A score as pawns for its side: a mate for it 100, less a tenth of a pawn a move; against it the opposite.
[[nodiscard]] double scorePawns(const fastchess::Score& score);

// A game guessed, as guess/games.jsonl keeps it (a line each).
struct GuessRecord {
    int64_t at = 0;
    std::string title;
    bool white = true;
    int points = 0, max = 0, same = 0, moves = 0;
    double loss = 0;  // the average pawns lost against the game's moves
};
void appendGuessRecord(const std::filesystem::path& folder, const GuessRecord& record);
[[nodiscard]] std::optional<GuessRecord> lastGuessRecord(const std::filesystem::path& folder);

// ---------------------------------------------------------------------------
// The sessions on the Board page (view_mistakes.cpp, view_guess.cpp)

// A session of exercises: the Board page's column becomes the exercise's, as for the repertoires and the puzzles.
struct MistakeRun {
    enum class Phase { Asking, Checking, Right, Wrong, End };
    bool review   = false;                   // the day's review: the exercises due alone
    bool practice = false;                   // "Review them now": the missed ones again, nothing graded
    std::string kind;                        // the kind it was started for ("" all)
    std::vector<MistakeExercise> exercises;  // as they were when it started
    std::deque<std::size_t> queue;           // to ask, the one asked first
    std::size_t total = 0, done = 0;
    Phase phase = Phase::Asking;

    // The exercise asked.
    std::size_t current = 0;
    bool again          = false;  // answered earlier in the session, and asked again
    bool hint = false, hinted = false;
    bool retry   = false;  // played again after a wrong answer: not graded again
    bool shown   = false;  // "I don't know": the review's move shown, none played
    bool hovered = false;  // the mouse over the answer: it does not go on by itself meanwhile
    std::string played;    // the answer, SAN
    chess::Move played_move{};
    std::chrono::steady_clock::time_point started, asked, answered;
    double seconds = 0;

    // The answer judged by the review's engine at the review's settings, the analysis engine when the library no longer
    // has it.
    Analyser judge;
    std::string judge_engine;  // the one judging, by its name
    bool judge_other = false;  // not the review's
    bool judge_used  = false;  // the answer was searched (not the review's move, the game's or a mate)
    AnalysisPosition checking;
    PositionEval searched;  // after the answer
    AnswerCheck check;
    std::string check_error;  // why the answer could not be judged

    // What the answer changed.
    study::fsrs::Rating rating = study::fsrs::Rating::Good;
    int64_t next_review        = 0;
    bool next_again            = false;

    // The first answer to each exercise, in the order asked.
    struct Outcome {
        std::size_t exercise = 0;
        bool right           = false;
        std::string played;  // SAN, "" for "I don't know"
    };
    std::vector<Outcome> outcomes;

    // The board before the session, given back after it.
    bool aside = false, flip = false;
    int tab = 0;

    // The end.
    double took      = 0;
    bool stopped     = false;
    int missed_shown = -1;  // the missed exercise whose position the board shows
};

// A game guessed: replayed from one side, a move to guess at each of its turns.
struct GuessRun {
    enum class Phase { Preparing, Asking, Checking, Shown, End };
    ReplayGame game;
    bool chess960 = false;
    std::string title;
    chess::Color side = chess::Color::WHITE;
    int from          = 0;  // the position it starts at
    int ply           = 0;  // the question's: the side to move there
    Phase phase       = Phase::Preparing;
    std::future<std::size_t> theory;  // the plies of theory of the user's databases, passed by themselves
    std::size_t book = 0;

    // The analysis engine, one thread, a second a move: the position after the game's move, then after the guess.
    Analyser engine;
    std::string engine_name;
    AnalysisPosition game_position, guess_position;
    std::optional<fastchess::Score> game_score, guess_score;  // for the side to move after each move
    bool guess_asked = false;
    std::string error;
    chess::Move guess{};
    std::chrono::steady_clock::time_point started, shown_at;
    bool hovered = false;

    struct Guess {
        int ply = 0;
        std::string guess, game;  // UCI
        int points        = 0;
        bool same         = false;
        double game_pawns = 0, guess_pawns = 0;  // after each, for the side that played
    };
    std::vector<Guess> guesses;
    int points = 0;

    bool aside = false, flip = false;
    int tab         = 0;
    bool stopped    = false;
    double took     = 0;
    int shown_guess = -1;  // the end: the guess whose position the board shows
};

}  // namespace fcg
