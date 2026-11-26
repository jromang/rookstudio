// Playing on Lichess from RookStudio (#84): the clocks the Board API allows, the challenges of the event stream, a
// game's stream and the board it makes, and the requests (a seek held open, a challenge kept alive, a move, the game's
// actions, the answers to a challenge) against a server on this machine.
#include <doctest/doctest.hpp>

#include <atomic>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <fmt/include/fmt/format.h>
#include <json.hpp>

#include "answering_server.hpp"
#include "free_ports.hpp"
#include "https.hpp"
#include "lichess_api.hpp"
#include "lichess_board.hpp"
#include "lichess_playing.hpp"
#include "scripted_server.hpp"

using namespace fcg;
using namespace fcg::lichess;
using namespace fcg::test;
using namespace std::chrono_literals;

namespace {

// What the server saw of a request: its line, its token, its body.
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

std::string reply(int status, const std::string& body, const char* type = "application/json") {
    return fmt::format("HTTP/1.1 {} X\r\nContent-Type: {}\r\nContent-Length: {}\r\nConnection: close\r\n\r\n{}", status,
                       type, body.size(), body);
}

// A streamed answer, as Lichess sends its NDJSON: chunked.
const char* const STREAM_HEAD =
    "HTTP/1.1 200 OK\r\nContent-Type: application/x-ndjson\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n";
std::string chunk(const std::string& data) { return fmt::format("{:x}\r\n{}\r\n", data.size(), data); }
const char* const LAST_CHUNK = "0\r\n\r\n";
// A JSON written as a line of NDJSON, in a chunk of its own.
std::string ndjson(const char* json) { return chunk(nlohmann::json::parse(json).dump() + "\n"); }

// True once the client closed the connection: a line it does not answer, sent until it fails.
bool waitClosed(net::TcpConnection& c, std::chrono::milliseconds limit = 10s) {
    const auto until = std::chrono::steady_clock::now() + limit;
    while (std::chrono::steady_clock::now() < until) {
        if (!c.sendAll(chunk("\n"), 500)) return true;
        (void)c.readUntil("\x01never", 1, 30);
    }
    return false;
}

const char* const GAME_FULL =
    R"({"type": "gameFull", "id": "gAme1234", "variant": {"key": "standard", "name": "Standard"},
  "speed": "rapid", "perf": {"name": "Rapid"}, "rated": true, "createdAt": 1793058022546,
  "white": {"id": "rs_sample", "name": "RS_Sample", "title": null, "rating": 1812},
  "black": {"id": "partner_3", "name": "Partner_3", "title": "FM", "rating": 1836, "provisional": true},
  "initialFen": "startpos", "clock": {"initial": 600000, "increment": 5000},
  "state": {"type": "gameState", "moves": "e2e4 e7e5", "wtime": 598000, "btime": 597000, "winc": 5000,
            "binc": 5000, "status": "started"}})";

}  // namespace

TEST_CASE("Lichess board: the clocks the Board API allows, as Lichess counts their speed") {
    CHECK(speedOf({0, 0, 0}) == "ultraBullet");
    CHECK(speedOf({1, 0, 0}) == "bullet");
    CHECK(speedOf({2, 1, 0}) == "bullet");  // 160 s
    CHECK(speedOf({3, 0, 0}) == "blitz");   // 180 s
    CHECK(speedOf({5, 3, 0}) == "blitz");   // 420 s
    CHECK(speedOf({8, 0, 0}) == "rapid");   // 480 s
    CHECK(speedOf({10, 5, 0}) == "rapid");
    CHECK(speedOf({15, 10, 0}) == "rapid");     // 1300 s
    CHECK(speedOf({25, 0, 0}) == "classical");  // 1500 s
    CHECK(speedOf({30, 20, 0}) == "classical");
    CHECK(speedOf({10, 5, 3}) == "correspondence");
    // A seek: rapid, classical, correspondence; never blitz nor bullet.
    CHECK(seekAllowed({10, 0, 0}));
    CHECK(seekAllowed({30, 20, 0}));
    CHECK(seekAllowed({0, 0, 14}));
    CHECK_FALSE(seekAllowed({5, 3, 0}));
    CHECK_FALSE(seekAllowed({1, 0, 0}));
    CHECK_FALSE(seekAllowed({0, 0, 4}));  // no such number of days
    // A challenge: blitz too, never bullet.
    CHECK(challengeAllowed({3, 2, 0}));
    CHECK(challengeAllowed({5, 3, 0}));
    CHECK(challengeAllowed({3, 0, 0}));
    CHECK(challengeAllowed({0, 0, 3}));
    CHECK_FALSE(challengeAllowed({2, 1, 0}));
    CHECK_FALSE(challengeAllowed({1, 0, 0}));
    // What Lichess takes.
    for (const int days : {1, 2, 3, 5, 7, 10, 14}) CHECK(validDays(days));
    for (const int days : {0, 4, 6, 15, 30}) CHECK_FALSE(validDays(days));
    CHECK(validClock({10, 180, 0}, true));
    CHECK_FALSE(validClock({10, 61, 0}, false));  // a challenge's increment, 60 s at most
    CHECK(validClock({10, 60, 0}, false));
    CHECK_FALSE(validClock({181, 0, 0}, true));
    CHECK_FALSE(validClock({0, 0, 0}, false));
    CHECK_FALSE(validClock({-1, 5, 0}, true));
}

