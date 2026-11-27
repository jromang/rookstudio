#include "rounds.hpp"

#include <algorithm>

#include "journal_events.hpp"
#include "pairing.hpp"

namespace fcg::tour {

namespace {

bool between(const Encounter& e, int a, int b) {
    return (e.first == a && e.second == b) || (e.first == b && e.second == a);
}

}  // namespace

void Rounds::addEncounters(const nlohmann::json& event, const TournamentSpec& spec, Schedule& schedule) {
    const auto type  = eventTypeOf(event);
    const auto round = event.at("round").get<uint32_t>();
    const int cycles = spec.format == Format::Knockout ? std::max(1, spec.cycles) : 1;

    std::vector<Encounter> encounters;
    const auto add = [&](int a, int b, uint32_t cycle) {
        Encounter e;
        e.round  = round;
        e.cycle  = cycle;
        e.first  = cycle % 2 == 1 ? a : b;
        e.second = cycle % 2 == 1 ? b : a;
        encounters.push_back(e);
    };
    if (type == EventType::RoundPaired) {
        for (const auto& p : event.at("pairs")) {
            for (int c = 1; c <= cycles; ++c) add(p.at(0).get<int>(), p.at(1).get<int>(), static_cast<uint32_t>(c));
        }
    } else {
        const auto& p = event.at("pair");
        add(p.at(0).get<int>(), p.at(1).get<int>(), static_cast<uint32_t>(cycles + event.at("index").get<int>()));
    }
    schedule.append(encounters);
}

void Rounds::apply(const nlohmann::json& event) {
    if (eventTypeOf(event) != EventType::RoundPaired) return;
    round_ = event.at("round").get<uint32_t>();
    for (const auto& id : event.value("byes", std::vector<int>{})) byes_[id]++;
    if (event.contains("bracket")) brackets_[round_] = event.at("bracket").get<std::vector<std::vector<int>>>();
}

double Rounds::pairPoints(int a, int b, uint32_t round) const {
    double points = 0;
    for (const auto& r : source_.results) {
        if (r.voided) continue;
        const bool between = (r.white == a && r.black == b) || (r.white == b && r.black == a);
        if (between && source_.schedule.at(r.encounter) && source_.schedule.at(r.encounter)->round == round)
            points += r.scoreOf(a);
    }
    return points;
}

int Rounds::tiebreaks(int a, int b) const {
    int n           = 0;
    const auto size = source_.schedule.size().value_or(0);
    for (uint32_t e = 0; e < size; ++e) {
        const auto enc = source_.schedule.at(e);
        if (enc->round == round_ && between(*enc, a, b) &&
            enc->cycle > static_cast<uint32_t>(std::max(1, source_.spec.cycles))) {
            ++n;
        }
    }
    return n;
}

bool Rounds::placeOver(int a, int b) const {
    const auto size = source_.schedule.size().value_or(0);
    for (uint32_t e = 0; e < size; ++e) {
        const auto enc = source_.schedule.at(e);
        if (enc->round != round_ || !between(*enc, a, b)) continue;
        for (int g = 0; g < 2; ++g) {
            const auto n = uint64_t{e} * 2 + g;
            if (!source_.done.count(n) && !source_.dropped.count(n)) return false;
        }
    }
    return true;
}

std::optional<int> Rounds::placeWinner(int a, int b) const {
    const double sa = pairPoints(a, b, round_), sb = pairPoints(b, a, round_);
    if (sa != sb) return sa > sb ? a : b;
    if (tiebreaks(a, b) < 10) return std::nullopt;

    // Ten tiebreaks without a winner: the higher seed goes through.
    const auto seed = [&](int id) {
        const auto& ps = source_.spec.participants;
        return std::find_if(ps.begin(), ps.end(), [&](const Participant& p) { return p.id == id; }) - ps.begin();
    };
    return seed(a) <= seed(b) ? a : b;
}

std::vector<nlohmann::json> Rounds::next() {
    // The round being played must be over: every game of it played (or dropped).
    const auto size = source_.schedule.size().value_or(0);
    for (uint32_t e = 0; e < size; ++e) {
        if (source_.schedule.at(e)->round != round_) continue;
        for (int g = 0; g < 2; ++g) {
            const auto n = uint64_t{e} * 2 + g;
            if (!source_.done.count(n) && !source_.dropped.count(n)) return {};
        }
    }

    std::vector<int> players;
    for (const auto& p : source_.spec.participants) {
        if (p.active) players.push_back(p.id);
    }

    auto event           = journalEvent(EventType::RoundPaired, {{"round", round_ + 1}});
    const auto pairsJson = [](const RoundPairing& p) {
        nlohmann::json pairs = nlohmann::json::array();
        for (const auto& [a, b] : p.pairs) pairs.push_back({a, b});
        return pairs;
    };

    if (source_.spec.format == Format::Swiss) {
        if (round_ >= static_cast<uint32_t>(source_.spec.rounds)) return {};
        std::map<int, double> score;
        for (const auto& r : source_.results) {
            if (r.voided) continue;
            score[r.white] += r.scoreOf(r.white);
            score[r.black] += r.scoreOf(r.black);
        }
        std::set<int> had_bye;
        for (const auto& [id, n] : byes_) {
            score[id] += n;
            had_bye.insert(id);
        }
        std::set<std::pair<int, int>> played;
        for (uint32_t e = 0; e < size; ++e) {
            const auto enc = source_.schedule.at(e);
            played.insert({std::min(enc->first, enc->second), std::max(enc->first, enc->second)});
        }
        const auto pairing = swissPairing(players, score, played, had_bye);
        event["pairs"]     = pairsJson(pairing);
        event["byes"]      = pairing.byes;
    } else if (round_ == 0) {
        const auto pairing = knockoutFirstRound(players);
        event["pairs"]     = pairsJson(pairing);
        event["byes"]      = pairing.byes;
        event["bracket"]   = pairing.bracket;
    } else {
        // Who won each place of the round; tied matches get another pair of games first, all at once.
        std::vector<int> winners;
        std::vector<nlohmann::json> tiebreaks_added;
        for (const auto& place : brackets_[round_]) {
            if (place.size() == 1) {
                winners.push_back(place[0]);
                continue;
            }
            const int a = place[0], b = place[1];
            if (const auto winner = placeWinner(a, b)) {
                winners.push_back(*winner);
                continue;
            }
            tiebreaks_added.push_back(journalEvent(
                EventType::TiebreakAdded, {{"round", round_}, {"pair", {a, b}}, {"index", tiebreaks(a, b) + 1}}));
        }
        if (!tiebreaks_added.empty()) return tiebreaks_added;  // the round goes on
        if (winners.size() < 2) return {};                     // the final is over

        std::vector<std::vector<int>> bracket;
        nlohmann::json pairs = nlohmann::json::array();
        for (std::size_t i = 0; i + 1 < winners.size(); i += 2) {
            bracket.push_back({winners[i], winners[i + 1]});
            pairs.push_back({winners[i], winners[i + 1]});
        }
        event["pairs"]   = pairs;
        event["bracket"] = bracket;
    }

    return {event};
}

}  // namespace fcg::tour
