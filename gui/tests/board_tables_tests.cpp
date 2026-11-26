// The study Board's Endgames tab (#42, mockup M13, board 7, point 20): which positions it speaks of, where it takes the
// answer from, the moves grouped by their outcome, the distances and the words, the profile's answers read before any
// request, the disk's tables.
//
// The answers in gui/tests/data/tablebase are those of Lichess's tablebase server, asked anonymously on 2026-11-24 for
// positions of our own: rook-ending-7 (6k1/2R4p/8/5P2/6P1/r7/6K1/8 w, seven pieces, a draw that Rxh7 and four other
// moves lose), rook-ending-after-rxh7 and its main line (the position after Rxh7: six pieces, Black mates in 19),
// op1-draw (8/p4k2/4p3/4P3/3K4/8/PR6/6r1 w, eight pieces of the op1 tables, asked with dtc=auxiliary), the same without
// it (unknown), and op1-cursed-win (R7/8/8/8/7q/2K1B2p/7P/2Bk4 w, the op1 example of lila-tablebase's tests).

#include <doctest/doctest.hpp>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include <fmt/include/fmt/format.h>

#include "board_tables.hpp"
#include "endgames.hpp"
#include "free_ports.hpp"
#include "https.hpp"
#include "online_cache.hpp"
#include "scripted_server.hpp"
#include "tablebases.hpp"
#include "test_support.hpp"

using namespace fcg;
using namespace fcg::test;
namespace fs = std::filesystem;
using namespace std::chrono_literals;

