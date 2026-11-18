// The searches the user keeps (mockup M1, board 6): their criteria, not their games, for every database; in the
// profile's searches.json, in the search panel and in Ctrl+K.

#include <algorithm>
#include <cstdio>
#include <string>

#include <json.hpp>

#include "app.hpp"
#include "i18n.hpp"
#include "storage.hpp"

namespace fcg {

namespace {

template <std::size_t N>
std::string text(const std::array<char, N>& a) {
    return std::string(a.data());
}

template <std::size_t N>
void set(std::array<char, N>& a, const std::string& s) {
    a.fill(0);
    std::snprintf(a.data(), a.size(), "%s", s.c_str());
}

}  // namespace

nlohmann::json App::keptSearchJson(const KeptSearch& k) {
    const auto& f           = k.fields;
    nlohmann::json material = nlohmann::json::array();
    for (const auto& m : f.material) material.push_back(text(m));
    return {{"name", k.name},
            {"player", text(f.player)},
            {"side", f.side},
            {"opponent", text(f.opponent)},
            {"event", text(f.event)},
            {"year_from", text(f.year_from)},
            {"year_to", text(f.year_to)},
            {"eco_from", text(f.eco_from)},
            {"eco_to", text(f.eco_to)},
            {"elo_min", text(f.elo_min)},
            {"result", f.result ? std::string(1, f.result) : std::string()},
            {"position", f.position ? f.position_fen : std::string()},
            {"material", material},
            {"material_plies", f.material_plies},
            {"pattern", f.pattern},
            {"only_pawns", f.only_pawns},
            {"damaged", f.damaged}};
}

App::KeptSearch App::keptSearchFrom(const nlohmann::json& j) {
    KeptSearch k;
    k.name       = j.value("name", std::string());
    auto& f      = k.fields;
    const auto s = [&](const char* key) { return j.value(key, std::string()); };
    set(f.player, s("player"));
    f.side = std::clamp(j.value("side", 0), 0, 2);
    set(f.opponent, s("opponent"));
    set(f.event, s("event"));
    set(f.year_from, s("year_from"));
    set(f.year_to, s("year_to"));
    set(f.eco_from, s("eco_from"));
    set(f.eco_to, s("eco_to"));
    set(f.elo_min, s("elo_min"));
    const auto result = s("result");
    f.result          = result.empty() ? 0 : result[0];
    f.position_fen    = s("position");
    f.position        = !f.position_fen.empty();
    if (const auto m = j.find("material"); m != j.end() && m->is_array())
        for (std::size_t i = 0; i < f.material.size() && i < m->size(); ++i)
            set(f.material[i], (*m)[i].is_string() ? (*m)[i].get<std::string>() : std::string());
    f.material_plies = j.value("material_plies", 4);
    if (const auto p = j.find("pattern"); p != j.end() && p->is_array() && p->size() == 64)
        for (std::size_t i = 0; i < 64; ++i) f.pattern[i] = static_cast<int8_t>((*p)[i].get<int>());
    if (const auto o = j.find("only_pawns"); o != j.end() && o->is_array() && o->size() == 2)
        f.only_pawns = {(*o)[0].get<bool>(), (*o)[1].get<bool>()};
    f.damaged = j.value("damaged", false);
    return k;
}

void App::loadKeptSearches() {
    kept_searches_.clear();
    if (options_.scripted) return;  // the screenshots show the same panel whatever the profile keeps
    const auto json = readJson<nlohmann::json>(storage_.root() / "searches.json");
    if (!json || !json->is_array()) return;
    for (const auto& j : *json)
        if (j.is_object()) kept_searches_.push_back(keptSearchFrom(j));
}

void App::saveKeptSearches() {
    nlohmann::json all = nlohmann::json::array();
    for (const auto& k : kept_searches_) all.push_back(keptSearchJson(k));
    (void)writeJson(storage_.root() / "searches.json", all);
}

std::string App::keptSearchName(const SearchFields& f) const {
    // Made of its criteria, as the panel shows them: « Carlsen · B90–B99 · 2024 ».
    std::string name;
    const auto add = [&](const std::string& part) {
        if (!part.empty()) name += name.empty() ? part : " · " + part;
    };
    add(text(f.player) + (f.side == 1   ? std::string(" (") + TR("White") + ")"
                          : f.side == 2 ? std::string(" (") + TR("Black") + ")"
                                        : std::string()));
    if (!text(f.opponent).empty()) add(trf("against {}", text(f.opponent)));
    add(text(f.event));
    const auto from = text(f.year_from), to = text(f.year_to);
    add(from == to ? from : from.empty() ? "–" + to : to.empty() ? from + "–" : from + "–" + to);
    const auto eco_from = text(f.eco_from), eco_to = text(f.eco_to);
    add(eco_from == eco_to || eco_to.empty() ? eco_from : eco_from + "–" + eco_to);
    if (!text(f.elo_min).empty()) add("Elo " + text(f.elo_min) + "+");
    if (f.result) add(f.result == '1' ? "1-0" : f.result == '0' ? "0-1" : "½-½");
    if (f.position) add(TR("the position"));
    if (std::any_of(f.material.begin(), f.material.end(), [](const auto& m) { return m[0] != 0; }))
        add(TR("a material"));
    if (std::any_of(f.pattern.begin(), f.pattern.end(), [](int8_t v) { return v >= 0; })) add(TR("a pattern"));
    if (f.damaged) add(TR("damaged games"));
    return name.empty() ? std::string(TR("A search")) : name;
}

void App::applyKeptSearch(std::size_t index) {
    if (index >= kept_searches_.size()) return;
    db_search_ = kept_searches_[index].fields;
    db_filter_.fill(0);
    db_search_open_ = true;
    db_list_        = true;
    db_row_         = 0;
    keep_naming_    = false;
    // The tab of its first criterion.
    const auto& f    = db_search_;
    const bool heads = f.player[0] || f.opponent[0] || f.event[0] || f.year_from[0] || f.year_to[0] || f.eco_from[0] ||
                       f.eco_to[0] || f.elo_min[0] || f.result || f.damaged;
    search_tab_      = heads                                                                                        ? 0
                       : f.position                                                                                 ? 1
                       : std::any_of(f.material.begin(), f.material.end(), [](const auto& m) { return m[0] != 0; }) ? 2
                                                                                                                    : 3;
}

}  // namespace fcg
