#include "lichess_share.hpp"

#include <fmt/include/fmt/format.h>
#include <json.hpp>

#include "https.hpp"
#include "i18n.hpp"

namespace fcg::lichess {

namespace {

std::string refusal(const HttpResult& r) {
    return r.status == 0     ? (r.error.empty() ? std::string("Lichess did not answer") : r.error)
           : r.status == 401 ? std::string("Lichess refused the token")
           : r.status == 403 ? std::string("the token may not do it")
           : r.status == 429 ? std::string("Lichess asks to wait a minute: too many requests")
                             : fmt::format("Lichess answered {}", r.status);
}

// Lichess's reason, when its answer gives one: {"error": "..."}.
std::string reasonOf(const HttpResult& r) {
    const auto j = nlohmann::json::parse(r.body, nullptr, false);
    if (!j.is_discarded() && j.is_object()) {
        if (j.contains("error") && j["error"].is_string()) return j["error"].get<std::string>();
        if (j.contains("global") && j["global"].is_array() && !j["global"].empty() && j["global"][0].is_string())
            return j["global"][0].get<std::string>();
    }
    return {};
}

HttpResult post(const std::string& path, const std::string& token,
                const std::vector<std::pair<std::string, std::string>>& fields, const std::atomic<bool>* cancel) {
    HttpRequest request;
    request.url          = servers().api + path;
    request.method       = "POST";
    request.token        = token;
    request.content_type = "application/x-www-form-urlencoded";
    request.body         = formEncode(fields);
    request.headers.emplace_back("Accept", "application/json");
    return politeRequest(request, cancel);
}

}  // namespace

std::string analysisUrl(const std::string& fen, bool chess960, bool black_below) {
    std::string path = fen;
    for (auto& c : path)
        if (c == ' ') c = '_';
    return fmt::format("{}/analysis/{}/{}{}", servers().api, chess960 ? "chess960" : "standard", path,
                       black_below ? "?color=black" : "");
}

Answer<std::string> importGame(const std::string& pgn, const std::atomic<bool>* cancel) {
    Answer<std::string> out;
    const auto r = post("/api/import", {}, {{"pgn", pgn}}, cancel);
    out.status   = r.status;
    if (r.status != 200) {
        const auto reason = reasonOf(r);
        out.error         = reason.empty() ? refusal(r) : reason;
        return out;
    }
    const auto j = nlohmann::json::parse(r.body, nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
        out.error = "Lichess's answer could not be read";
        return out;
    }
    if (j.contains("url") && j["url"].is_string()) {
        out.value = j["url"].get<std::string>();
    } else if (j.contains("id") && j["id"].is_string()) {
        out.value = fmt::format("{}/game/{}", servers().api, j["id"].get<std::string>());
    } else {
        out.error = "Lichess's answer could not be read";
    }
    return out;
}

std::string clockText(const ChallengeClock& clock) {
    if (clock.days > 0) return trn("{} day a move", "{} days a move", clock.days, clock.days);
    return fmt::format("{}+{}", clock.minutes, clock.increment);
}

Answer<OpenChallenge> openChallenge(const std::string& fen, bool chess960, const ChallengeClock& clock,
                                    const std::atomic<bool>* cancel) {
    Answer<OpenChallenge> out;
    std::vector<std::pair<std::string, std::string>> fields = {
        {"fen", fen}, {"variant", chess960 ? "chess960" : "standard"}, {"rated", "false"}};
    if (clock.days > 0) {
        fields.emplace_back("days", std::to_string(clock.days));
    } else {
        fields.emplace_back("clock.limit", std::to_string(clock.minutes * 60));
        fields.emplace_back("clock.increment", std::to_string(clock.increment));
    }
    const auto r = post("/api/challenge/open", {}, fields, cancel);
    out.status   = r.status;
    if (r.status != 200) {
        const auto reason = reasonOf(r);
        out.error         = reason.empty() ? refusal(r) : reason;
        return out;
    }
    auto j = nlohmann::json::parse(r.body, nullptr, false);
    if (!j.is_discarded() && j.is_object() && j.contains("challenge") && j["challenge"].is_object())
        j = j["challenge"];  // as some answers wrap it
    if (j.is_discarded() || !j.is_object()) {
        out.error = "Lichess's answer could not be read";
        return out;
    }
    OpenChallenge c;
    c.id    = j.value("id", "");
    c.url   = j.value("url", "");
    c.white = j.value("urlWhite", "");
    c.black = j.value("urlBlack", "");
    if (c.white.empty() || c.black.empty()) {
        out.error = "Lichess's answer has no link for the players";
        return out;
    }
    out.value = std::move(c);
    return out;
}

Answer<bool> bookmarkGame(const std::string& id, const std::string& token, bool on, const std::atomic<bool>* cancel) {
    Answer<bool> out;
    HttpRequest request;
    request.url    = fmt::format("{}/bookmark/{}?v={}", servers().api, id, on ? "true" : "false");
    request.method = "POST";
    request.token  = token;
    const auto r   = politeRequest(request, cancel);
    out.status     = r.status;
    if (r.status < 200 || r.status >= 300) {
        out.error = refusal(r);
        return out;
    }
    out.value = on;
    return out;
}

}  // namespace fcg::lichess