namespace {

constexpr const char* ROOK_ENDING = "6k1/2R4p/8/5P2/6P1/r7/6K1/8 w - - 0 1";
constexpr const char* AFTER_RXH7  = "6k1/7R/8/5P2/6P1/r7/6K1/8 b - - 0 1";
constexpr const char* OP1_DRAW    = "8/p4k2/4p3/4P3/3K4/8/PR6/6r1 w - - 0 1";
constexpr const char* OP1_CURSED  = "R7/8/8/8/7q/2K1B2p/7P/2Bk4 w - - 0 1";

std::string fixture(const char* name) {
    std::ifstream in(test::sourcePath() / "gui" / "tests" / "data" / "tablebase" / name, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void answer(net::TcpConnection& c, int status, const std::string& body) {
    c.sendAll(fmt::format("HTTP/1.1 {} X\r\nContent-Type: application/json\r\nContent-Length: {}\r\nConnection: "
                          "close\r\n\r\n",
                          status, body.size()) +
              body);
}

// Lichess's tablebase on this machine: the answers of the fixtures by position, `unknown` for any other, each request
// counted.
struct Tables {
    std::mutex mutex;
    std::vector<std::string> asked;
    uint16_t port = freePort();
    ScriptedServer server{
        port, [this](const std::string& head, net::TcpConnection& c) {
            const auto path = head.substr(4, head.find(' ', 4) - 4);
            {
                std::lock_guard lock(mutex);
                asked.push_back(path);
            }
            const auto has  = [&](const char* fen) { return path.find(lichess::urlEncode(fen)) != std::string::npos; };
            const bool line = path.find("/mainline") != std::string::npos;
            if (has(ROOK_ENDING) && !line)
                answer(c, 200, fixture("rook-ending-7.json"));
            else if (has(AFTER_RXH7))
                answer(c, 200, fixture(line ? "rook-ending-after-rxh7-mainline.json" : "rook-ending-after-rxh7.json"));
            else if (has(OP1_DRAW))
                answer(c, 200,
                       fixture(path.find("dtc=auxiliary") != std::string::npos ? "op1-draw.json"
                                                                               : "op1-draw-without-dtc.json"));
            else if (has(OP1_CURSED))
                answer(c, 200, fixture("op1-cursed-win.json"));
            else
                answer(c, 200, R"({"category":"unknown","moves":[]})");
        }};
    Tables() {
        REQUIRE(server.ok());
        const auto base    = fmt::format("http://127.0.0.1:{}", port);
        lichess::servers() = lichess::Servers{base, base + "/tb", base + "/explorer"};
        HostGate::instance().setPause(50ms);
    }
    ~Tables() {
        lichess::servers() = lichess::Servers{};
        HostGate::instance().setPause(60s);
    }
    std::size_t requests() {
        std::lock_guard lock(mutex);
        return asked.size();
    }
};

lichess::Tablebase asked(const char* fen) {
    auto a = lichess::tablebase(fen);
    REQUIRE_MESSAGE(a.value, lichess::describe(a));
    sortTableMoves(*a.value);
    return *a.value;
}

std::vector<std::string> sans(const lichess::Tablebase& t, const TablesMoves& g) {
    std::vector<std::string> out;
    for (const auto i : g.moves) out.push_back(t.moves[i].san);
    return out;
}

}  // namespace

TEST_CASE("endgames tab: seven pieces or fewer, op1's eight, never castling rights nor Chess960") {
    CHECK(tablesReach(chess::Board(ROOK_ENDING), false) == TablesReach::Yes);
    CHECK(tablesReach(chess::Board(OP1_DRAW), false) == TablesReach::Yes);
    CHECK(op1Position(chess::Board(OP1_DRAW)));
    CHECK(op1Position(chess::Board(OP1_CURSED)));
    // Eight pieces without a pawn of each side on one file; a lone pawn against a king and pawns.
    CHECK(tablesReach(chess::Board("8/5k2/8/3pP3/3K4/8/PR5p/6r1 w - - 0 1"), false) == TablesReach::TooMany);
    CHECK_FALSE(op1Position(chess::Board("4k3/4p3/8/8/8/8/3PPPPP/4K3 w - - 0 1")));
    CHECK(tablesReach(chess::Board(), false) == TablesReach::TooMany);
    CHECK(tablesReach(chess::Board("4k3/8/8/8/8/8/8/R3K3 w Q - 0 1"), false) == TablesReach::Castling);
    CHECK(tablesReach(chess::Board("4k3/8/8/8/8/8/8/R3K3 w - - 0 1"), true) == TablesReach::Chess960);
}

TEST_CASE("endgames tab: the disk's tables first, Lichess's once allowed, the consent asked first") {
    // The disk covers five pieces: a position of five is its, unless a tournament of this process holds the tables.
    CHECK(tablesWay(5, false, 5, false, 0, false) == TablesWay::Disk);
    CHECK(tablesWay(5, false, 5, true, 0, false) == TablesWay::Consent);
    // Seven pieces: Lichess's, once allowed always or for this position; asked first; nothing once refused.
    CHECK(tablesWay(7, false, 5, false, 0, false) == TablesWay::Consent);
    CHECK(tablesWay(7, false, 5, false, 1, false) == TablesWay::Lichess);
    CHECK(tablesWay(7, false, 5, false, 0, true) == TablesWay::Lichess);
    CHECK(tablesWay(7, false, 5, false, 2, true) == TablesWay::Refused);
    // Eight: op1's only, never the disk's.
    CHECK(tablesWay(8, true, 7, false, 1, false) == TablesWay::Lichess);
    CHECK(tablesWay(8, false, 7, false, 1, false) == TablesWay::Beyond);
}

TEST_CASE("endgames tab: the moves of a drawn rook ending grouped, the losing ones with what the tables then say") {
    Tables lichess;
    const auto a = asked(ROOK_ENDING);
    CHECK(positionGroup(a) == TablesGroup::Draw);
    CHECK(outcomeVerdict(a.outcome, chess::Color::WHITE) == "Draw");
    CHECK(tablesDistance(a.dtm, a.dtz, a.dtc).empty());  // a draw: no distance
    const auto groups = groupTablesMoves(a);
    REQUIRE(groups.size() == 2);
    CHECK(groups[0].group == TablesGroup::Draw);
    CHECK(groups[0].moves.size() == 16);
    CHECK(tablesGroupTitle(groups[0].group, TablesGroup::Draw) == "Keep the draw");
    CHECK(groups[1].group == TablesGroup::Loss);
    CHECK(tablesGroupTitle(groups[1].group, TablesGroup::Draw) == "Lose");
    // The five losing moves, the longest resistance first: the four of 1 ply to the next zeroing move, then Rxh7, whose
    // DTM (37 plies for Black) the six pieces left give.
    CHECK(sans(a, groups[1]).size() == 5);
    for (const auto i : groups[0].moves) CHECK_FALSE(spoilsOutcome(a, a.moves[i]));
    for (const auto i : groups[1].moves) CHECK(spoilsOutcome(a, a.moves[i]));
    const auto rxh7 = std::find_if(a.moves.begin(), a.moves.end(), [](const auto& m) { return m.san == "Rxh7"; });
    REQUIRE(rxh7 != a.moves.end());
    CHECK(afterMoveWords(*rxh7, chess::Color::WHITE) == "Black wins: mate in 19 moves");
    CHECK(std::string(tablesMetric(rxh7->dtm, rxh7->dtz, rxh7->dtc)) == "DTM");
    const auto rc3 = std::find_if(a.moves.begin(), a.moves.end(), [](const auto& m) { return m.san == "Rc3"; });
    REQUIRE(rc3 != a.moves.end());
    CHECK(afterMoveWords(*rc3, chess::Color::WHITE) == "Black wins: 1 move to a capture or a pawn move");
    // The refutation: the first move of the tables' main line after Rxh7.
    const auto line = lichess::tablebaseMainline(AFTER_RXH7);
    REQUIRE(line.value);
    REQUIRE(!line.value->empty());
    CHECK(line.value->front() == "g8h7");
    CHECK(line.value->size() == 35);
}

TEST_CASE("endgames tab: op1's eight pieces asked with their DTC, a cursed win said in players' words") {
    Tables lichess;
    const auto draw = asked(OP1_DRAW);
    CHECK(draw.outcome == lichess::Outcome::Draw);
    CHECK(draw.dtc == 0);
    CHECK(lichess.asked.back().find("dtc=auxiliary") != std::string::npos);
    const auto cursed = asked(OP1_CURSED);
    CHECK(positionGroup(cursed) == TablesGroup::CursedWin);
    CHECK(outcomeVerdict(cursed.outcome, chess::Color::WHITE) == "Won for White, but drawn by the fifty-move rule");
    CHECK(tablesDistance(cursed.dtm, cursed.dtz, cursed.dtc) == "584 moves to the next capture, promotion or mate");
    CHECK(std::string(tablesMetric(cursed.dtm, cursed.dtz, cursed.dtc)) == "DTC");
    const auto groups = groupTablesMoves(cursed);
    REQUIRE(!groups.empty());
    CHECK(groups[0].group == TablesGroup::CursedWin);  // Rf8: Black's blessed loss
    CHECK(tablesGroupTitle(groups[0].group, TablesGroup::CursedWin) == "Won, but drawn by the fifty-move rule");
    CHECK(sans(cursed, groups[0]) == std::vector<std::string>{"Rf8"});
    CHECK(tablesGroupTitle(TablesGroup::Draw, TablesGroup::CursedWin) == "Keep the draw");
    // A cursed win and a draw both draw by the fifty-move rule: a move into a plain draw spoils nothing, a move that
    // gives Black a win (13 of them, 9 "maybe": the rule and the game played do not settle it) spoils the draw.
    int spoiling = 0;
    for (const auto& m : cursed.moves) {
        CHECK(spoilsOutcome(cursed, m) == (moveGroup(m) == TablesGroup::Loss));
        spoiling += spoilsOutcome(cursed, m) ? 1 : 0;
    }
    CHECK(spoiling == 13);
}

TEST_CASE("endgames tab: the words of each distance and verdict") {
    CHECK(tablesDistance(-37, 1, std::nullopt) == "mate in 19 moves");
    CHECK(tablesDistance(1, 1, std::nullopt) == "mate in 1 move");
    CHECK(tablesDistance(std::nullopt, -24, std::nullopt) == "12 moves to a capture or a pawn move");
    CHECK(tablesDistance(std::nullopt, std::nullopt, 1) == "1 move to the next capture, promotion or mate");
    CHECK(tablesDistance(0, 0, std::nullopt).empty());
    CHECK(tablesVerdict(TablesGroup::Win, chess::Color::BLACK) == "Black wins");
    CHECK(tablesVerdict(TablesGroup::Loss, chess::Color::BLACK) == "White wins");
    CHECK(tablesVerdict(TablesGroup::BlessedLoss, chess::Color::WHITE) ==
          "Won for Black, but drawn by the fifty-move rule");
    CHECK(outcomeVerdict(lichess::Outcome::MaybeWin, chess::Color::WHITE) ==
          "White wins, maybe: the 50-move rule and the game played do not settle it");
    CHECK(tablesGroupTitle(TablesGroup::Draw, TablesGroup::Win) == "Throw the win away: a draw");
    CHECK(tablesGroupTitle(TablesGroup::Win, TablesGroup::Win) == "Keep the win");
    CHECK(tablesGroupTitle(TablesGroup::Loss, TablesGroup::Loss) == "Lose: the longest resistance first");
    CHECK(tablesSource(false, 4, 5) == "Your tables on the disk (Syzygy, up to 5 pieces)");
    CHECK(tablesSource(true, 7, 5) == "Lichess's 7-piece tables (yours on the disk go up to 5 pieces)");
    CHECK(tablesSource(true, 7, 0) == "Lichess's 7-piece tables (none on your disk)");
    CHECK(tablesSource(true, 8, 5) == "Lichess's 8-piece tables (op1)");
    lichess::TablebaseMove mate;
    mate.checkmate = true;
    CHECK(moveGroup(mate) == TablesGroup::Win);
    CHECK(afterMoveWords(mate, chess::Color::WHITE) == "checkmate");
}

TEST_CASE("endgames tab: the profile's answers read before any request, an unknown one asked again") {
    Tables lichess;
    const auto profile = fs::temp_directory_path() / "board-tables-profile";
    fs::remove_all(profile);
    bool sent  = false;
    auto first = keptOrAskedTables(profile, ROOK_ENDING, sent);
    REQUIRE(first.value);
    CHECK(sent);
    CHECK(lichess.requests() == 1);
    CHECK(cachedTablebase(profile, ROOK_ENDING));
    // Again: from the profile, nothing asked.
    auto again = keptOrAskedTables(profile, ROOK_ENDING, sent);
    REQUIRE(again.value);
    CHECK_FALSE(sent);
    CHECK(lichess.requests() == 1);
    CHECK(again.value->moves.size() == first.value->moves.size());
    // The main line too, kept for good.
    auto line = keptOrAskedLine(profile, AFTER_RXH7, sent);
    REQUIRE(line.value);
    CHECK(sent);
    CHECK(lichess.requests() == 2);
    line = keptOrAskedLine(profile, AFTER_RXH7, sent);
    REQUIRE(line.value);
    CHECK_FALSE(sent);
    CHECK(line.value->size() == 35);
    CHECK(lichess.requests() == 2);
    // A position the tables do not know is not kept: they may know it later.
    const char* unknown = "8/8/8/4k3/8/8/2PPP3/4K3 w - - 0 1";
    (void)keptOrAskedTables(profile, unknown, sent);
    CHECK(sent);
    CHECK_FALSE(cachedTablebase(profile, unknown));
    (void)keptOrAskedTables(profile, unknown, sent);
    CHECK(sent);
    CHECK(lichess.requests() == 4);
    fs::remove_all(profile);
}

TEST_CASE("endgames tab: the disk's tables answer as Lichess does, their line followed to the end") {
    const auto root = test::sourcePath();
#ifdef _WIN32
    const char* separator = ";";
#else
    const char* separator = ":";
#endif
    const auto paths = (root / "gui" / "tests" / "data" / "syzygy3").string() + separator +
                       (root / "app" / "tests" / "data" / "syzygy_wdl4").string();
    // King and queen against king: White wins, every queen move but those that hang it keeps the win.
    const chess::Board board("8/8/8/4k3/8/8/8/3QK3 w - - 0 1");
    CHECK(tablesWay(piecesOf(board), false, 3, false, 0, false) == TablesWay::Disk);
    std::optional<lichess::Tablebase> answer;
    std::vector<std::string> line;
    {
        const auto lock = lockSyzygy();
        answer          = probeDiskTables(board, paths);
        REQUIRE(answer);
        sortTableMoves(*answer);
        line = tablesLine(board, [&](const chess::Board& b) { return probeDiskTables(b, paths); }, 60);
    }
    CHECK(positionGroup(*answer) == TablesGroup::Win);
    CHECK(answer->dtz);
    CHECK_FALSE(answer->dtm);  // Syzygy: no distance to mate
    CHECK(tablesDistance(answer->dtm, answer->dtz, answer->dtc).find("to a capture or a pawn move") !=
          std::string::npos);
    const auto groups = groupTablesMoves(*answer);
    REQUIRE(!groups.empty());
    CHECK(groups[0].group == TablesGroup::Win);
    CHECK(tablesGroupTitle(groups[0].group, TablesGroup::Win) == "Keep the win");
    // The tables' line ends in mate.
    REQUIRE(!line.empty());
    auto end = board;
    for (const auto& u : line) end.makeMove(chess::uci::uciToMove(end, u));
    CHECK(end.isGameOver().first == chess::GameResultReason::CHECKMATE);
}
