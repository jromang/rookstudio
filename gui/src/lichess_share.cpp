#include "lichess_share.hpp"

#include <fmt/include/fmt/format.h>
#include <json.hpp>

#include "https.hpp"
#include "i18n.hpp"

namespace fcg::lichess {

namespace {

// A form posted to Lichess's API, without an account (an import, an open challenge).
Response post(const std::string& path, const Form& fields, const std::atomic<bool>* cancel) {
    return send(withForm(request("POST", servers().api + path), fields), cancel);
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
    return readAnswer<std::string>(post("/api/import", {{"pgn", pgn}}, cancel),
                                   [](const nlohmann::json& j) -> std::optional<std::string> {
                                       if (const auto url = textOf(j, "url"); !url.empty()) return url;
                                       if (const auto id = textOf(j, "id"); !id.empty())
                                           return fmt::format("{}/game/{}", servers().api, id);
                                       return std::nullopt;
                                   });
}

std::string clockText(const ChallengeClock& clock) {
    if (clock.days > 0) return trn("{} day a move", "{} days a move", clock.days, clock.days);
    return fmt::format("{}+{}", clock.minutes, clock.increment);
}

Answer<OpenChallenge> openChallenge(const std::string& fen, bool chess960, const ChallengeClock& clock,
                                    const std::atomic<bool>* cancel) {
    Form fields = {{"fen", fen}, {"variant", chess960 ? "chess960" : "standard"}, {"rated", "false"}};
    if (clock.days > 0) {
        fields.emplace_back("days", std::to_string(clock.days));
    } else {
        fields.emplace_back("clock.limit", std::to_string(clock.minutes * 60));
        fields.emplace_back("clock.increment", std::to_string(clock.increment));
    }
    return readAnswer<OpenChallenge>(post("/api/challenge/open", fields, cancel),
                                     [](nlohmann::json j) -> std::optional<OpenChallenge> {
                                         if (j.is_object() && j.contains("challenge") && j["challenge"].is_object())
                                             j = j["challenge"];  // as some answers wrap it
                                         if (!j.is_object()) return std::nullopt;
                                         OpenChallenge c;
                                         c.id    = j.value("id", "");
                                         c.url   = j.value("url", "");
                                         c.white = j.value("urlWhite", "");
                                         c.black = j.value("urlBlack", "");
                                         if (c.white.empty() || c.black.empty()) return std::nullopt;  // no links
                                         return c;
                                     });
}

Answer<bool> bookmarkGame(const std::string& id, const std::string& token, bool on, const std::atomic<bool>* cancel) {
    const auto r = send(
        request("POST", fmt::format("{}/bookmark/{}?v={}", servers().api, id, on ? "true" : "false"), token), cancel);
    auto out = failedWith<bool>(r);
    if (!r.failed()) out.value = on;
    return out;
}

}  // namespace fcg::lichess
