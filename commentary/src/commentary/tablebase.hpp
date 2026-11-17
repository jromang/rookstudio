#pragma once

// Endgame tablebases: the exact outcome of positions with few pieces left (the prototype's
// crates/facts/src/tablebase.rs). The outcomes come from tables a probe reads, and from what the program is taught: the
// answers of an online tablebase, which knows more pieces than a disk usually holds.

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <shared_mutex>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "fact_types.hpp"
#include "position.hpp"

namespace fcg::commentary {

// Tables on disk: their largest number of men, and the outcome for the side to move of a position with that many or
// fewer, a result the fifty-move rule would spoil counting as a draw.
struct TableProbe {
    int max_men = 0;
    std::function<std::optional<Verdict>(const Position&)> wdl;
};

class Tablebases {
   public:
    // Positions with more men than this are not worth asking a teacher about.
    static constexpr int TAUGHT_MEN = 7;

    // Whatever they are taught, and the tables `probe` reads, when there are any.
    explicit Tablebases(std::optional<TableProbe> probe = std::nullopt) : probe_(std::move(probe)) {}

    // Whether a teacher could tell more about a position than the tables know: its outcome, or that of its moves.
    [[nodiscard]] bool wants(const Position& pos) const;
    // Takes in the outcome of `pos` for the side to move and, for each of its moves, the best first, the outcome for
    // the side that plays it: the position is then known whole, and the positions its moves lead to by their outcome.
    void learn(const Position& pos, Verdict verdict, std::span<const std::pair<Move, Verdict>> moves);
    // The outcome for the side to move, if the position is in the tables or was taught.
    [[nodiscard]] std::optional<Verdict> verdict(const Position& pos) const;
    // The move a teacher named as the best in `pos`.
    [[nodiscard]] std::optional<Move> best(const Position& pos) const;
    // The outcome for the side that plays `mv` from `before`.
    [[nodiscard]] std::optional<Verdict> after(const Position& before, const Move& mv) const;
    // What a move does to the exact outcome: spoils it, or is the only one keeping it. When only the position it leads
    // to is known, the outcome there.
    [[nodiscard]] std::vector<Fact> moveFacts(const Position& before, const Move& mv) const;

   private:
    // What the tables were taught about a position: the outcome for the side to move, the move the teacher would play,
    // and whether the outcome of every move came with it.
    struct Lesson {
        Verdict verdict = Verdict::Draw;
        std::optional<Move> best;
        bool whole = false;
    };
    [[nodiscard]] static std::string key(const Position& pos);
    [[nodiscard]] std::optional<Lesson> lesson(const Position& pos) const;
    [[nodiscard]] bool onDisk(const Position& pos) const;

    std::optional<TableProbe> probe_;
    mutable std::shared_mutex mutex_;
    std::map<std::string, Lesson> learnt_;
};

}  // namespace fcg::commentary
