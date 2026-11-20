#include <doctest/doctest.hpp>

#include <algorithm>
#include <random>
#include <string>
#include <vector>

#include <study/pgn_lexer.hpp>
#include <study/pgn_reader.hpp>
#include <study/pgn_writer.hpp>

using namespace fcg;
using namespace fcg::study;

namespace {

const char* LICHESS = R"([Event "Rated Blitz game"]
[Site "https://lichess.org/abcd1234"]
[Date "2026.11.16"]
[White "alice"]
[Black "bob"]
[Result "1-0"]
[WhiteElo "1850"]
[BlackElo "1790"]
[TimeControl "180+2"]
[ECO "C50"]
[Opening "Italian Game"]

1. e4 { [%eval 0.17] [%clk 0:03:00] } 1... e5 { [%eval 0.19] [%clk 0:03:00] } 2. Nf3 { [%eval 0.13] [%clk 0:02:59] }
2... Nc6 { [%eval 0.2] [%clk 0:02:58] } 3. Bc4 { [%eval 0.0] [%clk 0:02:58] } 3... Nd4?! { [%eval 0.85] [%clk
0:02:55] } 4. Nxe5?? { [%eval -2.5] [%clk 0:02:50] } 4... Qg5! { [%eval -2.48] [%clk 0:02:54] } 5. Nxf7 { [%eval #-3]
[%clk 0:02:40] } 1-0
)";

const char* ANNOTATED = R"([Event "Training"]
[Site "?"]
[Date "2026.11.17"]
[Round "?"]
[White "White"]
[Black "Black"]
[Result "*"]
[Annotator "RookStudio"]

{A short game to test the notes.} 1. e4 {[%csl Gd4,Re5][%cal Ge2e4,Rd7d5] The most popular.} 1... c5 $1
(1... e5 $10 {Classical.} 2. Nf3 (2. f4 {[%evp 0,12,25] The King's Gambit.} 2... exf4) 2... Nc6) ({Also} 1... d5
$5) 2. Nf3 d6 3. d4 cxd4 4. Nxd4 Nf6 5. Nc3 a6 $146 {[%emt 0:00:12] The Najdorf.} (5... g6 { Dragon } ) 6. Bg5 *
)";

const char* TOURNAMENT = R"([Event "Gaia releases"]
[Site "?"]
[Date "2026.11.17"]
[Round "16"]
[White "Gaia 4.4.3"]
[Black "Gaia 4.3.2"]
[Result "1-0"]
[TimeControl "10+0.1"]

1. e4 {book} e5 {book} 2. Nf3 {+0.35/18 0.512s} Nc6 {-0.30/17 0.498s} 3. Bb5 {+0.41/19 0.533s, White wins by
adjudication} 1-0
)";

// A game of our tournaments, in each of the styles a tournament writes its PGN in: its engines' fields, the book, and
// how it ended after its last move (#87).
const char* FIELDS = R"([Event "Gaia releases"]
[Site "?"]
[Date "2026.11.20"]
[Round "1"]
[White "Gaia 4.4.3"]
[Black "Gaia 4.4.2"]
[Result "1-0"]
[TimeControl "40+0.4"]

1. d4 {book} d5 {book} 2. c4 {+0.28/20 0.955s, n=1162043, sd=32, nps=1216798, tbhits=0, pv="c2c4 e7e6 g1f3 g8f6 b1c3", wdl=193/734/73, dt=0.517s}
e6 {-0.19/18 0.423s, n=519779, sd=35, nps=1228791, tbhits=2, pv="e7e6 b1c3 f8b4", wdl=86/746/168, dt=0.012s}
3. Nc3 {+M5/40 0.165s, n=191573, sd=32, nps=1161048, tbhits=0, pv="b1c3 f8b4", wdl=1000/0/0, dt=0.001s, White mates} 1-0
)";

const char* CHESSBASE_STYLE = R"([Event "Gaia releases"]
[Site "?"]
[Date "2026.11.20"]
[Round "1"]
[White "Gaia 4.4.3"]
[Black "Gaia 4.4.2"]
[Result "1/2-1/2"]
[TimeControl "40+0.4"]

