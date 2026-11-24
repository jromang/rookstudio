#include "endgame_check.hpp"

#include <algorithm>
#include <utility>

#include <fmt/include/fmt/format.h>

#include "i18n.hpp"

namespace fcg {

namespace {

int sign(chess::Color c) { return c == chess::Color::WHITE ? 1 : -1; }

// A mate, a stalemate or too little material to mate: the verdict of the rules, White's point of view.
std::optional<int> rulesVerdict(const chess::Board& board) {
    if (board.isInsufficientMaterial()) return 0;
    if (chess::movegen::anylegalmoves(board)) return std::nullopt;
    return board.inCheck() ? -sign(board.sideToMove()) : 0;
}

}  // namespace

GameEnd gameEndOf(int termination, const std::string& reason, bool result_set) {
    if (result_set) return GameEnd::Other;  // the operator's result, not the game's
    if (termination == 0) return GameEnd::Rules;
    if (termination != 1) return GameEnd::Other;  // time, a disconnection, a stall, an illegal move, an interruption
    if (reason.find("SyzygyTB") != std::string::npos) return GameEnd::TablesAdjudicated;
    if (reason.find("operator") != std::string::npos) return GameEnd::Other;
    return GameEnd::Adjudicated;
}

EndgameGameCheck::EndgameGameCheck(const CheckedGame& game, int max_pieces) : game_(&game) {
    chess::Board board(game.start_fen.empty() ? std::string(chess::constants::STARTPOS) : game.start_fen,
                       game.chess960);
    // The positions from the first the tables can tell: as many pieces as theirs or fewer, no castling right (none
    // comes back, and pieces only go).
    const auto within = [&] { return board.occ().count() <= max_pieces && board.castlingRights().isEmpty(); };
    std::size_t ply   = 0;
    for (; ply < game.moves.size() && !within(); ++ply) board.makeMove(game.moves[ply]);
    if (!within()) return;
    start_ = ply;
    for (;; ++ply) {
        fens_.push_back(board.getFen());
        pieces_.push_back(board.occ().count());
        if (ply == game.moves.size()) break;
        board.makeMove(game.moves[ply]);
    }
    first_       = start_;
    last_        = game.moves.size();
    end_verdict_ = rulesVerdict(board);
    phase_       = Phase::First;
}

const std::string& EndgameGameCheck::fen(std::size_t ply) const { return fens_.at(ply - start_); }

int EndgameGameCheck::pieces(std::size_t ply) const { return pieces_.at(ply - start_); }

std::optional<std::size_t> EndgameGameCheck::next() const {
    switch (phase_) {
        case Phase::First:
            return first_;
        case Phase::Last:
            return last_;
        case Phase::Between:
            return low_ + (high_ - low_) / 2;
        case Phase::Done:
            break;
    }
    return std::nullopt;
}

void EndgameGameCheck::give(std::size_t ply, TablesSays says) {
    if (next() != ply) return;
    ++asked_;
    switch (phase_) {
        case Phase::First: {
            if (!says) {
                // The next position with fewer pieces, which tables of fewer pieces may cover.
                std::size_t p = ply;
                while (p < last_ && pieces(p) >= pieces(ply)) ++p;
                if (pieces(p) >= pieces(ply)) {
                    phase_ = Phase::Done;  // none: unjudged
                } else {
                    first_ = p;
                    if (p == last_ && end_verdict_) {  // the game's end, which the rules tell
                        says_[p] = *end_verdict_;
                        decide();
                    }
                }
                return;
            }
            says_[ply] = *says;
            if (ply == last_) {
                decide();
            } else if (end_verdict_) {
                says_[last_] = *end_verdict_;
                decide();
            } else {
                phase_ = Phase::Last;
            }
            return;
        }
        case Phase::Last:
            if (!says) {
                phase_ = Phase::Done;  // unjudged
                return;
            }
            says_[ply] = *says;
            decide();
            return;
        case Phase::Between:
            if (!says) {
                phase_ = Phase::Done;  // the verdicts at both ends judge the game; the move that changed it unknown
                return;
            }
            says_[ply] = *says;
            if (*says == says_.at(*first_))
                low_ = ply;
            else
                high_ = ply;
            if (high_ - low_ <= 1) {
                between_known_ = true;
                phase_         = Phase::Done;
            }
            return;
        case Phase::Done:
            return;
    }
}

void EndgameGameCheck::decide() {
    judged_ = true;
    if (says_.at(*first_) == says_.at(last_)) {
        phase_ = Phase::Done;
        return;
    }
    low_  = *first_;
    high_ = last_;
    if (high_ - low_ <= 1) {
        between_known_ = true;
        phase_         = Phase::Done;
    } else {
        phase_ = Phase::Between;
    }
}

std::vector<EndgameFinding> EndgameGameCheck::findings(std::size_t index) const {
    std::vector<EndgameFinding> out;
    if (!judged_) return out;
    const int first = says_.at(*first_), last = says_.at(last_);
    // The move that changed the verdict, its side's doing: a move cannot better its side's outcome.
    if (between_known_ && first != last) {
        const chess::Board before(fen(low_), game_->chess960);
        const auto mover = before.sideToMove();
        const int m      = sign(mover);
        const int after  = says_.at(high_);
        EndgameFinding f;
        f.game           = index;
        f.ply            = high_;
        f.side           = mover;
        f.before         = first;
        f.after          = after;
        const auto& move = game_->moves[high_ - 1];
        f.move           = fmt::format("{}{}{}", before.fullMoveNumber(), mover == chess::Color::WHITE ? "." : "…",
                                       chess::uci::moveToSan(before, move));
        if (first == m && after != m) {
            f.kind = EndgameFinding::Kind::WinLetSlip;
            out.push_back(std::move(f));
        } else if (first == 0 && after == -m) {
            f.kind = EndgameFinding::Kind::DrawLetSlip;
            out.push_back(std::move(f));
        }
    }
    // The result against the last position's verdict.
    const int result = game_->result;
    if (result != last) {
        EndgameFinding f;
        f.game   = index;
        f.ply    = last_;
        f.before = last;
        f.after  = result;
        if (game_->end == GameEnd::Adjudicated) {
            if (result == 0) {
                f.kind = EndgameFinding::Kind::DrawAdjudicatedWon;
                f.side = last > 0 ? chess::Color::WHITE : chess::Color::BLACK;
            } else {
                f.kind = EndgameFinding::Kind::WinAdjudicatedNotWon;
                f.side = result > 0 ? chess::Color::WHITE : chess::Color::BLACK;
            }
            out.push_back(std::move(f));
        } else if (game_->end == GameEnd::Rules && result == 0) {
            f.kind = EndgameFinding::Kind::LostPositionHeld;
            f.side = last > 0 ? chess::Color::BLACK : chess::Color::WHITE;
            out.push_back(std::move(f));
        }
    }
    return out;
}

EndgameCheckSummary checkEndgames(const std::vector<CheckedGame>& games,
                                  const std::function<TablesSays(const std::string& fen)>& tables, int max_pieces) {
    EndgameCheckSummary s;
    s.games = games.size();
    for (std::size_t i = 0; i < games.size(); ++i) {
        EndgameGameCheck check(games[i], max_pieces);
        if (!check.reached()) continue;
        ++s.reached;
        while (const auto ply = check.next()) check.give(*ply, tables(check.fen(*ply)));
        s.asked += check.asked();
        if (!check.judged()) continue;
        ++s.judged;
        for (auto& f : check.findings(i)) s.findings.push_back(std::move(f));
    }
    return s;
}

CheckSource checkSource(int pieces, int disk_pieces, bool disk_locked, bool kept, int lichess_setting, bool allowed) {
    if (pieces <= disk_pieces && !disk_locked) return CheckSource::Disk;
    if (kept) return CheckSource::Kept;
    if (pieces > 7 || lichess_setting == 2) return CheckSource::None;
    if (lichess_setting == 1 || allowed) return CheckSource::Lichess;
    return CheckSource::Consent;
}

std::string endgameCheckSentence(const EndgameCheckSummary& s, const std::vector<CheckedGame>& games) {
    if (s.reached == 0) return TR("No game reached seven pieces or fewer: there is nothing for the tables to check.");
    // The engines, the most first: "Alpha: 2, Bravo: 1".
    const auto engines = [&](EndgameFinding::Kind kind) {
        std::map<std::string, int> count;
        for (const auto& f : s.findings)
            if (f.kind == kind) {
                const auto& g = games[f.game];
                ++count[f.side == chess::Color::WHITE ? g.white : g.black];
            }
        std::vector<std::pair<std::string, int>> sorted(count.begin(), count.end());
        std::stable_sort(sorted.begin(), sorted.end(),
                         [](const auto& a, const auto& b) { return a.second > b.second; });
        std::string out;
        for (const auto& [name, n] : sorted) out += (out.empty() ? "" : ", ") + fmt::format("{}: {}", name, n);
        return out;
    };
    const auto count = [&](EndgameFinding::Kind kind) {
        return static_cast<int>(std::count_if(s.findings.begin(), s.findings.end(),
                                              [&](const EndgameFinding& f) { return f.kind == kind; }));
    };
    using K = EndgameFinding::Kind;
    std::vector<std::string> parts;
    if (const int n = count(K::WinLetSlip))
        parts.push_back(trn("{} win let slip ({})", "{} wins let slip ({})", n, n, engines(K::WinLetSlip)));
    if (const int n = count(K::DrawLetSlip))
        parts.push_back(trn("{} draw let slip ({})", "{} draws let slip ({})", n, n, engines(K::DrawLetSlip)));
    if (const int n = count(K::LostPositionHeld))
        parts.push_back(trn("{} lost position held", "{} lost positions held", n, n));
    if (const int n = count(K::DrawAdjudicatedWon))
        parts.push_back(trn("{} draw adjudicated in a won position", "{} draws adjudicated in a won position", n, n));
    if (const int n = count(K::WinAdjudicatedNotWon))
        parts.push_back(trn("{} win adjudicated against the tables", "{} wins adjudicated against the tables", n, n));
    const int reached = static_cast<int>(s.reached);
    std::string out;
    if (parts.empty()) {
        out = trn("{} game reached seven pieces or fewer: its result agrees with the tables.",
                  "{} games reached seven pieces or fewer: each result agrees with the tables.", reached, reached);
    } else {
        std::string list;
        for (const auto& p : parts) list += (list.empty() ? "" : ", ") + p;
        out = trn("{} game reached seven pieces or fewer: {}.", "{} games reached seven pieces or fewer: {}.", reached,
                  reached, list);
    }
    if (const int unjudged = static_cast<int>(s.reached - s.judged); unjudged > 0)
        out += " " + trn("{} game left unjudged: the tables at hand do not cover it.",
                         "{} games left unjudged: the tables at hand do not cover them.", unjudged, unjudged);
    return out;
}

std::string endgameFindingWords(const EndgameFinding& f, const CheckedGame& g) {
    const auto& engine = f.side == chess::Color::WHITE ? g.white : g.black;
    const auto& other  = f.side == chess::Color::WHITE ? g.black : g.white;
    const auto winner  = [&](int verdict) { return verdict > 0 ? g.white : g.black; };
    switch (f.kind) {
        case EndgameFinding::Kind::WinLetSlip:
            return f.after == 0
                       ? trf("{} let the win slip with {}: the tables then give a draw.", engine, f.move)
                       : trf("{} let the win slip with {}: the tables then give {} the win.", engine, f.move, other);
        case EndgameFinding::Kind::DrawLetSlip:
            return trf("{} let the draw slip with {}: the tables then give {} the win.", engine, f.move, other);
        case EndgameFinding::Kind::LostPositionHeld:
            return trf("{} held a lost position: the tables give {} the win at the end.", engine, other) +
                   (g.reason.empty() ? std::string() : " " + std::string(TR(g.reason.c_str())) + ".");
        case EndgameFinding::Kind::DrawAdjudicatedWon:
            return trf("A draw adjudicated where the tables give {} the win.", engine);
        case EndgameFinding::Kind::WinAdjudicatedNotWon:
            return f.before == 0
                       ? trf("A win adjudicated to {} where the tables give a draw.", engine)
                       : trf("A win adjudicated to {} where the tables give {} the win.", engine, winner(f.before));
    }
    return {};
}

}  // namespace fcg
