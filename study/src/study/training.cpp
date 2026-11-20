#include "training.hpp"

#include <algorithm>
#include <functional>
#include <map>
#include <utility>

#include "game_tree.hpp"
#include "scan_board.hpp"

namespace fcg::study {

namespace {

bool legal(const chess::Board& board, chess::Move move) {
    chess::Movelist list;
    chess::movegen::legalmoves(list, board);
    return std::find(list.begin(), list.end(), move) != list.end();
}

// What the repertoire says after each of its side's moves, by the position and the move's SAN: the first met. A
// variation's first move may carry its comment before it.
std::map<std::pair<uint64_t, std::string>, std::string> commentsOf(const GameTree& lines, chess::Color side,
                                                                   bool chess960) {
    std::map<std::pair<uint64_t, std::string>, std::string> out;
    for (int line = -1; line < static_cast<int>(lines.variations.size()); ++line) {
        const auto game = lineOf(lines, line);
        chess::Board board;
        board.set960(chess960);
        board.setFen(game.start_fen.empty() ? std::string(chess::constants::STARTPOS) : game.start_fen);
        for (const auto& m : game.moves) {
            if (m.move == chess::Move(chess::Move::NULL_MOVE) || !legal(board, m.move)) break;
            const auto& text = !m.notes.after.empty() ? m.notes.after : m.notes.before;
            if (board.sideToMove() == side && !text.empty()) {
                ScanBoard scan;
                if (scan.set(board)) out.try_emplace({scan.key(), chess::uci::moveToSan(board, m.move)}, text);
            }
            board.makeMove<false>(m.move);
        }
    }
    return out;
}

}  // namespace

TrainingPlan trainingPlan(const Repertoire& repertoire, const MovesAt& moves) {
    TrainingPlan plan;
    if (!moves) return plan;
    for (const auto& [key, pos] : repertoire.positions()) {
        ScanBoard scan;
        if (!scan.setFen(pos.fen, repertoire.chess960())) continue;
        const auto answer = moves(scan);
        if (!answer) continue;
        plan.games[key] = answer->total.games;
        if (pos.own) continue;
        auto& played = plan.played[key];
        for (const auto& m : answer->moves)
            for (const auto& r : pos.moves)
                if (r.code == m.code) played[m.code] = m.stats.games;
    }
    return plan;
}

std::vector<const RepertoireMove*> opponentOrder(const RepertoirePosition& position, const TrainingPlan& plan) {
    std::vector<const RepertoireMove*> order;
    for (const auto& m : position.moves) order.push_back(&m);
    const auto at = plan.played.find(position.key);
    if (at == plan.played.end()) return order;
    const auto games = [&](const RepertoireMove* m) -> uint64_t {
        const auto it = at->second.find(m->code);
        return it == at->second.end() ? 0 : it->second;
    };
    std::stable_sort(order.begin(), order.end(),
                     [&](const RepertoireMove* a, const RepertoireMove* b) { return games(a) > games(b); });
    return order;
}

std::vector<TrainingCard> trainingCards(const GameTree& lines, chess::Color side, const TrainingPlan& plan,
                                        bool chess960) {
    const Repertoire repertoire(lines, side, chess960);
    const auto comments = commentsOf(lines, side, chess960);
    std::vector<TrainingCard> cards;
    std::unordered_set<uint64_t> seen;
    std::vector<std::string> line;
    // Depth first from the start: the first line to reach a position is the one it is shown with.
    const std::function<void(uint64_t)> walk = [&](uint64_t key) {
        const auto* pos = repertoire.at(key);
        if (!pos || !seen.insert(key).second) return;
        std::vector<const RepertoireMove*> next;
        if (pos->own) {
            if (!pos->moves.empty()) {
                TrainingCard card;
                card.key  = key;
                card.fen  = pos->fen;
                card.line = line;
                for (const auto& m : pos->moves) {
                    card.moves.push_back(m.san);
                    if (const auto it = comments.find({key, m.san}); card.comment.empty() && it != comments.end())
                        card.comment = it->second;
                }
                if (const auto it = plan.games.find(key); it != plan.games.end()) card.games = it->second;
                cards.push_back(std::move(card));
            }
            for (const auto& m : pos->moves) next.push_back(&m);
        } else {
            next = opponentOrder(*pos, plan);
        }
        for (const auto* m : next) {
            line.push_back(m->san);
            walk(m->to);
            line.pop_back();
        }
    };
    walk(repertoire.start());
    return cards;
}

fsrs::Rating gradeOf(bool right, bool hint, double seconds) {
    if (!right) return fsrs::Rating::Again;
    return hint || seconds > HARD_AFTER ? fsrs::Rating::Hard : fsrs::Rating::Good;
}

std::string movesKey(std::vector<std::string> sans) {
    std::sort(sans.begin(), sans.end());
    std::string out;
    for (const auto& san : sans) out += (out.empty() ? "" : " ") + san;
    return out;
}

const CardMemory* memoryOf(const Memories& memories, const TrainingCard& card) {
    const auto it = memories.find(card.key);
    if (it == memories.end() || it->second.card.fresh() || it->second.moves != movesKey(card.moves)) return nullptr;
    return &it->second;
}

TrainingCounts trainingCounts(const std::vector<TrainingCard>& cards, const Memories& memories, const TrainingDay& day,
                              std::size_t new_per_day) {
    TrainingCounts out;
    out.cards         = cards.size();
    std::size_t begun = 0;
    for (const auto& card : cards) {
        const auto* m = memoryOf(memories, card);
        if (!m) {
            ++out.fresh;
            continue;
        }
        if (m->since > day.end - fsrs::DAY) ++begun;
        const auto due = m->card.due;
        if (due <= day.end) {
            ++out.due;
            continue;
        }
        ++out.known;
        if (due <= day.end + fsrs::DAY) ++out.tomorrow;
        if (due <= day.end + 3 * fsrs::DAY) ++out.three_days;
        if (due <= day.end + 7 * fsrs::DAY) ++out.week;
    }
    out.today = std::min(out.fresh, new_per_day > begun ? new_per_day - begun : 0);
    return out;
}

std::vector<std::size_t> sessionOrder(const std::vector<TrainingCard>& cards, const Memories& memories,
                                      const TrainingDay& day, std::size_t fresh, bool with_due,
                                      const std::unordered_set<uint64_t>& left_out) {
    std::vector<std::size_t> due, unseen;
    for (std::size_t i = 0; i < cards.size(); ++i) {
        if (left_out.contains(cards[i].key)) continue;
        const auto* m = memoryOf(memories, cards[i]);
        if (!m)
            unseen.push_back(i);
        else if (with_due && m->card.due <= day.end)
            due.push_back(i);
    }
    // The new ones the most met first; those the databases do not know after, in the walk's order.
    std::stable_sort(unseen.begin(), unseen.end(),
                     [&](std::size_t a, std::size_t b) { return cards[a].games > cards[b].games; });
    if (unseen.size() > fresh) unseen.resize(fresh);
    due.insert(due.end(), unseen.begin(), unseen.end());
    return due;
}

TrainingSession::TrainingSession(std::vector<TrainingCard> cards, std::vector<std::size_t> order, Memories memories,
                                 fsrs::Scheduler scheduler)
    : cards_(std::move(cards)), memories_(std::move(memories)), scheduler_(std::move(scheduler)) {
    for (const auto i : order)
        if (i < cards_.size()) queue_.push_back(i);
    total_ = queue_.size();
}

std::optional<std::size_t> TrainingSession::current() const {
    if (queue_.empty()) return std::nullopt;
    return queue_.front();
}

bool TrainingSession::askedAgain() const { return !queue_.empty() && answered_.contains(queue_.front()); }

std::size_t TrainingSession::asked() const {
    return answered_.size() + (!queue_.empty() && !answered_.contains(queue_.front()) ? 1 : 0);
}

bool TrainingSession::right(const std::string& san) const {
    if (queue_.empty()) return false;
    const auto& moves = cards_[queue_.front()].moves;
    return std::find(moves.begin(), moves.end(), san) != moves.end();
}

TrainingSession::Graded TrainingSession::grade(fsrs::Rating rating, const std::string& played, int64_t now) {
    Graded out;
    if (queue_.empty()) return out;
    const auto index = queue_.front();
    const auto& card = cards_[index];
    const auto* was  = memoryOf(memories_, card);
    if (was) out.before = *was;
    out.after       = out.before;
    out.after.moves = movesKey(card.moves);
    if (!was) out.after.since = now;
    // The same review, the same spread of its interval.
    const uint64_t seed = card.key ^ (static_cast<uint64_t>(out.before.card.reps) * 0x9E3779B97F4A7C15ull);
    out.after.card      = scheduler_.review(was ? was->card : fsrs::Card{}, rating, now, seed);
    memories_[card.key] = out.after;
    out.again           = out.after.card.due - now <= AGAIN_WITHIN;
    if (answered_.insert(index).second) outcomes_.push_back({index, rating, played});
    back_.reset();
    if (out.again)
        back_ = index;
    else
        ++done_;
    return out;
}

void TrainingSession::next() {
    if (queue_.empty()) return;
    queue_.pop_front();
    if (back_) {
        const auto at = std::min(AGAIN_AFTER, queue_.size());
        queue_.insert(queue_.begin() + static_cast<std::ptrdiff_t>(at), *back_);
        back_.reset();
    }
}

void TrainingSession::addMove(std::size_t card, const std::string& san) {
    if (card >= cards_.size()) return;
    auto& c        = cards_[card];
    const auto was = movesKey(c.moves);
    if (std::find(c.moves.begin(), c.moves.end(), san) != c.moves.end()) return;
    c.moves.push_back(san);
    if (const auto it = memories_.find(c.key); it != memories_.end() && it->second.moves == was)
        it->second.moves = movesKey(c.moves);
}

void TrainingSession::takeBack(std::size_t card, const std::string& san) {
    if (card >= cards_.size()) return;
    auto& c        = cards_[card];
    const auto was = movesKey(c.moves);
    const auto it  = std::find(c.moves.begin(), c.moves.end(), san);
    if (it == c.moves.end()) return;
    c.moves.erase(it);
    if (const auto m = memories_.find(c.key); m != memories_.end() && m->second.moves == was)
        m->second.moves = movesKey(c.moves);
}

}  // namespace fcg::study