1. d4 {book} d5 {book} 2. c4 {[%eval 0.28,20] [%clk 0:00:37.409] [%emt 0:00:00.955], n=1162043, sd=32, nps=1216798, tbhits=0, pv="c2c4 e7e6 g1f3 g8f6 b1c3", wdl=193/734/73, dt=0.517s}
e6 {[%eval 0.19,18] [%clk 0:00:36.000] [%emt 0:00:00.423], n=519779, sd=35, nps=1228791, tbhits=2, pv="e7e6 b1c3 f8b4", wdl=86/746/168, dt=0.012s}
3. Nc3 {[%eval 0.00,44] [%clk 0:00:22.103] [%emt 0:00:01.708], n=1965029, sd=43, nps=1150485, tbhits=4, pv="b1c3", wdl=30/940/30, dt=0.001s, Draw by 3-fold repetition} 1/2-1/2
)";

// The text of a PGN with its line ends as spaces: a comment reads the same however it is wrapped.
std::string flat(std::string text) {
    for (auto& c : text) c = c == '\n' ? ' ' : c;
    return text;
}

std::vector<std::string> sans(const std::vector<ReplayMove>& moves) {
    std::vector<std::string> out;
    for (const auto& m : moves) out.push_back(m.san);
    return out;
}

// What a tree holds that the PGN carries: the moves of every line, their notes, clocks and scores.
void sameTree(const GameTree& a, const GameTree& b) {
    REQUIRE(a.main.moves.size() == b.main.moves.size());
    REQUIRE(a.variations.size() == b.variations.size());
    CHECK(a.main.pre_comment == b.main.pre_comment);
    const auto sameMoves = [](const std::vector<ReplayMove>& x, const std::vector<ReplayMove>& y) {
        REQUIRE(x.size() == y.size());
        for (std::size_t i = 0; i < x.size(); ++i) {
            CAPTURE(i);
            CHECK(x[i].move == y[i].move);
            CHECK(x[i].notes == y[i].notes);
            CHECK(x[i].clock_ms == y[i].clock_ms);
            CHECK(x[i].book == y[i].book);
            CHECK(x[i].score.has_value() == y[i].score.has_value());
            if (x[i].score && y[i].score) {
                CHECK(x[i].score->type == y[i].score->type);
                CHECK(x[i].score->value == y[i].score->value);
            }
        }
    };
    sameMoves(a.main.moves, b.main.moves);
    for (std::size_t v = 0; v < a.variations.size(); ++v) {
        CAPTURE(v);
        CHECK(a.variations[v].parent == b.variations[v].parent);
        CHECK(a.variations[v].from == b.variations[v].from);
        sameMoves(a.variations[v].moves, b.variations[v].moves);
    }
}

// Written, read back and written again: the same tree, the same text.
void roundTrip(const char* pgn) {
    const auto first = readPgnGame(pgn);
    REQUIRE(first.tree);
    const auto text = writePgnGame(*first.tree, {first.chess960, {}});
    INFO(text);
    const auto second = readPgnGame(text);
    REQUIRE(second.tree);
    CHECK(second.error.empty());
    sameTree(*first.tree, *second.tree);
    CHECK(writePgnGame(*second.tree, {second.chess960, {}}) == text);
    // No line longer than 80 characters but a single word that is.
    std::size_t at = 0;
    while (at < text.size()) {
        auto end = text.find('\n', at);
        if (end == std::string::npos) end = text.size();
        const auto line = text.substr(at, end - at);
        if (line.find(' ') != std::string::npos) CHECK(line.size() <= 80);
        // A [%...] command is never cut by a line end.
        CHECK(std::count(line.begin(), line.end(), '[') == std::count(line.begin(), line.end(), ']'));
        at = end + 1;
    }
}

}  // namespace

