#include <doctest/doctest.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <chess.hpp>
#include <study/game_tree.hpp>
#include <study/pgn_reader.hpp>
#include <study/repertoire.hpp>
#include <study/scan_board.hpp>
#include <study/store_error.hpp>
#include <study/tree.hpp>

#include "test_games.hpp"

using namespace fcg::study;
using namespace fcg::test;
using fcg::GameTree;

namespace {

GameTree linesOf(const std::string& movetext) {
    const auto read = readPgnGame("[Event \"Repertoire\"]\n\n" + movetext + "\n");
    REQUIRE(read.tree);
    REQUIRE_MESSAGE(read.error.empty(), read.error);
    return *read.tree;
}

chess::Board boardAfter(const std::vector<std::string>& line) {
    chess::Board board;
    for (const auto& san : line) board.makeMove<false>(chess::uci::parseSan(board, san));
    return board;
}

std::vector<chess::Move> movesOf(const std::vector<std::string>& line) {
    chess::Board board;
    std::vector<chess::Move> moves;
    for (const auto& san : line) {
        moves.push_back(chess::uci::parseSan(board, san));
        board.makeMove<false>(moves.back());
    }
    return moves;
}

uint64_t keyAfter(const std::vector<std::string>& line) {
    ScanBoard scan;
    REQUIRE(scan.set(boardAfter(line)));
    return scan.key();
}

int codeAfter(const std::vector<std::string>& line, const std::string& san) {
    ScanBoard scan;
    REQUIRE(scan.set(boardAfter(line)));
    const auto coded = scan.parseSan(san);
    REQUIRE(coded);
    return scan.encode(*coded);
}

std::vector<std::string> sansOf(const fcg::ReplayGame& game) {
    std::vector<std::string> sans;
    for (const auto& m : game.moves) sans.push_back(m.san);
    return sans;
}

void repeat(std::vector<Spec>& specs, int times, const std::string& moves, char result = '1') {
    for (int i = 0; i < times; ++i) specs.push_back({moves, result});
}

TreeBase treeBase(const std::string& name, const std::vector<Spec>& specs) {
    auto db = database(name, make(specs));
    TreeProgress progress;
    std::atomic<bool> cancel{false};
    const auto built = buildTree(*db, *db->snapshot(), {}, progress, cancel);
    REQUIRE_MESSAGE(built.error.empty(), built.error);
    return {db->snapshot(), Tree::open(db->folder())};
}

// The games of the coverage tests: after 1. e4, 30 played 1... e5, 20 1... c5, 10 1... e6 and 3 1... a6.
std::vector<Spec> openings() {
    std::vector<Spec> specs;
    repeat(specs, 30, "1. e4 e5 2. Nf3 Nc6");
    repeat(specs, 20, "1. e4 c5 2. Nf3 d6", '0');
    repeat(specs, 10, "1. e4 e6 2. d4 d5", '=');
    repeat(specs, 3, "1. e4 a6 2. d4 b5");
    return specs;
}

}  // namespace