TEST_CASE("Lichess board: a stream's move read on the board, castling written either way") {
    chess::Board board("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1");
    const auto short_castle = legalUci(board, "e1g1");
    REQUIRE(short_castle);
    CHECK(short_castle->typeOf() == chess::Move::CASTLING);
    const auto as960 = legalUci(board, "e1h1");  // the king taking its rook
    REQUIRE(as960);
    CHECK(*as960 == *short_castle);
    CHECK(legalUci(board, "E1C1"));
    CHECK_FALSE(legalUci(board, "e1e3"));
    CHECK_FALSE(legalUci(board, "zz"));
    chess::Board promote("8/P6k/8/8/8/8/8/K7 w - - 0 1");
    const auto queen = legalUci(promote, "a7a8q");
    REQUIRE(queen);
    CHECK(queen->promotionType() == chess::PieceType::QUEEN);
    CHECK(legalUci(promote, "a7a8n")->promotionType() == chess::PieceType::KNIGHT);
    CHECK_FALSE(legalUci(promote, "a7a8"));
}

TEST_CASE("Lichess board: the event stream's challenges, sent to the user or by them, declined, called back") {
    const auto in = readGameEvent(R"({"type": "challenge", "challenge": {"id": "qUfdwN17",
        "url": "https://lichess.org/game/qUfdwN17", "status": "created",
        "challenger": {"name": "Player_F", "id": "player_f", "rating": 1790, "title": "CM", "provisional": true},
        "destUser": {"name": "RS_Sample", "id": "rs_sample", "rating": 1812, "online": true},
        "variant": {"key": "standard", "name": "Standard"}, "rated": true, "speed": "rapid",
        "timeControl": {"type": "clock", "limit": 900, "increment": 10, "show": "15+10"},
        "color": "white", "finalColor": "white", "perf": {"icon": "", "name": "Rapid"}, "direction": "in"},
        "compat": {"bot": false, "board": true}})");
    REQUIRE(in);
    CHECK(in->kind == GameEvent::Kind::Challenge);
    const auto& c = in->challenge;
    CHECK(c.id == "qUfdwN17");
    CHECK(c.incoming);
    CHECK(c.challenger == "Player_F");
    CHECK(c.challenger_title == "CM");
    CHECK(c.challenger_rating == 1790);
    CHECK(c.challenger_provisional);
    CHECK(c.dest == "RS_Sample");
    CHECK(c.rated);
    CHECK(c.speed == "rapid");
    CHECK(c.clock == ChallengeClock{15, 10, 0});
    CHECK(c.limit_s == 900);
    CHECK(c.board);
    REQUIRE(c.userWhite());
    CHECK_FALSE(*c.userWhite());  // the challenger has White
    // Sent by the user, by correspondence; a bullet one the Board API cannot play.
    const auto out = readGameEvent(R"({"type": "challenge", "challenge": {"id": "outC0001", "status": "created",
        "challenger": {"name": "RS_Sample", "id": "rs_sample"}, "destUser": {"name": "Player_B", "id": "player_b"},
        "variant": {"key": "standard"}, "rated": false, "speed": "correspondence",
        "timeControl": {"type": "correspondence", "daysPerTurn": 3}, "color": "random", "direction": "out"},
        "compat": {"bot": false, "board": true}})");
    REQUIRE(out);
    CHECK_FALSE(out->challenge.incoming);
    CHECK(out->challenge.clock.days == 3);
    CHECK_FALSE(out->challenge.userWhite());  // not drawn yet
    const auto bullet = readGameEvent(R"({"type": "challenge", "challenge": {"id": "bUll0001",
        "challenger": {"name": "Player_G"}, "speed": "bullet", "timeControl": {"type": "clock", "limit": 30,
        "increment": 0}, "color": "black"}, "compat": {"board": false}})");
    REQUIRE(bullet);
    CHECK_FALSE(bullet->challenge.board);
    CHECK(bullet->challenge.limit_s == 30);
    CHECK(*bullet->challenge.userWhite());  // the challenger asked for Black
    const auto declined = readGameEvent(R"({"type": "challengeDeclined", "challenge": {"id": "outC0001",
        "direction": "out", "declineReason": "This time control is too fast for me, please challenge again with a slower game.",
        "declineReasonKey": "toofast"}})");
    REQUIRE(declined);
    CHECK(declined->kind == GameEvent::Kind::ChallengeDeclined);
    CHECK(declined->challenge.decline_key == "toofast");
    CHECK(declined->challenge.decline_reason.find("too fast") != std::string::npos);
    const auto canceled = readGameEvent(R"({"type": "challengeCanceled", "challenge": {"id": "qUfdwN17"}})");
    REQUIRE(canceled);
    CHECK(canceled->kind == GameEvent::Kind::ChallengeCanceled);
    const auto unlimited = readChallenge(nlohmann::json::parse(R"({"id": "unl00001", "timeControl": {"type":
        "unlimited"}})"));
    REQUIRE(unlimited);
    CHECK(unlimited->unlimited);
    CHECK_FALSE(readGameEvent(R"({"type": "challenge"})"));
}

