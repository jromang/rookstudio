#pragma once

// The opening report of a position (#91, mockup M13, board 4) behind its page (view_opening_report.cpp): its figures
// made in the background from the Tree tab's databases (study/opening_report.hpp), section by section, the structures
// and plans named by the assistant's facts; the evaluations of its moves, Lichess's kept offline (#58) then the
// analysis' engine; its summary written by the assistant's model from the figures alone and held to them by the
// checker, the templates' otherwise (commentary/opening_summary.hpp). Nothing here leaves the machine: Lichess's
// sources are asked by the page as the Tree tab asks them.

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <chess.hpp>
#include <commentary/commentator.hpp>
#include <commentary/opening_summary.hpp>
#include <study/evals_index.hpp>
#include <study/opening_report.hpp>

#include "lichess_api.hpp"
#include "model.hpp"

namespace fcg {

// The structures and the plans of a report as the assistant's facts name them: a structure's kind is its formation's
// number (commentary::Formation), a plan's its commentary::PlanName.
[[nodiscard]] const study::StructureNamer& reportStructures();
[[nodiscard]] const study::PlanNamer& reportPlans();

// A database the report reads: one of the Tree tab's, whose opening tree covers its games.
struct ReportBase {
    std::string name;
    std::filesystem::path folder;
    std::shared_ptr<const study::Snapshot> snapshot;
    std::shared_ptr<const study::Tree> tree;
};

// The report's figures as they are made, read by the window at each frame: each section published once made.
struct ReportShared {
    std::mutex mutex;
    std::optional<study::OpeningReport> report;
    uint64_t serial = 0;  // the sections published so far
};

// The report of a position (its FEN) from the bases, in the background at the lowest priority: each section published
// in `shared`; the games of the user's (Settings › Your names) told by `user_names`.
void makeReport(const std::string& fen, const std::vector<ReportBase>& bases,
                const std::vector<std::string>& user_names, int threads, study::ReportProgress& progress,
                const std::atomic<bool>& cancel, ReportShared& shared);

// A move's figures from a source of Lichess's explorer, added to those of the databases when the source is ticked.
struct ReportLichessMove {
    std::string uci;
    uint64_t white = 0, draws = 0, black = 0;
    int rating = 0;  // the mean rating of its games
};

// A row of the report's moves, as the page shows it: the databases' figures and those of Lichess's sources ticked.
struct ReportRow {
    chess::Move move;
    std::string san;      // in English letters
    int code       = -1;  // the move's code on the position (ScanBoard), -1 for a move only Lichess's sources played
    uint64_t games = 0;
    double white = 0, draws = 0, black = 0;  // shares of the decided games
    int score = -1;                          // the side to move's, in percent; -1 without results
    int elo = 0, perf = 0, last = 0;
    std::vector<std::pair<std::string, uint64_t>> by_source;  // each source's games
};

// The rows of the position's moves: the databases' (`moves`, their codes on `board`) and each Lichess source's moves
// added, the most played first; a move under a hundredth of the games left out past the tenth.
[[nodiscard]] std::vector<ReportRow> reportRows(
    const chess::Board& board, const study::ReportMoves& moves, const std::vector<std::string>& base_names,
    const std::vector<std::pair<std::string, std::vector<ReportLichessMove>>>& lichess);

// FIDE's rating difference for a score (handbook B.02, table 8.1a), as the Tree tab's performance takes it.
[[nodiscard]] int scoreDifference(double score);

// A move's evaluation, the position after it, White's.
struct ReportEval {
    std::optional<int32_t> cp;
    std::optional<int> mate;
    int depth                                = 0;
    bool engine                              = false;  // the analysis' engine's; else Lichess's, kept offline
    bool operator==(const ReportEval&) const = default;
};

// The positions after the report's moves evaluated: Lichess's evaluations kept offline first; the analysis' engine
// for those it does not have, at most `engine_most`, one thread, `nodes` nodes each, from an empty hash. `done` counts
// the positions dealt with; `cancel` stops between two searches.
struct ReportEvalAsk {
    std::vector<std::string> fens;
    std::shared_ptr<const study::EvalsLookup> lookup;  // none: nothing kept offline
    std::optional<EngineEntry> engine;                 // none: no engine
    uint64_t nodes          = 1'000'000;
    std::size_t engine_most = 5;
};
[[nodiscard]] std::vector<std::optional<ReportEval>> evaluateReport(const ReportEvalAsk& ask, std::atomic<int>& done,
                                                                    const std::atomic<bool>& cancel,
                                                                    std::string* error = nullptr);

// The figures of the summary from the report as the page shows it, its moves written in the letters of `lang`.
struct ReportView {
    chess::Board board;             // the report's position
    std::vector<chess::Move> path;  // the moves that led there from the start, when known
    std::string opening;            // "D36 Queen's Gambit Declined: Exchange Variation", empty when not named
    const study::OpeningReport* report                  = nullptr;
    const std::vector<ReportRow>* rows                  = nullptr;
    const std::vector<std::optional<ReportEval>>* evals = nullptr;  // by row
};
[[nodiscard]] commentary::SummaryFigures summaryFigures(const ReportView& view, commentary::Lang lang);

// A move as numbered from a position: "11.Rab1", "10…Nf8", in the letters of `lang` (French: « 11.Tab1 »).
[[nodiscard]] std::string numberedSan(const chess::Board& before, const chess::Move& move, commentary::Lang lang);
// A line's codes from a position, as moves.
[[nodiscard]] std::vector<chess::Move> movesOfCodes(const chess::Board& from, const std::vector<int>& codes);

// Lichess's explorer answers the report showed, kept in the profile (lichess-cache/explorer/, by the Tree tab's key of
// the question): shown with their date when Lichess cannot be reached.
void keepExplorer(const std::filesystem::path& profile, const std::string& key, const lichess::Explorer& answer);
[[nodiscard]] std::optional<std::pair<lichess::Explorer, int64_t>> keptExplorer(const std::filesystem::path& profile,
                                                                                const std::string& key);

// The summary written: the model's passages, each held to the figures, the templates' where the checker refused twice;
// the templates' alone without the model.
struct ReportSummary {
    std::vector<commentary::SummaryPassage> passages;
    std::string error;       // why the model gave no answer
    bool by_model  = false;  // the model was asked
    bool cancelled = false;
    double seconds = 0;
};
[[nodiscard]] ReportSummary writeSummary(commentary::Model* model, const commentary::SummaryFigures& figures,
                                         commentary::Lang lang, uint32_t round, const std::atomic<bool>& cancel,
                                         std::atomic<int>& done);

}  // namespace fcg
