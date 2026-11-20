#pragma once

// What a tournament's engines wrote about their moves, as the study Board shows it (#87, mockup M12, board 8): the
// evaluation and the depth beside each move, every other field on hovering it, and the game's curve as the two engines
// saw it, drawn from the game alone, with each move's time and the moments that stand out.

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "pgn_store.hpp"

namespace fcg {

// Whether the move's evaluation is its player's own: a tournament engine's comment (fastchess's style, or its
// ChessBase style, which has an [%emt]), a game played from the Board; not an analysis written into the game ([%eval]
// alone: Lichess's analysis, an annotator's).
[[nodiscard]] bool playerEvaluation(const ReplayMove& move);
// Whether a move of the game has its player's evaluation.
[[nodiscard]] bool hasPlayerEvaluations(const ReplayGame& game);

// The time the move took: the tournament's, else its [%emt]; -1 when neither tells.
[[nodiscard]] int64_t moveTime(const ReplayMove& move);

// What the notation shows beside the move of an engine (a setting of the study Board's notation).
enum class EngineTags : uint8_t { EvalDepth, EvalDepthTime, Nothing };

// The text beside a move: White's evaluation and the depth ("+1.44/22", "#-3/40"), then its time if asked ("+1.44/22
// 1.34s"); empty for a move without its player's evaluation, a book move, or Nothing.
[[nodiscard]] std::string engineTag(const ReplayMove& move, EngineTags tags);

// A move's fields told in full, for its tooltip: rows of a label and a text, the mover's chances, the line it expected.
struct EngineDetail {
    std::string title;  // "7…O-O-O · Gaia 4.4.2, Black"
    std::vector<std::pair<std::string, std::string>> rows;
    std::optional<std::array<float, 3>> wdl;  // the mover's win, draw and loss shares, 0 to 1
    std::string wdl_text;                     // "win 1 % · draw 36 % · loss 63 %"
    std::string expected;                     // the line it expected after its move, in SAN, numbered
};
// The details of move `ply` of the game (1 for the first); nullopt for a move without its player's evaluation.
[[nodiscard]] std::optional<EngineDetail> engineDetail(const ReplayGame& game, bool chess960, int ply);

// The UCI moves the engine expected after its move: its PV without the move itself; none when its PV does not start
// with the move played.
[[nodiscard]] std::vector<std::string> expectedLine(const ReplayMove& move, bool chess960);

// White's chances after a move, as the engine that played it saw them (graphWin(): its WDL turned around for Black,
// else its evaluation converted).
struct CurvePoint {
    int ply   = 0;  // the position after the move
    float win = 0;
};

// What stands out in the engines' view of the game, each a click away.
struct EngineMoment {
    enum class Kind : uint8_t { BookEnd, Apart, Tables, Mate, End };
    Kind kind = Kind::End;
    int ply   = 0;   // where it is (the position after the move)
    int last  = -1;  // where it ends (Apart)
    int side  = 0;   // the engine it is about (Tables, Mate): 0 White's, 1 Black's
    std::string text;
};

// The game's curve as its two engines saw it.
struct EngineCurves {
    std::array<std::string, 2> names;                          // White's player, Black's
    std::array<std::vector<CurvePoint>, 2> win;                // each one at its own moves
    std::array<std::vector<std::pair<int, int64_t>>, 2> time;  // each one's moves: ply, milliseconds
    std::array<int64_t, 2> time_used{};                        // its moves out of the book
    std::array<std::optional<double>, 2> depth, nps;           // on average, over the moves that tell them
    int plies = 0;
    int book  = 0;                           // the book's moves from the start: moves 1 to `book`
    std::vector<std::pair<int, int>> apart;  // the plies where they saw different games: first, last
    std::vector<EngineMoment> moments;       // in the game's order
};

// Two engines apart by this share of White's chances, for two moves each in a row, see different games.
inline constexpr float APART_SHARE = 0.15f;

// The curves of a game that has its players' evaluations, and its moments: where the book ends and how each engine
// saw the game then, where they saw different games, where the tablebases enter a search, where a mate is seen, how
// the game ended.
[[nodiscard]] EngineCurves engineCurves(const ReplayGame& game);

// White's chances by the engine `side` at position `ply`: its last value at or before it; NaN before its first.
[[nodiscard]] float curveAt(const EngineCurves& curves, int side, int ply);

}  // namespace fcg
