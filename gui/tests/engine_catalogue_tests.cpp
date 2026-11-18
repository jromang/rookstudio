// The engine picker's catalogue: what it reads, and the file it picks of an engine's releases for a processor
// (data/engine_releases.json: the engines' latest releases on 2026-11-18, as GitHub's API gave them).

#include <doctest/doctest.hpp>

#include <fstream>
#include <map>
#include <string>
#include <vector>

#include <json.hpp>

#include "engine_catalogue.hpp"

using namespace fcg;
using namespace fcg::catalogue;

namespace {

CpuFeatures x86(const char* vendor, int family, int model, int level) {
    CpuFeatures c;
    c.arch   = CpuArch::X86_64;
    c.vendor = vendor;
    c.family = family;
    c.model  = model;
    c.sse3 = c.ssse3 = c.sse41 = c.cx16 = c.lahf = true;
    if (level >= 2) c.sse42 = c.popcnt = true;
    if (level >= 3) c.avx = c.avx2 = c.fma = c.f16c = c.bmi1 = c.bmi2 = c.lzcnt = c.movbe = true;
    if (level >= 4) c.avx512f = c.avx512bw = c.avx512cd = c.avx512dq = c.avx512vl = true;
    return c;
}

CpuFeatures iceLakeLike(CpuFeatures c) {
    c.avx512vnni = c.avx512vbmi = c.avx512vbmi2 = c.avx512bitalg = c.avx512vpopcntdq = c.avx512ifma = true;
    c.gfni = c.vaes = c.vpclmulqdq = true;
    return c;
}

CpuFeatures arm(bool dotprod, bool i8mm) {
    CpuFeatures c;
    c.arch    = CpuArch::Arm64;
    c.dotprod = dotprod;
    c.i8mm    = i8mm;
    return c;
}

const CpuFeatures ZEN5      = iceLakeLike(x86("AuthenticAMD", 0x1A, 0x44, 4));
const CpuFeatures ZEN2      = x86("AuthenticAMD", 0x17, 0x71, 3);
const CpuFeatures HASWELL   = x86("GenuineIntel", 6, 0x3C, 3);
const CpuFeatures SKYLAKE_X = x86("GenuineIntel", 6, 0x55, 4);
const CpuFeatures PENRYN    = [] {
    auto c  = x86("GenuineIntel", 6, 0x17, 1);  // SSE4.1, no POPCNT
    c.sse41 = true;
    return c;
}();
const CpuFeatures M1 = arm(true, false);
const CpuFeatures M2 = arm(true, true);
// An ARM64 PC whose emulator runs x86-64 programs with AVX2.
const CpuFeatures ARM_PC = [] {
    auto c         = x86("GenuineIntel", 6, 0x3C, 3);
    c.arch         = CpuArch::Arm64;
    c.x86_emulated = true;
    c.dotprod      = true;
    return c;
}();

std::map<std::string, std::vector<Release>> recorded() {
    std::ifstream in(FCG_SOURCE_DIR "/gui/tests/data/engine_releases.json", std::ios::binary);
    REQUIRE(in);
    const auto j = nlohmann::json::parse(in);
    std::map<std::string, std::vector<Release>> out;
    for (const auto& [id, list] : j.items()) out[id] = parseReleases(list.dump());
    return out;
}

Release release(std::string tag, std::string published, const std::vector<std::string>& names,
                bool prerelease = false) {
    Release r;
    r.tag        = std::move(tag);
    r.published  = std::move(published);
    r.prerelease = prerelease;
    for (const auto& n : names) r.assets.push_back({n, 1000, {}, "https://example.invalid/" + n, true});
    return r;
}

}  // namespace

TEST_CASE("engine catalogue: the built-in one, its ten engines and their files") {
    const auto& c = builtInCatalogue();
    REQUIRE(c.engines.size() == 10);
    CHECK(c.engines[0].id == "gaia");
    CHECK(c.engines[1].id == "stockfish");
    for (const auto& e : c.engines) {
        CAPTURE(e.id);
        for (const char* platform : {"linux-x64", "windows-x64", "macos-arm64"}) CHECK(e.files.count(platform) == 1);
    }
    CHECK(c.find("pawnocchio") != nullptr);
    CHECK(c.find("torch") == nullptr);
}

