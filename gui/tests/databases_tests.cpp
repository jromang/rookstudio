#include <doctest/doctest.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <thread>

#include <study/chessbase.hpp>

#include "databases.hpp"

namespace fs = std::filesystem;
using namespace fcg;

namespace {

fs::path freshDir(const std::string& name) {
    const auto dir = fs::temp_directory_path() / name;
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

// `count` short games, each its own.
fs::path pgnFile(const fs::path& dir, int count) {
    const auto file = dir / "games.pgn";
    std::ofstream out(file, std::ios::binary);
    const char* replies[] = {"e5", "c5", "e6", "c6", "d6", "g6", "Nf6", "d5"};
    for (int i = 0; i < count; ++i) {
        out << "[Event \"Test\"]\n[Round \"" << i << "\"]\n[White \"A" << i << "\"]\n[Black \"B" << i
            << "\"]\n[Result \"1-0\"]\n\n1. e4 " << replies[i % 8] << " 2. Nf3 1-0\n\n";
    }
    return file;
}

// The study's test data: archives of our own test games.
fs::path studyData(const std::string& name) {
    const char* env = std::getenv("FCG_SOURCE_DIR");
    return fs::path(env ? env : FCG_SOURCE_DIR) / "study" / "tests" / "data" / name;
}

std::optional<study::DatabaseImport> imported(DatabaseShelf& shelf) {
    std::optional<study::DatabaseImport> report;
    const auto give_up = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    while (!(report = shelf.finishedImport()) && std::chrono::steady_clock::now() < give_up)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    return report;
}

}  // namespace

TEST_CASE("databases: made, added, removed, and listed again from databases.json") {
    const auto profile = freshDir("shelf-profile");
    std::size_t made   = 0;
    {
        DatabaseShelf shelf(profile);
        CHECK(shelf.entries().empty());
        made = shelf.create("Mes parties");
        CHECK(shelf.entries()[made].name == "Mes parties");
        CHECK(fs::is_regular_file(shelf.entries()[made].folder / "manifest.json"));
        // The same name again: made unique, in a folder of its own.
        const auto twice = shelf.create("Mes parties");
        CHECK(shelf.entries()[twice].name == "Mes parties (2)");
        CHECK(shelf.entries()[twice].folder != shelf.entries()[made].folder);
        // A name a folder cannot carry.
        const auto odd = shelf.create("a/b:c");
        CHECK(shelf.entries()[odd].folder.filename() == "a_b_c.fcgdb");
        CHECK(shelf.facts(made).games == 0);
        CHECK(shelf.facts(made).error.empty());
    }
    {
        DatabaseShelf shelf(profile);
        REQUIRE(shelf.entries().size() == 3);
        CHECK(shelf.entries()[1].name == "Mes parties (2)");
        const auto folder = shelf.entries()[1].folder;
        shelf.remove(1);
        CHECK(shelf.entries().size() == 2);
        CHECK(fs::exists(folder));  // its files stay
        // Added back as a folder; added twice, listed once.
        const auto back = shelf.add(folder);
        CHECK(shelf.add(folder) == back);
        CHECK(shelf.entries().size() == 3);
        CHECK_THROWS(shelf.add(profile));  // no manifest: no database
    }
}

TEST_CASE("databases: an import in the background, then the database's opening tree built") {
    const auto profile = freshDir("shelf-import");
    DatabaseShelf shelf(profile);
    const auto index = shelf.create("Test");
    REQUIRE(shelf.startImport(index, pgnFile(profile, 40)));
    CHECK_FALSE(shelf.startImport(index, pgnFile(profile, 40)));  // one at a time
    std::optional<study::DatabaseImport> report;
    const auto give_up = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    while (!(report = shelf.finishedImport()) && std::chrono::steady_clock::now() < give_up)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    REQUIRE(report);
    CHECK(report->error.empty());
    CHECK(report->added == 40);
    CHECK(shelf.import() == nullptr);
    const auto facts = shelf.facts(index);
    CHECK(facts.games == 40);
    CHECK(facts.tree);
    CHECK(facts.bytes > 0);
    // Again: the games it has are left out.
    REQUIRE(shelf.startImport(index, profile / "games.pgn"));
    report.reset();
    while (!(report = shelf.finishedImport()) && std::chrono::steady_clock::now() < give_up)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    REQUIRE(report);
    CHECK(report->added == 0);
    CHECK(report->duplicates == 40);
}

TEST_CASE("databases: a cancelled import keeps what was committed") {
    const auto profile = freshDir("shelf-cancel");
    DatabaseShelf shelf(profile);
    const auto index = shelf.create("Test");
    REQUIRE(shelf.startImport(index, pgnFile(profile, 20)));
    shelf.cancelImport();
    std::optional<study::DatabaseImport> report;
    const auto give_up = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    while (!(report = shelf.finishedImport()) && std::chrono::steady_clock::now() < give_up)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    REQUIRE(report);
    CHECK(shelf.facts(index).games == report->added);
}

TEST_CASE("databases: the quick filter's words, a year, an ECO code and a result told apart") {
    auto f = quickFilter("  carlsen  b9 2024 1-0 ");
    CHECK(f.words == "carlsen");
    CHECK(f.date_from == study::packDate("2024.01.01"));
    CHECK(f.date_to == study::packDate("2024.12.31"));
    CHECK(f.eco_from == study::packEco("B90"));
    CHECK(f.eco_to == study::packEco("B99"));
    CHECK(f.result == '1');
    f = quickFilter("Kasparov Topalov C");
    CHECK(f.words == "Kasparov Topalov");
    CHECK(f.eco_from == study::packEco("C00"));
    CHECK(f.eco_to == study::packEco("C99"));
    f = quickFilter("½ 99 E97");
    CHECK(f.result == '=');
    CHECK(f.words == "99");  // not a year
    CHECK(f.eco_from == study::packEco("E97"));
    CHECK(f.eco_to == study::packEco("E97"));
    CHECK(quickFilter("").empty());
}

TEST_CASE("databases: a PGN file read as a database without a copy, after the user's own") {
    const auto profile = freshDir("shelf-linked");
    const auto file    = pgnFile(profile, 25);
    {
        DatabaseShelf shelf(profile);
        shelf.create("Mine");
        shelf.setLinked({{"games.pgn", study::LinkedSource{study::LinkedSource::Kind::PgnFile, file}}});
        REQUIRE(shelf.entries().size() == 2);
        CHECK_FALSE(shelf.entries()[0].source);
        REQUIRE(shelf.entries()[1].source);
        shelf.refresh(1);
        const auto give_up = std::chrono::steady_clock::now() + std::chrono::seconds(60);
        while (shelf.refreshing(1) && std::chrono::steady_clock::now() < give_up)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        CHECK(shelf.facts(1).games == 25);
        CHECK_FALSE(shelf.startImport(1, file));  // never written but by its refresh
        shelf.remove(1);                          // not removed: it is listed from its source
        CHECK(shelf.entries().size() == 2);
        // A database made now goes before the linked ones.
        CHECK(shelf.create("Mine too") == 1);
        CHECK(shelf.entries()[2].source);
        // The same source again keeps it as it is.
        shelf.setLinked({{"renamed", study::LinkedSource{study::LinkedSource::Kind::PgnFile, file}}});
        CHECK(shelf.entries()[2].name == "renamed");
        CHECK(shelf.facts(2).games == 25);
    }
    DatabaseShelf again(profile);
    CHECK(again.entries().size() == 2);  // databases.json keeps the user's own only
}

TEST_CASE("databases: an archive's wrong password, a file that is no text: nothing imported, the database discarded") {
    const auto profile = freshDir("shelf-refused");
    DatabaseShelf shelf(profile);
    // Archives of our own test games, enciphered with this password.
    const std::string password = "rstest20";
    for (const char* name : {"cbtest1.cbz", "cbtest2.2cbz"}) {
        INFO(name);
        const auto file = studyData(std::string("chessbase/") + name);
        CHECK(study::archivePasswordFits(file, password));
        CHECK_FALSE(study::archivePasswordFits(file, "wrongpwd"));
        const auto index = shelf.create(fs::path(name).stem().string());
        REQUIRE(shelf.startImport(index, file, "wrongpwd"));
        auto report = imported(shelf);
        REQUIRE(report);
        CHECK(report->added == 0);
        CHECK(report->read.error.find("wrong password") != std::string::npos);
        // Made for it, it is not kept.
        const auto folder = shelf.entries()[index].folder;
        shelf.discard(index);
        CHECK(shelf.entries().empty());
        CHECK_FALSE(fs::exists(folder));
        // With its password, its 35 games.
        const auto again = shelf.create(fs::path(name).stem().string());
        REQUIRE(shelf.startImport(again, file, password));
        report = imported(shelf);
        REQUIRE(report);
        CHECK(report->read.error.empty());
        CHECK(report->added == 35);
        CHECK(report->damaged == 0);
        shelf.remove(again);
    }
    // A file of bytes that are not text, given as a PGN.
    std::ifstream in(studyData("chessbase/cbtest2.2cbz"), std::ios::binary);
    const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const auto binary = profile / "games.pgn";
    std::ofstream(binary, std::ios::binary).write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    const auto index = shelf.create("games");
    REQUIRE(shelf.startImport(index, binary));
    const auto report = imported(shelf);
    REQUIRE(report);
    CHECK(report->added == 0);
    CHECK(report->read.games == 0);
    CHECK(report->read.error == "games.pgn is not a PGN file: its bytes are not text");
}
