#pragma once

// Every move explained (#94, mockup M13, board 7, point 15): a line for each move of a reviewed game, beside its moves
// on the study Board, kept with the review. The moves of theory are grouped, a line for each opening name they reach
// (three at most), with the games of the user's databases that reached their last position; a move too simple to need
// more is said in a word ("Forced recapture."); the others get a sentence of the model, written from the facts, its
// grammar naming only legal moves and the checker reading each sentence, or the template's when the checker refuses the
// model's (what it refused kept, for the hover), or the review's own words when neither has anything to say. Each line
// says where it comes from (R9). "Write them into the game" makes them the assistant's comments (assistant_notes.hpp).
//
// Kept in the profile beside the game's review: analyses/<key>.explained.json, for the game, its review and the
// assistant's language.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <commentary/brief.hpp>
#include <commentary/commentator.hpp>
#include <commentary/review.hpp>

namespace fcg {

struct ExplainedLine {
    enum class Source : uint8_t { Theory, Model, Template, Simple, Review };
    std::size_t from = 0,
                to   = 0;  // the plies it covers: the moves leading to positions from..to (one move: from == to)
    std::string text;
    Source source  = Source::Template;
    bool written   = false;  // its text is there: a line still to write while the explanations are being made
    uint64_t games = 0;      // a theory line: the games of the user's databases that reached its last position
    // A template standing for the model's sentence: what the checker refused of the model's last try.
    std::optional<commentary::Refusal> refused;
    bool matters = false;  // a judged move, an only move, a turning point, a plan, the novelty
    bool key     = false;  // one of the review's key moments
};

struct Explained {
    std::string key;     // the game's (reviewKey())
    std::string review;  // the review they were made from: its engine, passes and positions (explainedReview())
    std::string lang;    // the assistant's language: "en", "fr"
    std::vector<ExplainedLine> lines;
    bool complete     = false;
    double model_secs = 0;  // the model's time, over model_lines lines
    int model_lines   = 0;
    std::string device;  // where the model ran, as its process says
};

// What tells a review from another for the explanations: its engine, its passes and its positions.
[[nodiscard]] std::string explainedReview(const std::string& engine, int passes, std::size_t positions);

// The lines of a reviewed game before any is written: the theory runs, the moves said in a word written already, the
// others to write. `games` gives the games of the user's databases that reached a position (none when unknown).
[[nodiscard]] std::vector<ExplainedLine> explainPlan(const commentary::MainLine& line,
                                                     const commentary::GameReport& report, commentary::Lang lang,
                                                     const std::function<uint64_t(const commentary::Position&)>& games);

// The word a move too simple for more gets, in the assistant's language: a forced recapture, the only legal move; none
// for another move.
[[nodiscard]] std::optional<std::string> simpleMoveWord(const commentary::Position& before,
                                                        const std::optional<commentary::Move>& previous,
                                                        const commentary::Move& played, commentary::Lang lang);

// One move written: the model's comment when the checker lets it through (commentary::ATTEMPTS tries), else the
// template's, held to the boards, with what the checker refused of the model's last try; without a model, or nothing
// for it to put into words, the template's at once; the review's own words when the template has nothing either.
struct WrittenLine {
    std::string text;
    ExplainedLine::Source source = ExplainedLine::Source::Template;
    std::optional<commentary::Refusal> refused;
    bool asked = false;  // the model was asked
};
[[nodiscard]] WrittenLine writeExplainedLine(commentary::Model* model, const commentary::MoveContext& ctx,
                                             commentary::Lang lang, commentary::PieceLetters letters);

// The figures the summary says: the moves, those the model wrote, those of theory and their lines, those said in a
// word, the templates', those the checker refused, the review's.
struct ExplainedCounts {
    int moves = 0, model = 0, theory_moves = 0, theory_lines = 0, simple = 0, templates = 0, refused = 0, review = 0;
    int written = 0;  // the lines written so far, of lines.size()
};
[[nodiscard]] ExplainedCounts explainedCounts(const Explained& explained);

bool saveExplained(const std::filesystem::path& dir, const Explained& explained);
[[nodiscard]] std::optional<Explained> loadExplained(const std::filesystem::path& dir, const std::string& key,
                                                     const std::string& lang);

}  // namespace fcg
