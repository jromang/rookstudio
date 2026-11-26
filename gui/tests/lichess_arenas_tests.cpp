// Lichess's arenas and Swiss tournaments (#95): the list and its filters, an arena's standing and its players' sheets
// (a streak's doubled points), the conditions as Lichess judges them with the user's figures, the scopes a link asks,
// joining again each minute while the user waits (time made testable) and the lock between two games until a pause,
// berserk refused after the first move, a blitz arena followed and not joined, a Swiss's teams, rounds and notice, a
// game lost at its start, the standing's minute after a 429 while a game's stream goes on, a game's stream cut and
// opened again, a game kept in "Lichess · account", and a finished Swiss kept with its TRF so that #92's crosstable
// reads it. The answers are shaped as lichess.org's were on 2026-11-24, every name in them invented.
#include <doctest/doctest.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <fmt/include/fmt/format.h>
#include <json.hpp>
#include <study/crosstable.hpp>
#include <study/import.hpp>
#include <study/store.hpp>

#include "answering_server.hpp"
#include "free_ports.hpp"
#include "https.hpp"
#include "lichess_account.hpp"
#include "lichess_arenas.hpp"
#include "lichess_board.hpp"
#include "lichess_playing.hpp"
#include "scripted_server.hpp"

using namespace fcg;
using namespace fcg::lichess;
using namespace fcg::test;
using namespace std::chrono_literals;
namespace fs = std::filesystem;

namespace {

std::string reply(int status, const std::string& body, const char* type = "application/json") {
    return fmt::format("HTTP/1.1 {} X\r\nContent-Type: {}\r\nContent-Length: {}\r\nConnection: close\r\n\r\n{}", status,
                       type, body.size(), body);
}

const char* const STREAM_HEAD =
    "HTTP/1.1 200 OK\r\nContent-Type: application/x-ndjson\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n";
std::string chunk(const std::string& data) { return fmt::format("{:x}\r\n{}\r\n", data.size(), data); }
const char* const LAST_CHUNK = "0\r\n\r\n";
// A JSON written as a line of NDJSON, in a chunk of its own.
std::string line(const char* json) { return chunk(nlohmann::json::parse(json).dump() + "\n"); }

struct Seen {
    std::string line, path, authorization, body;
};

Seen readRequest(const std::string& head, net::TcpConnection& c) {
    Seen s;
    s.line            = head.substr(0, head.find("\r\n"));
    s.path            = requestPath(head);
    s.authorization   = httpHeader(head, "Authorization").value_or("");
    const auto length = std::stoul(httpHeader(head, "Content-Length").value_or("0"));
    s.body            = head.substr(head.find("\r\n\r\n") + 4);
    while (s.body.size() < length) {
        const auto more = c.readUntil("\x01never", length - s.body.size(), 2000);
        if (more.empty()) break;
        s.body += more;
    }
    return s;
}

// /api/tournament as lichess.org answered it: created, started, finished; variants among them.
const char* const LIST = R"({
 "created": [
  {"id": "cla55ic1", "createdBy": "lichess", "system": "arena", "minutes": 120, "clock": {"limit": 1800, "increment": 0},
   "rated": true, "fullName": "Daily Classical Arena", "nbPlayers": 388,
   "variant": {"key": "standard", "short": "Std", "name": "Standard"}, "startsAt": 1795515000000,
   "finishesAt": 1795522200000, "status": 10, "perf": {"key": "classical", "name": "Classical", "position": 3},
   "secondsToStart": 7800, "schedule": {"freq": "daily", "speed": "classical"}},
  {"id": "rap1d180", "createdBy": "lichess", "system": "arena", "minutes": 57, "clock": {"limit": 600, "increment": 5},
   "rated": true, "fullName": "<1800 Rapid Arena", "nbPlayers": 512,
   "variant": {"key": "standard", "short": "Std", "name": "Standard"}, "startsAt": 1795510000000,
   "finishesAt": 1795513420000, "status": 10, "perf": {"key": "rapid", "name": "Rapid", "position": 2},
   "secondsToStart": 2400, "hasMaxRating": true, "maxRating": {"rating": 1800}, "minRatedGames": {"nb": 20},
   "schedule": {"freq": "hourly", "speed": "rapid"}},
  {"id": "atom1c00", "createdBy": "lichess", "system": "arena", "minutes": 57, "clock": {"limit": 180, "increment": 2},
   "rated": true, "fullName": "Hourly Atomic Arena", "nbPlayers": 0,
   "variant": {"key": "atomic", "short": "Atom", "name": "Atomic"}, "startsAt": 1795510100000,
   "finishesAt": 1795513520000, "status": 10, "perf": {"key": "atomic", "name": "Atomic"}, "secondsToStart": 2500,
   "schedule": {"freq": "hourly", "speed": "blitz"}}
 ],
 "started": [
  {"id": "bl1tz320", "createdBy": "lichess", "system": "arena", "minutes": 57, "clock": {"limit": 180, "increment": 2},
   "rated": true, "fullName": "Hourly Blitz Arena", "nbPlayers": 4512,
   "variant": {"key": "standard", "short": "Std", "name": "Standard"}, "startsAt": 1795506000000,
   "finishesAt": 1795509420000, "status": 20, "perf": {"key": "blitz", "name": "Blitz", "position": 1},
   "minRatedGames": {"nb": 15}, "schedule": {"freq": "hourly", "speed": "blitz"}},
  {"id": "rap1d100", "createdBy": "lichess", "system": "arena", "minutes": 117, "clock": {"limit": 600, "increment": 0},
   "rated": true, "fullName": "Hourly Rapid Arena", "nbPlayers": 1204,
   "variant": {"key": "standard", "short": "Std", "name": "Standard"}, "startsAt": 1795504000000,
   "finishesAt": 1795511020000, "status": 20, "perf": {"key": "rapid", "name": "Rapid", "position": 2},
   "minRatedGames": {"nb": 10}, "schedule": {"freq": "hourly", "speed": "rapid"}},
  {"id": "bu11et10", "createdBy": "lichess", "system": "arena", "minutes": 27, "clock": {"limit": 60, "increment": 0},
   "rated": true, "fullName": "Hourly Bullet Arena", "nbPlayers": 3870,
   "variant": {"key": "standard", "short": "Std", "name": "Standard"}, "startsAt": 1795507000000,
   "finishesAt": 1795508620000, "status": 20, "perf": {"key": "bullet", "name": "Bullet", "position": 0},
   "schedule": {"freq": "hourly", "speed": "bullet"}}
 ],
 "finished": [
  {"id": "d0ne0001", "createdBy": "lichess", "system": "arena", "minutes": 57, "clock": {"limit": 600, "increment": 0},
   "rated": true, "fullName": "<1300 Rapid Arena", "nbPlayers": 66,
   "variant": {"key": "standard", "short": "Std", "name": "Standard"}, "startsAt": 1795500000000,
   "finishesAt": 1795503420000, "status": 30, "perf": {"key": "rapid", "name": "Rapid", "position": 2},
   "winner": {"name": "Player_W", "id": "player_w"}}
 ]
})";