TEST_CASE("Lichess board: a game's start tells whether RookStudio can play it") {
    const auto start = readGameEvent(R"({"type": "gameStart", "game": {"fullId": "p8Jy2ayw91cB", "gameId": "p8Jy2ayw",
        "fen": "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", "color": "white", "source": "lobby",
        "status": {"id": 20, "name": "started"}, "variant": {"key": "standard", "name": "Standard"},
        "speed": "rapid", "rated": true, "opponent": {"id": "aaron", "username": "Aaron", "rating": 1879},
        "isMyTurn": true, "secondsLeft": 600, "compat": {"bot": false, "board": true}}})");
    REQUIRE(start);
    CHECK(start->game.board == true);
    CHECK(start->game.source == "lobby");
    CHECK(start->game.variant == "standard");
    CHECK(start->game.status == "started");
    CHECK(playableHere(start->game));
    const auto finish = readGameEvent(R"({"type": "gameFinish", "game": {"fullId": "p8Jy2ayw91cB", "gameId": "p8Jy2ayw",
        "status": {"id": 31, "name": "resign"}, "speed": "rapid", "winner": "black", "ratingDiff": -9,
        "compat": {"board": true}}})");
    REQUIRE(finish);
    CHECK(finish->game.status == "resign");
    CHECK(finish->game.winner == "black");
    CHECK(finish->game.rating_diff == -9);
    // Without compat (/api/account/playing), as Lichess decides it.
    PlayingGame g;
    g.speed = "blitz";
    CHECK_FALSE(playableHere(g));
    g.source = "friend";
    CHECK(playableHere(g));
    g.speed = "bullet";
    CHECK_FALSE(playableHere(g));
    g.speed = "correspondence";
    CHECK(playableHere(g));
    g.variant = "crazyhouse";
    CHECK_FALSE(playableHere(g));  // not on RookStudio's board
    g.variant = "chess960";
    CHECK(playableHere(g));
    g.board = false;
    CHECK_FALSE(playableHere(g));
}

