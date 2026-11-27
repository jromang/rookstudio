// Practical moves (#94, mockup M13, board 7, point 17): the candidates within 0.3 pawn of the engine's best, never a
// losing one; the level's games from a fake Lichess explorer (its rating group and speed asked) and from databases of
// our own filtered by rating, counted apart; the choice from 200 games, else by the fewest good replies. Players'
// names are invented.

#include <doctest/doctest.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <map>
#include <mutex>
#include <random>
#include <string>
#include <vector>

#include <fmt/include/fmt/format.h>

#include <study/import.hpp>
#include <study/scan_board.hpp>
#include <study/store.hpp>
#include <study/tree.hpp>

#include "https.hpp"
#include "practical.hpp"
#include "scripted_server.hpp"

using namespace fcg;
using namespace fcg::test;
namespace fs = std::filesystem;
using namespace std::chrono_literals;

namespace {

// After 1.e4 e5 2.Nf3 Nc6 3.Bc4 Nf6: the mockup's position.
constexpr const char* TWO_KNIGHTS = "r1bqkb1r/pppp1ppp/2n2n2/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 4 4";

AnalysisLine line(int rank, int cp, std::vector<std::string> pv, bool mate = false) {
    AnalysisLine l;
    l.multipv = rank;
    l.depth   = 28;
    l.score   = fastchess::Score{mate ? fastchess::ScoreType::MATE : fastchess::ScoreType::CP, cp};
    l.pv      = std::move(pv);
    return l;
}

std::vector<AnalysisLine> mockupLines() {
    return {line(1, 31, {"d2d3", "f8c5"}), line(2, 24, {"b1c3", "f8b4"}), line(3, 18, {"f3g5", "d7d5", "e4d5", "f6d5"}),
            line(4, 12, {"d2d4", "e5d4"}), line(5, 10, {"e1g1", "f6e4"}), line(6, -20, {"b2b4", "c6b4"})};
}

void answer(net::TcpConnection& c, int status, const std::string& body) {
    c.sendAll(fmt::format("HTTP/1.1 {} X\r\nContent-Type: application/json\r\nContent-Length: {}\r\nConnection: "
                          "close\r\n\r\n",
                          status, body.size()) +
              body);
}

}  // namespace

TEST_CASE("practical moves: the levels, their explorer group and their band of ratings") {
    CHECK(practicalLevels().front() == 1100);
    CHECK(explorerGroup(1500) == 1400);
    CHECK(levelBand(1500) == std::pair<int, int>{1400, 1600});
    CHECK(explorerGroup(1100) == 1000);
    CHECK(levelBand(2350) == std::pair<int, int>{2200, 2500});
    CHECK(explorerGroup(2600) == 2500);
    CHECK(levelBand(2600).first == 2500);
    for (const int level : practicalLevels()) {
        const auto [low, high] = levelBand(level);
        CHECK(low <= level);
        CHECK(level < high);
    }
}

TEST_CASE("practical moves: the candidates within 0.3 pawn of the best, never a losing move") {
    const auto c = practicalCandidates(mockupLines());
    REQUIRE(c.size() == 5);  // b4, 0.51 behind, is out
    CHECK(c[0].uci == "d2d3");
    CHECK(c[0].loss_cp == 0);
    CHECK(c[2].uci == "f3g5");
    CHECK(c[2].loss_cp == 13);
    CHECK(c[4].loss_cp == 21);
    // A mate against the side to move is never one, where the best is not mated; nor a lost position.
    const auto mated = practicalCandidates({line(1, 0, {"a2a3"}), line(2, -3, {"h2h3"}, true)});
    CHECK(mated.size() == 1);
    const auto lost = practicalCandidates({line(1, -130, {"a2a3"}), line(2, -155, {"h2h3"})});
    CHECK(lost.size() == 1);
    // With the tables' answer, the moves that keep its verdict, whatever the engine's window.
    lichess::Tablebase t;
    t.outcome       = lichess::Outcome::Win;
    t.moves         = {{"a2a3", "a3", lichess::Outcome::Loss},
                       {"h2h3", "h3", lichess::Outcome::Loss},
                       {"b2b3", "b3", lichess::Outcome::Draw}};
    const auto kept = practicalCandidates({line(1, 900, {"a2a3"}), line(2, 300, {"h2h3"}), line(3, 890, {"b2b3"})}, &t);
    REQUIRE(kept.size() == 2);
    CHECK(kept[0].uci == "a2a3");
    CHECK(kept[1].uci == "h2h3");  // far behind in the engine's score, the same verdict
}

