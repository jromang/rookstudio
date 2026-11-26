// The engine picker's network and files, against a GitHub of this machine: its releases asked, a file downloaded,
// checked, taken out of its archive, made executable and started (the tests' random mover plays the engine).

#include <doctest/doctest.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include <fmt/include/fmt/format.h>
#include <core/config/config.hpp>

#include "engine_downloads.hpp"
#include "engines.hpp"
#include "free_ports.hpp"
#include "https.hpp"
#include "mock_engines.hpp"
#include "scripted_server.hpp"
#include "zip.hpp"

using namespace fcg;
using namespace fcg::catalogue;
using namespace fcg::test;
namespace fs = std::filesystem;

namespace {

#if defined(_WIN32)
const std::string EXE = ".exe";
#else
const std::string EXE;
#endif

void answer(net::TcpConnection& c, int status, const std::string& body) {
    c.sendAll(fmt::format("HTTP/1.1 {} X\r\nContent-Length: {}\r\nConnection: close\r\n\r\n", status, body.size()) +
              body);
}

// GitHub: its answers by path (the API's and the files'), and what was asked of it.
struct FakeGitHub {
    FakeGitHub()
        : port(freePort()), server(port, [this](const std::string& head, net::TcpConnection& c) {
              const auto line = head.substr(0, head.find("\r\n"));  // "GET /path HTTP/1.1"
              const auto path = line.substr(4, line.rfind(' ') - 4);
              std::lock_guard lock(mutex);
              asked.push_back(path);
              if (const auto it = answers.find(path); it != answers.end())
                  answer(c, it->second.first, it->second.second);
              else
                  answer(c, 404, R"({"message":"Not Found"})");
          }) {
        REQUIRE(server.ok());
    }
    [[nodiscard]] std::string url() const { return fmt::format("http://127.0.0.1:{}", port); }
    void set(const std::string& path, const std::string& body, int status = 200) {
        std::lock_guard lock(mutex);
        answers[path] = {status, body};
    }
    [[nodiscard]] std::vector<std::string> requests() {
        std::lock_guard lock(mutex);
        return asked;
    }

