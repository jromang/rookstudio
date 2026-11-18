#pragma once

// The state of the Board page (view_board.cpp) and of the engine console of the Engines page
// (view_engine_console.cpp): what they show, apart from the App that draws them.

#include <array>
#include <atomic>
#include <filesystem>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <chess.hpp>
#include <json.hpp>
#include <types/score.hpp>

#include <study/game_tree.hpp>
#include <tour/pgn_index.hpp>

#include "analysis.hpp"
#include "board_game.hpp"
#include "engine_client.hpp"
#include "game_analysis.hpp"
#include "game_graph.hpp"
#include "pgn_store.hpp"
#include "review_runner.hpp"

namespace fcg {

// The console of an engine of the library: the lines exchanged with it, typed by the user.
struct EngineConsole {
    std::string engine;  // the library engine, by name
    bool raw = false;    // a WinBoard engine spoken to in its own protocol, without the adapter
    EngineProcess process;
    std::vector<EngineProcess::Line> log;
    std::string input;
    std::vector<std::string> history;
    int history_pos = -1;  // browsing the history with Up and Down; -1: the line being typed
    bool follow     = true;
    bool focus      = true;  // the input line takes the keyboard on the next frame
    bool shown      = true;  // in the engine's card, instead of its settings
    // UCI: the library's option values are sent once the engine answers uciok.
    std::vector<std::pair<std::string, std::string>> options;
    bool options_due = false;
};

// The quick commands of the console, as typed: UCI's, or WinBoard's when spoken to in its own protocol.
[[nodiscard]] const std::vector<const char*>& consoleCommands(bool winboard);

// What the Board page keeps between two launches (board.json in the profile): the analysis's own settings.
struct BoardSettings {
    AnalysisSettings analysis;
    bool arrows = true;  // the first move of each line drawn on the board
    // The analysis of a whole game, refined pass after pass up to this time per position; 0: until stopped.
    int64_t review_ceiling_ms = 5120;
    // A game played from the board: the two engines, their clocks, and a second game with the colours reversed.
    std::string game_first, game_second;
    GameClock first_clock, second_clock;
    bool same_clock = true;
    bool game_pair  = false;
    int game_human  = -1;  // the user plays the first side (0) or the second (1), in place of its engine
    GraphView graph;       // the game graph's tab, hidden curves, folded
    // In the Chess space, a move where the game went another way: asked about (a variation, a new main line, or the
    // moves after replaced) as Ctrl does, rather than made a variation at once.
    bool ask_new_move = false;
    std::string save_database;  // the folder of the database a game from elsewhere was last saved into
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(BoardSettings, analysis, arrows, review_ceiling_ms, game_first,
                                                game_second, first_clock, second_clock, same_clock, game_pair,
                                                game_human, graph, ask_new_move, save_database)

// The same, for the analysis engine: the start position and the moves to it.
[[nodiscard]] AnalysisPosition analysisPosition(const ReplayGame& game, bool chess960, int ply);

// A score from the side to move's point of view, from White's.
[[nodiscard]] fastchess::Score whiteScore(const fastchess::Score& score, chess::Color side_to_move);

// The Syzygy tablebases' result of each position of `game` (the side to move's: 1 win, 0 draw, -1 loss, -2 not
// known), with the tables of the folders `paths` (fastchess' probing code, whose tables are the process's: loaded
// again when the folders changed or a tournament let them go). Each position is probed as if a capture or a pawn
// move had just been played: exact but where the 50-move rule is near.
[[nodiscard]] std::vector<int8_t> tablebaseResults(const ReplayGame& game, bool chess960, const std::string& paths);

// A tablebase result (`result` of the side `stm`: 1 win, 0 draw, -1 loss) as a score: "1-0", "½-½", "0-1".
[[nodiscard]] const char* tablebaseScore(int result, chess::Color stm);

// Each move's mover's clock after it: from its [%clk] when the game has them, else rebuilt from the TimeControl tag
// ("base+increment" in seconds, or "base") and the moves' times, book moves taking none; nullopt where neither tells.
[[nodiscard]] std::vector<std::optional<int64_t>> moveClocks(const ReplayGame& game);

// The legal moves of the piece on `from`, the target squares as the board shows them (castling on the king's square).
[[nodiscard]] std::vector<chess::Move> movesFrom(const chess::Board& board, chess::Square from);

// The square a move goes to on the board: castling, stored as the king taking its rook, goes to the king's square.
[[nodiscard]] chess::Square shownTarget(const chess::Move& move);

// A PGN of several games, opened or pasted: its games listed, filtered by their tags, one shown at a time.
struct PgnSource {
    std::string name;                         // the file's, or "clipboard"
    std::filesystem::path file;               // empty for a pasted text
    std::shared_ptr<const std::string> text;  // the pasted text
    std::shared_ptr<const tour::PgnIndex> index;
    std::string filter;
    std::vector<uint32_t> shown;  // the games the filter keeps
    bool filter_dirty = true;
    int selected      = -1;  // the game on the board
};

// A game's changes since it was opened or saved: + a change or a redo, − an undo. Once a change follows undos back
// past the saved state, no redo brings that state back: `lost`.
struct GameChanges {
    int count = 0;
    bool lost = false;
    std::string saved_in;  // where it was saved last ("« Mega »", a file's name), said while nothing changed
    [[nodiscard]] bool any() const noexcept { return count != 0 || lost; }
    void change() noexcept {
        if (count < 0) lost = true;
        ++count;
    }
    void undone(bool redo) noexcept { count += redo ? 1 : -1; }
    void saved(std::string where) {
        count    = 0;
        lost     = false;
        saved_in = std::move(where);
    }
    // Whether undos or redos bring the saved (or opened) state back, from histories of these sizes.
    [[nodiscard]] bool reversible(std::size_t undos, std::size_t redos) const noexcept {
        return !lost &&
               (count >= 0 ? undos >= static_cast<std::size_t>(count) : redos >= static_cast<std::size_t>(-count));
    }
};

// The text of one game of a source.
[[nodiscard]] std::optional<std::string> pgnSourceGame(const PgnSource& source, std::size_t game);

// A PGN file being read in the background.
struct PgnReading {
    std::filesystem::path file;
    uint64_t size = 0;
    std::future<tour::PgnIndex> result;
    std::shared_ptr<std::atomic<bool>> cancel;
    std::shared_ptr<std::atomic<uint64_t>> done;
};

// The position editor: a placement of pieces, any while it is edited, and the rest of a FEN.
struct PositionEditor {
    std::array<chess::Piece, 64> squares{};  // a1 = 0 ... h8 = 63
    chess::Color stm   = chess::Color::WHITE;
    bool castle[4]     = {true, true, true, true};  // K Q k q
    int ep_file        = -1;                        // the en passant square's file, -1 none
    int fullmove       = 1;
    bool chess960      = false;
    chess::Piece brush = chess::Piece(chess::PieceType::QUEEN, chess::Color::WHITE);
    bool erase         = false;
    std::string fen_text;  // as typed in its field
};

// The editor's position as a FEN: the castling rights its kings and rooks allow, the en passant square only when a
// pawn just moved two squares there.
[[nodiscard]] std::string editorFen(const PositionEditor& editor);
// The editor set to a FEN's position; false (unchanged) when the board cannot read it.
bool editorLoad(PositionEditor& editor, const std::string& fen);
// Whether the castling right (0 K, 1 Q, 2 k, 3 q) is possible: the king, and a rook on that side, on their first
// rank (where they start in Chess960; on e1 and h1 or a1 otherwise).
[[nodiscard]] bool editorCanCastle(const PositionEditor& editor, int right);

// The game and the position of the Board page, and its analysis.
// A line laid on the board without touching the game (Show, a line hovered): the position `ply` of the game, then
// the line's moves.
struct BoardPreview {
    int ply = 0;
    int at  = 0;                     // the board's ply when it was shown: moving through the game ends it
    std::vector<std::string> moves;  // UCI
    std::string label;               // what it is, over the board
    bool hover = false;              // shown while the mouse is on it: gone when it leaves
};

struct BoardPage {
    ReplayGame game;
    bool chess960 = false;
    std::string title;  // what the game is ("Alpha – Charlie · Round robin · round 12"), empty for the user's
    // The game with the variations played on the board; `game` is the line shown (the game itself for line -1).
    GameTree tree;
    int line          = -1;
    int menu_line     = -1;                   // the variation a menu is open for
    int ply      = 0;                    // moves played on the board
    int followed      = -1;                   // the ply the move list was last scrolled to
    int followed_line = -1;                   // and its line
    bool flip    = false;
    std::optional<chess::Square> selected;  // a piece picked up
    // A piece pressed with the left button, followed until the button is released: where from, whether the mouse
    // moved enough for a drag, whether it was picked up before the press (a click on it then puts it down).
    struct Drag {
        chess::Square from;
        bool moved         = false;
        bool picked_before = false;
    };
    std::optional<Drag> drag;
    std::optional<chess::Square> drag_over;  // the square under it, as the last frame saw it
    std::optional<chess::Move> promotion;   // a pawn's move waiting for the piece it becomes
    std::optional<BoardPreview> preview;    // a line shown on the board instead of the game
    bool preview_hovered = false;           // a hovered line asked for it this frame
    std::string message;                    // what the last paste or file gave, or why it gave nothing
    bool message_error = false;

