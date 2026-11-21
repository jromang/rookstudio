#pragma once

// The training of the repertoires (#76, mockup M11, board 1), kept in the profile beside each repertoire, so that its
// PGN never changes: <name>.training.json holds its cards' FSRS memories and the moves each was learnt with,
// <name>.reviews.jsonl one line per answer. The profile's training.json holds the training's settings. Nothing leaves
// the machine.
//
// A session runs on the Board page (view_training.cpp), whose column becomes the training's: TrainingRun is its state.

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <future>
#include <optional>
#include <string>
#include <vector>

#include <chess.hpp>
#include <study/game.hpp>
#include <study/training.hpp>

namespace fcg {

struct TrainingFile {
    study::Memories memories;
    int64_t answer_ms = 0;  // how long an answer takes on average (a session's time foreseen); 0 before any
};

// Beside a repertoire's PGN (Najdorf.pgn): Najdorf.training.json, Najdorf.reviews.jsonl.
[[nodiscard]] std::filesystem::path trainingFileOf(const std::filesystem::path& repertoire);
[[nodiscard]] std::filesystem::path reviewsFileOf(const std::filesystem::path& repertoire);
// Empty when the file is missing or cannot be read.
[[nodiscard]] TrainingFile readTrainingFile(const std::filesystem::path& repertoire);
bool writeTrainingFile(const std::filesystem::path& repertoire, const TrainingFile& file);
// An answer, at the end of the reviews: the position, when, its grade, how long it took, the move played ("" for
// "I don't know").
void appendReview(const std::filesystem::path& repertoire, uint64_t key, int64_t at, study::fsrs::Rating rating,
                  int64_t ms, const std::string& played);

struct TrainingSettings {
    int advance_ms  = 1000;  // after a right answer, the next position on its own after this; 0: on Enter only
    int new_per_day = 20;    // new positions a day at most
    bool guess_theory = true;  // guessing the moves (#81): the moves of theory pass by themselves
};
[[nodiscard]] TrainingSettings readTrainingSettings(const std::filesystem::path& profile);
void writeTrainingSettings(const std::filesystem::path& profile, const TrainingSettings& settings);

// The end of the training day that holds `now` (seconds since the epoch), in local time: the next 4 a.m., so that a
// session after midnight still counts for the evening before.
[[nodiscard]] int64_t trainingDayEnd(int64_t now);
[[nodiscard]] study::TrainingDay trainingDay(int64_t now);
[[nodiscard]] int64_t epochNow();

// The training's scheduler: FSRS-6's parameters, a 90 % retention, one learning step and one relearning step of ten
// minutes, which the session turns into the card asked again a few positions later.
[[nodiscard]] study::fsrs::Scheduler trainingScheduler();

// A session on the Board page.
struct TrainingRun {
    // Due: the positions due, then today's new ones; Review: the positions due alone (the day's review, #80).
    enum class Kind { Due, New, Missed, Review };
    enum class Phase { Preparing, Asking, Right, Wrong, End };
    Kind kind = Kind::Due;
    std::filesystem::path file;  // the repertoire's PGN
    std::string name;
    chess::Color side = chess::Color::WHITE;
    std::string start_fen;
    std::optional<study::TrainingSession> session;
    Phase phase = Phase::Preparing;
    TrainingFile kept;  // as written after each answer
    TrainingSettings settings;
    std::size_t left_out = 0;  // positions not asked while a correspondence game on Lichess passes through them

    // The question.
    std::size_t position = 0;      // its place in the session: "position 3 of 17"
    bool again           = false;  // answered earlier in the session, and asked again
    std::chrono::steady_clock::time_point started, asked, answered;
    bool hint  = false;
    bool retry = false;  // played again after a wrong answer: not graded again
    bool shown = false;  // "I don't know": the move shown without one played
    std::string played;  // the move played, in SAN
    chess::Move played_move{};
    study::TrainingSession::Graded graded;
    study::fsrs::Rating rating = study::fsrs::Rating::Good;
    double seconds             = 0;
    int64_t graded_at          = 0;
    bool hovered               = false;  // the mouse over the answer: it does not go on by itself meanwhile

    // The board before the session, given back after it.
    bool aside = false;  // the game shown was put among the recent games
    bool flip  = false;
    int tab    = 0;  // the column's tab: the Tree's during the session rather than the notation's

    // The move added to the repertoire from a wrong answer, which Ctrl+Z takes back.
    struct Added {
        std::size_t repertoire = 0;
        GameTree lines;
        std::size_t card = 0;
        std::string san;
    };
    std::optional<Added> added;

    // The end.
    double took      = 0;  // seconds
    bool stopped     = false;
    int missed_shown = -1;  // the missed move whose position the board shows
};

}  // namespace fcg
