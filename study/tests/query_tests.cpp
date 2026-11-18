#include <doctest/doctest.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <random>
#include <string>
#include <vector>

#include <study/import.hpp>
#include <study/query.hpp>
#include <study/store.hpp>

using namespace fcg::study;
namespace fs = std::filesystem;

namespace {

// Games of random players, events, dates, ratings, openings and results: enough to tell filters and sorts apart.
std::vector<ImportedGame> randomGames(std::size_t n, uint32_t seed) {
    std::mt19937 random(seed);
    const std::vector<std::string> first{"Magnus",  "Hikaru", "Fabiano", "René",  "Ding",
                                         "Alireza", "Anna",   "Judit",   "Bobby", "Garry"};
    const std::vector<std::string> last{"Carlsen",  "Nakamura", "Caruana", "Lévy",    "Liren",
                                        "Firouzja", "Muzychuk", "Polgar",  "Fischer", "Kasparov"};
    std::vector<ImportedGame> games(n);
    for (std::size_t i = 0; i < n; ++i) {
        auto& g        = games[i];
        const auto who = [&] { return last[random() % last.size()] + ", " + first[random() % first.size()]; };
        g.tags.emplace_back("Event", "Open " + std::to_string(random() % 20));
        g.tags.emplace_back("Site", random() % 2 ? "Wijk aan Zee" : "St. Louis");
        g.tags.emplace_back("Date", std::to_string(1990 + random() % 37) + ".0" + std::to_string(1 + random() % 9) +
                                        ".1" + std::to_string(random() % 10));
        g.tags.emplace_back("Round", std::to_string(1 + random() % 13));
        g.tags.emplace_back("White", who());
        g.tags.emplace_back("Black", who());
        const char results[] = {'1', '0', '=', '*'};
        g.result             = results[random() % 4];
        if (random() % 5) g.tags.emplace_back("WhiteElo", std::to_string(2000 + random() % 900));
        if (random() % 5) g.tags.emplace_back("BlackElo", std::to_string(2000 + random() % 900));
        if (random() % 3)
            g.tags.emplace_back("ECO", std::string(1, static_cast<char>('A' + random() % 5)) +
                                           std::to_string(random() % 10) + std::to_string(random() % 10));
        g.plies     = static_cast<uint32_t>(random() % 150);
        g.annotated = random() % 7 == 0;
        g.moves.assign(g.plies, 0);
        g.signature = GameSignature{};  // moves of no game: what they would tell the searches is not looked at
        g.text = g.annotated ? "{a comment}" : "";
    }
    return games;
}

// The same, by reading every row and sorting with the standard library: what makeView must give.
std::vector<GameId> naive(const Snapshot& snap, const Filter& f, SortBy sort, bool descending) {
    const auto has = [](std::string_view name, const std::string& words) {
        const auto folded = foldText(name);
        std::size_t at    = 0;
        const auto w      = foldText(words);
        while (at < w.size()) {
            auto end = w.find(' ', at);
            if (end == std::string::npos) end = w.size();
            if (end > at && folded.find(w.substr(at, end - at)) == std::string::npos) return false;
            at = end + 1;
        }
        return true;
    };
    std::vector<GameId> ids;
    for (GameId id = 0; id < snap.games(); ++id) {
        const auto r = snap.row(id);
        if (f.result && r.result != f.result) continue;
        if (f.annotated && !(r.flags & GAME_ANNOTATED)) continue;
        if (f.elo_min && (r.white_elo < f.elo_min || r.black_elo < f.elo_min)) continue;
        if ((f.date_from || f.date_to) &&
            (r.date == 0 || (f.date_from && r.date < f.date_from) || (f.date_to && r.date > f.date_to)))
            continue;
        if ((f.eco_from || f.eco_to) &&
            (r.eco == 0 || (f.eco_from && r.eco < f.eco_from) || (f.eco_to && r.eco > f.eco_to)))
            continue;
        if (!f.white.empty() && !has(r.white, f.white)) continue;
        if (!f.black.empty() && !has(r.black, f.black)) continue;
        if (!f.player.empty() && !has(r.white, f.player) && !has(r.black, f.player)) continue;
        if (!f.words.empty()) {
            bool all = true;
            for (std::size_t at = 0; at < f.words.size();) {
                auto end = f.words.find(' ', at);
                if (end == std::string::npos) end = f.words.size();
                const auto word = f.words.substr(at, end - at);
                if (!word.empty() && !has(r.white, word) && !has(r.black, word) && !has(r.event, word) &&
                    !has(r.site, word))
                    all = false;
                at = end + 1;
            }
            if (!all) continue;
        }
        ids.push_back(id);
    }
    if (sort == SortBy::Number) {
        if (descending) std::reverse(ids.begin(), ids.end());
        return ids;
    }
    const auto key = [&](GameId id) -> std::string {
        const auto r   = snap.row(id);
        const auto pad = [](uint32_t v) {
            auto s = std::to_string(v);
            return std::string(12 - s.size(), '0') + s;
        };
        switch (sort) {
            case SortBy::White:
                return foldText(r.white) + '\1' + std::string(r.white);
            case SortBy::Black:
                return foldText(r.black) + '\1' + std::string(r.black);
            case SortBy::Event:
                return foldText(r.event) + '\1' + std::string(r.event);
            case SortBy::Site:
                return foldText(r.site) + '\1' + std::string(r.site);
            case SortBy::Round:
                return foldText(r.round) + '\1' + std::string(r.round);
            case SortBy::Date:
                return pad(r.date);
            case SortBy::WhiteElo:
                return pad(r.white_elo);
            case SortBy::BlackElo:
                return pad(r.black_elo);
            case SortBy::Eco:
                return pad(r.eco);
            case SortBy::Plies:
                return pad(r.plies);
            default:
                return pad(static_cast<unsigned char>(r.result));
        }
    };
    std::stable_sort(ids.begin(), ids.end(),
                     [&](GameId a, GameId b) { return descending ? key(a) > key(b) : key(a) < key(b); });
    return ids;
}

}  // namespace

