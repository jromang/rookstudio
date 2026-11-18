#include <doctest/doctest.hpp>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <chess.hpp>
#include <study/import.hpp>
#include <study/scan_board.hpp>
#include <study/store.hpp>
#include <study/tree.hpp>

using namespace fcg::study;
namespace fs = std::filesystem;

namespace {

fs::path data(const std::string& name) {
    const char* env = std::getenv("FCG_SOURCE_DIR");
    return fs::path(env ? env : FCG_SOURCE_DIR) / "study" / "tests" / "data" / name;
}

struct Spec {
    std::string moves;
    char result   = '*';
    int white_elo = 0;
    int black_elo = 0;
    std::string date{"2020.01.01"};
    std::string fen;
    bool chess960 = false;
};

ImportedGame make(const Spec& s, std::size_t n) {
    const std::string result = s.result == '1' ? "1-0" : s.result == '0' ? "0-1" : s.result == '=' ? "1/2-1/2" : "*";
    std::string text         = "[Event \"Tree\"]\n[Round \"" + std::to_string(n) + "\"]\n[Date \"" + s.date + "\"]\n";
    if (s.white_elo) text += "[WhiteElo \"" + std::to_string(s.white_elo) + "\"]\n";
    if (s.black_elo) text += "[BlackElo \"" + std::to_string(s.black_elo) + "\"]\n";
    text += "[Result \"" + result + "\"]\n";
    if (s.chess960) text += "[Variant \"Chess960\"]\n";
    if (!s.fen.empty()) text += "[SetUp \"1\"]\n[FEN \"" + s.fen + "\"]\n";
    text += "\n" + s.moves + " " + result + "\n";
    ImportedGame g;
    REQUIRE(readImportedGame(text, g));
    REQUIRE_FALSE(g.damaged);
    return g;
}

std::vector<ImportedGame> make(const std::vector<Spec>& specs) {
    std::vector<ImportedGame> games;
    for (std::size_t i = 0; i < specs.size(); ++i) games.push_back(make(specs[i], i));
    return games;
}

std::shared_ptr<Database> database(const std::string& name, std::vector<ImportedGame> games) {
    const auto dir = fs::temp_directory_path() / name;
    fs::remove_all(dir);
    auto db = Database::create(dir);
    if (!games.empty()) db->append(std::move(games), true);
    return db;
}

TreeBuilt build(const Database& db, TreeOptions options = {}) {
    TreeProgress progress;
    std::atomic<bool> cancel{false};
    auto built = buildTree(db, *db.snapshot(), options, progress, cancel);
    REQUIRE_MESSAGE(built.error.empty(), built.error);
    REQUIRE_FALSE(built.cancelled);
    CHECK(progress.done == progress.total);
    return built;
}

// Every game replayed: each position of its first plies, where it reaches it first, with the move it plays from
// there. What the tree must answer.
struct Expected {
    std::map<int, TreeStats> moves;
    std::vector<TreeGame> games;
};

std::map<uint64_t, Expected> recount(const Snapshot& snap, uint32_t plies) {
    std::map<uint64_t, Expected> out;
    for (GameId id = 0; id < snap.games(); ++id) {
        const auto m        = snap.gameMoves(id);
        const bool chess960 = (m.flags & GAME_CHESS960) != 0;
        ScanBoard b;
        if (m.flags & GAME_FEN) {
            if (!b.setFen(snap.game(id).start_fen, chess960)) continue;
        } else {
            REQUIRE(b.setFen(chess::constants::STARTPOS, chess960));
        }
        const auto numbers  = snap.numbers(id);
        const uint32_t last = std::min(m.plies, plies);
        std::set<uint64_t> seen;
        std::size_t at = 0;
        for (uint32_t d = 0;; ++d) {
            const bool wide = b.codeCount() > 256;
            int code        = TreeMove::END;
            if (d < m.plies) code = wide ? (m.codes[at] << 8) | m.codes[at + 1] : m.codes[at];
            if (seen.insert(b.key()).second) {
                auto& e = out[b.key()];
                e.moves[code].add(numbers);
                e.games.push_back({id, d, code});
            }
            if (d == last || wide) break;
            b.play(b.decode(code));
            ++at;
        }
    }
    return out;
}

TreeAnswer answerOf(const std::map<int, TreeStats>& by) {
    TreeAnswer a;
    for (const auto& [code, stats] : by) {
        a.total.add(stats);
        a.moves.push_back({code, stats});
    }
    std::sort(a.moves.begin(), a.moves.end(), [](const TreeMove& x, const TreeMove& y) {
        if ((x.code == TreeMove::END) != (y.code == TreeMove::END)) return y.code == TreeMove::END;
        if (x.stats.games != y.stats.games) return x.stats.games > y.stats.games;
        return x.code < y.code;
    });
    return a;
}

// The tree against a recount: every position two games or more reach, with its moves and games, and no other.
// Returns the positions kept.
uint64_t checkTree(const Database& db, uint32_t plies = TREE_PLIES) {
    const auto snap = db.snapshot();
    const auto tree = Tree::open(db.folder());
    REQUIRE(tree);
    CHECK(tree->games() == snap->games());
    CHECK(tree->plies() == plies);
    const auto expected = recount(*snap, plies);
    uint64_t shared = 0, wrong = 0;
    std::string first;
    for (const auto& [key, e] : expected) {
        const auto got = tree->at(*snap, key);
        if (e.games.size() < 2) {
            if (got) {
                ++wrong;
                if (first.empty()) first = "a position of a single game is kept";
            }
            continue;
        }
        ++shared;
        if (!got || *got != answerOf(e.moves)) {
            ++wrong;
            if (first.empty()) first = got ? "moves differ" : "a shared position is missing";
            continue;
        }
        auto list        = tree->gamesAt(*snap, key);
        auto want        = e.games;
        const auto by_id = [](const TreeGame& a, const TreeGame& b) { return a.id < b.id; };
        std::sort(list.begin(), list.end(), by_id);
        std::sort(want.begin(), want.end(), by_id);
        if (list != want) {
            ++wrong;
            if (first.empty()) first = "games differ";
        }
    }
    CHECK_MESSAGE(wrong == 0, first);
    CHECK(tree->positions() == shared);
    for (uint64_t i = 1; i < tree->positions(); ++i) REQUIRE(tree->keyAt(i - 1) < tree->keyAt(i));
    return shared;
}

uint64_t keyAfter(const std::string& uci_moves, const std::string& fen = std::string(chess::constants::STARTPOS)) {
    chess::Board board(fen);
    std::istringstream in(uci_moves);
    for (std::string word; in >> word;) board.makeMove(chess::uci::uciToMove(board, word));
    ScanBoard b;
    REQUIRE(b.set(board));
    return b.key();
}

// Random games whose first plies choose among the first three legal moves in UCI order: games share them, cross
// their orders (transpositions) and repeat positions (a knight that goes and comes back).
std::vector<ImportedGame> randomGames(std::size_t count, uint32_t seed, int narrow) {
    std::mt19937 random(seed);
    std::vector<ImportedGame> games;
    const char results[] = {'1', '0', '=', '*'};
    for (std::size_t i = 0; i < count; ++i) {
        chess::Board board;
        std::string moves;
        const int length = static_cast<int>(random() % 90);
        for (int ply = 0; ply < length; ++ply) {
            chess::Movelist list;
            chess::movegen::legalmoves(list, board);
            if (list.empty()) break;
            std::vector<chess::Move> sorted(list.begin(), list.end());
            std::sort(sorted.begin(), sorted.end(), [](const chess::Move& a, const chess::Move& b) {
                return chess::uci::moveToUci(a) < chess::uci::moveToUci(b);
            });
            const std::size_t choices = ply < narrow ? std::min<std::size_t>(3, sorted.size()) : sorted.size();
            const auto move           = sorted[random() % choices];
            if (ply % 2 == 0) moves += std::to_string(ply / 2 + 1) + ". ";
            moves += chess::uci::moveToSan(board, move) + " ";
            board.makeMove(move);
        }
        Spec s;
        s.moves     = moves;
        s.result    = results[random() % 4];
        s.white_elo = random() % 4 ? 1800 + static_cast<int>(random() % 1000) : 0;
        s.black_elo = random() % 4 ? 1800 + static_cast<int>(random() % 1000) : 0;
        s.date      = std::to_string(1990 + random() % 37) + ".0" + std::to_string(1 + random() % 9) + ".1" +
                      std::to_string(random() % 10);
        games.push_back(make(s, i));
    }
    return games;
}

std::string fileText(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

}  // namespace

TEST_CASE("tree: transpositions, repetitions, duplicates and starts of their own, as a recount gives them") {
    const auto after_e4_e5 = "rnbqkbnr/pppp1ppp/8/4p3/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2";
    auto db = database("tree-crafted", make(std::vector<Spec>{
                                           // The same position after four plies by two move orders, each of a
                                           // single game: shared by their own moves only.
                                           {"1. d4 Nf6 2. c4 e6 3. Nc3 Bb4", '1', 2700, 2650, "2021.03.04"},
                                           {"1. c4 e6 2. d4 Nf6 3. Nc3 Bb4 4. Qc2", '=', 2500, 0, "2022.05.06"},
                                           // Back to the start: a game counts once in a position.
                                           {"1. Nf3 Nf6 2. Ng1 Ng8 3. Nf3 Nf6 4. e4", '0', 0, 2400},
                                           {"1. Nf3 Nf6 2. Ng1 Ng8 3. e4", '1'},
                                           // The same game twice, and games that share its first moves.
                                           {"1. e4 e5 2. Nf3 Nc6 3. Bb5 a6", '1', 2800, 2750, "2019.07.08"},
                                           {"1. e4 e5 2. Nf3 Nc6 3. Bb5 a6", '0', 2100, 2200, "2018.01.01"},
                                           {"1. e4 e5 2. Nf3 Nf6", '='},
                                           {"1. e4 c5 2. Nf3 d6 3. d4", '1'},
                                           {"1. e4 c5 2. Nc3", '0'},
                                           // Into a position several games reach by other moves.
                                           {"1. Nf3 e5 2. e4 Nc6", '*'},
                                           // No moves, and a start of its own that other games reach.
                                           {"", '='},
                                           {"2. Nf3 Nc6 3. Bb5", '1', 0, 0, "2023.02.02", after_e4_e5},
                                           {"2. Bc4", '0', 0, 0, "2023.02.03", after_e4_e5},
                                       }));
    build(*db);
    checkTree(*db);

    const auto snap = db->snapshot();
    const auto tree = Tree::open(db->folder());
    REQUIRE(tree);
    // The start: every game of the standard start, those that come back to it counted once.
    const auto start = tree->at(*snap, keyAfter(""));
    REQUIRE(start);
    CHECK(start->total.games == 11);
    // After 1.e4 e5: three games by those moves, and two that start there.
    const auto open = tree->at(*snap, keyAfter("e2e4 e7e5"));
    REQUIRE(open);
    CHECK(open->total.games == 5);
    // After 2.Nf3: three games by those moves, one by 1.Nf3 e5 2.e4, one that starts after 1...e5. After 1.e4
    // (Black to move): five games by 1.e4, and one that comes back to the start before playing it.
    CHECK(tree->at(*snap, keyAfter("e2e4 e7e5 g1f3"))->total.games == 5);
    CHECK(tree->at(*snap, keyAfter("e2e4"))->total.games == 6);
    // After 1.d4 Nf6 2.c4 e6, which two games reach by their own moves.
    const auto crossed = tree->at(*snap, keyAfter("d2d4 g8f6 c2c4 e7e6"));
    REQUIRE(crossed);
    CHECK(crossed->total.games == 2);
    CHECK(crossed->total.white == 1);
    CHECK(crossed->total.draws == 1);
    CHECK(crossed->total.white_rated == 2);
    CHECK(crossed->total.white_elo == 5200);
    CHECK(crossed->total.black_rated == 1);
    CHECK(crossed->total.last == packDate("2022.05.06"));
    REQUIRE(crossed->moves.size() == 1);
    // 1.d4 alone: a single game, not kept.
    CHECK_FALSE(tree->at(*snap, keyAfter("d2d4")));
    // The games that end in a position are told apart from the moves played there.
    const auto ruy = tree->at(*snap, keyAfter("e2e4 e7e5 g1f3 b8c6 f1b5 a7a6"));
    REQUIRE(ruy);
    REQUIRE(ruy->moves.size() == 1);
    CHECK(ruy->moves[0].code == TreeMove::END);
    CHECK(ruy->moves[0].stats.games == 2);
}

TEST_CASE("tree: random games with many transpositions, as a recount gives them, in one pass or many") {
    auto db           = database("tree-random", randomGames(3000, 11, 14));
    const auto single = build(*db);
    CHECK(single.passes == 1);
    const auto shared = checkTree(*db);
    CHECK(shared > 1000);
    const auto dir     = db->folder() / Tree::open(db->folder())->folderName();
    const auto records = fileText(dir / "records.bin");
    const auto keys    = fileText(dir / "keys.bin");
    const auto order   = fileText(dir / "order.bin");

    // Little memory: the positions of single games' moves in several passes, one thread: the same files.
    TreeOptions small;
    small.memory      = std::size_t{512} << 10;
    small.threads     = 1;
    const auto passes = build(*db, small);
    CHECK(passes.passes >= 4);
    const auto again = db->folder() / Tree::open(db->folder())->folderName();
    CHECK(again != dir);
    CHECK_FALSE(fs::exists(dir));  // the tree before, removed once the new one is named
    CHECK(fileText(again / "records.bin") == records);
    CHECK(fileText(again / "keys.bin") == keys);
    CHECK(fileText(again / "order.bin") == order);
    checkTree(*db);
}

TEST_CASE("tree: fewer plies followed") {
    auto db = database("tree-plies", randomGames(800, 5, 10));
    TreeOptions options;
    options.plies = 8;
    build(*db, options);
    checkTree(*db, 8);
    // No game is followed past its eighth ply.
    const auto snap = db->snapshot();
    const auto tree = Tree::open(db->folder());
    for (uint64_t i = 0; i < tree->positions(); ++i)
        for (const auto& g : tree->gamesAt(*snap, tree->keyAt(i))) REQUIRE(g.ply <= 8);
}

TEST_CASE("tree: real games, and the database read again") {
    std::vector<ImportedGame> games;
    {
        const auto text = fileText(data("games.pgn"));
        for (const auto piece : splitGames(text)) {
            ImportedGame g;
            if (readImportedGame(piece, g)) games.push_back(std::move(g));
        }
    }
    REQUIRE(games.size() >= 100);
    auto db = database("tree-real", games);
    build(*db);
    const auto dir = db->folder();
    db.reset();
    auto reopened = Database::open(dir);
    checkTree(*reopened);
}

TEST_CASE("tree: none at first, a cancel keeps the tree before, leftovers go") {
    auto db = database("tree-cancel", randomGames(300, 3, 8));
    CHECK_FALSE(Tree::open(db->folder()));
    build(*db);
    const auto before = Tree::open(db->folder())->folderName();
    // What a build killed in its middle leaves, and a tree no longer named.
    fs::create_directories(db->folder() / "tree-000077.tmp");
    fs::create_directories(db->folder() / "tree-000050");
    TreeProgress progress;
    std::atomic<bool> cancel{true};
    const auto cancelled = buildTree(*db, *db->snapshot(), {}, progress, cancel);
    CHECK(cancelled.cancelled);
    CHECK(Tree::open(db->folder())->folderName() == before);
    CHECK_FALSE(fs::exists(db->folder() / "tree-000077.tmp"));
    CHECK_FALSE(fs::exists(db->folder() / "tree-000050"));
    checkTree(*db);
    // A later build numbers its tree after those there.
    build(*db);
    CHECK(Tree::open(db->folder())->folderName() == "tree-000002");
    checkTree(*db);
}

TEST_CASE("tree: an empty database, and a single game") {
    auto empty = database("tree-empty", {});
    build(*empty);
    CHECK(checkTree(*empty) == 0);
    auto one = database("tree-one", make(std::vector<Spec>{{"1. e4 e5 2. Nf3", '1'}}));
    build(*one);
    CHECK(checkTree(*one) == 0);
}

namespace {

// Lines of moves made as randomGames makes games: from the start, the first plies among the first three moves in UCI
// order, then any move.
std::vector<std::vector<std::string>> randomLines(std::size_t count, uint32_t seed, int narrow, int longest) {
    std::mt19937 random(seed);
    std::vector<std::vector<std::string>> lines;
    for (std::size_t i = 0; i < count; ++i) {
        chess::Board board;
        std::vector<std::string> line;
        const int length = static_cast<int>(random() % static_cast<uint32_t>(longest));
        for (int ply = 0; ply < length; ++ply) {
            chess::Movelist list;
            chess::movegen::legalmoves(list, board);
            if (list.empty()) break;
            std::vector<chess::Move> sorted(list.begin(), list.end());
            std::sort(sorted.begin(), sorted.end(), [](const chess::Move& a, const chess::Move& b) {
                return chess::uci::moveToUci(a) < chess::uci::moveToUci(b);
            });
            const std::size_t choices = ply < narrow ? std::min<std::size_t>(3, sorted.size()) : sorted.size();
            const auto move           = sorted[random() % choices];
            line.push_back(chess::uci::moveToSan(board, move));
            board.makeMove(move);
        }
        lines.push_back(std::move(line));
    }
    return lines;
}

std::string movetext(const std::vector<std::string>& line) {
    std::string text;
    for (std::size_t i = 0; i < line.size(); ++i) {
        if (i % 2 == 0) text += std::to_string(i / 2 + 1) + ". ";
        text += line[i] + " ";
    }
    return text;
}

}  // namespace

TEST_CASE("tree: where a line leaves the database, as a recount tells it") {
    const auto played = randomLines(1500, 21, 12, 70);
    std::vector<ImportedGame> games;
    for (std::size_t i = 0; i < played.size(); ++i) games.push_back(make(Spec{movetext(played[i]), '1'}, i));
    auto db = database("tree-novelty", games);
    build(*db);
    const auto snap    = db->snapshot();
    const auto tree    = Tree::open(db->folder());
    const auto counted = recount(*snap, TREE_PLIES);
    ScanBoard start;
    REQUIRE(start.setFen(chess::constants::STARTPOS, false));

    // The games' own lines, and lines made the same way that part from them somewhere.
    auto lines = played;
    for (auto& line : randomLines(1500, 99, 12, 70)) lines.push_back(std::move(line));
    int left = 0, unseen = 0, wrong = 0;
    for (const auto& line : lines) {
        std::vector<int> codes;
        std::vector<uint64_t> keys{start.key()};
        ScanBoard b = start;
        for (const auto& san : line) {
            const auto move = b.parseSan(san);
            REQUIRE(move);
            codes.push_back(b.encode(*move));
            b.play(*move);
            keys.push_back(b.key());
        }
        // The recount's answer: the first position, within the tree's plies, that no game reaches.
        std::optional<uint32_t> expected;
        for (uint32_t i = 1; i < keys.size() && i <= TREE_PLIES; ++i) {
            if (!counted.count(keys[i])) {
                expected = i;
                break;
            }
        }
        const auto got = tree->novelty(*snap, start, codes);
        if (got && expected && *got == *expected) {
            ++left;
            continue;
        }
        if (!got && !expected) continue;
        // Earlier than the recount: only where a single game reaches the position by other moves than the line's.
        bool allowed = false;
        if (got && (!expected || *got < *expected)) {
            const auto& reach = counted.at(keys[*got]).games;
            if (reach.size() == 1) {
                allowed           = true;
                const auto before = counted.find(keys[*got - 1]);
                if (before != counted.end())
                    for (const auto& g : before->second.games)
                        if (g.id == reach[0].id && g.code == codes[*got - 1]) allowed = false;
            }
        }
        if (allowed)
            ++unseen;
        else
            ++wrong;
    }
    CHECK(wrong == 0);
    CHECK(left > 300);
    MESSAGE("lines leaving the database: ", left, "; earlier than a recount, by a transposition: ", unseen);
}