TEST_CASE("repertoire: its positions, transpositions merged, and the shortest line to each") {
    const auto lines = linesOf("1. d4 (1. c4 Nf6 2. d4 e6 3. g3) 1... Nf6 2. c4 e6 3. Nf3 (3. Nc3 Bb4) *");
    const Repertoire rep(lines, chess::Color::WHITE);
    // 1. d4 Nf6 2. c4 and 1. c4 Nf6 2. d4 are one position, and so are the positions after 2... e6.
    CHECK(rep.size() == 11);
    CHECK(rep.start() == keyAfter({}));
    const auto* top = rep.at(rep.start());
    REQUIRE(top);
    CHECK(top->own);
    CHECK(top->moves.size() == 2);

    const auto merged = keyAfter({"c4", "Nf6", "d4", "e6"});
    CHECK(merged == keyAfter({"d4", "Nf6", "c4", "e6"}));
    const auto* pos = rep.at(merged);
    REQUIRE(pos);
    CHECK(pos->own);
    CHECK(pos->ply == 4);
    std::vector<std::string> sans;
    for (const auto& m : pos->moves) sans.push_back(m.san);
    CHECK(sans == std::vector<std::string>{"Nf3", "g3", "Nc3"});
    CHECK(rep.has(merged, codeAfter({"d4", "Nf6", "c4", "e6"}, "g3")));
    CHECK_FALSE(rep.has(merged, codeAfter({"d4", "Nf6", "c4", "e6"}, "Bg5")));
    CHECK(rep.lineTo(merged) == std::vector<std::string>{"d4", "Nf6", "c4", "e6"});
    // Where the game first reaches it: a move added there goes into the game.
    CHECK(pos->line == -1);
    CHECK(pos->line_ply == 4);

    const auto* answer = rep.at(keyAfter({"d4", "Nf6", "c4", "e6", "Nc3"}));
    REQUIRE(answer);
    CHECK_FALSE(answer->own);
    REQUIRE(answer->moves.size() == 1);
    CHECK(answer->moves[0].san == "Bb4");
    CHECK(answer->moves[0].to == keyAfter({"d4", "Nf6", "c4", "e6", "Nc3", "Bb4"}));
    CHECK(rep.lineTo(rep.start()).empty());
    CHECK(rep.at(keyAfter({"e4"})) == nullptr);
    CHECK(rep.lineTo(keyAfter({"e4"})).empty());
}

TEST_CASE("repertoire: a move added where the lines reach its position, by any order") {
    auto lines = linesOf("1. d4 Nf6 2. c4 e6 *");
    // Reached by 1. c4 Nf6 2. d4 e6, the position is the game's: the move goes at its end.
    CHECK(addMove(lines, false, movesOf({"c4", "Nf6", "d4", "e6"}), movesOf({"d4", "Nf6", "c4", "e6", "Nf3"})[4]));
    CHECK(sansOf(lines.main) == std::vector<std::string>{"d4", "Nf6", "c4", "e6", "Nf3"});
    CHECK(lines.variations.empty());
    // A move the lines have already: nothing changes.
    CHECK(addMove(lines, false, movesOf({"d4"}), movesOf({"d4", "Nf6"})[1]));
    CHECK(lines.variations.empty());
    // A line of its own: a variation where it leaves them, with the moves to the position.
    CHECK(addMove(lines, false, movesOf({"e4", "e5"}), movesOf({"e4", "e5", "Nf3"})[2]));
    REQUIRE(lines.variations.size() == 1);
    CHECK(lines.variations[0].parent == -1);
    CHECK(lines.variations[0].from == 0);
    CHECK(sansOf(fcg::lineOf(lines, 0)) == std::vector<std::string>{"e4", "e5", "Nf3"});
    // An answer to another move of the opponent's, from the game's position after 1. d4.
    CHECK(addMove(lines, false, movesOf({"d4", "d5"}), movesOf({"d4", "d5", "c4"})[2]));
    REQUIRE(lines.variations.size() == 2);
    CHECK(lines.variations[1].from == 1);
    CHECK(sansOf(fcg::lineOf(lines, 1)) == std::vector<std::string>{"d4", "d5", "c4"});
    // A move that is not legal there: the lines stay as they are.
    const auto before = lines.variations.size();
    CHECK_FALSE(addMove(lines, false, movesOf({"d4"}), movesOf({"e4"})[0]));
    const std::vector<chess::Move> twice{movesOf({"e4"})[0], movesOf({"e4"})[0]};
    CHECK_FALSE(addMove(lines, false, twice, movesOf({"d4"})[0]));
    CHECK(lines.variations.size() == before);

    const Repertoire rep(lines, chess::Color::WHITE);
    CHECK(rep.has(keyAfter({"e4", "e5"}), codeAfter({"e4", "e5"}, "Nf3")));
    CHECK(rep.has(keyAfter({"d4", "d5"}), codeAfter({"d4", "d5"}, "c4")));
    CHECK(rep.has(keyAfter({"c4", "Nf6", "d4", "e6"}), codeAfter({"c4", "Nf6", "d4", "e6"}, "Nf3")));
}

