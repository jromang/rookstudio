#pragma once

// What Lichess tells of a position, asked one request at a time to each of its hosts (HostGate): the cloud's
// evaluation, the tablebase's answer for seven men or fewer, and the opening explorer (which wants an account's token:
// it answers 401 to anyone else since 2026). A position goes as its FEN, and nothing else of the user's with it.

#include <atomic>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fcg::lichess {

// The servers asked: Lichess's, or a test's.
struct Servers {
    std::string api       = "https://lichess.org";
    std::string tablebase = "https://tablebase.lichess.ovh";
    std::string explorer  = "https://explorer.lichess.ovh";
};
[[nodiscard]] Servers& servers();

// What a request gave: its value, or none and why. No value and no error: the server has nothing for it (a position
// the cloud has not evaluated).
template <typename T>
struct Answer {
    std::optional<T> value;
    std::string error;
    int status = 0;  // HTTP's, 0 without an answer
};

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
    std::optional<int> dtz, dtm;
    bool zeroing   = false;  // a capture or a pawn move
    bool checkmate = false, stalemate = false;
};
struct Tablebase {
    Outcome outcome = Outcome::Unknown;
    std::optional<int> dtz, dtm;
    bool checkmate = false, stalemate = false, insufficient_material = false;
    std::vector<TablebaseMove> moves;  // the best first, as Lichess gives them
};
[[nodiscard]] Answer<Tablebase> tablebase(std::string_view fen, const std::atomic<bool>* cancel = nullptr);

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
struct Explorer {
    uint64_t white = 0, draws = 0, black = 0;
    std::vector<ExplorerMove> moves;
    std::vector<ExplorerGame> top_games, recent_games;
    std::string eco, opening;  // the opening's code and name, when known
};
[[nodiscard]] Answer<Explorer> explorer(const ExplorerQuery& query, const std::atomic<bool>* cancel = nullptr);

// A value as a URL's query holds it (RFC 3986's unreserved characters as they are, the others %XX).
[[nodiscard]] std::string urlEncode(std::string_view value);

}  // namespace fcg::lichess