TEST_CASE("PGN lexer: the words of a PGN, as other programs write them") {
    const std::string text =
        "\xEF\xBB\xBF[Event \"A \\\"quoted\\\" event\"]\r\n\r\n% an escape line\r\n"
        "1.e4 e5!? 2. O-O?! $14 +- (2... 0-0) {a {comment} ; to the end\n3... -- Z0 1/2-1/2";
    PgnLexer lex(text);
    std::vector<PgnToken> kinds;
    std::vector<std::string> words;
    for (auto w = lex.next(); w.kind != PgnToken::End; w = lex.next()) {
        kinds.push_back(w.kind);
        words.emplace_back(w.kind == PgnToken::Nag ? std::to_string(w.nag) : std::string(w.text));
    }
    const std::vector<PgnToken> expected{PgnToken::Tag,        PgnToken::MoveNumber, PgnToken::San,
                                         PgnToken::San,        PgnToken::MoveNumber, PgnToken::San,
                                         PgnToken::Nag,        PgnToken::Nag,        PgnToken::VariationStart,
                                         PgnToken::MoveNumber, PgnToken::San,        PgnToken::VariationEnd,
                                         PgnToken::Comment,    PgnToken::Comment,    PgnToken::MoveNumber,
                                         PgnToken::San,        PgnToken::San,        PgnToken::Result};
    CHECK(kinds == expected);
    CHECK(words[0] == "Event");
    CHECK(words[2] == "e4");
    CHECK(words[3] == "e5");
    CHECK(words[6] == "14");
    CHECK(words[7] == "18");
    CHECK(words[10] == "0-0");
    CHECK(words[12] == "a {comment");
    CHECK(words[13] == " to the end");
    CHECK(words[15] == "--");
    CHECK(words[16] == "Z0");

    PgnLexer tag("[Event \"A \\\"quoted\\\" event\"]");
    const auto t = tag.next();
    CHECK(t.value == "A \\\"quoted\\\" event");
    CHECK(glyphNag("!?") == 5);
    CHECK(glyphNag("\xC2\xB1") == 16);
    CHECK(nagGlyph(4) == "??");
    CHECK(nagGlyph(10).empty());
}

TEST_CASE("PGN reader: a lichess export, its evaluations from White's side and its clocks") {
    const auto read = readPgnGame(LICHESS);
    REQUIRE(read.tree);
    CHECK(read.error.empty());
    const auto& game = read.tree->main;
    CHECK(game.header("Opening") == "Italian Game");
    CHECK(game.result == "1-0");
    REQUIRE(game.moves.size() == 9);
    CHECK(sans(game.moves) == std::vector<std::string>{"e4", "e5", "Nf3", "Nc6", "Bc4", "Nd4", "Nxe5", "Qg5", "Nxf7"});
    // [%eval] is White's view; a move's score is its mover's.
    REQUIRE(game.moves[0].score);
    CHECK(game.moves[0].score->value == 17);
    CHECK(game.moves[1].score->value == -19);
    CHECK(game.moves[0].notes.eval_tag);
    CHECK(game.moves[8].score->type == fastchess::ScoreType::MATE);
    CHECK(game.moves[8].score->value == -3);
    CHECK(game.moves[0].clock_ms == 180000);
    CHECK(game.moves[5].clock_ms == 175000);  // a clock cut by a line end
    CHECK(game.moves[5].notes.nags == std::vector<uint8_t>{6});
    CHECK(game.moves[6].notes.nags == std::vector<uint8_t>{4});
    CHECK(game.moves[0].notes.after.empty());
    roundTrip(LICHESS);
}

TEST_CASE("PGN reader: comments before and after moves, symbols, arrows and squares, nested variations") {
    const auto read = readPgnGame(ANNOTATED);
    REQUIRE(read.tree);
    CHECK(read.error.empty());
    const auto& tree = *read.tree;
    const auto& main = tree.main.moves;
    CHECK(tree.main.pre_comment == "A short game to test the notes.");
    REQUIRE(main.size() == 11);
    CHECK(main[0].notes.after == "The most popular.");
    CHECK(main[0].notes.squares == std::vector<BoardMark>{{'G', 27, -1}, {'R', 36, -1}});
    CHECK(main[0].notes.arrows == std::vector<BoardMark>{{'G', 12, 28}, {'R', 51, 35}});
    CHECK(main[1].notes.nags == std::vector<uint8_t>{1});
    CHECK(main[9].notes.nags == std::vector<uint8_t>{146});
    CHECK(main[9].notes.after == "The Najdorf.");
    CHECK(main[9].notes.emt_ms == 12000);

    REQUIRE(tree.variations.size() == 4);
    // 1... e5 2. Nf3 2... Nc6, replacing 1... c5 (ply 1).
    CHECK(tree.variations[0].parent == -1);
    CHECK(tree.variations[0].from == 1);
    CHECK(sans(tree.variations[0].moves) == std::vector<std::string>{"e5", "Nf3", "Nc6"});
    CHECK(tree.variations[0].moves[0].notes.nags == std::vector<uint8_t>{10});
    CHECK(tree.variations[0].moves[0].notes.after == "Classical.");
    // 2. f4 exf4 inside it, replacing its 2. Nf3 (ply 2).
    CHECK(tree.variations[1].parent == 0);
    CHECK(tree.variations[1].from == 2);
    CHECK(tree.variations[1].moves[0].notes.commands == std::vector<std::string>{"[%evp 0,12,25]"});
    CHECK(tree.variations[1].moves[0].notes.after == "The King's Gambit.");
    // ({Also} 1... d5 $5): a comment before its first move.
    CHECK(tree.variations[2].moves[0].notes.before == "Also");
    CHECK(tree.variations[2].moves[0].notes.nags == std::vector<uint8_t>{5});
    CHECK(tree.variations[3].moves[0].notes.after == "Dragon");
    roundTrip(ANNOTATED);
}