TEST_CASE("repertoire: what it covers of a database's games, and its gaps, the most met first") {
    const auto base   = treeBase("repertoire-coverage", openings());
    const auto played = treeMoves({base});

    SUBCASE("an answer to each move played often, one line ending on the opponent's move") {
        const Repertoire rep(linesOf("1. e4 e5 (1... c5 2. Nf3) (1... e6) 2. Nf3 *"), chess::Color::WHITE);
        const auto cover = coverage(rep, played);
        CHECK(cover.positions == 7);
        CHECK(cover.known == 7);
        // 1... a6, three games, is too seldom played to count.
        CHECK(cover.covered == doctest::Approx(50.0 / 60));
        REQUIRE(cover.gaps.size() == 1);
        CHECK(cover.gaps[0].line == std::vector<std::string>{"e4", "e6"});
        CHECK(cover.gaps[0].move.empty());
        CHECK(cover.gaps[0].games == 10);
        CHECK(cover.gaps[0].share == 1);
        CHECK(cover.gaps[0].reach == doctest::Approx(10.0 / 63));
    }
    SUBCASE("the opponent's moves it has no answer to") {
        const Repertoire rep(linesOf("1. e4 e5 2. Nf3 *"), chess::Color::WHITE);
        auto cover = coverage(rep, played);
        CHECK(cover.covered == doctest::Approx(30.0 / 60));
        REQUIRE(cover.gaps.size() == 2);
        CHECK(cover.gaps[0].line == std::vector<std::string>{"e4"});
        CHECK(cover.gaps[0].move == "c5");
        CHECK(cover.gaps[0].games == 20);
        CHECK(cover.gaps[0].share == doctest::Approx(20.0 / 63));
        CHECK(cover.gaps[0].reach == doctest::Approx(20.0 / 63));
        CHECK(cover.gaps[1].move == "e6");
        CHECK(cover.gaps[1].reach == doctest::Approx(10.0 / 63));
        // Fewer games asked for: 1... a6 too.
        CoverageOptions fewer;
        fewer.least_games = 2;
        cover             = coverage(rep, played, fewer);
        REQUIRE(cover.gaps.size() == 3);
        CHECK(cover.gaps[2].move == "a6");
        CHECK(cover.covered == doctest::Approx(30.0 / 63));
        // A share too small: not even then.
        fewer.least_share = 0.05;
        CHECK(coverage(rep, played, fewer).gaps.size() == 2);
        // The most listed.
        fewer.listed = 1;
        CHECK(coverage(rep, played, fewer).gaps.size() == 1);
    }
    SUBCASE("prepared to a depth: the replies to its last move are gaps before it") {
        const Repertoire rep(linesOf("1. e4 e5 2. Nf3 *"), chess::Color::WHITE);
        CoverageOptions deep;
        deep.until_ply   = 4;
        const auto cover = coverage(rep, played, deep);
        CHECK(cover.covered == doctest::Approx(0));
        REQUIRE(cover.gaps.size() == 3);
        CHECK(cover.gaps[0].line == std::vector<std::string>{"e4", "e5", "Nf3"});
        CHECK(cover.gaps[0].move == "Nc6");
        CHECK(cover.gaps[0].reach == doctest::Approx(30.0 / 63));
        deep.until_ply = 3;  // its last move is the third ply's
        CHECK(coverage(rep, played, deep).gaps.size() == 2);
    }
    SUBCASE("a repertoire with Black") {
        const Repertoire whole(linesOf("1. e4 c5 2. Nf3 d6 *"), chess::Color::BLACK);
        auto cover = coverage(whole, played);
        CHECK(cover.covered == doctest::Approx(1));
        CHECK(cover.gaps.empty());
        const Repertoire short_of(linesOf("1. e4 c5 2. Nf3 *"), chess::Color::BLACK);
        cover = coverage(short_of, played);
        CHECK(cover.covered == doctest::Approx(0));
        REQUIRE(cover.gaps.size() == 1);
        CHECK(cover.gaps[0].line == std::vector<std::string>{"e4", "c5", "Nf3"});
        CHECK(cover.gaps[0].move.empty());
        CHECK(cover.gaps[0].games == 20);
        CHECK(cover.gaps[0].reach == doctest::Approx(1));
    }
    SUBCASE("beyond the database's games") {
        const Repertoire rep(linesOf("1. d4 d5 2. c4 *"), chess::Color::WHITE);
        const auto cover = coverage(rep, played);
        CHECK(cover.positions == 4);
        CHECK(cover.known == 1);  // the start, which every game has
        // At the start the repertoire's side chooses: no gap, nothing to answer beyond.
        CHECK(cover.covered == doctest::Approx(1));
        CHECK(cover.gaps.empty());
    }
}

