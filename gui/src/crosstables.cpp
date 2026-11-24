#include "crosstables.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <unordered_map>

#include <fmt/include/fmt/format.h>
#include <json.hpp>

#include <study/import.hpp>
#include <study/players.hpp>
#include <study/query.hpp>

#include "i18n.hpp"
#include "storage.hpp"

namespace fcg {

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------------------------------------------------
// The settings

std::string crosstablePlace(const fs::path& database, const std::string& tournament) {
    const auto u8 = database.u8string();
    return std::string(u8.begin(), u8.end()) + "\n" + tournament;
}

std::string broadcastPlace(const std::string& tour_id) { return "lichess:" + tour_id; }

namespace {

nlohmann::json settingsJson(const CrosstableSettings& s) {
    nlohmann::json j = nlohmann::json::object();
    if (s.system) {
        j["system"] = s.system->system == study::TournamentSystem::Swiss ? "swiss" : "round robin";
        if (s.system->system == study::TournamentSystem::RoundRobin) j["cycles"] = s.system->cycles;
    }
    if (s.order) {
        auto list = nlohmann::json::array();
        for (const auto t : *s.order) list.push_back(std::string(study::tiebreakCode(t)));
        j["tiebreaks"] = std::move(list);
    }
    if (!s.hand.empty()) {
        auto list = nlohmann::json::array();
        for (const auto& h : s.hand) {
            nlohmann::json item{{"round", h.round}};
            if (h.black.empty()) {
                item["player"] = h.white;
                item["points"] = h.points;
            } else {
                item["white"]  = h.white;
                item["black"]  = h.black;
                item["result"] = h.result == '1' ? "1-0" : h.result == '0' ? "0-1" : "1/2-1/2";
            }
            list.push_back(std::move(item));
        }
        j["hand"] = std::move(list);
        if (!s.hand_from.empty()) j["hand_from"] = s.hand_from;
    }
    return j;
}

CrosstableSettings settingsFrom(const nlohmann::json& j) {
    CrosstableSettings s;
    if (!j.is_object()) return s;
    if (const auto it = j.find("system"); it != j.end() && it->is_string()) {
        const auto name = it->get<std::string>();
        if (name == "swiss") s.system = study::SystemChoice{study::TournamentSystem::Swiss, 1};
        if (name == "round robin")
            s.system =
                study::SystemChoice{study::TournamentSystem::RoundRobin, std::clamp(j.value("cycles", 1), 1, 16)};
    }
    if (const auto it = j.find("tiebreaks"); it != j.end() && it->is_array()) {
        std::vector<study::Tiebreak> order;
        for (const auto& t : *it)
            if (t.is_string())
                if (const auto known = study::tiebreakFromCode(t.get<std::string>())) order.push_back(*known);
        s.order = std::move(order);
    }
    if (const auto it = j.find("hand"); it != j.end() && it->is_array()) {
        for (const auto& item : *it) {
            if (!item.is_object()) continue;
            study::HandResult h;
            h.round = item.value("round", 0);
            if (item.contains("player")) {
                h.white  = item.value("player", std::string());
                h.points = item.value("points", 0.0);
            } else {
                h.white           = item.value("white", std::string());
                h.black           = item.value("black", std::string());
                const auto result = item.value("result", std::string());
                h.result          = result == "1-0" ? '1' : result == "0-1" ? '0' : result == "1/2-1/2" ? '=' : '*';
                if (h.black.empty() || h.result == '*') continue;
            }
            if (h.white.empty()) continue;
            s.hand.push_back(std::move(h));
        }
        if (const auto from = j.find("hand_from"); !s.hand.empty() && from != j.end() && from->is_string())
            s.hand_from = from->get<std::string>();
    }
    return s;
}

}  // namespace

CrosstableStore::CrosstableStore(fs::path profile) : file_(std::move(profile) / "crosstables.json") {
    const auto json = readJson<nlohmann::json>(file_);
    if (!json || !json->is_object()) return;
    if (const auto it = json->find("crosstables"); it != json->end() && it->is_object())
        for (const auto& [place, value] : it->items()) {
            auto s = settingsFrom(value);
            if (!s.empty()) settings_[place] = std::move(s);
        }
}

CrosstableSettings CrosstableStore::get(const std::string& place) const {
    const auto it = settings_.find(place);
    return it == settings_.end() ? CrosstableSettings{} : it->second;
}

void CrosstableStore::set(const std::string& place, const CrosstableSettings& settings) {
    if (settings.empty())
        settings_.erase(place);
    else
        settings_[place] = settings;
    save();
}

void CrosstableStore::save() const {
    nlohmann::json all = nlohmann::json::object();
    for (const auto& [place, s] : settings_) all[place] = settingsJson(s);
    (void)writeJson(file_, nlohmann::json{{"crosstables", std::move(all)}});
}

// ---------------------------------------------------------------------------------------------------------------------
// The words

std::string systemName(const study::SystemChoice& system) {
    switch (system.system) {
        case study::TournamentSystem::RoundRobin:
            if (system.cycles <= 1) return TR("round robin");
            if (system.cycles == 2) return TR("double round robin");
            return trf("round robin, {} cycles", system.cycles);
        case study::TournamentSystem::Swiss:
            return TR("Swiss system");  // within a sentence: "Played as Swiss system, as you chose"
        case study::TournamentSystem::Team:
            return TR("team event");
        case study::TournamentSystem::Knockout:
            return TR("knockout");
        case study::TournamentSystem::Unknown:
            break;
    }
    return TR("not known");
}

const char* tiebreakName(study::Tiebreak t) {
    using T = study::Tiebreak;
    switch (t) {
        case T::DirectEncounter:
            return TR("Direct encounter");
        case T::Wins:
            return TR("Wins");
        case T::GamesWon:
            return TR("Games won");
        case T::BlackGames:
            return TR("Games with Black");
        case T::BlackWins:
            return TR("Wins with Black");
        case T::Progressive:
            return TR("Progressive score");
        case T::Buchholz:
            return TR("Buchholz");
        case T::BuchholzCut1:
            return TR("Buchholz cut 1");
        case T::BuchholzMedian1:
            return TR("Median Buchholz");
        case T::SonnebornBerger:
            return TR("Sonneborn-Berger");
        case T::Koya:
            return TR("Koya");
        case T::AverageRating:
            return TR("Average rating of the opponents");
        case T::AverageRatingCut1:
            return TR("Average rating of the opponents, cut 1");
        case T::Performance:
            return TR("Performance");
    }
    return "";
}

const char* tiebreakHeading(study::Tiebreak t) {
    using T = study::Tiebreak;
    switch (t) {
        case T::DirectEncounter:
            return TR("DE");
        case T::Wins:
            return TR("Wins");
        case T::GamesWon:
            return TR("WON");
        case T::BlackGames:
            return TR("BPG");
        case T::BlackWins:
            return TR("BWG");
        case T::Progressive:
            return TR("PS");
        case T::Buchholz:
            return TR("BH");
        case T::BuchholzCut1:
            return TR("BH-C1");
        case T::BuchholzMedian1:
            return TR("BH-M1");
        case T::SonnebornBerger:
            return TR("SB");
        case T::Koya:
            return TR("KS");
        case T::AverageRating:
            return TR("ARO");
        case T::AverageRatingCut1:
            return TR("ARO-C1");
        case T::Performance:
            return TR("TPR");
    }
    return "";
}

const char* tiebreakHelp(study::Tiebreak t) {
    using T = study::Tiebreak;
    switch (t) {
        case T::DirectEncounter:
            return TR(
                "The points scored in the games between the players tied, when all of them met (in a Swiss, one "
                "is ranked first when the games they did not play could not change it).");
        case T::Wins:
            return TR("The rounds won, over the board or not: forfeits and byes worth a win count.");
        case T::GamesWon:
            return TR("The games won over the board.");
        case T::BlackGames:
            return TR("The games played with Black over the board.");
        case T::BlackWins:
            return TR("The games won with Black over the board.");
        case T::Progressive:
            return TR("The score after each round, added up.");
        case T::Buchholz:
            return TR(
                "The sum of the opponents' scores. In a Swiss, a round without a game counts as one against an "
                "opponent with the player's own score, at most half the rounds' points.");
        case T::BuchholzCut1:
            return TR("Buchholz without its lowest value (a round the player chose not to play goes first).");
        case T::BuchholzMedian1:
            return TR("Buchholz without its lowest and its highest values.");
        case T::SonnebornBerger:
            return TR("Each opponent's score times the points scored against them, added up.");
        case T::Koya:
            return TR("The points scored against the players who scored half the points or more.");
        case T::AverageRating:
            return TR("The average rating of the opponents met over the board.");
        case T::AverageRatingCut1:
            return TR(
                "The average rating of the opponents met over the board, without the lowest (nothing more cut "
                "for a player who had a round without a game).");
        case T::Performance:
            return TR(
                "The average rating of the opponents met over the board, plus the difference FIDE's table gives "
                "the score there.");
    }
    return "";
}

std::string pointsText(double points, int unknown) {
    const auto whole = static_cast<long>(std::floor(points + 1e-9));
    const bool half  = points - static_cast<double>(whole) > 0.25;
    auto text        = whole == 0 && half ? std::string("½") : fmt::format("{}{}", whole, half ? "½" : "");
    if (unknown > 0) text += "?";
    return text;
}

std::string tiebreakValue(study::Tiebreak t, double value, bool decimals) {
    if (t == study::Tiebreak::AverageRating || t == study::Tiebreak::AverageRatingCut1 ||
        t == study::Tiebreak::Performance)
        return fmt::format("{}", static_cast<long>(std::lround(value)));
    const double twice = value * 2;
    if (!decimals && std::abs(twice - std::round(twice)) < 1e-9) return pointsText(value);
    return fmt::format("{:.2f}", value);
}

std::string changeText(double change) {
    const double shown = std::round(change * 10) / 10;
    if (shown == 0) return "±0.0";
    return fmt::format("{}{:.1f}", shown > 0 ? "+" : "−", std::abs(shown));
}

std::string crossCellText(const study::Crosstable& table, const study::CrossEntry& e, bool swiss) {
    using K           = study::CellKind;
    const auto result = [&]() -> std::string {
        switch (e.kind) {
            case K::Forfeit:
                return e.points > 0.5 ? "+" : "−";
            case K::Missing:
                return "?";
            case K::Live:
                return "●";
            case K::ToCome:
                return "·";
            default:
                return e.points > 0.75 ? "1" : e.points > 0.25 ? "½" : "0";
        }
    };
    if (!swiss) return result();
    switch (e.kind) {
        case K::Bye:
            return TR("bye");
        case K::Absent:
            return e.unsure ? std::string("?") : e.hand ? pointsText(e.points) : std::string("–");
        case K::ToCome:
            return "·";
        default:
            break;
    }
    if (e.opponent == study::NO_PLAYER) return result();
    const auto* opponent = table.rowOf(e.opponent);
    const auto number    = opponent ? std::to_string(opponent->start) : std::string("?");
    if (e.kind == K::Missing) return number + "?";
    // A result set by hand for a game the database does not have: its colours not known.
    const bool colours = e.game >= table.games.size() || table.games[e.game].ref != UINT64_MAX;
    if (!colours) return number + "?" + result();
    // The colour's letter as each language writes it: w for White, b for Black in English ("4b½").
    return e.white ? trf("{}w{}", number, result()) : trf("{}b{}", number, result());
}

// ---------------------------------------------------------------------------------------------------------------------
// A followed broadcast's crosstable

LiveCrosstable liveCrosstableInput(const std::vector<lichess::BroadcastGame>& followed, const study::Snapshot* saved,
                                   std::span<const study::GameId> earlier) {
    LiveCrosstable out;
    std::map<std::string, uint32_t> by_name;
    const auto playerOf = [&](const std::string& name, int elo) -> uint32_t {
        if (study::byeName(name)) return study::NO_PLAYER;
        const auto [it, added] = by_name.emplace(name, static_cast<uint32_t>(out.players.size()));
        if (added) {
            study::CrossPlayer p;
            p.name = name;
            out.players.push_back(std::move(p));
        }
        auto& p = out.players[it->second];
        if (p.elo == 0 && elo > 0) p.elo = elo;
        return it->second;
    };
    // The rounds the broadcast still sends.
    std::vector<int> followed_rounds;
    for (const auto& g : followed) {
        study::ImportedGame game;
        const bool read = study::readImportedGame(g.pgn, game);
        study::CrossGame c;
        c.white       = playerOf(g.white, g.white_elo);
        c.black       = playerOf(g.black, g.black_elo);
        c.round       = study::readRound(g.round.empty() && read ? std::string() : g.round);
        c.plies       = read ? game.plies : 0;
        const auto& r = g.result;
        c.result      = r == "1-0" ? '1' : r == "0-1" ? '0' : (r == "1/2-1/2" || r == "½-½") ? '=' : '*';
        c.live        = c.result == '*';
        c.source      = 1;
        c.ref         = out.urls.size();
        if (read && out.event.empty())
            for (const auto& [tag, value] : game.tags)
                if (tag == "Event") out.event = value;
        if (c.round.round > 0 &&
            std::find(followed_rounds.begin(), followed_rounds.end(), c.round.round) == followed_rounds.end())
            followed_rounds.push_back(c.round.round);
        out.urls.push_back(g.url);
        out.pgns.push_back(g.pgn);
        out.games.push_back(c);
    }
    // The rounds kept, that no longer come.
    if (saved && !earlier.empty()) {
        const auto in = study::tournamentInput(*saved, earlier);
        std::vector<uint32_t> local(in.players.size(), study::NO_PLAYER);
        for (std::size_t p = 0; p < in.players.size(); ++p) local[p] = playerOf(in.players[p].name, in.players[p].elo);
        for (auto g : in.games) {
            if (g.round.round > 0 &&
                std::find(followed_rounds.begin(), followed_rounds.end(), g.round.round) != followed_rounds.end())
                continue;
            g.white  = g.white == study::NO_PLAYER ? study::NO_PLAYER : local[g.white];
            g.black  = g.black == study::NO_PLAYER ? study::NO_PLAYER : local[g.black];
            g.source = 0;
            out.games.push_back(g);
            ++out.saved;
        }
    }
    return out;
}

// ---------------------------------------------------------------------------------------------------------------------
// Ctrl+K

std::string tournamentLabel(const study::Snapshot& snapshot, const study::TournamentEntry& entry) {
    std::string label(snapshot.name(entry.event));
    if (label.empty()) label = entry.key.substr(0, entry.key.find('|'));
    const std::string site(snapshot.name(entry.site));
    if (!site.empty()) label = trf("{} · {}", label, site);
    // Its year when its event does not say it.
    const auto year = std::to_string(entry.ref.year_from);
    if (entry.ref.year_from > 0 && label.find(year) == std::string::npos) label = trf("{} · {}", label, year);
    return label;
}

std::vector<CrosstableName> crosstableNames(const std::vector<TournamentsOf>& lists, const std::string& words,
                                            std::size_t most) {
    std::vector<std::string> wanted;
    {
        const auto folded = study::foldText(words);
        std::size_t at    = 0;
        while (at < folded.size()) {
            auto end = folded.find(' ', at);
            if (end == std::string::npos) end = folded.size();
            if (end > at) wanted.push_back(folded.substr(at, end - at));
            at = end + 1;
        }
    }
    struct Hit {
        int elo = 0;
        CrosstableName name;
    };
    std::vector<Hit> hits;
    for (const auto& l : lists) {
        if (!l.list || !l.snapshot) continue;
        for (const auto& e : l.list->tournaments) {
            const auto label = tournamentLabel(*l.snapshot, e);
            const auto text  = study::foldText(label);
            if (!std::all_of(wanted.begin(), wanted.end(),
                             [&](const std::string& w) { return text.find(w) != std::string::npos; }))
                continue;
            hits.push_back({e.mean_elo, CrosstableName{l.folder, l.database, e.key, label}});
        }
    }
    std::stable_sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) { return a.elo > b.elo; });
    std::vector<CrosstableName> out;
    for (auto& h : hits) {
        if (out.size() >= most) break;
        out.push_back(std::move(h.name));
    }
    return out;
}

