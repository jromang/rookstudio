// FIDE's rating list in the profile (#88): downloaded from a ratings.fide.com of this machine, read into RookStudio's
// file, not downloaded again when FIDE has published nothing newer, a list whose columns changed refused, the day of
// a list, the month the Home page offers. The players are invented: FIDE's own list is never part of the tests.

#include <doctest/doctest.hpp>

#include <atomic>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>

#include <fmt/include/fmt/format.h>

#include "fide_rating_list.hpp"
#include "free_ports.hpp"
#include "scripted_server.hpp"
#include "test_support.hpp"
#include "zip.hpp"

using namespace fcg;
using namespace fcg::test;
namespace fs = std::filesystem;
using fcg::test::ScratchDir;

namespace {

constexpr std::size_t WIDTHS[] = {15, 61, 4, 4, 5, 5, 15, 4, 6, 4, 3, 6, 4, 3, 6, 4, 3, 6, 4};

std::string row(const std::vector<std::string>& values) {
    std::string out;
    for (std::size_t c = 0; c < values.size(); ++c) {
        auto v = values[c];
        v.resize(std::max(v.size(), WIDTHS[c]), ' ');
        out += v;
    }
    return out + "\r\n";
}

std::string list(const std::string& rating_column = "SRtng") {
    std::string text = row({"ID Number", "Name", "Fed", "Sex", "Tit", "WTit", "OTit", "FOA", rating_column, "SGm", "SK",
                            "RRtng", "RGm", "Rk", "BRtng", "BGm", "BK", "B-day", "Flag"});
    text += row({"90300001", "Moreau, Claire", "FRA", "F", "", "WIM", "", "", "2341", "9", "20", "0", "0", "20", "0",
                 "0", "20", "1994", "w"});
    text += row({"90300002", "Lindqvist, Erik", "SWE", "M", "IM", "", "", "", "2398", "9", "20", "0", "0", "20", "2410",
                 "0", "20", "1991", ""});
    return text;
}

// FIDE's download: the list as a zip, dated by its Last-Modified.
struct FakeFide {
    explicit FakeFide(std::string body, std::string modified = "Sun, 22 Nov 2026 22:03:51 GMT", int status = 200)
        : port(freePort()),
          zip(std::move(body)),
          modified_(std::move(modified)),
          status_(status),
          server(port, [this](const std::string& head, net::TcpConnection& c) {
              ++requests;
              if (head.rfind("GET /download/players_list.zip ", 0) != 0 || status_ != 200) {
                  c.sendAll(fmt::format("HTTP/1.1 {} X\r\nContent-Length: 0\r\nConnection: close\r\n\r\n",
                                        status_ == 200 ? 404 : status_));
                  return;
              }
              c.sendAll(fmt::format("HTTP/1.1 200 OK\r\nContent-Type: application/zip\r\nContent-Length: {}\r\n"
                                    "Last-Modified: {}\r\nConnection: close\r\n\r\n",
                                    zip.size(), modified_) +
                        zip);
          }) {
        REQUIRE(server.ok());
        fide::setServer(fmt::format("http://127.0.0.1:{}", port));
    }
    ~FakeFide() { fide::setServer("https://ratings.fide.com"); }
    uint16_t port;
    std::string zip;
    std::string modified_;
    int status_;
    std::atomic<int> requests{0};
    ScriptedServer server;
};

fide::Job::Outcome wait(std::unique_ptr<fide::Job> job) {
    REQUIRE(job->done.wait_for(std::chrono::seconds(60)) == std::future_status::ready);
    return job->done.get();
}

}  // namespace

