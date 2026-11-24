// "Check the endgames against the tables" (#94 with #42, mockup M13, board 7, point 20): a finished tournament's games
// that reached seven pieces or fewer set against the tables, from a few positions a game: what it finds, the
// bisection's bound, the local tables and the profile's answers before any request, the summary's words.

#include <doctest/doctest.hpp>

#include <cmath>
#include <map>
#include <string>
#include <vector>

#include "endgame_check.hpp"

using namespace fcg;

namespace {

// A game from `fen` with `moves` (UCI), and the tables' verdict of each of its positions, White's point of view.
struct Scripted {
    CheckedGame game;
    std::map<std::string, int> says;  // by FEN
};

Scripted scripted(const std::string& fen, const std::vector<std::string>& moves, const std::vector<int>& verdicts,
                  int result, GameEnd end, std::string reason = {}) {
    Scripted s;
    s.game.white     = "Alpha";
    s.game.black     = "Bravo";
    s.game.start_fen = fen;
    s.game.result    = result;
    s.game.end       = end;
    s.game.reason    = std::move(reason);
    chess::Board board(fen);
    for (std::size_t i = 0; i <= moves.size(); ++i) {
        if (i < verdicts.size()) s.says[board.getFen()] = verdicts[i];
        if (i == moves.size()) break;
        const auto m = chess::uci::uciToMove(board, moves[i]);
        REQUIRE(m != chess::Move::NO_MOVE);
        s.game.moves.push_back(m);
        board.makeMove(m);
    }
    return s;
}

// The kings stepping between e3 and d3, e6 and d6, a white rook out of their way: `plies` moves, every position its own
// FEN; the rook on another square for each game, so that the games' positions differ.
std::string kings(int n = 0) {
    const int file = "012567"[n % 6] - '0';
    const auto rank =
        (file > 0 ? std::to_string(file) : std::string()) + "R" + (file < 7 ? std::to_string(7 - file) : std::string());
    const auto ranks = n < 6 ? "8/" + rank : rank + "/8";
    return "8/8/4k3/8/8/4K3/" + ranks + " w - - 0 1";
}
const std::string KINGS = kings();
std::vector<std::string> steps(std::size_t plies) {
    const char* cycle[] = {"e3d3", "e6d6", "d3e3", "d6e6"};
    std::vector<std::string> out;
    for (std::size_t i = 0; i < plies; ++i) out.push_back(cycle[i % 4]);
    return out;
}
// Verdicts `a` up to `change` (excluded), `b` from it, for plies + 1 positions.
std::vector<int> verdicts(std::size_t plies, std::size_t change, int a, int b) {
    std::vector<int> out;
    for (std::size_t i = 0; i <= plies; ++i) out.push_back(i < change ? a : b);
    return out;
}

EndgameCheckSummary run(const std::vector<Scripted>& games, std::vector<CheckedGame>& out, int& calls) {
    out.clear();
    std::map<std::string, int> says;
    for (const auto& g : games) {
        out.push_back(g.game);
        says.insert(g.says.begin(), g.says.end());
    }
    calls = 0;
    return checkEndgames(out, [&](const std::string& fen) -> TablesSays {
        ++calls;
        const auto it = says.find(fen);
        if (it == says.end()) return std::nullopt;
        return it->second;
    });
}

}  // namespace

