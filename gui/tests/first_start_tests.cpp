#include <doctest/doctest.hpp>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <string>

#include "first_start.hpp"
#include "test_support.hpp"

namespace fs = std::filesystem;
using namespace fcg::first;
using fcg::test::readFile;

namespace {

void write(const fs::path& file, const std::string& text) {
    fs::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary) << text;
}

// A fastchess-gui profile: engines, books, settings, two tournaments, and what a copy leaves out.
fs::path legacy(const std::string& name) {
    const auto dir = fs::temp_directory_path() / name;
    fs::remove_all(dir);
    write(dir / "engines.json", R"([{"name":"Gaia"},{"name":"Stockfish"},{"name":"Lc0"}])");
    write(dir / "books.json", R"([{"name":"UHO"},{"name":"8moves"}])");
    write(dir / "settings.json", R"({"check_new_version":true})");
    write(dir / "setup.json", R"({"language":"fr"})");
    write(dir / "runs" / "gaia-vs-sf" / "tournament.json", "{}");
    write(dir / "runs" / "gaia-vs-sf" / "games.pgn", std::string(1000, 'p'));
    write(dir / "runs" / "gaia-vs-sf" / "control.json", "{}");
    write(dir / "runs" / "gauntlet" / "tournament.json", "{}");
    write(dir / "running.json", "[]");
    write(dir / "queue.json", "[]");
    write(dir / "fastchess-gui.log", "a log");
    return dir;
}

}  // namespace

TEST_CASE("first start: a profile that never started, and one that did") {
    const auto dir = fs::temp_directory_path() / "first-fresh";
    fs::remove_all(dir);
    fs::create_directories(dir);
    CHECK(fresh(dir));
    write(dir / "settings.json", R"({"check_new_version":true})");
    CHECK(fresh(dir));
    write(dir / "settings.json", R"({"space":"chess"})");  // a profile of before the views
    CHECK_FALSE(fresh(dir));
    write(dir / "settings.json", R"({"views":[{"name":"Study","pages":["board"]}],"view":0})");
    CHECK_FALSE(fresh(dir));
    write(dir / "settings.json", "not json");
    CHECK(fresh(dir));
}

TEST_CASE("first start: the view shown first, among those chosen") {
    Page page;
    // Without fastchess-gui's profile: Study, when chosen.
    page.views = {true, true, false};
    CHECK(shownFirst(page) == 1);
    page.views = {true, false, true};
    CHECK(shownFirst(page) == 0);
    page.views = {false, false, true};
    CHECK(shownFirst(page) == 2);
    // fastchess-gui's users: Tests, when chosen.
    page.legacy = "/somewhere/fastchess-gui";
    page.views  = {true, true, false};
    CHECK(shownFirst(page) == 0);
    page.views = {false, true, true};
    CHECK(shownFirst(page) == 1);
}

TEST_CASE("first start: fastchess-gui's profile, beside the program's own in the system's place") {
    const auto base = fs::temp_directory_path() / "first-base";
    fs::remove_all(base);
    const auto own = base / "rookstudio";
    fs::create_directories(own);
    // None yet; then one without anything to copy; then one.
    CHECK(legacyProfile(own / "").empty());
    fs::create_directories(base / "fastchess" / "fastchess-gui");
    CHECK(legacyProfile(own / "").empty());
    write(base / "fastchess" / "fastchess-gui" / "engines.json", "[]");
    CHECK(legacyProfile(own / "") == base / "fastchess" / "fastchess-gui");
    CHECK(legacyProfile(own) == base / "fastchess" / "fastchess-gui");
}

TEST_CASE("first start: what fastchess-gui's profile holds, and what a copy leaves out") {
    CHECK(copied("engines.json"));
    CHECK(copied("runs/t/games.pgn"));
    CHECK_FALSE(copied("running.json"));
    CHECK_FALSE(copied("queue.json"));
    CHECK_FALSE(copied("fastchess-gui.log"));
    CHECK_FALSE(copied("runs/t/control.json"));
    CHECK_FALSE(copied("runs/t/control.sock"));
    CHECK_FALSE(copied("imgui.ini"));

    const auto from = legacy("first-summary");
    const auto s    = summarize(from);
    CHECK(s.engines == 3);
    CHECK(s.books == 2);
    CHECK(s.tournaments == 2);
    CHECK(s.settings);
    // The files copied: neither running.json, queue.json, the log nor the control file.
    CHECK(s.bytes == readFile(from / "engines.json").size() + readFile(from / "books.json").size() +
                         readFile(from / "settings.json").size() + readFile(from / "setup.json").size() + 2 + 1000 + 2);
}

TEST_CASE("first start: the copy of fastchess-gui's profile, which stays as it was") {
    const auto from = legacy("first-from");
    const auto to   = fs::temp_directory_path() / "first-to";
    fs::remove_all(to);
    fs::create_directories(to);
    write(to / "rookstudio.log", "our log");
    write(to / "engines.json", "[]");  // what the program's profile had is replaced

    std::atomic<uint64_t> done{0};
    std::string error;
    REQUIRE(copyProfile(from, to, error, &done));
    CHECK(error.empty());
    CHECK(done == summarize(from).bytes);
    CHECK(readFile(to / "engines.json") == readFile(from / "engines.json"));
    CHECK(readFile(to / "setup.json") == R"({"language":"fr"})");
    CHECK(readFile(to / "runs" / "gaia-vs-sf" / "games.pgn") == std::string(1000, 'p'));
    CHECK(fs::is_directory(to / "runs" / "gauntlet"));
    CHECK_FALSE(fs::exists(to / "running.json"));
    CHECK_FALSE(fs::exists(to / "queue.json"));
    CHECK_FALSE(fs::exists(to / "fastchess-gui.log"));
    CHECK_FALSE(fs::exists(to / "runs" / "gaia-vs-sf" / "control.json"));
    CHECK(readFile(to / "rookstudio.log") == "our log");
    // fastchess-gui's profile is untouched.
    CHECK(readFile(from / "running.json") == "[]");
    CHECK(readFile(from / "fastchess-gui.log") == "a log");
    CHECK(fresh(to));  // the views are the first start's to keep
}
