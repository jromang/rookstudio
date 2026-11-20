// Theory studies (#77): an annotated PGN read as notes, indexed by position. The games are our own analyses, written
// for these tests.
#include <doctest/doctest.hpp>

#include <atomic>
#include <string>
#include <vector>

#include <chess.hpp>
#include <study/scan_board.hpp>
#include <study/theory_notes.hpp>

using namespace fcg::study;

namespace {

chess::Board boardAfter(const std::vector<std::string>& line, const std::string& fen = {}) {
    chess::Board board;
    if (!fen.empty()) board.setFen(fen);
    for (const auto& san : line) board.makeMove<true>(chess::uci::parseSan(board, san));
    return board;
}

uint64_t keyAfter(const std::vector<std::string>& line) { return theoryKey(boardAfter(line)); }

const std::vector<std::string> NAJDORF = {"e4", "c5", "Nf3", "d6", "d4", "cxd4", "Nxd4", "Nf6", "Nc3", "a6"};

std::vector<std::string> najdorf(std::vector<std::string> more) {
    auto line = NAJDORF;
    line.insert(line.end(), more.begin(), more.end());
    return line;
}

// The notes found at a key whose move is `san`.
const TheoryNote* onMove(const std::vector<const TheoryNote*>& notes, const std::string& san) {
    for (const auto* n : notes)
        if (n->san == san) return n;
    return nullptr;
}

constexpr const char* NOTES = R"([Event "Najdorf notes"]
[White "?"]
[Black "?"]
[Result "*"]

{The Najdorf, with 6.Bg5.} 1. e4 c5 2. Nf3 d6 3. d4 cxd4 4. Nxd4 Nf6 5. Nc3 a6 6. Bg5 $1 {The sharpest.} 6... e6
7. f4 Qb6 $1 {The Poisoned Pawn. [%cal Gd1d2,Rb6b2]} ( {Or} 7... Be7 {the main line.} 8. Qf3 h6 ( 8... Qc7 $5
{[%csl Gc7]} ) 9. Bh4 ) 8. Qd2 Qxb2 {[%eval 0.12,38]} 9. Rb1 Qa3 *
)";

}  // namespace