TEST_CASE("practical moves: the level's games from Lichess's explorer, the best score from 200 games") {
    std::mutex mutex;
    std::string asked;
    ScriptedServer server(0, [&](const std::string& head, net::TcpConnection& c) {
        {
            std::lock_guard lock(mutex);
            asked = head.substr(4, head.find(' ', 4) - 4);
        }
        // The explorer at 1400-1600, rapid: 4.Ng5 scores best there, 4.O-O has too few games.
        answer(c, 200,
               R"({"white":1800,"draws":300,"black":1500,"moves":[)"
               R"({"uci":"d2d3","san":"d3","white":900,"draws":200,"black":700},)"
               R"({"uci":"b1c3","san":"Nc3","white":500,"draws":80,"black":420},)"
               R"({"uci":"f3g5","san":"Ng5","white":330,"draws":10,"black":220},)"
               R"({"uci":"d2d4","san":"d4","white":60,"draws":5,"black":40},)"
               R"({"uci":"e1g1","san":"O-O","white":90,"draws":3,"black":40}],"topGames":[]})");
    });
    const auto port = server.port();
    REQUIRE(server.ok());
    const auto base    = fmt::format("http://127.0.0.1:{}", port);
    lichess::servers() = lichess::Servers{base, base + "/tb", base + "/explorer"};
    HostGate::instance().setPause(50ms);

    lichess::ExplorerQuery q;
    q.source     = lichess::ExplorerSource::Lichess;
    q.fen        = TWO_KNIGHTS;
    q.speeds     = {"rapid"};
    q.ratings    = {explorerGroup(1500)};
    q.token      = "test-token";
    const auto a = lichess::explorer(q);
    REQUIRE_MESSAGE(a.value, a.error);
    {
        std::lock_guard lock(mutex);
        CHECK(asked.find("speeds=rapid") != std::string::npos);
        CHECK(asked.find("ratings=1400") != std::string::npos);
    }
    const auto level = levelMoves(*a.value);
    REQUIRE(level.size() == 5);
    CHECK(level.front().uci == "d2d3");  // the most played first
    CHECK(level.front().games == 1800);

    auto candidates   = practicalCandidates(mockupLines());
    const auto choice = choosePractical(candidates, level, chess::Color::WHITE);
    CHECK(choice.way == PracticalWay::Games);
    REQUIRE(choice.index == 2);
    CHECK(candidates[2].uci == "f3g5");
    // 330 wins and 10 draws of 560: 59.8 %; d3 55.6 %, Nc3 54 %, d4 too few games (105), O-O too (133).
    CHECK(candidates[2].expected == doctest::Approx(100.0 * (330 + 5) / 560.0));
    CHECK(candidates[0].expected == doctest::Approx(100.0 * (900 + 100) / 1800.0));
    CHECK_FALSE(candidates[3].expected);
    CHECK(candidates[3].games == 105);
    CHECK_FALSE(candidates[4].expected);
    CHECK_FALSE(outsideKnownGames(candidates));
    // The same games seen from Black: the score is the mover's.
    CHECK(*moverScore(level[2], chess::Color::BLACK) == doctest::Approx(100.0 * (220 + 5) / 560.0));

    lichess::servers() = lichess::Servers{};
    HostGate::instance().setPause(60s);
}

TEST_CASE("practical moves: outside known games, the fewest good replies by the engine") {
    auto candidates = practicalCandidates(mockupLines());
    // A level with a few games only: none counts.
    const std::vector<LevelMove> few{{"d2d3", 120, 60, 10, 50}, {"f3g5", 80, 50, 0, 30}};
    auto choice = choosePractical(candidates, few, chess::Color::WHITE);
    CHECK(outsideKnownGames(candidates));
    CHECK(choice.way == PracticalWay::Engine);  // the replies not counted yet: the engine's best
    CHECK(choice.index == 0);
    CHECK(candidates[0].games == 120);
    // The replies counted: after Ng5, 2 of 31 hold; after d3, 9 of 30.
    candidates[0].holding = 9;
    candidates[0].replies = 30;
    candidates[2].holding = 2;
    candidates[2].replies = 31;
    choice                = choosePractical(candidates, few, chess::Color::WHITE);
    CHECK(choice.way == PracticalWay::Replies);
    CHECK(choice.index == 2);
    // The replies that hold: within 0.3 pawn of the opponent's best, from its lines over all of them.
    CHECK(holdingReplies({line(1, 40, {"d7d5"}), line(2, 15, {"f6g4"}), line(3, 9, {"d7d6"}), line(4, -90, {"h7h6"}),
                          line(5, -400, {"a7a6"})}) == 2);
    CHECK(holdingReplies({line(1, 2, {"a7a6"}, true), line(2, 900, {"h7h6"})}) == 1);  // a mate holds alone
    CHECK(std::string(replyMark(60)) == "?!");
    CHECK(std::string(replyMark(150)) == "?");
    CHECK(std::string(replyMark(400)) == "??");
    CHECK(std::string(replyMark(20)).empty());
}

