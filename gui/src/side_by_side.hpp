#pragma once

// Several engines of the library analysing the study Board's position side by side (#78, mockup M11, board 3): each
// engine's settings there, the threads they share, the words that say how they agree, the arrows they draw on the
// board, their evaluations along the game for the graph, and their processes kept to what is asked. Without ImGui.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <json.hpp>

#include <types/score.hpp>

#include "analysis.hpp"
#include "game_analysis.hpp"
#include "model.hpp"

namespace fcg {

// At most this many engines analyse side by side.
inline constexpr std::size_t MAX_SIDE_ENGINES = 4;

// One engine of the analysis and its settings there, which are the analysis's own: neither the library engine nor a
// tournament sees them.
struct SideEngine {
    std::string engine;                            // a library engine's name
    int threads                              = 0;  // set by hand; 0: its share of the analysis's threads
    int hash                                 = 0;  // MB; 0: the engine's own
    int multipv                              = 3;
    bool operator==(const SideEngine&) const = default;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(SideEngine, engine, threads, hash, multipv)

// The colour of each engine by its place: dark blue, light orange, teal, grey, their lightness apart too (the names
// are always shown beside them, so that the colour is never the only cue). IM_COL32 values, opaque.
[[nodiscard]] uint32_t sideColour(std::size_t index);

// What an engine asks of the analysis's threads.
struct ThreadAsk {
    int hand = 0;      // set by hand; 0: its share
    bool gpu = false;  // it searches on a GPU: its threads are not counted
};

// The threads each engine searches with: those set by hand as they are; a GPU engine's left to the engine (0) unless
// set by hand, and out of the total either way; the rest of the total shared equally between the others, the first
// ones taking what does not divide, one at least each.
[[nodiscard]] std::vector<int> shareThreads(int total, const std::vector<ThreadAsk>& asks);

// Whether an engine searches on a GPU: the library says so ("Uses a GPU"), or its options do, as Lc0's Backend (with
// its BackendOptions) naming a GPU backend: cuda, cudnn, opencl, metal, dx12, onnx-cuda, onnx-dml, trt…
[[nodiscard]] bool searchesOnGpu(const EngineEntry& entry);

// What one engine says of the position, for the words over the cards.
struct EngineVerdict {
    std::string name;
    std::string move;                       // its best move as shown ("7…Be7"); empty: none yet
    std::optional<fastchess::Score> white;  // its evaluation, White's
};

// A piece of a sentence, strong (a move) or not.
struct TextRun {
    std::string text;
    bool strong = false;
};

// How the engines agree, in words: "7…Be7 for Stockfish and Gaia · 7…Qb6 for Leela · their evaluations within 0.11",
// "Stockfish and Gaia agree on 7…Be7 · the same evaluation", "… · Stockfish sees #5"; the moves strong. Empty with
// fewer than two engines that have a move.
[[nodiscard]] std::vector<TextRun> agreementRuns(const std::vector<EngineVerdict>& verdicts);
// The runs' text, one after the other.
[[nodiscard]] std::string runsText(const std::vector<TextRun>& runs);

// One arrow on the board for the engines that play a move: the move (UCI), the first engine that plays it (whose
// colour it takes), and how many do.
struct SideArrow {
    std::string move;
    std::size_t engine = 0;
    int count          = 1;
};
// The arrows of the engines' best moves (UCI, in the engines' order; empty: none yet): one per move, in the order of
// the first engine that plays each.
[[nodiscard]] std::vector<SideArrow> sideArrows(const std::vector<std::string>& best);

// Keeps an engine's search of the position `ply` of a game of `plies` moves in `trail`, for the graph's thin line:
// its best line's score, move and depth, unless the trail has a deeper search of that position. A bound is not kept.
void keepTrail(GameReview& trail, std::size_t plies, int ply, const AnalysisLine& line);

// One engine to run: its library entry (null when the library no longer has it) and the settings its analyser gets.
struct SideRun {
    const EngineEntry* entry = nullptr;
    AnalysisSettings settings;
};

// Brings `analysers` to `runs`, one each, in order. An analyser of another engine ends, and one that is off starts
// when `search`; one that failed keeps its error until it is stopped; the others are told their settings
// (Analyser::update). Then each searches `position` when `search`, or halts. Those beyond `runs` end.
void runSideBySide(std::vector<std::unique_ptr<Analyser>>& analysers, const std::vector<SideRun>& runs, bool search,
                   const AnalysisPosition& position, bool new_game);

}  // namespace fcg
