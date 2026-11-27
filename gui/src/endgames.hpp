#pragma once

// Endgames against the tablebases (#82, mockup M12, board 3; docs/design/endgames.md). A catalogue of endgames to win
// or to hold, played against the tables: they answer each move with the one that resists the longest, and judge each of
// the user's at once; a move that turns a win into a draw (or a draw into a loss) is said with the moves that kept the
// result, and the position before it comes back in the day's review (review_queue.hpp) as a card of its own (FSRS, as
// the repertoires' training schedules its positions).
//
// The tables are those of the disk (Syzygy: the probing the Board page and the tournaments use, under lockSyzygy()) or
// Lichess's online (up to 7 pieces, a request a position, after the user's consent, M4). Both answer as Lichess does
// (lichess::Tablebase): the side to move's outcome, and each move with the outcome it leaves the other side, the best
// first for the side to move.
//
// Kept in the profile: endgames/record.json (each endgame's results, the positions missed with their cards). Nothing
// leaves the machine with the tables of the disk; with Lichess's, each position played goes as its FEN.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <future>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <chess.hpp>
#include <study/fsrs.hpp>

#include "lichess_api.hpp"

namespace fcg {

// ---------------------------------------------------------------------------
// The catalogue

enum class EndgameGroup { Mates, Pawns, Rooks, Pieces };
enum class EndgameGoal { Win, Draw };

struct EndgameEntry {
    const char* id     = "";  // kept in the profile: "queen", "lucena"…
    EndgameGroup group = EndgameGroup::Mates;
    const char* name   = "";  // N_(): translated where it is shown
    EndgameGoal goal   = EndgameGoal::Win;
    const char* fen    = "";     // the start, the user to move
    const char* idea   = "";     // N_(): what wins or holds, said with a fault and with the right move
    bool mate          = false;  // the goal is the mate itself (the elementary mates), counted against the tables'
};
// The endgames in the order the page shows them, group by group; each start checked against the tables
// (docs/design/endgames.md): the goal is what the tables say of it.
[[nodiscard]] const std::vector<EndgameEntry>& endgameCatalogue();
[[nodiscard]] const EndgameEntry* endgameEntry(std::string_view id);
[[nodiscard]] const char* endgameGroupName(EndgameGroup group);  // N_()
// The pieces of a position, kings included: what the tables must cover.
[[nodiscard]] int piecesOf(const chess::Board& board);
[[nodiscard]] int piecesOf(const std::string& fen);

// An endgame played: one of the catalogue, a position missed coming back, or a position of the user's games.
struct EndgameTask {
    std::string fen;
    std::string entry;  // the catalogue's id, "" for a game's position
    std::string title;  // what it is shown as
    EndgameGoal goal = EndgameGoal::Win;
    bool mate        = false;
    bool review      = false;  // a card of the day's review: graded by its end
    std::string idea;          // the catalogue's idea, in English (translated where shown)
};
[[nodiscard]] EndgameTask taskOf(const EndgameEntry& entry);

// ---------------------------------------------------------------------------
// The tables' answers

// The outcome for the side to move: +1 a win, 0 a draw (a win or a loss the fifty-move rule spoils included), -1 a
// loss; none when the tables do not know.
[[nodiscard]] std::optional<int> outcomeResult(lichess::Outcome outcome);
// A move's, for the side that plays it: the tables give the outcome it leaves the other side.
[[nodiscard]] std::optional<int> moveResult(const lichess::TablebaseMove& move);
// The outcome Lichess names, for the other side, as the result a move gives the side that plays it: +2 a win, +1 a win
// the fifty-move rule spoils, 0 a draw, -1, -2.
[[nodiscard]] int moveRank(const lichess::TablebaseMove& move);

// The tables of the disk on `board`, answering as Lichess does: the side to move's outcome and its distance to the next
// capture or pawn move (DTZ, when the DTZ tables are there), each legal move with the outcome it leaves the other side,
// the best first for the side to move (see sortTableMoves()). With lockSyzygy() held; `paths` the tables' folders, as
// loadSyzygy() takes them. None when they do not cover the position (more pieces, castling rights, a table missing).
[[nodiscard]] std::optional<lichess::Tablebase> probeDiskTables(const chess::Board& board, const std::string& paths);

// The moves of `answer` in the order the tables play them, the best for the side to move first: the best outcome; among
// wins the quickest (to mate when known, else to the next capture or pawn move), among losses the longest; then, given
// `replies` (the other side's moves that keep its outcome after each move, by UCI), the move that leaves the fewest.
void sortTableMoves(lichess::Tablebase& answer, const std::map<std::string, int>& replies = {});

// The tables' move from their answer: the best for the side to move, none when it has no move.
[[nodiscard]] std::optional<std::string> tablesMove(const lichess::Tablebase& answer);

// What the user's move did, by the tables' answer for the position before it.
struct EndgameJudgement {
    bool known = false;             // the tables knew the position and the move
    int before = 0;                 // the user's result before the move: +1 a win, 0 a draw, -1 a loss
    int after  = 0;                 // after it
    std::vector<std::string> kept;  // the moves that kept `before`, UCI, the best first
    [[nodiscard]] bool fault() const noexcept { return known && after < before; }
};
[[nodiscard]] EndgameJudgement judgeEndgameMove(const lichess::Tablebase& before, std::string_view uci);

// How the game stands after a move, by the goal.
enum class EndgameOutcome { Going, Won, Held, Drawn, Lost };
// A game is held this many of the user's moves, the draw kept, when no rule ends it before.
inline constexpr int HOLD_MOVES = 15;
// `board`: the position after the move, played from the start (its repetitions); `last` the move, `capture` whether it
// took a man; `user` the user's side; `user_result` what the tables say there for the user, when known; `held` the
// user's moves played with the draw kept. A win counts at the mate; or, unless the mate itself is the goal, when a pawn
// of the user's becomes a queen or takes the other side's last man but its king, the win kept. A draw counts when a
// rule draws the game (stalemate, too little material, the fifty moves, a threefold repetition), or after HOLD_MOVES
// moves held.
[[nodiscard]] EndgameOutcome endgameOutcome(const chess::Board& board, chess::Move last, bool capture,
                                            chess::Color user, const EndgameTask& task, std::optional<int> user_result,
                                            int held);

// The tables' best line from `board`, following their best moves through `answer` (a position's answer, by its FEN),
// up to `plies` moves, the end, or a position it does not know; UCI.
template <typename Answer>
[[nodiscard]] std::vector<std::string> tablesLine(chess::Board board, Answer&& answer, int plies) {
    std::vector<std::string> out;
    for (int i = 0; i < plies; ++i) {
        const auto a = answer(board);
        if (!a) break;
        const auto move = tablesMove(*a);
        if (!move) break;
        const auto m = chess::uci::uciToMove(board, *move);
        if (m == chess::Move::NO_MOVE) break;
        out.push_back(*move);
        board.makeMove(m);
        if (board.isGameOver().first != chess::GameResultReason::NONE) break;
    }
    return out;
}

// The moves to mate the tables give the side to move from `answer`: DTM when known, else from DTZ, which is the same
// in the elementary mates (no capture nor pawn move before the mate); none for a draw or a loss.
[[nodiscard]] std::optional<int> tablesMateMoves(const lichess::Tablebase& answer);

// ---------------------------------------------------------------------------
// What the profile keeps

// An endgame of the catalogue: its tries, those won or held, whether once without a fault nor a hint, the mates' best.
struct EndgameResult {
    int tries        = 0;
    int done         = 0;
    bool clean       = false;
    int best         = 0;      // the elementary mates: the fewest moves the user mated in, 0 before
    int tables       = 0;      // and the tables' from the start
    int64_t last     = 0;      // seconds since the epoch
    bool missed_last = false;  // the last try had a fault
    bool operator==(const EndgameResult&) const = default;
};

// A position missed: a card of the day's review, the endgame played again from it.
struct EndgameCard {
    std::string fen;    // the position before the fault, the user to move
    std::string entry;  // the catalogue's endgame, "" for a game's position
    std::string title;
    EndgameGoal goal = EndgameGoal::Win;
    bool mate        = false;
    study::fsrs::Card card;
    int64_t since                             = 0;  // when it was missed first
    bool operator==(const EndgameCard&) const = default;
};

struct EndgameRecord {
    std::map<std::string, EndgameResult> results;  // by the catalogue's id
    std::map<std::string, EndgameCard> cards;      // by position: positionKey()
    int64_t answer_ms = 0;                         // how long a card takes, on average; 0 before any
};
// A position as the cards are kept: the FEN's first four fields (pieces, side, castling, en passant).
[[nodiscard]] std::string positionKey(const std::string& fen);
[[nodiscard]] EndgameRecord readEndgameRecord(const std::filesystem::path& folder);
bool writeEndgameRecord(const std::filesystem::path& folder, const EndgameRecord& record);
// The cards due by `limit` (seconds since the epoch).
[[nodiscard]] std::size_t endgameCardsDue(const EndgameRecord& record, int64_t limit);
// The card of a position just missed: graded Again, back once the training day ends.
[[nodiscard]] EndgameCard missedEndgameCard(const EndgameTask& task, const std::string& fen, int64_t now,
                                            int64_t day_end, const study::fsrs::Scheduler& scheduler);
// The grade of a card played again: its end reached without a fault nor a hint Good, with a hint or a move taken back
// Hard, a fault Again.
[[nodiscard]] study::fsrs::Rating endgameRating(bool reached, bool fault, bool helped);

// ---------------------------------------------------------------------------
// A session on the Board page (endgames_page.hpp)

struct EndgameRun {
    // Asking: the user's move (the tables' answer for the position there, or coming); Reply: the tables' move, half a
    // second after the user's (their answer for the position coming first); Fault: the user's move changed the verdict,
    // the tables replied; Done: the task's end; End: the session's.
    enum class Phase { Asking, Reply, Fault, Done, End };
    enum class Source { Catalogue, Review, Games };
    Source source = Source::Catalogue;
    std::string title;  // "Lucena", "Endgames to review"…
    std::vector<EndgameTask> tasks;
    std::size_t current = 0;  // the task played
    Phase phase         = Phase::Asking;