TEST_CASE("Lichess board: a game's stream read into its state, its board, its clocks") {
    BoardState s;
    CHECK_FALSE(readBoardLine("", s));
    CHECK_FALSE(readBoardLine("not json", s));
    CHECK_FALSE(readBoardLine(R"({"type": "somethingElse"})", s));
    REQUIRE(readBoardLine(GAME_FULL, s));
    CHECK(s.full);
    CHECK(s.id == "gAme1234");
    CHECK(s.white.name == "RS_Sample");
    CHECK(s.white.rating == 1812);
    CHECK(s.black.title == "FM");
    CHECK(s.black.provisional);
    CHECK(s.speed == "rapid");
    CHECK(s.perf == "Rapid");
    CHECK(s.rated);
    CHECK(s.initial_fen.empty());
    CHECK(s.clock_ms == 600000);
    CHECK(s.increment_ms == 5000);
    CHECK(s.live());
    CHECK(s.moves == std::vector<std::string>{"e2e4", "e7e5"});
    CHECK(s.white_ms == 598000);
    CHECK_FALSE(s.over());
    // White to move: White's clock runs, Black's waits.
    const auto later = s.told + 3s;
    CHECK(s.clockNow(true, later) == 595000);
    CHECK(s.clockNow(false, later) == 597000);
    REQUIRE(readBoardLine(R"({"type": "gameState", "moves": "e2e4 e7e5 g1f3", "wtime": 596000, "btime": 597000,
        "winc": 5000, "binc": 5000, "status": "started", "bdraw": true})",
                          s));
    CHECK(s.moves.size() == 3);
    CHECK(s.black_draw);
    CHECK_FALSE(s.white_draw);
    CHECK(s.clockNow(false, s.told + 1s) == 596000);
    REQUIRE(
        readBoardLine(R"({"type": "chatLine", "room": "player", "username": "Partner_3", "text": "Good luck"})", s));
    REQUIRE(s.chat.size() == 1);
    CHECK(s.chat[0].text == "Good luck");
    REQUIRE(readBoardLine(R"({"type": "opponentGone", "gone": true, "claimWinInSeconds": 51})", s));
    CHECK(s.opponent_gone);
    CHECK(s.claim_in_s == 51);
    REQUIRE(readBoardLine(R"({"type": "opponentGone", "gone": false})", s));
    CHECK_FALSE(s.opponent_gone);
    CHECK(s.claim_in_s == -1);
    // The board: the moves in SAN, the tags as Lichess writes them.
    auto game = replayOf(s);
    REQUIRE(game.moves.size() == 3);
    CHECK(game.moves[2].san == "Nf3");
    CHECK(game.moves[2].mover == chess::Color::WHITE);
    CHECK(game.header("White") == "RS_Sample");
    CHECK(game.header("BlackElo") == "1836");
    CHECK(game.header("BlackTitle") == "FM");
    CHECK(game.header("TimeControl") == "600+5");
    CHECK(game.header("Site") == "https://lichess.org/gAme1234");
    CHECK(game.header("Event") == "Rated rapid game");
    CHECK(game.result == "*");
    // The end: the clocks stop; the result; the first moves' expiration.
    REQUIRE(readBoardLine(R"({"type": "gameState", "moves": "e2e4 e7e5 g1f3", "wtime": 596000, "btime": 590000,
        "winc": 5000, "binc": 5000, "status": "resign", "winner": "white"})",
                          s));
    CHECK(s.over());
    CHECK(s.clockNow(false, s.told + 5s) == 590000);
    game = replayOf(s);
    CHECK(game.result == "1-0");
    BoardState first;
    REQUIRE(readBoardLine(R"({"type": "gameFull", "id": "c0rr0001", "speed": "correspondence", "daysPerTurn": 3,
        "white": {"name": "A"}, "black": {"name": "B"}, "initialFen": "8/8/8/4k3/8/8/4P3/4K3 b - - 0 1",
        "state": {"type": "gameState", "moves": "", "wtime": 259200000, "btime": 259200000, "status": "started",
                  "expiration": {"idleMillis": 1000, "millisToMove": 30000}}})",
                          first));
    CHECK_FALSE(first.live());
    CHECK(first.days == 3);
    CHECK(first.first_move_ms == 29000);
    // From a position where Black moves first, by correspondence: Black's time runs from the start.
    CHECK(first.clockNow(false, first.told + 2s) == 259198000);
    CHECK(first.clockNow(true, first.told + 2s) == 259200000);
    CHECK(replayOf(first).start_fen == "8/8/8/4k3/8/8/4P3/4K3 b - - 0 1");
    CHECK(replayOf(first).header("TimeControl") == "-");
    // A move that is not legal ends the board there.
    first.moves = {"e5e4", "e1e8"};
    CHECK(replayOf(first).moves.size() == 1);
}

