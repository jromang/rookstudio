#pragma once

// "Check the endgames against the tables" (#94 with #42, mockup M13, board 7, point 20), from a finished tournament's
// menu in the Tests view: the games that reached seven pieces or fewer, each set against what the tablebases say, from
// a few positions a game: the first one the tables at hand can tell and the last, and between them, when the verdict
// changed, a bisection to the move that changed it. What it finds:
//   - a win let slip: an engine's move after which the tables no longer give it the win (a draw, or the other side's
//     win);
//   - a draw let slip: an engine's move after which the tables give the other side the win;
//   - a lost position held: a game drawn by the rules (a threefold repetition, the fifty-move rule) while the tables
//     still give one side the win at its end, no move having changed their verdict;
//   - a draw adjudicated in a won position: a draw by adjudication (the draw rule, the move limit) where the tables
//     give one side the win;
//   - a win adjudicated against the tables: a resignation adjudicated where the tables do not give that side the win.
// A game ended otherwise (time, an illegal move, an engine that stopped answering, a result set by hand) is judged on
// its moves only.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <chess.hpp>

namespace fcg {

// How a game ended, as the check reads it: by the rules of chess (mate, stalemate, a repetition, the fifty-move rule,
// insufficient material), by an adjudication of the tournament's rules (a draw, a resignation, the move limit), by the
// tables' own adjudication, or otherwise.
enum class GameEnd { Rules, Adjudicated, TablesAdjudicated, Other };

// fastchess' termination (fastchess::MatchTermination, as an integer: 0 normal, 1 adjudication) and reason as a
// GameEnd; `result_set` a result the operator set by hand.
[[nodiscard]] GameEnd gameEndOf(int termination, const std::string& reason, bool result_set);

// A finished game, as the check takes it.
struct CheckedGame {
    uint64_t number = 0;       // in the tournament, from 1
    std::string white, black;  // the engines
    std::string start_fen;     // empty: the usual start
    bool chess960 = false;
    std::vector<chess::Move> moves;
    int result  = 0;  // White's: +1, 0, -1
    GameEnd end = GameEnd::Rules;
    std::string reason;  // as the tournament wrote it: "Draw by 3-fold repetition"
};

// What the tables say of a position, White's point of view: +1 White wins, 0 a draw (a win the fifty-move rule spoils
// included), -1 Black wins; none when they cannot tell it.
using TablesSays = std::optional<int>;

// What the check found, about one engine of one game.
struct EndgameFinding {
    enum class Kind { WinLetSlip, DrawLetSlip, LostPositionHeld, DrawAdjudicatedWon, WinAdjudicatedNotWon };
    Kind kind        = Kind::WinLetSlip;
    std::size_t game = 0;  // index into the games checked
    // The position it is about, as the moves played to reach it: after the move that let the win or the draw slip, the
    // game's last otherwise.
    std::size_t ply = 0;
    // The engine it is about: the one that let the win or the draw slip, the one that held the lost position, the one
    // the tables give the win (a draw adjudicated), the one the win was adjudicated to.
    chess::Color side = chess::Color::WHITE;
    // The tables' verdicts, White's point of view: before and after the move that let it slip; at the game's end and
    // the game's result for the others.
    int before = 0, after = 0;
    std::string move;  // the move that let it slip, numbered, in English SAN ("54.Kd3", "61…Rb2"); empty for the others
};

// One game's check, a position at a time: next() names the position whose verdict it needs, give() hands it over.
class EndgameGameCheck {
   public:
    // `max_pieces`: the most pieces of the tables at hand (seven with Lichess's).
    explicit EndgameGameCheck(const CheckedGame& game, int max_pieces = 7);

    // Whether the game reached a position the tables can tell: `max_pieces` or fewer, no castling right left.
    [[nodiscard]] bool reached() const { return first_.has_value(); }
    // The position whose verdict it needs next, as the moves played to reach it; none once it is done.
    [[nodiscard]] std::optional<std::size_t> next() const;
    // That position's FEN (the board's own counters), and its pieces.
    [[nodiscard]] const std::string& fen(std::size_t ply) const;
    [[nodiscard]] int pieces(std::size_t ply) const;
    // What the tables say of the position next() named. Of the first one, nothing: the next position with fewer
    // pieces is asked instead (the tables at hand may cover it); of another, nothing leaves the game unjudged.
    void give(std::size_t ply, TablesSays says);

    [[nodiscard]] bool done() const { return !next().has_value(); }
    // Done with the tables' verdicts of its first and last positions.
    [[nodiscard]] bool judged() const { return judged_; }
    // What it found, `index` the game's in the games checked.
    [[nodiscard]] std::vector<EndgameFinding> findings(std::size_t index) const;
    // The positions it asked for.
    [[nodiscard]] int asked() const { return asked_; }

   private:
    enum class Phase { First, Last, Between, Done };
    void decide();  // once the first and last verdicts are known

    const CheckedGame* game_ = nullptr;
    std::size_t start_       = 0;  // the ply of fens_[0]
    std::vector<std::string> fens_;
    std::vector<int> pieces_;
    std::optional<std::size_t> first_;
    std::size_t last_ = 0;
    std::optional<int> end_verdict_;  // the last position's by the rules alone: a mate, a stalemate, no mating material
    Phase phase_ = Phase::Done;
    std::map<std::size_t, int> says_;
    std::size_t low_ = 0, high_ = 0;  // the bisection: the verdict at low_ is the first's, not at high_
    bool judged_ = false, between_known_ = false;
    int asked_ = 0;
};

// A tournament's games as the check takes them, with their checks, read off the window's thread: each check points to
// its game, the vectors are moved whole, never copied.
struct EndgameCheckGames {
    std::vector<CheckedGame> games;
    std::vector<std::unique_ptr<EndgameGameCheck>> checks;
    std::string error;
};

// What a tournament's check found.
struct EndgameCheckSummary {
    std::size_t games   = 0;  // the games looked at
    std::size_t reached = 0;  // those that reached a position the tables can tell
    std::size_t judged  = 0;  // those the tables told
    std::vector<EndgameFinding> findings;
    int asked = 0;  // the positions asked for, all games
};

// The check of `games` at once, `tables` answering each position (the tests; a check whose answers are all at hand).
[[nodiscard]] EndgameCheckSummary checkEndgames(const std::vector<CheckedGame>& games,
                                                const std::function<TablesSays(const std::string& fen)>& tables,
                                                int max_pieces = 7);

// Where the answer for a position comes from, the local tables first: the disk's when they cover it and no tournament
// of this process holds them; the answer the profile kept; Lichess's, once allowed (always, or for this check); the
// consent asked while the user has not decided; none when the tables cannot tell it (refused, or beyond them).
enum class CheckSource { Disk, Kept, Lichess, Consent, None };
[[nodiscard]] CheckSource checkSource(int pieces, int disk_pieces, bool disk_locked, bool kept, int lichess_setting,
                                      bool allowed);

// The words, in the interface's language. The summary: "12 games reached seven pieces or fewer: 3 wins let slip
// (Alpha: 2, Bravo: 1), 1 lost position held, 2 draws adjudicated in a won position."
[[nodiscard]] std::string endgameCheckSentence(const EndgameCheckSummary& summary,
                                               const std::vector<CheckedGame>& games);
// A finding: "Alpha let the win slip with 54.Kd3: the tables then give a draw."
[[nodiscard]] std::string endgameFindingWords(const EndgameFinding& finding, const CheckedGame& game);

}  // namespace fcg
