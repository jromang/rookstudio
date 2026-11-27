#include "incidents.hpp"

#include <algorithm>
#include <fstream>
#include <string>

#include <json.hpp>

#include "journal.hpp"
#include "journal_events.hpp"
#include "state.hpp"
#include "summary.hpp"

namespace fcg::tour {

namespace fs = std::filesystem;
using fastchess::MatchTermination;

namespace {

bool incident(MatchTermination termination) {
    return termination == MatchTermination::TIMEOUT || termination == MatchTermination::DISCONNECT ||
           termination == MatchTermination::STALL || termination == MatchTermination::ILLEGAL_MOVE;
}

// The participant (id, name) a journal or tournament.json line describes, if it does.
void addName(const nlohmann::json& participant, std::map<int, std::string>& names) {
    if (!participant.is_object() || !participant.contains("id")) return;
    const auto engine = participant.value("engine", nlohmann::json::object());
    if (engine.is_object() && engine.contains("name")) {
        names[participant.at("id").get<int>()] = engine.at("name").get<std::string>();
    }
}

}  // namespace

Incidents IncidentScanner::scan(const fs::path& root, int64_t since_ms, std::chrono::seconds window) {
    std::map<fs::path, Journal> kept;
    const auto now = fs::file_time_type::clock::now();
    for (const auto& dir : tournamentDirs(root)) {
        const auto file = dir / JOURNAL_FILE;
        std::error_code ec;
        const auto written = fs::last_write_time(file, ec);
        if (ec || now - written > window) continue;
        const auto size = fs::file_size(file, ec);
        if (ec) continue;

        Journal journal;
        if (const auto it = journals_.find(file); it != journals_.end()) journal = std::move(it->second);
        if (size < journal.read) journal = {};  // not the file read before

        // The participants' names, once: those added later are in the journal.
        if (!journal.named) {
            journal.named = true;
            try {
                std::ifstream in(dir / SPEC_FILE, std::ios::binary);
                const auto spec = nlohmann::json::parse(in);
                for (const auto& p : spec.value("participants", nlohmann::json::array())) addName(p, journal.names);
            } catch (const std::exception&) {
            }
        }

        // What was appended since the last scan, up to its last complete line. A line that does not read as its type
        // says is passed over.
        const auto line = [&](uint64_t, uint64_t, const nlohmann::json& event) {
            try {
                switch (eventTypeOf(event)) {
                    case EventType::ParticipantAdded:
                    case EventType::ParticipantReplaced:
                        addName(event.value("participant", nlohmann::json()), journal.names);
                        break;
                    case EventType::GamesVoided:
                        // The games an engine on a machine lost to the network: not its incidents.
                        if (!event.value("network", false)) break;
                        for (const auto& g : event.at("games")) {
                            const auto n = g.at(0).get<uint64_t>() * 2 + g.at(1).get<uint64_t>();
                            for (auto& f : journal.found) {
                                if (f.number == n) f.network = true;
                            }
                        }
                        break;
                    case EventType::GameFinished: {
                        const auto game = gameResultFromJson(event);
                        if (!incident(game.termination)) break;
                        Found found;
                        found.at     = game.finished_at;
                        found.how    = game.termination;
                        found.plies  = game.plies;
                        found.number = game.number();
                        // The side that lost; a draw (a timeout against a bare king) has no loser.
                        if (game.result == "1-0") found.loser = game.black;
                        if (game.result == "0-1") found.loser = game.white;
                        journal.found.push_back(found);
                        break;
                    }
                    default:
                        break;
                }
            } catch (const std::exception&) {
            }
        };
        if (size > journal.read) journal.read = tour::Journal::readFrom(file, journal.read, line);

        std::erase_if(journal.found, [&](const auto& f) { return f.at < since_ms; });
        kept.emplace(file, std::move(journal));
    }
    journals_ = std::move(kept);  // journals gone, or too old, are forgotten

    Incidents out;
    std::map<std::string, EngineIncidents> by_engine;
    std::map<std::string, int64_t> last;  // the time of each engine's last incident
    for (const auto& [file, journal] : journals_) {
        for (const auto& f : journal.found) {
            if (f.network) {
                ++out.network_cuts;
                continue;
            }
            const bool time  = f.how == MatchTermination::TIMEOUT;
            const bool crash = f.how == MatchTermination::DISCONNECT || f.how == MatchTermination::STALL;
            const bool move  = f.how == MatchTermination::ILLEGAL_MOVE;
            out.time_losses += time;
            out.crashes += crash;
            out.illegal_moves += move;

            const auto name = journal.names.find(f.loser);
            if (name == journal.names.end()) continue;
            auto& e  = by_engine[name->second];
            e.engine = name->second;
            e.time_losses += time;
            e.early_time_losses += time && f.plies <= EARLY_TIME_LOSS_PLIES;
            e.crashes += crash;
            e.illegal_moves += move;
            if (auto& when = last[e.engine]; e.dir.empty() || f.at >= when) {
                when  = f.at;
                e.dir = file.parent_path();
            }
        }
    }
    for (auto& [name, e] : by_engine) out.engines.push_back(std::move(e));
    std::stable_sort(out.engines.begin(), out.engines.end(),
                     [](const EngineIncidents& a, const EngineIncidents& b) { return a.total() > b.total(); });
    return out;
}

}  // namespace fcg::tour
