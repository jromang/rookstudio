#include "chesscom.hpp"

#include <algorithm>
#include <cctype>

#include <fmt/include/fmt/format.h>
#include <json.hpp>

#include "https.hpp"
#include "i18n.hpp"

namespace fcg::chesscom {

namespace {
std::string& server() {
    static std::string base = "https://api.chess.com";
    return base;
}
}  // namespace

void setServer(std::string base) { server() = std::move(base); }

std::string playerKey(std::string_view name) {
    std::string key;
    for (const char c : name) {
        const auto u = static_cast<unsigned char>(c);
        if (std::isspace(u)) continue;
        if (!std::isalnum(u) && c != '-' && c != '_') return {};
        key += static_cast<char>(std::tolower(u));
    }
    return key.size() > 50 ? std::string() : key;
}

std::string archivesUrl(const std::string& player) { return server() + "/pub/player/" + player + "/games/archives"; }

std::string monthPgnUrl(const std::string& player, const std::string& month) {
    return server() + "/pub/player/" + player + "/games/" + month + "/pgn";
}

std::vector<std::string> parseArchives(std::string_view json) {
    std::vector<std::string> out;
    try {
        const auto j = nlohmann::json::parse(json);
        if (!j.contains("archives") || !j["archives"].is_array()) return {};
        for (const auto& url : j["archives"]) {
            if (!url.is_string()) continue;
            const auto text = url.get<std::string>();
            // ".../games/2026/10": its last two parts.
            const auto last = text.rfind('/');
            if (last == std::string::npos || last < 5) continue;
            const auto before = text.rfind('/', last - 1);
            if (before == std::string::npos) continue;
            const auto month = text.substr(before + 1);
            if (month.size() == 7 && month[4] == '/' && std::all_of(month.begin(), month.end(), [](char c) {
                    return c == '/' || std::isdigit(static_cast<unsigned char>(c));
                }))
                out.push_back(month);
        }
    } catch (const std::exception&) {
        return {};
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

std::string toJson(const std::vector<Followed>& players) {
    nlohmann::json list = nlohmann::json::array();
    for (const auto& p : players) list.push_back({{"name", p.name}, {"database", p.database}, {"months", p.months}});
    return nlohmann::json{{"players", list}}.dump(2);
}

std::vector<Followed> fromJson(std::string_view text) {
    std::vector<Followed> out;
    try {
        const auto j = nlohmann::json::parse(text);
        if (!j.contains("players") || !j["players"].is_array()) return {};
        for (const auto& p : j["players"]) {
            Followed f;
            f.name     = p.value("name", std::string());
            f.database = p.value("database", std::string());
            if (p.contains("months") && p["months"].is_array())
                for (const auto& m : p["months"])
                    if (m.is_string()) f.months.insert(m.get<std::string>());
            if (!f.name.empty()) out.push_back(std::move(f));
        }
    } catch (const std::exception&) {
        return {};
    }
    return out;
}

std::vector<std::string> monthsToAsk(const std::vector<std::string>& listed, const std::set<std::string>& added) {
    std::vector<std::string> out;
    for (const auto& m : listed)
        if (!added.count(m)) out.push_back(m);
    if (!added.empty()) {
        const auto& newest = *added.rbegin();
        if (std::find(listed.begin(), listed.end(), newest) != listed.end() &&
            std::find(out.begin(), out.end(), newest) == out.end())
            out.insert(std::lower_bound(out.begin(), out.end(), newest), newest);
    }
    return out;
}

std::pair<std::vector<std::string>, NetFailure> fetchMonths(const std::string& player,
                                                            const std::atomic<bool>* cancel) {
    HttpRequest request;
    request.url    = archivesUrl(player);
    request.method = "GET";
    request.headers.emplace_back("Accept", "application/json");
    std::pair<std::vector<std::string>, NetFailure> out;
    const auto answer = politeRequest(request, cancel);
    out.second        = netFailureOf(answer, request.url);
    if (out.second.kind == NetFailure::Kind::None) out.first = parseArchives(answer.body);
    return out;
}

std::string monthsSaid(const NetFailure& failure) {
    if (failure.kind == NetFailure::Kind::NotFound) return TR("chess.com knows no player of that name.");
    if (auto own = siteSentence(failure, "chess.com"); !own.empty()) return own;
    return fmt::format("HTTP {}", failure.status);
}

}  // namespace fcg::chesscom
