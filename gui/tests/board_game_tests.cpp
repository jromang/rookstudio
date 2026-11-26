// A game between two engines of the library from a position of the Board page, played outside fastchess, against
// the mock engine: a pair with the colours reversed, a loss on time, a stop, the PGN file; the user against an
// engine; and the Play page's game (#83): the engine's openings, moves taken back, a draw offered, a game resumed.

#include <doctest/doctest.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <thread>

#include <tour/pgn_index.hpp>

#include "board_game.hpp"
#include "board_text.hpp"
#include "run.hpp"
#include "self_path.hpp"

using namespace fcg;
namespace fs = std::filesystem;
using namespace std::chrono_literals;

namespace {

std::string mockEngine() { return (fcg::test::selfPath().parent_path() / FCG_RANDOM_MOVER).string(); }

void setVar(const char* name, const char* value) {
#ifdef _WIN32
    _putenv_s(name, value ? value : "");
#else
    if (value)
        setenv(name, value, 1);
    else
        unsetenv(name);
#endif
}

EngineEntry engine(const std::string& name, int threads = 1) {
    EngineEntry e;
    e.name     = name;
    e.cmd      = mockEngine();
    e.protocol = "uci";
    if (threads > 1) e.options.emplace_back("Threads", std::to_string(threads));
    return e;
}

BoardMatch::State waitEnd(BoardMatch& match, std::chrono::seconds limit = 60s) {
    BoardMatch::State state;
    uint64_t version = 0;
    const auto until = std::chrono::steady_clock::now() + limit;
    while (std::chrono::steady_clock::now() < until) {
        match.state(state, version);
        if (!state.running) break;
        std::this_thread::sleep_for(10ms);
    }
    match.reap();
    return state;
}

// The state once `done` holds, or the last one after `limit`.
template <typename F>
BoardMatch::State waitFor(BoardMatch& match, F done, std::chrono::seconds limit = 30s) {
    BoardMatch::State state;
    uint64_t version = 0;
    const auto until = std::chrono::steady_clock::now() + limit;
    while (std::chrono::steady_clock::now() < until) {
        match.state(state, version);
        if (done(state)) break;
        std::this_thread::sleep_for(5ms);
    }
    return state;
}

// A legal move of the game's position, in UCI.
std::string anyMove(const BoardMatch::State& state, const std::string& fen) {
    chess::Board board;
    board.setFen(fen);
    for (const auto& m : state.current.moves) board.makeMove<true>(m.move);
    chess::Movelist moves;
    chess::movegen::legalmoves(moves, board);
    return moves.empty() ? std::string() : chess::uci::moveToUci(moves[0]);
}

fs::path freshDir(const char* name) {
    const auto dir =
        fs::temp_directory_path() /
        (std::string(name) + "-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(dir);
    return dir;
}

}  // namespace

TEST_CASE("board game: a pair of games with the colours reversed, every move legal, both in the PGN file") {
    if (!fs::is_regular_file(mockEngine())) return;
    const auto dir = freshDir("fcg-board-game");
    const auto pgn = dir / "games.pgn";

    BoardMatch match;
    BoardMatchSetup setup;
    setup.first        = "Alpha";
    setup.second       = "Bravo";
    setup.first_clock  = {10, 0.1};
    setup.second_clock = {10, 0.1};
    setup.pair         = true;
    setup.fen          = "r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 3";
    std::string error;
    const int before = boardThreads();
    REQUIRE(match.start(engine("Alpha", 3), engine("Bravo"), setup, pgn, error));
    CHECK(boardThreads() == before + 3);  // the larger of the two: they take turns
    const auto state = waitEnd(match);
    CHECK(boardThreads() == before);
    CHECK(state.error.empty());
    REQUIRE(state.finished.size() == 2);
    CHECK(state.finished[0].header("White") == "Alpha");
    CHECK(state.finished[0].header("Black") == "Bravo");
    CHECK(state.finished[1].header("White") == "Bravo");
    CHECK(state.finished[1].header("Round") == "2");
    CHECK(state.finished[0].header("TimeControl") == "10+0.1");
    for (const auto& g : state.finished) {
        CAPTURE(g.final_comment);
        CHECK((g.result == "1-0" || g.result == "0-1" || g.result == "1/2-1/2"));
        CHECK(!g.final_comment.empty());
        CHECK(g.start_fen == setup.fen);
        REQUIRE(!g.moves.empty());
        CHECK(g.moves[0].mover == chess::Color::WHITE);
        CHECK(g.moves[0].time_ms >= 0);
    }

    // Both games in the file, read back move for move.
    std::ifstream in(pgn, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();  // Windows removes no file still open
    std::istringstream stream(text);
    const auto index = tour::indexPgn(stream);
    REQUIRE(index.size() == 2);
    for (std::size_t i = 0; i < 2; ++i) {
        const auto& span = index.games[i].span;
        std::string read_error;
        const auto game = readPgnGame(text.substr(span.offset, span.length), read_error);
        REQUIRE(game);
        CHECK(game->moves.size() == state.finished[i].moves.size());
        CHECK(game->result == state.finished[i].result);
        CHECK(game->header("Termination") == state.finished[i].final_comment);
    }
    fs::remove_all(dir);
}

TEST_CASE("board game: an engine that thinks past its clock loses on time; a stop ends the game without a result") {
    if (!fs::is_regular_file(mockEngine())) return;
    const auto dir = freshDir("fcg-board-game-time");
    BoardMatchSetup setup;
    setup.first        = "Slow";
    setup.second       = "Bravo";
    setup.first_clock  = {0.1, 0};
    setup.second_clock = {60, 0};
    setup.fen          = std::string(chess::constants::STARTPOS);
    std::string error;
    {
        setVar("FCG_MOCK_MOVE_MS", "400");
        BoardMatch match;
        REQUIRE(match.start(engine("Slow"), engine("Bravo"), setup, dir / "time.pgn", error));
        const auto state = waitEnd(match);
        setVar("FCG_MOCK_MOVE_MS", nullptr);
        REQUIRE(state.finished.size() == 1);
        CHECK(state.finished[0].result == "0-1");
        CHECK(state.finished[0].final_comment.find("Slow") != std::string::npos);
        CHECK(state.finished[0].moves.empty());
    }
    {
        setVar("FCG_MOCK_MOVE_MS", "200");
        setup.first_clock = {60, 0};
        BoardMatch match;
        REQUIRE(match.start(engine("Slow"), engine("Bravo"), setup, dir / "stop.pgn", error));
        std::this_thread::sleep_for(700ms);
        const auto asked = std::chrono::steady_clock::now();
        match.stop();
        setVar("FCG_MOCK_MOVE_MS", nullptr);
        CHECK(std::chrono::steady_clock::now() - asked < 5s);
        CHECK_FALSE(match.running());
        BoardMatch::State state;
        uint64_t version = 0;
        match.state(state, version);
        REQUIRE(state.finished.size() == 1);
        CHECK(state.finished[0].result == "*");
    }
    // An engine without its program.
    {
        BoardMatch match;
        auto missing = engine("Gone");
        missing.cmd  = (dir / "no-such-engine").string();
        CHECK_FALSE(match.start(missing, engine("Bravo"), setup, dir / "x.pgn", error));
        CHECK(error.find("no-such-engine") != std::string::npos);
    }
    CHECK(clockText({60, 0.6}) == "1+0.6");
    CHECK(clockText({2400, 0}) == "40+0");
    CHECK(clockText({30, 0.2}) == "0:30+0.2");
    CHECK(clockText({90, 1}) == "1:30+1");
    fs::remove_all(dir);
}

TEST_CASE("board game: the user against an engine, their moves at their turn only, the engine's scores kept back") {
    if (!fs::is_regular_file(mockEngine())) return;
    const auto dir = freshDir("fcg-board-game-human");
    BoardMatchSetup setup;
    setup.first        = "You";
    setup.second       = "Bravo";
    setup.first_clock  = {60, 0};
    setup.second_clock = {60, 0};
    setup.fen          = std::string(chess::constants::STARTPOS);
    setup.human        = 0;
    EngineEntry you;
    you.name = "You";  // no program: the user
    std::string error;

    setVar("FCG_MOCK_MOVE_MS", "300");
    BoardMatch match;
    const int before = boardThreads();
    REQUIRE(match.start(you, engine("Bravo", 2), setup, dir / "human.pgn", error));
    CHECK(boardThreads() == before + 2);  // the engine's threads only

    // Their turn: the move is taken.
    auto state = waitFor(match, [](const auto& s) { return s.thinking == chess::Color::WHITE; });
    REQUIRE(state.you == chess::Color::WHITE);
    match.play("e2e4");
    // The engine's turn: a move given now is not taken.
    state = waitFor(match, [](const auto& s) { return s.thinking == chess::Color::BLACK; });
    REQUIRE(state.current.moves.size() == 1);
    CHECK(state.current.moves[0].san == "e4");
    match.play("d2d4");
    state =
        waitFor(match, [](const auto& s) { return s.thinking == chess::Color::WHITE && s.current.moves.size() == 2; });
    REQUIRE(state.current.moves.size() == 2);
    CHECK(state.current.moves[1].mover == chess::Color::BLACK);
    // The engine's score and depth are not shown while the user plays.
    CHECK_FALSE(state.current.moves[1].score.has_value());
    CHECK(state.current.moves[1].depth == 0);
    CHECK(state.info.empty());

    // Resigned while the engine thinks: the user's side loses at once, the engine stopped.
    match.play(anyMove(state, setup.fen));
    state = waitFor(match, [](const auto& s) { return s.thinking == chess::Color::BLACK; });
    match.resign();
    state = waitEnd(match);
    setVar("FCG_MOCK_MOVE_MS", nullptr);
    CHECK(boardThreads() == before);
    REQUIRE(state.finished.size() == 1);
    const auto& game = state.finished[0];
    CHECK(game.result == "0-1");
    CHECK(game.final_comment.find("You") != std::string::npos);
    CHECK(game.header("White") == "You");
    REQUIRE(game.moves.size() >= 3);
    CHECK(game.moves[1].score.has_value());  // the finished game has them

    // A pair: the user has Black, then White; their clock runs out in the first game.
    setup.human        = 1;
    setup.first        = "Bravo";
    setup.second       = "You";
    setup.second_clock = {0.3, 0};
    setup.pair         = true;
    BoardMatch pair;
    REQUIRE(pair.start(engine("Bravo"), you, setup, dir / "pair.pgn", error));
    state = waitFor(pair, [](const auto& s) { return s.you.has_value(); });
    CHECK(state.you == chess::Color::BLACK);
    state = waitFor(pair, [](const auto& s) { return s.game == 1 && s.you == chess::Color::WHITE; });
    REQUIRE(state.game == 1);
    REQUIRE(state.finished.size() == 1);
    CHECK(state.finished[0].result == "1-0");
    CHECK(state.finished[0].final_comment.find("on time") != std::string::npos);
    pair.stop();
    fs::remove_all(dir);
}

TEST_CASE("board game: against the engine of the Play page, its openings first, moves taken back, a draw declined") {
    if (!fs::is_regular_file(mockEngine())) return;
    const auto dir = freshDir("fcg-board-game-play");
    BoardMatchSetup setup;
    setup.first        = "You";
    setup.second       = "Bravo";
    setup.first_clock  = {60, 0};
    setup.second_clock = {60, 0};
    setup.fen          = std::string(chess::constants::STARTPOS);
    setup.human        = 0;
    setup.event        = "RookStudio game against an engine";
    setup.tags         = {{"Strength", "UCI_Elo 1500"}};
    // Its openings: c5 after e4, d6 after Nf3, nothing else.
    setup.book = [](const chess::Board& board) -> std::optional<std::string> {
        const auto fen = board.getFen();
        if (fen.rfind("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b", 0) == 0) return "c7c5";
        if (fen.rfind("rnbqkbnr/pp1ppppp/8/2p5/4P3/5N2/PPPP1PPP/RNBQKB1R b", 0) == 0) return "d7d6";
        return std::nullopt;
    };
    EngineEntry you;
    you.name = "You";
    std::string error;
    setVar("FCG_MOCK_MOVE_MS", "300");  // its own moves take a while: one is taken back while it thinks
    BoardMatch match;
    REQUIRE(match.start(you, engine("Bravo"), setup, {}, error));
    const auto your_turn = [](std::size_t moves) {
        return [moves](const BoardMatch::State& s) {
            return s.current.moves.size() == moves && s.thinking == chess::Color::WHITE;
        };
    };
    auto state = waitFor(match, your_turn(0));
    CHECK(state.current.header("Event") == "RookStudio game against an engine");
    CHECK(state.current.header("Strength") == "UCI_Elo 1500");
    match.play("e2e4");
    state = waitFor(match, your_turn(2));
    REQUIRE(state.current.moves.size() == 2);
    CHECK(state.current.moves[1].san == "c5");
    CHECK(state.current.moves[1].time_ms == 0);  // from its openings: at once, taking nothing from its clock
    CHECK(state.clock_ms[1] == 60000);
    CHECK(state.last_book == 1);
    CHECK_FALSE(state.book_out);
    match.play("g1f3");
    state = waitFor(match, your_turn(4));
    REQUIRE(state.current.moves.size() == 4);
    CHECK(state.current.moves[3].san == "d6");
    CHECK(state.last_book == 3);
    // Out of its openings: the engine plays.
    match.play("d2d4");
    state = waitFor(match, your_turn(6));
    REQUIRE(state.current.moves.size() == 6);
    CHECK(state.book_out);
    CHECK(state.last_book == 3);
    REQUIRE(state.clocks_before.size() == 6);

    // Taken back at the user's turn: their move and the engine's answer, the clocks as before it, its openings asked
    // again.
    const auto before = state.clocks_before[4];
    match.takeBack();
    state = waitFor(match,
                    [](const BoardMatch::State& s) { return s.takebacks == 1 && s.thinking == chess::Color::WHITE; });
    CHECK(state.current.moves.size() == 4);
    CHECK(state.clock_ms == before);
    CHECK(state.clocks_before.size() == 4);
    CHECK_FALSE(state.book_out);

    // A draw offered so early: declined with its move.
    match.offerDraw();
    state = waitFor(match, [](const BoardMatch::State& s) { return s.draw_offered; });
    CHECK(state.draw_offered);
    match.play("d2d4");
    state = waitFor(match, your_turn(6));
    REQUIRE(state.current.moves.size() == 6);
    CHECK_FALSE(state.draw_offered);
    CHECK(state.draw_declined == 5);

    // Taken back while the engine thinks: the user's move alone, the engine's search stopped.
    match.play(anyMove(state, setup.fen));
    state = waitFor(match, [](const BoardMatch::State& s) {
        return s.current.moves.size() == 7 && s.thinking == chess::Color::BLACK;
    });
    match.takeBack();
    state = waitFor(match,
                    [](const BoardMatch::State& s) { return s.takebacks == 2 && s.thinking == chess::Color::WHITE; });
    CHECK(state.current.moves.size() == 6);
    match.stop();
    setVar("FCG_MOCK_MOVE_MS", nullptr);
    uint64_t version = 0;
    match.state(state, version);
    REQUIRE(state.finished.size() == 1);
    CHECK(state.finished[0].result == "*");
    CHECK(state.finished[0].header("Strength") == "UCI_Elo 1500");
    // No PGN file asked for: none written.
    CHECK(fs::is_empty(dir));
    fs::remove_all(dir);
}

TEST_CASE("board game: a game resumed where it was left; a draw taken once 20 moves are played") {
    if (!fs::is_regular_file(mockEngine())) return;
    const auto dir = freshDir("fcg-board-game-resume");
    // 40 plies played at random from the start, none ending the game.
    ReplayGame so_far;
    so_far.start_fen = std::string(chess::constants::STARTPOS);
    so_far.headers   = {{"Event", "Resumed"}, {"White", "You"}, {"Black", "Bravo"}};
    chess::Board board;
    std::mt19937 rng(83);
    while (so_far.moves.size() < 40) {
        chess::Movelist legal;
        chess::movegen::legalmoves(legal, board);
        REQUIRE(legal.size() > 0);
        const auto move = legal[static_cast<int>(rng() % legal.size())];
        auto after      = board;
        after.makeMove<true>(move);
        if (after.isGameOver().second != chess::GameResult::NONE) continue;
        ReplayMove m;
        m.move    = move;
        m.mover   = board.sideToMove();
        m.san     = chess::uci::moveToSan(board, move);
        m.time_ms = 100;
        so_far.moves.push_back(m);
        board = after;
    }
    BoardMatchSetup setup;
    setup.first                = "You";
    setup.second               = "Bravo";
    setup.first_clock          = {60, 0};
    setup.second_clock         = {60, 0};
    setup.fen                  = so_far.start_fen;
    setup.human                = 0;
    setup.resume               = so_far;
    setup.resume_clock_ms      = {50000, 40000};
    setup.resume_clocks_before = std::vector<std::array<int64_t, 2>>(40, {60000, 60000});
    setup.resume_takebacks     = 3;
    EngineEntry you;
    you.name = "You";
    std::string error;
    BoardMatch match;
    REQUIRE(match.start(you, engine("Bravo"), setup, dir / "resumed.pgn", error));
    auto state = waitFor(match, [](const BoardMatch::State& s) { return s.thinking == chess::Color::WHITE; });
    CHECK(state.current.moves.size() == 40);
    CHECK(state.current.header("Event") == "Resumed");
    CHECK(state.clock_ms == std::array<int64_t, 2>{50000, 40000});
    CHECK(state.takebacks == 3);
    // The mock engine says 0.00 from its side: after move 20, it takes the draw.
    match.offerDraw();
    match.play(anyMove(state, setup.fen));
    state = waitEnd(match);
    REQUIRE(state.finished.size() == 1);
    CHECK(state.finished[0].result == "1/2-1/2");
    CHECK(state.finished[0].final_comment.find("agreement") != std::string::npos);
    CHECK(state.finished[0].moves.size() == 41);
    CHECK(fs::is_regular_file(dir / "resumed.pgn"));
    fs::remove_all(dir);
}