// /api/tournament/{id}, in progress, as lichess.org answered it to anyone (its verdicts anyone's).
const char* const ARENA = R"({
 "nbPlayers": 1204,
 "duels": [{"id": "duel0001", "p": [{"n": "Player_X", "r": 2171, "k": 201}, {"n": "Player_Y", "r": 2114, "k": 191}]},
           {"id": "duel0002", "p": [{"n": "Player_I", "r": 1775, "k": 9}, {"n": "Player_C", "r": 2202, "k": 3}]}],
 "secondsToFinish": 1860, "isStarted": true,
 "featured": {"id": "feat0001", "fen": "r2qkb1r/1pp3pp/p1p1bpn1/4P3/3P4/5N2/PP3PPP/RNBQ1RK1 b", "orientation": "black",
              "color": "black", "lastMove": "e4e5",
              "white": {"name": "Player_E", "id": "player_e", "rank": 5, "rating": 1789, "berserk": true},
              "black": {"name": "Player_Z", "id": "player_z", "rank": 26, "rating": 1851},
              "c": {"white": 286, "black": 588}},
 "standing": {"page": 1, "players": [
  {"name": "Player_A", "rank": 1, "rating": 1834, "score": 21, "sheet": {"scores": "55533", "fire": true},
   "withdraw": true},
  {"name": "Player_B", "rank": 2, "rating": 1793, "score": 20, "sheet": {"scores": "444422", "fire": true}},
  {"name": "Player_C", "title": "FM", "rank": 3, "rating": 2202, "score": 20, "sheet": {"scores": "455330",
   "fire": true}},
  {"name": "Player_D", "rank": 4, "rating": 1934, "score": 19, "sheet": {"scores": "045532"}},
  {"name": "Player_E", "rank": 5, "rating": 1789, "score": 19, "sheet": {"scores": "5533003", "fire": true}},
  {"name": "Player_F", "rank": 6, "rating": 2050, "score": 14, "sheet": {"scores": "5522", "fire": true}},
  {"name": "Player_G", "rank": 7, "rating": 1684, "score": 14, "sheet": {"scores": "54230", "fire": true}},
  {"name": "Player_H", "rank": 8, "rating": 2175, "score": 13, "sheet": {"scores": "044221"}},
  {"name": "Player_I", "rank": 9, "rating": 1775, "score": 13, "sheet": {"scores": "1302002032"}},
  {"name": "Player_J", "rank": 10, "rating": 1684, "score": 13, "sheet": {"scores": "2222221"}}]},
 "id": "rap1d100", "createdBy": "lichess", "startsAt": "2026-11-24T08:00:25Z", "system": "arena",
 "fullName": "Hourly Rapid Arena", "minutes": 117, "perf": {"key": "rapid", "name": "Rapid", "icon": "#"},
 "clock": {"limit": 600, "increment": 0}, "variant": "standard", "rated": true, "berserkable": true,
 "verdicts": {"list": [{"condition": "≥ 10 rated Rapid games", "verdict": "ok"}], "accepted": true},
 "schedule": {"freq": "hourly", "speed": "rapid"}, "minRatedGames": {"nb": 10}
})";

// The same arena asked with the user's token: their verdicts, their rank, the game they play.
std::string arenaWithToken(bool refused, bool paused) {
    auto j         = nlohmann::json::parse(ARENA);
    j["maxRating"] = {{"rating", 1800}};
    j["verdicts"]  = {{"list",
                       {{{"condition", "≥ 10 rated Rapid games"}, {"verdict", "ok"}},
                        {{"condition", "Rated ≤ 1800 in Rapid for the last week"},
                         {"verdict", refused ? "Your top weekly Rapid rating (1812) is too high" : "ok"}}}},
                      {"accepted", !refused}};
    j["me"]        = {{"rank", 214}, {"withdraw", paused}, {"gameId", paused ? "" : "mine0001"}, {"pauseDelay", 0}};
    j["standing"]["page"] = 22;
    return j.dump();
}

// A profile of the user's: rapid 1812 after 312 rated games, classical 1905.
Profile sampleProfile() {
    Profile p;
    p.username = "RS_Sample";
    for (const auto& [key, rating, games] :
         {std::tuple{"bullet", 1500, 0}, std::tuple{"blitz", 1795, 310}, std::tuple{"rapid", 1812, 312},
          std::tuple{"classical", 1905, 95}, std::tuple{"correspondence", 1760, 40}}) {
        SpeedRating s;
        s.key    = key;
        s.rating = rating;
        s.games  = games;
        p.speeds.push_back(s);
    }
    return p;
}

}  // namespace

TEST_CASE("Lichess arenas: the list read, in progress then to come, its filters, and what each line offers") {
    const auto list = readArenaList(nlohmann::json::parse(LIST));
    // Standard chess only, nothing finished; in progress the soonest to end first, then to come the soonest first.
    REQUIRE(list.size() == 5);
    CHECK(list[0].id == "bu11et10");
    CHECK(list[1].id == "bl1tz320");
    CHECK(list[2].id == "rap1d100");
    CHECK(list[3].id == "rap1d180");
    CHECK(list[4].id == "cla55ic1");
    const auto& rapid = list[2];
    CHECK(rapid.name == "Hourly Rapid Arena");
    CHECK(rapid.speed == "rapid");
    CHECK(rapid.limit_s == 600);
    CHECK(rapid.increment_s == 0);
    CHECK(rapid.players == 1204);
    CHECK(rapid.state == TourState::Started);
    CHECK(rapid.finishes_at == 1795511020000);
    CHECK(rapid.freq == "hourly");
    REQUIRE(rapid.conditions.size() == 1);
    CHECK(rapid.conditions[0].kind == TourCondition::Kind::RatedGames);
    CHECK(rapid.conditions[0].value == 10);
    CHECK_FALSE(rapid.conditions[0].ok);  // the list judges nothing
    const auto& under = list[3];
    REQUIRE(under.conditions.size() == 2);
    CHECK(under.conditions[0].kind == TourCondition::Kind::RatedGames);
    CHECK(under.conditions[1].kind == TourCondition::Kind::MaxRating);
    CHECK(under.conditions[1].value == 1800);
    CHECK(under.state == TourState::Created);

    // The filters: rapid, classical, all.
    const auto count = [&](ArenaSpeeds s) {
        return std::count_if(list.begin(), list.end(), [&](const auto& a) { return arenaShown(a, s); });
    };
    CHECK(count(ArenaSpeeds::Rapid) == 2);
    CHECK(count(ArenaSpeeds::Classical) == 1);
    CHECK(count(ArenaSpeeds::All) == 5);

    // What each line offers: the Board API plays rapid and classical; blitz and bullet are followed, played on
    // lichess.org; a condition the user's figures miss said before joining.
    const auto profile = sampleProfile();
    CHECK(playableHere(rapid));
    CHECK_FALSE(playableHere(list[0]));  // bullet
    CHECK_FALSE(playableHere(list[1]));  // blitz
    CHECK(joinOffer(list[1], &profile) == JoinOffer::WebOnly);
    CHECK(joinOffer(list[0], &profile) == JoinOffer::WebOnly);
    CHECK(joinOffer(rapid, &profile) == JoinOffer::Join);
    CHECK(joinOffer(under, &profile) == JoinOffer::ConditionNotMet);  // 1812 > 1800
    CHECK(joinOffer(list[4], &profile) == JoinOffer::Join);
    CHECK(joinOffer(under, nullptr) == JoinOffer::Join);  // not known: Lichess judges it on joining
    auto done  = rapid;
    done.state = TourState::Finished;
    CHECK(joinOffer(done, &profile) == JoinOffer::Finished);
}

