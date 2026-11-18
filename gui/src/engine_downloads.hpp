#pragma once

// The engine picker's network and files: an engine's releases asked of GitHub, then the file chosen for this machine
// downloaded into the profile, checked against its published hash, taken out of its archive, made executable and
// started (uci, uciok) before it counts as installed. Each version in a folder of its own, <profile>/engines/<id>/
// <version>, so that a tournament keeps the files it started with. Nothing is asked nor downloaded but when a call
// here is made, at the user's request.

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <json.hpp>

#include "engine_catalogue.hpp"
#include "engines.hpp"
#include "https.hpp"

namespace fcg::catalogue {

inline constexpr std::string_view GITHUB_API = "https://api.github.com";

struct Releases {
    std::vector<Release> list;  // what GitHub answered
    std::string error;          // why there is no answer
    bool limited = false;       // GitHub refused: its limit of requests without an account (60 an hour) is reached
};

// The engine's releases: its latest; when that one is not the engine's or has no file for this machine, its 20
// latest (a second request). `api`: GitHub's API, a test's server otherwise. Blocks for up to 40 seconds.
[[nodiscard]] Releases fetchReleases(const Engine& engine, const CpuFeatures& cpu, std::string_view api = GITHUB_API);

// An engine installed from the catalogue, as <profile>/engines/installed.json keeps it.
struct Installed {
    std::string id;
    std::string version;
    std::string tag;
    std::string asset;    // the file downloaded
    std::string sha256;   // its hash, as GitHub published it (empty when it gave none)
    std::string licence;  // SPDX
    std::string program;  // the executable, its absolute path
    std::string id_name;  // what it answered to uci
    std::string id_author;
    int64_t installed = 0;  // when, seconds since 1970
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Installed, id, version, tag, asset, sha256, licence, program, id_name,
                                                id_author, installed)

// <profile>/engines, and the folder of one version: <profile>/engines/<id>/<version>.
[[nodiscard]] std::filesystem::path enginesFolder(const std::filesystem::path& profile);
[[nodiscard]] std::filesystem::path versionFolder(const std::filesystem::path& profile, std::string_view id,
                                                  std::string_view version);

// The engines installed from the catalogue; none when the list cannot be read.
[[nodiscard]] std::vector<Installed> loadInstalled(const std::filesystem::path& profile);
// Writes the list, safely; false, with why in `error`, when it cannot.
bool saveInstalled(const std::filesystem::path& profile, const std::vector<Installed>& installed,
                   std::string* error = nullptr);

struct Extracted {
    std::filesystem::path program;  // where it was written
    std::string error;              // why not, empty when it was
};
// The program of an archive (a .tar.gz, a .tar or a .zip, told by its first bytes): the file whose path in it matches
// `inside` (a regular expression of the whole path), written into `folder` under its own name.
[[nodiscard]] Extracted extractProgram(const std::filesystem::path& archive, std::string_view inside,
                                       const std::filesystem::path& folder);

struct InstallResult {
    bool ok        = false;
    bool cancelled = false;  // `cancel` stopped the download: what came is kept, the next try goes on from there
    std::string error;
    Installed engine;
    ProbeResult probe;                // what the engine answered: its name, its author, its options
    std::vector<std::string> failed;  // the files that did not start before it, each with why
};

// Downloads the best of `pick`'s files into versionFolder(), checks it against its published hash (its size when
// GitHub gave none), takes the program out of an archive, makes it executable and starts it. A file that does not
// start (an instruction this processor lacks after all, a library the system misses) is removed and the next one
// tried; a download that fails stops there. The engine installed is added to installed.json, in place of an earlier
// install of the same version. Blocks for the download and up to the startup timeout for each file tried: call it
// from a thread of its own. Needs config::TournamentConfig to be set, as probeEngine() does.
[[nodiscard]] InstallResult installEngine(const std::filesystem::path& profile, const Engine& engine, const Pick& pick,
                                          DownloadProgress* progress      = nullptr,
                                          const std::atomic<bool>* cancel = nullptr);

}  // namespace fcg::catalogue