TEST_CASE("Lichess board: the requests of a game and of a challenge, and Lichess's reasons") {
    std::mutex mutex;
    std::vector<Seen> seen;
    const auto port = freePort();
    ScriptedServer server(port, [&](const std::string& head, net::TcpConnection& c) {
        const auto s = readRequest(head, c);
        {
            std::lock_guard lock(mutex);
            seen.push_back(s);
        }
        if (s.path.find("/move/e2e5") != std::string::npos) {
            c.sendAll(reply(400, R"({"error": "Not your turn, or game already over"})"));
            return;
        }
        if (s.path.find("/missing/") != std::string::npos) {
            c.sendAll(reply(404, R"({"error": "Not found"})"));
            return;
        }
        c.sendAll(reply(200, R"({"ok": true})"));
    });
    REQUIRE(server.ok());
    servers().api     = fmt::format("http://127.0.0.1:{}", port);
    const auto played = boardMove("tok", "gAme1234", "e2e4", true);
    CHECK(played.ok);
    const auto refused = boardMove("tok", "gAme1234", "e2e5");
    CHECK_FALSE(refused.ok);
    CHECK(refused.status == 400);
    CHECK(refused.net == NetError::Server);
    CHECK(describe(refused) == "Not your turn, or game already over");  // Lichess's words
    CHECK(boardAction("tok", "gAme1234", "resign").ok);
    CHECK(boardAction("tok", "gAme1234", "draw/no").ok);
    CHECK(boardAction("tok", "gAme1234", "claim-victory").ok);
    CHECK(boardChat("tok", "gAme1234", "Thank you for the game!").ok);
    CHECK(acceptChallenge("tok", "qUfdwN17").ok);
    CHECK(declineChallenge("tok", "qUfdwN18", "tooSlow").ok);
    CHECK(cancelChallenge("tok", "qUfdwN19").ok);
    const auto missing = boardAction("tok", "missing", "abort");
    CHECK(missing.status == 404);
    CHECK(missing.net == NetError::NotFound);
    CHECK(describe(missing) == "Lichess does not know it");
    servers() = Servers{};
    std::lock_guard lock(mutex);
    REQUIRE(seen.size() == 10);
    CHECK(seen[0].line == "POST /api/board/game/gAme1234/move/e2e4?offeringDraw=true HTTP/1.1");
    CHECK(seen[0].authorization == "Bearer tok");
    CHECK(seen[1].path == "/api/board/game/gAme1234/move/e2e5");
    CHECK(seen[2].path == "/api/board/game/gAme1234/resign");
    CHECK(seen[3].path == "/api/board/game/gAme1234/draw/no");
    CHECK(seen[4].path == "/api/board/game/gAme1234/claim-victory");
    CHECK(seen[5].path == "/api/board/game/gAme1234/chat");
    CHECK(seen[5].body == "room=player&text=Thank+you+for+the+game%21");
    CHECK(seen[6].path == "/api/challenge/qUfdwN17/accept");
    CHECK(seen[7].path == "/api/challenge/qUfdwN18/decline");
    CHECK(seen[7].body == "reason=tooSlow");
    CHECK(seen[8].path == "/api/challenge/qUfdwN19/cancel");
}

TEST_CASE("Lichess board: a real-time seek held open, called back by closing it; one Lichess closes; one refused") {
    std::mutex mutex;
    std::vector<Seen> seen;
    std::atomic<bool> closed_by_client{false};
    std::atomic<int> mode{0};  // 0 held until the client closes, 1 taken at once, 2 refused
    const auto port = freePort();
    ScriptedServer server(port, [&](const std::string& head, net::TcpConnection& c) {
        const auto s = readRequest(head, c);
        {
            std::lock_guard lock(mutex);
            seen.push_back(s);
        }
        if (mode == 2) {
            c.sendAll(reply(400, R"({"error": {"time": ["Invalid value"]}})"));
            return;
        }
        if (s.body.find("days=") != std::string::npos) {
            c.sendAll(reply(200, R"({"id": "c0rrSeek"})"));
            return;
        }
        c.sendAll(STREAM_HEAD);
        if (mode == 1) {  // someone takes it: Lichess closes the request
            c.sendAll(chunk("\n") + LAST_CHUNK);
            return;
        }
        closed_by_client = waitClosed(c);
    });
    REQUIRE(server.ok());
    servers().api = fmt::format("http://127.0.0.1:{}", port);
    {
        Seek seek("tok", SeekAsk{{10, 5, 0}, true, 1650, 1950});
        CHECK(waitUntil([&] { return seek.state() == Seek::State::Open; }));
    }  // called back
    CHECK(waitUntil([&] { return closed_by_client.load(); }));
    mode = 1;
    {
        Seek seek("tok", SeekAsk{{30, 20, 0}, false, 0, 0});
        CHECK(waitUntil([&] { return seek.state() == Seek::State::Closed; }));
        CHECK_FALSE(seek.failure().failed());
    }
    {
        Seek seek("tok", SeekAsk{{0, 0, 3}, true, 0, 0});
        CHECK(waitUntil([&] { return seek.state() == Seek::State::Closed; }));
        CHECK(seek.id() == "c0rrSeek");
    }
    mode = 2;
    {
        Seek seek("tok", SeekAsk{{10, 0, 0}, true, 0, 0});
        CHECK(waitUntil([&] { return seek.state() == Seek::State::Failed; }));
        CHECK(seek.failure().net == NetError::Server);
        CHECK(describe(seek.failure()) == "time: Invalid value");  // Lichess's words, with the field they are of
    }
    servers() = Servers{};
    std::lock_guard lock(mutex);
    REQUIRE(seen.size() == 4);
    CHECK(seen[0].line == "POST /api/board/seek HTTP/1.1");
    CHECK(seen[0].authorization == "Bearer tok");
    CHECK(seen[0].body == "rated=true&variant=standard&time=10&increment=5&ratingRange=1650-1950");
    CHECK(seen[1].body == "rated=false&variant=standard&time=30&increment=20");
    CHECK(seen[2].body == "rated=true&variant=standard&days=3");
}

