#include "engine_downloads.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <memory>
#include <optional>
#include <regex>
#include <system_error>
#include <utility>

#include <fmt/include/fmt/format.h>
#include <study/sources.hpp>
#include <tour/files.hpp>

#include "model.hpp"

namespace fcg::catalogue {

namespace fs = std::filesystem;

namespace {

constexpr uint64_t WHOLE = std::numeric_limits<uint64_t>::max();  // to the end of the stream

HttpResult apiGet(std::string_view api, const std::string& path) {
    HttpRequest request;
    request.url       = std::string(api) + path;
    request.method    = "GET";
    request.timeout_s = 20;
    request.headers   = {{"Accept", "application/vnd.github+json"}, {"X-GitHub-Api-Version", "2022-11-28"}};
    return httpRequest(request);
}

// Why GitHub's answer is not one to read.
void refused(const HttpResult& r, const Engine& engine, Releases& out) {
    if (r.status == 0) {
        out.error = r.error.empty() ? "GitHub did not answer" : "GitHub did not answer: " + r.error;
    } else if (r.status == 429 || (r.status == 403 && r.body.find("rate limit") != std::string::npos)) {
        out.limited = true;
        out.error =
            "GitHub refuses more requests from this address for now (60 an hour without an account): "
            "try again later";
    } else if (r.status == 404) {
        out.error = "GitHub has no repository " + engine.repo;
    } else {
        out.error = fmt::format("GitHub answered {} for {}", r.status, engine.repo);
    }
}

// `size` bytes of `in` into `out`, fewer only at its end; how many came.
std::size_t readFull(study::ByteSource& in, char* out, std::size_t size) {
    std::size_t n = 0;
    while (n < size) {
        const auto k = in.read(out + n, size - n);
        if (k == 0) break;
        n += k;
    }
    return n;
}

// A number of a tar header: octal digits, or base 256 when the first byte's high bit is set (GNU, big files).
uint64_t tarNumber(const char* p, std::size_t n) {
    if (static_cast<unsigned char>(p[0]) & 0x80) {
        uint64_t v = static_cast<unsigned char>(p[0]) & 0x7F;
        for (std::size_t i = 1; i < n; ++i) v = (v << 8) | static_cast<unsigned char>(p[i]);
        return v;
    }
    uint64_t v    = 0;
    std::size_t i = 0;
    while (i < n && (p[i] == ' ' || p[i] == '\0')) ++i;
    for (; i < n && p[i] >= '0' && p[i] <= '7'; ++i) v = v * 8 + static_cast<uint64_t>(p[i] - '0');
    return v;
}

std::string tarText(const char* p, std::size_t n) { return std::string(p, strnlen(p, n)); }

// The header's checksum: the sum of its bytes, its own field taken as spaces.
bool tarChecksum(const std::array<char, 512>& h) {
    uint64_t sum = 0;
    for (std::size_t i = 0; i < h.size(); ++i) sum += i >= 148 && i < 156 ? ' ' : static_cast<unsigned char>(h[i]);
    return sum == tarNumber(h.data() + 148, 8);
}

// The path a pax header gives the next file: records "<length> <key>=<value>\n".
std::string paxPath(std::string_view data) {
    std::string path;
    for (std::size_t i = 0; i < data.size();) {
        const auto space = data.find(' ', i);
        if (space == std::string_view::npos) break;
        std::size_t length = 0;
        std::from_chars(data.data() + i, data.data() + space, length);
        if (length == 0 || i + length > data.size() || space + 1 >= i + length) break;
        const auto record = data.substr(space + 1, i + length - space - 2);  // without its "\n"
        if (record.rfind("path=", 0) == 0) path = std::string(record.substr(5));
        i += length;
    }
    return path;
}

// The name a file of an archive is written under: its last part, as a file name on every system.
std::optional<std::string> programName(std::string_view path) {
    const auto slash = path.find_last_of("/\\");
    const auto name  = std::string(slash == std::string_view::npos ? path : path.substr(slash + 1));
    if (name.empty() || name == "." || name == "..") return std::nullopt;
    return tour::fileNameOf(name);
}

// `size` bytes of `in` (WHOLE: to its end) written to `file`, through <file>.part.
std::string writeStream(study::ByteSource& in, uint64_t size, const fs::path& file) {
    std::error_code ec;
    const auto part = fs::path(file).concat(".part");
    std::unique_ptr<std::FILE, int (*)(std::FILE*)> f(tour::openFile(part, "wb"), &std::fclose);
    if (!f) return "cannot write " + part.string();
    std::vector<char> buffer(1 << 16);
    std::string error;
    for (uint64_t left = size; left > 0;) {
        const auto want = static_cast<std::size_t>(std::min<uint64_t>(buffer.size(), left));
        const auto n    = in.read(buffer.data(), want);
        if (n == 0) {
            if (size != WHOLE) error = "the archive is cut short";
            break;
        }
        if (std::fwrite(buffer.data(), 1, n, f.get()) != n) {
            error = "cannot write " + part.string() + " (is the disk full?)";
            break;
        }
        if (size != WHOLE) left -= n;
    }
    if (std::fclose(f.release()) != 0 && error.empty()) error = "cannot write " + part.string();
    if (error.empty()) {
        fs::rename(part, file, ec);
        if (ec) error = "cannot rename " + part.string() + ": " + ec.message();
    }
    if (!error.empty()) fs::remove(part, ec);
    return error;
}

// The program of a tar stream (ustar, GNU long names, pax paths): the regular file whose path matches `inside`.
Extracted fromTar(study::ByteSource& in, const std::regex& inside, const fs::path& folder) {
    std::array<char, 512> h{};
    std::string next_name;  // a GNU long name or a pax path, for the file after it
    std::vector<char> skip(1 << 16);
    for (;;) {
        const auto got = readFull(in, h.data(), h.size());
        if (got == 0) break;
        if (got < h.size()) return {{}, "the archive is cut short"};
        if (std::all_of(h.begin(), h.end(), [](char c) { return c == 0; })) break;  // its end
        if (!tarChecksum(h)) return {{}, "it is not a tar archive"};
        std::string name = tarText(h.data(), 100);
        if (std::memcmp(h.data() + 257, "ustar", 5) == 0) {
            const auto prefix = tarText(h.data() + 345, 155);
            if (!prefix.empty()) name = prefix + "/" + name;
        }
        if (!next_name.empty()) name = std::exchange(next_name, {});
        const char type       = h[156];
        const uint64_t size   = tarNumber(h.data() + 124, 12);
        const uint64_t padded = (size + 511) / 512 * 512;
        if (type == 'L' || type == 'x') {
            if (size > (1u << 20)) return {{}, "a damaged archive"};
            std::string data(static_cast<std::size_t>(padded), '\0');
            if (readFull(in, data.data(), data.size()) < data.size()) return {{}, "the archive is cut short"};
            data.resize(static_cast<std::size_t>(size));
            next_name = type == 'L' ? std::string(data.c_str()) : paxPath(data);
            continue;
        }
        while (name.rfind("./", 0) == 0) name.erase(0, 2);
        const bool regular = type == '0' || type == '\0' || type == '7';
        if (regular && std::regex_match(name, inside)) {
            const auto file_name = programName(name);
            if (!file_name) return {{}, "the program's path in the archive does not end with a file's name"};
            const auto program = folder / *file_name;
            auto error         = writeStream(in, size, program);
            if (!error.empty()) return {{}, std::move(error)};
            return {program, {}};
        }
        for (uint64_t left = padded; left > 0;) {
            const auto want = static_cast<std::size_t>(std::min<uint64_t>(skip.size(), left));
            if (readFull(in, skip.data(), want) < want) return {{}, "the archive is cut short"};
            left -= want;
        }
    }
    return {{}, "the program is not in the archive"};
}

int64_t nowSeconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

}  // namespace

Releases fetchReleases(const Engine& engine, const CpuFeatures& cpu, std::string_view api) {
    Releases out;
    const auto latest = apiGet(api, "/repos/" + engine.repo + "/releases/latest");
    if (latest.status == 200) {
        if (auto release = parseRelease(latest.body)) {
            out.list.push_back(std::move(*release));
            const auto pick = pickBuild(engine, out.list, cpu);
            // The latest is the engine's and has a file for this machine, or the engine has none for this system.
            // Otherwise an earlier release may have one: the engine's when the latest is a side release, one whose
            // files came, one for an older processor.
            if (pick.error == PickError::None || pick.error == PickError::NoPlatform) return out;
        }
    } else if (latest.status != 404) {  // 404: no release is marked latest (none, or pre-releases only)
        refused(latest, engine, out);
        return out;
    }
    // A side release (another network, a tool), or files not sent yet: the releases before.
    const auto list = apiGet(api, "/repos/" + engine.repo + "/releases?per_page=20");
    if (list.status != 200) {
        if (out.list.empty()) refused(list, engine, out);
        return out;
    }
    if (auto all = parseReleases(list.body); !all.empty()) out.list = std::move(all);
    return out;
}

fs::path enginesFolder(const fs::path& profile) { return profile / "engines"; }

namespace {

nlohmann::json releaseJson(const Release& r) {
    nlohmann::json assets = nlohmann::json::array();
    for (const auto& a : r.assets) {
        assets.push_back(
            {{"name", a.name}, {"size", a.size}, {"sha256", a.sha256}, {"url", a.url}, {"uploaded", a.uploaded}});
    }
    return {{"tag", r.tag},
            {"published", r.published},
            {"draft", r.draft},
            {"prerelease", r.prerelease},
            {"assets", std::move(assets)}};
}

std::optional<Release> releaseOf(const nlohmann::json& j) {
    if (!j.is_object()) return std::nullopt;
    Release r;
    r.tag        = j.value("tag", "");
    r.published  = j.value("published", "");
    r.draft      = j.value("draft", false);
    r.prerelease = j.value("prerelease", false);
    if (const auto it = j.find("assets"); it != j.end() && it->is_array()) {
        for (const auto& a : *it) {
            if (!a.is_object()) continue;
            r.assets.push_back({a.value("name", ""), a.value("size", uint64_t{0}), a.value("sha256", ""),
                                a.value("url", ""), a.value("uploaded", false)});
        }
    }
    if (r.tag.empty()) return std::nullopt;
    return r;
}

// A file of the profile, whole: empty when it cannot be read.
std::string readWhole(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return {};
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

bool youngerThan(const fs::path& file, std::chrono::seconds age) {
    std::error_code ec;
    const auto written = fs::last_write_time(file, ec);
    if (ec) return false;
    return fs::file_time_type::clock::now() - written < age;
}

}  // namespace

Catalogue currentCatalogue(const fs::path& profile, bool fetch, std::string_view url) {
    const auto kept = enginesFolder(profile) / "catalogue.json";
    if (youngerThan(kept, std::chrono::hours(24))) {
        if (auto catalogue = parseCatalogue(readWhole(kept))) return *catalogue;
    }
    if (fetch) {
        HttpRequest request;
        request.url       = std::string(url);
        request.method    = "GET";
        request.timeout_s = 20;
        const auto answer = httpRequest(request);
        if (answer.status == 200) {
            if (auto catalogue = parseCatalogue(answer.body)) {
                std::error_code ec;
                fs::create_directories(kept.parent_path(), ec);
                tour::replaceFile(kept, answer.body);
                return *catalogue;
            }
        }
    }
    return builtInCatalogue();
}

std::optional<std::vector<Release>> keptReleases(const fs::path& profile, std::string_view id,
                                                 std::chrono::seconds max_age) {
    const auto file = enginesFolder(profile) / "releases" / (tour::fileNameOf(std::string(id)) + ".json");
    if (!youngerThan(file, max_age)) return std::nullopt;
    const auto j = nlohmann::json::parse(readWhole(file), nullptr, false);
    if (j.is_discarded() || !j.is_array()) return std::nullopt;
    std::vector<Release> out;
    for (const auto& r : j) {
        if (auto release = releaseOf(r)) out.push_back(std::move(*release));
    }
    return out;
}

void keepReleases(const fs::path& profile, std::string_view id, const std::vector<Release>& list) {
    const auto folder = enginesFolder(profile) / "releases";
    std::error_code ec;
    fs::create_directories(folder, ec);
    nlohmann::json j = nlohmann::json::array();
    for (const auto& r : list) j.push_back(releaseJson(r));
    tour::replaceFile(folder / (tour::fileNameOf(std::string(id)) + ".json"), j.dump());
}

fs::path versionFolder(const fs::path& profile, std::string_view id, std::string_view version) {
    return enginesFolder(profile) / tour::fileNameOf(std::string(id)) / tour::fileNameOf(std::string(version));
}

std::vector<Installed> loadInstalled(const fs::path& profile) {
    std::vector<Installed> out;
    std::ifstream in(enginesFolder(profile) / "installed.json", std::ios::binary);
    if (!in) return out;
    const auto j = nlohmann::json::parse(in, nullptr, false);
    if (j.is_discarded() || !j.is_array()) return out;
    for (const auto& e : j) {
        try {
            auto installed = e.get<Installed>();
            if (!installed.id.empty() && !installed.program.empty()) out.push_back(std::move(installed));
        } catch (const nlohmann::json::exception&) {
        }
    }
    return out;
}

bool saveInstalled(const fs::path& profile, const std::vector<Installed>& installed, std::string* error) {
    std::error_code ec;
    fs::create_directories(enginesFolder(profile), ec);
    try {
        tour::replaceFile(enginesFolder(profile) / "installed.json", nlohmann::json(installed).dump(2) + "\n");
        return true;
    } catch (const std::exception& e) {
        if (error) *error = e.what();
        return false;
    }
}

Extracted extractProgram(const fs::path& archive, std::string_view inside, const fs::path& folder) {
    std::regex re;
    try {
        re = std::regex(std::string(inside), std::regex::ECMAScript);
    } catch (const std::regex_error&) {
        return {{}, "the program's path in the catalogue is not a regular expression"};
    }
    std::error_code ec;
    fs::create_directories(folder, ec);
    try {
        std::array<unsigned char, 4> magic{};
        {
            std::unique_ptr<std::FILE, int (*)(std::FILE*)> f(tour::openFile(archive, "rb"), &std::fclose);
            if (!f || std::fread(magic.data(), 1, magic.size(), f.get()) != magic.size())
                return {{}, "cannot read " + archive.string()};
        }
        if (magic[0] == 'P' && magic[1] == 'K' && magic[2] == 3 && magic[3] == 4) {
            auto files = study::zipFiles(archive, [&](std::string_view name) {
                return !name.empty() && name.back() != '/' && std::regex_match(name.begin(), name.end(), re);
            });
            if (files.empty()) return {{}, "the program is not in the archive"};
            const auto file_name = programName(files.front()->name());
            if (!file_name) return {{}, "the program's path in the archive does not end with a file's name"};
            const auto program = folder / *file_name;
            auto error         = writeStream(*files.front(), WHOLE, program);
            if (!error.empty()) return {{}, std::move(error)};
            return {program, {}};
        }
        // A tar archive, compressed with gzip or not.
        auto sources = study::openSources(archive);
        if (sources.size() != 1) return {{}, "it is not a tar archive"};
        return fromTar(*sources.front(), re, folder);
    } catch (const std::exception& e) {
        return {{}, e.what()};
    }
}

InstallResult installEngine(const fs::path& profile, const Engine& engine, const Pick& pick, DownloadProgress* progress,
                            const std::atomic<bool>* cancel) {
    InstallResult out;
    if (pick.choices.empty()) {
        out.error = pick.error == PickError::None ? "nothing to download" : pick.message();
        return out;
    }
    std::error_code ec;
    fs::path folder;
    for (const auto& choice : pick.choices) {
        folder          = versionFolder(profile, engine.id, choice.version);
        const auto file = folder / tour::fileNameOf(choice.asset.name);
        const auto got  = httpDownload(choice.asset.url, file, progress, cancel, choice.asset.sha256);
        if (!got.ok) {
            // The network or the disk: another file would fare no better.
            out.cancelled = got.error == "cancelled";
            out.error     = got.error;
            return out;
        }
        if (choice.asset.sha256.empty() && choice.asset.size > 0 && fs::file_size(file, ec) != choice.asset.size) {
            fs::remove(file, ec);
            out.error = choice.asset.name + " downloaded is not of the size GitHub gives";
            return out;
        }
        fs::path program = file;
        if (!choice.build.inside.empty()) {
            const auto extracted = extractProgram(file, regexOf(choice.build.inside), folder);
            fs::remove(file, ec);
            if (!extracted.error.empty()) {
                out.failed.push_back(choice.asset.name + ": " + extracted.error);
                continue;
            }
            program = extracted.program;
        }
#ifndef _WIN32
        fs::permissions(program,
                        fs::perms::owner_all | fs::perms::group_read | fs::perms::group_exec | fs::perms::others_read |
                            fs::perms::others_exec,
                        fs::perm_options::replace, ec);
#endif
        EngineEntry entry;
        entry.name     = engine.name;
        entry.cmd      = fs::absolute(program, ec).string();
        entry.protocol = "uci";
        auto probe     = probeEngine(entry, cancel);
        if (cancel && cancel->load()) {
            fs::remove(program, ec);
            out.cancelled = true;
            out.error     = "cancelled";
            return out;
        }
        if (!probe.ok) {
            fs::remove(program, ec);
            out.failed.push_back(choice.asset.name + ": " +
                                 (probe.error.empty() ? std::string("it does not answer uci") : probe.error));
            continue;
        }
        Installed installed{engine.id,      choice.version, choice.tag,    choice.asset.name, choice.asset.sha256,
                            choice.licence, entry.cmd,      probe.id_name, probe.id_author,   nowSeconds()};
        auto list = loadInstalled(profile);
        list.erase(std::remove_if(list.begin(), list.end(),
                                  [&](const Installed& i) { return i.id == engine.id && i.version == choice.version; }),
                   list.end());
        list.push_back(installed);
        std::string error;
        if (!saveInstalled(profile, list, &error)) {
            out.error = "the engine is installed, but its list cannot be written: " + error;
            return out;
        }
        out.ok     = true;
        out.engine = std::move(installed);
        out.probe  = std::move(probe);
        return out;
    }
    if (fs::is_empty(folder, ec)) fs::remove(folder, ec);
    std::string why;
    for (const auto& f : out.failed) why += (why.empty() ? "" : "; ") + f;
    out.error = "none of its files for this machine started: " + why;
    return out;
}

}  // namespace fcg::catalogue