TEST_CASE(
    "endgame check: a win or a draw let slip by a move, a lost position held, the adjudications against the tables") {
    std::vector<Scripted> games;
    // 0: White wins until its 5th move (ply 4 to 5), a draw after: Alpha let the win slip; drawn by the rules.
    games.push_back(scripted(kings(1), steps(8), verdicts(8, 5, 1, 0), 0, GameEnd::Rules, "Draw by 3-fold repetition"));
    // 1: a draw until White's move from ply 2, Black wins after: Alpha let the draw slip, and lost.
    games.push_back(scripted(kings(2), steps(6), verdicts(6, 3, 0, -1), -1, GameEnd::Other, "White loses on time"));
    // 2: White wins all along, the game drawn by repetition: Bravo held the lost position.
    games.push_back(scripted(kings(3), steps(8), verdicts(8, 9, 1, 1), 0, GameEnd::Rules, "Draw by 3-fold repetition"));
    // 3: Black wins all along, a draw adjudicated: in a won position (Bravo's).
    games.push_back(
        scripted(kings(4), steps(4), verdicts(4, 5, -1, -1), 0, GameEnd::Adjudicated, "Draw by adjudication"));
    // 4: a draw all along, White's win adjudicated: against the tables.
    games.push_back(
        scripted(kings(5), steps(4), verdicts(4, 5, 0, 0), 1, GameEnd::Adjudicated, "White wins by adjudication"));
    // 5: White wins all along and loses on time: no endgame matter.
    games.push_back(scripted(kings(6), steps(4), verdicts(4, 5, 1, 1), -1, GameEnd::Other, "White loses on time"));
    // 6: the result the tables give.
    games.push_back(scripted(kings(7), steps(4), verdicts(4, 5, 0, 0), 0, GameEnd::Rules, "Draw by 3-fold repetition"));
    // 7: never fewer than eight pieces.
    games.push_back(scripted(std::string(chess::constants::STARTPOS), {"e2e4", "e7e5"}, {}, 0, GameEnd::Rules));

    std::vector<CheckedGame> checked;
    int calls    = 0;
    const auto s = run(games, checked, calls);
    CHECK(s.games == 8);
    CHECK(s.reached == 7);
    CHECK(s.judged == 7);
    REQUIRE(s.findings.size() == 5);
    using K = EndgameFinding::Kind;

    const auto& slip = s.findings[0];
    CHECK(slip.kind == K::WinLetSlip);
    CHECK(slip.game == 0);
    CHECK(slip.ply == 5);
    CHECK(slip.side == chess::Color::WHITE);
    CHECK(slip.before == 1);
    CHECK(slip.after == 0);
    CHECK(slip.move == "3.Kd3");
    CHECK(endgameFindingWords(slip, checked[0]) == "Alpha let the win slip with 3.Kd3: the tables then give a draw.");

    const auto& draw = s.findings[1];
    CHECK(draw.kind == K::DrawLetSlip);
    CHECK(draw.ply == 3);
    CHECK(draw.side == chess::Color::WHITE);
    CHECK(draw.move == "2.Ke3");
    CHECK(endgameFindingWords(draw, checked[1]) ==
          "Alpha let the draw slip with 2.Ke3: the tables then give Bravo the win.");

    const auto& held = s.findings[2];
    CHECK(held.kind == K::LostPositionHeld);
    CHECK(held.game == 2);
    CHECK(held.ply == 8);
    CHECK(held.side == chess::Color::BLACK);
    CHECK(held.move.empty());
    CHECK(endgameFindingWords(held, checked[2]) ==
          "Bravo held a lost position: the tables give Alpha the win at the end. Draw by 3-fold repetition.");

    const auto& adjudicated = s.findings[3];
    CHECK(adjudicated.kind == K::DrawAdjudicatedWon);
    CHECK(adjudicated.side == chess::Color::BLACK);
    CHECK(endgameFindingWords(adjudicated, checked[3]) == "A draw adjudicated where the tables give Bravo the win.");

    const auto& win = s.findings[4];
    CHECK(win.kind == K::WinAdjudicatedNotWon);
    CHECK(win.side == chess::Color::WHITE);
    CHECK(win.before == 0);
    CHECK(endgameFindingWords(win, checked[4]) == "A win adjudicated to Alpha where the tables give a draw.");

    CHECK(endgameCheckSentence(s, checked) ==
          "7 games reached seven pieces or fewer: 1 win let slip (Alpha: 1), 1 draw let slip (Alpha: 1), 1 lost "
          "position held, 1 draw adjudicated in a won position, 1 win adjudicated against the tables.");
}

TEST_CASE("endgame check: a few positions a game, the first the tables tell and the last, a bisection between") {
    std::vector<CheckedGame> checked;
    int calls = 0;
    SUBCASE("a verdict changed: the move found in 2 + log2(plies) positions at most") {
        for (const std::size_t plies : {9u, 40u, 61u, 200u}) {
            // The change after a White move: from an even ply to the next.
            const std::size_t change = plies / 2 % 2 == 0 ? plies / 2 + 1 : plies / 2;
            const auto s =
                run({scripted(KINGS, steps(plies), verdicts(plies, change, 1, 0), 0, GameEnd::Rules)}, checked, calls);
            CAPTURE(plies);
            REQUIRE(s.findings.size() == 1);
            CHECK(s.findings[0].ply == change);
            CHECK(calls == s.asked);
            CHECK(calls <= 2 + static_cast<int>(std::ceil(std::log2(static_cast<double>(plies)))));
        }
    }
    SUBCASE("the same verdict at both ends: two positions") {
        run({scripted(KINGS, steps(120), verdicts(120, 121, 1, 1), 1, GameEnd::Other)}, checked, calls);
        CHECK(calls == 2);
    }
    SUBCASE("a mate at the end: the rules tell it, one position asked") {
        const auto s =
            run({scripted("7k/8/6K1/8/8/8/8/R7 w - - 0 1", {"a1a8"}, {1}, 1, GameEnd::Rules)}, checked, calls);
        CHECK(calls == 1);
        CHECK(s.judged == 1);
        CHECK(s.findings.empty());
    }
    SUBCASE("tables of fewer pieces: the first position they cover, after the capture") {
        // Five pieces, then Rxa5 leaves four: tables of four pieces tell the second position on.
        auto g = scripted("4k3/8/8/n7/8/8/8/R3K2R w - - 0 1", {"a1a5", "e8f7", "a5a1", "f7e7"}, {}, 0, GameEnd::Rules);
        chess::Board board(g.game.start_fen);
        for (const auto& m : g.game.moves) {
            board.makeMove(m);
            g.says[board.getFen()] = 1;
        }
        EndgameGameCheck check(g.game);
        REQUIRE(check.reached());
        CHECK(check.next() == std::size_t{0});
        check.give(0, std::nullopt);  // five pieces: beyond the tables at hand
        CHECK(check.next() == std::size_t{1});
        CHECK(check.pieces(1) == 4);
        check.give(1, 1);
        CHECK(check.next() == std::size_t{4});
        check.give(4, 1);
        CHECK(check.done());
        CHECK(check.judged());
        REQUIRE(check.findings(0).size() == 1);
        CHECK(check.findings(0)[0].kind == EndgameFinding::Kind::LostPositionHeld);
    }
    SUBCASE("castling still possible: not before both sides lost it") {
        auto g = scripted("r3k3/8/8/8/8/8/8/4K2R w Kq - 0 1", {"e1f1", "e8d8", "f1g1"}, {}, 0, GameEnd::Rules);
        EndgameGameCheck check(g.game);
        REQUIRE(check.reached());
        CHECK(check.next() == std::size_t{2});
    }
    SUBCASE("the tables silent on the last position: the game left unjudged") {
        auto g = scripted(KINGS, steps(10), verdicts(10, 11, 1, 1), 0, GameEnd::Rules);
        EndgameGameCheck check(g.game);
        check.give(0, 1);
        check.give(10, std::nullopt);
        CHECK(check.done());
        CHECK_FALSE(check.judged());
        CHECK(check.findings(0).empty());
    }
}

