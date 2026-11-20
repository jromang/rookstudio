#include "review_queue.hpp"

#include <algorithm>
#include <cmath>

namespace fcg {

const std::vector<ReviewKind>& reviewKinds() {
    static const std::vector<ReviewKind> kinds = {ReviewKind::Mistakes, ReviewKind::Repertoire, ReviewKind::Puzzles,
                                                  ReviewKind::Endgames};
    return kinds;
}

std::size_t ReviewDay::due() const {
    std::size_t n = 0;
    for (const auto& k : kinds) n += k.due;
    return n;
}

std::size_t ReviewDay::tomorrow() const {
    std::size_t n = 0;
    for (const auto& k : kinds) n += k.tomorrow;
    return n;
}

std::size_t ReviewDay::threeDays() const {
    std::size_t n = 0;
    for (const auto& k : kinds) n += k.three_days;
    return n;
}

int64_t ReviewDay::minutes() const {
    double ms = 0;
    for (const auto& k : kinds) ms += static_cast<double>(k.due) * (k.answer_ms > 0 ? k.answer_ms : 10000);
    return std::max<int64_t>(1, std::llround(ms / 60000.0));
}

std::vector<ReviewKind> ReviewDay::dueKinds() const {
    std::vector<ReviewKind> out;
    for (const auto kind : reviewKinds())
        if (const auto* k = find(kind); k && k->due > 0) out.push_back(kind);
    return out;
}

const ReviewCount* ReviewDay::find(ReviewKind kind) const {
    const auto it = std::find_if(kinds.begin(), kinds.end(), [&](const auto& k) { return k.kind == kind; });
    return it == kinds.end() ? nullptr : &*it;
}

}  // namespace fcg
