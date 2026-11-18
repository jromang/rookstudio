#pragma once

// The engines the engine picker offers and how their GitHub releases name their files (gui/assets/engines.json,
// docs/design/engine-picker.md): which release of an engine is its latest, and which of its files this machine runs,
// the fastest first. Nothing here touches the network (engine_downloads.hpp does).

#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cpu_features.hpp"

namespace fcg::catalogue {

// One of an engine's files for a platform.
struct BuildFile {
    std::string asset;   // a regular expression of the whole file name, {ver} standing for a version
    std::string needs;   // what the processor must run: one of needNames()
    std::string inside;  // the file is an archive (.tar.gz, .tar, .zip): the regular expression of its program's path
};

struct Engine {
    std::string id;       // "stockfish": its folder under the profile's engines
    std::string name;     // "Stockfish"
    std::string repo;     // "official-stockfish/Stockfish"
    std::string licence;  // SPDX
    // (version, licence): the licence from that version on, the versions in increasing order.
    std::vector<std::pair<std::string, std::string>> licence_from;
    std::string tag;  // a regular expression of its releases' tags, {ver} the version they give
    // By platform (platformNames()), the fastest file first.
    std::map<std::string, std::vector<BuildFile>, std::less<>> files;

    // The licence of `version`.
    [[nodiscard]] std::string licenceOf(std::string_view version) const;
};

struct Catalogue {
    int format = 0;
    std::vector<Engine> engines;

    [[nodiscard]] const Engine* find(std::string_view id) const;
};

// The catalogue format this program reads.
inline constexpr int CATALOGUE_FORMAT = 1;

// The catalogue in `json`; nullopt, with why in `error`, when it is not one this program reads: a field missing, an
// unknown platform or need, a pattern that is not a regular expression, a newer format.
[[nodiscard]] std::optional<Catalogue> parseCatalogue(std::string_view json, std::string* error = nullptr);
// The copy built into the program.
[[nodiscard]] const Catalogue& builtInCatalogue();

// A pattern of the catalogue as an ECMAScript regular expression: {ver} standing for a version.
[[nodiscard]] std::string regexOf(std::string_view pattern);

// The platforms of the catalogue: "linux-x64", "linux-arm64", "windows-x64", "windows-arm64", "macos-x64",
// "macos-arm64".
[[nodiscard]] std::span<const std::string_view> platformNames();
// The platforms whose files run on this system and processor, the best first: "windows-arm64" then "windows-x64"
// (by emulation) on an ARM PC.
[[nodiscard]] std::vector<std::string> enginePlatforms(const CpuFeatures& cpu);

// What a file may need, from the safest to the most demanding: "x86-64", "ssse3", "sse41-popcnt", "v2", "fma",
// "v3", "v3-fastpext", "zen1"…"zen5", "zen12", "v4", "v4-vnni", "v4-icl", "arm64", "arm64-dotprod", "arm64-i8mm".
[[nodiscard]] std::span<const std::string_view> needNames();
// The processor runs what `need` names; false for a name it does not know.
[[nodiscard]] bool meetsNeed(const CpuFeatures& cpu, std::string_view need);
// `need` in words: "AVX2 (x86-64-v3)".
[[nodiscard]] std::string describeNeed(std::string_view need);

struct ReleaseAsset {
    std::string name;
    uint64_t size = 0;
    std::string sha256;     // hexadecimal, empty when GitHub gives none
    std::string url;        // where it is downloaded from (GitHub's browser_download_url)
    bool uploaded = false;  // whole (GitHub's state "uploaded"); one still being sent is left out
};

struct Release {
    std::string tag;
    std::string published;  // "2026-11-18T06:38:32Z"
    bool draft      = false;
    bool prerelease = false;
    std::vector<ReleaseAsset> assets;
};

// GitHub's answer for one release (releases/latest); nullopt when it is not one.
[[nodiscard]] std::optional<Release> parseRelease(std::string_view json);
// GitHub's answer for a list of releases (releases?per_page=20); empty when it is not one.
[[nodiscard]] std::vector<Release> parseReleases(std::string_view json);

// A file of a release that this machine runs.
struct Choice {
    std::string version;   // "19", from the tag
    std::string tag;       // "sf_19"
    std::string platform;  // "linux-x64"
    BuildFile build;
    ReleaseAsset asset;
    std::string licence;
};

enum class PickError {
    None,
    NoPlatform,  // the engine publishes nothing for this system
    Processor,   // its files for this system need more than this processor runs (`need`)
    NoRelease,   // none of its releases is one of the engine's (no tag matches)
    NoFile,      // its releases have none of the files the catalogue names: their names may have changed
};

struct Pick {
    // The files this machine runs of the newest release that has one, the best first: the next ones are for when a
    // file does not start.
    std::vector<Choice> choices;
    std::string newer;  // a newer release's version that has no file for this machine
    // What the least of its files needs, when it has files for this system but none this processor runs; empty when
    // it has none for this system yet (they may come later).
    std::string newer_needs;
    PickError error = PickError::None;
    std::string need;                           // Processor: the least its files need
    [[nodiscard]] std::string message() const;  // why there is no choice, in English
};

// What to download of `engine` from its `releases` (any order; drafts, pre-releases and tags that are not the
// engine's left out), for this machine.
[[nodiscard]] Pick pickBuild(const Engine& engine, const std::vector<Release>& releases, const CpuFeatures& cpu);
// The same for the platforms given, the best first (what the tests use for other machines).
[[nodiscard]] Pick pickBuild(const Engine& engine, const std::vector<Release>& releases, const CpuFeatures& cpu,
                             const std::vector<std::string>& platforms);

}  // namespace fcg::catalogue