TEST_CASE("PGN reader: a tournament's game, its engine data and how it ended") {
    const auto read = readPgnGame(TOURNAMENT);
    REQUIRE(read.tree);
    const auto& game = read.tree->main;
    REQUIRE(game.moves.size() == 5);
    CHECK(game.moves[0].book);
    CHECK(game.moves[0].notes.after.empty());
    REQUIRE(game.moves[2].score);
    CHECK(game.moves[2].score->value == 35);
    CHECK(game.moves[2].depth == 18);
    CHECK(game.moves[2].time_ms == 512);
    CHECK(!game.moves[2].notes.eval_tag);
    CHECK(game.final_comment == "White wins by adjudication");
    // Written as a tournament writes it, how it ended with its last move's comment.
    auto text = writePgnGame(*read.tree);
    for (auto& c : text) c = c == '\n' ? ' ' : c;
    CHECK(text.find("1. e4 {book} e5 {book} 2. Nf3 {+0.35/18 0.512s} Nc6 {-0.30/17 0.498s} 3. Bb5 {+0.41/19 0.533s, "
                    "White wins by adjudication} 1-0") != std::string::npos);
    roundTrip(TOURNAMENT);
}

TEST_CASE("PGN reader and writer: a tournament's fields, read in both its styles, written back as they were") {
    // fastchess's style: the fields after the score and the time, how the game ended after the last move's.
    auto read = readPgnGame(FIELDS);
    REQUIRE(read.tree);
    CHECK(read.error.empty());
    {
        const auto& moves = read.tree->main.moves;
        REQUIRE(moves.size() == 5);
        CHECK(moves[0].engine_comment == "book");
        CHECK(moves[2].nodes == 1162043);
        CHECK(moves[2].seldepth == 32);
        CHECK(moves[2].nps == 1216798);
        CHECK(moves[2].pv == "c2c4 e7e6 g1f3 g8f6 b1c3");
        CHECK(moves[2].wdl == std::array<int64_t, 3>{193, 734, 73});
        CHECK(moves[2].decision_ms == 517);
        CHECK(moves[3].tbhits == 2);
        CHECK(moves[2].fields == (PGN_NODES | PGN_SELDEPTH | PGN_NPS | PGN_TBHITS | PGN_PV | PGN_WDL | PGN_DECISION));
        CHECK(moves[4].score->type == fastchess::ScoreType::MATE);
        CHECK(moves[4].notes.after.empty());
        CHECK(read.tree->main.final_comment == "White mates");
    }
    // Written back: every field as it was, the moves' comments as the tournament wrote them.
    auto text = flat(writePgnGame(*read.tree));
    CHECK_MESSAGE(text.find("2. c4 {+0.28/20 0.955s, n=1162043, sd=32, nps=1216798, tbhits=0, pv=\"c2c4 e7e6 g1f3 g8f6 "
                            "b1c3\", wdl=193/734/73, dt=0.517s} e6 {-0.19/18 0.423s, n=519779, sd=35, nps=1228791, "
                            "tbhits=2, pv=\"e7e6 b1c3 f8b4\", wdl=86/746/168, dt=0.012s} 3. Nc3 {+M5/40 0.165s, "
                            "n=191573, sd=32, nps=1161048, tbhits=0, pv=\"b1c3 f8b4\", wdl=1000/0/0, dt=0.001s, White "
                            "mates} 1-0") != std::string::npos,
                  text);
    roundTrip(FIELDS);

    // The ChessBase style: its commands read, the fields after them too; nothing of it left in the notes.
    read = readPgnGame(CHESSBASE_STYLE);
    REQUIRE(read.tree);
    CHECK(read.error.empty());
    {
        const auto& moves = read.tree->main.moves;
        REQUIRE(moves.size() == 5);
        CHECK(moves[2].score->value == 28);
        CHECK(moves[2].depth == 20);
        CHECK(moves[2].notes.eval_tag);
        CHECK(moves[2].clock_ms == 37409);
        CHECK(moves[2].notes.emt_ms == 955);
        CHECK(moves[2].nodes == 1162043);
        CHECK(moves[2].pv == "c2c4 e7e6 g1f3 g8f6 b1c3");
        CHECK(moves[3].wdl == std::array<int64_t, 3>{86, 746, 168});
        CHECK(moves[3].score->value == -19);  // White's 0.19: Black's -0.19
        CHECK(moves[3].tbhits == 2);
        CHECK(moves[4].decision_ms == 1);
        for (const auto& m : moves) {
            CHECK(m.notes.after.empty());
            CHECK(m.notes.commands.empty());
        }
        CHECK(moves[2].engine_comment ==
              "[%eval 0.28,20] [%clk 0:00:37.409] [%emt 0:00:00.955], n=1162043, sd=32, "
              "nps=1216798, tbhits=0, pv=\"c2c4 e7e6 g1f3 g8f6 b1c3\", wdl=193/734/73, "
              "dt=0.517s");
        CHECK(read.tree->main.final_comment == "Draw by 3-fold repetition");
    }
    // Written back as it was read: its commands in their order, the clock's thousandths, its fields.
    text = flat(writePgnGame(*read.tree));
    CHECK_MESSAGE(
        text.find("e6 {[%eval 0.19,18] [%clk 0:00:36.000] [%emt 0:00:00.423], n=519779, sd=35, nps=1228791, "
                  "tbhits=2, pv=\"e7e6 b1c3 f8b4\", wdl=86/746/168, dt=0.012s} 3. Nc3 {[%eval 0.00,44] "
                  "[%clk 0:00:22.103] [%emt 0:00:01.708], n=1965029, sd=43, nps=1150485, tbhits=4, "
                  "pv=\"b1c3\", wdl=30/940/30, dt=0.001s, Draw by 3-fold repetition} 1/2-1/2") != std::string::npos,
        text);
    roundTrip(CHESSBASE_STYLE);

    // A comment of the user's beside a tournament's, and a mark: both kept, the tournament's first, as it was.
    auto tree                      = *readPgnGame(FIELDS).tree;
    tree.main.moves[2].notes.after = "The Queen's Gambit.";
    tree.main.moves[2].notes.arrows.push_back({'G', 12, 28});
    text = flat(writePgnGame(tree));
    CHECK_MESSAGE(text.find("2. c4 {+0.28/20 0.955s, n=1162043") != std::string::npos, text);
    CHECK_MESSAGE(text.find("dt=0.517s} {[%cal Ge2e4] The Queen's Gambit.} 2... e6 {-0.19/18") != std::string::npos,
                  text);
    const auto again = readPgnGame(writePgnGame(tree));
    REQUIRE(again.tree);
    CHECK(again.tree->main.moves[2].notes.after == "The Queen's Gambit.");
    CHECK(again.tree->main.moves[2].engine_comment == tree.main.moves[2].engine_comment);

    // A score that is no longer the one the comment says: written anew from the move's data, its fields with it.
    tree.main.moves[3].score = fastchess::Score{fastchess::ScoreType::CP, -45};
    text                     = flat(writePgnGame(tree));
    CHECK_MESSAGE(text.find("e6 {-0.45/18 0.423s, n=519779, sd=35, nps=1228791, tbhits=2, pv=\"e7e6 b1c3 f8b4\", "
                            "wdl=86/746/168, dt=0.012s}") != std::string::npos,
                  text);
    CHECK(engineComment(tree.main.moves[3]) ==
          "-0.45/18 0.423s, n=519779, sd=35, nps=1228791, tbhits=2, pv=\"e7e6 b1c3 f8b4\", wdl=86/746/168, dt=0.012s");
    CHECK(engineComment(tree.main.moves[2]) == tree.main.moves[2].engine_comment);

    // A comment of [%clk] and [%emt] alone, from elsewhere, reads and writes back the same.
    roundTrip("[Event \"?\"]\n\n1. e4 {[%emt 0:00:05]} 1... e5 $1 {[%clk 0:01:00] [%emt 0:00:02]} *\n");
}

