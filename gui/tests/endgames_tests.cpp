// Endgames against the tablebases (#82): the catalogue's starts as the tables say them, a move judged by the outcome it
// leaves, the tables' answer, how a game ends by its goal, the record and its cards.
#include <doctest/doctest.hpp>

#include <cstdlib>
#include <filesystem>
#include <set>
#include <string>

#include "endgames.hpp"
#include "tablebases.hpp"
#include "test_support.hpp"
#include "training.hpp"

using namespace fcg;
namespace fs = std::filesystem;

namespace {

std::string joined(std::initializer_list<fs::path> dirs) {
#ifdef _WIN32
    const char* separator = ";";
#else
    const char* separator = ":";
#endif
    std::string out;
    for (const auto& d : dirs) out += (out.empty() ? "" : separator) + d.string();
    return out;
}

// The 3-piece tables with their DTZ files (ours), and fastchess' WDL tables of 4 pieces.
std::string tables34() {
    const auto root = test::sourcePath();
    return joined({root / "gui" / "tests" / "data" / "syzygy3", root / "app" / "tests" / "data" / "syzygy_wdl4"});
}

lichess::TablebaseMove move(const char* uci, lichess::Outcome left, std::optional<int> dtz = {},
                            std::optional<int> dtm = {}) {
    lichess::TablebaseMove m;
    m.uci     = uci;
    m.outcome = left;
    m.dtz     = dtz;
    m.dtm     = dtm;
    return m;
}

std::vector<std::string> ucis(const lichess::Tablebase& t) {
    std::vector<std::string> out;
    for (const auto& m : t.moves) out.push_back(m.uci);
    return out;
}

}  // namespace

TEST_CASE("endgames: the catalogue, each start what the tables say of it") {
    const auto& all = endgameCatalogue();
    CHECK(all.size() == 18);
    std::set<std::string> ids;
    for (const auto& e : all) {
        CAPTURE(e.id);
        CHECK(ids.insert(e.id).second);
        CHECK(endgameEntry(e.id) == &e);
        const chess::Board board(e.fen);
        CHECK(board.castlingRights().isEmpty());
        CHECK(piecesOf(board) <= 7);
        CHECK(piecesOf(std::string(e.fen)) == piecesOf(board));
        CHECK(std::string(e.idea).size() > 20);
        CHECK(e.mate == (e.group == EndgameGroup::Mates));
        if (e.mate) CHECK(e.goal == EndgameGoal::Win);
    }
    CHECK_FALSE(endgameEntry("nothing"));

    // Those of 4 pieces or fewer, with the tables of the repository; those of 5 too with a set of 3 to 5 pieces named
    // by FCG_SYZYGY (docs/design/endgames.md).
    const auto lock  = lockSyzygy();
    const auto check = [&](const std::string& paths, int pieces) {
        forgetSyzygy();
        int checked = 0;
        for (const auto& e : all) {
            const chess::Board board(e.fen);
            if (piecesOf(board) > pieces) continue;
            CAPTURE(e.id);
            const auto answer = probeDiskTables(board, paths);
            REQUIRE(answer);
            CHECK(outcomeResult(answer->outcome) == (e.goal == EndgameGoal::Win ? 1 : 0));
            ++checked;
        }
        return checked;
    };
    CHECK(check(tables34(), 4) == 12);
    if (const char* full = std::getenv("FCG_SYZYGY")) CHECK(check(full, 5) == 16);
    forgetSyzygy();
}