    uint16_t port;
    std::mutex mutex;
    std::map<std::string, std::pair<int, std::string>> answers;
    std::vector<std::string> asked;
    ScriptedServer server;
};

std::string mover() { return readFile(randomMover()); }

// A ustar header, its checksum made.
std::string tarHeader(const std::string& name, uint64_t size, char type) {
    std::string h(512, '\0');
    std::copy_n(name.begin(), std::min<std::size_t>(name.size(), 100), h.begin());
    const auto put = [&](std::size_t at, const std::string& text) {
        std::copy(text.begin(), text.end(), h.begin() + at);
    };
    put(100, "0000755");
    put(108, "0000000");
    put(116, "0000000");
    put(124, fmt::format("{:011o}", size));
    put(136, "00000000000");
    h[156] = type;
    put(257, "ustar");
    put(263, "00");
    std::fill(h.begin() + 148, h.begin() + 156, ' ');
    unsigned sum = 0;
    for (const unsigned char c : h) sum += c;
    put(148, fmt::format("{:06o}", sum));
    h[154] = '\0';
    return h;
}

std::string padded(std::string data) {
    data.resize((data.size() + 511) / 512 * 512, '\0');
    return data;
}

enum class LongName { No, Gnu, Pax };

// A tar archive of a folder and files: (path, data), a path past 100 characters written as GNU or pax write it.
std::string tarOf(const std::vector<std::pair<std::string, std::string>>& files, LongName long_name = LongName::No) {
    std::string out = tarHeader("engine/", 0, '5');
    for (const auto& [path, data] : files) {
        if (long_name == LongName::Gnu) {
            out += tarHeader("././@LongLink", path.size() + 1, 'L') + padded(path + '\0');
        } else if (long_name == LongName::Pax) {
            // "<length> path=<path>\n", the length counting its own digits.
            const auto record = " path=" + path + "\n";
            auto length       = record.size() + 1;
            while (std::to_string(length).size() + record.size() != length) ++length;
            const auto pax = std::to_string(length) + record;
            out += tarHeader("PaxHeaders/x", pax.size(), 'x') + padded(pax);
        }
        out += tarHeader(path.substr(0, std::min<std::size_t>(path.size(), 99)), data.size(), '0') + padded(data);
    }
    return out + std::string(1024, '\0');
}

// The engine of the tests, with one file for this machine.
Engine testEngine(const std::string& asset) {
    const auto cpu = detectCpu();
    Engine e;
    e.id                                  = "mover";
    e.name                                = "Mover";
    e.repo                                = "test/mover";
    e.licence                             = "MIT";
    e.tag                                 = "v{ver}";
    e.files[enginePlatforms(cpu).front()] = {{asset, cpu.arch == CpuArch::Arm64 ? "arm64" : "x86-64", {}}};
    return e;
}

Choice choiceOf(const FakeGitHub& github, const std::string& name, const std::string& data, std::string sha256,
                std::string inside = {}) {
    Choice c;
    c.version        = "1.0";
    c.tag            = "v1.0";
    c.platform       = enginePlatforms(detectCpu()).front();
    c.build          = {name, "x86-64", std::move(inside)};
    c.asset.name     = name;
    c.asset.size     = data.size();
    c.asset.sha256   = std::move(sha256);
    c.asset.url      = github.url() + "/download/" + name;
    c.asset.uploaded = true;
    c.licence        = "MIT";
    return c;
}

std::string releaseJson(const std::string& tag, const std::vector<std::string>& names, const std::string& base) {
    std::string assets;
    for (const auto& n : names)
        assets +=
            fmt::format(R"({}{{"name":"{}","state":"uploaded","size":1,"browser_download_url":"{}/download/{}"}})",
                        assets.empty() ? "" : ",", n, base, n);
    return fmt::format(
        R"({{"tag_name":"{}","draft":false,"prerelease":false,"published_at":"2026-11-18T00:00:00Z","assets":[{}]}})",
        tag, assets);
}

// Probes read their timeouts from fastchess' configuration, set by main() in the program.
void configure() {
    if (!fastchess::config::TournamentConfig) {
        fastchess::config::TournamentConfig               = std::make_unique<fastchess::config::Tournament>();
        fastchess::config::TournamentConfig->startup_time = ENGINE_STARTUP_TIME;
    }
}

// A profile of the test's own, removed after it.
struct Profile {
    Profile() : path(fs::temp_directory_path() / "engine-downloads") {
        std::error_code ec;
        fs::remove_all(path, ec);
        fs::create_directories(path);
    }
    ~Profile() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
    fs::path path;
};

}  // namespace

