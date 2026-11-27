#include "board_tables.hpp"

#include <array>
#include <cstdlib>

#include "endgames.hpp"
#include "https.hpp"
#include "i18n.hpp"
#include "online_cache.hpp"

namespace fcg {

namespace {

const char* sideName(chess::Color side) { return side == chess::Color::WHITE ? TR("White") : TR("Black"); }

TablesGroup groupOfOutcome(lichess::Outcome outcome) {
    switch (outcome) {
        case lichess::Outcome::Win:
        case lichess::Outcome::MaybeWin:
            return TablesGroup::Win;
        case lichess::Outcome::CursedWin:
            return TablesGroup::CursedWin;
        case lichess::Outcome::Draw:
            return TablesGroup::Draw;
        case lichess::Outcome::BlessedLoss:
            return TablesGroup::BlessedLoss;
        case lichess::Outcome::Loss:
        case lichess::Outcome::MaybeLoss:
            return TablesGroup::Loss;
        case lichess::Outcome::Unknown:
            break;
    }
    return TablesGroup::Unknown;
}

}  // namespace

bool op1Position(const chess::Board& board) {
    if (board.occ().count() != 8 || !board.castlingRights().isEmpty()) return false;
    const auto black_pawns = board.pieces(chess::PieceType::PAWN, chess::Color::BLACK);
    bool opposed           = false;
    for (auto w = board.pieces(chess::PieceType::PAWN, chess::Color::WHITE); !w.empty() && !opposed;) {
        const int square = w.pop();
        for (int ahead = square + 8; ahead < 64 && !opposed; ahead += 8) opposed = black_pawns.check(ahead);
    }
    const auto more_than_lone_pawn = [&](chess::Color c) {
        const auto pawns = board.pieces(chess::PieceType::PAWN, c);
        const auto rest  = board.us(c).count() - pawns.count() - 1;  // without the king
        return pawns.count() > 1 || rest > 0;
    };
    return opposed && more_than_lone_pawn(chess::Color::WHITE) && more_than_lone_pawn(chess::Color::BLACK);
}

TablesReach tablesReach(const chess::Board& board, bool chess960) {
    if (chess960) return TablesReach::Chess960;
    const int pieces = static_cast<int>(board.occ().count());
    if (pieces > 7 && !op1Position(board)) return TablesReach::TooMany;
    if (!board.castlingRights().isEmpty()) return TablesReach::Castling;
    return TablesReach::Yes;
}

TablesWay tablesWay(int pieces, bool op1, int disk_pieces, bool disk_locked, int lichess_setting, bool once) {
    if (pieces <= 7 && !disk_locked && disk_pieces >= pieces) return TablesWay::Disk;
    if (pieces > 7 && !op1) return TablesWay::Beyond;
    if (lichess_setting == 2) return TablesWay::Refused;
    if (lichess_setting == 1 || once) return TablesWay::Lichess;
    return TablesWay::Consent;
}

TablesGroup positionGroup(const lichess::Tablebase& answer) {
    if (answer.checkmate) return TablesGroup::Loss;
    if (answer.stalemate || answer.insufficient_material) return TablesGroup::Draw;
    return groupOfOutcome(answer.outcome);
}

TablesGroup moveGroup(const lichess::TablebaseMove& move) {
    if (move.checkmate) return TablesGroup::Win;
    if (move.stalemate) return TablesGroup::Draw;
    // The tables give the outcome the move leaves the other side.
    switch (groupOfOutcome(move.outcome)) {
        case TablesGroup::Win:
            return TablesGroup::Loss;
        case TablesGroup::CursedWin:
            return TablesGroup::BlessedLoss;
        case TablesGroup::Draw:
            return TablesGroup::Draw;
        case TablesGroup::BlessedLoss:
            return TablesGroup::CursedWin;
        case TablesGroup::Loss:
            return TablesGroup::Win;
        case TablesGroup::Unknown:
            break;
    }
    return TablesGroup::Unknown;
}

std::vector<TablesMoves> groupTablesMoves(const lichess::Tablebase& answer) {
    static constexpr std::array<TablesGroup, 6> order{TablesGroup::Win,  TablesGroup::CursedWin,
                                                      TablesGroup::Draw, TablesGroup::BlessedLoss,
                                                      TablesGroup::Loss, TablesGroup::Unknown};
    std::vector<TablesMoves> out;
    for (const auto g : order) {
        TablesMoves group;
        group.group = g;
        for (std::size_t i = 0; i < answer.moves.size(); ++i)
            if (moveGroup(answer.moves[i]) == g) group.moves.push_back(i);
        if (!group.moves.empty()) out.push_back(std::move(group));
    }
    return out;
}

bool spoilsOutcome(const lichess::Tablebase& answer, const lichess::TablebaseMove& move) {
    const auto now   = outcomeResult(answer.outcome);
    const auto after = moveResult(move);
    return now && after && *after < *now;
}

int movesOfPlies(int plies) { return (std::abs(plies) + 1) / 2; }

std::string tablesVerdict(TablesGroup group, chess::Color side_to_move) {
    const auto* side  = sideName(side_to_move);
    const auto* other = sideName(~side_to_move);
    switch (group) {
        case TablesGroup::Win:
            return trf("{} wins", side);
        case TablesGroup::CursedWin:
            return trf("Won for {}, but drawn by the fifty-move rule", side);
        case TablesGroup::Draw:
            return TR("Draw");
        case TablesGroup::BlessedLoss:
            return trf("Won for {}, but drawn by the fifty-move rule", other);
        case TablesGroup::Loss:
            return trf("{} wins", other);
        case TablesGroup::Unknown:
            break;
    }
    return TR("Not in the tables");
}

std::string outcomeVerdict(lichess::Outcome outcome, chess::Color side_to_move) {
    if (outcome == lichess::Outcome::MaybeWin || outcome == lichess::Outcome::MaybeLoss)
        return trf("{} wins, maybe: the 50-move rule and the game played do not settle it",
                   sideName(outcome == lichess::Outcome::MaybeWin ? side_to_move : ~side_to_move));
    return tablesVerdict(groupOfOutcome(outcome), side_to_move);
}

std::string tablesDistance(std::optional<int> dtm, std::optional<int> dtz, std::optional<int> dtc) {
    if (dtm && *dtm != 0) {
        const int n = movesOfPlies(*dtm);
        return trn("mate in {} move", "mate in {} moves", n, n);
    }
    if (dtz && *dtz != 0) {
        const int n = movesOfPlies(*dtz);
        return trn("{} move to a capture or a pawn move", "{} moves to a capture or a pawn move", n, n);
    }
    if (dtc && *dtc != 0) {
        const int n = std::abs(*dtc);
        return trn("{} move to the next capture, promotion or mate", "{} moves to the next capture, promotion or mate",
                   n, n);
    }
    return {};
}

const char* tablesMetric(std::optional<int> dtm, std::optional<int> dtz, std::optional<int> dtc) {
    if (dtm && *dtm != 0) return "DTM";
    if (dtz && *dtz != 0) return "DTZ";
    if (dtc && *dtc != 0) return "DTC";
    return "";
}

std::string tablesGroupTitle(TablesGroup group, TablesGroup position) {
    switch (group) {
        case TablesGroup::Win:
            return position == TablesGroup::Win ? TR("Keep the win") : TR("Win");
        case TablesGroup::CursedWin:
            return TR("Won, but drawn by the fifty-move rule");
        case TablesGroup::Draw:
            if (position == TablesGroup::Win) return TR("Throw the win away: a draw");
            return position == TablesGroup::Draw || position == TablesGroup::CursedWin ? TR("Keep the draw")
                                                                                       : TR("A draw");
        case TablesGroup::BlessedLoss:
            return TR("Lost, but saved by the fifty-move rule");
        case TablesGroup::Loss:
            return position == TablesGroup::Loss ? TR("Lose: the longest resistance first") : TR("Lose");
        case TablesGroup::Unknown:
            break;
    }
    return TR("Not in the tables");
}

std::string afterMoveWords(const lichess::TablebaseMove& move, chess::Color mover) {
    if (move.checkmate) return TR("checkmate");
    if (move.stalemate) return TR("stalemate");
    // The entry is the other side's, to move after it.
    const auto verdict  = tablesVerdict(groupOfOutcome(move.outcome), ~mover);
    const auto distance = tablesDistance(move.dtm, move.dtz, move.dtc);
    return distance.empty() ? verdict : trf("{}: {}", verdict, distance);
}

std::string tablesSource(bool online, int pieces, int disk_pieces) {
    if (!online) return trf("Your tables on the disk (Syzygy, up to {} pieces)", disk_pieces);
    if (pieces > 7) return TR("Lichess's 8-piece tables (op1)");
    if (disk_pieces <= 0) return TR("Lichess's 7-piece tables (none on your disk)");
    return trf("Lichess's 7-piece tables (yours on the disk go up to {} pieces)", disk_pieces);
}

lichess::Answer<lichess::Tablebase> keptOrAskedTables(const std::filesystem::path& root, const std::string& fen,
                                                      bool& asked, const std::atomic<bool>* cancel) {
    asked = false;
    if (auto kept = cachedTablebase(root, fen); kept && kept->outcome != lichess::Outcome::Unknown) {
        lichess::Answer<lichess::Tablebase> a;
        a.value  = std::move(kept);
        a.status = 200;
        return a;
    }
    asked  = true;
    auto a = lichess::tablebase(fen, cancel);
    // Kept for good, a verdict not changing; not a position the tables do not know (op1's eight pieces mostly), which
    // they may know later.
    if (a.value && a.value->outcome != lichess::Outcome::Unknown) keepTablebase(root, fen, *a.value);
    return a;
}

lichess::Answer<std::vector<std::string>> keptOrAskedLine(const std::filesystem::path& root, const std::string& fen,
                                                          bool& asked, const std::atomic<bool>* cancel) {
    asked = false;
    if (auto kept = cachedTablebaseLine(root, fen)) {
        lichess::Answer<std::vector<std::string>> a;
        a.value  = std::move(kept);
        a.status = 200;
        return a;
    }
    asked  = true;
    auto a = lichess::tablebaseMainline(fen, cancel);
    if (a.value && !a.value->empty()) keepTablebaseLine(root, fen, *a.value);
    return a;
}

std::chrono::milliseconds tablesPause() {
    return HostGate::instance().pausedFor(urlHost(lichess::servers().tablebase));
}

std::string tablesNothing(const lichess::Failure& answer, std::chrono::milliseconds pause) {
    if (answer.net == lichess::NetError::RateLimited && pause.count() > 0) {
        const auto s = std::chrono::duration_cast<std::chrono::seconds>(pause).count();
        return trf("Too many requests in a short time: Lichess asked for a pause. Trying again in {}:{:02}.", s / 60,
                   s % 60);
    }
    if (answer.failed()) return lichess::gaveNothing(answer);
    return TR("Lichess's tables do not know this position.");
}

bool tablesAskAgain(const lichess::Failure& answer, std::chrono::milliseconds pause) {
    return answer.net == lichess::NetError::RateLimited && pause.count() == 0;
}

std::optional<std::optional<int>> checkedVerdict(const lichess::Answer<lichess::Tablebase>& answer,
                                                 std::string_view fen) {
    if (answer.failed()) return std::nullopt;
    std::optional<int> says;
    if (answer.value && answer.value->outcome != lichess::Outcome::Unknown)
        if (const auto r = outcomeResult(answer.value->outcome))
            says = *r * (fen.find(" b ") == std::string_view::npos ? 1 : -1);  // the side to move's, made White's
    return says;
}

}  // namespace fcg