TEST_CASE("PGN reader: what goes wrong is told, and what was read before it kept") {
    // An illegal move ends the game's moves there.
    auto read = readPgnGame("[Event \"?\"]\n\n1. e4 e5 2. Ke3 Nc6 *\n");
    REQUIRE(read.tree);
    CHECK(read.error == "illegal move Ke3");
    CHECK(read.tree->main.moves.size() == 2);
    // A variation not closed: its moves are kept, and it is said.
    read = readPgnGame("[Event \"?\"]\n\n1. e4 e5 (1... c5 2. Nf3 *\n");
    REQUIRE(read.tree);
    CHECK(read.error == "a variation is not closed");
    CHECK(read.tree->variations.size() == 1);
    // A start position that is none: no game.
    read = readPgnGame("[Event \"?\"]\n[FEN \"8/8/8/8/8/8/8/8 w - - 0 1\"]\n\n*\n");
    CHECK(!read.tree);
    // No game at all.
    CHECK(!readPgnGame("   \n").tree);
    // An empty variation and one whose first move is illegal leave nothing behind.
    read = readPgnGame("[Event \"?\"]\n\n1. e4 () e5 (1... Ke7) (1... c5) *\n");
    REQUIRE(read.tree);
    CHECK(read.tree->variations.size() == 1);
    CHECK(read.tree->variations[0].moves[0].san == "c5");
}