    BoardSettings settings;
    bool settings_loaded = false;
    Analyser analyser;
    bool analysing = false;  // asked for: the analysis follows the board while the page is shown
    Analyser threat;         // the opponent's best move were it to move again (X), on an engine of its own
    bool threat_on = false;
    bool new_game  = true;   // another game came: the engine gets ucinewgame

    std::optional<PgnSource> pgn;          // the games of a file or a paste
    std::optional<PgnReading> reading;     // a file being read
    std::optional<PositionEditor> editor;  // the position being set up
    int game_tab = 0;                      // the game card: 0 its moves, 1 the games of the file
    int open_tab = -1;                     // a tab to select on the next frame

    // The game analysed in passes (the continuous analysis), by one engine or several, each on a thread of its own.
    struct ReviewTask {
        std::string engine;
        std::unique_ptr<ReviewRunner> runner;
        ReviewRunner::State state;  // copied when it changes
        uint64_t version = 0;
        int saved        = 0;  // its passes kept in the profile
    };
    std::vector<ReviewTask> review_tasks;
    ReplayGame review_game;  // the game they analyse
    bool review_chess960 = false;
    std::string review_game_key;  // reviewKey(review_game)
    int64_t review_offered = 0;   // the time of the board's search last offered to them
    [[nodiscard]] bool reviewing() const { return !review_tasks.empty(); }
    // The tablebases' result of each position of the game (the side to move's: 1 win, 0 draw, -1 loss; -2 not
    // known), with the Syzygy folders of the setup.
    std::vector<int8_t> tb;
    std::string tb_key;               // the game they are of, and the folders
    std::vector<GameReview> reviews;  // the analyses of the game shown, the judging one first
    std::string review_key;           // the game they are of (reviewKey)
    std::vector<MoveReview> judged;   // its moves judged by the first, for the game shown