TEST_CASE("theory notes: a PGN's comments, symbols, evaluations, arrows and squares, its variations included") {
    const auto notes = readTheory(NOTES);
    CHECK(notes.error().empty());
    const auto& c = notes.counts();
    CHECK(c.games == 1);
    CHECK(c.unreadable == 0);
    CHECK(c.variations == 2);
    CHECK(c.notes == 6);      // before the first move, 6.Bg5, 7...Qb6, 7...Be7, 8...Qc7, 8...Qxb2
    CHECK(c.positions == 6);  // each about a position of its own
    CHECK(c.comments == 5);   // before the first move, 6.Bg5, 7...Qb6, and before and after 7...Be7
    CHECK(c.symbols == 3);
    CHECK(c.evaluations == 1);
    CHECK(c.marks == 3);
    REQUIRE(notes.games().size() == 1);
    CHECK(notes.games()[0].name == "Najdorf notes");  // "?" against "?": its event
    CHECK(notes.games()[0].standard);

    // Before the first move: about the start.
    const auto start = notes.about(keyAfter({}));
    REQUIRE(start.size() == 1);
    CHECK(start[0]->san.empty());
    CHECK(start[0]->from == 0);
    CHECK(start[0]->after == "The Najdorf, with 6.Bg5.");
    CHECK(start[0]->path.empty());

    // 6.Bg5!: on the move from the position before it, about the position after it.
    const auto bg5 = onMove(notes.from(keyAfter(NAJDORF)), "Bg5");
    REQUIRE(bg5);
    CHECK(bg5 == notes.about(keyAfter(najdorf({"Bg5"}))).front());
    CHECK(bg5->number == 6);
    CHECK_FALSE(bg5->black);
    CHECK(bg5->nags == std::vector<uint8_t>{1});
    CHECK(bg5->after == "The sharpest.");
    CHECK(bg5->line == -1);
    CHECK(bg5->ply == 11);
    CHECK(bg5->path == najdorf({"Bg5"}));
    CHECK(bg5->next == std::vector<std::string>{"e6", "f4", "Qb6", "Qd2"});
    CHECK(bg5->hasText());

    // 7...Qb6!, its arrows; and 7...Be7, a variation's first move, its comments before and after.
    const auto at_f4 = notes.from(keyAfter(najdorf({"Bg5", "e6", "f4"})));
    REQUIRE(at_f4.size() == 2);
    CHECK(at_f4[0]->san == "Qb6");  // the file's order: the game's moves, then its variations
    CHECK(at_f4[0]->number == 7);
    CHECK(at_f4[0]->black);
    CHECK(at_f4[0]->after == "The Poisoned Pawn.");
    REQUIRE(at_f4[0]->arrows.size() == 2);
    CHECK(at_f4[0]->arrows[0] == fcg::BoardMark{'G', 3, 11});  // d1 to d2
    CHECK(at_f4[0]->arrows[1] == fcg::BoardMark{'R', 41, 9});  // b6 to b2
    CHECK(at_f4[1]->san == "Be7");
    CHECK(at_f4[1]->before == "Or");
    CHECK(at_f4[1]->after == "the main line.");
    CHECK(at_f4[1]->line == 0);
    CHECK(at_f4[1]->ply == 14);
    CHECK(at_f4[1]->next == std::vector<std::string>{"Qf3", "h6", "Bh4"});

    // A variation within a variation: 8...Qc7!?, its square, symbols without a comment.
    const auto qc7 = onMove(notes.from(keyAfter(najdorf({"Bg5", "e6", "f4", "Be7", "Qf3"}))), "Qc7");
    REQUIRE(qc7);
    CHECK(qc7->line == 1);
    CHECK(qc7->nags == std::vector<uint8_t>{5});
    REQUIRE(qc7->squares.size() == 1);
    CHECK(qc7->squares[0] == fcg::BoardMark{'G', 50, -1});
    CHECK_FALSE(qc7->hasText());
    CHECK(qc7->path == najdorf({"Bg5", "e6", "f4", "Be7", "Qf3", "Qc7"}));

    // A written evaluation, Black's move: from White's point of view, with its depth.
    const auto qxb2 = notes.about(keyAfter(najdorf({"Bg5", "e6", "f4", "Qb6", "Qd2", "Qxb2"})));
    REQUIRE(qxb2.size() == 1);
    REQUIRE(qxb2[0]->eval);
    CHECK(qxb2[0]->eval->type == fastchess::ScoreType::CP);
    CHECK(qxb2[0]->eval->value == 12);
    CHECK(qxb2[0]->depth == 38);
    CHECK(qxb2[0]->hasText());

    // Nothing at a position the file does not comment.
    CHECK(notes.about(keyAfter(najdorf({"Bg5", "e6"}))).empty());
    CHECK(notes.from(keyAfter(najdorf({"Bg5", "e6", "f4", "Qb6"}))).empty());
}