TEST_CASE("engine downloads: the latest release, the list when it is a side release, GitHub's limit") {
    FakeGitHub github;
    const auto cpu    = detectCpu();
    const auto engine = testEngine("mover-file");
    const auto base   = github.url();

    // The latest is the engine's: one question.
    github.set("/repos/test/mover/releases/latest", releaseJson("v1.0", {"mover-file"}, base));
    auto got = fetchReleases(engine, cpu, base);
    CHECK_FALSE(got.failed());
    REQUIRE(got.list.size() == 1);
    CHECK(github.requests() == std::vector<std::string>{"/repos/test/mover/releases/latest"});

    // A side release marked latest: the list, and the engine's release in it.
    github.set("/repos/test/mover/releases/latest", releaseJson("v1.1-tool", {"tool"}, base));
    github.set("/repos/test/mover/releases?per_page=20",
               "[" + releaseJson("v1.1-tool", {"tool"}, base) + "," + releaseJson("v1.0", {"mover-file"}, base) + "]");
    got = fetchReleases(engine, cpu, base);
    REQUIRE(got.list.size() == 2);
    const auto pick = pickBuild(engine, got.list, cpu);
    REQUIRE(pick.choices.size() == 1);
    CHECK(pick.choices.front().tag == "v1.0");
    CHECK(pick.choices.front().asset.url == base + "/download/mover-file");

    // GitHub's limit for requests without an account.
    github.set("/repos/test/mover/releases/latest", R"({"message":"API rate limit exceeded for 127.0.0.1."})", 403);
    got = fetchReleases(engine, cpu, base);
    CHECK(got.limited());
    CHECK(describe(got).find("60 an hour") != std::string::npos);
    CHECK(got.list.empty());
    github.set("/repos/test/mover/releases/latest", "{}", 429);
    CHECK(fetchReleases(engine, cpu, base).failure == GitHubFailure::RateLimited);

    // GitHub in trouble, or refusing for another reason: its status.
    github.set("/repos/test/mover/releases/latest", "<html>down</html>", 503);
    got = fetchReleases(engine, cpu, base);
    CHECK(got.failure == GitHubFailure::Refused);
    CHECK(describe(got) == "GitHub answered 503 for test/mover");

    // A repository that is not there.
    const auto absent = fetchReleases(testEngine("x"), cpu, base + "/nowhere");
    CHECK(absent.failure == GitHubFailure::NoRepository);
    CHECK(describe(absent) == "GitHub has no repository test/mover");

    // No answer: GitHub named, the network's words in the log.
    const auto nobody = fetchReleases(engine, cpu, fmt::format("http://127.0.0.1:{}", freePort()));
    CHECK(nobody.failure == GitHubFailure::Offline);
    CHECK(describe(nobody) == "GitHub cannot be reached");
    CHECK(describe(Releases{}).empty());
}

TEST_CASE("engine downloads: a program downloaded, checked against its hash, made executable and started") {
    configure();
    FakeGitHub github;
    Profile profile;
    const auto program = mover();
    REQUIRE_FALSE(program.empty());
    const auto name = "mover-file" + EXE;
    github.set("/download/" + name, program);
    const auto engine = testEngine(name);
    Pick pick;
    pick.choices = {choiceOf(github, name, program, sha256File(randomMover()))};

    const auto result = installEngine(profile.path, engine, pick);
    REQUIRE_MESSAGE(result.ok, result.error);
    const auto installed = fs::path(result.engine.program);
    CHECK(installed == versionFolder(profile.path, "mover", "1.0") / name);
    CHECK(readFile(installed) == program);
    CHECK_FALSE(result.probe.id_name.empty());
#ifndef _WIN32
    CHECK((fs::status(installed).permissions() & fs::perms::owner_exec) != fs::perms::none);
#endif
    const auto list = loadInstalled(profile.path);
    REQUIRE(list.size() == 1);
    CHECK(list[0].id == "mover");
    CHECK(list[0].version == "1.0");
    CHECK(list[0].tag == "v1.0");
    CHECK(list[0].licence == "MIT");
    CHECK(list[0].program == result.engine.program);
    CHECK(list[0].id_name == result.probe.id_name);
    CHECK(list[0].installed > 0);

    // The same version again: in its place, not twice.
    REQUIRE(installEngine(profile.path, engine, pick).ok);
    CHECK(loadInstalled(profile.path).size() == 1);
}

TEST_CASE("engine downloads: a file whose hash differs is not kept") {
    configure();
    FakeGitHub github;
    Profile profile;
    const auto name = "mover-file" + EXE;
    github.set("/download/" + name, mover());
    Pick pick;
    pick.choices      = {choiceOf(github, name, mover(), std::string(64, '0'))};
    const auto result = installEngine(profile.path, testEngine(name), pick);
    CHECK_FALSE(result.ok);
    CHECK(result.error.find("SHA-256") != std::string::npos);
    CHECK_FALSE(fs::exists(versionFolder(profile.path, "mover", "1.0") / name));
    CHECK(loadInstalled(profile.path).empty());
}

