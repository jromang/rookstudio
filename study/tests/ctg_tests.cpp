#include <doctest/doctest.hpp>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <chess.hpp>
#include <study/ctg.hpp>
#include <study/pgn_reader.hpp>

#include "test_games.hpp"

using namespace fcg::study;
using namespace fcg::test;
namespace fs = std::filesystem;

// ctgbook.ctg was made of the 42 games of ctgbook.pgn, our own, to 50 plies. They share their openings (1.d4 Nf6 2.c4
// e6 and 1.c4 e6 2.d4 Nf6 meet), take en passant on either side, lose castling with their kings on files a to d (the
// lookups with files reversed), castle on both sides, promote to a queen and to a knight, and one ends in a mate. In
// the book, after the games: 1.e4 the main move (green), 1.d4 not to play in a tournament (red), 1.c4!, 1.a4?, 1.g4
// the only move, and the position after 1.Nf3 +=.

namespace {

constexpr std::size_t BOOK_PLIES = 50;

struct Expected {
    TreeStats games;
    std::set<std::string> moves;  // UCI
};

std::vector<std::string> past_plies;  // the positions of the longest games one ply past the book's

std::string keyOf(const chess::Board& board) { return board.getFen(false); }

// Each position the games reach within the book's plies: their games, counted once a game, and the moves they play.
std::map<std::string, Expected> fromGames() {
    past_plies.clear();
    std::ifstream in(data("ctgbook.pgn"), std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::map<std::string, Expected> out;
    std::string_view rest(text);
    int games = 0;
    while (true) {
        const auto read = readPgnGame(rest);
        if (!read.tree) break;
        ++games;
        const auto& game     = read.tree->main;
        const auto result    = game.result;
        const auto white_elo = game.header("WhiteElo");
        const auto black_elo = game.header("BlackElo");
        chess::Board board;
        std::set<std::string> seen;
        for (std::size_t ply = 0; ply <= std::min(BOOK_PLIES, game.moves.size()); ++ply) {
            const auto key = keyOf(board);
            auto& e        = out[key];
            if (seen.insert(key).second) {
                ++e.games.games;
                if (result == "1-0")
                    ++e.games.white;
                else if (result == "0-1")
                    ++e.games.black;
                else
                    ++e.games.draws;
                if (!white_elo.empty()) {
                    ++e.games.white_rated;
                    e.games.white_elo += std::stoul(white_elo);
                }
                if (!black_elo.empty()) {
                    ++e.games.black_rated;
                    e.games.black_elo += std::stoul(black_elo);
                }
            }
            if (ply == BOOK_PLIES && game.moves.size() > BOOK_PLIES) {
                auto past = board;
                past.makeMove(game.moves[ply].move);
                past_plies.push_back(past.getFen());
            }
            if (ply == game.moves.size() || ply == BOOK_PLIES) break;
            e.moves.insert(chess::uci::moveToUci(game.moves[ply].move));
            board.makeMove(game.moves[ply].move);
        }
        rest.remove_prefix(read.end);
    }
    REQUIRE(games == 42);
    return out;
}

std::set<std::string> movesOf(const CtgAnswer& answer) {
    std::set<std::string> out;
    for (const auto& m : answer.moves) out.insert(chess::uci::moveToUci(m.move));
    return out;
}

const CtgMove* moveOf(const CtgAnswer& answer, const std::string& uci) {
    for (const auto& m : answer.moves)
        if (chess::uci::moveToUci(m.move) == uci) return &m;
    return nullptr;
}

}  // namespace

TEST_CASE("CTG book: every position of the games it was made of, with their games and their moves") {
    const CtgBook book(data("ctgbook.ctg"));
    CHECK(book.games() == 42);
    CHECK(book.positions() == 404);
    const auto expected = fromGames();
    CHECK(expected.size() == 404);
    for (const auto& [fen, e] : expected) {
        CAPTURE(fen);
        const chess::Board board(fen + " 0 1");
        const auto answer = book.at(board);
        REQUIRE(answer.has_value());
        CHECK(answer->games == e.games);
        CHECK(movesOf(*answer) == e.moves);
        for (const auto& m : answer->moves) {  // each move's games are those of the position it leads to
            auto after = board;
            after.makeMove(m.move);
            const auto it = expected.find(keyOf(after));
            REQUIRE(it != expected.end());
            REQUIRE(m.games.has_value());
            CHECK(*m.games == it->second.games);
        }
    }
    CHECK(past_plies.size() == 2);
    for (const auto& fen : past_plies) CHECK_FALSE(book.at(chess::Board(fen)).has_value());
}

TEST_CASE("CTG book: positions looked up mirrored, with files reversed, with en passant") {
    const CtgBook book(data("ctgbook.ctg"));
    const auto line = [&](const std::string& moves) {
        chess::Board board;
        std::string word;
        for (const char c : moves + " ") {
            if (c != ' ') {
                word += c;
                continue;
            }
            if (!word.empty()) board.makeMove(chess::uci::uciToMove(board, word));
            word.clear();
        }
        return book.at(board);
    };
    // Black to move: looked up mirrored.
    const auto e4 = line("e2e4");
    REQUIRE(e4);
    CHECK(e4->games.games == 23);
    CHECK(movesOf(*e4) == std::set<std::string>{"e7e5", "c7c5", "g8f6"});
    // Nobody may castle, the king of the side to move on file d: files reversed, for White then for Black.
    const auto kd7 = line("d2d4 d7d5 e1d2 e8d7");
    REQUIRE(kd7);
    CHECK(movesOf(*kd7) == std::set<std::string>{"d2c3"});
    const auto kc3 = line("d2d4 d7d5 e1d2 e8d7 d2c3");
    REQUIRE(kc3);
    CHECK(movesOf(*kc3) == std::set<std::string>{"d7c6"});
    // En passant possible for White, then for Black, and taken.
    const auto white_ep = line("e2e4 g8f6 e4e5 d7d5");
    REQUIRE(white_ep);
    CHECK(movesOf(*white_ep).count("e5d6") == 1);
    const auto black_ep = line("g1f3 d7d5 b1c3 d5d4 e2e4");
    REQUIRE(black_ep);
    CHECK(movesOf(*black_ep).count("d4e3") == 1);
    // A promotion to a knight: the format writes it as the queen's move, the position after it tells it apart.
    const auto promotion = line("a2a4 b7b5 a4b5 a7a6 b5a6 c8b7 a6b7 b8c6");
    REQUIRE(promotion);
    CHECK(movesOf(*promotion) == std::set<std::string>{"b7a8q", "b7a8n"});
    // The mate that ends a game: its position, with no move.
    const auto mate = line("g2g4 e7e5 f2f3 d8h4");
    REQUIRE(mate);
    CHECK(mate->games.games == 1);
    CHECK(mate->games.black == 1);
    CHECK(mate->moves.empty());
    // Positions the games never reached, or past the book's plies.
    CHECK_FALSE(line("h2h4").has_value());
}

TEST_CASE("CTG book: a move's colour, its symbol and the evaluation after it") {
    const CtgBook book(data("ctgbook.ctg"));
    const auto start = book.at(chess::Board());
    REQUIRE(start);
    CHECK(start->games.games == 42);
    CHECK(start->games.white == 21);
    CHECK(start->games.draws == 12);
    CHECK(start->games.black == 9);
    CHECK(start->moves.size() == 6);
    const auto* e4  = moveOf(*start, "e2e4");
    const auto* d4  = moveOf(*start, "d2d4");
    const auto* c4  = moveOf(*start, "c2c4");
    const auto* nf3 = moveOf(*start, "g1f3");
    const auto* a4  = moveOf(*start, "a2a4");
    const auto* g4  = moveOf(*start, "g2g4");
    REQUIRE((e4 && d4 && c4 && nf3 && a4 && g4));
    CHECK(e4->main);
    CHECK_FALSE(e4->avoid);
    CHECK(d4->avoid);
    CHECK_FALSE(d4->main);
    CHECK(c4->symbol == 1);
    CHECK(a4->symbol == 2);
    CHECK(g4->symbol == 8);
    CHECK(e4->symbol == 0);
    CHECK(nf3->evaluation == 14);  // += : White, who played it, is slightly better
    CHECK(e4->evaluation == 0);
    // The games after 1.e4, as White and Black, with the players' ratings.
    REQUIRE(e4->games);
    CHECK(e4->games->games == 23);
    CHECK(e4->games->white == 13);
    CHECK(e4->games->draws == 5);
    CHECK(e4->games->black == 5);
    CHECK(e4->games->white_rated == 20);
    CHECK(e4->games->white_elo == 46226);
    CHECK(e4->games->black_elo == 42573);
}

TEST_CASE("CTG book: what a whole book holds") {
    const CtgBook book(data("ctgbook.ctg"));
    const auto expected = fromGames();
    uint64_t codes      = 0;  // a promotion to a queen and to a knight are one code
    for (const auto& [fen, e] : expected) {
        std::set<std::string> squares;
        for (const auto& m : e.moves) squares.insert(m.substr(0, 4));
        codes += squares.size();
    }
    const auto s = book.summary();
    CHECK(s.positions == 404);
    CHECK(s.moves == codes);
    CHECK(s.main == 1);
    CHECK(s.avoid == 1);
    CHECK_FALSE(s.cancelled);
    std::atomic<bool> cancel{true};
    CHECK(book.summary(&cancel).cancelled);
}

TEST_CASE("CTG book: opened by any of its files, refused without one of them") {
    const auto dir = fs::temp_directory_path() / "ctg-open";
    fs::create_directories(dir);
    for (const char* ext : {".ctg", ".ctb", ".cto"})
        fs::copy_file(data(std::string("ctgbook") + ext), dir / (std::string("Book") + ext),
                      fs::copy_options::overwrite_existing);
    for (const char* ext : {".ctg", ".ctb", ".cto"}) {
        const CtgBook book(dir / (std::string("Book") + ext));
        CHECK(book.path() == dir / "Book.ctg");
        CHECK(book.at(chess::Board()).has_value());
    }
    CHECK_THROWS_AS(CtgBook(dir / "Book.bin"), std::runtime_error);
    fs::remove(dir / "Book.cto");
    CHECK_THROWS_AS(CtgBook(dir / "Book.ctg"), std::runtime_error);
    CHECK_THROWS_AS(CtgBook(dir / "None.ctg"), std::runtime_error);
}

TEST_CASE("CTG book: damaged pages answer nothing, never past the files") {
    const auto dir = fs::temp_directory_path() / "ctg-damaged";
    fs::create_directories(dir);
    for (const char* ext : {".ctb", ".cto"})
        fs::copy_file(data(std::string("ctgbook") + ext), dir / (std::string("Book") + ext),
                      fs::copy_options::overwrite_existing);
    std::ifstream in(data("ctgbook.ctg"), std::ios::binary);
    std::string ctg((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    // Every page claims more positions and bytes than a page holds, and its first entry has no length.
    for (std::size_t page = 4096; page < ctg.size(); page += 4096) {
        ctg[page] = ctg[page + 1] = ctg[page + 2] = ctg[page + 3] = '\xff';
        ctg[page + 4]                                             = '\0';
    }
    std::ofstream(dir / "Book.ctg", std::ios::binary) << ctg;
    const CtgBook book(dir / "Book.ctg");
    CHECK_FALSE(book.at(chess::Board()).has_value());
    CHECK(book.summary().positions == 0);
    // An index that points past the pages.
    std::string cto(76, '\x7f');
    cto.replace(0, 16, std::string(16, '\0'));
    std::ofstream(dir / "Book.cto", std::ios::binary) << cto;
    CHECK_FALSE(CtgBook(dir / "Book.ctg").at(chess::Board()).has_value());
}