TEST_CASE("Lichess arenas: an arena's standing, its players' sheets and streaks, its games, the user in it") {
    const auto a = readArena(nlohmann::json::parse(ARENA));
    CHECK(a.info.id == "rap1d100");
    CHECK(a.info.name == "Hourly Rapid Arena");
    CHECK(a.info.state == TourState::Started);
    CHECK(a.info.berserkable);
    CHECK(a.info.starts_at == 1795507225000);  // 2026-11-24T08:00:25Z
    CHECK(a.info.finishes_at == a.info.starts_at + 117 * 60000);
    CHECK(a.seconds_to_finish == 1860);
    CHECK(a.streaks);
    REQUIRE(a.standing.size() == 10);
    CHECK(a.standing[0].name == "Player_A");
    CHECK(a.standing[0].paused);
    CHECK(a.standing[0].fire);
    CHECK(a.standing[2].title == "FM");
    CHECK(a.standing[3].score == 19);
    REQUIRE(a.duels.size() == 2);
    CHECK(a.duels[1].id == "duel0002");
    CHECK(a.duels[1].b == "Player_C");
    CHECK(a.duels[1].b_rank == 3);
    CHECK(a.featured == "feat0001");
    CHECK_FALSE(a.me);
    CHECK(standingPage(1) == 1);
    CHECK(standingPage(10) == 1);
    CHECK(standingPage(11) == 2);
    CHECK(standingPage(214) == 22);
    REQUIRE(standingOf(a, "player_e"));
    CHECK(standingOf(a, "player_e")->rank == 5);
    CHECK_FALSE(standingOf(a, "Nobody_Q"));

    // The sheets, the oldest game first: two wins in a row start a streak, the next wins count 4 (5 berserk), a draw
    // 2, until a game is not won; each sums to the player's score.
    using S                 = SheetScore;
    const auto sheet_of_two = readSheet("444422");
    CHECK(sheet_of_two == std::vector<S>{{2, false, false},
                                         {2, false, false},
                                         {4, true, false},
                                         {4, true, false},
                                         {4, true, false},
                                         {4, true, false}});
    CHECK(readSheet("5533003") == std::vector<S>{{3, false, true},
                                                 {0, false, false},
                                                 {0, false, false},
                                                 {3, false, true},
                                                 {3, false, true},
                                                 {5, true, true},
                                                 {5, true, true}});
    // A draw within a streak counts double and ends it; a loss there counts nothing.
    CHECK(readSheet("20") == std::vector<S>{{0, false, false}, {2, false, false}});
    CHECK(readSheet("2422") ==
          std::vector<S>{{2, false, false}, {2, false, false}, {4, true, false}, {2, true, false}});
    CHECK(readSheet("0422") ==
          std::vector<S>{{2, false, false}, {2, false, false}, {4, true, false}, {0, false, false}});
    for (const auto& p : a.standing) {
        CAPTURE(p.name);
        int sum = 0;
        for (const auto& s : readSheet(p.sheet)) sum += s.points;
        CHECK(sum == p.score);
    }
    // An arena without streaks doubles nothing.
    CHECK(readSheet("2222", false) ==
          std::vector<S>{{2, false, false}, {2, false, false}, {2, false, false}, {2, false, false}});
    auto j        = nlohmann::json::parse(ARENA);
    j["noStreak"] = true;
    CHECK_FALSE(readArena(j).streaks);

    // Asked with the token: the user's rank, the game they play, a pause.
    const auto mine = readArena(nlohmann::json::parse(arenaWithToken(false, false)));
    REQUIRE(mine.me);
    CHECK(mine.me->rank == 214);
    CHECK(mine.me->game == "mine0001");
    CHECK_FALSE(mine.me->paused);
    CHECK(mine.page == 22);
    CHECK(readArena(nlohmann::json::parse(arenaWithToken(false, true))).me->paused);

    // Finished: its podium, with each one's games and performance.
    auto over = nlohmann::json::parse(ARENA);
    over.erase("isStarted");
    over["isFinished"]  = true;
    over["podium"]      = nlohmann::json::array({{{"name", "Player_A"},
                                                  {"rank", 1},
                                                  {"rating", 1834},
                                                  {"score", 31},
                                                  {"nb", {{"game", 12}, {"berserk", 4}, {"win", 10}}},
                                                  {"performance", 2104}}});
    const auto finished = readArena(over);
    CHECK(finished.info.state == TourState::Finished);
    REQUIRE(finished.podium.size() == 1);
    CHECK(finished.podium[0].games == 12);
    CHECK(finished.podium[0].performance == 2104);
}

TEST_CASE("Lichess arenas: the conditions as Lichess judges them, the user's figures, asked with the token only") {
    // Judged for the user: each of the arena's own conditions given its verdict, in lila's order.
    const auto judged = readConditions(nlohmann::json::parse(arenaWithToken(true, false)));
    REQUIRE(judged.size() == 2);
    CHECK(judged[0].kind == TourCondition::Kind::RatedGames);
    CHECK(judged[0].ok == std::optional<bool>(true));
    CHECK(judged[0].text == "≥ 10 rated Rapid games");
    CHECK(judged[1].kind == TourCondition::Kind::MaxRating);
    CHECK(judged[1].value == 1800);
    CHECK(judged[1].ok == std::optional<bool>(false));
    CHECK(judged[1].refusal == "Your top weekly Rapid rating (1812) is too high");
    const auto profile = sampleProfile();
    CHECK(conditionsMet(judged, "rapid", &profile) == std::optional<bool>(false));
    CHECK(conditionsMet(judged, "rapid", nullptr) == std::optional<bool>(false));  // Lichess's word is enough
    // Not judged: the user's figures decide what they can.
    std::vector<TourCondition> own(2);
    own[0].kind  = TourCondition::Kind::RatedGames;
    own[0].value = 20;
    own[1].kind  = TourCondition::Kind::MinRating;
    own[1].value = 1700;
    CHECK(conditionsMet(own, "rapid", &profile) == std::optional<bool>(true));
    own[0].value = 400;  // 312 games
    CHECK(conditionsMet(own, "rapid", &profile) == std::optional<bool>(false));
    own[0].value = 20;
    own[1].perf  = "bullet";  // no rated bullet game: not known
    CHECK(conditionsMet(own, "rapid", &profile) == std::nullopt);
    std::vector<TourCondition> titled(1);
    titled[0].kind = TourCondition::Kind::Titled;
    CHECK(conditionsMet(titled, "rapid", &profile) == std::nullopt);
    // A condition RookStudio does not read (a Swiss's "Play your games"): Lichess's list as it words it.
    const auto swiss = readConditions(nlohmann::json::parse(
        R"({"verdicts": {"list": [{"condition": "≥ 8 rated Rapid games", "verdict": "ok"},
                                  {"condition": "Play your games", "verdict": "You did not play your last game"}],
                         "accepted": false}})"));
    REQUIRE(swiss.size() == 2);
    CHECK(swiss[1].kind == TourCondition::Kind::Other);
    CHECK(swiss[1].text == "Play your games");
    CHECK(swiss[1].refusal == "You did not play your last game");

    // Against a server: the token sent when there is one, the standing's page; an anonymous answer judges nothing.
    std::mutex mutex;
    std::vector<Seen> seen;
    const auto port = freePort();
    ScriptedServer server(port, [&](const std::string& head, net::TcpConnection& c) {
        const auto s = readRequest(head, c);
        {
            std::lock_guard lock(mutex);
            seen.push_back(s);
        }
        c.sendAll(reply(200, s.authorization.empty() ? std::string(ARENA) : arenaWithToken(true, false)));
    });
    REQUIRE(server.ok());
    servers().api       = fmt::format("http://127.0.0.1:{}", port);
    const auto anyone   = arenaOf("rap1d100");
    const auto the_user = arenaOf("rap1d100", 22, "tok");
    servers()           = Servers{};
    REQUIRE(anyone.value);
    CHECK_FALSE(anyone.value->info.judged);
    for (const auto& c : anyone.value->info.conditions) CHECK_FALSE(c.ok);
    REQUIRE(the_user.value);
    CHECK(the_user.value->info.judged);
    CHECK_FALSE(the_user.value->info.accepted);
    CHECK(joinOffer(the_user.value->info, &profile) == JoinOffer::ConditionNotMet);
    std::lock_guard lock(mutex);
    REQUIRE(seen.size() == 2);
    CHECK(seen[0].line == "GET /api/tournament/rap1d100 HTTP/1.1");
    CHECK(seen[0].authorization.empty());
    CHECK(seen[1].line == "GET /api/tournament/rap1d100?page=22 HTTP/1.1");
    CHECK(seen[1].authorization == "Bearer tok");
}