TEST_CASE("engine downloads: the program taken out of a .tar.gz and of a .zip") {
    configure();
    FakeGitHub github;
    Profile profile;
    const auto program = mover();
    const auto inside  = "engine/mover" + EXE;

    const auto tgz = gzipped(tarOf({{"engine/README.md", "an engine"}, {inside, program}}));
    REQUIRE_FALSE(tgz.empty());
    const auto zip = zipArchive(inside, program, 0, 0x21);
    for (const auto& [name, data] :
         std::vector<std::pair<std::string, std::string>>{{"mover.tar.gz", tgz}, {"mover.zip", zip}}) {
        CAPTURE(name);
        github.set("/download/" + name, data);
        Pick pick;
        // The archive's hash, as GitHub gives it: the downloaded file is checked before it is opened.
        const auto file = profile.path / ("hash-" + name);
        std::ofstream(file, std::ios::binary) << data;
        pick.choices      = {choiceOf(github, name, data, sha256File(file), "engine/mover(\\.exe)?")};
        const auto result = installEngine(profile.path, testEngine(name), pick);
        REQUIRE_MESSAGE(result.ok, result.error);
        CHECK(fs::path(result.engine.program).filename() == "mover" + EXE);
        CHECK(readFile(result.engine.program) == program);
        CHECK_FALSE(fs::exists(versionFolder(profile.path, "mover", "1.0") / name));  // the archive, removed
        std::error_code ec;
        fs::remove_all(versionFolder(profile.path, "mover", "1.0"), ec);
    }
}

TEST_CASE("engine downloads: a file that does not start, then the next one") {
    configure();
    FakeGitHub github;
    Profile profile;
    const std::string broken = "not a program\n";
    const auto good          = mover();
    const auto broken_name   = "mover-fast" + EXE;
    const auto good_name     = "mover-safe" + EXE;
    github.set("/download/" + broken_name, broken);
    github.set("/download/" + good_name, good);
    Pick pick;
    pick.choices      = {choiceOf(github, broken_name, broken, {}), choiceOf(github, good_name, good, {})};
    const auto result = installEngine(profile.path, testEngine(broken_name), pick);
    REQUIRE_MESSAGE(result.ok, result.error);
    CHECK(result.engine.asset == good_name);
    REQUIRE(result.failed.size() == 1);
    CHECK(result.failed[0].rfind(broken_name + ": ", 0) == 0);
    CHECK_FALSE(fs::exists(versionFolder(profile.path, "mover", "1.0") / broken_name));

    // None starts: nothing kept, each said.
    Pick none;
    none.choices = {choiceOf(github, broken_name, broken, {})};
    std::error_code ec;
    fs::remove_all(versionFolder(profile.path, "mover", "1.0"), ec);
    const auto failed = installEngine(profile.path, testEngine(broken_name), none);
    CHECK_FALSE(failed.ok);
    CHECK(failed.error.find("none of its files for this machine started") == 0);
    CHECK_FALSE(fs::exists(versionFolder(profile.path, "mover", "1.0")));

    // A size that differs, when GitHub gives no hash.
    Pick short_file;
    short_file.choices = {choiceOf(github, good_name, good, {})};
    short_file.choices[0].asset.size += 1;
    const auto wrong = installEngine(profile.path, testEngine(good_name), short_file);
    CHECK_FALSE(wrong.ok);
    CHECK(wrong.error.find("not of the size GitHub gives") != std::string::npos);
}