TEST_CASE("practical moves: databases filtered by rating, the moves counted apart over our own games") {
    const auto dir = fs::temp_directory_path() / "practical-db";
    fs::remove_all(dir);
    auto db = study::Database::create(dir);
    // Games of our own through the position, from three bands of ratings, each with one of four moves and a result.
    const char* moves[]   = {"d3", "Nc3", "Ng5", "O-O"};
    const char* results[] = {"1-0", "1/2-1/2", "0-1", "1-0", "1-0"};
    struct Kept {
        std::string move;
        int white_elo = 0, black_elo = 0;
        std::string result;
    };
    std::vector<Kept> kept;
    std::vector<study::ImportedGame> games;
    std::mt19937 random(94);
    for (int i = 0; i < 360; ++i) {
        const int band = i % 3;  // 1400-1600, 1900-2000, and no rating
        const int base = band == 0 ? 1410 : 1900;
        Kept k;
        k.move           = moves[random() % 4];
        k.white_elo      = band == 2 ? 0 : base + static_cast<int>(random() % 180);
        k.black_elo      = band == 2 ? 0 : base + static_cast<int>(random() % 180);
        k.result         = results[random() % 5];
        std::string text = fmt::format("[White \"Player_{}\"]\n[Black \"Partner_{}\"]\n", i, i);
        if (k.white_elo) text += fmt::format("[WhiteElo \"{}\"]\n[BlackElo \"{}\"]\n", k.white_elo, k.black_elo);
        text += fmt::format("[Result \"{}\"]\n\n1. e4 e5 2. Nf3 Nc6 3. Bc4 Nf6 4. {} {}\n", k.result, k.move, k.result);
        study::ImportedGame g;
        REQUIRE(study::readImportedGame(text, g));
        games.push_back(std::move(g));
        kept.push_back(std::move(k));
    }
    db->append(std::move(games), true);
    study::TreeOptions options;
    options.threads = 2;
    study::TreeProgress progress;
    std::atomic<bool> cancel{false};
    REQUIRE(study::buildTree(*db, *db->snapshot(), options, progress, cancel).error.empty());
    const auto tree = study::Tree::open(dir);
    REQUIRE(tree);

    const chess::Board board(TWO_KNIGHTS);
    for (const int level : {1500, 1900}) {
        CAPTURE(level);
        const auto got = levelMoves({{tree, db->snapshot()}}, board, level);
        // Counted apart: the games whose players' average rating is in the band, by their fourth move.
        const auto [low, high] = levelBand(level);
        std::map<std::string, LevelMove> apart;
        for (const auto& k : kept) {
            if (!k.white_elo) continue;
            const int average = (k.white_elo + k.black_elo) / 2;
            if (average < low || average >= high) continue;
            auto& m = apart[k.move];
            ++m.games;
            if (k.result == "1-0") ++m.white;
            if (k.result == "0-1") ++m.black;
            if (k.result == "1/2-1/2") ++m.draws;
        }
        uint64_t total = 0;
        for (const auto& m : got) {
            chess::Board b(TWO_KNIGHTS);
            const auto san = chess::uci::moveToSan(b, chess::uci::uciToMove(b, m.uci));
            CAPTURE(san);
            REQUIRE(apart.count(san) == 1);
            CHECK(m.games == apart[san].games);
            CHECK(m.white == apart[san].white);
            CHECK(m.draws == apart[san].draws);
            CHECK(m.black == apart[san].black);
            total += m.games;
        }
        CHECK(got.size() == apart.size());
        CHECK(total > 0);
        for (std::size_t i = 1; i < got.size(); ++i) CHECK(got[i - 1].games >= got[i].games);
    }
    // 2350: no game at that level.
    CHECK(levelMoves({{tree, db->snapshot()}}, board, 2350).empty());
    // The folder goes with the tests' own temporary folder: the database, still open here, holds a file of it that
    // Windows would not let go.
}
