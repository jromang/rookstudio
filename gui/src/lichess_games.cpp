#include "lichess_games.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <vector>

#include <json.hpp>

#include <fmt/include/fmt/format.h>
#include <study/import.hpp>
#include <tour/files.hpp>

#include "https.hpp"
#include "lichess_api.hpp"

namespace fcg::lichess {

namespace {

constexpr const char* SYNC_FILE = "lichess.json";

// Days from 1970-01-01 to a date of the proleptic Gregorian calendar (Howard Hinnant's days_from_civil).
int64_t daysFromCivil(int64_t y, unsigned m, unsigned d) {
    y -= m <= 2;
    const int64_t era  = (y >= 0 ? y : y - 399) / 400;
    const auto yoe     = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

std::string lower(std::string s) {
    for (auto& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

}  // namespace

std::optional<int64_t> startOf(std::string_view utc_date, std::string_view utc_time) {
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0;
    char extra = 0;
    if (std::sscanf(std::string(utc_date).c_str(), "%4d.%2d.%2d%c", &y, &mo, &d, &extra) != 3 ||
        std::sscanf(std::string(utc_time).c_str(), "%2d:%2d:%2d%c", &h, &mi, &s, &extra) != 3)
        return std::nullopt;
    if (y < 1970 || mo < 1 || mo > 12 || d < 1 || d > 31 || h > 23 || mi > 59 || s > 60 || h < 0 || mi < 0 || s < 0)
        return std::nullopt;
    const int64_t days = daysFromCivil(y, static_cast<unsigned>(mo), static_cast<unsigned>(d));
    return ((days * 24 + h) * 60 + mi) * 60000 + int64_t{s} * 1000;
}

std::optional<SyncState> loadSyncState(const std::filesystem::path& database) {
    std::ifstream in(database / SYNC_FILE, std::ios::binary);
    if (!in) return std::nullopt;
    try {
        const auto j = nlohmann::json::parse(in);
        if (j.value("format", "") != "rookstudio-lichess-sync") return std::nullopt;
        SyncState s;
        s.username = j.at("username").get<std::string>();
        s.since    = j.value("since", int64_t{0});
        return s;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

void saveSyncState(const std::filesystem::path& database, const SyncState& state) {
    const nlohmann::json j{{"format", "rookstudio-lichess-sync"}, {"username", state.username}, {"since", state.since}};
    tour::replaceFile(database / SYNC_FILE, j.dump(1));
}

std::string perfTypes(int speeds) {
    switch (speeds) {
        case 1:
            return "blitz,rapid,classical,chess960";
        case 2:
            return "classical,correspondence,chess960";
        default:
            return "ultraBullet,bullet,blitz,rapid,classical,correspondence,chess960";
    }
}

namespace {

// The export's address: the player, what comes with each game, the filters, and the games from `since` on.
std::string exportUrl(const std::string& username, const SyncFilters& filters, int64_t since, bool moves) {
    auto url = fmt::format("{}/api/games/user/{}?", servers().api, urlEncode(username));
    url += moves ? "clocks=true&evals=true&opening=true&sort=dateAsc" : "moves=false&tags=false&max=1000";
    if (!filters.perf_types.empty()) url += "&perfType=" + filters.perf_types;
    if (since > 0) url += fmt::format("&since={}", since);
    return url;
}

// Lichess's `since` reads when a game was made, a little before it started: a minute more is asked again, the games
// already there left out; the filters' moment when it is later.
int64_t askedSince(const SyncState& state, const SyncFilters& filters) {
    const int64_t from = state.since > 0 ? std::max<int64_t>(0, state.since - 60000) : 0;
    return std::max(from, filters.since);
}

}  // namespace

SyncReport syncGames(study::Database& db, const std::string& username, const std::string& token, SyncProgress& progress,
                     const std::atomic<bool>& cancel, std::size_t commit_games, const SyncFilters& filters) {
    SyncReport report;
    const auto folder = db.folder();
    const auto before = loadSyncState(folder);
    if (before && lower(before->username) != lower(username)) {
        report.error = fmt::format("this database holds {}'s games", before->username);
        return report;
    }
    SyncState state{before ? before->username : username, before ? before->since : 0};  // the name as first written

    HttpRequest request;
    request.url       = exportUrl(username, filters, askedSince(state, filters), true);
    request.method    = "GET";
    request.token     = token;
    request.timeout_s = 60;
    request.headers.emplace_back("Accept", "application/x-chess-pgn");

    std::string pending;  // what came of the games not read yet
    std::vector<study::ImportedGame> batch;
    int64_t newest = state.since;
    std::string refused;
    const auto commit = [&] {
        if (batch.empty()) return true;
        try {
            const auto appended = db.append(std::move(batch));
            batch.clear();
            report.added += appended.added;
            report.duplicates += appended.duplicates;
            state.since = newest;
            saveSyncState(folder, state);
            return true;
        } catch (const std::exception& e) {
            refused = e.what();
            return false;
        }
    };
    const auto take = [&](std::string_view text) {
        study::ImportedGame g;
        if (!study::readImportedGame(text, g)) return true;
        ++report.read;
        ++progress.games;
        if (g.damaged) ++report.damaged;
        std::string date, time;
        for (const auto& [tag, value] : g.tags) {
            if (tag == "UTCDate") date = value;
            if (tag == "UTCTime") time = value;
        }
        if (const auto start = startOf(date, time)) newest = std::max(newest, *start);
        batch.push_back(std::move(g));
        return batch.size() < commit_games || commit();
    };

    int status = 0;
    HttpStream stream;
    stream.on_head = [&](int s, std::string_view) {
        status = s;
        return s == 200;
    };
    // Every game but the last that came is whole: another starts after it.
    stream.on_data = [&](std::string_view data) {
        pending.append(data);
        const auto pieces = study::splitGames(pending);
        if (pieces.size() >= 2) {
            for (std::size_t i = 0; i + 1 < pieces.size(); ++i)
                if (!take(pieces[i])) return false;
            pending.erase(0, static_cast<std::size_t>(pieces.back().data() - pending.data()));
        }
        return !cancel;
    };
    stream.cancel     = &cancel;
    stream.idle_s     = 60;
    const auto result = politeStream(request, stream);

    report.cancelled = cancel;
    const bool whole = status == 200 && result.error.empty() && !report.cancelled && refused.empty();
    if (whole) {
        for (const auto piece : study::splitGames(pending))
            if (!take(piece)) break;
    }
    commit();
    if (!refused.empty()) {
        report.error = "the database could not take the games: " + refused;
    } else if (status != 200 && !report.cancelled) {
        report.error = status == 0     ? (result.error.empty() ? std::string("Lichess did not answer") : result.error)
                       : status == 404 ? fmt::format("Lichess has no account named {}", username)
                       : status == 429 ? std::string("Lichess asks to wait a minute: too many requests")
                                       : fmt::format("Lichess answered {}", status);
    } else if (!whole && !report.cancelled) {
        report.error = result.error.empty() ? std::string("the games stopped coming") : result.error;
    }
    return report;
}

Answer<uint64_t> countNewGames(const std::filesystem::path& database, const std::string& username,
                               const std::string& token, const SyncFilters& filters, const std::atomic<bool>* cancel,
                               uint64_t most) {
    Answer<uint64_t> out;
    const auto state = loadSyncState(database);
    if (!state) {
        out.error = "the database holds no Lichess games";
        return out;
    }
    // The games after the latest one held: from its start on, itself left out (it comes again within the minute).
    HttpRequest request;
    request.url = exportUrl(username, filters, state->since + 1000, false);
    request.url.replace(request.url.find("max=1000"), 8, fmt::format("max={}", most));
    request.method    = "GET";
    request.token     = token;
    request.timeout_s = 30;
    request.headers.emplace_back("Accept", "application/x-ndjson");
    uint64_t lines = 0;
    int status     = 0;
    HttpStream stream;
    stream.on_head = [&](int s, std::string_view) {
        status = s;
        return s == 200;
    };
    stream.on_data = [&](std::string_view data) {
        lines += static_cast<uint64_t>(std::count(data.begin(), data.end(), '\n'));
        return !(cancel && *cancel);
    };
    stream.cancel     = cancel;
    stream.idle_s     = 30;
    const auto result = politeStream(request, stream);
    out.status        = status;
    if (status == 200 && result.error.empty())
        out.value = lines;
    else
        out.error = status == 0 ? (result.error.empty() ? std::string("Lichess did not answer") : result.error)
                                : fmt::format("Lichess answered {}", status);
    return out;
}

}  // namespace fcg::lichess
