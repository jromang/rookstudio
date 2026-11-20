#include "twic.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <unordered_map>

#include <json.hpp>

namespace fcg::twic {

namespace {

// A cell's text: its tags taken out, its spaces trimmed.
std::string cellText(std::string_view cell) {
    std::string out;
    bool in_tag = false;
    for (const char c : cell) {
        if (c == '<')
            in_tag = true;
        else if (c == '>')
            in_tag = false;
        else if (!in_tag)
            out += c;
    }
    const auto first = out.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    return out.substr(first, out.find_last_not_of(" \t\r\n") - first + 1);
}

bool allDigits(std::string_view s) {
    return !s.empty() && std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isdigit(c) != 0; });
}

bool isDate(std::string_view s) {
    return s.size() == 10 && s[4] == '-' && s[7] == '-' && allDigits(s.substr(0, 4)) && allDigits(s.substr(5, 2)) &&
           allDigits(s.substr(8, 2));
}

}  // namespace

std::vector<Issue> parseIndex(std::string_view html) {
    std::vector<Issue> out;
    // Each row of its table: the issue's number, its Monday, its links (HTML, PGN, CBV), its games, its stories.
    std::size_t at = 0;
    while ((at = html.find("<tr", at)) != std::string_view::npos) {
        const auto end = html.find("</tr>", at);
        const auto row = html.substr(at, end == std::string_view::npos ? std::string_view::npos : end - at);
        at             = end == std::string_view::npos ? html.size() : end;
        std::vector<std::string> cells;
        std::size_t c = 0;
        while ((c = row.find("<td", c)) != std::string_view::npos) {
            const auto open  = row.find('>', c);
            const auto close = row.find("</td>", c);
            if (open == std::string_view::npos || close == std::string_view::npos || close < open) break;
            cells.push_back(cellText(row.substr(open + 1, close - open - 1)));
            c = close + 5;
        }
        if (cells.size() < 6 || !allDigits(cells[0]) || !isDate(cells[1])) continue;
        Issue issue;
        issue.number = std::stoi(cells[0].substr(0, 9));
        issue.date   = cells[1];
        if (allDigits(cells[5])) issue.games = static_cast<uint32_t>(std::stoul(cells[5].substr(0, 9)));
        if (row.find("twic" + cells[0] + "g.zip") == std::string_view::npos) continue;  // no PGN to download
        out.push_back(std::move(issue));
    }
    std::sort(out.begin(), out.end(), [](const Issue& a, const Issue& b) { return a.number > b.number; });
    out.erase(std::unique(out.begin(), out.end(), [](const Issue& a, const Issue& b) { return a.number == b.number; }),
              out.end());
    return out;
}

namespace {
std::string& server() {
    static std::string base = "https://theweekinchess.com";
    return base;
}
}  // namespace

void setServer(std::string base) { server() = std::move(base); }

std::string indexUrl() { return server() + "/twic"; }

std::string pgnFile(int number) { return "twic" + std::to_string(number) + "g.zip"; }

std::string pgnUrl(int number) { return server() + "/zips/" + pgnFile(number); }

std::string nextMonday(const std::string& date) {
    if (!isDate(date)) return {};
    using namespace std::chrono;
    const year_month_day ymd{year{std::stoi(date.substr(0, 4))},
                             month{static_cast<unsigned>(std::stoi(date.substr(5, 2)))},
                             day{static_cast<unsigned>(std::stoi(date.substr(8, 2)))}};
    if (!ymd.ok()) return {};
    const year_month_day next{sys_days{ymd} + days{7}};
    char text[16];
    std::snprintf(text, sizeof text, "%04d-%02u-%02u", static_cast<int>(next.year()),
                  static_cast<unsigned>(next.month()), static_cast<unsigned>(next.day()));
    return text;
}

Report reportOf(const study::Snapshot& snapshot, study::GameId from, study::GameId to, std::size_t most) {
    struct EventSum {
        uint64_t rated = 0, sum = 0, games = 0;
        uint32_t first = 0, last = 0;
    };
    std::unordered_map<std::string, EventSum> events;
    std::unordered_map<std::string, Report::Player> players;
    to = std::min<study::GameId>(to, static_cast<study::GameId>(snapshot.games()));
    for (study::GameId id = from; id < to; ++id) {
        if (snapshot.deleted(id)) continue;
        const auto row = snapshot.row(id);
        auto& e        = events[std::string(row.event)];
        ++e.games;
        for (const auto elo : {row.white_elo, row.black_elo})
            if (elo > 0) {
                ++e.rated;
                e.sum += elo;
            }
        if (row.date) {
            e.first = e.first ? std::min(e.first, row.date) : row.date;
            e.last  = std::max(e.last, row.date);
        }
        for (const auto& [name, elo] : {std::pair{row.white, row.white_elo}, std::pair{row.black, row.black_elo}}) {
            if (name.empty()) continue;
            auto& p = players[std::string(name)];
            p.name  = name;
            p.elo   = std::max<int>(p.elo, elo);
            ++p.games;
        }
    }
    Report r;
    for (auto& [name, e] : events) {
        if (name.empty() || name == "?" || e.rated == 0) continue;
        r.events.push_back({name, static_cast<int>(e.sum / e.rated), e.first, e.last, e.games});
    }
    std::sort(r.events.begin(), r.events.end(), [](const Report::Event& a, const Report::Event& b) {
        return a.average != b.average ? a.average > b.average : a.name < b.name;
    });
    if (r.events.size() > most) r.events.resize(most);
    for (auto& [name, p] : players)
        if (p.elo > 0) r.players.push_back(p);
    std::sort(r.players.begin(), r.players.end(), [](const Report::Player& a, const Report::Player& b) {
        return a.elo != b.elo ? a.elo > b.elo : a.name < b.name;
    });
    if (r.players.size() > most) r.players.resize(most);
    return r;
}

std::string toJson(const State& state) {
    nlohmann::json j;
    j["weekly"]          = state.weekly;
    j["database"]        = state.database;
    nlohmann::json added = nlohmann::json::object();
    for (const auto& [folder, numbers] : state.added) added[folder] = numbers;
    j["added"]        = added;
    j["checked"]      = state.checked;
    j["newest"]       = state.newest;
    j["newest_games"] = state.newest_games;
    j["newest_date"]  = state.newest_date;
    return j.dump(2);
}

State stateFromJson(std::string_view text) {
    State s;
    try {
        const auto j = nlohmann::json::parse(text);
        s.weekly     = j.value("weekly", false);
        s.database   = j.value("database", std::string());
        if (const auto it = j.find("added"); it != j.end() && it->is_object())
            for (const auto& [folder, numbers] : it->items())
                if (numbers.is_array())
                    for (const auto& n : numbers)
                        if (n.is_number_integer()) s.added[folder].insert(n.get<int>());
        s.checked      = j.value("checked", int64_t{0});
        s.newest       = j.value("newest", 0);
        s.newest_games = j.value("newest_games", uint32_t{0});
        s.newest_date  = j.value("newest_date", std::string());
    } catch (const std::exception&) {
        return {};
    }
    return s;
}

std::optional<Issue> newIssue(const State& state, const std::string& database) {
    const auto it = state.added.find(database);
    if (it == state.added.end() || it->second.empty() || state.newest <= *it->second.rbegin()) return std::nullopt;
    return Issue{state.newest, state.newest_date, state.newest_games};
}

bool lookDue(const State& state, int64_t now) { return state.weekly && now - state.checked >= 6 * 3600; }

}  // namespace fcg::twic
