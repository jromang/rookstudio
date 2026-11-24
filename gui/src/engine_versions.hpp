#pragma once

// The versions of the engines taken from the engine picker, each in a folder of its own in the profile
// (<profile>/engines/<id>/<version>, #59), and what becomes of the old ones (#65, decided in M9, point 5): once an
// update has replaced a version in the library, its folder is deleted when no tournament that is not over (playing,
// paused, stopped) plays its program, which is checked again at the end of each of them. A version kept on purpose
// ("Keep … too": another engine of the library plays it) is never deleted. A deleted version stays listed in
// installed.json, so that it can be downloaded again, its hash checked, if a tournament asks for it again. A version
// left from before (never replaced by an update since) is deleted on the user's word only.

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "engine_catalogue.hpp"
#include "engine_downloads.hpp"
#include "model.hpp"

namespace fcg::catalogue {

// A tournament that is not over, and the programs its participants play.
struct ProgramUser {
    std::string name;
    std::filesystem::path dir;
    std::vector<std::string> programs;
    bool playing = false, paused = false;  // neither: stopped (or played elsewhere)
    uint64_t played = 0;                   // its games so far
    std::optional<uint64_t> total;         // of how many, when it says
};

enum class VersionState {
    Current,  // the library's engine from the catalogue plays it
    Kept,     // another engine of the library plays it ("Keep … too"): never deleted
    Held,     // a tournament not over plays it: deleted at the end of the last one, once an update retired it
    Retired,  // replaced by an update and played by nothing: deleted
    Unused,   // played by nothing, never replaced by an update since (from before): deleted on the user's word only
    Asked,    // deleted, and a tournament not over asks for it again: to download again
};

struct DiskVersion {
    Installed installed;  // its entry of installed.json
    std::filesystem::path folder;
    uint64_t bytes     = 0;  // on the disk; what deleting it frees
    VersionState state = VersionState::Unused;
    std::vector<std::string> engines;      // the library's engines that play it
    std::vector<ProgramUser> tournaments;  // the tournaments not over that play it
};

// Whether `program` is in `folder` (both as written in the profile).
[[nodiscard]] bool inFolder(const std::filesystem::path& program, const std::filesystem::path& folder);
// The bytes of a folder's files, its subfolders' included.
[[nodiscard]] uint64_t folderBytes(const std::filesystem::path& folder);

// The versions of the engine `id` on the disk, and those deleted that a tournament asks for again: the newest install
// first. `library`'s engines from the catalogue (`catalogue` == id) play the current one.
[[nodiscard]] std::vector<DiskVersion> diskVersions(const std::filesystem::path& profile,
                                                    const std::vector<Installed>& installed, std::string_view id,
                                                    const std::vector<EngineEntry>& library,
                                                    const std::vector<ProgramUser>& tournaments);

// An update replaced `version` of `id` in the library at `when`: the version retired, to be deleted once unused.
void retireVersion(std::vector<Installed>& installed, std::string_view id, std::string_view version, int64_t when);
// Kept on purpose again ("Keep … too"), or downloaded again: no longer retired nor deleted.
void reviveVersion(std::vector<Installed>& installed, std::string_view id, std::string_view version);

// A version deleted: its folder removed, its entry kept with when and the bytes freed. False, with why in `error`
// (a file in use), when the folder could not be removed whole.
bool deleteVersion(std::vector<Installed>& installed, const DiskVersion& v, int64_t when, std::string* error = nullptr);

// What the check deletes now: the retired versions played by nothing (`profile`'s folders, `installed` updated and
// written by the caller), each with the tournament that held it last, when one did, for the notice. The retired
// versions still held note their tournament (Installed::held_by).
struct Deleted {
    std::string id, version;
    uint64_t freed = 0;
    std::string error;    // why it could not be deleted
    std::string held_by;  // the tournament whose end freed it, when one held it
};
[[nodiscard]] std::vector<Deleted> deleteRetired(const std::filesystem::path& profile,
                                                 std::vector<Installed>& installed,
                                                 const std::vector<EngineEntry>& library,
                                                 const std::vector<ProgramUser>& tournaments, int64_t when);

// A deleted version as a pick, to download it again from its release (`github`: GitHub's site, a test's server
// otherwise), its hash checked as it was the first time; nullopt when the catalogue no longer names its file.
[[nodiscard]] std::optional<Pick> pickAgain(const Engine& engine, const Installed& installed,
                                            std::string_view github = "https://github.com");

}  // namespace fcg::catalogue