TEST_CASE("endgames: a move that turns the win into a draw, and the moves that kept it") {
    const auto lock  = lockSyzygy();
    const auto paths = tables34();
    forgetSyzygy();
    // The opposition: Kd6 and Kf6 keep the win, Kd5 and Kf5 let the king in front of the pawn.
    const chess::Board start("4k3/8/4K3/4P3/8/8/8/8 w - - 0 1");
    const auto answer = probeDiskTables(start, paths);
    REQUIRE(answer);
    CHECK(answer->outcome == lichess::Outcome::Win);
    REQUIRE(answer->dtz);
    CHECK(*answer->dtz > 0);
    CHECK(answer->moves.size() == 4);

    const auto fault = judgeEndgameMove(*answer, "e6d5");
    CHECK(fault.known);
    CHECK(fault.fault());
    CHECK(fault.before == 1);
    CHECK(fault.after == 0);
    CHECK(std::set<std::string>(fault.kept.begin(), fault.kept.end()) == std::set<std::string>{"e6d6", "e6f6"});
    const auto good = judgeEndgameMove(*answer, "e6f6");
    CHECK(good.known);
    CHECK_FALSE(good.fault());
    CHECK(good.after == 1);
    CHECK_FALSE(judgeEndgameMove(*answer, "e1e2").known);
    // The best first: the moves that win before those that draw.
    CHECK(moveResult(answer->moves[0]) == 1);
    CHECK(moveResult(answer->moves[1]) == 1);
    CHECK(moveResult(answer->moves[2]) == 0);

    // The tables' answer after the fault: one of the two moves that hold the draw (the others lose).
    chess::Board after = start;
    after.makeMove(chess::uci::uciToMove(after, "e6d5"));
    const auto reply = probeDiskTables(after, paths);
    REQUIRE(reply);
    CHECK(reply->outcome == lichess::Outcome::Draw);
    const auto played = tablesMove(*reply);
    REQUIRE(played);
    CHECK((*played == "e8e7" || *played == "e8d7"));

    // Without the DTZ tables (fastchess' 3-piece WDL files), the same judgement from the WDL tables alone.
    forgetSyzygy();
    const auto wdl = probeDiskTables(start, (test::sourcePath() / "app" / "tests" / "data" / "syzygy_wdl3").string());
    REQUIRE(wdl);
    CHECK(wdl->outcome == lichess::Outcome::Win);
    CHECK_FALSE(wdl->dtz);
    CHECK(judgeEndgameMove(*wdl, "e6d5").fault());
    CHECK_FALSE(judgeEndgameMove(*wdl, "e6d6").fault());

    // A position the tables do not cover: more pieces than they have.
    forgetSyzygy();
    CHECK_FALSE(probeDiskTables(chess::Board(), paths));
    CHECK_FALSE(probeDiskTables(chess::Board("8/1R6/5k2/8/4KPP1/r7/8/8 w - - 0 1"), paths));
    forgetSyzygy();
}

TEST_CASE("endgames: the mate the tables give, and the move that mates") {
    const auto lock  = lockSyzygy();
    const auto paths = tables34();
    forgetSyzygy();
    // The queen's start: the tables mate in 7.
    const auto queen = probeDiskTables(chess::Board(endgameEntry("queen")->fen), paths);
    REQUIRE(queen);
    REQUIRE(tablesMateMoves(*queen));
    CHECK(*tablesMateMoves(*queen) == 7);
    // A mate in one: the move that mates comes first, said as a mate.
    const auto one = probeDiskTables(chess::Board("7k/8/6K1/8/8/8/8/1Q6 w - - 0 1"), paths);
    REQUIRE(one);
    REQUIRE_FALSE(one->moves.empty());
    CHECK(one->moves.front().checkmate);
    CHECK(moveResult(one->moves.front()) == 1);
    CHECK(tablesMateMoves(*one) == 1);
    // A position that cannot be (the side not to move in check): none.
    CHECK_FALSE(probeDiskTables(chess::Board("7k/8/6K1/8/8/8/8/Q7 w - - 0 1"), paths));
    // The side mated: no move, the loss said.
    const auto mated = probeDiskTables(chess::Board("Q6k/8/6K1/8/8/8/8/8 b - - 0 1"), paths);
    REQUIRE(mated);
    CHECK(mated->checkmate);
    CHECK(mated->moves.empty());
    CHECK(outcomeResult(mated->outcome) == -1);
    CHECK_FALSE(tablesMove(*mated));
    // The tables' line from the opposition reaches the pawn's promotion.
    forgetSyzygy();
    const auto line = tablesLine(
        chess::Board("4k3/8/4K3/4P3/8/8/8/8 w - - 0 1"),
        [&](const chess::Board& b) { return probeDiskTables(b, paths); }, 40);
    REQUIRE(line.size() >= 5);
    chess::Board end("4k3/8/4K3/4P3/8/8/8/8 w - - 0 1");
    for (const auto& u : line) end.makeMove(chess::uci::uciToMove(end, u));
    CHECK(end.pieces(chess::PieceType::PAWN).count() == 0);  // promoted: a queen or a rook now
    forgetSyzygy();
}

