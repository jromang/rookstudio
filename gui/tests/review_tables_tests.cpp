// Lichess's tablebase for a game's review: its answers taught to the tables, the positions asked once each and kept,
// the review held to them. On the ending of So–Shankland, U.S. Championship 2026: after 65...Kxh4 only 66.Rb5 wins.

#include <doctest/doctest.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

#include <fmt/include/fmt/format.h>

#include "board_text.hpp"
#include "free_ports.hpp"
#include "https.hpp"
#include "online_cache.hpp"
#include "review_bridge.hpp"
#include "review_tables.hpp"
#include "scripted_server.hpp"

using namespace fcg;
using namespace fcg::test;
namespace fs = std::filesystem;
using namespace std::chrono_literals;

namespace {

constexpr const char* BEFORE_KXH4 = "8/8/6p1/R1K3P1/P5kP/8/8/1r6 b - - 0 65";
constexpr const char* AFTER_KXH4  = "8/8/6p1/R1K3P1/P6k/8/8/1r6 w - - 0 66";

// What Lichess says after 65...Kxh4: White wins, by 66.Rb5 alone (a few of the other moves, drawing).
const char* const ANSWER =
    R"({"checkmate":false,"stalemate":false,"insufficient_material":false,"dtz":25,"dtm":null,"category":"win",)"
    R"("moves":[{"uci":"a5b5","san":"Rb5","zeroing":false,"checkmate":false,"stalemate":false,"dtz":-24,)"
    R"("dtm":null,"category":"loss"},{"uci":"a5a6","san":"Ra6","zeroing":false,"checkmate":false,)"
    R"("stalemate":false,"dtz":0,"dtm":null,"category":"draw"},{"uci":"c5c4","san":"Kc4","zeroing":false,)"
    R"("checkmate":false,"stalemate":false,"dtz":0,"dtm":null,"category":"draw"}]})";

lichess::Tablebase answerAfterKxh4() {
    lichess::Tablebase t;
    t.outcome = lichess::Outcome::Win;
    t.moves   = {{"a5b5", "Rb5", lichess::Outcome::Loss},
                 {"a5a6", "Ra6", lichess::Outcome::Draw},
                 {"c5c4", "Kc4", lichess::Outcome::Draw}};
    return t;
}

ReplayGame ending() {
    std::string error;
    const auto text = readBoardText(fmt::format("[FEN \"{}\"]\n[SetUp \"1\"]\n\n65... Kxh4 66. Rb5 *\n", BEFORE_KXH4));
    REQUIRE(text.kind == BoardText::Kind::Games);
    auto game = readPgnGame(text.pgn, error);
    REQUIRE_MESSAGE(game, error);
    return *game;
}

commentary::Move move(const commentary::Position& p, const char* uci) { return *p.parseUci(uci); }

void answer(net::TcpConnection& c, int status, const std::string& body) {
    c.sendAll(fmt::format("HTTP/1.1 {} X\r\nContent-Type: application/json\r\nContent-Length: {}\r\nConnection: "
                          "close\r\n\r\n",
                          status, body.size()) +
              body);
}

}  // namespace

TEST_CASE("review tables: Lichess's outcomes as the tables count them") {
    using commentary::Verdict;
    using lichess::Outcome;
    CHECK(verdictOf(Outcome::Win) == Verdict::Win);
    CHECK(verdictOf(Outcome::MaybeWin) == Verdict::Win);
    CHECK(verdictOf(Outcome::CursedWin) == Verdict::Draw);  // the fifty-move rule spoils it
    CHECK(verdictOf(Outcome::Draw) == Verdict::Draw);
    CHECK(verdictOf(Outcome::BlessedLoss) == Verdict::Draw);
    CHECK(verdictOf(Outcome::MaybeLoss) == Verdict::Loss);
    CHECK(verdictOf(Outcome::Loss) == Verdict::Loss);
    CHECK_FALSE(verdictOf(Outcome::Unknown));
}

TEST_CASE("review tables: an answer taught, the position and its moves") {
    const auto pos = *commentary::Position::fromFen(AFTER_KXH4);
    commentary::Tablebases tables;
    CHECK(tables.wants(pos));
    REQUIRE(teachTables(tables, pos, answerAfterKxh4()));
    CHECK(tables.verdict(pos) == commentary::Verdict::Win);
    // A move's outcome is for the side that plays it: Lichess gives the other side's.
    CHECK(tables.after(pos, move(pos, "a5b5")) == commentary::Verdict::Win);
    CHECK(tables.after(pos, move(pos, "c5c4")) == commentary::Verdict::Draw);
    CHECK(tables.best(pos) == move(pos, "a5b5"));
    CHECK_FALSE(tables.wants(pos));

    // What tells nothing teaches nothing: an unknown position, a move that is not one here.
    const auto other = *commentary::Position::fromFen(BEFORE_KXH4);
    commentary::Tablebases fresh;
    lichess::Tablebase unknown;
    CHECK_FALSE(teachTables(fresh, other, unknown));
    auto wrong     = answerAfterKxh4();
    wrong.moves[0] = {"e2e4", "e4", lichess::Outcome::Loss};
    CHECK_FALSE(teachTables(fresh, pos, wrong));
    CHECK_FALSE(fresh.verdict(pos));
}