TEST_CASE("Lichess arenas: the scopes a link asks once, the others kept") {
    // tournament:write the first time the user joins, team:read the first time their teams' Swiss tournaments are
    // read; the link made again keeps what was granted.
    const std::vector<std::string> granted{"board:play", "challenge:read"};
    const auto with = scopesAdding(granted, SCOPE_TOURNAMENT_WRITE);
    CHECK(with == std::vector<std::string>{"board:play", "challenge:read", "tournament:write"});
    CHECK(scopesAdding(with, SCOPE_TOURNAMENT_WRITE) == with);  // granted already: asked no more
    CHECK(scopesAdding(with, SCOPE_TEAM_READ).back() == "team:read");
    Pkce pkce{"verifier", "challenge", "state"};
    const auto url = authorizeUrl(pkce, "http://127.0.0.1:1/callback", with, "RS_Sample");
    CHECK(url.find("scope=board%3Aplay%20challenge%3Aread%20tournament%3Awrite") != std::string::npos);
    Account account;
    account.scopes = with;
    CHECK(account.has(SCOPE_TOURNAMENT_WRITE));
    CHECK_FALSE(account.has(SCOPE_TEAM_READ));
}

TEST_CASE("Lichess arenas: joined again each minute while waiting, the lock between games, the pause") {
    using Clock = ArenaSeat::Clock;
    const Clock::time_point t0{std::chrono::hours(1)};
    ArenaSeat seat;
    CHECK_FALSE(seat.in());
    CHECK_FALSE(seat.locks());
    CHECK_FALSE(seat.due(t0));
    seat.joined(t0);
    CHECK(seat.in());
    CHECK(seat.waiting());
    CHECK(seat.locks());  // the aids locked from the join on, before the first game (once the arena has started)
    // Lichess's pairing of a join lasts a minute: joined again before it ends, while the user waits.
    CHECK_FALSE(seat.due(t0 + 49s));
    CHECK(seat.due(t0 + ARENA_REJOIN));
    CHECK(seat.due(t0 + 5min));
    // A game: no join, the lock held; the game over, the next join due at once (the last one is old).
    seat.playing(true);
    CHECK_FALSE(seat.due(t0 + 3min));
    CHECK(seat.locks());
    seat.playing(false);
    CHECK(seat.locks());  // between two games too (point 18)
    CHECK(seat.due(t0 + 3min));
    seat.joined(t0 + 3min);
    CHECK_FALSE(seat.due(t0 + 3min + 10s));
    // The pause gives the aids back and joins no more; joining again takes them back.
    seat.pause();
    CHECK_FALSE(seat.in());
    CHECK(seat.paused());
    CHECK_FALSE(seat.locks());
    CHECK_FALSE(seat.due(t0 + 10min));
    seat.joined(t0 + 10min);
    CHECK(seat.locks());
    CHECK_FALSE(seat.paused());
    // The arena over: nothing more.
    seat.over();
    CHECK(seat.isOver());
    CHECK_FALSE(seat.locks());
    CHECK_FALSE(seat.due(t0 + 20min));

    // The requests: joining with pairMeAsap and the token, pausing.
    std::mutex mutex;
    std::vector<Seen> seen;
    const auto port = freePort();
    ScriptedServer server(port, [&](const std::string& head, net::TcpConnection& c) {
        const auto s = readRequest(head, c);
        {
            std::lock_guard lock(mutex);
            seen.push_back(s);
        }
        if (s.path == "/api/tournament/team0001/join") {
            c.sendAll(reply(400, R"({"error":"This tournament is reserved for the members of team Rook Club"})"));
            return;
        }
        c.sendAll(reply(200, R"({"ok":true})"));
    });
    REQUIRE(server.ok());
    servers().api    = fmt::format("http://127.0.0.1:{}", port);
    const auto join  = joinArena("tok", "rap1d100");
    const auto again = joinArena("tok", "rap1d100");
    const auto pause = withdrawArena("tok", "rap1d100");
    const auto team  = joinArena("tok", "team0001");
    servers()        = Servers{};
    CHECK(join.ok);
    CHECK(again.ok);
    CHECK(pause.ok);
    CHECK_FALSE(team.ok);
    CHECK(team.net == NetError::Server);
    CHECK(describe(team) == "This tournament is reserved for the members of team Rook Club");  // Lichess's words
    std::lock_guard lock(mutex);
    REQUIRE(seen.size() == 4);
    CHECK(seen[0].line == "POST /api/tournament/rap1d100/join HTTP/1.1");
    CHECK(seen[0].authorization == "Bearer tok");
    CHECK(seen[0].body == "pairMeAsap=true");
    CHECK(seen[2].line == "POST /api/tournament/rap1d100/withdraw HTTP/1.1");
}

