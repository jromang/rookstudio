#include "engine_versions.hpp"

#include <algorithm>
#include <regex>
#include <system_error>

#include <fmt/include/fmt/format.h>
#include <tour/files.hpp>

#include "lichess_api.hpp"

namespace fcg::catalogue {

namespace fs = std::filesystem;

namespace {

std::string normal(const fs::path& p) {
    std::error_code ec;
    auto absolute = fs::absolute(p, ec);
    if (ec) absolute = p;
    auto text = absolute.lexically_normal().generic_string();
    while (text.size() > 1 && text.back() == '/') text.pop_back();
    return text;
}

bool played(const ProgramUser& u, const fs::path& folder) {
    return std::any_of(u.programs.begin(), u.programs.end(),
                       [&](const std::string& program) { return !program.empty() && inFolder(program, folder); });
}

}  // namespace

bool inFolder(const fs::path& program, const fs::path& folder) {
    const auto p = normal(program), f = normal(folder);
    return p.size() > f.size() && p.compare(0, f.size(), f) == 0 && p[f.size()] == '/';
}

uint64_t folderBytes(const fs::path& folder) {
    uint64_t bytes = 0;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code e;
        if (it->is_regular_file(e)) bytes += it->file_size(e);
    }
    return bytes;
}

std::vector<DiskVersion> diskVersions(const fs::path& profile, const std::vector<Installed>& installed,
                                      std::string_view id, const std::vector<EngineEntry>& library,
                                      const std::vector<ProgramUser>& tournaments) {
    std::vector<DiskVersion> out;
    for (const auto& i : installed) {
        if (i.id != id || i.id.empty() || i.version.empty()) continue;
        DiskVersion v;
        v.installed = i;
        v.folder    = versionFolder(profile, i.id, i.version);
        for (const auto& e : library)
            if (e.machine.empty() && !e.cmd.empty() && inFolder(e.cmd, v.folder)) v.engines.push_back(e.name);
        for (const auto& u : tournaments)
            if (played(u, v.folder)) v.tournaments.push_back(u);
        std::error_code ec;
        const bool there = fs::exists(i.program, ec);
        if (i.deleted > 0 || !there) {
            // Gone: listed only while a tournament asks for it again.
            if (v.tournaments.empty()) continue;
            v.state = VersionState::Asked;
            out.push_back(std::move(v));
            continue;
        }
        v.bytes            = folderBytes(v.folder);
        const bool current = std::any_of(library.begin(), library.end(), [&](const EngineEntry& e) {
            return e.catalogue == id && e.machine.empty() && inFolder(e.cmd, v.folder);
        });
        if (current)
            v.state = VersionState::Current;
        else if (!v.engines.empty())
            v.state = VersionState::Kept;
        else if (!v.tournaments.empty())
            v.state = VersionState::Held;
        else
            v.state = i.retired > 0 ? VersionState::Retired : VersionState::Unused;
        out.push_back(std::move(v));
    }
    std::stable_sort(out.begin(), out.end(), [](const DiskVersion& a, const DiskVersion& b) {
        return a.installed.installed > b.installed.installed;
    });
    return out;
}

void retireVersion(std::vector<Installed>& installed, std::string_view id, std::string_view version, int64_t when) {
    for (auto& i : installed)
        if (i.id == id && i.version == version && i.retired == 0) i.retired = when;
}

void reviveVersion(std::vector<Installed>& installed, std::string_view id, std::string_view version) {
    for (auto& i : installed) {
        if (i.id != id || i.version != version) continue;
        i.retired = 0;
        i.deleted = 0;
        i.freed   = 0;
        i.held_by.clear();
    }
}

bool deleteVersion(std::vector<Installed>& installed, const DiskVersion& v, int64_t when, std::string* error) {
    // A version's own folder only, <profile>/engines/<id>/<version>: never the engines' folder whole.
    const auto version_name = tour::fileNameOf(v.installed.version), id_name = tour::fileNameOf(v.installed.id);
    if (v.installed.id.empty() || v.installed.version.empty() || version_name.empty() || id_name.empty() ||
        v.folder.filename() != version_name || v.folder.parent_path().filename() != id_name ||
        v.folder.parent_path().parent_path().filename() != "engines") {
        if (error) *error = "not a version's folder";
        return false;
    }
    std::error_code ec;
    fs::remove_all(v.folder, ec);
    if (ec || fs::exists(v.folder)) {
        if (error) *error = ec ? ec.message() : std::string("a file of it is in use");
        return false;
    }
    // Its engine's folder, when it held nothing else.
    const auto parent = v.folder.parent_path();
    if (fs::is_empty(parent, ec)) fs::remove(parent, ec);
    for (auto& i : installed) {
        if (i.id != v.installed.id || i.version != v.installed.version) continue;
        i.deleted = when;
        i.freed   = v.bytes;
    }
    return true;
}

std::vector<Deleted> deleteRetired(const fs::path& profile, std::vector<Installed>& installed,
                                   const std::vector<EngineEntry>& library, const std::vector<ProgramUser>& tournaments,
                                   int64_t when) {
    std::vector<std::string> ids;
    for (const auto& i : installed)
        if (std::find(ids.begin(), ids.end(), i.id) == ids.end()) ids.push_back(i.id);
    std::vector<Deleted> out;
    for (const auto& id : ids) {
        for (const auto& v : diskVersions(profile, installed, id, library, tournaments)) {
            if (v.state == VersionState::Held && v.installed.retired > 0) {
                // Its holder noted (a tournament: what holds a version and has no name is not one), to name it once
                // its end frees the version.
                for (const auto& u : v.tournaments) {
                    if (u.name.empty()) continue;
                    for (auto& i : installed)
                        if (i.id == v.installed.id && i.version == v.installed.version) i.held_by = u.name;
                    break;
                }
                continue;
            }
            if (v.state != VersionState::Retired) continue;
            Deleted d{v.installed.id, v.installed.version, v.bytes, {}, v.installed.held_by};
            if (!deleteVersion(installed, v, when, &d.error)) d.freed = 0;
            out.push_back(std::move(d));
        }
    }
    return out;
}

std::optional<Pick> pickAgain(const Engine& engine, const Installed& installed, std::string_view github) {
    for (const auto& [platform, builds] : engine.files) {
        for (const auto& build : builds) {
            try {
                if (!std::regex_match(installed.asset, std::regex(regexOf(build.asset)))) continue;
            } catch (const std::regex_error&) {
                continue;
            }
            Choice c;
            c.version        = installed.version;
            c.tag            = installed.tag;
            c.platform       = platform;
            c.build          = build;
            c.asset.name     = installed.asset;
            c.asset.sha256   = installed.sha256;
            c.asset.uploaded = true;
            c.asset.url      = fmt::format("{}/{}/releases/download/{}/{}", github, engine.repo,
                                           lichess::urlEncode(installed.tag), lichess::urlEncode(installed.asset));
            c.licence        = installed.licence.empty() ? engine.licenceOf(installed.version) : installed.licence;
            Pick p;
            p.choices.push_back(std::move(c));
            return p;
        }
    }
    return std::nullopt;
}

}  // namespace fcg::catalogue
