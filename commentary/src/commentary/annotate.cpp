#include "annotate.hpp"

#include <algorithm>
#include <optional>

#include <chess.hpp>

#include "eval.hpp"
#include "position.hpp"

namespace fcg::commentary {

namespace {

bool isMoveAssessment(uint8_t n) noexcept { return n >= 1 && n <= 9; }
bool isPositionAssessment(uint8_t n) noexcept { return n >= 10 && n <= 19; }

// Adds `n` unless it, or a NAG `excludes` takes, is there already.
void addIfMissing(std::vector<uint8_t>& nags, uint8_t n, bool (*excludes)(uint8_t) noexcept = nullptr) {
    for (const auto x : nags)
        if (x == n || (excludes && excludes(x))) return;
    toggleNag(nags, n);
}

// White's score as the mover's, the way a game holds it: centipawns, or a mate in moves (positive: the mover mates).
fastchess::Score forMover(const Score& white, chess::Color mover) {
    const int64_t v = mover == chess::Color::WHITE ? white.value : -int64_t{white.value};
    return {white.kind == Score::Kind::Mate ? fastchess::ScoreType::MATE : fastchess::ScoreType::CP, v};
}

// The engine's best line from the position before the move `index` of the main line, as a variation: none when it
// is the move played, when the game has that line already, or when the engine has none.
std::optional<int> betterLine(GameTree& game, const MainLine& line, std::size_t index, const Analyses& analyses,
                              std::size_t plies) {
    if (index >= line.moves().size() || index >= analyses.size() || !analyses[index]) return std::nullopt;
    const auto* best = analyses[index]->best();
    if (!best || best->moves.empty() || plies == 0 || best->moves[0] == line.moves()[index]) return std::nullopt;
    const auto first = uci(best->moves[0]);
    for (const auto& v : game.variations)
        if (v.parent == -1 && v.from == static_cast<int>(index) && !v.moves.empty() &&
            chess::uci::moveToUci(v.moves[0].move) == first)
            return std::nullopt;
    chess::Board board(line.positions()[index].fen());
    Variation variation;
    variation.parent = -1;
    variation.from   = static_cast<int>(index);
    for (std::size_t i = 0; i < std::min(plies, best->moves.size()); ++i) {
        const auto move = chess::uci::uciToMove(board, uci(best->moves[i]));
        if (move == chess::Move::NO_MOVE) break;
        ReplayMove played;
        played.san   = chess::uci::moveToSan(board, move);
        played.move  = move;
        played.mover = board.sideToMove();
        board.makeMove(move);
        variation.moves.push_back(std::move(played));
    }
    if (variation.moves.empty()) return std::nullopt;
    addIfMissing(variation.moves.front().notes.nags, NAG_BETTER_IS);
    addIfMissing(variation.moves.back().notes.nags, positionNag(best->whiteExpected()), isPositionAssessment);
    game.variations.push_back(std::move(variation));
    return static_cast<int>(game.variations.size() - 1);
}

}  // namespace

void toggleNag(std::vector<uint8_t>& nags, uint8_t nag) {
    if (const auto it = std::find(nags.begin(), nags.end(), nag); it != nags.end()) {
        nags.erase(it);
        return;
    }
    if (isMoveAssessment(nag)) std::erase_if(nags, isMoveAssessment);
    if (isPositionAssessment(nag)) std::erase_if(nags, isPositionAssessment);
    nags.push_back(nag);
    std::stable_sort(nags.begin(), nags.end(), [](uint8_t a, uint8_t b) {
        const auto rank = [](uint8_t n) { return isMoveAssessment(n) ? 0 : isPositionAssessment(n) ? 1 : 2; };
        return rank(a) != rank(b) ? rank(a) < rank(b) : a < b;
    });
}

std::vector<int> annotate(GameTree& game, const MainLine& line, const GameReport& report, const Analyses& analyses,
                          const AnnotateOptions& options) {
    std::vector<int> added;
    auto& moves = game.main.moves;
    for (const auto& m : report.moves) {
        if (m.ply == 0 || m.ply > moves.size()) continue;
        auto& move = moves[m.ply - 1];
        // The evaluation after the move, unless the game has another one of its own (a tournament engine's).
        const auto* after = m.ply < analyses.size() && analyses[m.ply] ? analyses[m.ply]->best() : nullptr;
        if (after && (!move.score || move.notes.eval_tag)) {
            move.score          = forMover(after->score, move.mover);
            move.depth          = static_cast<int>(after->depth);
            move.notes.eval_tag = true;
        }
        if (m.novelty) addIfMissing(move.notes.nags, NAG_NOVELTY);
        const std::optional<uint8_t> symbol = m.judgment    ? std::optional<uint8_t>(nag(*m.judgment))
                                              : m.only_move ? std::optional<uint8_t>(NAG_ONLY_MOVE)
                                                            : std::nullopt;
        if (symbol && std::none_of(move.notes.nags.begin(), move.notes.nags.end(), isMoveAssessment))
            toggleNag(move.notes.nags, *symbol);
        if (m.judgment && *m.judgment >= options.variations_from)
            if (const auto v = betterLine(game, line, m.ply - 1, analyses, options.variation_plies))
                added.push_back(*v);
    }
    return added;
}

}  // namespace fcg::commentary
