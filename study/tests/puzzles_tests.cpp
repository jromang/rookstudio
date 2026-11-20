// Lichess's puzzles kept offline (#80): the database's lines read, the store made from them keeping every puzzle as it
// was (its position, its moves, castlings, en passant, promotions and positions of many queens among them), a rating's
// and a theme's puzzles, the random picks, a reading cut short and gone on with, the lines left out. The puzzles are
// made by the tests: random games of our own, a position of each and moves played from it.
#include <doctest/doctest.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <random>
#include <set>
#include <string>
#include <vector>

#include <chess.hpp>

#include <study/puzzles.hpp>

using namespace fcg::study;
namespace fs = std::filesystem;

namespace {

const std::vector<std::string> THEMES = {"fork",   "pin",      "mateIn1",   "mateIn2",       "endgame",   "middlegame",
                                         "short",  "long",     "crushing",  "advantage",     "sacrifice", "promotion",
                                         "skewer", "castling", "enPassant", "underPromotion"};
const std::vector<std::string> OPENINGS = {"Sicilian_Defense", "Sicilian_Defense_Najdorf_Variation", "French_Defense",
                                           "French_Defense_Tarrasch_Variation", "Italian_Game"};

std::string text62(std::mt19937& rng, std::size_t length) {
    static constexpr char DIGITS[] = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    std::string out;
    for (std::size_t i = 0; i < length; ++i) out += DIGITS[rng() % 62];
    return out;
}

// The line of the database for a puzzle, as Lichess writes one.
std::string lineOf(const Puzzle& p, int plays = 1000) {
    std::string moves, themes, openings;
    for (const auto& m : p.moves) moves += (moves.empty() ? "" : " ") + m;
    for (const auto& t : p.themes) themes += (themes.empty() ? "" : " ") + t;
    for (const auto& o : p.openings) openings += (openings.empty() ? "" : " ") + o;
    return p.id + "," + p.fen + "," + moves + "," + std::to_string(p.rating) + "," + std::to_string(p.deviation) + "," +
           std::to_string(p.popularity) + "," + std::to_string(plays) + "," + themes + "," + p.gameUrl() + "," +
           openings + ",";
}

// Puzzles of our own: a position of a random game, then two to eight moves played from it, each legal.
std::vector<Puzzle> randomPuzzles(std::size_t n, uint32_t seed) {
    std::mt19937 rng(seed);
    std::vector<Puzzle> out;
    std::set<std::string> ids;
    while (out.size() < n) {
        chess::Board board;
        const int plies = 4 + static_cast<int>(rng() % 70);
        bool over       = false;
        for (int ply = 0; ply < plies; ++ply) {
            chess::Movelist moves;
            chess::movegen::legalmoves(moves, board);
            if (moves.empty()) {
                over = true;
                break;
            }
            board.makeMove(moves[static_cast<int>(rng() % static_cast<uint32_t>(moves.size()))]);
        }
        if (over) continue;
        Puzzle p;
        p.fen            = board.getFen();
        const int length = 2 * (1 + static_cast<int>(rng() % 4));
        for (int i = 0; i < length; ++i) {
            chess::Movelist moves;
            chess::movegen::legalmoves(moves, board);
            if (moves.empty()) break;
            const auto m = moves[static_cast<int>(rng() % static_cast<uint32_t>(moves.size()))];
            p.moves.push_back(chess::uci::moveToUci(m));
            board.makeMove(m);
        }
        if (p.moves.size() < 2) continue;
        do {
            p.id = text62(rng, 5);
        } while (!ids.insert(p.id).second);
        p.game       = text62(rng, 8);
        p.rating     = 400 + static_cast<int>(rng() % 2900);
        p.deviation  = 60 + static_cast<int>(rng() % 300);
        p.popularity = static_cast<int>(rng() % 201) - 100;
        for (const auto& t : THEMES)
            if (rng() % 4 == 0) p.themes.push_back(t);
        if (rng() % 3 == 0) {
            const auto o = rng() % 3;
            p.openings.push_back(OPENINGS[o == 2 ? 4 : o * 2]);
            if (o < 2 && rng() % 2) p.openings.push_back(OPENINGS[o * 2 + 1]);
        }
        out.push_back(std::move(p));
    }
    return out;
}

// Puzzles of the moves that are not like the others: the two castlings, en passant, promotions and an
// underpromotion, a side to move with nine queens (codes of two bytes).
std::vector<Puzzle> specialPuzzles() {
    const auto make = [](std::string id, std::string fen, std::vector<std::string> moves) {
        chess::Board board(fen);
        for (const auto& uci : moves) {
            const auto m = chess::uci::uciToMove(board, uci);
            chess::Movelist legal;
            chess::movegen::legalmoves(legal, board);
            REQUIRE_MESSAGE(std::find(legal.begin(), legal.end(), m) != legal.end(), uci, " in ", fen);
            board.makeMove(m);
        }
        Puzzle p;
        p.id         = std::move(id);
        p.fen        = std::move(fen);
        p.moves      = std::move(moves);
        p.rating     = 1500;
        p.deviation  = 80;
        p.popularity = 90;
        p.game       = "Abc12345";
        p.themes     = {"castling"};
        return p;
    };
    std::vector<Puzzle> out;
    out.push_back(
        make("cAst1", "r3k2r/pppq1ppp/8/8/8/8/PPPQ1PPP/R3K2R w KQkq - 4 20", {"e1g1", "e8c8", "d2d7", "d8d7"}));
    out.push_back(make("cAst2", "r3k2r/8/8/8/8/8/8/R3K2R b Kq - 0 31", {"e8c8", "e1g1"}));
    out.push_back(make("enPas", "4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 30", {"e5d6", "e8d7", "e1e2", "d7d6"}));
    out.push_back(make("prom1", "8/P5k1/8/8/8/8/6p1/K7 w - - 0 50", {"a7a8q", "g2g1n", "a8h1", "g1e2"}));
    out.push_back(make("queen", "k6K/6PP/8/8/8/8/qq6/qqqqqqq1 b - - 0 70", {"a2a3", "h8g8", "a3a4", "g8h8"}));
    return out;
}

// The store made from these lines, in a folder of the test's own.
fs::path made(const fs::path& folder, const std::vector<std::string>& lines, const std::string& date = "2026-11-01") {
    fs::remove_all(folder);
    PuzzleBuilder builder(folder / "build");
    builder.line("PuzzleId,FEN,Moves,Rating,RatingDeviation,Popularity,NbPlays,Themes,GameUrl,OpeningTags,DailyDate");
    for (const auto& l : lines) builder.line(l);
    const auto store = nextPuzzleStore(folder);
    REQUIRE(builder.finish(store, date) == "");
    return store;
}

std::map<std::string, uint32_t> numbersOf(const PuzzleStore& store) {
    std::map<std::string, uint32_t> out;
    for (uint32_t k = 0; k < store.size(); ++k) out[store.idOf(k)] = k;
    return out;
}

std::string bytesOf(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

}  // namespace

TEST_CASE("puzzles: a line of the database, its game's id and address") {
    PuzzleLine line;
    CHECK_FALSE(readPuzzleLine(
        "PuzzleId,FEN,Moves,Rating,RatingDeviation,Popularity,NbPlays,Themes,GameUrl,OpeningTags,DailyDate", line));
    CHECK_FALSE(readPuzzleLine("", line));
    REQUIRE(
        readPuzzleLine("0abCd,6k1/5ppp/8/8/8/8/5PPP/3R2K1 w - - 0 31,g1f1 g8f8 d1d8,1234,77,-3,99,"
                       "mate mateIn1 backRankMate,https://lichess.org/game/Ab3dE6gH/black#61,,\r",
                       line));
    CHECK(line.id == "0abCd");
    CHECK(line.moves == "g1f1 g8f8 d1d8");
    CHECK(line.rating == 1234);
    CHECK(line.deviation == 77);
    CHECK(line.popularity == -3);
    CHECK(line.themes == "mate mateIn1 backRankMate");
    CHECK(line.openings.empty());
    CHECK(puzzleGameId(line.url) == "Ab3dE6gH");
    CHECK(puzzleGameId("https://lichess.org/Ab3dE6gH#12") == "Ab3dE6gH");
    CHECK(puzzleGameId("https://lichess.org/game/short#12").empty());
    Puzzle p;
    p.fen  = "6k1/5ppp/8/8/8/8/5PPP/3R2K1 b - - 0 31";
    p.game = "Ab3dE6gH";
    CHECK(p.gameUrl() == "https://lichess.org/game/Ab3dE6gH/black#62");
    p.fen = "6k1/5ppp/8/8/8/8/5PPP/3R2K1 w - - 2 27";
    CHECK(p.gameUrl() == "https://lichess.org/game/Ab3dE6gH#53");
}

TEST_CASE("puzzles: the store keeps every puzzle as it was read, by rating") {
    auto puzzles = randomPuzzles(3000, 80);
    for (auto& p : specialPuzzles()) puzzles.push_back(std::move(p));
    std::vector<std::string> lines;
    for (const auto& p : puzzles) lines.push_back(lineOf(p));
    const auto folder = fs::temp_directory_path() / "puzzles-kept";
    const auto file   = made(folder, lines);
    const PuzzleStore store(file);
    REQUIRE(store.size() == puzzles.size());
    CHECK(store.lines() == puzzles.size());
    CHECK(store.skipped() == 0);
    CHECK(store.date() == "2026-11-01");
    CHECK(store.bytes() == fs::file_size(file));
    // Each as it was: its deviation kept up to 255, its themes in the order the database first named them.
    const auto numbers = numbersOf(store);
    std::map<std::string, uint32_t> theme_counts;
    for (const auto& p : puzzles) {
        REQUIRE(numbers.count(p.id) == 1);
        auto kept          = store.puzzle(numbers.at(p.id));
        auto expected      = p;
        expected.deviation = std::min(p.deviation, 255);
        std::sort(kept.themes.begin(), kept.themes.end());
        std::sort(expected.themes.begin(), expected.themes.end());
        CHECK(kept.id == expected.id);
        CHECK(kept.fen == expected.fen);
        CHECK(kept.moves == expected.moves);
        CHECK(kept.rating == expected.rating);
        CHECK(kept.deviation == expected.deviation);
        CHECK(kept.popularity == expected.popularity);
        CHECK(kept.themes == expected.themes);
        CHECK(kept.openings == expected.openings);
        CHECK(kept.game == expected.game);
        CHECK(kept.gameUrl() == p.gameUrl());
        for (const auto& t : p.themes) ++theme_counts[t];
    }
    // Sorted by rating, then id.
    for (uint32_t k = 1; k < store.size(); ++k) {
        REQUIRE(store.ratingOf(k - 1) <= store.ratingOf(k));
        if (store.ratingOf(k - 1) == store.ratingOf(k)) CHECK(store.idOf(k - 1) != store.idOf(k));
    }
    for (const auto& t : store.themes()) CHECK(t.puzzles == theme_counts[t.name]);
    CHECK(store.openings().size() == OPENINGS.size());
}

TEST_CASE("puzzles: a rating's puzzles, a theme's and an opening's, picked at random") {
    const auto puzzles = randomPuzzles(4000, 81);
    std::vector<std::string> lines;
    for (const auto& p : puzzles) lines.push_back(lineOf(p));
    const PuzzleStore store(made(fs::temp_directory_path() / "puzzles-picked", lines));
    std::map<std::string, const Puzzle*> by_id;
    for (const auto& p : puzzles) by_id[p.id] = &p;
    const auto has = [](const std::vector<std::string>& tags, const std::string& tag) {
        return std::find(tags.begin(), tags.end(), tag) != tags.end();
    };

    // A rating window: the puzzles between its bounds, and only them.
    const auto [first, last] = store.ratingRange(1400, 1600);
    std::size_t expected     = 0;
    for (const auto& p : puzzles) expected += p.rating >= 1400 && p.rating <= 1600;
    CHECK(last - first == expected);
    for (uint32_t k = first; k < last; ++k) CHECK((store.ratingOf(k) >= 1400 && store.ratingOf(k) <= 1600));
    CHECK(store.ratingRange(5000, 6000).first == store.ratingRange(5000, 6000).second);

    // A theme's list, and an opening's, within the window.
    const auto fork = store.themeList("fork");
    REQUIRE(fork);
    const auto forks           = store.listed(*fork, first, last);
    std::size_t expected_forks = 0;
    for (const auto& p : puzzles) expected_forks += p.rating >= 1400 && p.rating <= 1600 && has(p.themes, "fork");
    CHECK(forks.size() == expected_forks);
    for (const auto k : forks) {
        CHECK(has(by_id.at(store.idOf(k))->themes, "fork"));
        CHECK(store.inList(*fork, k));
    }
    const auto najdorf = store.openingList("Sicilian_Defense_Najdorf_Variation");
    REQUIRE(najdorf);
    for (const auto k : store.listed(*najdorf, 0, store.size()))
        CHECK(has(by_id.at(store.idOf(k))->openings, "Sicilian_Defense_Najdorf_Variation"));
    CHECK_FALSE(store.themeList("noSuchTheme"));

    // Twenty picks near 1500 of the fork: of the theme, near the rating, never twice, never disliked, never one seen.
    std::set<std::string> seen;
    for (uint32_t k = first; k < last && seen.size() < 30; ++k) seen.insert(store.idOf(k));
    const auto picked = store.pick(*fork, 1500, 20, [&](const std::string& id) { return seen.count(id) > 0; }, 7);
    CHECK(picked.size() == 20);
    std::set<uint32_t> once(picked.begin(), picked.end());
    CHECK(once.size() == picked.size());
    for (const auto k : picked) {
        const auto& p = *by_id.at(store.idOf(k));
        CHECK(has(p.themes, "fork"));
        CHECK(p.popularity >= 0);
        CHECK(seen.count(p.id) == 0);
        CHECK(std::abs(p.rating - 1500) <= 400);  // 4000 puzzles over 2900 points: the first windows hold 20
    }
    // The same seed, the same picks; every puzzle seen, none.
    CHECK(store.pick(*fork, 1500, 20, [&](const std::string& id) { return seen.count(id) > 0; }, 7) == picked);
    CHECK(store.pick(-1, 1500, 10, [](const std::string&) { return true; }, 7).empty());
    // Every puzzle, near a rating few have: the window widens.
    const auto far = store.pick(-1, 3900, 5, {}, 3);
    CHECK(far.size() == 5);
}

TEST_CASE("puzzles: a reading cut short goes on from its mark, and gives the same store") {
    const auto puzzles = randomPuzzles(1500, 82);
    std::vector<std::string> lines;
    for (const auto& p : puzzles) lines.push_back(lineOf(p));
    const auto whole = made(fs::temp_directory_path() / "puzzles-whole", lines);

    const auto folder = fs::temp_directory_path() / "puzzles-cut";
    fs::remove_all(folder);
    nlohmann::json state;
    {
        PuzzleBuilder builder(folder / "build");
        for (std::size_t i = 0; i < 700; ++i) builder.line(lines[i]);
        state = builder.mark();
        builder.commit();
        // Lines read after the mark, then the program closed: written, but not counted by the state kept.
        for (std::size_t i = 700; i < 900; ++i) builder.line(lines[i]);
        builder.commit();
    }
    PuzzleBuilder builder(folder / "build");
    builder.restore(state);
    CHECK(builder.error().empty());
    CHECK(builder.kept() == 700);
    for (std::size_t i = 700; i < lines.size(); ++i) builder.line(lines[i]);
    const auto store = nextPuzzleStore(folder);
    REQUIRE(builder.finish(store, "2026-11-01") == "");
    CHECK(bytesOf(store) == bytesOf(whole));
    // The build's files gone; a state of files that no longer exist cannot be gone on with.
    PuzzleBuilder again(folder / "build");
    again.restore(state);
    CHECK_FALSE(again.error().empty());
    again.startAgain();
    CHECK(again.error().empty());
}

TEST_CASE("puzzles: lines that do not read are counted and left out") {
    auto puzzles = randomPuzzles(20, 83);
    std::vector<std::string> lines;
    for (const auto& p : puzzles) lines.push_back(lineOf(p));
    auto bad         = puzzles[0];
    bad.id           = "badMv";
    bad.moves.back() = "a1a1";  // not a move
    lines.push_back(lineOf(bad));
    bad.id  = "badFn";
    bad.fen = "not a fen";
    lines.push_back(lineOf(bad));
    bad    = puzzles[1];
    bad.id = "toolongid";
    lines.push_back(lineOf(bad));
    bad       = puzzles[2];
    bad.id    = "one1m";
    bad.moves = {bad.moves.front()};  // the opponent's move alone: nothing to solve
    lines.push_back(lineOf(bad));
    lines.push_back("a,line,of,something,else");
    const PuzzleStore store(made(fs::temp_directory_path() / "puzzles-bad", lines, ""));
    CHECK(store.size() == 20);
    CHECK(store.lines() == 24);  // the line of something else is no puzzle at all
    CHECK(store.skipped() == 4);
    CHECK(store.date().empty());
    const auto numbers = numbersOf(store);
    CHECK(numbers.count("badMv") == 0);
    CHECK(numbers.count("one1m") == 0);
}

TEST_CASE("puzzles: the folder's stores, the newest answering, the older removed") {
    const auto folder = fs::temp_directory_path() / "puzzles-folder";
    fs::remove_all(folder);
    fs::create_directories(folder);
    CHECK(newestPuzzleStore(folder).empty());
    CHECK(nextPuzzleStore(folder).filename() == "puzzles-1.bin");
    std::ofstream(folder / "puzzles-1.bin") << "old";
    std::ofstream(folder / "puzzles-3.bin") << "newer";
    std::ofstream(folder / "puzzles-4.bin.tmp") << "cut short";
    CHECK(newestPuzzleStore(folder).filename() == "puzzles-3.bin");
    CHECK(nextPuzzleStore(folder).filename() == "puzzles-5.bin");
    tidyPuzzleFolder(folder);
    CHECK_FALSE(fs::exists(folder / "puzzles-1.bin"));
    CHECK_FALSE(fs::exists(folder / "puzzles-4.bin.tmp"));
    CHECK(fs::exists(folder / "puzzles-3.bin"));
    CHECK_THROWS_AS(PuzzleStore(folder / "puzzles-3.bin"), std::runtime_error);
}