    // Games played from the board's position between two engines.
    bool play_open   = false;  // their settings shown
    bool play_anyway = false;  // asked to play on a machine the tournament and the analysis fill
    std::unique_ptr<BoardMatch> match;
    BoardMatch::State match_state;
    uint64_t match_version = 0;
    bool watching          = false;  // the board shows the game being played
    int flipped_game       = -1;     // the game the board was turned for, the user's side at the bottom
    // The user plays the game being played, and it is their move.
    [[nodiscard]] bool humanGame() const { return match && match_state.running && match_state.you.has_value(); }
    [[nodiscard]] bool humanTurn() const { return humanGame() && match_state.thinking == match_state.you; }

    // The Chess space's notation written in place and its marks (mockup M2).
    std::optional<chess::Square> mark_from;   // 2D: the right button pressed on this square, a mark being drawn
    std::optional<chess::Square> mark_hover;  // the square under the pointer meanwhile
    MoveNotes start_notes;                    // the marks of the start position
    struct CommentEdit {
        int line = -1, ply = 0;  // the move leading to position `ply` of `line`
        bool before = false;     // the comment before the move, else after it
        std::array<char, 2048> text{};
        bool focus = true;  // the field takes the keys on its first frame
    };
    std::optional<CommentEdit> comment_edit;
    struct Snapshot {
        GameTree tree;
        MoveNotes start_notes;
        int line = -1, ply = 0;
    };
    std::vector<Snapshot> undo, redo;  // the game before each change, and the changes undone
    GameChanges changes;               // since it was opened or saved: the strip and the recent games say so
    std::string nag_typed;             // ! and ? typed in a row on a move
    double nag_time = 0;               // when the last of them was
    std::string toast;                 // a change said for a moment over the moves
    double toast_time = -100;
    // The move being typed (the Chess space): opened by its first letter, played by Enter, Escape closing it.
    std::optional<std::array<char, 16>> move_typed;
    bool move_focus = false;  // the field takes the keys on its next frame
    std::string move_error;
};

}  // namespace fcg
