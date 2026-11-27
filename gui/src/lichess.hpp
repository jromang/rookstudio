#pragma once

// A tournament on a lichess broadcast (https://lichess.org/api#tag/Broadcasts): the games in progress pushed every
// few seconds, and each finished game once, with its evaluations and clocks. lichess shows them with its board,
// eval bar and chat, to anyone, for free.

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <json.hpp>

#include "broadcast.hpp"
#include "https.hpp"
#include "lichess_http.hpp"

namespace fcg {

// Where a tournament goes on lichess: lichess.json in its folder (the token is not there).
struct LichessTarget {
    std::string tour_id;                           // the broadcast tournament; empty when only a round was given
    std::string round_id;                          // the round games go to now
    std::string url;                               // its page
    int round_index = 1;                           // the rounds made here: "Part 1", "Part 2"...
    std::map<std::string, std::string> rounds;     // game key -> the round it was pushed to
    std::set<std::string> finished;                // game keys pushed with their result
    std::map<std::string, std::size_t> annotated;  // game key -> the moves pushed with their comments
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(LichessTarget, tour_id, round_id, url, round_index, rounds, finished,
                                                annotated)

// The tournament's lichess.json in `dir`, read with its types checked (#144, profile_entries.hpp): an entry of another
// type keeps its default and is said, the file as it was kept beside it; the defaults when there is none, or when it
// is cut short, which is said and kept aside too, rather than a new broadcast made in silence.
[[nodiscard]] LichessTarget readLichessTarget(const std::filesystem::path& dir);

// A lichess round holds this many games; the next ones go to a new round of the same broadcast.
constexpr std::size_t LICHESS_ROUND_GAMES = 64;

// A game as lichess gets it. The tags stay the same while it is played and once it is finished, so that lichess
// updates the same board: the encounter as Round, the engines, no date. The last moves of each side carry their clocks
// (lichess keeps nothing else of a comment in a broadcast: not the engines' evaluations, nor any text; in lila,
// StudyPgnImport keeps the clock, the move time and the arrows and circles of a comment and strips every other [%...]
// command, [%eval] among them, and RelayGame.fromStudyImport drops the text), sent once:
// lichess keeps the comments of the moves it has, and rewriting the comments of many moves at once overflows its queue
// of changes to the study.
struct LichessGame {
    std::string key;      // "round|white|black"
    std::string headers;  // the tags
    bool white_first = true;
    int first_number = 1;  // the move number of the first move
    struct Move {
        std::string san;
        std::string comment;  // "[%clk 0:00:59]", or empty
    };
    std::vector<Move> moves;
    std::string result = "*";
    bool finished      = false;

    // Its PGN, with the comments of the moves from `annotate_from` on.
    [[nodiscard]] std::string pgn(std::size_t annotate_from) const;
};
// A game in progress: the clocks after the last move of each side.
[[nodiscard]] LichessGame lichessLiveGame(const LiveGame& game, const std::string& event, int white_elo, int black_elo,
                                          bool frc);
// A finished game, from its ChessBase style PGN: its clocks, with the same tags.
[[nodiscard]] LichessGame lichessFinishedGame(std::string_view pgn, const std::string& event, uint32_t round,
                                              const std::string& white, const std::string& black, int white_elo,
                                              int black_elo, const std::string& result, bool frc);

// The round id of a lichess round URL (https://lichess.org/broadcast/<tour>/<round>/<id>) or the id itself.
[[nodiscard]] std::optional<std::string> lichessRoundId(std::string_view text);

class LichessRelay {
   public:
    struct Settings {
        std::string token;
        std::string round;    // a round's URL or id; empty: a new broadcast named after the tournament
        bool listed = false;  // listed among lichess' community broadcasts, otherwise only for those with the link
        std::string name;     // the tournament's
        std::filesystem::path dir;  // the tournament's folder, for lichess.json
        std::string base = "https://lichess.org";
        std::string description;  // a new broadcast's, before "Engine games played with RookStudio."
    };
    // What failed last: the broadcast or a round made, the games pushed.
    enum class Step { None, Broadcast, Round, Push };
    struct Status {
        bool running = false;
        std::string url;
        Step failed = Step::None;  // None while all goes well
        lichess::Failure error;    // why: no answer, Lichess's refusal, or the program's own words
        std::string last_push;     // "20:15:03"
        std::size_t games = 0;     // pushed so far
    };

    LichessRelay() = default;
    // Gives the last push a few seconds, then leaves it to end on its own.
    ~LichessRelay();
    LichessRelay(const LichessRelay&)            = delete;
    LichessRelay& operator=(const LichessRelay&) = delete;

    // Starts on a thread of its own, which first waits for the last push of the previous start to end.
    void start(Settings settings);
    // Pushes what was offered last, then stops, on the relay's thread: returns at once, the push may take seconds.
    void stop();
    // Waits up to `timeout` for the thread to end, after a stop; false when it did not.
    bool wait(std::chrono::milliseconds timeout = std::chrono::minutes(2));
    [[nodiscard]] bool running() const noexcept;
    [[nodiscard]] Status status() const;

    // The games to push: those in progress and those finished since the last offer.
    void offer(std::vector<LichessGame> live, std::vector<LichessGame> finished);

   private:
    // What the thread works with, shared with it: a stopped relay's last push can outlive the relay.
    struct Session;
    std::shared_ptr<Session> session_;
    std::thread thread_;
};

// What failed last, translated ("The games could not be pushed to Lichess: …"); empty while all goes well.
[[nodiscard]] std::string relayError(const LichessRelay::Status& status);

}  // namespace fcg
