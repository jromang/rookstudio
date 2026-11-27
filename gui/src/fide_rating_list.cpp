#include "fide_rating_list.hpp"

#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <mutex>

#include <fmt/include/fmt/format.h>
#include <json.hpp>

#include <study/sources.hpp>
#include <tour/files.hpp>
#include <tour/util.hpp>

#include "i18n.hpp"

namespace fs = std::filesystem;

namespace fcg::fide {

namespace {

std::mutex server_mutex;
std::string server_url = "https://ratings.fide.com";

// A list's month and its year, each language its own order and form ("May 2026", "2026年5月"): not "May" alone, which
// is the short month of a date elsewhere.
constexpr std::array<const char*, 12> MONTH_YEAR = {
    N_("January {}"), N_("February {}"), N_("March {}"),     N_("April {}"),   N_("May {}"),      N_("June {}"),
    N_("July {}"),    N_("August {}"),   N_("September {}"), N_("October {}"), N_("November {}"), N_("December {}")};

fs::path folder(const fs::path& profile) { return profile / "fide"; }
fs::path keptFile(const fs::path& profile) { return folder(profile) / "list.json"; }

std::optional<Day> today() {
    const auto now = std::time(nullptr);
    const auto t   = localTime(now);
    return Day{t.tm_year + 1900, t.tm_mon + 1, t.tm_mday};
}

}  // namespace

void setServer(std::string url) {
    std::lock_guard lock(server_mutex);
    server_url = std::move(url);
}

std::string listUrl() {
    std::lock_guard lock(server_mutex);
    return server_url + "/download/players_list.zip";
}

std::string Day::monthKey() const { return fmt::format("{:04}-{:02}", year, month); }
std::string Day::text() const { return fmt::format("{:04}-{:02}-{:02}", year, month, day); }

std::optional<Day> httpDay(std::string_view text) {
    // "Sun, 22 Nov 2026 22:03:51 GMT"
    static constexpr std::array<std::string_view, 12> names = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                                               "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    const auto comma                                        = text.find(',');
    if (comma != std::string_view::npos) text.remove_prefix(comma + 1);
    int day = 0, year = 0;
    char month[4] = {};
    if (std::sscanf(std::string(text).c_str(), " %d %3s %d", &day, month, &year) != 3) return std::nullopt;
    for (std::size_t m = 0; m < names.size(); ++m)
        if (names[m] == month && day >= 1 && day <= 31 && year >= 2000 && year < 3000)
            return Day{year, static_cast<int>(m) + 1, day};
    return std::nullopt;
}

std::optional<Day> zipDay(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return std::nullopt;
    std::error_code ec;
    const auto size = fs::file_size(file, ec);
    if (ec) return std::nullopt;
    const study::ArchiveBytes read = [&](uint64_t offset, std::size_t n) {
        std::vector<unsigned char> bytes(n);
        in.clear();
        in.seekg(static_cast<std::streamoff>(offset));
        in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(n));
        bytes.resize(static_cast<std::size_t>(in.gcount()));
        return bytes;
    };
    try {
        for (const auto& entry : study::zipEntries(size, read, file.string())) {
            const auto header = read(entry.header, 30);
            if (header.size() < 30) return std::nullopt;
            const int date = header[12] | (header[13] << 8);
            const Day d{1980 + (date >> 9), (date >> 5) & 15, date & 31};
            if (d.month < 1 || d.month > 12 || d.day < 1) return std::nullopt;
            return d;
        }
    } catch (const std::exception&) {
    }
    return std::nullopt;
}

std::string monthText(std::string_view key) {
    int year = 0, month = 0;
    if (std::sscanf(std::string(key).c_str(), "%d-%d", &year, &month) != 2 || month < 1 || month > 12)
        return std::string(key);
    return trf(MONTH_YEAR[static_cast<std::size_t>(month - 1)], year);
}

fs::path listFile(const fs::path& profile, const Kept& kept) {
    return folder(profile) / (kept.file.empty() ? std::string("list.bin") : kept.file);
}

std::optional<Kept> readKept(const fs::path& profile) {
    std::ifstream in(keptFile(profile), std::ios::binary);
    if (!in) return std::nullopt;
    try {
        const auto j = nlohmann::json::parse(in);
        Kept k;
        k.file = j.value("file", std::string("list.bin"));
        std::error_code ec;
        if (!fs::is_regular_file(listFile(profile, k), ec)) return std::nullopt;
        k.bytes         = fs::file_size(listFile(profile, k), ec);
        k.month         = j.value("month", std::string());
        k.published     = j.value("published", std::string());
        k.offered       = j.value("offered", std::string());
        k.info.month    = k.month;
        k.info.players  = j.value("players", uint64_t{0});
        k.info.rated    = j.value("rated", uint64_t{0});
        k.info.standard = j.value("standard", uint64_t{0});
        k.info.rapid    = j.value("rapid", uint64_t{0});
        k.info.blitz    = j.value("blitz", uint64_t{0});
        if (k.month.empty()) return std::nullopt;
        return k;
    } catch (const nlohmann::json::exception&) {
        return std::nullopt;
    }
}

