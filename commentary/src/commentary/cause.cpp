#include "cause.hpp"

#include "attack.hpp"

namespace fcg::commentary {

namespace {

bool isSlider(Role role) noexcept { return role == Role::Bishop || role == Role::Rook || role == Role::Queen; }

}  // namespace

Square landing(const Move& mv, Color side) noexcept {
    if (const auto wing = mv.castlingSide()) return kingTo(*wing, side);
    return mv.to();
}

std::vector<Fact> causes(const Position& before, const Move& played, const Move& answer) {
    const Color mover = before.turn();
    auto after        = before;
    after.playUnchecked(played);
    const auto& was = before.board();
    const auto& now = after.board();
    const auto to   = landing(played, mover);
    std::vector<Fact> facts;

    // What the answer takes at once, other than the piece that just moved.
    const auto target = answer.to();
    if (answer.isCapture() && target != to) {
        const auto taken = Placed::at(now, target);
        if (taken && Placed::at(was, target) == taken && see(now, target, !mover) > 0) {
            const auto guarded = [&](Square from) { return was.attacksFrom(from).contains(target); };
            if (see(was, target, !mover) > 0) {
                facts.push_back(Fact{fact::Neglects{*taken}});
            } else if (guarded(played.from()) && !now.attacksFrom(to).contains(target)) {
                if (const auto piece = Placed::at(now, to)) facts.push_back(Fact{fact::Unguards{*piece, *taken}});
            }
        }
    }

    // The square the move left stood in the way of the piece that answers: on its path, or on the line of the check it
    // gives. The piece is named where it stands before answering.
    const auto left  = played.from();
    const auto start = answer.from();
    auto replied     = after;
    replied.playUnchecked(answer);
    const auto through = [&](Square a, Square b) { return attacks::between(a, b).contains(left); };
    struct Opened {
        Square stood, from, toward;
    };
    std::optional<Opened> opened;
    if (through(start, target)) {
        opened = Opened{start, start, target};
    } else if (const auto king = replied.board().kingOf(mover)) {
        for (const auto c : replied.checkers()) {
            if (through(c, *king)) {
                const auto stood = c == landing(answer, !mover) ? start : c;
                opened           = Opened{stood, c, *king};
                break;
            }
        }
    }
    if (opened) {
        const auto slider = Placed::at(now, opened->stood);
        const auto line   = lineKindOf(opened->from, opened->toward);
        if (slider && isSlider(slider->role) && line) facts.push_back(Fact{fact::OpensLine{*slider, *line}});
    }
    return facts;
}

std::optional<Fact> saved(const Position& before, const Move& mv) {
    const Color mover = before.turn();
    if (mv.isCapture() || mv.isCastle()) return std::nullopt;
    const auto from  = mv.from();
    const auto piece = Placed::at(before.board(), from);
    if (!piece || piece->role == Role::King) return std::nullopt;
    auto after = before;
    after.playUnchecked(mv);
    const bool escapes = see(before.board(), from, !mover) > 0 && see(after.board(), mv.to(), !mover) <= 0;
    if (escapes) return Fact{fact::Saves{*piece}};
    return std::nullopt;
}

std::vector<Fact> remedies(const Position& before, const Move& played, const Move& best, const Move& answer,
                           std::optional<Move> previous) {
    const Color mover = before.turn();
    auto after = before, better = before;
    after.playUnchecked(played);
    better.playUnchecked(best);
    const auto& was = before.board();
    std::vector<Fact> facts;

    if (played.isCapture() && best.isCapture() && played.to() == best.to() && played.from() != best.from()) {
        if (const auto by = Placed::at(was, best.from())) {
            const bool back = previous && previous->isCapture() && previous->to() == best.to();
            facts.push_back(Fact{fact::TakesWith{*by, back}});
        }
    }

    const auto target = answer.to();
    const auto lands  = landing(best, mover);
    // A guard the better move adds: one its piece did not give from where it stood.
    const auto adds_guard = [&](Square square) {
        return better.board().attacksFrom(lands).contains(square) && !was.attacksFrom(best.from()).contains(square);
    };
    if (answer.isCapture()) {
        const auto taken = Placed::at(after.board(), target);
        if (!taken || taken->color != mover) return facts;
        if (see(after.board(), target, !mover) <= 0 || Placed::at(was, target) != taken) return facts;
        if (best.from() == target) {
            // Only a piece that is safe where it goes has been saved.
            if (see(better.board(), lands, !mover) <= 0) facts.push_back(Fact{fact::Saves{*taken}});
        } else if (adds_guard(target) && see(better.board(), target, !mover) <= 0) {
            facts.push_back(Fact{fact::Guards{*taken}});
        }
    } else if (adds_guard(target)) {
        // The same answer, after the better move, loses the piece that lands there.
        std::optional<Move> same;
        for (const auto& m : better.legalMoves()) {
            if (m.from() == answer.from() && m.to() == target && m.promotion() == answer.promotion()) {
                same = m;
                break;
            }
        }
        auto replied = after;
        replied.playUnchecked(answer);
        const bool was_safe = see(replied.board(), target, mover) <= 0;
        if (same) {
            auto there = better;
            there.playUnchecked(*same);
            if (was_safe && see(there.board(), target, mover) > 0) facts.push_back(Fact{fact::Covers{mover, target}});
        }
    }
    return facts;
}

}  // namespace fcg::commentary