TEST_CASE("engine downloads: archives with long names, pax paths, './', and what is not one") {
    Profile profile;
    const auto archive     = profile.path / "a.tar";
    const auto out         = profile.path / "out";
    const std::string deep = "engine/" + std::string(120, 'd') + "/stockfish-linux";
    const auto take        = [&](const std::string& bytes, const std::string& inside) {
        std::ofstream(archive, std::ios::binary | std::ios::trunc) << bytes;
        return extractProgram(archive, inside, out);
    };

    for (const auto kind : {LongName::Gnu, LongName::Pax}) {
        const auto got = take(tarOf({{deep, "the program"}}, kind), "engine/d+/stockfish-linux");
        REQUIRE_MESSAGE(got.error.empty(), got.error);
        CHECK(got.program == out / "stockfish-linux");
        CHECK(readFile(got.program) == "the program");
    }
    // A path written "./engine/…", a gzip of it.
    auto got = take(gzipped(tarOf({{"./engine/sf", "gzipped"}})), "engine/sf");
    REQUIRE_MESSAGE(got.error.empty(), got.error);
    CHECK(readFile(got.program) == "gzipped");

    CHECK(take(tarOf({{"engine/other", "x"}}), "engine/sf").error == "the program is not in the archive");
    auto damaged = tarOf({{"engine/sf", "x"}});
    damaged[600] ^= 0x40;  // in the second header
    CHECK(take(damaged, "engine/sf").error == "it is not a tar archive");
    CHECK(take(zipArchive("engine/other", "x", 0, 0x21), "engine/sf").error == "the program is not in the archive");
    CHECK_FALSE(take("short", "engine/sf").error.empty());
    CHECK(take(tarOf({{"engine/sf", "x"}}), "engine/(").error.find("regular expression") != std::string::npos);
}

TEST_CASE("engine downloads: the releases kept a day, the catalogue read from the repository and kept") {
    Profile profile;
    FakeGitHub github;
    const auto base = github.url();

    // What GitHub gave, kept and read back as it was.
    std::vector<Release> list;
    list.push_back(*parseRelease(releaseJson("v1.0", {"mover-file", "mover.zip"}, base)));
    list.front().assets.front().sha256 = std::string(64, 'a');
    keepReleases(profile.path, "mover", list);
    const auto kept = keptReleases(profile.path, "mover");
    REQUIRE(kept);
    REQUIRE(kept->size() == 1);
    CHECK(kept->front().tag == "v1.0");
    CHECK(kept->front().published == "2026-11-18T00:00:00Z");
    REQUIRE(kept->front().assets.size() == 2);
    CHECK(kept->front().assets.front().name == "mover-file");
    CHECK(kept->front().assets.front().sha256 == std::string(64, 'a'));
    CHECK(kept->front().assets.front().url == base + "/download/mover-file");
    CHECK(kept->front().assets.front().uploaded);
    // Older than asked for, or never asked: nothing.
    CHECK_FALSE(keptReleases(profile.path, "mover", std::chrono::seconds(0)));
    CHECK_FALSE(keptReleases(profile.path, "stockfish"));

    // The repository's catalogue, kept a day: read again only when older.
    const std::string catalogue = R"({"format":1,"engines":[{"id":"mover","name":"Mover","repo":"test/mover",)"
                                  R"("licence":"MIT","tag":"v{ver}","files":{"linux-x64":[{"asset":"mover-file",)"
                                  R"("needs":"x86-64"}]}}]})";
    github.set("/engines.json", catalogue);
    const auto read = currentCatalogue(profile.path, true, base + "/engines.json");
    REQUIRE(read.engines.size() == 1);
    CHECK(read.engines.front().id == "mover");
    CHECK(fs::exists(enginesFolder(profile.path) / "catalogue.json"));
    const auto asked = github.requests().size();
    const auto again = currentCatalogue(profile.path, true, base + "/engines.json");
    CHECK(again.engines.size() == 1);
    CHECK(github.requests().size() == asked);  // the kept one, less than a day old

    // None kept, the repository out of reach (a private repository answers 404): the program's own copy.
    fs::remove(enginesFolder(profile.path) / "catalogue.json");
    const auto built_in = currentCatalogue(profile.path, true, base + "/missing.json");
    CHECK(built_in.engines.size() == builtInCatalogue().engines.size());
    CHECK(built_in.find("gaia"));
    // Without asking: the kept copy or the program's.
    CHECK(currentCatalogue(profile.path, false, base + "/engines.json").find("gaia"));
}