TEST_CASE("engine catalogue: what it does not read") {
    const auto with = [](const std::string& engine, int format = 1) {
        return R"({"format":)" + std::to_string(format) + R"(,"engines":[)" + engine + "]}";
    };
    const auto engine = [](const std::string& field, const std::string& value) {
        std::map<std::string, std::string> f = {
            {"id", R"("x")"},       {"name", R"("X")"},
            {"repo", R"("a/b")"},   {"licence", R"("MIT")"},
            {"tag", R"("v{ver}")"}, {"files", R"({"linux-x64":[{"asset":"x-{ver}","needs":"v3"}]})"}};
        f[field]        = value;
        std::string out = "{";
        for (const auto& [k, v] : f) out += (out.size() > 1 ? "," : "") + ("\"" + k + "\":" + v);
        return out + "}";
    };
    const auto why = [](const std::string& json) {
        std::string error;
        CHECK_FALSE(parseCatalogue(json, &error));
        return error;
    };
    CHECK(parseCatalogue(with(engine("id", R"("x")"))));
    CHECK(why("not json") == "the catalogue is not JSON");
    CHECK(why(with(engine("id", R"("x")"), 2)).find("newer than this program reads") != std::string::npos);
    CHECK(why(with(engine("files", R"({"linux-x64":[{"asset":"x","needs":"v5"}]})"))).find("unknown") !=
          std::string::npos);
    CHECK(why(with(engine("files", R"({"linux-x64":[{"asset":"x-(","needs":"v3"}]})"))).find("regular") !=
          std::string::npos);
    CHECK(why(with(engine("files", R"({"beos-x64":[{"asset":"x","needs":"v3"}]})"))).find("no platform") !=
          std::string::npos);
    CHECK(why(with(engine("tag", R"("v1")"))).find("{ver}") != std::string::npos);
    CHECK(why(with(engine("repo", R"("ab")"))).find("owner/name") != std::string::npos);
    CHECK(why(with(engine("id", R"("Big Name")"))).find("small letters") != std::string::npos);
    CHECK(why(with(engine("id", R"("x")") + "," + engine("id", R"("x")"))).find("two engines") != std::string::npos);
}

TEST_CASE("engine catalogue: what a processor runs") {
    CHECK(meetsNeed(ZEN5, "zen5"));
    CHECK_FALSE(meetsNeed(ZEN5, "zen4"));
    CHECK(meetsNeed(ZEN5, "v4-icl"));
    CHECK(meetsNeed(ZEN2, "zen12"));
    CHECK_FALSE(meetsNeed(ZEN2, "v3-fastpext"));
    CHECK_FALSE(meetsNeed(HASWELL, "zen12"));
    CHECK(meetsNeed(HASWELL, "v3-fastpext"));
    auto cascade_lake       = SKYLAKE_X;
    cascade_lake.avx512vnni = true;
    CHECK(meetsNeed(cascade_lake, "v4-vnni"));
    CHECK_FALSE(meetsNeed(cascade_lake, "v4-icl"));
    CHECK_FALSE(meetsNeed(SKYLAKE_X, "v4-vnni"));
    CHECK(meetsNeed(PENRYN, "ssse3"));
    CHECK_FALSE(meetsNeed(PENRYN, "sse41-popcnt"));
    CHECK(meetsNeed(M2, "arm64-i8mm"));
    CHECK_FALSE(meetsNeed(M1, "arm64-i8mm"));
    CHECK_FALSE(meetsNeed(M1, "x86-64"));
    CHECK_FALSE(meetsNeed(ZEN5, "arm64"));
    CHECK(meetsNeed(ARM_PC, "v3"));  // by emulation
    CHECK(meetsNeed(ARM_PC, "arm64-dotprod"));
    CHECK_FALSE(meetsNeed(ZEN5, "v5"));
    for (const auto need : needNames()) CHECK_FALSE(describeNeed(need).empty());
}

TEST_CASE("engine catalogue: the file each engine's latest release has for a processor") {
    const auto releases = recorded();
    const auto& c       = builtInCatalogue();
    const auto pick     = [&](const std::string& id, const CpuFeatures& cpu, const std::string& platform) {
        const auto* e = c.find(id);
        REQUIRE(e);
        return pickBuild(*e, releases.at(id), cpu, {platform});
    };
    const auto first = [&](const std::string& id, const CpuFeatures& cpu, const std::string& platform) {
        const auto p = pick(id, cpu, platform);
        return p.choices.empty() ? "none: " + p.message() : p.choices.front().asset.name;
    };

    // A Ryzen 9 9950X on Linux: the files of mockup M9.
    CHECK(first("gaia", ZEN5, "linux-x64") == "gaiachess-linux-universal");
    CHECK(first("stockfish", ZEN5, "linux-x64") == "stockfish-linux-x86-64-universal.tar.gz");
    CHECK(first("triumviratus", ZEN5, "linux-x64") == "Triumviratus_8.1_linux_avx512");
    CHECK(first("reckless", ZEN5, "linux-x64") == "reckless-linux-avx512");
    CHECK(first("plentychess", ZEN5, "linux-x64") == "PlentyChess-8.0.0-linux-avx512vbmi2");
    CHECK(first("pawnocchio", ZEN5, "linux-x64") == "pawnocchio-2.0.1-linux-x86_64_znver5");
    CHECK(first("coda", ZEN5, "linux-x64") == "coda-0.9.5-linux-x86-64-v3");
    CHECK(first("cinder", ZEN5, "linux-x64") == "cinder-v0.6.1-linux-avx512");
    CHECK(first("stormphrax", ZEN5, "linux-x64") == "stormphrax-8.0.0-avx512");
    CHECK(first("viridithas", ZEN5, "linux-x64") == "viridithas-20-linux-x86-64-v4");

    // Zen 2 runs PEXT in microcode: the builds without it.
    CHECK(first("plentychess", ZEN2, "linux-x64") == "PlentyChess-8.0.0-linux-avx2");
    CHECK(first("triumviratus", ZEN2, "linux-x64") == "Triumviratus_8.1_linux_avx2-nopext");
    CHECK(first("stormphrax", ZEN2, "linux-x64") == "stormphrax-8.0.0-zen2");
    CHECK(first("pawnocchio", ZEN2, "linux-x64") == "pawnocchio-2.0.1-linux-x86_64_znver2");

    // Skylake-X: AVX-512, without what Ice Lake added.
    CHECK(first("plentychess", SKYLAKE_X, "linux-x64") == "PlentyChess-8.0.0-linux-avx512");
    CHECK(first("reckless", SKYLAKE_X, "linux-x64") == "reckless-linux-avx2");
    CHECK(first("triumviratus", SKYLAKE_X, "linux-x64") == "Triumviratus_8.1_linux_avx512");
    CHECK(first("viridithas", SKYLAKE_X, "linux-x64") == "viridithas-20-linux-x86-64-v3");
    CHECK(first("pawnocchio", SKYLAKE_X, "linux-x64") == "pawnocchio-2.0.1-linux-x86_64_v4");

    // Haswell, on Windows.
    CHECK(first("gaia", HASWELL, "windows-x64") == "gaiachess-windows-console.exe");
    CHECK(first("stockfish", HASWELL, "windows-x64") == "stockfish-windows-x86-64-universal.zip");
    CHECK(first("triumviratus", HASWELL, "windows-x64") == "Triumviratus_8.1_windows_universal.exe");
    CHECK(first("plentychess", HASWELL, "windows-x64") == "PlentyChess-8.0.0-windows-bmi2.exe");
    CHECK(first("stormphrax", HASWELL, "windows-x64") == "stormphrax-8.0.0-avx2-bmi2.exe");
    CHECK(first("viridithas", HASWELL, "windows-x64") == "viridithas-20-win-x86-64-v3.exe");
    CHECK(first("coda", HASWELL, "windows-x64") == "coda-0.9.5-windows-x86-64-v3.exe");

    // Every engine has a file for a Mac with Apple Silicon.
    for (const auto& e : c.engines) {
        CAPTURE(e.id);
        CHECK_FALSE(pick(e.id, M1, "macos-arm64").choices.empty());
    }

    // The next files, for one that would not start: those this processor runs, in their order.
    const auto plenty = pick("plentychess", ZEN5, "linux-x64");
    std::vector<std::string> names;
    for (const auto& choice : plenty.choices) names.push_back(choice.asset.name);
    CHECK(names == std::vector<std::string>{"PlentyChess-8.0.0-linux-avx512vbmi2", "PlentyChess-8.0.0-linux-avx512",
                                            "PlentyChess-8.0.0-linux-bmi2", "PlentyChess-8.0.0-linux-avx2",
                                            "PlentyChess-8.0.0-linux-fma", "PlentyChess-8.0.0-linux-ssse3",
                                            "PlentyChess-8.0.0-linux-generic"});
    CHECK(plenty.choices.front().version == "8.0.0");
    CHECK(plenty.choices.front().tag == "b-v8.0.0");
    CHECK(plenty.choices.front().licence == "GPL-3.0");
    const auto stockfish = pick("stockfish", ZEN5, "linux-x64");
    CHECK(stockfish.choices.front().version == "19");
    CHECK(stockfish.choices.front().build.inside == "stockfish/stockfish-linux-x86-64-universal");
    const auto gaia = pick("gaia", ZEN5, "linux-x64").choices.front();
    CHECK(gaia.asset.sha256.size() == 64);
    CHECK(gaia.asset.size > 100'000'000);
    CHECK(gaia.asset.url == "https://github.com/jromang/gaiachess/releases/download/v4.4.4/gaiachess-linux-universal");
    CHECK(pick("viridithas", ZEN5, "linux-x64").choices.front().licence == "MIT");
}

TEST_CASE("engine catalogue: an old processor, and what it lacks") {
    const auto releases = recorded();
    const auto& c       = builtInCatalogue();
    const auto pick     = [&](const std::string& id, const CpuFeatures& cpu, const std::string& platform) {
        return pickBuild(*c.find(id), releases.at(id), cpu, {platform});
    };
    // A Core 2 has no AVX2: nothing of Stormphrax, of Coda for Windows, of Viridithas 20 (whose earlier versions had
    // builds for it); the safest file of the others.
    const auto storm = pick("stormphrax", PENRYN, "linux-x64");
    CHECK(storm.choices.empty());
    CHECK(storm.error == PickError::Processor);
    CHECK(storm.need == "v3");
    CHECK(storm.message() == "its files for this system need at least AVX2 (x86-64-v3)");
    CHECK(pick("coda", PENRYN, "windows-x64").need == "v3");
    const auto viridithas = pick("viridithas", PENRYN, "linux-x64");
    CHECK(viridithas.error == PickError::Processor);
    CHECK(viridithas.need == "v3");
    CHECK(pick("triumviratus", PENRYN, "linux-x64").need == "sse41-popcnt");
    CHECK(pick("plentychess", PENRYN, "linux-x64").choices.front().asset.name == "PlentyChess-8.0.0-linux-ssse3");
    CHECK(pick("gaia", PENRYN, "linux-x64").choices.front().asset.name == "gaiachess-linux-compat");
    // Nothing published for a system.
    const auto arm_linux = pick("stormphrax", M1, "linux-arm64");
    CHECK(arm_linux.error == PickError::NoPlatform);
    CHECK(arm_linux.message() == "it publishes no file for this system");
}

TEST_CASE("engine catalogue: a side release, a pre-release, files that have not come, the newest version") {
    const auto& triumviratus = *builtInCatalogue().find("triumviratus");
    const auto v81           = release("v8.1", "2026-11-07T09:20:00Z",
                                       {"Triumviratus_8.1_linux_avx2", "Triumviratus_8.1_windows_universal.exe"});
    const auto solver = release("v8.1-possolver", "2026-11-07T09:57:00Z", {"Triumviratus_8.1_PosSolver_linux_avx2"});

    // The side release, newer, is not the engine's.
    auto pick = pickBuild(triumviratus, {solver, v81}, HASWELL, {"linux-x64"});
    REQUIRE_FALSE(pick.choices.empty());
    CHECK(pick.choices.front().asset.name == "Triumviratus_8.1_linux_avx2");
    CHECK(pick.newer.empty());

    // A pre-release, a draft: left out.
    auto draft  = release("v9.1", "2026-12-02T00:00:00Z", {"Triumviratus_9.1_linux_avx2"});
    draft.draft = true;
    pick = pickBuild(triumviratus,
                     {release("v9.0", "2026-12-01T00:00:00Z", {"Triumviratus_9.0_linux_avx2"}, true), draft, v81},
                     HASWELL, {"linux-x64"});
    CHECK(pick.choices.front().version == "8.1");
    CHECK(pick.newer.empty());

    // A newer release whose Linux files have not come yet (Windows first): 8.1, and 9.0 said.
    pick = pickBuild(triumviratus,
                     {release("v9.0", "2026-12-01T00:00:00Z", {"Triumviratus_9.0_windows_universal.exe"}), v81},
                     HASWELL, {"linux-x64"});
    CHECK(pick.choices.front().version == "8.1");
    CHECK(pick.newer == "9.0");
    CHECK(pick.newer_needs.empty());

    // A newer release for better processors only: 8.1, and what 9.0 needs.
    pick = pickBuild(triumviratus, {v81, release("v9.0", "2026-12-01T00:00:00Z", {"Triumviratus_9.0_linux_avx512"})},
                     HASWELL, {"linux-x64"});
    CHECK(pick.choices.front().version == "8.1");
    CHECK(pick.newer == "9.0");
    CHECK(pick.newer_needs == "v4");

    // A file still being sent does not count.
    auto sending               = release("v9.0", "2026-12-01T00:00:00Z", {"Triumviratus_9.0_linux_avx2"});
    sending.assets[0].uploaded = false;
    CHECK(pickBuild(triumviratus, {sending, v81}, HASWELL, {"linux-x64"}).choices.front().version == "8.1");

    // Versions by their numbers, whatever the order of publication: 10 after 9.
    pick = pickBuild(triumviratus,
                     {release("v10.0", "2026-01-01T00:00:00Z", {"Triumviratus_10.0_linux_avx2"}),
                      release("v9.0", "2026-12-01T00:00:00Z", {"Triumviratus_9.0_linux_avx2"})},
                     HASWELL, {"linux-x64"});
    CHECK(pick.choices.front().version == "10.0");

    // No release of the engine; releases without any file the catalogue names.
    CHECK(pickBuild(triumviratus, {release("networks", "2026-01-01T00:00:00Z", {"net.nnue"})}, HASWELL, {"linux-x64"})
              .error == PickError::NoRelease);
    CHECK(pickBuild(triumviratus, {release("v9.0", "2026-12-01T00:00:00Z", {"triumviratus-linux"})}, HASWELL,
                    {"linux-x64"})
              .error == PickError::NoFile);
}

TEST_CASE("engine catalogue: an ARM PC, a Mac, the platforms of this system") {
    const auto releases               = recorded();
    const auto& c                     = builtInCatalogue();
    const std::vector<std::string> pc = {"windows-arm64", "windows-x64"};
    const auto first                  = [&](const std::string& id) {
        const auto p = pickBuild(*c.find(id), releases.at(id), ARM_PC, pc);
        return p.choices.empty() ? std::string("none") : p.choices.front().asset.name;
    };
    CHECK(first("stockfish") == "stockfish-windows-arm64-universal.zip");
    CHECK(first("cinder") == "cinder-v0.6.1-windows-neon.exe");
    CHECK(first("pawnocchio") == "pawnocchio-2.0.1-windows-aarch64-dotprod.exe");
    CHECK(first("gaia") == "gaiachess-windows-console.exe");  // no ARM64 build: the x86-64 one, emulated
    CHECK(first("reckless") == "reckless-windows-avx2.exe");

    CHECK(pickBuild(*c.find("pawnocchio"), releases.at("pawnocchio"), M2, {"macos-arm64"}).choices.front().asset.name ==
          "pawnocchio-2.0.1-macos-aarch64-apple_m1");

#if defined(_WIN32)
    const std::string os = "windows";
#elif defined(__APPLE__)
    const std::string os = "macos";
#else
    const std::string os = "linux";
#endif
    CHECK(enginePlatforms(ZEN5) == std::vector<std::string>{os + "-x64"});
    CHECK(enginePlatforms(M1) == std::vector<std::string>{os + "-arm64"});
    CHECK(enginePlatforms(ARM_PC) == std::vector<std::string>{os + "-arm64", os + "-x64"});
}

TEST_CASE("engine catalogue: a licence that changes with the version, GitHub's answers read") {
    const auto& viridithas = *builtInCatalogue().find("viridithas");
    CHECK(viridithas.licenceOf("20.0.0") == "MIT");
    CHECK(viridithas.licenceOf("21") == "AGPL-3.0-only");
    CHECK(viridithas.licenceOf("21.1.0") == "AGPL-3.0-only");

    const auto r = parseRelease(R"({"tag_name":"v1.0","published_at":"2026-11-18T06:38:32Z","prerelease":false,
        "assets":[
          {"name":"a","state":"uploaded","size":12,"browser_download_url":"https://x/a",
           "digest":"sha256:ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789"},
          {"name":"b","state":"open","size":12,"browser_download_url":"https://x/b","digest":null},
          {"name":"c","state":"uploaded","size":12,"browser_download_url":"https://x/c","digest":"sha256:12"},
          {"name":"d","state":"uploaded","size":12}]})");
    REQUIRE(r);
    CHECK(r->tag == "v1.0");
    REQUIRE(r->assets.size() == 3);  // d has nowhere to be downloaded from
    CHECK(r->assets[0].sha256 == "abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789");
    CHECK(r->assets[0].size == 12);
    CHECK(r->assets[0].uploaded);
    CHECK_FALSE(r->assets[1].uploaded);
    CHECK(r->assets[1].sha256.empty());
    CHECK(r->assets[2].sha256.empty());
    CHECK_FALSE(parseRelease("not json"));
    CHECK_FALSE(parseRelease(R"({"name":"no tag"})"));
    CHECK(parseReleases(R"({"tag_name":"v1.0"})").empty());
    CHECK(parseReleases(R"([{"tag_name":"v1.0"},{"tag_name":"v0.9"}])").size() == 2);
}
