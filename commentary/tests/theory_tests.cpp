// A game's theory and novelty against a database (the prototype's refdb/theory.rs, mod tests, and more).

#include <doctest/doctest.hpp>

#include <atomic>
#include <filesystem>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

#include <commentary/theory.hpp>
#include <study/import.hpp>
#include <study/store.hpp>
#include <study/tree.hpp>

#include "helpers.hpp"

using namespace fcg;
using namespace fcg::commentary;
namespace fs = std::filesystem;

namespace {

// A database of games written as moves and a year, and its tree.
struct Reference {
    explicit Reference(const std::string& name, const std::vector<std::pair<std::string, int>>& games) {
        const auto dir = fs::temp_directory_path() / name;
        fs::remove_all(dir);
        db = study::Database::create(dir);
        std::vector<study::ImportedGame> imported;
        int round = 0;
        for (const auto& [moves, year] : games) {
            study::ImportedGame g;
            const auto text = "[Event \"Ref\"]\n[Round \"" + std::to_string(++round) + "\"]\n[Date \"" +
                              std::to_string(year) + ".01.01\"]\n[Result \"*\"]\n\n" + moves + " *\n";
            REQUIRE(study::readImportedGame(text, g));
            imported.push_back(std::move(g));
        }
        db->append(std::move(imported), true);
        study::TreeProgress progress;
        std::atomic<bool> cancel{false};
        REQUIRE(study::buildTree(*db, *db->snapshot(), {}, progress, cancel).error.empty());
        tree = study::Tree::open(db->folder());
        REQUIRE(tree);
        snapshot = db->snapshot();
    }
    std::shared_ptr<study::Database> db;
    std::shared_ptr<const study::Tree> tree;
    std::shared_ptr<const study::Snapshot> snapshot;
};

MainLine line(std::initializer_list<std::string_view> ucis) {
    const auto start = Position::initial();
    return MainLine(start, test::moves(start, ucis));
}

}  // namespace

TEST_CASE("theory: the moves many games played, then the first none had") {
    Reference ref("theory-book", {{"1. e4 e5 2. Nf3 Nc6 3. Bb5 a6", 1990},
                                  {"1. e4 e5 2. Nf3 Nc6 3. Bb5 a6", 1991},
                                  {"1. e4 e5 2. Nf3 Nc6 3. Bb5 a6", 1992},
                                  {"1. e4 e5 2. Nf3 Nc6 3. Bb5 Nf6", 1993},
                                  {"1. e4 e5 2. Nf3 Nc6 3. Bc4", 1994},
                                  {"1. e4 e5 2. Nf3 Nf6", 1995},
                                  {"1. e4 e5 2. Nf3 Nf6 3. Nxe5", 1996},
                                  {"1. e4 c5 2. Nf3 d6", 2020}});
    // 2...Nf6, two games, is not theory; 3.d4 none played: a novelty after three moves of theory.
    auto t = theoryOf(*ref.tree, ref.snapshot, line({"e2e4", "e7e5", "g1f3", "g8f6", "d2d4"}));
    CHECK(t.book_plies == 3);
    CHECK(t.novelty == 3 + 1);
    CHECK(t.reached == 2);  // the two games of 2...Nf6, one that stopped there, one with 3.Nxe5
    // With fewer games asked of theory, 2...Nf6 is.
    TheoryOptions two;
    two.book_games = 2;
    t              = theoryOf(*ref.tree, ref.snapshot, line({"e2e4", "e7e5", "g1f3", "g8f6", "d2d4"}), two);
    CHECK(t.book_plies == 4);
    CHECK(t.novelty == 4);
    // A line the games all followed: no novelty.
    t = theoryOf(*ref.tree, ref.snapshot, line({"e2e4", "e7e5", "g1f3", "b8c6"}));
    CHECK(t.book_plies == 4);
    CHECK_FALSE(t.novelty);
}

TEST_CASE("theory: a single game followed, the game itself, and another copy of it") {
    Reference ref("theory-single", {{"1. e4 e5 2. Nf3 Nc6", 1990},
                                    {"1. e4 e5 2. Nf3 Nc6", 1991},
                                    {"1. e4 e5 2. Nf3 Nc6", 1992},
                                    {"1. e4 e5 2. Nf3 Nc6", 1993},
                                    {"1. e4 e5 2. Nf3 Nc6", 1994},
                                    {"1. e4 c5 2. Nf3 d6", 2020},
                                    {"1. d4 d5 2. c4 e6 3. Nc3", 2021}});
    // 1...c5, one game: followed while the line is its moves; 3.d4 leaves it.
    auto t = theoryOf(*ref.tree, ref.snapshot, line({"e2e4", "c7c5", "g1f3", "d7d6", "d2d4"}));
    CHECK(t.book_plies == 1);
    CHECK(t.novelty == 4);
    CHECK(t.reached == 1);
    // The same game of 2020, another copy: its single game of the same year is itself, a novelty at 1...c5.
    TheoryOptions copy;
    copy.year = 2020;
    t         = theoryOf(*ref.tree, ref.snapshot, line({"e2e4", "c7c5", "g1f3", "d7d6", "d2d4"}), copy);
    CHECK(t.novelty == 1);
    CHECK(t.reached == 5);  // 1.e4 in six games, the copy left out
    // The 1.d4 game reviewed from the database itself: no other game played 1.d4.
    TheoryOptions itself;
    itself.itself = 6;
    t             = theoryOf(*ref.tree, ref.snapshot, line({"d2d4", "d7d5", "c2c4", "e7e6", "b1c3"}), itself);
    CHECK(t.novelty == 0);
    CHECK(t.book_plies == 0);
    CHECK(t.reached == 6);  // seven games, the one reviewed left out
}

TEST_CASE("theory: a position another game reached by other moves is no novelty") {
    Reference ref("theory-transposed", {{"1. d4 d5 2. c4 e6", 1990},
                                        {"1. d4 d5 2. c4 c6", 1991},
                                        {"1. d4 d5 2. c4 dxc4", 1992},
                                        {"1. d4 d5 2. c4 Nf6", 1993},
                                        {"1. d4 d5 2. c4 e5", 1994},
                                        {"1. Nf3 d5 2. d4 Nf6", 1995}});
    // 2.Nf3: none of the 1.d4 d5 games played it, but 1.Nf3 d5 2.d4 reaches the same position.
    TheoryOptions blind;
    blind.confirm = false;
    auto t        = theoryOf(*ref.tree, ref.snapshot, line({"d2d4", "d7d5", "g1f3", "g8f6"}), blind);
    CHECK(t.book_plies == 2);
    CHECK(t.novelty == 2);
    t = theoryOf(*ref.tree, ref.snapshot, line({"d2d4", "d7d5", "g1f3", "g8f6"}));
    CHECK(t.book_plies == 2);
    CHECK_FALSE(t.novelty);
}
