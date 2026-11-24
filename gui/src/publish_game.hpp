#pragma once

// A game to print (#93, mockup M13, board 6, point 14): its headers at the top of the page, its moves in one or two
// columns with their comments and variations, the assistant's comments marked as its own and the evaluations beside
// the moves when asked, diagrams at the key moments, at each marked comment or every 10 moves; and its lines as the
// replayable lines of a page of HTML. The documents of the other printables (a repertoire, a dossier, an opening
// report, a crosstable) are made where their views are, from the same blocks (publish_doc.hpp).

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <study/game.hpp>
#include <study/repertoire.hpp>

#include "publish_doc.hpp"

namespace fcg::publish {

struct PrintGame {
    GameTree tree;
    bool chess960 = false;
    MoveNotes start_notes;
    std::string white, black;    // as shown
    std::vector<int> key_plies;  // its key moments, plies of the game: the review's mistakes when it has one
};

enum class DiagramsAt : int { KeyMoments = 0, MarkedComments, EveryTenMoves, None };

struct GamePrintOptions {
    bool letter         = false;
    int columns         = 2;
    DiagramsAt diagrams = DiagramsAt::KeyMoments;
    bool assistant      = false;  // the assistant's comments, marked
    bool evals          = false;  // the evaluations beside the moves
    bool headers        = true;   // the game's headers at the top of the page
};

// The plies of the game that get a diagram: the key moments (the review's, else the moves marked !!, ??, ?, !), the
// moves whose comment has marks (arrows, squares, or [#]), or the end of every tenth move.
[[nodiscard]] std::vector<int> diagramPlies(const PrintGame& game, DiagramsAt at);

// What every document of RookStudio carries: its pieces, their notice, the board's colours, the language.
struct DocumentLook {
    std::shared_ptr<const PieceShapes> pieces;
    PiecesNotice notice;
    appearance::BoardColours colours;
    std::string lang = "en";
};

[[nodiscard]] Document gameDocument(const PrintGame& game, const GamePrintOptions& options, const DocumentLook& look);

// A line of moves from the start position as spans, each move a node of `game` in the page's games (its plies from 0)
// when `game` is not -1: "1.e4 c5 2.Nf3".
[[nodiscard]] std::vector<Span> lineSpans(const std::vector<std::string>& sans, int game, int first_ply = 0,
                                          bool black_first = false, int first_number = 1);
// The same line as a game of a page of HTML, to replay; nullopt when its moves do not play from the start.
[[nodiscard]] std::optional<HtmlGame> lineGame(const std::vector<std::string>& sans);

// A repertoire printed: each line under the one it starts from, a diagram at each branching, the user's notes; its
// coverage of the databases' games and its gaps as an annex, when measured.
[[nodiscard]] Document repertoireDocument(const std::string& name, chess::Color side, const GameTree& lines,
                                          const std::optional<study::Coverage>& coverage, bool letter,
                                          const DocumentLook& look);

}  // namespace fcg::publish