TEST_CASE("Lichess arenas: berserk only before the user's first move where the arena allows it, refused after") {
    BoardState s;
    REQUIRE(readBoardLine(
        R"({"type": "gameFull", "id": "aRena001", "variant": {"key": "standard"}, "speed": "rapid", "rated": true,
            "white": {"id": "rs_sample", "name": "RS_Sample", "rating": 1812},
            "black": {"id": "player_q", "name": "Player_Q", "rating": 1790},
            "initialFen": "startpos", "clock": {"initial": 600000, "increment": 0}, "tournamentId": "rap1d100",
            "state": {"type": "gameState", "moves": "", "wtime": 600000, "btime": 600000, "status": "started"}})",
        s));
    CHECK(s.tournament == "rap1d100");
    CHECK(tournamentGame(s));
    CHECK(berserkAllowed(true, s, true));  // White, before its first move
    CHECK(berserkAllowed(true, s, false));
    CHECK_FALSE(berserkAllowed(false, s, true));  // the arena does not allow it
    REQUIRE(readBoardLine(R"({"type": "gameState", "moves": "e2e4", "wtime": 600000, "btime": 600000,
                              "status": "started"})",
                          s));
    CHECK_FALSE(berserkAllowed(true, s, true));  // White has moved
    CHECK(berserkAllowed(true, s, false));       // Black not yet
    REQUIRE(readBoardLine(R"({"type": "gameState", "moves": "e2e4", "wtime": 600000, "btime": 300000,
                              "status": "started", "bberserk": true})",
                          s));
    CHECK(s.black_berserk);
    CHECK_FALSE(s.white_berserk);
    CHECK_FALSE(berserkAllowed(true, s, false));  // gone berserk already
    REQUIRE(readBoardLine(R"({"type": "gameState", "moves": "e2e4 e7e5", "wtime": 600000, "btime": 300000,
                              "status": "started"})",
                          s));
    CHECK(s.black_berserk);  // it stays, though Lichess writes it no more
    CHECK_FALSE(berserkAllowed(true, s, false));
    // A game that is not a tournament's: its first move missed aborts it, it does not lose it.
    BoardState lobby;
    REQUIRE(readBoardLine(R"({"type": "gameFull", "id": "lobby001", "speed": "rapid", "white": {"name": "A"},
                              "black": {"name": "B"}, "state": {"moves": "", "status": "started"}})",
                          lobby));
    CHECK_FALSE(tournamentGame(lobby));

    // Lichess refuses it after the first move: its words said.
    std::vector<std::string> paths;
    std::mutex mutex;
    const auto port = freePort();
    ScriptedServer server(port, [&](const std::string& head, net::TcpConnection& c) {
        const auto s2 = readRequest(head, c);
        {
            std::lock_guard lock(mutex);
            paths.push_back(s2.line);
        }
        if (s2.path == "/api/board/game/late0001/berserk")
            c.sendAll(reply(400, R"({"error":"Cannot berserk after the first move"})"));
        else
            c.sendAll(reply(200, R"({"ok":true})"));
    });
    REQUIRE(server.ok());
    servers().api    = fmt::format("http://127.0.0.1:{}", port);
    const auto early = berserk("tok", "aRena001");
    const auto late  = berserk("tok", "late0001");
    servers()        = Servers{};
    CHECK(early.ok);
    CHECK_FALSE(late.ok);
    CHECK(describe(late) == "Cannot berserk after the first move");
    std::lock_guard lock(mutex);
    REQUIRE(paths.size() == 2);
    CHECK(paths[0] == "POST /api/board/game/aRena001/berserk HTTP/1.1");
}

TEST_CASE("Lichess arenas: the games of the user's arenas heard on the event stream with their tournament") {
    const auto e = readGameEvent(
        R"({"type": "gameStart", "game": {"fullId": "aRena001wxyz", "gameId": "aRena001", "fen": "startpos",
            "color": "white", "source": "arena", "speed": "rapid", "perf": "rapid", "rated": true, "isMyTurn": true,
            "opponent": {"id": "player_q", "username": "Player_Q", "rating": 1790}, "secondsLeft": 600,
            "tournamentId": "rap1d100", "compat": {"bot": false, "board": true}}})");
    REQUIRE(e);
    CHECK(e->game.tournament == "rap1d100");
    CHECK(e->game.swiss.empty());
    CHECK(e->game.source == "arena");
    CHECK(playableHere(e->game));
    const auto blitz = readGameEvent(
        R"({"type": "gameStart", "game": {"gameId": "bl1tz001", "color": "black", "source": "arena",
            "speed": "blitz", "tournamentId": "bl1tz320", "compat": {"bot": false, "board": false}}})");
    REQUIRE(blitz);
    CHECK_FALSE(playableHere(blitz->game));  // a blitz arena's game: played on lichess.org
    const auto swiss = readGameEvent(
        R"({"type": "gameStart", "game": {"gameId": "sw1ss001", "color": "black", "source": "swiss",
            "speed": "rapid", "swissId": "club0001", "compat": {"board": true}}})");
    REQUIRE(swiss);
    CHECK(swiss->game.swiss == "club0001");
}

TEST_CASE("Lichess Swiss: the user's teams, their Swiss tournaments, the next round said two minutes before") {
    const auto teams = readTeams(nlohmann::json::parse(
        R"([{"id": "rook-club", "name": "Rook Club", "nbMembers": 46, "leaders": [{"name": "Player_L"}]},
            {"id": "knights-of-somewhere", "name": "Knights Club", "nbMembers": 1200}])"));
    REQUIRE(teams.size() == 2);
    CHECK(teams[0] == Team{"rook-club", "Rook Club", 46});
    const char* const swisses =
        R"({"id": "club0002", "createdBy": "player_l", "startsAt": "2026-11-26T20:00:00Z", "name": "Thursday Swiss",
            "clock": {"limit": 900, "increment": 10}, "variant": "standard", "round": 0, "nbRounds": 7,
            "nbPlayers": 46, "nbOngoing": 0, "status": "created",
            "nextRound": {"at": "2026-11-26T20:00:00Z", "in": 223200},
            "verdicts": {"list": [{"condition": "Play your games", "verdict": "ok"}], "accepted": true},
            "rated": true}
           {"id": "club0001", "createdBy": "player_l", "startsAt": "2026-11-19T20:00:00Z", "name": "Thursday Swiss",
            "clock": {"limit": 900, "increment": 10}, "variant": "standard", "round": 7, "nbRounds": 7,
            "nbPlayers": 41, "nbOngoing": 0, "status": "finished", "rated": true}
           {"id": "club0003", "createdBy": "player_l", "startsAt": "2026-11-27T19:00:00Z", "name": "Friday Blitz",
            "clock": {"limit": 180, "increment": 2}, "variant": "standard", "round": 0, "nbRounds": 9,
            "nbPlayers": 12, "nbOngoing": 0, "status": "created", "rated": true}
)";
    // NDJSON: each object on a line of its own.
    std::string ndjson;
    {
        std::string text(swisses), object;
        int depth = 0;
        for (const char c : text) {
            if (c == '{') ++depth;
            if (depth > 0) object += c;
            if (c == '}' && --depth == 0) {
                ndjson += nlohmann::json::parse(object).dump() + "\n";
                object.clear();
            }
        }
    }
    const auto list = readSwissList(ndjson);
    REQUIRE(list.size() == 3);
    CHECK(list[0].name == "Thursday Swiss");
    CHECK(list[0].rounds == 7);
    CHECK(list[0].players == 46);
    CHECK(list[0].state == TourState::Created);
    CHECK(list[0].speed() == "rapid");
    CHECK(list[0].next_round_at == *isoMillis("2026-11-26T20:00:00Z"));
    CHECK(list[1].state == TourState::Finished);
    CHECK(playableHere(list[0]));
    CHECK_FALSE(playableHere(list[2]));  // blitz: on lichess.org
    CHECK(isoMillis("2026-11-26T20:00:00.250Z") == *isoMillis("2026-11-26T20:00:00Z") + 250);
    CHECK_FALSE(isoMillis("not a date"));

    // The notice: two minutes before the round, not after it started.
    const int64_t at = *isoMillis("2026-11-26T21:10:00Z");
    CHECK_FALSE(roundNoticeDue(at, at - 121000));
    CHECK(roundNoticeDue(at, at - 120000));
    CHECK(roundNoticeDue(at, at - 1000));
    CHECK_FALSE(roundNoticeDue(at, at));
    CHECK_FALSE(roundNoticeDue(0, at));

    // Against a server: the teams with the token (403 without the permission), a team's list, a standing.
    std::mutex mutex;
    std::vector<Seen> seen;
    const auto port = freePort();
    ScriptedServer server(port, [&](const std::string& head, net::TcpConnection& c) {
        const auto s = readRequest(head, c);
        {
            std::lock_guard lock(mutex);
            seen.push_back(s);
        }
        if (s.path == "/api/team/of/RS_Sample")
            c.sendAll(s.authorization == "Bearer tok-team"
                          ? reply(200, R"([{"id": "rook-club", "name": "Rook Club", "nbMembers": 46}])")
                          : reply(403, R"({"error":"Missing scope"})"));
        else if (s.path == "/api/team/rook-club/swiss?max=5")
            c.sendAll(reply(200, ndjson, "application/x-ndjson"));
        else if (s.path == "/api/swiss/club0001/results")
            c.sendAll(reply(200,
                            "{\"rank\":1,\"points\":6,\"tieBreak\":25.5,\"rating\":2104,\"username\":\"Player_V\","
                            "\"performance\":2388}\n{\"rank\":2,\"points\":5.5,\"tieBreak\":22,\"rating\":1987,"
                            "\"username\":\"Player_W\",\"performance\":2251,\"absent\":true}\n",
                            "application/x-ndjson"));
        else
            c.sendAll(reply(404, R"({"error":"Not found"})"));
    });
    REQUIRE(server.ok());
    servers().api       = fmt::format("http://127.0.0.1:{}", port);
    const auto refused  = teamsOf("RS_Sample", "tok");
    const auto allowed  = teamsOf("RS_Sample", "tok-team");
    const auto of_team  = teamSwisses("rook-club", "tok-team", 5);
    const auto standing = swissResults("club0001");
    servers()           = Servers{};
    CHECK_FALSE(refused.value);
    CHECK(refused.status == 403);
    CHECK(refused.net == NetError::Forbidden);
    REQUIRE(allowed.value);
    CHECK(allowed.value->front().name == "Rook Club");
    REQUIRE(of_team.value);
    CHECK(of_team.value->size() == 3);
    CHECK(of_team.value->front().team_id == "rook-club");
    CHECK(of_team.value->front().judged);
    REQUIRE(standing.value);
    REQUIRE(standing.value->size() == 2);
    CHECK(standing.value->at(0) == SwissRow{1, "Player_V", "", 2104, 6, 25.5, 2388, false});
    CHECK(standing.value->at(1).absent);
    std::lock_guard lock(mutex);
    CHECK(seen[0].authorization == "Bearer tok");
}

