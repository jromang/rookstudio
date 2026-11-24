#pragma once

// A replayable HTML page of games in one file that works offline (#93, mockup M13, board 6, point 13): the board in
// the app's theme and pieces (drawn as the diagrams are, publish_draw.hpp), the moves with their comments and
// variations, a move or a variation followed with a click, the arrows and squares at their move, ← and → on the
// keyboard; the assistant's comments marked as its own, the evaluations and their curve, the clocks, as asked. Nothing
// in it is fetched from a network: its script, styles and pieces are in the file, and the only addresses in it are
// those of the pieces' notice. Beyond MAX_GAMES_PER_PAGE games, one page per game in a folder with their list.

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <study/game.hpp>

#include "publish_draw.hpp"

namespace fcg::publish {

inline constexpr std::size_t MAX_GAMES_PER_PAGE = 200;

struct HtmlOptions {
    bool comments   = true;
    bool variations = true;
    bool marks      = true;   // arrows and squares
    bool assistant  = false;  // the assistant's comments, marked
    bool evals      = false;  // the evaluations beside the moves, and their curve
    bool clocks     = false;
};

// A game as the page shows it: its tree, and its headers as the table of names writes the players.
struct HtmlGame {
    GameTree tree;
    bool chess960 = false;
    MoveNotes start_notes;
    std::string white, black;  // as shown (Snapshot::shownName())
    // The position the page opens at: a line of the tree (-1 the game) and its ply.
    int line = -1, ply = 0;
};

// What the page says, in the interface's language, and its look.
struct HtmlLook {
    std::string lang = "en";
    appearance::BoardColours colours;
    std::shared_ptr<const PieceShapes> pieces;
    std::string pieces_notice;   // "Pieces: Cburnett, by Colin M.L. Burnett, BSD 3-clause license."
    std::string pieces_license;  // its whole text, folded at the page's end
};

// The headers line of a game: "Autumn Open 2026, Lyon · round 9 · 2026.10.20 · B90"; its title "White – Black · ½-½".
[[nodiscard]] std::string gameTitle(const HtmlGame& game);
[[nodiscard]] std::string gameSubtitle(const HtmlGame& game);
// The result as the page writes it: "1-0", "½-½", "0-1", "*".
[[nodiscard]] std::string resultText(const std::string& result);

// The page of `games` (one or several, chosen in a list at its top).
[[nodiscard]] std::string gamesHtml(const std::vector<HtmlGame>& games, const HtmlOptions& options,
                                    const HtmlLook& look, const std::string& title);
// The list of a folder of pages, one a game: each line a link to its page, with the game's title and headers.
struct IndexedPage {
    std::string file, title, subtitle;
};
[[nodiscard]] std::string gamesIndexHtml(const std::vector<IndexedPage>& pages, const HtmlLook& look,
                                         const std::string& title);
// A page per game in `folder` (made when missing), "0001-moreau-claire-lindqvist-erik.html"…, and their list in
// index.html: `count` games, each read when its page is written (none: left out), so that tens of thousands never are
// in memory at once. The error, empty when written; `cancel` stops between two pages, `done` counts them.
[[nodiscard]] std::string writeGamePages(const std::filesystem::path& folder, std::size_t count,
                                         const std::function<std::optional<HtmlGame>(std::size_t)>& game,
                                         const HtmlOptions& options, const HtmlLook& look, const std::string& title,
                                         const std::atomic<bool>* cancel = nullptr, std::atomic<int>* done = nullptr);

// Pieces of a page shared with the reports' pages (publish_doc.hpp): the styles, the board's script and pieces, a
// text made safe for HTML, a game's moves as the page writes them (`id` its number in the page's games).
[[nodiscard]] std::string htmlText(const std::string& text);
[[nodiscard]] std::string pageStart(const std::string& title, const std::string& lang, const std::string& styles);
[[nodiscard]] std::string boardStyles(const appearance::BoardColours& colours);
[[nodiscard]] std::string pieceSymbols(const PieceShapes& pieces);
[[nodiscard]] std::string boardScript();
[[nodiscard]] std::string gameData(const HtmlGame& game, const HtmlOptions& options, int id);
[[nodiscard]] std::string movetextHtml(const HtmlGame& game, const HtmlOptions& options, int id);
[[nodiscard]] std::string noticeHtml(const HtmlLook& look);
// The board's widget: the board, its buttons, the curve's place.
[[nodiscard]] std::string boardWidget(bool curve);

// A rough weight of the page for `games`, before it is made: the page's own part, then each game's.
[[nodiscard]] uint64_t estimateHtml(const std::vector<HtmlGame>& sample, std::size_t games, const HtmlOptions& options,
                                    const HtmlLook& look);

}  // namespace fcg::publish
