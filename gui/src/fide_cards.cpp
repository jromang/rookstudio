#include "fide_cards.hpp"

#include <cctype>
#include <chrono>

#include <fmt/include/fmt/format.h>
#include <json.hpp>
#include <tour/files.hpp>

#include "https.hpp"
#include "profile_entries.hpp"

namespace fcg::lichess {

namespace {

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

}  // namespace

Answer<FideCard> fidePlayer(int64_t id, const std::atomic<bool>* cancel) {
    auto a = call<FideCard>(
        request("GET", fmt::format("{}/api/fide/player/{}", servers().api, id)),
        [](const nlohmann::json& j) { return j.is_object() ? std::optional<FideCard>(cardOf(j)) : std::nullopt; },
        cancel);
    a.asked = Asked::FidePlayer;
    return a;
}

Answer<std::vector<FideCard>> fideSearch(const std::string& name, const std::atomic<bool>* cancel) {
    return call<std::vector<FideCard>>(
        request("GET", fmt::format("{}/api/fide/player?q={}", servers().api, urlEncode(name))),
        [](const nlohmann::json& j) -> std::optional<std::vector<FideCard>> {
            if (!j.is_array()) return std::nullopt;
            std::vector<FideCard> found;
            for (const auto& p : j)
                if (p.is_object()) found.push_back(cardOf(p));
            return found;
        },
        cancel);
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
    // Read with its types checked (#136): a card with a value of another type than written (another version, a hand
    // edit) is set aside, the others kept, said in the console with the file as it was kept beside it; a file cut short
    // is kept aside and said. A card set aside is asked of Lichess again when it is wanted.
    const auto j = readProfileJson(file_);
    if (!j) return;
    ProfileEntries entries(file_);
    if (const auto cards = j->find("cards"); cards != j->end() && cards->is_array()) {
        for (std::size_t i = 0; i < cards->size(); ++i) {
            const auto& c = (*cards)[i];
            if (!c.is_object()) continue;
            entries.entry(fmt::format("cards/{}", i + 1), [&] {
                auto card  = cardOf(c);
                card.asked = c.value("asked", int64_t{0});
                if (card.id > 0) cards_[card.id] = std::move(card);
            });
        }
    }
    if (const auto names = j->find("names"); names != j->end() && names->is_object())
        for (const auto& [name, id] : names->items())
            if (id.is_number_integer()) names_[name] = id.get<int64_t>();
    entries.sayUnread();
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
