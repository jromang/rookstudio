#pragma once

// What Lichess tells of a position, asked one request at a time to each of its hosts (HostGate): the cloud's
// evaluation, the tablebase's answer for seven men or fewer, and the opening explorer (which wants an account's token:
// it answers 401 to anyone else since 2026). A position goes as its FEN, and nothing else of the user's with it.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "lichess_http.hpp"  // Answer<T>, NetError

namespace fcg::lichess {

// The servers asked: Lichess's, or a test's.
struct Servers {
    std::string api       = "https://lichess.org";
    std::string tablebase = "https://tablebase.lichess.ovh";
    std::string explorer  = "https://explorer.lichess.ovh";
    std::string engine    = "https://engine.lichess.ovh";  // the external engines' work (#96)
};
[[nodiscard]] Servers& servers();
// The host of servers().api, as HostGate names it; how long it still pauses after a 429 (none, most of the time).
[[nodiscard]] std::string apiHost();
[[nodiscard]] std::chrono::milliseconds apiPause();

// The cloud: lines of moves (UCI) with their evaluation for White, in centipawns or as a mate in so many moves
// (negative: Black mates).
struct CloudLine {
    std::vector<std::string> moves;
    std::optional<int> cp;
    std::optional<int> mate;
};
struct CloudEval {
    int depth       = 0;
    uint64_t knodes = 0;
    std::vector<CloudLine> lines;
};
[[nodiscard]] Answer<CloudEval> cloudEval(std::string_view fen, int lines = 1,
                                          const std::atomic<bool>* cancel = nullptr);

// The tablebase: what the position is for the side to move, and each move with what it leaves for the other side.
enum class Outcome { Win, CursedWin, MaybeWin, Draw, Unknown, MaybeLoss, BlessedLoss, Loss };
struct TablebaseMove {
    std::string uci, san;
    Outcome outcome = Outcome::Unknown;  // for the side to move after it
    std::optional<int> dtz, dtm;         // in plies
    std::optional<int> dtc;  // moves to the next capture, promotion or mate: 8-piece positions (op1), in moves
    bool zeroing   = false;  // a capture or a pawn move
    bool checkmate = false, stalemate = false;
};
struct Tablebase {
    Outcome outcome = Outcome::Unknown;
    std::optional<int> dtz, dtm;  // in plies: DTZ up to 7 pieces, DTM up to 6
    std::optional<int> dtc;       // in moves: the 8-piece positions with opposing pawns (op1)
    bool checkmate = false, stalemate = false, insufficient_material = false;
    std::vector<TablebaseMove> moves;  // the best first, as Lichess gives them
};
[[nodiscard]] Answer<Tablebase> tablebase(std::string_view fen, const std::atomic<bool>* cancel = nullptr);
// The tables' best line from the position, as UCI moves, up to the end (a mate, a capture into a draw): one request.
[[nodiscard]] Answer<std::vector<std::string>> tablebaseMainline(std::string_view fen,
                                                                 const std::atomic<bool>* cancel = nullptr);

// The opening explorer: masters' games, Lichess's, or a player's.
enum class ExplorerSource { Masters, Lichess, Player };
struct ExplorerQuery {
    ExplorerSource source = ExplorerSource::Masters;
    std::string fen;
    std::vector<std::string> speeds;  // Lichess and Player: "bullet", "blitz", "rapid", "classical"...; none: all
    std::vector<int> ratings;         // Lichess: the rating groups (0, 1000, 1200 ... 2500); none: all
    std::string player;               // Player: whose games
    bool player_white = true;         // Player: the side the player had
    std::string since, until;         // "YYYY-MM" (Masters: "YYYY"); empty: no bound
    int moves        = 12;            // the moves listed, at most
    int top_games    = 4;
    int recent_games = 0;  // not for Masters
    std::string token;     // an account's: the explorer answers 401 without it
    bool history = false;  // Lichess: the position's games month by month too (#56)
};
struct ExplorerMove {
    std::string uci, san;
    uint64_t white = 0, draws = 0, black = 0;
    int average_rating = 0;
    std::string game;  // the game's id when a single game played it
};
struct ExplorerGame {
    std::string id;
    char winner = '=';  // 'w', 'b', or '=' for a draw
    std::string white, black;
    int white_rating = 0, black_rating = 0;
    int year = 0;
    std::string month;  // "YYYY-MM", when told
    std::string uci;    // the move it played from the position, when told
};
// A month of a position's games on Lichess.
struct ExplorerMonth {
    std::string month;  // "YYYY-MM"
    uint64_t games = 0;
};
struct Explorer {
    uint64_t white = 0, draws = 0, black = 0;
    std::vector<ExplorerMove> moves;
    std::vector<ExplorerGame> top_games, recent_games;
    std::string eco, opening;            // the opening's code and name, when known
    std::vector<ExplorerMonth> history;  // with ExplorerQuery::history, the oldest first
};
[[nodiscard]] Answer<Explorer> explorer(const ExplorerQuery& query, const std::atomic<bool>* cancel = nullptr);
// Where a position's games go (#56): its last six whole months against the six before them, the month still under way
// left out; Unknown with fewer than a year of months or too few games.
enum class Trend { Unknown, Rising, Steady, Falling };
[[nodiscard]] Trend trendOf(const std::vector<ExplorerMonth>& history);
// A master's game the explorer names, as PGN (#56): an account's token.
[[nodiscard]] Answer<std::string> mastersGame(const std::string& id, const std::string& token,
                                              const std::atomic<bool>* cancel = nullptr);

// A value as a URL's query holds it (RFC 3986's unreserved characters as they are, the others %XX).
[[nodiscard]] std::string urlEncode(std::string_view value);

}  // namespace fcg::lichess
