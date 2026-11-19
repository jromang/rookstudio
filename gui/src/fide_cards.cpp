#include "fide_cards.hpp"

#include <cctype>
#include <chrono>
#include <fstream>

#include <fmt/include/fmt/format.h>
#include <json.hpp>
#include <tour/files.hpp>

#include "https.hpp"

namespace fcg::lichess {

namespace {

std::string refusal(const HttpResult& r) {
    return r.status == 0     ? (r.error.empty() ? std::string("Lichess did not answer") : r.error)
           : r.status == 404 ? std::string("Lichess has no such FIDE player")
           : r.status == 429 ? std::string("Lichess asks to wait a minute: too many requests")
                             : fmt::format("Lichess answered {}", r.status);
}

FideCard cardOf(const nlohmann::json& j) {
    FideCard c;
    const auto number = [&](const char* key) {
        const auto it = j.find(key);
        return it != j.end() && it->is_number_integer() ? it->get<int64_t>() : int64_t{0};
    };
    const auto text = [&](const char* key) {
        const auto it = j.find(key);
        return it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
    };
    c.id         = number("id");
    c.name       = text("name");
    c.title      = text("title");
    c.federation = text("federation");
    c.year       = static_cast<int>(number("year"));
    c.standard   = static_cast<int>(number("standard"));
    c.rapid      = static_cast<int>(number("rapid"));
    c.blitz      = static_cast<int>(number("blitz"));
    c.inactive   = number("inactive") != 0;
    c.asked =
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    return c;
}

HttpResult ask(const std::string& url, const std::atomic<bool>* cancel) {
    HttpRequest request;
    request.url    = url;
    request.method = "GET";
    request.headers.emplace_back("Accept", "application/json");
    return politeRequest(request, cancel);
}

}  // namespace

Answer<FideCard> fidePlayer(int64_t id, const std::atomic<bool>* cancel) {
    Answer<FideCard> a;
    const auto r = ask(fmt::format("{}/api/fide/player/{}", servers().api, id), cancel);
    a.status     = r.status;
    if (r.status != 200) {
        a.error = refusal(r);
        return a;
    }
    const auto j = nlohmann::json::parse(r.body, nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
        a.error = "Lichess's answer could not be read";
        return a;
    }
    a.value = cardOf(j);
    return a;
}

Answer<std::vector<FideCard>> fideSearch(const std::string& name, const std::atomic<bool>* cancel) {
    Answer<std::vector<FideCard>> a;
    const auto r = ask(fmt::format("{}/api/fide/player?q={}", servers().api, urlEncode(name)), cancel);
    a.status     = r.status;
    if (r.status != 200) {
        a.error = refusal(r);
        return a;
    }
    const auto j = nlohmann::json::parse(r.body, nullptr, false);
    if (j.is_discarded() || !j.is_array()) {
        a.error = "Lichess's answer could not be read";
        return a;
    }
    std::vector<FideCard> found;
    for (const auto& p : j)
        if (p.is_object()) found.push_back(cardOf(p));
    a.value = std::move(found);
    return a;
}

std::string fideNameKey(std::string_view name) {
    std::string out;
    for (const char c : name) {
        const auto u = static_cast<unsigned char>(c);
        if (std::isalnum(u))
            out += static_cast<char>(std::tolower(u));
        else if (u >= 0x80)
            out += c;  // a letter beyond ASCII, kept as it is
    }
    return out;
}

std::optional<FideCard> fideMatch(const std::vector<FideCard>& found, std::string_view name) {
    const auto key = fideNameKey(name);
    std::optional<FideCard> match;
    for (const auto& card : found) {
        if (fideNameKey(card.name) != key) continue;
        if (match) return std::nullopt;  // two of that name: the user chooses
        match = card;
    }
    return match;
}

std::string fideUrl(const FideCard& card) {
    std::string slug;
    for (const char c : card.name) {
        if (c == ',') continue;
        slug += c == ' ' ? '_' : c;
    }
    return fmt::format("https://lichess.org/fide/{}/{}", card.id, urlEncode(slug));
}

FideCards::FideCards(std::filesystem::path file) : file_(std::move(file)) {
    std::ifstream in(file_, std::ios::binary);
    if (!in) return;
    const auto j = nlohmann::json::parse(in, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return;
    if (const auto cards = j.find("cards"); cards != j.end() && cards->is_array()) {
        for (const auto& c : *cards) {
            if (!c.is_object()) continue;
            auto card  = cardOf(c);
            card.asked = c.value("asked", int64_t{0});
            if (card.id > 0) cards_[card.id] = std::move(card);
        }
    }
    if (const auto names = j.find("names"); names != j.end() && names->is_object())
        for (const auto& [name, id] : names->items())
            if (id.is_number_integer()) names_[name] = id.get<int64_t>();
}

const FideCard* FideCards::byName(std::string_view name) const {
    const auto it = names_.find(fideNameKey(name));
    return it == names_.end() ? nullptr : byId(it->second);
}

const FideCard* FideCards::byId(int64_t id) const {
    const auto it = cards_.find(id);
    return it == cards_.end() ? nullptr : &it->second;
}

void FideCards::keep(const FideCard& card, std::string_view name) {
    if (card.id <= 0) return;
    cards_[card.id] = card;
    if (!name.empty()) names_[fideNameKey(name)] = card.id;
}

void FideCards::save() const {
    nlohmann::json cards = nlohmann::json::array();
    for (const auto& [id, c] : cards_)
        cards.push_back({{"id", c.id},
                         {"name", c.name},
                         {"title", c.title},
                         {"federation", c.federation},
                         {"year", c.year},
                         {"standard", c.standard},
                         {"rapid", c.rapid},
                         {"blitz", c.blitz},
                         {"inactive", c.inactive ? 1 : 0},
                         {"asked", c.asked}});
    nlohmann::json names = nlohmann::json::object();
    for (const auto& [name, id] : names_) names[name] = id;
    std::error_code ec;
    std::filesystem::create_directories(file_.parent_path(), ec);
    tour::replaceFile(file_, nlohmann::json{{"cards", cards}, {"names", names}}.dump());
}

}  // namespace fcg::lichess