TEST_CASE("theory notes: found by position, whatever the move order that reaches it") {
    // The same position by the Scheveningen's order (2...e6, 5...d6, 6...a6) and by the Najdorf's.
    const std::vector<std::string> scheveningen = {"e4",  "c5",  "Nf3", "e6",  "d4", "cxd4", "Nxd4",
                                                   "Nf6", "Nc3", "d6",  "Bg5", "a6", "f4"};
    const std::string text                      = R"([Event "Plans"]
[White "Analysis"]
[Black "?"]

1. e4 c5 2. Nf3 e6 3. d4 cxd4 4. Nxd4 Nf6 5. Nc3 d6 6. Bg5 a6 7. f4 {White aims at e5 and castles long.} *

[Event "Lines"]

1. e4 c5 2. Nf3 d6 3. d4 cxd4 4. Nxd4 Nf6 5. Nc3 a6 6. Bg5 e6 7. f4 Qb6 {Poisoned.} *
)";
    const auto notes                            = readTheory(text);
    REQUIRE(notes.games().size() == 2);
    CHECK(notes.games()[0].name == "Analysis – ?");
    CHECK(notes.games()[1].name == "Lines");
    const auto board = boardAfter(najdorf({"Bg5", "e6", "f4"}));
    // The tree's key, from the board's FEN, is the notes' key.
    ScanBoard scan;
    REQUIRE(scan.setFen(board.getFen(), false));
    const auto key = scan.key();
    CHECK(key == theoryKey(board));
    CHECK(key == keyAfter(scheveningen));

    const auto about = notes.about(key);
    REQUIRE(about.size() == 1);
    CHECK(about[0]->game == 0);
    CHECK(about[0]->san == "f4");
    CHECK(about[0]->after == "White aims at e5 and castles long.");
    CHECK(about[0]->path == scheveningen);  // its own order, not the board's
    const auto from = notes.from(key);
    REQUIRE(from.size() == 1);
    CHECK(from[0]->game == 1);
    CHECK(from[0]->san == "Qb6");
    CHECK(from[0]->path == najdorf({"Bg5", "e6", "f4", "Qb6"}));
}

TEST_CASE("theory notes: an en passant square is in the key as the tree has it") {
    // After 1.e4 c6 2.e5 d5 White may take en passant: the key says so, as ScanBoard's from the FEN does.
    const std::string text = "[Event \"Caro\"]\n\n1. e4 c6 2. e5 d5 {The advance.} 3. exd6 *\n";
    const auto notes       = readTheory(text);
    const auto board       = boardAfter({"e4", "c6", "e5", "d5"});
    ScanBoard scan;
    REQUIRE(scan.setFen(board.getFen(), false));
    REQUIRE(notes.about(scan.key()).size() == 1);
    CHECK(notes.about(scan.key())[0]->san == "d5");
    // Without a pawn beside it, no en passant: after 1.e4 c6 2.d4 d5 no white pawn can take on d6.
    const auto other = boardAfter({"e4", "c6", "d4", "d5"});
    ScanBoard plain;
    REQUIRE(plain.setFen(other.getFen(), false));
    CHECK(plain.key() == theoryKey(other));
}

TEST_CASE("theory notes: a Lichess study's PGN, its chapters, shapes and evaluations; clocks are no notes") {
    // As Lichess exports a study: a chapter a game, its name in ChapterName, its shapes and evaluations in the
    // comments, clocks too; a chapter from a set-up position.
    const std::string text = R"([Event "Openings: Plans after 7.f4"]
[Site "https://lichess.org/study/abcd1234/Chap0001"]
[Result "*"]
[Variant "Standard"]
[ECO "B96"]
[Annotator "https://lichess.org/@/rs_sample_0000"]
[StudyName "Openings"]
[ChapterName "Plans after 7.f4"]
[ChapterURL "https://lichess.org/study/abcd1234/Chap0001"]
[Orientation "black"]

1. e4 c5 2. Nf3 d6 3. d4 cxd4 4. Nxd4 Nf6 5. Nc3 a6 6. Bg5 e6 7. f4 { [%cal Ge1c1,Ge4e5] White aims at e5 and castles long. } 7... Nbd7 { [%clk 0:03:00] } 8. Qf3 { [%csl Gb5][%eval 0.3] } 8... b5 *


[Event "Openings: A set-up position"]
[Site "https://lichess.org/study/abcd1234/Chap0002"]
[Result "*"]
[StudyName "Openings"]
[ChapterName "A set-up position"]
[ChapterURL "https://lichess.org/study/abcd1234/Chap0002"]
[FEN "8/8/8/4k3/8/8/4P3/4K3 w - - 0 1"]
[SetUp "1"]

{ The opposition decides. } 1. Kd2 $1 Kd4 2. e3+ *
)";
    const auto notes       = readTheory(text);
    CHECK(notes.error().empty());
    REQUIRE(notes.games().size() == 2);
    CHECK(notes.games()[0].name == "Plans after 7.f4");
    CHECK(notes.games()[0].standard);
    CHECK(notes.games()[1].name == "A set-up position");
    CHECK_FALSE(notes.games()[1].standard);
    const auto& c = notes.counts();
    CHECK(c.notes == 4);  // 7.f4, 8.Qf3; the second chapter's start and 1.Kd2!; not 7...Nbd7's clock
    CHECK(c.evaluations == 1);
    CHECK(c.marks == 3);

    const auto f4 = notes.about(keyAfter(najdorf({"Bg5", "e6", "f4"})));
    REQUIRE(f4.size() == 1);
    CHECK(f4[0]->after == "White aims at e5 and castles long.");
    CHECK(f4[0]->arrows.size() == 2);
    CHECK(notes.about(keyAfter(najdorf({"Bg5", "e6", "f4", "Nbd7"}))).empty());
    const auto qf3 = notes.about(keyAfter(najdorf({"Bg5", "e6", "f4", "Nbd7", "Qf3"})));
    REQUIRE(qf3.size() == 1);
    REQUIRE(qf3[0]->eval);
    CHECK(qf3[0]->eval->value == 30);
    CHECK(qf3[0]->squares.size() == 1);
    CHECK(qf3[0]->after.empty());

    // The set-up position: its comment before the first move, then 1.Kd2!.
    const auto endgame = std::string("8/8/8/4k3/8/8/4P3/4K3 w - - 0 1");
    const auto start   = notes.about(theoryKey(boardAfter({}, endgame)));
    REQUIRE(start.size() == 1);
    CHECK(start[0]->game == 1);
    CHECK(start[0]->after == "The opposition decides.");
    const auto kd2 = notes.from(theoryKey(boardAfter({}, endgame)));
    REQUIRE(kd2.size() == 1);
    CHECK(kd2[0]->san == "Kd2");
    CHECK(kd2[0]->game == 1);
    CHECK(kd2[0]->next == std::vector<std::string>{"Kd4", "e3+"});
}