    // The task played: its start, the moves played since (the user's and the tables'), from the user's side.
    chess::Color user = chess::Color::WHITE;
    std::string start_fen;
    std::vector<chess::Move> moves;
    // The tables' answer for the board's position, when it came; whose (Lichess's or the disk's).
    std::optional<lichess::Tablebase> answer;
    std::string answer_fen;
    bool online = false;
    std::map<std::string, lichess::Tablebase> answers;  // by FEN: those of this session, for a move taken back
    // A question to Lichess under way, and why the last one gave nothing (the network, a refusal): asked again then.
    std::future<lichess::Answer<lichess::Tablebase>> asking;
    std::string asking_fen;
    std::shared_ptr<std::atomic<bool>> cancel = std::make_shared<std::atomic<bool>>(false);  // the session left
    std::string waiting;
    lichess::NetError waiting_net = lichess::NetError::None;  // its kind: a 429 waits a minute
    std::chrono::steady_clock::time_point retry_at{};
    std::string missing;  // the tables a position needs and nothing gives ("" none)
    // A move of the user's played before the tables' answer came: judged when it comes.
    std::optional<chess::Move> kept;
    // The user's last move judged, and the result for them: +1 a win, 0 a draw, -1 a loss.
    std::optional<EndgameJudgement> judged;
    chess::Move judged_move{};
    int verdict       = 0;
    int start_verdict = 0;
    // The verdict at the start, then each move that changed it: the moves (SAN, numbered) and the result after them.
    struct Change {
        std::string moves;
        int verdict = 0;
    };
    std::vector<Change> changes;
    bool fault_pending = false;         // the fault is shown once the tables replied
    bool fault         = false;         // a fault in this task
    bool helped        = false;         // the right move shown, a move taken back
    bool hint          = false;         // the right move shown now
    int held           = 0;             // the user's moves with the draw kept
    std::optional<int> tables_mate;     // the elementary mates: the tables' mate from the start, in moves
    std::vector<std::string> win_line;  // "See the win": the tables' line from the position before the fault
    std::future<lichess::Answer<std::vector<std::string>>> win_line_job;
    EndgameOutcome outcome = EndgameOutcome::Going;
    std::chrono::steady_clock::time_point started, task_started, reply_at;
    int64_t next_review = 0;  // a card played again: when it comes back

    // Each task's end, in the order played.
    struct Result {
        std::size_t task       = 0;
        EndgameOutcome outcome = EndgameOutcome::Going;
        bool fault = false, helped = false;
        int moves = 0;  // the user's
    };
    std::vector<Result> results;

    // The board before the session, given back after it.
    bool aside   = false;
    bool flip    = false;
    int tab      = 0;
    double took  = 0;
    bool stopped = false;
};

}  // namespace fcg