TEST_CASE("query: views equal a naive filter and sort, whatever the cores") {
    const auto dir = fs::temp_directory_path() / "query.fcgdb";
    fs::remove_all(dir);
    auto db = Database::create(dir);
    db->append(randomGames(3000, 1), true);
    db->append(randomGames(2000, 2), true);  // two segments, names shared
    const auto snap = db->snapshot();
    REQUIRE(snap->games() == 5000);

    std::vector<Filter> filters(9);
    filters[1].words     = "carlsen";
    filters[2].player    = "rene levy";  // accents left out
    filters[3].white     = "Polgar Judit";
    filters[3].result    = '1';
    filters[4].date_from = packDate("2000.01.01");
    filters[4].date_to   = packDate("2010.12.31");
    filters[5].elo_min   = 2500;
    filters[6].eco_from  = packEco("B20");
    filters[6].eco_to    = packEco("B99");
    filters[7].annotated = true;
    filters[8].words     = "wijk open 1";
    filters[8].black     = "fischer";

    for (std::size_t f = 0; f < filters.size(); ++f) {
        for (const auto sort : {SortBy::Number, SortBy::White, SortBy::Black, SortBy::Event, SortBy::Round,
                                SortBy::Date, SortBy::WhiteElo, SortBy::Eco, SortBy::Plies, SortBy::Result}) {
            for (const bool descending : {false, true}) {
                CAPTURE(f);
                CAPTURE(static_cast<int>(sort));
                CAPTURE(descending);
                const auto expected = naive(*snap, filters[f], sort, descending);
                CHECK(makeView(snap, filters[f], sort, descending, 1).ids == expected);
                CHECK(makeView(snap, filters[f], sort, descending, 4).ids == expected);
            }
        }
    }
    CHECK(makeView(snap, filters[1], SortBy::Number).ids.size() > 100);
    CHECK(!makeView(snap, filters[2], SortBy::Number).ids.empty());
    std::atomic<bool> cancel{true};
    CHECK(makeView(snap, filters[1], SortBy::White, false, 2, &cancel).ids.empty());
    CHECK(foldText("Ébène, Zoë") == "ebene, zoe");
}

TEST_CASE("query: a million games filtered and sorted in a fraction of a second") {
    const auto dir = fs::temp_directory_path() / "query-big.fcgdb";
    fs::remove_all(dir);
    auto db = Database::create(dir);
    for (uint32_t part = 0; part < 4; ++part) db->append(randomGames(250000, 10 + part), true);
    const auto snap = db->snapshot();
    REQUIRE(snap->games() == 1000000);
    Filter carlsen;
    carlsen.words       = "carlsen";
    const auto t0       = std::chrono::steady_clock::now();
    const auto all      = makeView(snap, Filter{}, SortBy::Number);
    const auto t1       = std::chrono::steady_clock::now();
    const auto filtered = makeView(snap, carlsen, SortBy::Number);
    const auto t2       = std::chrono::steady_clock::now();
    const auto by_white = makeView(snap, Filter{}, SortBy::White);
    const auto t3       = std::chrono::steady_clock::now();
    const auto by_date  = makeView(snap, Filter{}, SortBy::Date, true);
    const auto t4       = std::chrono::steady_clock::now();
    const auto ms       = [](auto a, auto b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
    MESSAGE("1M games: all " << ms(t0, t1) << " ms, a player " << ms(t1, t2) << " ms (" << filtered.ids.size()
                             << "), by White " << ms(t2, t3) << " ms, by date " << ms(t3, t4) << " ms");
    CHECK(all.ids.size() == 1000000);
    CHECK(by_white.ids.size() == 1000000);
    CHECK(snap->row(by_date.ids.front()).date >= snap->row(by_date.ids.back()).date);
}
