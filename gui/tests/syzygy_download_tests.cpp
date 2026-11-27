// The Syzygy tables of 3 to 5 pieces downloaded into the profile (#82): one file at a time, each checked, a download
// cut short going on from where it stopped, against a server on this machine serving the repository's 3-piece tables.
#include <doctest/doctest.hpp>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string>
#include <vector>

#include <fmt/include/fmt/format.h>

#include "free_ports.hpp"
#include "https.hpp"
#include "scripted_server.hpp"
#include "syzygy_download.hpp"
#include "test_support.hpp"

using namespace fcg;
using namespace fcg::test;
namespace fs = std::filesystem;

namespace {

fs::path tables3() { return test::sourcePath() / "gui" / "tests" / "data" / "syzygy3"; }

}  // namespace

TEST_CASE("Syzygy download: the set's files, the smallest piece counts first") {
    const auto& files = syzygy345Files();
    CHECK(files.size() == 290);
    CHECK(syzygy345Bytes() == 983957920ull);
    int pieces = 3;
    for (const auto& f : files) {
        CAPTURE(f.name);
        const int n = syzygyFilePieces(f.name);
        CHECK(n >= pieces);
        pieces = n;
        CHECK(f.size % 64 == 16);
    }
    CHECK(syzygyFilePieces("KRPvKR.rtbz") == 5);
    CHECK(std::string(syzygy345Url()).back() == '/');
    // The repository's tables are tables; a file of another kind is not one.
    CHECK(syzygyFileValid(tables3() / "KQvK.rtbw"));
    CHECK(syzygyFileValid(tables3() / "KQvK.rtbz"));
    const auto dir = fs::temp_directory_path() / "syzygy-marks";
    fs::create_directories(dir);
    fs::copy_file(tables3() / "KQvK.rtbw", dir / "KQvK.rtbz", fs::copy_options::overwrite_existing);
    CHECK_FALSE(syzygyFileValid(dir / "KQvK.rtbz"));  // a WDL file's mark under a DTZ file's name
    std::ofstream(dir / "KRvK.rtbw", std::ios::binary) << std::string(80, 'x');
    CHECK_FALSE(syzygyFileValid(dir / "KRvK.rtbw"));
    fs::remove_all(dir);
}

TEST_CASE("Syzygy download: each file in turn, cut short and resumed, the tables ready by piece count") {
    // The set: the five 3-piece tables of the repository, WDL and DTZ.
    std::vector<SyzygyFile> set;
    static const std::vector<std::string> names = {"KBvK.rtbw", "KBvK.rtbz", "KNvK.rtbw", "KNvK.rtbz", "KPvK.rtbw",
                                                   "KPvK.rtbz", "KQvK.rtbw", "KQvK.rtbz", "KRvK.rtbw", "KRvK.rtbz"};
    for (const auto& n : names) set.push_back({n.c_str(), fs::file_size(tables3() / n)});

    std::atomic<int> cut_at{-1};  // the next answer stops after this many bytes
    std::atomic<bool> broken{false};
    std::mutex mutex;
    std::vector<std::string> asked;
    const auto port = freePort();
    ScriptedServer server(port, [&](const std::string& head, net::TcpConnection& c) {
        const auto path  = requestPath(head);
        const auto name  = path.substr(path.rfind('/') + 1);
        const auto range = httpHeader(head, "Range");
        {
            std::lock_guard lock(mutex);
            asked.push_back(name + (range ? " " + *range : std::string()));
        }
        std::string content = readFile(tables3() / name);
        if (content.empty()) {
            c.sendAll("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n");
            return;
        }
        if (broken && name == "KRvK.rtbz") content[0] = 'x';  // a file that is no table
        std::size_t from = 0;
        if (range) from = std::stoul(range->substr(6));
        if (from > 0)
            c.sendAll(
                fmt::format("HTTP/1.1 206 Partial Content\r\nETag: \"t\"\r\nContent-Range: bytes {}-{}/{}\r\n"
                            "Content-Length: {}\r\n\r\n",
                            from, content.size() - 1, content.size(), content.size() - from));
        else
            c.sendAll(fmt::format("HTTP/1.1 200 OK\r\nETag: \"t\"\r\nContent-Length: {}\r\n\r\n", content.size()));
        const int cut        = name == "KPvK.rtbw" ? cut_at.exchange(-1) : -1;
        const std::size_t to = cut >= 0 ? static_cast<std::size_t>(cut) : content.size();
        if (to > from) c.sendAll(std::string_view(content).substr(from, to - from));
    });
    REQUIRE(server.ok());
    const auto folder = fs::temp_directory_path() / "syzygy";
    fs::remove_all(folder);
    const auto base = fmt::format("http://127.0.0.1:{}/tables/standard/3-4-5", port);

    // Cut in the middle of KPvK.rtbw: the files before it are there and checked, KPvK's part kept.
    cut_at = 3000;
    JobGroup jobs;
    SyzygyDownload download(jobs, folder, base, set);
    CHECK(download.state().files == 0);
    CHECK(download.state().ready == 0);
    download.start();
    REQUIRE(waitUntil([&] { return !download.busy(); }));
    auto why = download.finished();
    REQUIRE(why);
    CHECK_FALSE(why->done());
    CHECK(download.state().files == 4);
    CHECK(download.state().ready == 0);
    CHECK(fs::file_size(fs::path(folder / "KPvK.rtbw").concat(".part")) == 3000);

    // Go on: from where it stopped, then the rest; the 3-piece tables ready.
    const auto generation = download.generation();
    download.start();
    REQUIRE(waitUntil([&] { return !download.busy(); }));
    why = download.finished();
    REQUIRE(why);
    CHECK(why->done());
    const auto state = download.state();
    CHECK(state.files == 10);
    CHECK(state.complete);
    CHECK(state.ready == 5);  // every file of the set is there: nothing of 4 or 5 pieces is missing from it
    CHECK(download.generation() > generation);
    CHECK(download.progress().bytes() == state.bytes);
    for (const auto& n : names) CHECK(readFile(folder / n) == readFile(tables3() / n));
    {
        std::lock_guard lock(mutex);
        CHECK(std::find(asked.begin(), asked.end(), "KPvK.rtbw bytes=3000-") != asked.end());
        CHECK(std::count(asked.begin(), asked.end(), "KBvK.rtbw") == 1);  // a file there is not asked again
    }

    // A file the server sends that is no table: removed, said, the others kept.
    fs::remove(folder / "KRvK.rtbz");
    broken = true;
    download.start();
    REQUIRE(waitUntil([&] { return !download.busy(); }));
    why = download.finished();
    REQUIRE(why);
    CHECK(why->error.find("KRvK.rtbz") != std::string::npos);
    CHECK_FALSE(fs::exists(folder / "KRvK.rtbz"));
    CHECK(download.state().files == 9);
    fs::remove_all(folder);
}