TEST_CASE("Lichess Swiss: a game whose first move is not played in time is lost, not aborted") {
    BoardState s;
    REQUIRE(readBoardLine(
        R"({"type": "gameFull", "id": "sw1ss002", "speed": "rapid", "rated": true, "swissId": "club0002",
            "white": {"id": "player_u", "name": "Player_U", "rating": 1902},
            "black": {"id": "rs_sample", "name": "RS_Sample", "rating": 1812},
            "clock": {"initial": 900000, "increment": 10000},
            "state": {"type": "gameState", "moves": "d2d4", "wtime": 900000, "btime": 900000, "status": "started",
                      "expiration": {"idleMillis": 1000, "millisToMove": 30000}}})",
        s));
    CHECK(s.swiss == "club0002");
    CHECK(tournamentGame(s));
    CHECK(s.first_move_ms == 29000);
    REQUIRE(readBoardLine(R"({"type": "gameState", "moves": "d2d4", "wtime": 900000, "btime": 900000,
                              "status": "noStart", "winner": "white"})",
                          s));
    CHECK(s.over());
    CHECK(lostAtStart(s, false));  // the user had Black
    CHECK_FALSE(lostAtStart(s, true));
    CHECK(replayOf(s).result == "1-0");
}

TEST_CASE("Lichess arenas: the standing waits its minute after a 429, a game's stream going on meanwhile") {
    HostGate::instance().setPause(700ms);  // a minute, short for the test
    std::atomic<int> arena_asked{0};
    std::atomic<bool> sent_during_pause{false};
    const auto port = freePort();
    ScriptedServer server(port, [&](const std::string& head, net::TcpConnection& c) {
        const auto path = requestPath(head);
        if (path.rfind("/api/tournament/", 0) == 0) {
            if (++arena_asked == 1) {
                c.sendAll(reply(429, R"({"error":"Too many requests"})"));
                return;
            }
            c.sendAll(reply(200, ARENA));
            return;
        }
        if (path.find("/chat") != std::string::npos) {
            c.sendAll(reply(200, "[]"));
            return;
        }
        // The user's game, its stream open before the 429: its moves go on coming.
        c.sendAll(STREAM_HEAD);
        c.sendAll(line(
            R"({"type": "gameFull", "id": "aRena001", "speed": "rapid", "white": {"name": "RS_Sample"},
                "black": {"name": "Player_Q"}, "clock": {"initial": 600000, "increment": 0}, "tournamentId": "rap1d100",
                "state": {"type": "gameState", "moves": "e2e4", "wtime": 600000, "btime": 600000,
                          "status": "started"}})"));
        (void)waitUntil([&] { return arena_asked.load() >= 1 && HostGate::instance().pausedFor("127.0.0.1") > 0ms; },
                        10s);
        sent_during_pause = HostGate::instance().pausedFor("127.0.0.1") > 0ms;
        c.sendAll(line(R"({"type": "gameState", "moves": "e2e4 e7e5", "wtime": 600000, "btime": 600000,
                           "status": "started"})"));
        (void)waitUntil([] { return false; }, 1500ms);
        c.sendAll(LAST_CHUNK);
    });
    REQUIRE(server.ok());
    servers().api = fmt::format("http://127.0.0.1:{}", port);
    GameStream game("tok", "aRena001");
    BoardState state;
    uint64_t version = 0;
    REQUIRE(waitUntil([&] { return game.state(state, version) && state.full; }));
    const auto first = arenaOf("rap1d100");
    CHECK(first.status == 429);
    CHECK(first.net == NetError::RateLimited);
    CHECK(describe(first) == "Lichess asks to wait a minute: too many requests");
    CHECK(HostGate::instance().pausedFor("127.0.0.1") > 0ms);
    // The game's moves come during the pause.
    CHECK(waitUntil([&] {
        (void)game.state(state, version);
        return state.moves.size() == 2;
    }));
    CHECK(sent_during_pause.load());
    // The standing asked again waits for the pause's end.
    const auto before = std::chrono::steady_clock::now();
    const auto again  = arenaOf("rap1d100");
    CHECK(again.value);
    CHECK(std::chrono::steady_clock::now() - before >= 300ms);
    servers() = Servers{};
    HostGate::instance().setPause(60s);
}