TEST_CASE("fide list: a day read from HTTP and from a zip, the month offered, the list kept in the profile") {
    const auto day = fide::httpDay("Sun, 22 Nov 2026 22:03:51 GMT");
    REQUIRE(day);
    CHECK(day->monthKey() == "2026-11");
    CHECK(day->text() == "2026-11-22");
    CHECK_FALSE(fide::httpDay("yesterday"));
    CHECK(fide::monthText("2026-11") == "November 2026");

    std::tm t{};
    t.tm_year           = 2026 - 1900;
    t.tm_mon            = 11;  // December
    t.tm_mday           = 2;
    t.tm_hour           = 12;
    const auto december = std::mktime(&t);
    fide::Kept kept;
    kept.month = "2026-11";
    CHECK(fide::monthDue(kept, december) == "2026-12");
    kept.offered = "2026-12";  // "Not now"
    CHECK(fide::monthDue(kept, december).empty());
    kept.month   = "2026-12";
    kept.offered = "";
    CHECK(fide::monthDue(kept, december).empty());

    const auto zip = fs::temp_directory_path() / "fide-day.zip";
    {
        // 2026-11-22, 22:03 in MS-DOS format.
        const uint16_t date = static_cast<uint16_t>(((2026 - 1980) << 9) | (11 << 5) | 22);
        const uint16_t time = static_cast<uint16_t>((22 << 11) | (3 << 5));
        std::ofstream out(zip, std::ios::binary);
        out << zipArchive("players_list_foa.txt", list(), time, date);
    }
    const auto zipped = fide::zipDay(zip);
    REQUIRE(zipped);
    CHECK(zipped->text() == "2026-11-22");

    const ScratchDir profile("fide-kept");
    CHECK_FALSE(fide::readKept(profile));
    kept.month         = "2026-11";
    kept.published     = "2026-11-22";
    kept.info.players  = 2;
    kept.info.standard = 2;
    kept.file          = "list-2026-11.bin";
    fide::writeKept(profile, kept);
    CHECK_FALSE(fide::readKept(profile));  // its list is not there
    std::ofstream(fide::listFile(profile, kept)) << "x";
    const auto back = fide::readKept(profile);
    REQUIRE(back);
    CHECK(back->month == "2026-11");
    CHECK(back->info.standard == 2);
    CHECK(back->file == "list-2026-11.bin");
    CHECK(back->bytes == 1);
    fide::removeKept(profile);
    CHECK_FALSE(fide::readKept(profile));
}

TEST_CASE("fide list: downloaded, read into the profile, not downloaded again; a list that changed refused") {
    const uint16_t date = static_cast<uint16_t>(((2026 - 1980) << 9) | (11 << 5) | 22);
    const ScratchDir profile("fide-download");
    {
        FakeFide fide(zipArchive("players_list_foa.txt", list(), 0, date));
        const auto first = wait(fide::start(profile));
        REQUIRE(first.ok);
        CHECK(first.kept.month == "2026-11");
        CHECK(first.kept.published == "2026-11-22");
        CHECK(first.kept.info.players == 2);
        CHECK(first.kept.info.blitz == 1);
        CHECK_FALSE(fs::exists(profile / "fide" / "players_list.zip"));  // read: the zip goes
        const auto kept = fide::readKept(profile);
        REQUIRE(kept);
        CHECK(kept->month == "2026-11");
        CHECK(kept->file == "list-2026-11.bin");
        const study::FideList read(fide::listFile(profile, *kept));
        CHECK(read.player(*read.byId(90300002)).name == "Lindqvist, Erik");

        // The same month: asked, not downloaded again.
        const int before = fide.requests;
        const auto again = wait(fide::start(profile));
        CHECK(again.same);
        CHECK_FALSE(again.ok);
        CHECK(fide.requests == before + 1);
    }
    {
        // December's list, its columns changed: refused, November's kept.
        FakeFide fide(zipArchive("players_list_foa.txt", list("STD"), 0, date), "Tue, 22 Dec 2026 21:00:00 GMT");
        const auto changed = wait(fide::start(profile));
        CHECK(changed.format);
        CHECK_FALSE(changed.ok);
        CHECK(fide::readKept(profile)->month == "2026-11");
        CHECK(study::FideList(fide::listFile(profile, *fide::readKept(profile))).size() == 2);
    }
    {
        // The site down: why, nothing changed.
        FakeFide fide("", "", 503);
        const auto down = wait(fide::start(profile));
        CHECK_FALSE(down.ok);
        CHECK(down.error.find("503") != std::string::npos);
        CHECK(down.net.kind == NetFailure::Kind::Status);  // kept by its kind, worded translated (#141)
        CHECK(down.net.status == 503);
        CHECK(fide::readKept(profile)->month == "2026-11");
    }
    // A file downloaded before, read from the disk: its day from the archive.
    const auto file = fs::temp_directory_path() / "fide-file.zip";
    {
        std::ofstream out(file, std::ios::binary);
        out << zipArchive("players_list_foa.txt", list(), 0,
                          static_cast<uint16_t>(((2026 - 1980) << 9) | (10 << 5) | 3));
    }
    const auto from_file = wait(fide::start(profile, file));
    REQUIRE(from_file.ok);
    CHECK(from_file.kept.month == "2026-10");
    CHECK(fs::exists(file));  // the user's file stays
    // The list before goes: a file of each list, one kept.
    int lists = 0;
    for (const auto& entry : fs::directory_iterator(profile / "fide"))
        if (entry.path().extension() == ".bin") ++lists;
    CHECK(lists == 1);
    CHECK(fide::readKept(profile)->file == "list-2026-10.bin");
}
