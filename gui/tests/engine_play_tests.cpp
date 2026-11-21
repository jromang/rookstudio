// Playing against an engine (#83): the handicap given, the strength told to the engine, the openings picked, the
// draw it takes, the games played and their tally, a game kept and read back, the Play page's choice kept.

#include <doctest/doctest.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <random>

#include "engine_play.hpp"
#include "i18n.hpp"

using namespace fcg;
namespace fs = std::filesystem;

namespace {

fs::path freshDir(const char* name) {
    const auto dir =
        fs::temp_directory_path() /
        (std::string(name) + "-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(dir);
    return dir;
}

EngineEntry withOptions(std::vector<UciOptionInfo> options) {
    EngineEntry e;
    e.name        = "Engine";
    e.uci_options = std::move(options);
    return e;
}

chess::Move uciMove(const std::string& fen, const std::string& uci) {
    chess::Board board(fen);
    return chess::uci::uciToMove(board, uci);
}

}  // namespace

TEST_CASE("play: the man the engine gives, from its side") {
    const std::string start(chess::constants::STARTPOS);
    using H = Handicap;
    CHECK(handicapFen(start, H::Pawn, chess::Color::BLACK) ==
          "rnbqkbnr/ppppp1pp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    CHECK(handicapFen(start, H::Pawn, chess::Color::WHITE) ==
          "rnbqkbnr/pppppppp/8/8/8/8/PPPPP1PP/RNBQKBNR w KQkq - 0 1");
    CHECK(handicapFen(start, H::Knight, chess::Color::WHITE) ==
          "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/R1BQKBNR w KQkq - 0 1");
    // The a-rook gone, castling on its side with it.
    CHECK(handicapFen(start, H::Rook, chess::Color::BLACK) ==
          "1nbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQk - 0 1");
    CHECK(handicapFen(start, H::Rook, chess::Color::WHITE) ==
          "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/1NBQKBNR w Kkq - 0 1");
    CHECK(handicapFen(start, H::Queen, chess::Color::BLACK) ==
          "rnb1kbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    // Every one of them makes a position that can be played.
    for (const auto h : {H::Pawn, H::Knight, H::Rook, H::Queen}) {
        for (const auto side : {chess::Color::WHITE, chess::Color::BLACK}) {
            const auto fen = handicapFen(start, h, side);
            REQUIRE(fen);
            chess::Board board;
            CHECK(board.setFen(*fen));
            chess::Movelist moves;
            chess::movegen::legalmoves(moves, board);
            CHECK(moves.size() > 0);
        }
    }
    // The queen elsewhere: the only one it has; two queens, or none: nothing to give.
    const std::string moved = "rnb1kbnr/pppp1ppp/8/4p3/4P2q/8/PPPP1PPP/RNBQKBNR w KQkq - 2 3";
    CHECK(handicapFen(moved, H::Queen, chess::Color::BLACK) ==
          "rnb1kbnr/pppp1ppp/8/4p3/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 2 3");
    CHECK(handicapTag(moved, H::Queen, chess::Color::BLACK) == "queen h4");
    CHECK_FALSE(handicapFen("4k3/8/8/8/8/8/8/4K3 w - - 0 1", H::Queen, chess::Color::WHITE));
    CHECK_FALSE(handicapFen("4k3/8/8/8/8/8/8/QQ2K3 w - - 0 1", H::Queen, chess::Color::WHITE));
    // A man not where it starts, nothing to give, Chess960, a FEN that is none.
    CHECK_FALSE(
        handicapFen("rnbqkbnr/pppppppp/8/8/8/5P2/PPPPP1PP/RNBQKBNR b KQkq - 0 1", H::Pawn, chess::Color::WHITE));
    CHECK_FALSE(handicapFen(start, H::None, chess::Color::WHITE));
    CHECK_FALSE(handicapFen(start, H::Knight, chess::Color::WHITE, true));
    CHECK_FALSE(handicapFen("not a fen", H::Knight, chess::Color::WHITE));
    // A FEN of four fields gets its move counters.
    CHECK(handicapFen("4k3/8/8/8/8/8/8/1N2K3 w - -", H::Knight, chess::Color::WHITE) ==
          "4k3/8/8/8/8/8/8/4K3 w - - 0 1");
    CHECK(handicapTag(start, H::Pawn, chess::Color::BLACK) == "pawn f7");
    CHECK(handicapTag(start, H::Rook, chess::Color::WHITE) == "rook a1");
    CHECK(handicapTag(start, H::None, chess::Color::WHITE).empty());
}

TEST_CASE("play: the strength told to the engine") {
    const auto elo = withOptions(
        {{"UCI_LimitStrength", "check", "false", "", "", {}}, {"UCI_Elo", "spin", "2640", "580", "2640", {}}});
    const auto plain = withOptions({{"Hash", "spin", "16", "1", "1024", {}}});
    REQUIRE(eloRange(elo));
    CHECK(eloRange(elo)->min == 580);
    CHECK(eloRange(elo)->max == 2640);
    CHECK_FALSE(eloRange(plain));
    CHECK_FALSE(eloRange(withOptions({{"UCI_Elo", "spin", "1500", "x", "y", {}}})));

    PlaySettings s;
    s.elo   = 1800;
    s.nodes = 3000;
    CHECK(strengthFor(s, elo) == Strength{1800, 0});
    CHECK(strengthFor(s, plain) == Strength{0, 3000});
    s.elo = 100;  // under its range
    CHECK(strengthFor(s, elo).elo == 580);
    s.full = true;
    CHECK(strengthFor(s, elo).full());
    CHECK(strengthFor(s, plain).full());
    CHECK(nodeSteps().front() == 1);
    CHECK(nodeSteps().back() == 1000000);

    // Its copy given the options of an Elo; at its full strength, whatever its card limits left aside.
    auto copy = elo;
    applyStrength(copy, {1500, 0});
    REQUIRE(copy.option("UCI_LimitStrength"));
    CHECK(*copy.option("UCI_LimitStrength") == "true");
    CHECK(*copy.option("UCI_Elo") == "1500");
    copy.setOption("UCI_LimitStrength", "true");
    applyStrength(copy, {});
    CHECK(*copy.option("UCI_LimitStrength") == "false");
    auto other = plain;
    applyStrength(other, {0, 1000});
    CHECK(other.options.empty());

    CHECK(strengthTag({1800, 0}) == "UCI_Elo 1800");
    CHECK(strengthTag({0, 1000}) == "nodes 1000");
    CHECK(strengthTag({}) == "full");
    i18n::setLanguage(i18n::Language::English);
    CHECK(strengthText({1800, 0}) == "Elo 1800");
    CHECK(strengthText({0, 1000}) == "1,000 nodes a move");
    CHECK(strengthText({0, 1}) == "1 node a move");
    CHECK(strengthText({}) == "full strength");
}

TEST_CASE("play: the openings picked, at random or the most played, then the engine") {
    const std::string start(chess::constants::STARTPOS);
    const auto e4 = uciMove(start, "e2e4"), d4 = uciMove(start, "d2d4"), c4 = uciMove(start, "c2c4");
    std::mt19937 rng(83);
    // The most played, the main one first.
    CHECK(pickOpening({{e4, 10}, {d4, 30}, {c4, 2}}, false, 5, rng) == d4);
    CHECK(pickOpening({{e4, 10, true}, {d4, 30}}, false, 1, rng) == e4);
    // Nothing played often enough: the engine plays.
    CHECK_FALSE(pickOpening({{e4, 4}, {d4, 3}}, true, 5, rng));
    CHECK_FALSE(pickOpening({}, false, 1, rng));
    // Varying: each move played often enough, as often as it was played.
    std::map<uint16_t, int> picks;
    for (int i = 0; i < 4000; ++i) {
        const auto m = pickOpening({{e4, 30}, {d4, 10}, {c4, 2}}, true, 5, rng);
        REQUIRE(m);
        ++picks[m->move()];
    }
    CHECK(picks.count(c4.move()) == 0);
    CHECK(picks[e4.move()] > 2 * picks[d4.move()]);
    CHECK(picks[d4.move()] > 500);

    // A book: the source's moves that are legal, in UCI, castling as the board writes it.
    const std::string castle = "r3k2r/pppqbppp/2npbn2/4p3/4P3/2NPBN2/PPPQBPPP/R3K2R w KQkq - 6 8";
    OpeningBook book(
        [&](const chess::Board& board) {
            std::vector<OpeningChoice> out;
            if (board.getFen() == castle) {
                chess::Movelist moves;
                chess::movegen::legalmoves(moves, board);
                for (const auto m : moves)
                    if (m.typeOf() == chess::Move::CASTLING && m.to() > m.from()) out.push_back({m, 9});
                out.push_back({uciMove(start, "e2e4"), 50});  // not legal here: left out
            }
            return out;
        },
        false, 1, 7);
    CHECK(book.move(chess::Board(castle)) == "e1g1");
    CHECK_FALSE(book.move(chess::Board(start)));
    CHECK_FALSE(OpeningBook({}, true, 1, 1).move(chess::Board(start)));

    // The draw the engine takes.
    using S = fastchess::Score;
    using T = fastchess::ScoreType;
    CHECK_FALSE(engineAcceptsDraw(std::nullopt, 80));
    CHECK(engineAcceptsDraw(S{T::MATE, -3}, 10));
    CHECK_FALSE(engineAcceptsDraw(S{T::MATE, 3}, 80));
    CHECK(engineAcceptsDraw(S{T::CP, -200}, 4));
    CHECK_FALSE(engineAcceptsDraw(S{T::CP, 0}, 39));
    CHECK(engineAcceptsDraw(S{T::CP, 0}, 40));
    CHECK(engineAcceptsDraw(S{T::CP, 10}, 60));
    CHECK_FALSE(engineAcceptsDraw(S{T::CP, 40}, 60));
}

TEST_CASE("play: the games played, their tally, where they ended") {
    i18n::setLanguage(i18n::Language::English);
    const auto dir = freshDir("fcg-play-games");
    CHECK(readPlayedGames(dir).empty());
    const auto game = [](const char* engine, const char* strength, bool white, const char* result, bool aids = false) {
        PlayedGame g;
        g.engine   = engine;
        g.strength = strength;
        g.white    = white;
        g.result   = result;
        g.aids     = aids;
        g.plies    = 60;
        g.at       = 1700000000;
        return g;
    };
    auto lost      = game("Gaia", "UCI_Elo 1500", true, "0-1");
    lost.final_fen = "8/5k2/8/3r4/8/2R5/5K2/8 w - - 0 50";
    lost.key       = "abc";
    for (const auto& g : {game("Gaia", "UCI_Elo 1500", true, "1-0"), game("Gaia", "UCI_Elo 1500", false, "1-0"),
                          game("Gaia", "UCI_Elo 1500", false, "1/2-1/2"), lost,
                          game("Gaia", "UCI_Elo 1500", true, "1-0", true), game("Gaia", "UCI_Elo 1500", true, "*"),
                          game("Gaia", "UCI_Elo 1600", true, "1-0"), game("Other", "UCI_Elo 1500", true, "1-0")})
        appendPlayedGame(dir, g);
    std::ofstream(dir / "games.jsonl", std::ios::app) << "a line damaged\n";
    const auto read = readPlayedGames(dir);
    REQUIRE(read.size() == 8);
    CHECK(read[3].final_fen == lost.final_fen);
    CHECK(read[3].key == "abc");
    CHECK(userScore(read[0]) == 1);
    CHECK(userScore(read[1]) == -1);
    CHECK(userScore(read[2]) == 0);
    CHECK_FALSE(userScore(read[5]));
    const auto t = playTally(read, "Gaia", "UCI_Elo 1500");
    CHECK(t.games == 4);
    CHECK(t.won == 1);
    CHECK(t.drawn == 1);
    CHECK(t.lost == 2);
    CHECK(t.with_aids == 1);
    REQUIRE(t.last_lost);
    CHECK(t.last_lost->key == "abc");
    CHECK(playTally(read, "Gaia", "nodes 1000").games == 0);

    CHECK(endingOf(std::string(chess::constants::STARTPOS), 10) == "the opening");
    CHECK(endingOf("r1bq1rk1/pp2bppp/2n1pn2/3p4/3P4/2NBPN2/PP3PPP/R1BQ1RK1 w - - 0 10", 40) == "the middlegame");
    CHECK(endingOf(lost.final_fen, 99) == "a rook endgame");
    CHECK(endingOf("8/5k2/8/3p4/8/2P5/5K2/8 w - - 0 50", 99) == "a pawn endgame");
    CHECK(endingOf("8/5k2/8/3q4/8/2Q5/5K2/8 w - - 0 50", 99) == "a queen endgame");
    CHECK(endingOf("8/5k2/2b5/8/8/2N5/5K2/8 w - - 0 50", 99) == "an endgame of minor pieces");
    CHECK(endingOf("8/5k2/2b5/3r4/8/2N1R3/5K2/8 w - - 0 50", 99) == "an endgame");
    fs::remove_all(dir);
}

TEST_CASE("play: a game kept, read back, and its moves played again") {
    const auto dir = freshDir("fcg-play-kept");
    CHECK_FALSE(readKeptGame(dir));
    KeptGame k;
    k.settings.engine   = "Gaia";
    k.settings.openings = Openings::Repertoire;
    k.settings.handicap = Handicap::Knight;
    k.engine            = "Gaia";
    k.strength          = {1500, 0};
    k.openings_tag      = "repertoire Najdorf";
    k.openings_text     = "your repertoire “Najdorf”";
    k.white             = false;
    k.start_fen         = std::string(chess::constants::STARTPOS);
    k.headers           = {{"White", "Gaia"}, {"Black", "Tester"}};
    k.moves             = {"e2e4", "c7c5", "g1f3"};
    k.times             = {100, 5000, 200};
    k.clocks_before     = {{60000, 60000}, {59900, 60000}, {59900, 55000}};
    k.clock_ms          = {59700, 55000};
    k.clocks            = {GameClock{60, 1}, GameClock{120, 2}};
    k.takebacks         = 2;
    k.hints_used        = 1;
    k.aids              = true;
    k.last_book         = 2;
    k.book_out          = true;
    REQUIRE(writeKeptGame(dir, k));
    const auto read = readKeptGame(dir);
    REQUIRE(read);
    CHECK(read->engine == "Gaia");
    CHECK(read->settings.openings == Openings::Repertoire);
    CHECK(read->settings.handicap == Handicap::Knight);
    CHECK(read->strength == k.strength);
    CHECK(read->openings_tag == k.openings_tag);
    CHECK(read->openings_text == k.openings_text);
    CHECK_FALSE(read->white);
    CHECK(read->headers == k.headers);
    CHECK(read->moves == k.moves);
    CHECK(read->times == k.times);
    CHECK(read->clocks_before == k.clocks_before);
    CHECK(read->clock_ms == k.clock_ms);
    CHECK(read->clocks[1].base_s == 120);
    CHECK(read->takebacks == 2);
    CHECK(read->hints_used == 1);
    CHECK(read->aids);
    CHECK(read->last_book == 2);
    CHECK(read->book_out);
    const auto game = keptReplay(*read);
    REQUIRE(game);
    REQUIRE(game->moves.size() == 3);
    CHECK(game->moves[2].san == "Nf3");
    CHECK(game->moves[1].mover == chess::Color::BLACK);
    CHECK(game->moves[1].time_ms == 5000);
    CHECK(game->header("Black") == "Tester");
    // A move that cannot be played: nothing to resume.
    auto bad  = *read;
    bad.moves = {"e2e4", "e2e4"};
    CHECK_FALSE(keptReplay(bad));
    removeKeptGame(dir);
    CHECK_FALSE(readKeptGame(dir));
    std::ofstream(dir / "current.json") << "{\"version\": 1}";
    CHECK_FALSE(readKeptGame(dir));
    fs::remove_all(dir);
}

TEST_CASE("play: the Play page's choice kept") {
    const auto dir  = freshDir("fcg-play-settings");
    const auto none = readPlaySettings(dir);
    CHECK(none.engine.empty());
    CHECK(none.takebacks);
    CHECK_FALSE(none.hints);
    PlaySettings s;
    s.engine       = "Gaia 4.4.3";
    s.elo          = 1800;
    s.openings     = Openings::Book;
    s.book         = "/books/Human.bin";
    s.vary         = false;
    s.colour       = 2;
    s.clock        = {300, 3};
    s.same_clock   = false;
    s.engine_clock = {60, 0};
    s.from_board   = true;
    s.handicap     = Handicap::Queen;
    s.hints        = true;
    s.games_folder = "/profile/databases/Mine.fcgdb";
    REQUIRE(writePlaySettings(dir, s));
    const auto read = readPlaySettings(dir);
    CHECK(read.engine == s.engine);
    CHECK(read.elo == 1800);
    CHECK(read.openings == Openings::Book);
    CHECK(read.book == s.book);
    CHECK_FALSE(read.vary);
    CHECK(read.colour == 2);
    CHECK(read.clock.base_s == 300);
    CHECK_FALSE(read.same_clock);
    CHECK(read.engine_clock.base_s == 60);
    CHECK(read.from_board);
    CHECK(read.handicap == Handicap::Queen);
    CHECK(read.hints);
    CHECK(read.games_folder == s.games_folder);
    std::ofstream(dir / "play.json") << "[1, 2";
    CHECK(readPlaySettings(dir).engine.empty());
    fs::remove_all(dir);
}