TEST_CASE("theory notes: comments with the left-out mark are no notes, their move's symbols are") {
    const std::string text =
        "[Event \"Assisted\"]\n\n1. e4 {The centre. [%rs assistant]} 1... c5 $1 {[%rs assistant] "
        "The Sicilian.} 2. Nf3 {Mine.} *\n";
    TheoryOptions options;
    options.left_out = "[%rs assistant]";
    const auto notes = readTheory(text, options);
    CHECK(notes.counts().left_out == 2);
    CHECK(notes.counts().notes == 2);  // 1...c5's symbol, and 2.Nf3's comment
    CHECK(notes.about(keyAfter({"e4"})).empty());
    const auto c5 = notes.about(keyAfter({"e4", "c5"}));
    REQUIRE(c5.size() == 1);
    CHECK(c5[0]->after.empty());
    CHECK(c5[0]->nags == std::vector<uint8_t>{1});
    // Without the option, they are notes like any.
    CHECK(readTheory(text).counts().notes == 3);
}

TEST_CASE("theory notes: a game that cannot be read keeps its place, the games after it their numbers") {
    const std::string text =
        "[Event \"One\"]\n\n1. e4 {First.} *\n\n"
        "[Event \"Two\"]\n[FEN \"not a position\"]\n[SetUp \"1\"]\n\n1. e4 {Lost.} *\n\n"
        "[Event \"Three\"]\n\n1. d4 {Third.} *\n";
    std::atomic<uint64_t> done{0};
    const auto notes = readTheory(text, {}, nullptr, &done);
    CHECK(done.load() == text.size());
    REQUIRE(notes.games().size() == 3);
    CHECK(notes.counts().unreadable == 1);
    CHECK_FALSE(notes.games()[1].read);
    CHECK(notes.error().rfind("game 2:", 0) == 0);
    const auto d4 = notes.about(keyAfter({"d4"}));
    REQUIRE(d4.size() == 1);
    CHECK(d4[0]->game == 2);
    CHECK(d4[0]->after == "Third.");
    // Cancelled: what was read is kept.
    std::atomic<bool> cancel{true};
    CHECK(readTheory(text, {}, &cancel).games().empty());
}