TEST_CASE("repertoire: a position two move orders reach counts the games of both") {
    std::vector<Spec> specs;
    repeat(specs, 10, "1. d4 Nf6 2. c4 e6 3. Nf3 d5");
    repeat(specs, 10, "1. d4 e6 2. c4 Nf6 3. Nf3 b6");
    const auto played = treeMoves({treeBase("repertoire-transposed", specs)});
    const Repertoire rep(linesOf("1. d4 Nf6 (1... e6 2. c4 Nf6 3. Nf3) 2. c4 e6 3. Nf3 *"), chess::Color::WHITE);
    CoverageOptions deep;
    deep.until_ply   = 6;
    const auto cover = coverage(rep, played, deep);
    REQUIRE(cover.gaps.size() == 2);
    // Each half of the games comes to the position by its order: all of them reach it, half reply each way.
    CHECK(cover.gaps[0].reach == doctest::Approx(0.5));
    CHECK(cover.gaps[1].reach == doctest::Approx(0.5));
    CHECK(cover.gaps[0].line == std::vector<std::string>{"d4", "Nf6", "c4", "e6", "Nf3"});
    CHECK(cover.covered == doctest::Approx(0));
    CHECK(coverage(rep, played).covered == doctest::Approx(1));
}

TEST_CASE("repertoire: the trees of two databases summed") {
    std::vector<Spec> first, second;
    repeat(first, 30, "1. e4 e5");
    repeat(second, 20, "1. e4 c5");
    repeat(second, 5, "1. e4 e5");
    const auto played = treeMoves({treeBase("repertoire-first", first), treeBase("repertoire-second", second)});
    ScanBoard scan;
    REQUIRE(scan.set(boardAfter({"e4"})));
    const auto answer = played(scan);
    REQUIRE(answer);
    CHECK(answer->total.games == 55);
    REQUIRE(answer->moves.size() >= 2);
    CHECK(answer->moves[0].code == codeAfter({"e4"}, "e5"));
    CHECK(answer->moves[0].stats.games == 35);
    CHECK(answer->moves[1].code == codeAfter({"e4"}, "c5"));
    CHECK(answer->moves[1].stats.games == 20);
    // A position neither has.
    REQUIRE(scan.set(boardAfter({"d4"})));
    CHECK_FALSE(played(scan));
    // None at all.
    CHECK_FALSE(treeMoves({})(scan));
}