// ---------------------------------------------------------------------------------------------------------------------
// The games missing, looked for elsewhere

std::vector<FoundMissing> lookForMissing(const std::vector<MissingAsked>& asked,
                                         const std::vector<OtherDatabase>& others, const std::atomic<bool>& cancel) {
    std::vector<FoundMissing> out;
    if (asked.empty()) return out;
    struct Wanted {
        study::NameParts parts;
        std::string skeleton;
    };
    std::vector<std::pair<Wanted, Wanted>> pairs;
    for (const auto& a : asked) {
        Wanted w{study::nameParts(a.white), {}}, b{study::nameParts(a.black), {}};
        w.skeleton = study::nameSkeleton(w.parts.surname);
        b.skeleton = study::nameSkeleton(b.parts.surname);
        pairs.emplace_back(std::move(w), std::move(b));
    }
    const auto fits = [](const Wanted& w, const study::NameParts& parts) {
        if (w.parts.surname.empty() || parts.surname.empty()) return false;
        if (w.parts.surname != parts.surname && w.skeleton != study::nameSkeleton(parts.surname)) return false;
        return study::givenCompatible(w.parts.given, parts.given);
    };
    for (const auto& db : others) {
        if (!db.snapshot) continue;
        const auto& snap = *db.snapshot;
        // The names that can be each asked player's: a bit for each side of each game asked.
        std::vector<uint64_t> marks(snap.names(), 0);
        const auto limit = std::min<std::size_t>(asked.size(), 32);
        for (uint32_t id = 0; id < snap.names(); ++id) {
            if ((id & 0xFFF) == 0 && cancel.load(std::memory_order_relaxed)) return out;
            const auto name = snap.shownName(id);
            if (name.empty() || name.size() > 80) continue;
            const auto parts = study::nameParts(name);
            if (parts.surname.empty()) continue;
            for (std::size_t k = 0; k < limit; ++k) {
                if (fits(pairs[k].first, parts)) marks[id] |= uint64_t{1} << (2 * k);
                if (fits(pairs[k].second, parts)) marks[id] |= uint64_t{1} << (2 * k + 1);
            }
        }
        for (study::GameId id = 0; id < snap.games(); ++id) {
            if ((id & 0xFFFF) == 0 && cancel.load(std::memory_order_relaxed)) return out;
            if (snap.deleted(id)) continue;
            const auto names = snap.rowIds(id);
            const auto w     = names.white < marks.size() ? marks[names.white] : 0;
            const auto b     = names.black < marks.size() ? marks[names.black] : 0;
            if (!w || !b) continue;
            for (std::size_t k = 0; k < limit; ++k) {
                const auto first = uint64_t{1} << (2 * k), second = uint64_t{1} << (2 * k + 1);
                if (!((w & first) && (b & second)) && !((w & second) && (b & first))) continue;
                const auto numbers = snap.numbers(id);
                const int year     = static_cast<int>(numbers.date >> 9);
                if (asked[k].year > 0 && year > 0 && year != asked[k].year) continue;
                const auto round = snap.name(names.round);
                if (asked[k].round > 0 && study::readRound(round).round > 0 &&
                    study::readRound(round).round != asked[k].round)
                    continue;
                FoundMissing f;
                f.asked    = k;
                f.folder   = db.folder;
                f.database = db.name;
                f.id       = id;
                f.round    = std::string(round);
                f.white    = std::string(snap.shownName(names.white));
                f.black    = std::string(snap.shownName(names.black));
                f.result   = numbers.result;
                out.push_back(std::move(f));
            }
        }
    }
    return out;
}