TEST_CASE("PGN reader: the next game, the text's code page, Chess960 and null moves") {
    const std::string two = "[Event \"One\"]\n\n1. e4 *\n\n[Event \"Two\"]\n\n1. d4 *\n";
    const auto first      = readPgnGame(two);
    REQUIRE(first.tree);
    CHECK(first.tree->main.header("Event") == "One");
    const auto second = readPgnGame(std::string_view(two).substr(first.end));
    REQUIRE(second.tree);
    CHECK(second.tree->main.header("Event") == "Two");
    CHECK(second.tree->main.moves[0].san == "d4");

    // Windows-1252 in an older file: read as UTF-8.
    const auto latin = readPgnGame("[White \"Ren\xE9\"]\n\n1. e4 {Tr\xE8s bon} *\n");
    REQUIRE(latin.tree);
    CHECK(latin.tree->main.header("White") == "Ren\xC3\xA9");
    CHECK(latin.tree->main.moves[0].notes.after == "Tr\xC3\xA8s bon");

    // Chess960: castling with the king on f1 and the rook on h1, read and written back the same.
    const char* c960 =
        "[Variant \"Chess960\"]\n[SetUp \"1\"]\n[FEN \"r4k1r/pppppppp/8/8/8/8/PPPPPPPP/R4K1R w HAha - 0 1\"]\n\n"
        "1. O-O O-O 2. Rad1 Rad8 *\n";
    const auto chess960 = readPgnGame(c960);
    REQUIRE(chess960.tree);
    CHECK(chess960.chess960);
    CHECK(chess960.error.empty());
    CHECK(chess960.tree->main.moves.size() == 4);
    roundTrip(c960);

    // A null move in a variation, as analysts write a threat.
    const char* null_move = "[Event \"?\"]\n\n1. e4 e5 2. Nf3 (2. -- d5) 2... Nc6 *\n";
    const auto with_null  = readPgnGame(null_move);
    REQUIRE(with_null.tree);
    REQUIRE(with_null.tree->variations.size() == 1);
    CHECK(with_null.tree->variations[0].moves[0].move == chess::Move::NULL_MOVE);
    CHECK(writePgnGame(*with_null.tree).find("(2. -- d5)") != std::string::npos);
    roundTrip(null_move);
}