TEST_CASE("endgames: Lichess's answers judged as those of the disk") {
    using O = lichess::Outcome;
    CHECK(outcomeResult(O::Win) == 1);
    CHECK(outcomeResult(O::MaybeWin) == 1);
    CHECK(outcomeResult(O::CursedWin) == 0);
    CHECK(outcomeResult(O::BlessedLoss) == 0);
    CHECK(outcomeResult(O::Draw) == 0);
    CHECK(outcomeResult(O::Loss) == -1);
    CHECK_FALSE(outcomeResult(O::Unknown));

    // A win: the moves as Lichess sends them, each with what it leaves the other side.
    lichess::Tablebase t;
    t.outcome = O::Win;
    t.dtm     = 13;
    t.moves   = {move("a1a2", O::Loss, -12, -12), move("a1a3", O::Loss, -20, -20), move("a1a4", O::BlessedLoss),
                 move("a1a5", O::Draw), move("a1a6", O::Win)};
    CHECK(tablesMateMoves(t) == 7);
    CHECK(tablesMove(t) == "a1a2");
    auto j = judgeEndgameMove(t, "a1a4");  // a win the fifty-move rule spoils: a draw
    CHECK(j.fault());
    CHECK(j.after == 0);
    CHECK(j.kept == std::vector<std::string>{"a1a2", "a1a3"});
    j = judgeEndgameMove(t, "a1a6");
    CHECK(j.after == -1);
    CHECK(judgeEndgameMove(t, "a1a3").after == 1);

    // The order the tables play in: the best outcome, the quickest win, the longest loss, then the fewest replies.
    lichess::Tablebase mixed;
    mixed.outcome = O::Win;
    mixed.moves   = {move("b1b2", O::Draw), move("b1b3", O::Loss, -30), move("b1b4", O::Loss, -9),
                     move("b1b5", O::Win, 4)};
    sortTableMoves(mixed);
    CHECK(ucis(mixed) == std::vector<std::string>{"b1b4", "b1b3", "b1b2", "b1b5"});
    lichess::Tablebase losing;
    losing.outcome = O::Loss;
    losing.moves   = {move("c1c2", O::Win, 3), move("c1c3", O::Win, 11), move("c1c4", O::Win, 11)};
    sortTableMoves(losing, {{"c1c3", 5}, {"c1c4", 2}});
    CHECK(ucis(losing) == std::vector<std::string>{"c1c4", "c1c3", "c1c2"});
    lichess::Tablebase drawn;
    drawn.outcome = O::Draw;
    drawn.moves   = {move("d1d2", O::Draw), move("d1d3", O::Draw), move("d1d4", O::Win)};
    sortTableMoves(drawn, {{"d1d2", 6}, {"d1d3", 1}});
    CHECK(ucis(drawn) == std::vector<std::string>{"d1d3", "d1d2", "d1d4"});
}

