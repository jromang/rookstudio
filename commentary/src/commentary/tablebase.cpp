#include "tablebase.hpp"

#include <mutex>

namespace fcg::commentary {

std::string Tablebases::key(const Position& pos) {
    // The EPD: the FEN without its move counters.
    const auto fen = pos.fen();
    std::size_t at = 0;
    for (int spaces = 0; at < fen.size(); ++at) {
        if (fen[at] == ' ' && ++spaces == 4) break;
    }
    return fen.substr(0, at);
}

std::optional<Tablebases::Lesson> Tablebases::lesson(const Position& pos) const {
    std::shared_lock lock(mutex_);
    const auto it = learnt_.find(key(pos));
    if (it == learnt_.end()) return std::nullopt;
    return it->second;
}

bool Tablebases::onDisk(const Position& pos) const {
    return probe_ && pos.board().occupied().count() <= probe_->max_men;
}

bool Tablebases::wants(const Position& pos) const {
    const int men = pos.board().occupied().count();
    const auto l  = lesson(pos);
    return men <= TAUGHT_MEN && pos.castles().isEmpty() && !pos.isGameOver() && !onDisk(pos) && !(l && l->whole);
}

void Tablebases::learn(const Position& pos, Verdict verdict, std::span<const std::pair<Move, Verdict>> moves) {
    std::unique_lock lock(mutex_);
    Lesson whole;
    whole.verdict = verdict;
    if (!moves.empty()) whole.best = moves.front().first;
    whole.whole       = true;
    learnt_[key(pos)] = whole;
    for (const auto& [mv, v] : moves) {
        auto after = pos;
        after.playUnchecked(mv);
        auto [it, fresh]   = learnt_.try_emplace(key(after), Lesson{flip(v), std::nullopt, false});
        it->second.verdict = flip(v);
        (void)fresh;
    }
}

std::optional<Verdict> Tablebases::verdict(const Position& pos) const {
    if (!pos.castles().isEmpty()) return std::nullopt;
    if (onDisk(pos)) return probe_->wdl(pos);
    const auto l = lesson(pos);
    if (!l) return std::nullopt;
    return l->verdict;
}

std::optional<Move> Tablebases::best(const Position& pos) const {
    const auto l = lesson(pos);
    if (!l) return std::nullopt;
    return l->best;
}

std::optional<Verdict> Tablebases::after(const Position& before, const Move& mv) const {
    auto a = before;
    a.playUnchecked(mv);
    if (a.isCheckmate()) return Verdict::Win;
    if (a.isStalemate() || a.isInsufficientMaterial()) return Verdict::Draw;
    const auto v = verdict(a);
    if (!v) return std::nullopt;
    return flip(*v);
}

std::vector<Fact> Tablebases::moveFacts(const Position& before, const Move& mv) const {
    const Color side = before.turn();
    const auto now   = after(before, mv);
    if (!now) return {};
    std::vector<Fact> facts{Fact{fact::Tablebase{side, *now}}};
    const auto was = verdict(before);
    if (!was) return facts;
    if (*was == Verdict::Win && (*now == Verdict::Draw || *now == Verdict::Loss)) {
        facts.push_back(Fact{fact::SpoilsWin{side, *now}});
    } else if (*was == Verdict::Draw && *now == Verdict::Loss) {
        facts.push_back(Fact{fact::SpoilsDraw{side}});
    } else if ((*was == Verdict::Win || *was == Verdict::Draw) && *was == *now) {
        int keeping = 0;
        for (const auto& m : before.legalMoves()) keeping += after(before, m) == *was;
        if (keeping == 1) facts.push_back(Fact{fact::OnlyMoveToKeep{side, *was}});
    }
    return facts;
}

}  // namespace fcg::commentary
