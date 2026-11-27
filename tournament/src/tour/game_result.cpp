#include "game_result.hpp"

#include <algorithm>
#include <stdexcept>

#include "journal_events.hpp"

namespace fcg::tour {

double GameResult::scoreOf(int id) const noexcept {
    if (result == "1/2-1/2") return 0.5;
    const bool white_won = result == "1-0";
    if (id == white) return white_won ? 1.0 : 0.0;
    if (id == black) return white_won ? 0.0 : 1.0;
    return 0.0;
}

std::string setResultComment(const GameResult& r) {
    if (!r.set) return {};
    std::string out    = OPERATOR_REASON;
    const bool stopped = r.set->played_result == "*";
    if (stopped) out += " during the game";
    if (!r.set->why.empty()) out += ": " + r.set->why;
    if (!stopped) {
        out += " (as played: " + r.set->played_result;
        if (!r.set->played_reason.empty()) out += ", " + r.set->played_reason;
        out += ")";
    }
    return out;
}

std::string countedPgn(std::string text, const GameResult& r) {
    if (!r.set) return text;
    return withResult(std::move(text), r.result, OPERATOR_TERMINATION, setResultComment(r));
}

nlohmann::json gameResultJson(const GameResult& r) {
    auto j = journalEvent(EventType::GameFinished, {{"encounter", r.encounter},
                                                    {"game", r.game},
                                                    {"white", r.white},
                                                    {"black", r.black},
                                                    {"result", r.result},
                                                    {"termination", static_cast<int>(r.termination)},
                                                    {"reason", r.reason},
                                                    {"plies", r.plies},
                                                    {"ms", r.duration_ms},
                                                    {"pgn", {r.pgn.offset, r.pgn.length}},
                                                    {"cb", {r.pgn_chessbase.offset, r.pgn_chessbase.length}},
                                                    {"at", r.finished_at}});
    if (!r.pv_warnings.empty()) j["pv"] = r.pv_warnings;
    if (r.unbalanced != 0) j["unbalanced"] = r.unbalanced;
    if (r.set) {
        j["set"] = {{"why", r.set->why},
                    {"played", r.set->played_result},
                    {"termination", static_cast<int>(r.set->played_termination)},
                    {"reason", r.set->played_reason}};
    }
    return j;
}

GameResult gameResultFromJson(const nlohmann::json& j) {
    GameResult r;
    r.encounter     = j.at("encounter").get<uint32_t>();
    r.game          = j.at("game").get<int>();
    r.white         = j.at("white").get<int>();
    r.black         = j.at("black").get<int>();
    r.result        = j.at("result").get<std::string>();
    r.termination   = static_cast<fastchess::MatchTermination>(j.value("termination", 0));
    r.reason        = j.value("reason", std::string{});
    r.plies         = j.value("plies", 0u);
    r.duration_ms   = j.value("ms", 0u);
    r.pgn           = {j.at("pgn").at(0).get<uint64_t>(), j.at("pgn").at(1).get<uint32_t>()};
    r.pgn_chessbase = {j.at("cb").at(0).get<uint64_t>(), j.at("cb").at(1).get<uint32_t>()};
    r.finished_at   = j.value("at", int64_t{0});
    if (j.contains("pv")) j.at("pv").get_to(r.pv_warnings);
    r.unbalanced = std::clamp(j.value("unbalanced", 0), -1, 1);
    if (const auto it = j.find("set"); it != j.end() && it->is_object()) {
        ResultSet set;
        set.why                = it->value("why", std::string{});
        set.played_result      = it->value("played", std::string{"*"});
        set.played_termination = static_cast<fastchess::MatchTermination>(it->value("termination", 0));
        set.played_reason      = it->value("reason", std::string{});
        r.set                  = set;
    }
    if (r.game != 0 && r.game != 1) throw std::runtime_error("bad game number");
    return r;
}

}  // namespace fcg::tour
