#include "engine_catalogue.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <regex>
#include <set>

#include <fmt/include/fmt/format.h>
#include <json.hpp>

#include "update_check.hpp"

namespace fcg::assets {
extern const unsigned char engine_catalogue[];
extern const std::size_t engine_catalogue_size;
}  // namespace fcg::assets

namespace fcg::catalogue {

namespace {

constexpr std::array<std::string_view, 6> PLATFORMS = {"linux-x64",     "linux-arm64", "windows-x64",
                                                       "windows-arm64", "macos-x64",   "macos-arm64"};
constexpr std::array<std::string_view, 19> NEEDS    = {
    "x86-64", "ssse3", "sse41-popcnt", "v2", "fma",     "v3",     "v3-fastpext", "zen1",          "zen2",      "zen3",
    "zen4",   "zen5",  "zen12",        "v4", "v4-vnni", "v4-icl", "arm64",       "arm64-dotprod", "arm64-i8mm"};

constexpr std::string_view VERSION = "[0-9]+(?:\\.[0-9]+)*";

// `pattern` with {ver} standing for a version: in a tag, the group that gives it.
std::string expand(std::string_view pattern, bool capture) {
    const std::string ver = capture ? fmt::format("({})", VERSION) : fmt::format("(?:{})", VERSION);
    std::string out;
    for (std::size_t i = 0; i < pattern.size();) {
        if (pattern.compare(i, 5, "{ver}") == 0) {
            out += ver;
            i += 5;
        } else {
            out += pattern[i++];
        }
    }
    return out;
}

std::optional<std::regex> compile(const std::string& pattern) {
    try {
        return std::regex(pattern, std::regex::ECMAScript);
    } catch (const std::regex_error&) {
        return std::nullopt;
    }
}

std::string text(const nlohmann::json& j, const char* key) {
    const auto it = j.find(key);
    return it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
}

bool flag(const nlohmann::json& j, const char* key) {
    const auto it = j.find(key);
    return it != j.end() && it->is_boolean() && it->get<bool>();
}

uint64_t number(const nlohmann::json& j, const char* key) {
    const auto it = j.find(key);
    return it != j.end() && it->is_number_unsigned() ? it->get<uint64_t>() : 0;
}

bool known(std::span<const std::string_view> names, std::string_view name) {
    return std::find(names.begin(), names.end(), name) != names.end();
}

// The level a need stands on, to say what a processor lacks: AVX2 for the files that also want a fast PEXT or a
// Zen of AMD's.
std::string_view baseNeed(std::string_view need) {
    if (need == "v3-fastpext" || need == "zen1" || need == "zen2" || need == "zen3" || need == "zen12") return "v3";
    if (need == "zen4" || need == "zen5") return "v4";
    return need;
}

std::size_t rank(std::string_view need) {
    return static_cast<std::size_t>(std::find(NEEDS.begin(), NEEDS.end(), need) - NEEDS.begin());
}

// Letters, digits and the few signs GitHub allows in an owner or a repository's name.
bool repoName(std::string_view s) {
    const auto slash = s.find('/');
    if (slash == std::string_view::npos || slash == 0 || slash + 1 == s.size()) return false;
    return std::all_of(
               s.begin(), s.end(),
               [](unsigned char c) { return std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '/'; }) &&
           std::count(s.begin(), s.end(), '/') == 1 && s.find("..") == std::string_view::npos;
}

// A folder's name on every system.
bool idName(std::string_view s) {
    return !s.empty() && s.size() <= 40 && std::all_of(s.begin(), s.end(), [](unsigned char c) {
        return std::islower(c) || std::isdigit(c) || c == '-';
    });
}

std::optional<Release> releaseOf(const nlohmann::json& r) {
    if (!r.is_object()) return std::nullopt;
    Release out;
    out.tag = text(r, "tag_name");
    if (out.tag.empty()) return std::nullopt;
    out.published  = text(r, "published_at");
    out.draft      = flag(r, "draft");
    out.prerelease = flag(r, "prerelease");
    if (const auto assets = r.find("assets"); assets != r.end() && assets->is_array()) {
        for (const auto& a : *assets) {
            if (!a.is_object()) continue;
            ReleaseAsset asset;
            asset.name     = text(a, "name");
            asset.size     = number(a, "size");
            asset.url      = text(a, "browser_download_url");
            asset.uploaded = text(a, "state") == "uploaded";
            // "sha256:<64 hexadecimal digits>", or null for files sent before GitHub kept them (June 2025).
            const auto digest = text(a, "digest");
            if (digest.size() == 7 + 64 && digest.rfind("sha256:", 0) == 0) {
                auto hex = digest.substr(7);
                std::transform(hex.begin(), hex.end(), hex.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (std::all_of(hex.begin(), hex.end(), [](unsigned char c) { return std::isxdigit(c); }))
                    asset.sha256 = hex;
            }
            if (!asset.name.empty() && !asset.url.empty()) out.assets.push_back(std::move(asset));
        }
    }
    return out;
}

}  // namespace

std::string Engine::licenceOf(std::string_view version) const {
    std::string out = licence;
    for (const auto& [from, l] : licence_from)
        if (update::compareVersions(version, from) >= 0) out = l;
    return out;
}

const Engine* Catalogue::find(std::string_view id) const {
    for (const auto& e : engines)
        if (e.id == id) return &e;
    return nullptr;
}

std::optional<Catalogue> parseCatalogue(std::string_view json, std::string* error) {
    const auto fail = [&](std::string why) -> std::optional<Catalogue> {
        if (error) *error = std::move(why);
        return std::nullopt;
    };
    const auto j = nlohmann::json::parse(json, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return fail("the catalogue is not JSON");
    Catalogue c;
    c.format = static_cast<int>(number(j, "format"));
    if (c.format < 1) return fail("the catalogue has no format");
    if (c.format > CATALOGUE_FORMAT)
        return fail(fmt::format("the catalogue's format, {}, is newer than this program reads ({})", c.format,
                                CATALOGUE_FORMAT));
    const auto engines = j.find("engines");
    if (engines == j.end() || !engines->is_array() || engines->empty()) return fail("the catalogue has no engine");
    std::set<std::string, std::less<>> ids;
    for (const auto& e : *engines) {
        if (!e.is_object()) return fail("an engine of the catalogue is not an object");
        Engine x;
        x.id         = text(e, "id");
        x.name       = text(e, "name");
        x.repo       = text(e, "repo");
        x.licence    = text(e, "licence");
        x.tag        = text(e, "tag");
        const auto n = x.id.empty() ? std::string("an engine") : x.id;
        if (!idName(x.id)) return fail(n + ": its id must be small letters, digits and '-'");
        if (!ids.insert(x.id).second) return fail(n + ": two engines have this id");
        if (x.name.empty() || x.licence.empty()) return fail(n + ": its name and licence are needed");
        if (!repoName(x.repo)) return fail(n + ": its repo must be owner/name");
        if (x.tag.find("{ver}") == std::string::npos || !compile(expand(x.tag, true)))
            return fail(n + ": its tag must be a regular expression with {ver}");
        if (const auto from = e.find("licence_from"); from != e.end()) {
            if (!from->is_object()) return fail(n + ": licence_from must map versions to licences");
            for (const auto& [version, l] : from->items()) {
                if (!l.is_string() || l.get<std::string>().empty())
                    return fail(n + ": licence_from must map versions to licences");
                x.licence_from.emplace_back(version, l.get<std::string>());
            }
            std::sort(x.licence_from.begin(), x.licence_from.end(),
                      [](const auto& a, const auto& b) { return update::compareVersions(a.first, b.first) < 0; });
        }
        const auto files = e.find("files");
        if (files == e.end() || !files->is_object() || files->empty()) return fail(n + ": it has no file");
        for (const auto& [platform, list] : files->items()) {
            if (!known(PLATFORMS, platform)) return fail(n + ": no platform is called " + platform);
            if (!list.is_array() || list.empty()) return fail(n + ": no file for " + platform);
            auto& builds = x.files[platform];
            for (const auto& f : list) {
                if (!f.is_object()) return fail(n + ": a file for " + platform + " is not an object");
                BuildFile b;
                b.asset  = text(f, "asset");
                b.needs  = text(f, "needs");
                b.inside = text(f, "inside");
                if (b.asset.empty() || !compile(expand(b.asset, false)))
                    return fail(n + ": a file for " + platform + " without a regular expression for its name");
                if (!known(NEEDS, b.needs)) return fail(n + ": " + b.asset + " needs '" + b.needs + "', unknown");
                if (!b.inside.empty() && !compile(expand(b.inside, false)))
                    return fail(n + ": " + b.asset + ": 'inside' is not a regular expression");
                builds.push_back(std::move(b));
            }
        }
        c.engines.push_back(std::move(x));
    }
    return c;
}

const Catalogue& builtInCatalogue() {
    static const Catalogue catalogue = [] {
        const std::string_view json(reinterpret_cast<const char*>(assets::engine_catalogue),
                                    assets::engine_catalogue_size);
        return parseCatalogue(json).value_or(Catalogue{});
    }();
    return catalogue;
}

std::string regexOf(std::string_view pattern) { return expand(pattern, false); }

std::span<const std::string_view> needNames() { return NEEDS; }

std::vector<std::string> enginePlatforms(const CpuFeatures& cpu) {
#if defined(_WIN32)
    const std::string os = "windows";
#elif defined(__APPLE__)
    const std::string os = "macos";
#elif defined(__linux__)
    const std::string os = "linux";
#else
    const std::string os;
#endif
    std::vector<std::string> out;
    if (os.empty()) return out;
    if (cpu.arch == CpuArch::Arm64) {
        out.push_back(os + "-arm64");
        if (cpu.x86_emulated) out.push_back(os + "-x64");
    } else if (cpu.arch == CpuArch::X86_64) {
        out.push_back(os + "-x64");
    }
    return out;
}

bool meetsNeed(const CpuFeatures& cpu, std::string_view need) {
    const int level = cpu.x86Level();
    const bool arm  = cpu.arch == CpuArch::Arm64;
    if (need == "x86-64") return level >= 1;
    if (need == "ssse3") return level >= 1 && cpu.ssse3;
    if (need == "sse41-popcnt") return level >= 1 && cpu.sse41 && cpu.popcnt;
    if (need == "v2") return level >= 2;
    if (need == "fma") return level >= 1 && cpu.avx && cpu.fma && cpu.ssse3;
    if (need == "v3") return level >= 3;
    if (need == "v3-fastpext") return level >= 3 && cpu.fastPext();
    if (need == "zen12") return level >= 3 && (cpu.zen() == 1 || cpu.zen() == 2);
    if (need.size() == 4 && need.rfind("zen", 0) == 0 && need[3] >= '1' && need[3] <= '5') {
        const int zen = need[3] - '0';
        return cpu.zen() == zen && level >= (zen >= 4 ? 4 : 3);
    }
    if (need == "v4") return level >= 4;
    if (need == "v4-vnni") return level >= 4 && cpu.avx512vnni;
    if (need == "v4-icl")
        return level >= 4 && cpu.avx512vnni && cpu.avx512vbmi && cpu.avx512vbmi2 && cpu.avx512bitalg &&
               cpu.avx512vpopcntdq && cpu.avx512ifma && cpu.gfni && cpu.vaes && cpu.vpclmulqdq;
    if (need == "arm64") return arm;
    if (need == "arm64-dotprod") return arm && cpu.dotprod;
    if (need == "arm64-i8mm") return arm && cpu.dotprod && cpu.i8mm;
    return false;
}

std::string describeNeed(std::string_view need) {
    if (need == "x86-64") return "an x86-64 processor";
    if (need == "ssse3") return "SSSE3";
    if (need == "sse41-popcnt") return "SSE4.1 and POPCNT";
    if (need == "v2") return "SSE4.2 and POPCNT (x86-64-v2)";
    if (need == "fma") return "AVX and FMA";
    if (need == "v3") return "AVX2 (x86-64-v3)";
    if (need == "v3-fastpext") return "AVX2 and a fast PEXT (not an AMD processor before Zen 3)";
    if (need == "zen12") return "an AMD Zen or Zen 2 processor";
    if (need.size() == 4 && need.rfind("zen", 0) == 0) return fmt::format("an AMD Zen {} processor", need[3]);
    if (need == "v4") return "AVX-512 (x86-64-v4)";
    if (need == "v4-vnni") return "AVX-512 with VNNI";
    if (need == "v4-icl") return "AVX-512 with VNNI and VBMI2 (Ice Lake, Zen 4 and later)";
    if (need == "arm64") return "an ARM64 processor";
    if (need == "arm64-dotprod") return "ARM64 with DotProd";
    if (need == "arm64-i8mm") return "ARM64 with DotProd and I8MM";
    return std::string(need);
}

std::optional<Release> parseRelease(std::string_view json) {
    const auto j = nlohmann::json::parse(json, nullptr, false);
    if (j.is_discarded()) return std::nullopt;
    return releaseOf(j);
}

std::vector<Release> parseReleases(std::string_view json) {
    std::vector<Release> out;
    const auto j = nlohmann::json::parse(json, nullptr, false);
    if (j.is_discarded() || !j.is_array()) return out;
    for (const auto& r : j)
        if (auto release = releaseOf(r)) out.push_back(std::move(*release));
    return out;
}

std::string Pick::message() const {
    switch (error) {
        case PickError::None:
            return {};
        case PickError::NoPlatform:
            return "it publishes no file for this system";
        case PickError::Processor:
            return "its files for this system need at least " + describeNeed(need);
        case PickError::NoRelease:
            return "none of its releases on GitHub is one of the engine's";
        case PickError::NoFile:
            return "its releases have none of the files the catalogue names (their names may have changed)";
    }
    return {};
}

Pick pickBuild(const Engine& engine, const std::vector<Release>& releases, const CpuFeatures& cpu) {
    return pickBuild(engine, releases, cpu, enginePlatforms(cpu));
}

Pick pickBuild(const Engine& engine, const std::vector<Release>& releases, const CpuFeatures& cpu,
               const std::vector<std::string>& platforms) {
    Pick out;
    struct Candidate {
        std::string platform;
        const BuildFile* build;
        std::regex name;
    };
    // The files for this machine's platforms, the best first: all of them, and those this processor runs.
    std::vector<Candidate> files, candidates;
    for (const auto& platform : platforms) {
        const auto it = engine.files.find(platform);
        if (it == engine.files.end()) continue;
        for (const auto& b : it->second) {
            auto re = compile(expand(b.asset, false));
            if (!re) continue;
            if (meetsNeed(cpu, b.needs)) candidates.push_back({platform, &b, *re});
            files.push_back({platform, &b, std::move(*re)});
        }
    }
    // What the least demanding of some files needs, said as the level it stands on.
    const auto least = [](const std::vector<Candidate>& some) {
        std::string_view best;
        for (const auto& c : some) {
            const auto need = baseNeed(c.build->needs);
            if (best.empty() || rank(need) < rank(best)) best = need;
        }
        return std::string(best);
    };
    if (files.empty()) {
        out.error = PickError::NoPlatform;
        return out;
    }
    if (candidates.empty()) {
        out.error = PickError::Processor;
        out.need  = least(files);
        return out;
    }

    // The engine's releases, the newest version first.
    struct Tagged {
        const Release* release;
        std::string version;
    };
    std::vector<Tagged> tagged;
    if (const auto tag = compile(expand(engine.tag, true))) {
        for (const auto& r : releases) {
            if (r.draft || r.prerelease) continue;
            std::smatch m;
            if (std::regex_match(r.tag, m, *tag) && m.size() > 1) tagged.push_back({&r, m[1].str()});
        }
    }
    std::stable_sort(tagged.begin(), tagged.end(), [](const Tagged& a, const Tagged& b) {
        const int c = update::compareVersions(a.version, b.version);
        return c != 0 ? c > 0 : a.release->published > b.release->published;
    });
    if (tagged.empty()) {
        out.error = PickError::NoRelease;
        return out;
    }
    // The files of the platforms a release has, whatever they need.
    const auto published = [&](const Release& r) {
        std::vector<Candidate> there;
        for (const auto& c : files)
            if (std::any_of(r.assets.begin(), r.assets.end(),
                            [&](const ReleaseAsset& a) { return a.uploaded && std::regex_match(a.name, c.name); }))
                there.push_back(c);
        return there;
    };
    for (const auto& t : tagged) {
        for (const auto& c : candidates) {
            for (const auto& a : t.release->assets) {
                if (!a.uploaded || !std::regex_match(a.name, c.name)) continue;
                const bool again = std::any_of(out.choices.begin(), out.choices.end(),
                                               [&](const Choice& seen) { return seen.asset.name == a.name; });
                if (!again)
                    out.choices.push_back(
                        Choice{t.version, t.release->tag, c.platform, *c.build, a, engine.licenceOf(t.version)});
                break;
            }
        }
        if (!out.choices.empty()) {
            if (t.release != tagged.front().release) {
                out.newer = tagged.front().version;
                // Its files are for more than this processor runs, or have not come yet.
                out.newer_needs = least(published(*tagged.front().release));
            }
            return out;
        }
    }
    // Why the newest has none: its files are for more than this processor runs, or none is one the catalogue names.
    if (const auto there = published(*tagged.front().release); !there.empty()) {
        out.error = PickError::Processor;
        out.need  = least(there);
    } else {
        out.error = PickError::NoFile;
    }
    return out;
}

}  // namespace fcg::catalogue