TEST_CASE("PGN reader and writer: the assistant's comment beside another, two comments in a row, back to its slot") {
    // A game analysed by Lichess, its comment on 3...Nd4?! kept as it was; the assistant's after it, marked (#60).
    const std::string pgn =
        "[Event \"?\"]\n\n1. e4 e5 2. Nf3 Nc6 3. Bc4 Nd4?! { [%eval 0.85] Inaccuracy. Nxd4 was best. } "
        "{ [%rs assistant] Black moves the knight a second time, and the pawn on e5 is left. } 4. Nxe5 "
        "{ [%clk 0:02:50] [%rs assistant] The knight takes the pawn. } 4... Qg5 *\n";
    const auto read = readPgnGame(pgn);
    REQUIRE(read.tree);
    CHECK(read.error.empty());
    const auto& moves = read.tree->main.moves;
    REQUIRE(moves.size() == 8);
    CHECK(moves[5].notes.after == "Inaccuracy. Nxd4 was best.");
    CHECK(moves[5].notes.assistant == "Black moves the knight a second time, and the pawn on e5 is left.");
    CHECK(moves[5].notes.commands.empty());  // the mark is the slot, not a command kept
    CHECK(moves[5].notes.eval_tag);
    CHECK(moves[6].notes.after.empty());
    CHECK(moves[6].notes.assistant == "The knight takes the pawn.");
    CHECK(moves[6].clock_ms == 170000);
    // Written: the other comment first, as it was, then the assistant's, a comment of its own; alone, the assistant's
    // is one comment with the move's commands, as before.
    auto text = writePgnGame(*read.tree);
    for (auto& c : text) c = c == '\n' ? ' ' : c;
    CHECK_MESSAGE(text.find("Nd4?! {[%eval 0.85] Inaccuracy. Nxd4 was best.} {[%rs assistant] Black moves the knight a "
                            "second time, and the pawn on e5 is left.} 4. Nxe5 {[%clk 0:02:50] [%rs assistant] The "
                            "knight takes the pawn.}") != std::string::npos,
                  text);
    roundTrip(pgn.c_str());
    // The marked comment first: it still goes to its slot, the other to the comment's.
    const auto swapped = readPgnGame("[Event \"?\"]\n\n1. e4 { [%rs assistant] The centre. } { Mine. } *\n");
    REQUIRE(swapped.tree);
    CHECK(swapped.tree->main.moves[0].notes.after == "Mine.");
    CHECK(swapped.tree->main.moves[0].notes.assistant == "The centre.");
    // For another program, without the mark: two plain comments.
    PgnWriteOptions plain;
    plain.assistant_mark = false;
    auto unmarked        = writePgnGame(*read.tree, plain);
    for (auto& c : unmarked) c = c == '\n' ? ' ' : c;
    CHECK(unmarked.find("[%rs") == std::string::npos);
    CHECK(unmarked.find("Nxd4 was best.} {Black moves the knight") != std::string::npos);
}

TEST_CASE("PGN writer: a long comment wraps between its words, and reads back whole") {
    std::string comment;
    for (int i = 0; i < 40; ++i) comment += "word" + std::to_string(i) + " ";
    const std::string pgn = "[Event \"?\"]\n\n1. e4 {" + comment + "} 1... e5 *\n";
    const auto read       = readPgnGame(pgn);
    REQUIRE(read.tree);
    CHECK(read.tree->main.moves[0].notes.after.size() == comment.size() - 1);
    roundTrip(pgn.c_str());
}

TEST_CASE("PGN reader and writer: any text, the samples changed at random, written back to a fixpoint") {
    std::mt19937 random(20261117);
    for (const char* sample : {LICHESS, ANNOTATED, TOURNAMENT, FIELDS, CHESSBASE_STYLE}) {
        const std::string base(sample);
        for (int round = 0; round < 1500; ++round) {
            auto text           = base;
            const int mutations = 1 + static_cast<int>(random() % 6);
            for (int m = 0; m < mutations; ++m) {
                const auto at = random() % text.size();
                switch (random() % 4) {
                    case 0:
                        text[at] = static_cast<char>(random() % 256);
                        break;
                    case 1:
                        text.erase(at, 1 + random() % 8);
                        break;
                    case 2:
                        text.insert(at, 1, "(){};$!?%[]\\"[random() % 12]);
                        break;
                    default:
                        text.insert(at, base.substr(random() % base.size(), random() % 24));
                        break;
                }
                if (text.empty()) text = " ";
            }
            const auto read = readPgnGame(text);
            if (!read.tree) continue;
            const auto first = writePgnGame(*read.tree, {read.chess960, {}});
            const auto again = readPgnGame(first);
            REQUIRE(again.tree);
            const auto second = writePgnGame(*again.tree, {again.chess960, {}});
            if (second != first) {
                INFO(text);
                INFO(first);
                CHECK(second == first);
                break;
            }
        }
    }
}