TEST_CASE("review tables: the positions asked, and the review held to the answers") {
    const auto game = ending();
    const auto line = mainLineOf(game, false);
    REQUIRE(line);
    REQUIRE(line->positions().size() == 3);
    commentary::Tablebases tables;
    // Eight men before 65...Kxh4: not for the seven-man tables.
    auto wanted = tablePositions(*line, tables);
    REQUIRE(wanted.size() == 2);
    CHECK(wanted[0].index == 1);
    CHECK(wanted[0].fen == AFTER_KXH4);
    CHECK(wanted[1].index == 2);
    REQUIRE(teachTables(tables, line->positions()[1], answerAfterKxh4()));
    wanted = tablePositions(*line, tables);
    REQUIRE(wanted.size() == 1);
    CHECK(wanted[0].index == 2);

    // The engine prefers 66.Kc4 by a hair; the tables know only 66.Rb5 wins.
    GameReview review;
    review.engine = "Engine";
    review.positions.push_back({fastchess::Score{fastchess::ScoreType::CP, -80}, "b1a1", 20, 100, "b1a1", {}});
    review.positions.push_back({fastchess::Score{fastchess::ScoreType::CP, 88}, "c5c4", 20, 100, "c5c4", {}});
    review.positions.push_back({fastchess::Score{fastchess::ScoreType::CP, -90}, "b1a1", 20, 100, "b1a1", {}});
    const auto plain = commentaryReview(game, false, review);
    REQUIRE(plain);
    REQUIRE(plain->report.get(2));
    CHECK_FALSE(plain->report.get(2)->only_move);
    CHECK_FALSE(plain->report.get(2)->played_best);
    const auto held = commentaryReview(game, false, review, {}, &tables);
    REQUIRE(held);
    REQUIRE(held->analyses[1]);
    CHECK(held->analyses[1]->isExact());
    const auto* rb5 = held->report.get(2);
    REQUIRE(rb5);
    CHECK(rb5->played_best);
    CHECK(rb5->only_move);  // the only winning move, according to the tables
    // The engine found it too, its one line: the tables still tell it was the only one.
    review.positions[1] = {fastchess::Score{fastchess::ScoreType::CP, 87}, "a5b5", 20, 100, "a5b5", {}};
    const auto found    = commentaryReview(game, false, review, {}, &tables);
    REQUIRE(found);
    CHECK(found->report.get(2)->only_move);
    CHECK_FALSE(commentaryReview(game, false, review)->report.get(2)->only_move);
}

TEST_CASE("review tables: asked one at a time, kept in the profile, asked once") {
    std::mutex mutex;
    std::vector<std::string> asked;
    const uint16_t port = freePort();
    ScriptedServer server(port, [&](const std::string& head, net::TcpConnection& c) {
        const auto path = head.substr(4, head.find(' ', 4) - 4);
        {
            std::lock_guard lock(mutex);
            asked.push_back(path);
        }
        // The position after 65...Kxh4 is known; the next one, as Lichess would not have it, is not.
        if (path.find("8%2F8%2F6p1%2FR1K3P1%2FP6k") != std::string::npos)
            answer(c, 200, ANSWER);
        else
            answer(c, 200, R"({"category":"unknown","moves":[]})");
    });
    REQUIRE(server.ok());
    const auto base    = fmt::format("http://127.0.0.1:{}", port);
    lichess::servers() = lichess::Servers{base, base + "/tb", base + "/explorer"};
    HostGate::instance().setPause(50ms);
    const auto profile = fs::temp_directory_path() / "review-tables-profile";
    fs::remove_all(profile);

    const auto game = ending();
    const auto line = mainLineOf(game, false);
    REQUIRE(line);
    commentary::Tablebases tables;
    std::atomic<bool> cancel{false};
    std::atomic<int> done{0};
    const auto positions = tablePositions(*line, tables);
    CHECK(askTables(positions, *line, profile, tables, cancel, done).empty());
    CHECK(done == 2);
    CHECK(tables.verdict(line->positions()[1]) == commentary::Verdict::Win);
    {
        std::lock_guard lock(mutex);
        CHECK(asked.size() == 2);
    }
    // The answer kept for good; the unknown one not.
    CHECK(cachedTablebase(profile, AFTER_KXH4));
    CHECK_FALSE(cachedTablebase(profile, line->positions()[2].fen()));

    // Another review of the game: what was kept is not asked again.
    commentary::Tablebases again;
    done = 0;
    CHECK(askTables(tablePositions(*line, again), *line, profile, again, cancel, done).empty());
    CHECK(again.verdict(line->positions()[1]) == commentary::Verdict::Win);
    {
        std::lock_guard lock(mutex);
        CHECK(asked.size() == 3);  // the unknown one only
    }

    // Stopped: nothing asked.
    cancel = true;
    commentary::Tablebases stopped;
    done = 0;
    CHECK(askTables(tablePositions(*line, stopped), *line, profile, stopped, cancel, done).empty());
    CHECK(done == 0);

    lichess::servers() = lichess::Servers{};
    HostGate::instance().setPause(60s);
    fs::remove_all(profile);
}