TEST_CASE("endgames: how a game ends, by its goal") {
    EndgameTask win;
    win.goal         = EndgameGoal::Win;
    EndgameTask mate = win;
    mate.mate        = true;
    EndgameTask hold;
    hold.goal    = EndgameGoal::Draw;
    const auto W = chess::Color::WHITE, B = chess::Color::BLACK;
    bool capture    = false;
    const auto play = [&](chess::Board& b, const char* uci) {
        const auto m = chess::uci::uciToMove(b, uci);
        capture      = b.isCapture(m);
        b.makeMove(m);
        return m;
    };

    // The mate: won, whatever the goal; the user mated: lost.
    {
        chess::Board b("7k/8/6K1/8/8/8/8/1Q6 w - - 0 1");
        const auto m = play(b, "b1b8");
        CHECK(endgameOutcome(b, m, capture, W, mate, 1, 0) == EndgameOutcome::Won);
        CHECK(endgameOutcome(b, m, capture, B, hold, -1, 0) == EndgameOutcome::Lost);
    }
    // A pawn queened, the win kept: won, unless the mate itself is the goal.
    {
        chess::Board b("8/4P3/8/8/8/8/k7/6K1 w - - 0 1");
        const auto m = play(b, "e7e8q");
        CHECK(endgameOutcome(b, m, capture, W, win, 1, 0) == EndgameOutcome::Won);
        CHECK(endgameOutcome(b, m, capture, W, mate, 1, 0) == EndgameOutcome::Going);
        CHECK(endgameOutcome(b, m, capture, W, win, 0, 0) == EndgameOutcome::Going);  // queened into a draw: on
    }
    // The other side's last man taken but its king, the win kept: won; a king alone from the start is no win.
    {
        chess::Board b("8/8/8/3k4/8/8/2r5/1Q2K3 w - - 0 1");
        const auto m = play(b, "b1c2");
        CHECK(endgameOutcome(b, m, capture, W, win, 1, 0) == EndgameOutcome::Won);
        chess::Board alone("4k3/8/4K3/4P3/8/8/8/8 w - - 0 1");
        const auto k = play(alone, "e6f6");
        CHECK(endgameOutcome(alone, k, capture, W, win, 1, 0) == EndgameOutcome::Going);
    }
    // A stalemate: held when the draw is the goal, drawn when the win is.
    {
        chess::Board b("7k/8/5K2/8/8/8/8/6Q1 w - - 0 1");
        const auto m = play(b, "g1g6");
        CHECK(endgameOutcome(b, m, capture, W, win, 0, 0) == EndgameOutcome::Drawn);
        CHECK(endgameOutcome(b, m, capture, B, hold, 0, 0) == EndgameOutcome::Held);
    }
    // A threefold repetition, the same.
    {
        chess::Board b("8/8/8/8/8/k7/8/K6R b - - 0 1");
        chess::Move m;
        for (int i = 0; i < 2; ++i) {
            m = play(b, "a3b3");
            m = play(b, "h1h2");
            m = play(b, "b3a3");
            m = play(b, "h2h1");
        }
        CHECK(endgameOutcome(b, m, capture, B, hold, 0, 3) == EndgameOutcome::Held);
        CHECK(endgameOutcome(b, m, capture, W, win, 1, 0) == EndgameOutcome::Drawn);
    }
    // The draw held long enough.
    {
        chess::Board b("4k3/R7/1r6/4PK2/8/8/8/8 b - - 0 1");
        const auto m = play(b, "b6c6");
        CHECK(endgameOutcome(b, m, capture, B, hold, 0, HOLD_MOVES - 1) == EndgameOutcome::Going);
        CHECK(endgameOutcome(b, m, capture, B, hold, 0, HOLD_MOVES) == EndgameOutcome::Held);
        CHECK(endgameOutcome(b, m, capture, B, hold, -1, HOLD_MOVES) == EndgameOutcome::Going);
    }
}

TEST_CASE("endgames: the record and the positions missed, kept in the profile") {
    const auto folder = fs::temp_directory_path() / "endgames-record";
    fs::remove_all(folder);
    CHECK(readEndgameRecord(folder).results.empty());

    const auto scheduler = trainingScheduler();
    const int64_t now    = 1'800'000'000;
    const auto day_end   = trainingDayEnd(now);
    EndgameRecord record;
    record.results["queen"] = {3, 2, true, 9, 7, now, false};
    const auto task         = taskOf(*endgameEntry("opposition"));
    const auto card         = missedEndgameCard(task, "8/4k3/8/3KP3/8/8/8/8 w - - 0 1", now, day_end, scheduler);
    CHECK(card.card.due > day_end);
    CHECK(card.entry == "opposition");
    CHECK(card.goal == EndgameGoal::Win);
    record.cards[positionKey(card.fen)] = card;
    record.answer_ms                    = 42000;
    REQUIRE(writeEndgameRecord(folder, record));

    const auto read = readEndgameRecord(folder);
    CHECK(read.results == record.results);
    CHECK(read.cards == record.cards);
    CHECK(read.answer_ms == 42000);
    CHECK(positionKey("8/4k3/8/3KP3/8/8/8/8 w - - 3 9") == "8/4k3/8/3KP3/8/8/8/8 w - -");
    CHECK(endgameCardsDue(read, day_end) == 0);
    CHECK(endgameCardsDue(read, day_end + study::fsrs::DAY) == 1);

    // Played again: its end without a fault nor a hint Good, with a hint Hard, a fault or no end Again.
    CHECK(endgameRating(true, false, false) == study::fsrs::Rating::Good);
    CHECK(endgameRating(true, false, true) == study::fsrs::Rating::Hard);
    CHECK(endgameRating(true, true, false) == study::fsrs::Rating::Again);
    CHECK(endgameRating(false, false, false) == study::fsrs::Rating::Again);
    fs::remove_all(folder);
}