TEST_CASE("Lichess board: a challenge kept alive until Lichess says how it ended, or called back") {
    std::mutex mutex;
    std::vector<Seen> seen;
    std::atomic<int> mode{0};  // 0 declined, 1 held until the client closes
    std::atomic<bool> closed_by_client{false};
    const auto port = freePort();
    ScriptedServer server(port, [&](const std::string& head, net::TcpConnection& c) {
        const auto s = readRequest(head, c);
        {
            std::lock_guard lock(mutex);
            seen.push_back(s);
        }
        if (s.path.find("/cancel") != std::string::npos) {
            c.sendAll(reply(200, R"({"ok": true})"));
            return;
        }
        if (s.path == "/api/challenge/nobody") {
            c.sendAll(reply(400, R"({"error": "User not found"})"));
            return;
        }
        if (s.body.find("days=") != std::string::npos) {
            c.sendAll(reply(200, R"({"id": "c0rrChal", "status": "created", "direction": "out"})"));
            return;
        }
        c.sendAll(STREAM_HEAD);
        c.sendAll(ndjson(R"({"id": "chAl0001", "status": "created", "direction": "out"})"));
        if (mode == 0) {
            c.sendAll(chunk("\n") + ndjson(R"({"done": "declined"})") + LAST_CHUNK);
            return;
        }
        closed_by_client = waitClosed(c);
    });
    REQUIRE(server.ok());
    servers().api = fmt::format("http://127.0.0.1:{}", port);
    {
        ChallengeOut out("tok", ChallengeAsk{"Player_B", {5, 3, 0}, false, "random"});
        CHECK(waitUntil([&] { return out.state() == ChallengeOut::State::Done; }));
        CHECK(out.id() == "chAl0001");
        CHECK(out.done() == "declined");
    }
    mode = 1;
    {
        ChallengeOut out("tok", ChallengeAsk{"Player_B", {10, 5, 0}, true, "white"});
        CHECK(waitUntil([&] { return !out.id().empty(); }));
        CHECK(out.state() == ChallengeOut::State::Waiting);
        out.cancel();
        CHECK(waitUntil([&] { return out.state() == ChallengeOut::State::Done; }));
        CHECK(out.done() == "canceled");
    }
    CHECK(waitUntil([&] { return closed_by_client.load(); }));
    {
        // Accepted, as the event stream told: its request closed, nothing cancelled.
        ChallengeOut out("tok", ChallengeAsk{"Player_B", {10, 5, 0}, true, "white"});
        CHECK(waitUntil([&] { return !out.id().empty(); }));
        out.settle("accepted");
        CHECK(waitUntil([&] { return out.state() == ChallengeOut::State::Done; }));
        CHECK(out.done() == "accepted");
    }
    {
        ChallengeOut out("tok", ChallengeAsk{"Player_C", {0, 0, 3}, false, "black"});
        CHECK(waitUntil([&] { return out.state() == ChallengeOut::State::Waiting; }));
        CHECK(out.id() == "c0rrChal");
    }  // called back on the way out
    {
        ChallengeOut out("tok", ChallengeAsk{"nobody", {5, 3, 0}, false, "random"});
        CHECK(waitUntil([&] { return out.state() == ChallengeOut::State::Failed; }));
        CHECK(out.failure().net == NetError::Server);
        CHECK(describe(out.failure()) == "User not found");
    }
    servers() = Servers{};
    std::lock_guard lock(mutex);
    std::vector<std::string> paths;
    for (const auto& s : seen) paths.push_back(s.path);
    CHECK(seen[0].path == "/api/challenge/Player_B");
    CHECK(seen[0].body ==
          "rated=false&color=random&variant=standard&clock.limit=300&clock.increment=3&"
          "keepAliveStream=true");
    CHECK(std::count(paths.begin(), paths.end(), "/api/challenge/chAl0001/cancel") == 1);  // not when accepted
    CHECK(std::count(paths.begin(), paths.end(), "/api/challenge/c0rrChal/cancel") == 1);
    const auto corr = std::find(paths.begin(), paths.end(), "/api/challenge/Player_C");
    REQUIRE(corr != paths.end());
    CHECK(seen[static_cast<std::size_t>(corr - paths.begin())].body ==
          "rated=false&color=black&variant=standard&days=3");
}