TEST_CASE("repertoire: a damaged tree among sound ones, the base being read told (#149)") {
    std::vector<Spec> specs;
    repeat(specs, 12, "1. e4 e5 2. Nf3 Nc6");
    repeat(specs, 8, "1. e4 c5 2. Nf3 d6");
    const auto first = treeBase("repertoire-sound-first", specs);
    const auto third = treeBase("repertoire-sound-third", specs);
    // The second's records overwritten, their header left: its tree opens, a position read in it is a damaged one.
    auto db = database("repertoire-damaged", make(specs));
    TreeProgress progress;
    std::atomic<bool> cancel{false};
    REQUIRE(buildTree(*db, *db->snapshot(), {}, progress, cancel).error.empty());
    std::string name;
    {
        const auto sound = Tree::open(db->folder());  // let go before its file is written (Windows maps it)
        REQUIRE(sound);
        name = sound->folderName();
    }
    const auto records = db->folder() / name / "records.bin";
    {
        const auto size = std::filesystem::file_size(records);
        REQUIRE(size > 32);
        std::fstream bytes(records, std::ios::in | std::ios::out | std::ios::binary);
        bytes.seekp(32);
        const std::string junk(static_cast<std::size_t>(size - 32), '\xff');
        bytes.write(junk.data(), static_cast<std::streamsize>(junk.size()));
    }
    const TreeBase damaged{db->snapshot(), Tree::open(db->folder())};
    REQUIRE(damaged.tree);
    const Repertoire rep(linesOf("1. e4 e5 2. Nf3 *"), chess::Color::WHITE);

    // Measured on the three: the store's error, the damaged tree told by its place among them.
    std::size_t reading = SIZE_MAX;
    bool refused        = false;
    try {
        (void)coverage(rep, treeMoves({first, damaged, third}, &reading));
    } catch (const StoreError& e) {
        refused = true;
        CHECK(e.code() == StoreErrc::Damaged);
    }
    CHECK(refused);
    CHECK(reading == 1);
    // First among them, the same.
    reading = SIZE_MAX;
    CHECK_THROWS_AS((void)coverage(rep, treeMoves({damaged, first}, &reading)), StoreError);
    CHECK(reading == 0);
    // The sound ones alone: measured, the last one read told.
    reading          = SIZE_MAX;
    const auto cover = coverage(rep, treeMoves({first, third}, &reading));
    CHECK(cover.known > 0);
    CHECK(reading == 1);
}

TEST_CASE("repertoire: a player's moves, out of the games they had that colour, and a repertoire's gaps against them") {
    std::vector<Spec> specs;
    const auto add = [&](int times, const std::string& moves, const std::string& white, const std::string& black) {
        for (int i = 0; i < times; ++i) {
            Spec s{moves, '='};
            s.white = white;
            s.black = black;
            specs.push_back(s);
        }
    };
    add(12, "1. e4 c5", "Someone", "Opponent_A");  // their answer to 1. e4, with Black
    add(4, "1. e4 e5", "Someone", "Opponent_A");
    add(9, "1. e4 e5", "Opponent_A", "Another");  // their opponents' answer, when they had White
    add(5, "1. d4 d5", "Opponent_A", "Another");
    add(30, "1. e4 e6", "Another", "Someone");  // nothing of theirs
    const auto base = treeBase("repertoire-player", specs);

    ScanBoard start;
    REQUIRE(start.set(chess::Board()));
    ScanBoard after_e4;
    REQUIRE(after_e4.set(boardAfter({"e4"})));
    // With Black, against 1. e4: their own moves only, whatever the case of the name.
    const auto black = playerMoves(base, "opponent_a", false)(after_e4);
    REQUIRE(black);
    CHECK(black->total.games == 16);
    REQUIRE(black->moves.size() == 2);
    CHECK(black->moves[0].code == codeAfter({"e4"}, "c5"));
    CHECK(black->moves[0].stats.games == 12);
    // With White, from the start: 1. e4 and 1. d4, their games alone.
    const auto white = playerMoves(base, "Opponent_A", true)(start);
    REQUIRE(white);
    CHECK(white->total.games == 14);
    CHECK(white->moves[0].code == codeAfter({}, "e4"));
    CHECK(white->moves[0].stats.games == 9);
    CHECK_FALSE(playerMoves(base, "Nobody", true)(start));

    // A repertoire with White against their games with Black: 1... c5 is the gap, 1... e6 is not theirs.
    const Repertoire rep(linesOf("1. e4 e5 2. Nf3 *"), chess::Color::WHITE);
    CoverageOptions options;
    options.least_games = 3;
    const auto cover    = coverage(rep, playerMoves(base, "Opponent_A", false), options);
    REQUIRE_FALSE(cover.gaps.empty());
    CHECK(cover.gaps[0].line == std::vector<std::string>{"e4"});
    CHECK(cover.gaps[0].move == "c5");
    CHECK(cover.gaps[0].games == 12);
}
