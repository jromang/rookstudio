// 7z archives (#75): their PGN files read as they are decoded, as Lumbra's Gigabase comes.
#include <doctest/doctest.hpp>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <study/import.hpp>
#include <study/sources.hpp>

#include "test_games.hpp"

using namespace fcg::study;
using namespace fcg::test;
namespace fs = std::filesystem;

namespace {

// The games of a file, each as the import reads it: its tags and moves.
std::vector<std::string> gamesOf(const fs::path& file, ImportReport* report = nullptr) {
    ImportProgress progress;
    std::atomic<bool> cancel{false};
    std::vector<std::string> out;
    const auto r = importGames(file, ImportOptions{2, 4096}, progress, cancel, [&](std::vector<ImportedGame>&& batch) {
        for (const auto& g : batch) out.push_back(importedPgn(g));
    });
    if (report) *report = r;
    return out;
}

}  // namespace

TEST_CASE("7z archives: the games of an archive in LZMA2 and in LZMA, those of the PGN file it holds") {
    const auto plain = gamesOf(data("games.pgn"));
    REQUIRE(plain.size() > 50);
    CHECK(gamesOf(data("games.7z")) == plain);
    CHECK(gamesOf(data("games-lzma.7z")) == plain);
}

TEST_CASE("7z archives: a solid archive's PGN files one after the other, its other files passed over") {
    // first.pgn ends without a line break, README.txt holds an [Event line, second.pgn the rest of games.pgn.
    CHECK(gamesOf(data("several.7z")) == gamesOf(data("games.pgn")));
}

TEST_CASE("7z archives: one cut short or not an archive is said, never read past") {
    std::ifstream in(data("games.7z"), std::ios::binary);
    const std::string whole((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const auto cut = fs::temp_directory_path() / "cut.7z";
    for (const std::size_t keep : {std::size_t{40}, whole.size() / 2, whole.size() - 10}) {
        CAPTURE(keep);
        std::ofstream(cut, std::ios::binary | std::ios::trunc) << whole.substr(0, keep);
        ImportReport report;
        (void)gamesOf(cut, &report);
        CHECK_FALSE(report.error.empty());
    }
    // Garbled bytes in the middle: an error, or fewer games, never a crash.
    std::string garbled = whole;
    for (std::size_t i = 40; i < garbled.size() - 40; i += 7) garbled[i] = static_cast<char>(garbled[i] ^ 0x5A);
    std::ofstream(cut, std::ios::binary | std::ios::trunc) << garbled;
    (void)gamesOf(cut);
}
