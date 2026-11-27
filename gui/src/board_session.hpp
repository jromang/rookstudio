#pragma once

// A training session on the Board page (#76, #80–#82, #107): the repertoires' training, a series of puzzles, the
// exercises of the user's mistakes, a game guessed, an endgame against the tables, board vision. One session at a time
// holds the Board, whatever its kind: it takes it from the program (context::SessionBoard, app_context.hpp), sets its
// positions, draws its page with the Board's own parts, and gives it back as it was at its end. App's hooks in
// fastchess-gui's Board page (view_board.cpp: a move played, the page, another game opened) ask the session that holds
// the Board (App::boardSession(), which the Training page answers: training_page.hpp) through this interface, whatever
// its kind: each kind is a module of its own and its own session (repertoire_training_page.hpp, puzzles_page.hpp,
// mistakes_page.hpp, guess_page.hpp, endgames_page.hpp, vision_page.hpp), and a kind of training of the Training
// page's (training_kind.hpp).

#include <chess.hpp>

namespace fcg {

class BoardSession {
   public:
    virtual ~BoardSession() = default;

    // A move the user played on the Board, or typed (App::boardPlayAsked()): the answer to the session's question.
    virtual void sessionMove(chess::Move move) = 0;
    // The Board page while the session holds it (App::drawBoardPage()): its board and its column are the session's.
    virtual void drawSessionPage() = 0;
    // Another game comes on the Board (App::openOnBoard()): the session is left where it is, its answers kept, the
    // game it put aside staying among the recent games.
    virtual void sessionDropped() = 0;
    // Closed, the Board given back: for another session (`back` false: the Board page stays shown), or by the day's
    // review's "Back to the Training page" (`back`: the page the session was started from).
    virtual void closeSession(bool back) = 0;
    // A question is being asked: the tree, the engines and the notes are hidden meanwhile.
    [[nodiscard]] virtual bool hidesAids() const = 0;
    // The game the Board showed before the session was put among the recent games: Ctrl+Tab's list shows it as the
    // game on the board.
    [[nodiscard]] virtual bool keptAside() const = 0;
    // The guide's section of the page (F1, the page title's `?`).
    [[nodiscard]] virtual const char* guideTopic() const = 0;
};

// What the Board was before a session took it (context::SessionBoard::takeBoard()), given back at the session's end.
struct BoardTaken {
    bool aside = false;  // its game was put among the recent games
    bool flip  = false;  // the side below
    int tab    = 0;      // the column's tab
};

}  // namespace fcg
