// An engine's old versions (#65): each in its own folder of the profile; once an update has replaced one, deleted when
// the last tournament not over that plays it ends (a stopped one holds it too), never when kept on purpose ("Keep …
// too"), a version from before deleted on the user's word only; deleted, it is downloaded again from its release,
// its hash checked, when a tournament asks for it again. The names are invented.

#include <doctest/doctest.hpp>

#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include <fmt/include/fmt/format.h>
#include <core/config/config.hpp>

#include "engine_downloads.hpp"
#include "engine_versions.hpp"
#include "engines.hpp"
#include "free_ports.hpp"
#include "https.hpp"
#include "mock_engines.hpp"
#include "scripted_server.hpp"
#include "test_support.hpp"

using namespace fcg;
using namespace fcg::catalogue;
using namespace fcg::test;
namespace fs = std::filesystem;

namespace {

struct Profile {
    Profile() : path(fs::temp_directory_path() / "engine-versions") {
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

// A version of "sample" installed in the profile: its folder, its program of `bytes` bytes, its entry.
Installed install(const fs::path& profile, const std::string& version, std::size_t bytes, int64_t when) {
    const auto folder = versionFolder(profile, "sample", version);
    fs::create_directories(folder);
    const auto program = folder / "sample-engine";
    std::ofstream(program, std::ios::binary) << std::string(bytes, 'x');
    Installed i;
    i.id        = "sample";
    i.version   = version;
    i.tag       = "v" + version;
    i.asset     = "sample-engine";
    i.program   = program.string();
    i.installed = when;
    return i;
}

EngineEntry engineOf(const std::string& name, const Installed& i, bool from_catalogue) {
    EngineEntry e;
    e.name = name;
    e.cmd  = i.program;
    if (from_catalogue) e.catalogue = "sample";
    return e;
}

ProgramUser tournament(const std::string& name, const Installed& i, bool playing, bool paused = false) {
    ProgramUser u;
    u.name     = name;
    u.dir      = fs::path("/tournaments") / name;
    u.programs = {i.program, "/elsewhere/other-engine"};
    u.playing  = playing;
    u.paused   = paused;
    return u;
}

VersionState stateOf(const fs::path& profile, const std::vector<Installed>& installed, const std::string& version,
                     const std::vector<EngineEntry>& library, const std::vector<ProgramUser>& tournaments) {
    for (const auto& v : diskVersions(profile, installed, "sample", library, tournaments))
        if (v.installed.version == version) return v.state;
    FAIL("no such version: " << version);
    return VersionState::Unused;
}

}  // namespace

TEST_CASE("engine versions: an old version deleted after the last tournament that plays it ends") {
    Profile profile;
    std::vector<Installed> installed{install(profile.path, "4.4.3", 1000, 100),
                                     install(profile.path, "4.4.4", 2000, 200)};
    // The library's engine took 4.4.4; the update retired 4.4.3.
    const std::vector<EngineEntry> library{engineOf("Sample", installed[1], true)};
    retireVersion(installed, "sample", "4.4.3", 300);
    // Two tournaments not over play it: one playing, one stopped.
    const auto night   = tournament("Night Sample", installed[0], true);
    const auto stopped = tournament("Old Gauntlet", installed[0], false);
    auto versions      = diskVersions(profile.path, installed, "sample", library, {night, stopped});
    REQUIRE(versions.size() == 2);
    CHECK(versions[0].installed.version == "4.4.4");  // the newest first
    CHECK(versions[0].state == VersionState::Current);
    CHECK(versions[1].state == VersionState::Held);
    CHECK(versions[1].tournaments.size() == 2);
    CHECK(versions[1].bytes == 1000);
    CHECK(deleteRetired(profile.path, installed, library, {night, stopped}, 400).empty());
    CHECK(fs::exists(installed[0].program));
    // The first one ends: the stopped one still holds it, and is noted as its holder.
    CHECK(deleteRetired(profile.path, installed, library, {stopped}, 500).empty());
    CHECK(fs::exists(installed[0].program));
    CHECK(installed[0].held_by == "Old Gauntlet");
    // The last one ends: deleted, its entry kept with when and what it freed.
    const auto deleted = deleteRetired(profile.path, installed, library, {}, 600);
    REQUIRE(deleted.size() == 1);
    CHECK(deleted[0].version == "4.4.3");
    CHECK(deleted[0].freed == 1000);
    CHECK(deleted[0].error.empty());
    CHECK(deleted[0].held_by == "Old Gauntlet");  // the notice names it: its end freed the version
    CHECK_FALSE(fs::exists(versionFolder(profile.path, "sample", "4.4.3")));
    CHECK(fs::exists(installed[1].program));
    CHECK(installed[0].deleted == 600);
    CHECK(installed[0].freed == 1000);
    // Gone: no longer listed, and not deleted twice.
    CHECK(diskVersions(profile.path, installed, "sample", library, {}).size() == 1);
    CHECK(deleteRetired(profile.path, installed, library, {}, 700).empty());
}

TEST_CASE("engine versions: kept while a tournament is not over; Keep … too never deleted; from before, kept") {
    Profile profile;
    std::vector<Installed> installed{install(profile.path, "4.4.2", 10, 50), install(profile.path, "4.4.3", 10, 100),
                                     install(profile.path, "4.4.4", 10, 200)};
    retireVersion(installed, "sample", "4.4.2", 150);
    retireVersion(installed, "sample", "4.4.3", 250);
    // Paused, or stopped: not over, so 4.4.3 is held.
    const auto paused = tournament("Paused Match", installed[1], false, true);
    // 4.4.2 kept on purpose: an engine of the library of its own plays it.
    std::vector<EngineEntry> library{engineOf("Sample", installed[2], true),
                                     engineOf("Sample 4.4.2", installed[0], false)};
    CHECK(stateOf(profile.path, installed, "4.4.2", library, {paused}) == VersionState::Kept);
    CHECK(stateOf(profile.path, installed, "4.4.3", library, {paused}) == VersionState::Held);
    CHECK(deleteRetired(profile.path, installed, library, {}, 300).size() == 1);  // 4.4.3 only
    CHECK(fs::exists(installed[0].program));
    CHECK_FALSE(fs::exists(installed[1].program));
    // Stop keeping it: the engine of its own leaves the library, and the version, retired, goes.
    library.pop_back();
    CHECK(stateOf(profile.path, installed, "4.4.2", library, {}) == VersionState::Retired);
    CHECK(deleteRetired(profile.path, installed, library, {}, 400).size() == 1);
    CHECK_FALSE(fs::exists(installed[0].program));

    // A version from before, never retired by an update: played by nothing, it stays until the user deletes it.
    std::vector<Installed> before{install(profile.path, "4.4.0", 10, 10), install(profile.path, "4.4.1", 10, 20)};
    const std::vector<EngineEntry> one{engineOf("Sample", before[1], true)};
    CHECK(stateOf(profile.path, before, "4.4.0", one, {}) == VersionState::Unused);
    CHECK(deleteRetired(profile.path, before, one, {}, 500).empty());
    CHECK(fs::exists(before[0].program));
    // Kept on purpose again (Keep … too on the card): no longer retired.
    std::vector<Installed> again{install(profile.path, "4.4.5", 10, 30)};
    retireVersion(again, "sample", "4.4.5", 40);
    reviveVersion(again, "sample", "4.4.5");
    CHECK(again[0].retired == 0);
}

TEST_CASE("engine versions: a deleted version a tournament asks for again, downloaded again, its hash checked") {
    if (!fastchess::config::TournamentConfig) {
        fastchess::config::TournamentConfig               = std::make_unique<fastchess::config::Tournament>();
        fastchess::config::TournamentConfig->startup_time = ENGINE_STARTUP_TIME;
    }
    Profile profile;
    const auto mover_path = randomMover();
    const auto program    = readFile(mover_path);
    REQUIRE_FALSE(program.empty());
#ifdef _WIN32
    const std::string name = "sample-engine.exe";
#else
    const std::string name = "sample-engine";
#endif
    // GitHub, on this machine: the release's file at the address GitHub gives its files.
    const auto port = freePort();
    std::mutex mutex;
    std::vector<std::string> asked;
    ScriptedServer github(port, [&](const std::string& head, net::TcpConnection& c) {
        const auto path = requestPath(head);
        {
            std::lock_guard lock(mutex);
            asked.push_back(path);
        }
        const bool ours = path == "/test/sample/releases/download/v4.4.3/" + name;
        const auto body = ours ? program : std::string("Not Found");
        c.sendAll(fmt::format("HTTP/1.1 {} X\r\nContent-Length: {}\r\nConnection: close\r\n\r\n", ours ? 200 : 404,
                              body.size()) +
                  body);
    });
    REQUIRE(github.ok());
    const auto cpu = detectCpu();
    Engine engine;
    engine.id                                  = "sample";
    engine.name                                = "Sample";
    engine.repo                                = "test/sample";
    engine.licence                             = "MIT";
    engine.tag                                 = "v{ver}";
    engine.files[enginePlatforms(cpu).front()] = {{"sample-engine(\\.exe)?", "x86-64", {}}};

    // 4.4.3 was deleted; a stopped tournament (resumed from a folder the check did not see) asks for it again.
    Installed old;
    old.id        = "sample";
    old.version   = "4.4.3";
    old.tag       = "v4.4.3";
    old.asset     = name;
    old.sha256    = sha256File(mover_path);
    old.program   = (versionFolder(profile.path, "sample", "4.4.3") / name).string();
    old.installed = 100;
    old.retired   = 200;
    old.deleted   = 300;
    old.freed     = program.size();
    std::vector<Installed> installed{old};
    const auto asking = tournament("Old Gauntlet", old, false);
    auto versions     = diskVersions(profile.path, installed, "sample", {}, {asking});
    REQUIRE(versions.size() == 1);
    CHECK(versions[0].state == VersionState::Asked);
    CHECK(diskVersions(profile.path, installed, "sample", {}, {}).empty());  // nobody asks: not listed

    const auto pick = pickAgain(engine, old, fmt::format("http://127.0.0.1:{}", port));
    REQUIRE(pick);
    REQUIRE(pick->choices.size() == 1);
    CHECK(pick->choices[0].asset.url ==
          fmt::format("http://127.0.0.1:{}/test/sample/releases/download/v4.4.3/{}", port, name));
    CHECK(pick->choices[0].asset.sha256 == old.sha256);
    REQUIRE(saveInstalled(profile.path, installed));
    const auto result = installEngine(profile.path, engine, *pick);
    REQUIRE_MESSAGE(result.ok, result.error);
    // At the same place: the tournament's program is there again, its entry no longer deleted.
    CHECK(result.engine.program == old.program);
    CHECK(fs::exists(old.program));
    const auto list = loadInstalled(profile.path);
    REQUIRE(list.size() == 1);
    CHECK(list[0].deleted == 0);
    CHECK(list[0].retired == 0);

    // A file that is not the one of its hash: refused, nothing kept.
    fs::remove_all(versionFolder(profile.path, "sample", "4.4.3"));
    auto wrong                    = *pick;
    wrong.choices[0].asset.sha256 = std::string(64, '0');
    const auto refused            = installEngine(profile.path, engine, wrong);
    CHECK_FALSE(refused.ok);
    CHECK_FALSE(fs::exists(old.program));
    // A file the catalogue no longer names: no pick.
    auto renamed  = old;
    renamed.asset = "renamed.tar.gz";
    CHECK_FALSE(pickAgain(engine, renamed));
}
