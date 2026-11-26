#include "game_analysis.hpp"

#include <algorithm>
#include <cmath>

#include <fmt/include/fmt/format.h>

#include "storage.hpp"

namespace fcg {

double winChance(const fastchess::Score& score) {
    if (score.type == fastchess::ScoreType::MATE) return score.value > 0 ? 100.0 : 0.0;
    const double cp = std::clamp(static_cast<double>(score.value), -10000.0, 10000.0);
    return 50.0 + 50.0 * (2.0 / (1.0 + std::exp(-0.00368208 * cp)) - 1.0);
}

Judgement judge(double before, double after) {
    const double loss = before - after;
    if (loss >= 30.0) return Judgement::Blunder;
    if (loss >= 20.0) return Judgement::Mistake;
    if (loss >= 10.0) return Judgement::Inaccuracy;
    return Judgement::None;
}

const char* judgementMark(Judgement judgement) {
    switch (judgement) {
        case Judgement::Inaccuracy:
            return "?!";
        case Judgement::Mistake:
            return "?";
        case Judgement::Blunder:
            return "??";
        case Judgement::None:
            break;
    }
    return "";
}

std::optional<fastchess::Score> endScore(const chess::Board& board) {
    const auto [reason, result] = board.isGameOver();
    if (result == chess::GameResult::NONE) return std::nullopt;
    if (reason == chess::GameResultReason::CHECKMATE) return fastchess::Score{fastchess::ScoreType::MATE, 0};
    return fastchess::Score{fastchess::ScoreType::CP, 0};
}

std::vector<MoveReview> reviewMoves(const ReplayGame& game, bool chess960, const GameReview& review) {
    std::vector<MoveReview> out(game.moves.size());
    for (std::size_t i = 0; i < game.moves.size() && i + 1 < review.positions.size(); ++i) {
        const auto& before = review.positions[i];
        const auto& after  = review.positions[i + 1];
        if (!before.score || !after.score) continue;
        auto& m = out[i];
        // The mover's chances: the position before is its to move, the one after its opponent's.
        const double was = winChance(*before.score);
        const double is  = 100.0 - winChance(*after.score);
        m.loss           = std::max(0.0, was - is);
        m.best           = !before.best.empty() && before.best == chess::uci::moveToUci(game.moves[i].move, chess960);
        if (!m.best) m.judgement = judge(was, is);
    }
    return out;
}

std::array<std::array<int, 3>, 2> judgementCounts(const ReplayGame& game, const std::vector<MoveReview>& moves) {
    std::array<std::array<int, 3>, 2> counts{};
    for (std::size_t i = 0; i < moves.size() && i < game.moves.size(); ++i) {
        const auto j = moves[i].judgement;
        if (j == Judgement::None) continue;
        const int side = game.moves[i].mover == chess::Color::WHITE ? 0 : 1;
        counts[static_cast<std::size_t>(side)][static_cast<std::size_t>(j) - 1]++;
    }
    return counts;
}

std::string reviewKey(const ReplayGame& game) {
    // FNV-1a over the start position and the moves.
    uint64_t hash   = 1469598103934665603ULL;
    const auto feed = [&](std::string_view text) {
        for (const char c : text) {
            hash ^= static_cast<unsigned char>(c);
            hash *= 1099511628211ULL;
        }
    };
    feed(game.start_fen.empty() ? std::string(chess::constants::STARTPOS) : game.start_fen);
    for (const auto& m : game.moves) {
        feed(" ");
        feed(chess::uci::moveToUci(m.move, true));
    }
    return fmt::format("{:016x}", hash);
}

namespace {

// A score and its win, draw and loss figures as an analysis file has them ("type", "value", "wdl").
std::optional<fastchess::Score> scoreFromJson(const nlohmann::json& p) {
    const auto type = p.value("type", "");
    if (type != "cp" && type != "mate") return std::nullopt;
    return fastchess::Score{type == "cp" ? fastchess::ScoreType::CP : fastchess::ScoreType::MATE,
                            p.value("value", int64_t{0})};
}

std::optional<std::array<int64_t, 3>> wdlFromJson(const nlohmann::json& p) {
    if (!p.contains("wdl") || !p["wdl"].is_array() || p["wdl"].size() != 3) return std::nullopt;
    return std::array<int64_t, 3>{p["wdl"][0].get<int64_t>(), p["wdl"][1].get<int64_t>(), p["wdl"][2].get<int64_t>()};
}

void scoreToJson(nlohmann::json& e, const fastchess::Score& score) {
    e["type"]  = score.type == fastchess::ScoreType::MATE ? "mate" : "cp";
    e["value"] = score.value;
}

GameReview reviewFromJson(const nlohmann::json& j) {
    GameReview review;
    review.engine      = j.value("engine", "");
    review.movetime_ms = j.value("movetime_ms", int64_t{0});
    review.depth       = j.value("depth", 0);
    review.passes      = j.value("passes", 0);
    review.nodes       = j.value("nodes", uint64_t{0});
    for (const auto& p : j.at("positions")) {
        PositionEval e;
        e.score   = scoreFromJson(p);
        e.best    = p.value("best", "");
        e.depth   = p.value("depth", 0);
        e.time_ms = p.value("time_ms", int64_t{0});
        e.pv      = p.value("pv", "");
        e.known   = p.value("known", false);
        e.wdl     = wdlFromJson(p);
        if (p.contains("second") && p["second"].is_object()) {
            const auto& s  = p["second"];
            e.second_score = scoreFromJson(s);
            e.second_pv    = s.value("pv", "");
            e.second_depth = s.value("depth", 0);
            e.second_wdl   = wdlFromJson(s);
        }
        review.positions.push_back(std::move(e));
    }
    if (j.contains("previous") && j["previous"].is_array())
        for (const auto& v : j["previous"]) review.previous.push_back(static_cast<int8_t>(v.get<int>()));
    return review;
}

nlohmann::json reviewToJson(const GameReview& review) {
    nlohmann::json j;
    j["engine"]      = review.engine;
    j["movetime_ms"] = review.movetime_ms;
    j["depth"]       = review.depth;
    j["passes"]      = review.passes;
    if (review.nodes > 0) j["nodes"] = review.nodes;
    j["positions"] = nlohmann::json::array();
    for (const auto& p : review.positions) {
        nlohmann::json e;
        if (p.score) scoreToJson(e, *p.score);
        e["best"]  = p.best;
        e["depth"] = p.depth;
        if (p.time_ms > 0) e["time_ms"] = p.time_ms;
        if (!p.pv.empty()) e["pv"] = p.pv;
        if (p.wdl) e["wdl"] = *p.wdl;
        if (p.known) e["known"] = true;
        if (p.second_score) {
            nlohmann::json s;
            scoreToJson(s, *p.second_score);
            s["pv"]    = p.second_pv;
            s["depth"] = p.second_depth;
            if (p.second_wdl) s["wdl"] = *p.second_wdl;
            e["second"] = std::move(s);
        }
        j["positions"].push_back(std::move(e));
    }
    if (!review.previous.empty()) {
        j["previous"] = nlohmann::json::array();
        for (const auto v : review.previous) j["previous"].push_back(static_cast<int>(v));
    }
    return j;
}

}  // namespace

std::vector<GameReview> loadReviews(const std::filesystem::path& dir, const ReplayGame& game) {
    const auto key = reviewKey(game);
    const auto j   = readJson<nlohmann::json>(dir / (key + ".json"));
    if (!j || !j->is_object()) return {};
    std::vector<GameReview> out;
    try {
        // Its key read with the rest: of another type than a text, the file goes unread as for any other error in it
        // (#136), rather than stopping the program.
        if (j->value("key", "") != key) return {};
        // Several analyses, or one as the first versions kept it.
        if (j->contains("analyses")) {
            for (const auto& a : j->at("analyses")) out.push_back(reviewFromJson(a));
        } else {
            out.push_back(reviewFromJson(*j));
        }
    } catch (...) {
        return {};
    }
    std::erase_if(out, [&](const GameReview& r) { return r.positions.size() > game.moves.size() + 1; });
    return out;
}

bool saveReviews(const std::filesystem::path& dir, const ReplayGame& game, const std::vector<GameReview>& reviews) {
    std::error_code ec;
    const auto file = dir / (reviewKey(game) + ".json");
    if (reviews.empty()) {
        std::filesystem::remove(file, ec);
        return true;
    }
    nlohmann::json j;
    j["key"]      = reviewKey(game);
    j["analyses"] = nlohmann::json::array();
    for (const auto& r : reviews) j["analyses"].push_back(reviewToJson(r));
    std::filesystem::create_directories(dir, ec);
    return writeJson(file, j);
}

std::optional<GameReview> loadReview(const std::filesystem::path& dir, const ReplayGame& game) {
    auto all = loadReviews(dir, game);
    if (all.empty()) return std::nullopt;
    return std::move(all.front());
}

bool saveReview(const std::filesystem::path& dir, const ReplayGame& game, const GameReview& review) {
    auto all = loadReviews(dir, game);
    // The same engine's analysis replaced in its place, another one after the others.
    const auto it =
        std::find_if(all.begin(), all.end(), [&](const GameReview& r) { return r.engine == review.engine; });
    if (it != all.end())
        *it = review;
    else
        all.push_back(review);
    return saveReviews(dir, game, all);
}

}  // namespace fcg