TEST_CASE("Lichess board: a game's stream heard, opened again after a cut, stopped at the game's end") {
    std::atomic<int> opened{0};
    const auto port = freePort();
    ScriptedServer server(port, [&](const std::string& head, net::TcpConnection& c) {
        const auto path = requestPath(head);
        if (path == "/api/board/game/stream/unknown1") {
            c.sendAll(reply(404, R"({"error": "Not found"})"));
            return;
        }
        if (path == "/api/board/game/gAme1234/chat") {  // the chat so far, asked at each opening
            c.sendAll(reply(
                200, R"([{"text": "Good luck", "user": "Partner_3"}, {"text": "You too", "user": "RS_Sample"}])"));
            return;
        }
        if (path != "/api/board/game/stream/gAme1234") {
            c.sendAll(reply(404, "{}"));
            return;
        }
        const int n = ++opened;
        c.sendAll(STREAM_HEAD);
        if (n == 1) {  // the game, a move, then the connection cut without its end
            c.sendAll(chunk(nlohmann::json::parse(GAME_FULL).dump() + "\n"));
            c.sendAll(ndjson(R"({"type": "gameState", "moves": "e2e4 e7e5 g1f3", "wtime": 596000, "btime": 597000,
                "winc": 5000, "binc": 5000, "status": "started"})"));
            return;
        }
        // Opened again: the whole game, then its end; Lichess closes the stream.
        auto full              = nlohmann::json::parse(GAME_FULL);
        full["state"]["moves"] = "e2e4 e7e5 g1f3 b8c6";
        c.sendAll(chunk(full.dump() + "\n"));
        c.sendAll(ndjson(R"({"type": "gameState", "moves": "e2e4 e7e5 g1f3 b8c6", "wtime": 590000, "btime": 580000,
            "winc": 5000, "binc": 5000, "status": "mate", "winner": "black"})") +
                  LAST_CHUNK);
    });
    REQUIRE(server.ok());
    servers().api = fmt::format("http://127.0.0.1:{}", port);
    {
        GameStream stream("tok", "gAme1234");
        BoardState state;
        uint64_t version = 0;
        bool saw_cut     = false;
        CHECK(waitUntil([&] {
            if (stream.state(state, version) && !state.connected && state.tries > 0) saw_cut = true;
            return state.ended;
        }));
        CHECK(saw_cut);
        CHECK(opened == 2);
        CHECK(state.moves.size() == 4);
        CHECK(state.status == "mate");
        CHECK(state.winner == "black");
        CHECK_FALSE(state.failure.failed());
        CHECK(state.white.name == "RS_Sample");
        // The chat as Lichess keeps it, not twice for having opened the stream twice.
        REQUIRE(state.chat.size() == 2);
        CHECK(state.chat[0].user == "Partner_3");
        CHECK(state.chat[1].text == "You too");
    }
    {
        GameStream stream("tok", "unknown1");
        BoardState state;
        uint64_t version = 0;
        CHECK(waitUntil([&] {
            (void)stream.state(state, version);
            return state.ended;
        }));
        CHECK(state.failure.net == NetError::NotFound);
        CHECK(describe(state.failure) == "Lichess has no such game");
    }
    servers() = Servers{};
}