TEST_CASE("Lichess arenas: an arena game's stream cut is opened again, tried after 1 s, then 2 s") {
    std::atomic<int> opened{0};
    const auto port = freePort();
    ScriptedServer server(port, [&](const std::string& head, net::TcpConnection& c) {
        const auto path = requestPath(head);
        if (path.find("/chat") != std::string::npos) {
            c.sendAll(reply(200, "[]"));
            return;
        }
        const int n = ++opened;
        if (n <= 2) return;  // cut: closed without an answer
        c.sendAll(STREAM_HEAD);
        c.sendAll(line(
            R"({"type": "gameFull", "id": "aRena002", "speed": "rapid", "white": {"name": "RS_Sample"},
                "black": {"name": "Player_Q"}, "clock": {"initial": 600000, "increment": 0}, "tournamentId": "rap1d100",
                "state": {"type": "gameState", "moves": "e2e4 c7c5", "wtime": 600000, "btime": 600000,
                          "status": "started"}})"));
        (void)waitUntil([] { return false; }, 300ms);
        c.sendAll(LAST_CHUNK);
    });
    REQUIRE(server.ok());
    servers().api = fmt::format("http://127.0.0.1:{}", port);
    GameStream game("tok", "aRena002");
    BoardState state;
    uint64_t version = 0;
    std::vector<int> tries;
    std::vector<int64_t> waits;
    CHECK(waitUntil(
        [&] {
            if (game.state(state, version) && !state.connected && state.tries > 0 &&
                (tries.empty() || tries.back() != state.tries)) {
                tries.push_back(state.tries);
                waits.push_back(std::chrono::duration_cast<std::chrono::milliseconds>(state.retry_at -
                                                                                      std::chrono::steady_clock::now())
                                    .count());
            }
            return state.full;
        },
        20s));
    servers() = Servers{};
    CHECK(state.tournament == "rap1d100");
    CHECK(state.moves.size() == 2);
    REQUIRE(tries.size() >= 2);
    CHECK(tries[0] == 1);
    CHECK(tries[1] == 2);
    CHECK(waits[0] <= 1000);
    CHECK(waits[1] > 1000);  // the second try after 2 s
    CHECK(waits[1] <= 2000);
}

TEST_CASE("Lichess arenas: a finished arena game kept in the user's Lichess database, its event the arena's") {
    const auto port = freePort();
    ScriptedServer server(port, [&](const std::string& head, net::TcpConnection& c) {
        if (requestPath(head).rfind("/game/export/aRena001", 0) == 0)
            c.sendAll(reply(200,
                            "[Event \"Hourly Rapid Arena\"]\n[Site \"https://lichess.org/aRena001\"]\n"
                            "[Date \"2026.11.24\"]\n[White \"RS_Sample\"]\n[Black \"Player_Q\"]\n[Result \"1-0\"]\n"
                            "[WhiteElo \"1812\"]\n[BlackElo \"1790\"]\n[TimeControl \"600+0\"]\n\n"
                            "1. e4 e5 2. Qh5 Nc6 3. Bc4 Nf6 4. Qxf7# 1-0\n",
                            "application/x-chess-pgn"));
        else
            c.sendAll(reply(404, ""));
    });
    REQUIRE(server.ok());
    servers().api  = fmt::format("http://127.0.0.1:{}", port);
    const auto dir = fs::temp_directory_path() / "arena-account";
    fs::remove_all(dir);
    auto db         = study::Database::create(dir);
    const auto came = bringGame("aRena001", "tok", db.get());
    servers()       = Servers{};
    CHECK_FALSE(came.failure.failed());
    CHECK(came.added);
    CHECK(came.plies == 7);
    const auto snap = db->snapshot();
    REQUIRE(snap->games() == 1);
    CHECK(snap->name(snap->rowIds(0).event) == "Hourly Rapid Arena");
}

namespace {

// A Swiss of six players and three rounds, as Lichess's TRF writes it (its columns, its lowercase names): a bye (U), a
// late join's half point (H), an absence (-), a forfeit (+ and -).
std::string trfLine(int number, const std::string& name, int rating, const std::string& points,
                    const std::vector<std::string>& rounds) {
    std::string line(99 + 10 * (rounds.size() - 1), ' ');
    const auto put = [&](std::size_t end, const std::string& text) {
        line.replace(end - text.size(), text.size(), text);
    };
    put(3, "001");
    put(8, std::to_string(number));
    put(15 + name.size(), name);
    put(52, std::to_string(rating));
    put(84, points);
    for (std::size_t r = 0; r < rounds.size(); ++r) {
        const auto& cell = rounds[r];  // "4 w 1", or a code alone
        const auto at    = 10 * r;
        if (cell.size() == 1) {
            put(99 + at, cell);
            continue;
        }
        const auto first = cell.find(' '), second = cell.rfind(' ');
        put(95 + at, cell.substr(0, first));
        put(97 + at, cell.substr(first + 1, 1));
        put(99 + at, cell.substr(second + 1));
    }
    while (!line.empty() && line.back() == ' ') line.pop_back();
    return line;
}

std::string swissTrfText() {
    std::string t =
        "012 Thursday Swiss\n022 https://lichess.org/swiss/club0001\n032 Lichess\n042 Nov 19, 2026\n"
        "052 Nov 19, 2026\n062 6\n092 Individual: Swiss-System\n102 https://lichess.org/swiss\nXXR 3\n"
        "XXC white1\n";
    t += trfLine(1, "player_v", 2104, "2.5", {"4 w 1", "2 b =", "3 w 1"}) + "\n";
    t += trfLine(2, "player_w", 1987, "2.5", {"5 w 1", "1 w =", "4 b 1"}) + "\n";
    t += trfLine(3, "player_x", 2050, "1.0", {"6 b +", "4 w 0", "1 b 0"}) + "\n";
    t += trfLine(4, "player_y", 1902, "1.0", {"1 b 0", "3 b 1", "2 w 0"}) + "\n";
    t += trfLine(5, "player_z", 1944, "1.5", {"2 b 0", "U", "H"}) + "\n";
    t += trfLine(6, "rs_sample", 1812, "0.0", {"3 w -", "-", "-"}) + "\n";
    return t;
}

std::string swissGame(const std::string& id, const std::string& white, const std::string& black,
                      const std::string& result, const std::string& moves = "1. d4 d5 2. c4 e6 3. Nc3 Nf6 ") {
    return fmt::format(
        "[Event \"Thursday Swiss\"]\n[Site \"https://lichess.org/{0}\"]\n[Date \"2026.11.19\"]\n[Round \"-\"]\n"
        "[White \"{1}\"]\n[Black \"{2}\"]\n[Result \"{3}\"]\n[GameId \"{0}\"]\n[WhiteElo \"2000\"]\n"
        "[BlackElo \"1950\"]\n[TimeControl \"900+10\"]\n\n{4}{3}\n\n",
        id, white, black, result, moves);
}

}  // namespace

