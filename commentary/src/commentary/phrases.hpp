#pragma once

// The sentences, one phrasebook per language (the prototype's crates/commentary/src/phrases.rs). A new language is a
// new implementation.

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "fact_types.hpp"
#include "gather.hpp"

namespace fcg::commentary {

enum class Lang : uint8_t { Fr, En };

// Everything said, in one language. Sentences come out complete, with their full stop.
class Phrasebook {
   public:
    virtual ~Phrasebook() = default;
    // What makes a move fail, told of the move itself. None when the fact does not explain a failure.
    [[nodiscard]] virtual std::optional<std::string> punishment(const Fact& fact) const = 0;
    // What makes a move fail, told with the opponent's answer `mv`: the answer does it `at_once`, or the line it starts
    // does. None when the fact is better told of the move.
    [[nodiscard]] virtual std::optional<std::string> refuted(const Fact& fact, std::string_view mv,
                                                             bool at_once) const = 0;
    // How the move makes its refutation possible. With `falls`, the opponent's answer, the sentence also says that the
    // piece left alone is lost to it.
    [[nodiscard]] virtual std::optional<std::string> cause(const Fact& fact,
                                                           std::optional<std::string_view> falls) const = 0;
    // `sentence` with what the mechanism it tells costs in material.
    [[nodiscard]] virtual std::string costs(std::string_view sentence, const Fact& cost) const = 0;
    // The move that should have been played, and what it would have done, as the facts of its line say, the most
    // telling first. `chance` when nothing punishes the move played: it only misses this one.
    [[nodiscard]] virtual std::string better(std::string_view mv, std::span<const Fact> why, bool chance) const = 0;
    // An only move, with how things stand after it.
    [[nodiscard]] virtual std::string onlyMove(double win_after, const Fact* why) const = 0;
    // `sentence`, which tells what refutes a move, told of `alt`, the next best move to an only move.
    [[nodiscard]] virtual std::string otherwise(std::string_view alt, std::string_view sentence) const = 0;
    // What a good move achieves. None when the fact is not worth a sentence.
    [[nodiscard]] virtual std::optional<std::string> achieves(const Fact& fact) const = 0;
    // A novelty, and what theory plays instead: the surveys' main line, or the move the reference games played.
    [[nodiscard]] virtual std::string novelty(
        std::optional<std::pair<std::string_view, Theory::Source>> usual) const = 0;
    // A novelty the surveys have since made their main line.
    [[nodiscard]] virtual std::string noveltyTakenUp() const = 0;
};

[[nodiscard]] const Phrasebook& phrasebook(Lang lang);

// A formation by its name: « l'étau de Maroczy », "the Maroczy bind"; a chain by the squares of its owner's pawns.
[[nodiscard]] std::string formationName(Formation kind, Color side, Lang lang);
// A pawn break by what it means in the structure.
[[nodiscard]] const char* leverWords(const Lever& lever, Lang lang);
// Where on the board a pawn gains space: « à l'aile dame », "in the centre".
[[nodiscard]] const char* wingWords(Square square, Lang lang);
// A wing of the board: « à l'aile dame », "on the kingside".
[[nodiscard]] const char* ofWing(Wing wing, Lang lang);
// The squares of one colour: « les cases noires », "the dark squares".
[[nodiscard]] const char* colourSquares(bool dark, Lang lang);
// The file or rank a rook holds from `square`: « la colonne e », "the fifth rank".
[[nodiscard]] std::string barrierWords(LineKind line, Square square, Lang lang);
// Material in words: « la qualité », "a piece for two pawns"; by its worth in pawns when the trade is not known.
[[nodiscard]] std::string materialWords(int32_t pawns, std::optional<Trade> trade, Lang lang);
// What was given for the piece a trade comes down to: « un pion », "two pawns".
[[nodiscard]] std::optional<std::string> givenWords(std::optional<Trade> trade, Lang lang);
// A formation and what it means for both sides, as the brief and the phrasebook tell it.
[[nodiscard]] std::string formationSentence(Formation kind, Color side, Lang lang);
[[nodiscard]] const char* sideName(Color color, Lang lang);

// The first letter in capitals, or in small letters (Latin letters, accented ones included).
[[nodiscard]] std::string capitalize(std::string text);
[[nodiscard]] std::string lowerFirst(std::string_view sentence);

namespace phrases_detail {

// What a trade is told in: the exchange (a rook for a piece), then by kind.
enum class Kind : uint8_t { Exchange, Queen, Rook, Piece, Pawn };
// A phrasebook's words for a count of one kind of material.
using KindWords = const char* (*)(Kind, uint8_t);
// A trade told with a phrasebook's words: what was won, and what was given for it when something was. `kind` names a
// count of one kind, `and_` joins the last two items.
[[nodiscard]] std::optional<std::pair<std::string, std::optional<std::string>>> tradeParts(const Trade& trade,
                                                                                           KindWords kind,
                                                                                           std::string_view and_);
// The same in one phrase, `against` joining what was won to what was given.
[[nodiscard]] std::optional<std::string> tradeWords(const Trade& trade, KindWords kind, std::string_view and_,
                                                    std::string_view against);
// Whether a fact names a tactic: its cost in material is worth adding.
[[nodiscard]] bool isMechanism(const Fact& fact);
// The reasons a better move is given: what it wins, with the tactic that wins it as a "by" phrase when it has one; else
// its most telling fact, and what it wins besides.
template <typename Would, typename By>
[[nodiscard]] std::vector<std::string> reasons(std::span<const Fact> why, Would would, By by);
// How things stand after an only move: 0 better, 1 balanced, 2 worse.
[[nodiscard]] std::size_t standing(double win_after) noexcept;

template <typename Would, typename By>
std::vector<std::string> reasons(std::span<const Fact> why, Would would, By by) {
    const Fact* top = nullptr;
    std::string said;
    for (const auto& f : why) {
        if (auto s = would(f)) {
            top  = &f;
            said = std::move(*s);
            break;
        }
    }
    if (!top) return {};
    const Fact* wins = nullptr;
    for (const auto& f : why) {
        if (std::holds_alternative<fact::WinsMaterial>(f)) {
            wins = &f;
            break;
        }
    }
    std::optional<std::string> gain;
    if (wins && isMechanism(*top)) gain = would(*wins);
    // The piece a tactic is about is the piece won: said once, as the gain when the tactic only takes it, as the tactic
    // when it traps it. (Compared in place: a copy of `taken` makes gcc 14 warn of a payload "maybe uninitialized".)
    const auto won = [&](const Placed& p) { return wins && std::get<fact::WinsMaterial>(*wins).taken == p; };
    if (const auto* t = std::get_if<fact::Trapped>(top); t && won(t->piece)) return {said};
    if (const auto* h = std::get_if<fact::Hanging>(top); h && won(h->piece)) return {said};
    if (const auto* r = std::get_if<fact::RemovesDefender>(top); r && won(r->defender)) {
        if (gain) return {*gain};
        return {};
    }
    const auto how = by(*top);
    if (gain && how) return {*gain + " " + *how};
    if (gain) return {said, *gain};
    return {said};
}

}  // namespace phrases_detail

}  // namespace fcg::commentary
