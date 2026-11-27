#include <doctest/doctest.hpp>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
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

#include "test_games.hpp"

using namespace fcg::study;
using namespace fcg::test;
namespace fs = std::filesystem;

namespace {

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
    const auto records = readFile(dir / "records.bin");
    const auto keys    = readFile(dir / "keys.bin");
    const auto order   = readFile(dir / "order.bin");

    // Little memory: the positions of single games' moves in several passes, one thread: the same files.
    TreeOptions small;
    small.memory      = std::size_t{512} << 10;
    small.threads     = 1;
    const auto passes = build(*db, small);
    CHECK(passes.passes >= 4);
    const auto again = db->folder() / Tree::open(db->folder())->folderName();
    CHECK(again != dir);
    CHECK_FALSE(fs::exists(dir));  // the tree before, removed once the new one is named
    CHECK(readFile(again / "records.bin") == records);
    CHECK(readFile(again / "keys.bin") == keys);
    CHECK(readFile(again / "order.bin") == order);
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
        const auto text = readFile(data("games.pgn"));
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

TEST_CASE("tree: the games that played a move from a position, as a recount gives them") {
    auto db = database("tree-with", randomGames(2000, 31, 12));
    build(*db);
    const auto snap     = db->snapshot();
    const auto tree     = Tree::open(db->folder());
    const auto expected = recount(*snap, TREE_PLIES);
    uint64_t checked = 0, wrong = 0;
    for (const auto& [key, e] : expected) {
        if (e.games.size() < 2) continue;
        for (const auto& [code, stats] : e.moves) {
            auto got = tree->gamesWith(*snap, key, code);
            std::vector<TreeGame> want;
            for (const auto& g : e.games)
                if (g.code == code) want.push_back(g);
            const auto by_id = [](const TreeGame& a, const TreeGame& b) { return a.id < b.id; };
            std::sort(got.begin(), got.end(), by_id);
            std::sort(want.begin(), want.end(), by_id);
            if (got != want) ++wrong;
            ++checked;
        }
        // A move nobody played there.
        if (tree->gamesWith(*snap, key, 250).size() != 0) ++wrong;
    }
    CHECK(wrong == 0);
    CHECK(checked > 1000);
}

namespace {

// A tree's files copied to a folder of their own, `file` written over with `bytes`: the tree that opens there. The tree
// before must be let go first (its files are removed, and Windows keeps a mapped file).
std::shared_ptr<const Tree> damagedCopy(const fs::path& database, const std::string& name, const std::string& file,
                                        const std::string& bytes) {
    const auto dir = fs::temp_directory_path() / "tree-damaged-copy";
    fs::remove_all(dir);
    fs::create_directories(dir);
    fs::copy_file(database / "tree.json", dir / "tree.json");
    fs::copy(database / name, dir / name, fs::copy_options::recursive);
    std::ofstream(dir / name / file, std::ios::binary | std::ios::trunc) << bytes;
    return Tree::open(dir);
}

void putU64(std::string& bytes, std::size_t at, uint64_t v) { std::memcpy(bytes.data() + at, &v, 8); }

}  // namespace

TEST_CASE("tree: a damaged tree throws a store error where it is read, never reads outside its files (#139)") {
    auto db = database("tree-damaged", randomGames(1200, 139, 8));
    build(*db);
    const auto snap = db->snapshot();
    std::string name;
    std::vector<uint64_t> keys;
    {
        const auto sound = Tree::open(db->folder());
        REQUIRE(sound);
        name = sound->folderName();
        for (uint64_t i = 0; i < sound->positions(); ++i) keys.push_back(sound->keyAt(i));
    }
    REQUIRE(keys.size() > 200);
    const auto dir = db->folder() / name;
    const std::map<std::string, std::string> sound{{"order.bin", readFile(dir / "order.bin")},
                                                   {"index.bin", readFile(dir / "index.bin")},
                                                   {"records.bin", readFile(dir / "records.bin")}};
    // Lines from the start, to look for where they leave the database: the first plies of the games.
    ScanBoard start;
    REQUIRE(start.setFen(chess::constants::STARTPOS, false));
    std::vector<std::vector<int>> lines;
    for (GameId id = 0; id < snap->games(); id += 37) {
        const auto m = snap->gameMoves(id);
        lines.emplace_back(m.codes.begin(), m.codes.begin() + std::min<std::size_t>(m.codes.size(), 20));
    }

    // Everything asked of a tree at every kept position and along the lines: an answer, or a store error saying the
    // tree is damaged, and naming its folder; never another exception, nor a read outside its files (the sanitizers').
    // How many questions it refused.
    const auto ask = [&](const Tree& tree) {
        std::size_t refused = 0;
        const auto one      = [&](auto&& question) {
            try {
                question();
            } catch (const StoreError& e) {
                CHECK(e.code() == StoreErrc::Damaged);
                CHECK(std::string(e.what()).find(name + ": a damaged tree") != std::string::npos);
                ++refused;
            }
        };
        for (const auto key : keys) {
            one([&] {
                if (const auto answer = tree.at(*snap, key))
                    for (const auto& m : answer->moves) CHECK((m.code >= TreeMove::END && m.code <= 65535));
            });
            one([&] {
                for (const auto& g : tree.gamesAt(*snap, key)) CHECK(g.id < snap->games());
            });
            one([&] { (void)tree.visitAt(*snap, key, [&](GameId id, uint32_t) { CHECK(id < snap->games()); }); });
            one([&] { (void)tree.gamesWith(*snap, key, 0); });
        }
        for (const auto& line : lines) one([&] { (void)tree.novelty(*snap, start, line); });
        return refused;
    };
    const std::size_t questions = 4 * keys.size() + lines.size();
    {
        const auto copy = damagedCopy(db->folder(), name, "records.bin", sound.at("records.bin"));
        REQUIRE(copy);
        CHECK(ask(*copy) == 0);  // the copy reads as the tree does
    }

    SUBCASE("its records' bytes overwritten, their header left: every position read is refused") {
        auto records = sound.at("records.bin");
        std::fill(records.begin() + 32, records.end(), '\xff');
        const auto copy = damagedCopy(db->folder(), name, "records.bin", records);
        REQUIRE(copy);  // it opens: only its records are changed
        CHECK(ask(*copy) == questions);
    }

    SUBCASE("its index's places past its records, and its games' ids past the database's games") {
        auto index = sound.at("index.bin");
        for (std::size_t at = 32; at + 8 <= index.size(); at += 8)
            putU64(index, at, at % 16 == 0 ? ~uint64_t{0} : sound.at("records.bin").size());
        {
            const auto copy = damagedCopy(db->folder(), name, "index.bin", index);
            REQUIRE(copy);
            CHECK(ask(*copy) == questions);
        }
        auto order = sound.at("order.bin");
        std::fill(order.begin() + 32, order.end(), '\xff');
        const auto copy = damagedCopy(db->folder(), name, "order.bin", order);
        REQUIRE(copy);
        CHECK(ask(*copy) > 0);  // the popular positions keep their numbers: those answer without the games
    }

    SUBCASE("its records, index and order changed at random, thirty times") {
        std::mt19937 random(139);
        const char* files[] = {"records.bin", "records.bin", "index.bin", "order.bin"};
        for (int round = 0; round < 30; ++round) {
            CAPTURE(round);
            const std::string file = files[random() % 4];
            auto bytes             = sound.at(file);
            for (int k = 0; k < 16; ++k)
                bytes[32 + random() % (bytes.size() - 32)] ^= static_cast<char>(1 + random() % 255);
            const auto copy = damagedCopy(db->folder(), name, file, bytes);
            REQUIRE(copy);
            (void)ask(*copy);
        }
    }
}