study::ImportedGame missingGameCopy(study::ImportedGame game, const std::string& event, const std::string& site,
                                    int round, const std::string& white, const std::string& black) {
    // Its players in the colours it was played with: the names of the tournament put on them.
    std::string old_white, old_black;
    for (const auto& [tag, value] : game.tags) {
        if (tag == "White") old_white = value;
        if (tag == "Black") old_black = value;
    }
    const auto same = [](const std::string& a, const std::string& b) {
        const auto pa = study::nameParts(a), pb = study::nameParts(b);
        return !pa.surname.empty() && study::nameSkeleton(pa.surname) == study::nameSkeleton(pb.surname) &&
               study::givenCompatible(pa.given, pb.given);
    };
    const bool swapped = !same(old_white, white) && same(old_white, black);
    const auto put     = [&](const std::string& tag, const std::string& value) {
        if (value.empty()) return;
        for (auto& [k, v] : game.tags)
            if (k == tag) {
                v = value;
                return;
            }
        game.tags.emplace_back(tag, value);
    };
    put("Event", event);
    put("Site", site);
    put("White", swapped ? black : white);
    put("Black", swapped ? white : black);
    // Its round as the tournament counts it, when it gives none.
    std::string written;
    for (const auto& [k, v] : game.tags)
        if (k == "Round") written = v;
    if (round > 0 && study::readRound(written).round != round) put("Round", std::to_string(round));
    return game;
}

}  // namespace fcg
