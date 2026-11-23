#include "endgames.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include <pyrrhic/tbprobe.h>
#include <json.hpp>
#include <matchmaking/syzygy.hpp>

#include "i18n.hpp"
#include "storage.hpp"
#include "tablebases.hpp"

namespace fcg {

namespace fs = std::filesystem;

namespace {

// Each start was probed with the tables of 3 to 5 pieces (the result and the moves that keep it, said beside it), the
// two of 6 pieces with an engine and the 5-piece tables: docs/design/endgames.md.
const std::vector<EndgameEntry> CATALOGUE = {
    // The elementary mates: the tables mate in 7, 14, 16 and 29 moves.
    {"queen", EndgameGroup::Mates, N_("Queen"), EndgameGoal::Win, "8/8/8/4k3/8/8/8/4K2Q w - - 0 1",
     N_("Keep the king boxed in with the queen a knight's jump away, bring your own king, and mind the stalemate."),
     true},
    {"rook", EndgameGroup::Mates, N_("Rook"), EndgameGoal::Win, "8/8/8/4k3/8/8/8/R3K3 w - - 0 1",
     N_("The rook cuts the king off; your king faces it, then the rook checks: the box shrinks a rank at a time."),
     true},
    {"two_bishops", EndgameGroup::Mates, N_("Two bishops"), EndgameGoal::Win, "8/8/8/4k3/8/8/8/2B1KB2 w - - 0 1",
     N_("Side by side, the bishops make a wall; with your king, drive the other one into a corner."), true},
    {"bishop_knight", EndgameGroup::Mates, N_("Bishop and knight"), EndgameGoal::Win,
     "8/8/8/4k3/8/8/8/1N2KB2 w - - 0 1",
     N_("The mate comes in a corner of your bishop's colour: drive the king there with the knight and your king."),
     true},
    // Pawns: Kd6 and Kf6 alone win the opposition; a5 alone the square; every move the outside pawn; Kg7 alone holds
    // Réti's.
    {"opposition", EndgameGroup::Pawns, N_("The opposition"), EndgameGoal::Win, "4k3/8/4K3/4P3/8/8/8/8 w - - 0 1",
     N_("The king stays ahead of its pawn, on a file beside it, and takes the opposition.")},
    {"square", EndgameGroup::Pawns, N_("The rule of the square"), EndgameGoal::Win, "8/8/8/8/P4k2/8/8/7K w - - 0 1",
     N_("Outside the pawn's square, the king cannot catch it: push the pawn at once.")},
    {"outside_pawn", EndgameGroup::Pawns, N_("The outside passed pawn"), EndgameGoal::Win,
     "8/8/3k4/6p1/P2K2P1/8/8/8 w - - 0 1",
     N_("The far passed pawn draws the king away; your king takes what it leaves.")},
    {"reti", EndgameGroup::Pawns, N_("Réti's manoeuvre"), EndgameGoal::Draw, "7K/8/k1P5/7p/8/8/8/8 w - - 0 1",
     N_("The king walks the diagonal: it chases the pawn and helps its own at the same time.")},
    // Rooks: 11 moves of 14 keep Lucena's win; 8 of 16 hold Philidor's position, 4 of 19 Vancura's; Kb5 alone wins
    // the rook against the pawn.
    {"lucena", EndgameGroup::Rooks, N_("Lucena"), EndgameGoal::Win, "1K6/1P1k4/8/8/8/8/r7/2R5 w - - 0 1",
     N_("Build a bridge: the rook on the fourth rank shelters your king from the checks.")},
    {"philidor", EndgameGroup::Rooks, N_("Philidor"), EndgameGoal::Draw, "4k3/R7/1r6/4PK2/8/8/8/8 b - - 0 1",
     N_("Keep the rook on your third rank; once the pawn steps onto it, check from behind.")},
    {"vancura", EndgameGroup::Rooks, N_("Vancura"), EndgameGoal::Draw, "R7/8/P4rk1/8/8/8/6K1/8 b - - 0 1",
     N_("Keep the rook beside the pawn, on its rank, and your king on g7 or h7: White's rook stays tied to it.")},
    {"rook_pawn", EndgameGroup::Rooks, N_("Rook against pawn"), EndgameGoal::Win, "8/8/K7/8/5k2/4p3/8/7R w - - 0 1",
     N_("Bring your king back toward the pawn at once: with its king beside it, the rook alone cannot stop it.")},
    {"rook_two_pawns", EndgameGroup::Rooks, N_("Rook and two pawns against rook"), EndgameGoal::Win,
     "8/1R6/5k2/8/4KPP1/r7/8/8 w - - 0 1",
     N_("Push the pawns side by side with your king close; your rook keeps the other king away.")},
    // Pieces and queen: Qa5 wins the rook at once (14 other moves later); 4 moves of 23 keep the win against the pawn;
    // Kg7 alone holds the wrong bishop; 9 moves of 13 hold the rook against the bishop.
    {"queen_rook", EndgameGroup::Pieces, N_("Queen against rook"), EndgameGoal::Win,
     "3k4/2r5/3K4/8/8/8/8/4Q3 w - - 0 1",
     N_("Take the squares around the king: once Black has to move, the rook leaves its king and falls.")},
    {"queen_pawn", EndgameGroup::Pieces, N_("Queen against pawn"), EndgameGoal::Win, "8/8/8/8/8/8/3pk3/K6Q w - - 0 1",
     N_("Check toward the pawn until the king has to stand in front of it, then bring your king a step closer.")},
    {"wrong_bishop", EndgameGroup::Pieces, N_("The wrong bishop"), EndgameGoal::Draw,
     "8/5k2/8/4K2P/2B5/8/8/8 b - - 0 1",
     N_("The bishop cannot cover h8: get your king into the corner and stay there.")},
    {"rook_bishop", EndgameGroup::Pieces, N_("Rook against bishop"), EndgameGoal::Draw,
     "8/6k1/4K3/8/8/8/1R6/5b2 b - - 0 1",
     N_("Go to the corner your bishop does not cover: there it blocks the checks.")},
    {"bishop_knight_pawns", EndgameGroup::Pieces, N_("Bishop against knight"), EndgameGoal::Win,
     "8/4n3/3k4/1P6/2K2P2/3B4/8/8 w - - 0 1",
     N_("Pawns on both wings stretch the knight; the bishop guards both at once.")},
};

// Pyrrhic's WDL (TB_LOSS 0 … TB_WIN 4) as a rank for the side it is about: -2 a loss … +2 a win, ±1 the fifty-move
// rule spoiling it.
int rankOfWdl(unsigned wdl) { return static_cast<int>(wdl) - 2; }

// A rank for the side to move, as Lichess names it.
lichess::Outcome outcomeOfRank(int rank) {
    switch (rank) {
        case 2:
            return lichess::Outcome::Win;
        case 1:
            return lichess::Outcome::CursedWin;
        case -1:
            return lichess::Outcome::BlessedLoss;
        case -2:
            return lichess::Outcome::Loss;
        default:
            return lichess::Outcome::Draw;
    }
}

// The rank of the outcome a move leaves the other side, for the side that plays it.
lichess::Outcome leftOfRank(int mover_rank) { return outcomeOfRank(-mover_rank); }

uint64_t bits(const chess::Board& board, chess::PieceType type) { return board.pieces(type).getBits(); }

// The position as Syzygy's WDL tables know them: right after a capture or a pawn move.
chess::Board zeroed(const chess::Board& board) {
    if (board.halfMoveClock() == 0) return board;
    std::istringstream fields(board.getFen());
    std::string placement, side, castling, ep;
    fields >> placement >> side >> castling >> ep;
    return chess::Board(placement + " " + side + " " + castling + " " + ep + " 0 1");
}

// A position probed: the side to move's rank and DTZ (when the DTZ tables are there), each legal move's rank for the
// side that plays it, with its DTZ from the position.
struct Probed {
    int rank = 0;
    std::optional<int> dtz;
    struct Move {
        chess::Move move;
        int rank = 0;
        std::optional<int> dtz;
        bool checkmate = false, stalemate = false, zeroing = false;
    };
    std::vector<Move> moves;
    bool checkmate = false, stalemate = false, insufficient = false;
};

// The legal move Pyrrhic's result names.
std::optional<chess::Move> moveOfResult(const chess::Movelist& legal, unsigned result) {
    const auto from = static_cast<int>(TB_GET_FROM(result)), to = static_cast<int>(TB_GET_TO(result));
    const auto promotes = TB_GET_PROMOTES(result);
    for (const auto& m : legal) {
        if (m.from().index() != from || m.to().index() != to) continue;
        if (m.typeOf() == chess::Move::PROMOTION) {
            const auto type     = m.promotionType();
            const unsigned flag = type == chess::PieceType::QUEEN    ? PYRRHIC_FLAG_QPROMO
                                  : type == chess::PieceType::ROOK   ? PYRRHIC_FLAG_RPROMO
                                  : type == chess::PieceType::BISHOP ? PYRRHIC_FLAG_BPROMO
                                                                     : PYRRHIC_FLAG_NPROMO;
            if (flag != promotes) continue;
        } else if (promotes != 0) {
            continue;
        }
        return m;
    }
    return std::nullopt;
}

// With lockSyzygy() held and the tables loaded: `board`'s ranks, none when the tables do not cover it.
std::optional<Probed> probeRaw(const chess::Board& board, int largest) {
    if (largest <= 0 || piecesOf(board) > largest || !board.castlingRights().isEmpty()) return std::nullopt;
    // A position where the side not to move is in check is none: the tables' code would take the king.
    if (board.isAttacked(board.kingSq(~board.sideToMove()), board.sideToMove())) return std::nullopt;
    Probed out;
    chess::Movelist legal;
    chess::movegen::legalmoves(legal, board);
    if (legal.empty()) {
        out.checkmate = board.inCheck();
        out.stalemate = !out.checkmate;
        out.rank      = out.checkmate ? -2 : 0;
        return out;
    }
    if (board.isInsufficientMaterial()) out.insufficient = true;
    const auto ep       = board.enpassantSq();
    const unsigned epsq = ep == chess::Square::NO_SQ ? 0u : static_cast<unsigned>(ep.index());
    const unsigned rule = std::min<unsigned>(board.halfMoveClock(), 255);
    const bool white    = board.sideToMove() == chess::Color::WHITE;
    unsigned results[TB_MAX_MOVES + 1];
    const unsigned found = tb_probe_root(board.us(chess::Color::WHITE).getBits(),
                                         board.us(chess::Color::BLACK).getBits(), bits(board, chess::PieceType::KING),
                                         bits(board, chess::PieceType::QUEEN), bits(board, chess::PieceType::ROOK),
                                         bits(board, chess::PieceType::BISHOP), bits(board, chess::PieceType::KNIGHT),
                                         bits(board, chess::PieceType::PAWN), rule, epsq, white, results);
    const auto describe  = [&](Probed::Move& m) {
        auto after = board;
        after.makeMove(m.move);
        chess::Movelist replies;
        chess::movegen::legalmoves(replies, after);
        m.checkmate = replies.empty() && after.inCheck();
        m.stalemate = replies.empty() && !after.inCheck();
        m.zeroing   = after.halfMoveClock() == 0;
        return after;
    };
    if (found != TB_RESULT_FAILED && found != TB_RESULT_CHECKMATE && found != TB_RESULT_STALEMATE) {
        out.rank = rankOfWdl(TB_GET_WDL(found));
        if (out.rank != 0) out.dtz = static_cast<int>(TB_GET_DTZ(found)) * (out.rank > 0 ? 1 : -1);
        for (std::size_t i = 0; i < TB_MAX_MOVES && results[i] != TB_RESULT_FAILED; ++i) {
            const auto move = moveOfResult(legal, results[i]);
            if (!move) return std::nullopt;
            Probed::Move m;
            m.move = *move;
            m.rank = rankOfWdl(TB_GET_WDL(results[i]));
            if (m.rank != 0) m.dtz = static_cast<int>(TB_GET_DTZ(results[i])) * (m.rank > 0 ? 1 : -1);
            describe(m);
            out.moves.push_back(m);
        }
        if (out.moves.size() == static_cast<std::size_t>(legal.size())) return out;
        out.moves.clear();
    }
    // Without the DTZ tables: the WDL tables, each position as if right after a capture or a pawn move (they only know
    // those), which errs only where the fifty-move rule is near.
    const auto root = zeroed(board);
    if (!fastchess::canProbeSyzgyWdl(root)) return std::nullopt;
    const auto r = fastchess::probeSyzygyWdl(root, false);
    if (r == chess::GameResult::NONE) return std::nullopt;
    out.rank = r == chess::GameResult::WIN ? 2 : r == chess::GameResult::LOSE ? -2 : 0;
    out.dtz.reset();
    for (const auto& move : legal) {
        Probed::Move m;
        m.move           = move;
        const auto after = describe(m);
        if (m.checkmate) {
            m.rank = 2;
        } else if (m.stalemate || after.isInsufficientMaterial()) {
            m.rank = 0;
        } else {
            const auto child = zeroed(after);
            if (!fastchess::canProbeSyzgyWdl(child)) return std::nullopt;
            const auto c = fastchess::probeSyzygyWdl(child, false);
            if (c == chess::GameResult::NONE) return std::nullopt;
            m.rank = c == chess::GameResult::WIN ? -2 : c == chess::GameResult::LOSE ? 2 : 0;
        }
        out.moves.push_back(m);
    }
    return out;
}

}  // namespace

// ---------------------------------------------------------------------------
// The catalogue

const std::vector<EndgameEntry>& endgameCatalogue() { return CATALOGUE; }

const EndgameEntry* endgameEntry(std::string_view id) {
    const auto it = std::find_if(CATALOGUE.begin(), CATALOGUE.end(), [&](const auto& e) { return e.id == id; });
    return it == CATALOGUE.end() ? nullptr : &*it;
}

const char* endgameGroupName(EndgameGroup group) {
    switch (group) {
        case EndgameGroup::Mates:
            return N_("Elementary mates");
        case EndgameGroup::Pawns:
            return N_("Pawns");
        case EndgameGroup::Rooks:
            return N_("Rooks");
        case EndgameGroup::Pieces:
            return N_("Pieces and queen");
    }
    return "";
}

int piecesOf(const chess::Board& board) { return static_cast<int>(board.occ().count()); }

int piecesOf(const std::string& fen) {
    int n = 0;
    for (const char c : fen) {
        if (c == ' ') break;
        if (std::isalpha(static_cast<unsigned char>(c))) ++n;
    }
    return n;
}

EndgameTask taskOf(const EndgameEntry& entry) {
    EndgameTask t;
    t.fen   = entry.fen;
    t.entry = entry.id;
    t.title = entry.name;
    t.goal  = entry.goal;
    t.mate  = entry.mate;
    t.idea  = entry.idea;
    return t;
}

// ---------------------------------------------------------------------------
// The tables' answers

std::optional<int> outcomeResult(lichess::Outcome outcome) {
    switch (outcome) {
        case lichess::Outcome::Win:
        case lichess::Outcome::MaybeWin:
            return 1;
        case lichess::Outcome::Draw:
        case lichess::Outcome::CursedWin:
        case lichess::Outcome::BlessedLoss:
            return 0;
        case lichess::Outcome::Loss:
        case lichess::Outcome::MaybeLoss:
            return -1;
        case lichess::Outcome::Unknown:
            break;
    }
    return std::nullopt;
}

std::optional<int> moveResult(const lichess::TablebaseMove& move) {
    if (move.checkmate) return 1;
    if (move.stalemate) return 0;
    const auto theirs = outcomeResult(move.outcome);
    if (!theirs) return std::nullopt;
    return -*theirs;
}

int moveRank(const lichess::TablebaseMove& move) {
    if (move.checkmate) return 2;
    switch (move.outcome) {
        case lichess::Outcome::Loss:
        case lichess::Outcome::MaybeLoss:
            return 2;
        case lichess::Outcome::BlessedLoss:
            return 1;
        case lichess::Outcome::CursedWin:
            return -1;
        case lichess::Outcome::Win:
        case lichess::Outcome::MaybeWin:
            return -2;
        default:
            return 0;
    }
}

void sortTableMoves(lichess::Tablebase& answer, const std::map<std::string, int>& replies) {
    const auto left = [&](const lichess::TablebaseMove& m) {
        const auto it = replies.find(m.uci);
        return it == replies.end() ? 1000 : it->second;
    };
    std::stable_sort(answer.moves.begin(), answer.moves.end(), [&](const auto& a, const auto& b) {
        const int ra = moveRank(a), rb = moveRank(b);
        if (ra != rb) return ra > rb;
        if (a.checkmate != b.checkmate) return a.checkmate;
        if (ra != 0) {
            // The quickest win, the longest loss: to mate when both say it, else to the next capture or pawn move.
            const auto distance = [](const lichess::TablebaseMove& m) -> std::optional<int> {
                if (m.dtm) return std::abs(*m.dtm);
                if (m.dtz) return std::abs(*m.dtz);
                return std::nullopt;
            };
            const bool mates = a.dtm && b.dtm;
            const auto da    = mates ? std::optional<int>(std::abs(*a.dtm)) : distance(a);
            const auto db    = mates ? std::optional<int>(std::abs(*b.dtm)) : distance(b);
            if (da && db && *da != *db) return ra > 0 ? *da < *db : *da > *db;
        }
        return left(a) < left(b);
    });
}

std::optional<lichess::Tablebase> probeDiskTables(const chess::Board& board, const std::string& paths) {
    const int largest = loadSyzygy(paths);
    const auto probed = probeRaw(board, largest);
    if (!probed) return std::nullopt;
    lichess::Tablebase t;
    t.outcome               = outcomeOfRank(probed->rank);
    t.dtz                   = probed->dtz;
    t.checkmate             = probed->checkmate;
    t.stalemate             = probed->stalemate;
    t.insufficient_material = probed->insufficient;
    if (t.checkmate) t.outcome = lichess::Outcome::Loss;
    int best = -3;
    for (const auto& m : probed->moves) best = std::max(best, m.rank);
    std::map<std::string, int> replies;
    std::size_t at_best = 0;
    for (const auto& m : probed->moves) at_best += m.rank == best ? 1 : 0;
    for (const auto& m : probed->moves) {
        lichess::TablebaseMove move;
        move.uci       = chess::uci::moveToUci(m.move);
        move.san       = chess::uci::moveToSan(board, m.move);
        move.outcome   = leftOfRank(m.rank);
        move.checkmate = m.checkmate;
        move.stalemate = m.stalemate;
        move.zeroing   = m.zeroing;
        if (m.dtz) move.dtz = -*m.dtz;  // as Lichess gives it: for the side to move after the move
        // Between moves as good, the one that leaves the other side the fewest moves keeping its outcome: the tables
        // make it hard.
        if (m.rank == best && at_best > 1 && !m.checkmate) {
            auto after = board;
            after.makeMove(m.move);
            if (const auto next = probeRaw(after, largest)) {
                int keep = 0;
                for (const auto& r : next->moves) keep += r.rank == next->rank ? 1 : 0;
                replies[move.uci] = keep;
            }
        }
        t.moves.push_back(std::move(move));
    }
    sortTableMoves(t, replies);
    return t;
}

std::optional<std::string> tablesMove(const lichess::Tablebase& answer) {
    if (answer.moves.empty()) return std::nullopt;
    return answer.moves.front().uci;
}

EndgameJudgement judgeEndgameMove(const lichess::Tablebase& before, std::string_view uci) {
    EndgameJudgement j;
    const auto now = outcomeResult(before.outcome);
    if (!now) return j;
    const auto it = std::find_if(before.moves.begin(), before.moves.end(), [&](const auto& m) { return m.uci == uci; });
    if (it == before.moves.end()) return j;
    const auto after = moveResult(*it);
    if (!after) return j;
    j.known  = true;
    j.before = *now;
    j.after  = *after;
    for (const auto& m : before.moves)
        if (const auto r = moveResult(m); r && *r == *now) j.kept.push_back(m.uci);
    return j;
}

EndgameOutcome endgameOutcome(const chess::Board& board, chess::Move last, bool capture, chess::Color user,
                              const EndgameTask& task, std::optional<int> user_result, int held) {
    const auto [reason, result] = board.isGameOver();
    (void)result;
    if (reason == chess::GameResultReason::CHECKMATE)
        return board.sideToMove() == user ? EndgameOutcome::Lost : EndgameOutcome::Won;
    if (reason != chess::GameResultReason::NONE)
        return task.goal == EndgameGoal::Draw ? EndgameOutcome::Held : EndgameOutcome::Drawn;
    if (task.goal == EndgameGoal::Win) {
        if (task.mate || !user_result || *user_result <= 0) return EndgameOutcome::Going;
        const bool users = board.sideToMove() != user;  // the last move was the user's
        if (users && last.typeOf() == chess::Move::PROMOTION) return EndgameOutcome::Won;
        if (users && capture && board.us(~user).count() == 1) return EndgameOutcome::Won;
        return EndgameOutcome::Going;
    }
    if (held >= HOLD_MOVES && (!user_result || *user_result >= 0)) return EndgameOutcome::Held;
    return EndgameOutcome::Going;
}

std::optional<int> tablesMateMoves(const lichess::Tablebase& answer) {
    if (outcomeResult(answer.outcome).value_or(0) <= 0) return std::nullopt;
    if (answer.dtm) return (std::abs(*answer.dtm) + 1) / 2;
    if (answer.dtz) return (std::abs(*answer.dtz) + 1) / 2;
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// What the profile keeps

std::string positionKey(const std::string& fen) {
    std::istringstream fields(fen);
    std::string placement, side, castling, ep;
    fields >> placement >> side >> castling >> ep;
    return placement + " " + side + " " + castling + " " + ep;
}

EndgameRecord readEndgameRecord(const fs::path& folder) {
    EndgameRecord out;
    std::ifstream in(folder / "record.json", std::ios::binary);
    if (!in) return out;
    const auto j = nlohmann::json::parse(in, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return out;
    out.answer_ms = j.value("answer_ms", int64_t{0});
    if (const auto it = j.find("results"); it != j.end() && it->is_object()) {
        for (const auto& [id, r] : it->items()) {
            if (!r.is_object()) continue;
            EndgameResult e;
            e.tries         = r.value("tries", 0);
            e.done          = r.value("done", 0);
            e.clean         = r.value("clean", false);
            e.best          = r.value("best", 0);
            e.tables        = r.value("tables", 0);
            e.last          = r.value("last", int64_t{0});
            e.missed_last   = r.value("missed_last", false);
            out.results[id] = e;
        }
    }
    if (const auto it = j.find("cards"); it != j.end() && it->is_object()) {
        for (const auto& [key, c] : it->items()) {
            if (!c.is_object()) continue;
            EndgameCard card;
            card.fen             = c.value("fen", "");
            card.entry           = c.value("entry", "");
            card.title           = c.value("title", "");
            card.goal            = c.value("goal", "win") == "draw" ? EndgameGoal::Draw : EndgameGoal::Win;
            card.mate            = c.value("mate", false);
            card.since           = c.value("since", int64_t{0});
            const int state      = c.value("state", 1);
            card.card.state      = state == 2   ? study::fsrs::State::Review
                                   : state == 3 ? study::fsrs::State::Relearning
                                                : study::fsrs::State::Learning;
            card.card.step       = c.value("step", 0);
            card.card.stability  = c.value("stability", 0.0);
            card.card.difficulty = c.value("difficulty", 0.0);
            card.card.due        = c.value("due", int64_t{0});
            card.card.last       = c.value("last", int64_t{0});
            card.card.reps       = c.value("reps", 0);
            card.card.lapses     = c.value("lapses", 0);
            if (card.fen.empty()) continue;
            try {
                (void)chess::Board(card.fen);
            } catch (...) {
                continue;
            }
            out.cards[key] = std::move(card);
        }
    }
    return out;
}

bool writeEndgameRecord(const fs::path& folder, const EndgameRecord& record) {
    std::error_code ec;
    fs::create_directories(folder, ec);
    nlohmann::json results = nlohmann::json::object();
    for (const auto& [id, e] : record.results)
        results[id] = {{"tries", e.tries},
                       {"done", e.done},
                       {"clean", e.clean},
                       {"best", e.best},
                       {"tables", e.tables},
                       {"last", e.last},
                       {"missed_last", e.missed_last}};
    nlohmann::json cards = nlohmann::json::object();
    for (const auto& [key, c] : record.cards)
        cards[key] = {{"fen", c.fen},
                      {"entry", c.entry},
                      {"title", c.title},
                      {"goal", c.goal == EndgameGoal::Draw ? "draw" : "win"},
                      {"mate", c.mate},
                      {"since", c.since},
                      {"state", static_cast<int>(c.card.state)},
                      {"step", c.card.step},
                      {"stability", c.card.stability},
                      {"difficulty", c.card.difficulty},
                      {"due", c.card.due},
                      {"last", c.card.last},
                      {"reps", c.card.reps},
                      {"lapses", c.card.lapses}};
    return writeJson(folder / "record.json", {{"version", 1},
                                              {"results", std::move(results)},
                                              {"cards", std::move(cards)},
                                              {"answer_ms", record.answer_ms}});
}

std::size_t endgameCardsDue(const EndgameRecord& record, int64_t limit) {
    return static_cast<std::size_t>(std::count_if(record.cards.begin(), record.cards.end(),
                                                  [&](const auto& c) { return c.second.card.due <= limit; }));
}

EndgameCard missedEndgameCard(const EndgameTask& task, const std::string& fen, int64_t now, int64_t day_end,
                              const study::fsrs::Scheduler& scheduler) {
    EndgameCard out;
    out.fen      = fen;
    out.entry    = task.entry;
    out.title    = task.title;
    out.goal     = task.goal;
    out.mate     = task.mate;
    out.since    = now;
    out.card     = scheduler.review(study::fsrs::Card{}, study::fsrs::Rating::Again, now);
    out.card.due = std::max(out.card.due, day_end + 1);  // not again in the session: the next training day
    return out;
}

study::fsrs::Rating endgameRating(bool reached, bool fault, bool helped) {
    if (!reached || fault) return study::fsrs::Rating::Again;
    return helped ? study::fsrs::Rating::Hard : study::fsrs::Rating::Good;
}

}  // namespace fcg