TEST_CASE("Lichess board: each failure of a request, a seek, a challenge and a game's stream told by its kind") {
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
        CHECK(boardMove("tok", "gAme1234", "e2e4").net == net);
        CHECK_FALSE(boardMove("tok", "gAme1234", "e2e4").ok);
        CHECK(boardAction("tok", "gAme1234", "resign").net == net);
        CHECK(boardChat("tok", "gAme1234", "Hello").net == net);
        CHECK(acceptChallenge("tok", "qUfdwN17").net == net);
        CHECK(declineChallenge("tok", "qUfdwN17", "later").net == net);
        CHECK(cancelChallenge("tok", "qUfdwN17").net == net);
        CHECK(boardChatLines("tok", "gAme1234").net == net);
        Seek seek("tok", SeekAsk{{10, 5, 0}, true, 0, 0});  // held open in real time
        CHECK(waitUntil([&] { return seek.state() == Seek::State::Failed; }));
        CHECK(seek.failure().net == net);
        ChallengeOut out("tok", ChallengeAsk{"Player_B", {5, 3, 0}, false, "random"});
        CHECK(waitUntil([&] { return out.state() == ChallengeOut::State::Failed; }));
        CHECK(out.failure().net == net);
        GameStream stream("tok", "gAme1234");
        BoardState state;
        uint64_t version = 0;
        // Unknown to Lichess, or the token refused: it stops; otherwise tried again.
        CHECK(waitUntil([&] {
            (void)stream.state(state, version);
            return state.failure.net == net && (state.ended || state.tries > 0);
        }));
        const bool stops = net == NetError::NotFound || net == NetError::Unauthorized || net == NetError::Forbidden;
        CHECK(state.ended == stops);
    }
    // An answer that is not what Lichess sends: the chat's; a POST's answer is not read.
    server.answer(200, "<html>not JSON</html>");
    CHECK(boardChatLines("tok", "gAme1234").net == NetError::BadAnswer);
    CHECK(boardMove("tok", "gAme1234", "e2e4").ok);
    // Nothing listening: no answer.
    pointNowhere();
    const auto nobody = boardMove("tok", "gAme1234", "e2e4");
    CHECK(nobody.net == NetError::Offline);
    CHECK(describe(nobody) == "Lichess cannot be reached");
    CHECK(boardChatLines("tok", "gAme1234").net == NetError::Offline);
    {
        Seek seek("tok", SeekAsk{{10, 5, 0}, true, 0, 0});
        CHECK(waitUntil([&] { return seek.state() == Seek::State::Failed; }));
        CHECK(seek.failure().net == NetError::Offline);
        ChallengeOut out("tok", ChallengeAsk{"Player_B", {5, 3, 0}, false, "random"});
        CHECK(waitUntil([&] { return out.state() == ChallengeOut::State::Failed; }));
        CHECK(out.failure().net == NetError::Offline);
        GameStream stream("tok", "gAme1234");
        BoardState state;
        uint64_t version = 0;
        CHECK(waitUntil([&] {
            (void)stream.state(state, version);
            return state.tries > 0;
        }));
        CHECK(state.failure.net == NetError::Offline);
        CHECK_FALSE(state.ended);
    }
    // Cancelled while the host pauses after a 429: sent nowhere.
    pointAtPausedHost("lichess-board.invalid");
    std::atomic<bool> cancel{true};
    CHECK(boardMove("tok", "gAme1234", "e2e4", false, &cancel).net == NetError::Cancelled);
    CHECK(boardChatLines("tok", "gAme1234", &cancel).net == NetError::Cancelled);
    servers() = Servers{};
}

TEST_CASE("Lichess board: the event stream opened with the right to play, its challenges heard") {
    const auto port = freePort();
    ScriptedServer server(port, [&](const std::string& head, net::TcpConnection& c) {
        if (requestPath(head) != "/api/stream/event") {
            c.sendAll(reply(404, "{}"));
            return;
        }
        c.sendAll(STREAM_HEAD);
        c.sendAll(ndjson(R"({"type": "challenge", "challenge": {"id": "qUfdwN17", "challenger": {"name": "Player_F"},
            "destUser": {"name": "RS_Sample"}, "speed": "rapid", "timeControl": {"type": "clock", "limit": 900,
            "increment": 10}, "color": "random", "finalColor": "black", "direction": "in"},
            "compat": {"board": true}})"));
        (void)waitClosed(c);
    });
    REQUIRE(server.ok());
    servers().api = fmt::format("http://127.0.0.1:{}", port);
    {
        EventListener listener("tok");
        std::vector<GameEvent> heard;
        CHECK(waitUntil([&] {
            for (auto& e : listener.take()) heard.push_back(std::move(e));
            return heard.size() >= 2;
        }));
        CHECK(listener.connected());
        REQUIRE(heard.size() == 2);
        CHECK(heard[0].kind == GameEvent::Kind::Opened);
        CHECK(heard[1].kind == GameEvent::Kind::Challenge);
        CHECK(heard[1].challenge.id == "qUfdwN17");
        CHECK(*heard[1].challenge.userWhite());  // the challenger drew Black
    }
    servers() = Servers{};
}

TEST_CASE("Lichess board: the choices kept in the profile") {
    const auto dir = std::filesystem::temp_directory_path() / "fcg-lichess-board";
    std::filesystem::remove_all(dir);
    CHECK(readOnlinePlay(dir).seek_minutes == 10);  // nothing kept yet
    OnlinePlaySettings s;
    s.seek_minutes     = 30;
    s.seek_increment   = 20;
    s.seek_range       = 150;
    s.opponent         = "Player_B";
    s.challenge_days   = 3;
    s.challenge_colour = 1;
    s.scope_declined   = true;
    s.playing          = "gAme1234";
    REQUIRE(writeOnlinePlay(dir, s));
    const auto back = readOnlinePlay(dir);
    CHECK(back.seekClock() == ChallengeClock{30, 20, 0});
    CHECK(back.seek_range == 150);
    CHECK(back.opponent == "Player_B");
    CHECK(back.challengeClock() == ChallengeClock{5, 3, 3});
    CHECK(back.challenge_colour == 1);
    CHECK(back.scope_declined);
    CHECK(back.playing == "gAme1234");
    std::filesystem::remove_all(dir);
}