TEST_CASE("endgame check: the local tables first, the profile's answers before any request, Lichess's once allowed") {
    // The disk's tables cover it: taken, even with an answer kept.
    CHECK(checkSource(5, 5, false, true, 1, false) == CheckSource::Disk);
    // A tournament of this process holds them: the answer kept, else Lichess's when allowed.
    CHECK(checkSource(5, 5, true, true, 1, false) == CheckSource::Kept);
    CHECK(checkSource(5, 5, true, false, 1, false) == CheckSource::Lichess);
    // Beyond the disk's: kept first, never asked again.
    CHECK(checkSource(7, 5, false, true, 0, false) == CheckSource::Kept);
    CHECK(checkSource(7, 5, false, false, 1, false) == CheckSource::Lichess);
    CHECK(checkSource(7, 5, false, false, 0, true) == CheckSource::Lichess);  // for this check
    CHECK(checkSource(7, 5, false, false, 0, false) == CheckSource::Consent);
    CHECK(checkSource(7, 5, false, false, 2, true) == CheckSource::None);  // refused on the Lichess page
    CHECK(checkSource(8, 5, false, false, 1, false) == CheckSource::None);

    // How the games ended, as fastchess wrote it.
    CHECK(gameEndOf(0, "Draw by 3-fold repetition", false) == GameEnd::Rules);
    CHECK(gameEndOf(1, "Draw by adjudication", false) == GameEnd::Adjudicated);
    CHECK(gameEndOf(1, "Black wins by adjudication", false) == GameEnd::Adjudicated);
    CHECK(gameEndOf(1, "White wins by adjudication: SyzygyTB", false) == GameEnd::TablesAdjudicated);
    CHECK(gameEndOf(2, "White loses on time", false) == GameEnd::Other);
    CHECK(gameEndOf(1, "Adjudicated by the operator", true) == GameEnd::Other);
}

TEST_CASE("endgame check: the summary's words, each result agreeing, games left unjudged, none reaching the tables") {
    std::vector<CheckedGame> games(3);
    EndgameCheckSummary s;
    s.games = 3;
    CHECK(endgameCheckSentence(s, games) ==
          "No game reached seven pieces or fewer: there is nothing for the tables to check.");
    s.reached = 1;
    s.judged  = 1;
    CHECK(endgameCheckSentence(s, games) == "1 game reached seven pieces or fewer: its result agrees with the tables.");
    s.reached = 3;
    s.judged  = 1;
    CHECK(endgameCheckSentence(s, games) ==
          "3 games reached seven pieces or fewer: each result agrees with the tables. 2 games left unjudged: the "
          "tables at hand do not cover them.");
    // Several engines: the most first.
    games[0].white = games[1].white = "Alpha";
    games[0].black = games[1].black = "Bravo";
    games[2].white                  = "Bravo";
    games[2].black                  = "Alpha";
    s.judged                        = 3;
    for (std::size_t i = 0; i < 3; ++i) {
        EndgameFinding f;
        f.kind = EndgameFinding::Kind::WinLetSlip;
        f.game = i;
        f.side = i == 1 ? chess::Color::WHITE : chess::Color::BLACK;
        s.findings.push_back(f);
    }
    CHECK(endgameCheckSentence(s, games) ==
          "3 games reached seven pieces or fewer: 3 wins let slip (Alpha: 2, Bravo: 1).");
}