void writeKept(const fs::path& profile, const Kept& k) {
    std::error_code ec;
    fs::create_directories(folder(profile), ec);
    const nlohmann::json j{{"file", k.file},
                           {"month", k.month},
                           {"published", k.published},
                           {"offered", k.offered},
                           {"players", k.info.players},
                           {"rated", k.info.rated},
                           {"standard", k.info.standard},
                           {"rapid", k.info.rapid},
                           {"blitz", k.info.blitz}};
    tour::replaceFile(keptFile(profile), j.dump(1) + "\n");
}

void removeKept(const fs::path& profile) {
    std::error_code ec;
    fs::remove(keptFile(profile), ec);
    // Every list there: the one kept, and those a mapping kept before.
    for (const auto& entry : fs::directory_iterator(folder(profile), ec)) {
        const auto name = entry.path().filename().string();
        if (name.rfind("list", 0) == 0 && entry.path().extension() == ".bin") fs::remove(entry.path(), ec);
    }
}

std::string monthDue(const Kept& kept, std::time_t now) {
    const auto t       = localTime(now);
    const auto current = fmt::format("{:04}-{:02}", t.tm_year + 1900, t.tm_mon + 1);
    if (current <= kept.month || current <= kept.offered) return {};
    return current;
}

std::unique_ptr<Job> start(const fs::path& profile, const fs::path& file) {
    auto job = std::make_unique<Job>();
    Job* j   = job.get();
    j->done  = std::async(std::launch::async, [j, profile, file] {
        Job::Outcome out;
        const auto before = readKept(profile);
        fs::path source   = file;
        std::optional<Day> day;
        const auto zip = folder(profile) / "players_list.zip";
        if (file.empty()) {
            // The day FIDE published the list, asked first: the list kept already is not downloaded again.
            const auto url = listUrl();
            int status     = 0;
            std::string modified;
            HttpStream stream;
            stream.on_head = [&](int s, std::string_view head) {
                status   = s;
                modified = httpHeader(head, "Last-Modified").value_or("");
                return false;
            };
            stream.cancel = &j->cancel;
            HttpRequest request;
            request.url       = url;
            request.method    = "GET";
            const auto answer = politeStream(request, stream);
            if (j->cancel) {
                out.cancelled = true;
                return out;
            }
            if (status != 200) {
                out.error = status ? fmt::format("ratings.fide.com answered {}", status)
                                   : (answer.error.empty() ? std::string("no answer") : answer.error);
                return out;
            }
            day = httpDay(modified);
            if (!day) day = today();
            if (before && before->month == day->monthKey()) {
                out.same = true;
                out.kept = *before;
                return out;
            }
            j->phase       = Job::Phase::Downloading;
            const auto got = politeDownload(url, zip, &j->download, &j->cancel);
            if (!got.ok) {
                std::error_code ec;
                const auto part = fs::path(zip.string() + ".part");
                out.cut         = fs::exists(part, ec) ? fs::file_size(part, ec) : 0;
                out.cancelled   = j->cancel.load();
                out.error       = out.cancelled ? std::string() : got.error;
                return out;
            }
            source = zip;
        } else {
            day = zipDay(file);
            if (!day) {
                std::error_code ec;
                const auto written = fs::last_write_time(file, ec);
                if (!ec) {
                    const auto when = std::chrono::system_clock::to_time_t(
                        std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                            std::chrono::file_clock::to_sys(written)));
                    const auto t = localTime(when);
                    day = Day{t.tm_year + 1900, t.tm_mon + 1, t.tm_mday};
                } else {
                    day = today();
                }
            }
        }
        j->phase  = Job::Phase::Reading;
        out.month = day->monthKey();
        try {
            // A file of its own: the list kept before may still be mapped (Windows replaces no mapped file).
            Kept k;
            k.file = "list-" + day->monthKey() + ".bin";
            std::error_code ec;
            for (int n = 2; fs::exists(folder(profile) / k.file, ec); ++n)
                k.file = "list-" + day->monthKey() + "-" + std::to_string(n) + ".bin";
            const auto info =
                study::prepareFideList(source, folder(profile) / k.file, day->monthKey(), j->reading, j->cancel);
            k.month     = day->monthKey();
            k.published = day->text();
            k.info      = info;
            k.offered   = before ? before->offered : std::string();
            k.bytes     = fs::file_size(folder(profile) / k.file, ec);
            writeKept(profile, k);
            // The lists before go; one still mapped goes the next time.
            for (const auto& entry : fs::directory_iterator(folder(profile), ec)) {
                const auto name = entry.path().filename().string();
                if (name != k.file && name.rfind("list", 0) == 0 && entry.path().extension() == ".bin")
                    fs::remove(entry.path(), ec);
            }
            out.ok   = true;
            out.kept = k;
        } catch (const study::FideFormatError& e) {
            out.format = true;
            out.error  = e.what();
        } catch (const std::exception& e) {
            out.cancelled = j->cancel.load();
            out.error     = out.cancelled ? std::string() : std::string(e.what());
        }
        if (file.empty()) {
            std::error_code ec;
            fs::remove(zip, ec);  // read: RookStudio's file holds what it needs
        }
        return out;
    });
    return job;
}

}  // namespace fcg::fide