TEST_CASE("Lichess Swiss: kept in a database with its TRF, its crosstable made by #92 with Lichess's points") {
    const auto trf = readTrf(swissTrfText());
    CHECK(trf.name == "Thursday Swiss");
    CHECK(trf.url == "https://lichess.org/swiss/club0001");
    CHECK(trf.rounds == 3);
    REQUIRE(trf.players.size() == 6);
    CHECK(trf.players[0].name == "player_v");
    CHECK(trf.players[0].rating == 2104);
    CHECK(trf.players[0].points == 2.5);
    CHECK(trf.players[0].rounds[1] == TrfRound{2, 'b', '='});
    CHECK(trf.players[4].rounds[1] == TrfRound{0, 0, 'U'});
    CHECK(trf.players[4].rounds[2] == TrfRound{0, 0, 'H'});
    CHECK(trf.players[5].rounds[0] == TrfRound{3, 'w', '-'});
    CHECK(trf.player("Player_Y")->number == 4);
    CHECK(trfPoints('U') == 1.0);
    CHECK(trfPoints('H') == 0.5);
    CHECK(trfPoints('-') == 0.0);
    CHECK(trfPoints('+') == 1.0);

    // Its games as Lichess exports them (no round, each its own address); the forfeit a game without a move.
    std::string pgn;
    pgn += swissGame("sw000001", "Player_V", "Player_Y", "1-0");
    pgn += swissGame("sw000002", "Player_W", "Player_Z", "1-0");
    pgn += swissGame("sw000007", "RS_Sample", "Player_X", "0-1", "");
    pgn += swissGame("sw000003", "Player_W", "Player_V", "1/2-1/2");
    pgn += swissGame("sw000004", "Player_X", "Player_Y", "0-1");
    pgn += swissGame("sw000005", "Player_V", "Player_X", "1-0");
    pgn += swissGame("sw000006", "Player_Y", "Player_W", "0-1");
    const auto kept = keepSwiss(trf, pgn, "Thursday Swiss", "club0001", "20:00 on 19 November 2026");
    CHECK(kept.event == "Thursday Swiss, 20:00 on 19 November 2026");
    CHECK(kept.unmatched == 0);
    REQUIRE(kept.games.size() == 7);
    const auto tag = [](const study::ImportedGame& g, const char* key) {
        for (const auto& [k, v] : g.tags)
            if (k == key) return v;
        return std::string();
    };
    CHECK(tag(kept.games[0], "Round") == "1");
    CHECK(tag(kept.games[2], "Round") == "1");
    CHECK(tag(kept.games[3], "Round") == "2");
    CHECK(tag(kept.games[5], "Round") == "3");
    CHECK(tag(kept.games[0], "Site") == "https://lichess.org/swiss/club0001");
    CHECK(tag(kept.games[0], "GameId") == "sw000001");
    CHECK(tag(kept.games[0], "Event") == kept.event);
    // The rounds without a game: Player_Z's bye and late half point, the user's absences.
    using study::HandResult;
    CHECK(kept.hand == std::vector<HandResult>{{"Player_Z", "", 2, '*', 1.0},
                                               {"Player_Z", "", 3, '*', 0.5},
                                               {"RS_Sample", "", 2, '*', 0.0},
                                               {"RS_Sample", "", 3, '*', 0.0}});
    // A game the export lacks: its pair's result set by hand, from the TRF.
    std::string less;
    for (const auto text : study::splitGames(pgn))
        if (text.find("sw000004") == std::string_view::npos) less += std::string(text) + "\n";
    const auto lacking = keepSwiss(trf, less, "Thursday Swiss", "club0001", "20:00 on 19 November 2026");
    CHECK(lacking.games.size() == 6);
    CHECK(std::find(lacking.hand.begin(), lacking.hand.end(), HandResult{"Player_X", "Player_Y", 2, '0', 0}) !=
          lacking.hand.end());
    CHECK(kept.key == study::eventKey(kept.event) + "|2026");

    // Into a database: one tournament, the key computed, a Swiss of three rounds; each player's points Lichess's.
    const auto dir = fs::temp_directory_path() / "kept-swiss";
    fs::remove_all(dir);
    auto db    = study::Database::create(dir);
    auto games = kept.games;
    (void)db->append(std::move(games), true);
    const auto snap = db->snapshot();
    study::TournamentProgress progress;
    std::atomic<bool> cancel{false};
    const auto list = study::listTournaments(snap, progress, cancel, 2);
    REQUIRE(list.tournaments.size() == 1);
    const auto& entry = list.tournaments[0];
    CHECK(entry.key == kept.key);
    CHECK(entry.shape.system.system == study::TournamentSystem::Swiss);
    CHECK(entry.shape.rounds == 3);
    const auto ids = study::tournamentGames(*snap, entry.ref, cancel);
    auto input     = study::tournamentInput(*snap, ids);
    study::CrossOptions options;
    options.hand     = kept.hand;
    const auto table = study::makeCrosstable(std::move(input.players), std::move(input.games), options);
    std::map<std::string, double> points;
    for (const auto& row : table.rows) points[table.players[row.player].name] = row.points;
    CHECK(points.size() == 6);  // the user in it through their forfeit
    for (const auto& p : trf.players) {
        const auto* shown = &p.name;
        std::string name;
        for (const auto& [n, v] : points) {
            std::string folded = n;
            for (auto& c : folded) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (folded == p.name) name = n;
        }
        CAPTURE(*shown);
        REQUIRE_FALSE(name.empty());
        CHECK(points[name] == p.points);
    }
}

TEST_CASE("Lichess arenas and Swiss: each failure of their requests told by its kind") {
    AnsweringServer server;
    REQUIRE(server.ok());
    HostGate::instance().setPause(50ms);
    struct Case {
        int status;
        const char* body;
        NetError net;
    };
    for (const auto& [status, body, net] :
         {Case{401, "{}", NetError::Unauthorized}, Case{403, R"({"error":"Missing scope"})", NetError::Forbidden},
          Case{404, R"({"error":"Not found"})", NetError::NotFound}, Case{429, "{}", NetError::RateLimited},
          Case{503, "<html>down</html>", NetError::Server}}) {
        CAPTURE(status);
        server.answer(status, body);
        CHECK(arenaList().net == net);
        CHECK(arenaOf("rap1d100").net == net);
        CHECK(teamsOf("RS_Sample", "tok").net == net);
        CHECK(teamSwisses("rook-club", "tok", 5).net == net);
        CHECK(swissOf("club0001", "tok").net == net);
        CHECK(swissResults("club0001").net == net);
        CHECK(swissTrf("club0001").net == net);
        CHECK(swissGames("club0001", "tok").net == net);
        CHECK(joinArena("tok", "rap1d100").net == net);
        CHECK(withdrawArena("tok", "rap1d100").net == net);
        CHECK(joinSwiss("tok", "club0001").net == net);
        CHECK(withdrawSwiss("tok", "club0001").net == net);
        CHECK(berserk("tok", "aRena001").net == net);
    }
    // What Lichess has not: a tournament, in those words.
    server.answer(404, R"({"error":"Not found"})");
    CHECK(describe(arenaOf("rap1d100")) == "Lichess has no such tournament");
    CHECK(describe(swissOf("club0001", {})) == "Lichess has no such tournament");
    // An answer that is not JSON where JSON is read.
    server.answer(200, "<html>not JSON</html>");
    CHECK(arenaList().net == NetError::BadAnswer);
    CHECK(arenaOf("rap1d100").net == NetError::BadAnswer);
    CHECK(teamsOf("RS_Sample", "tok").net == NetError::BadAnswer);
    CHECK(swissOf("club0001", "tok").net == NetError::BadAnswer);
    pointNowhere();
    CHECK(arenaList().net == NetError::Offline);
    CHECK(describe(arenaOf("rap1d100")) == "Lichess cannot be reached");
    CHECK(swissResults("club0001").net == NetError::Offline);
    CHECK(joinArena("tok", "rap1d100").net == NetError::Offline);
    pointAtPausedHost("lichess-arenas.invalid");
    std::atomic<bool> cancel{true};
    CHECK(arenaList(&cancel).net == NetError::Cancelled);
    CHECK(swissGames("club0001", "tok", &cancel).net == NetError::Cancelled);
    CHECK(joinArena("tok", "rap1d100", true, &cancel).net == NetError::Cancelled);
    servers() = Servers{};
}
