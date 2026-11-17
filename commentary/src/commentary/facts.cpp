#include "facts.hpp"

#include <algorithm>
#include <type_traits>

namespace fcg::commentary {

namespace {

template <typename>
inline constexpr bool always_false = false;

template <typename T, typename... U>
inline constexpr bool is_one_of = (std::is_same_v<T, U> || ...);

}  // namespace

std::optional<Color> beneficiary(const Fact& fact) {
    using namespace fact;
    return std::visit(
        [](const auto& f) -> std::optional<Color> {
            using T = std::decay_t<decltype(f)>;
            if constexpr (is_one_of<T, Fork, DiscoveredAttack, Skewer>) {
                return f.attacker.color;
            } else if constexpr (std::is_same_v<T, Pin>) {
                return f.pinner.color;
            } else if constexpr (std::is_same_v<T, DoubleCheck>) {
                return f.by;
            } else if constexpr (is_one_of<T, Hanging, Trapped>) {
                return !f.piece.color;
            } else if constexpr (is_one_of<T, IsolatedPawn, BackwardPawn>) {
                return !f.pawn.color;
            } else if constexpr (std::is_same_v<T, BadBishop>) {
                return !f.bishop.color;
            } else if constexpr (std::is_same_v<T, ExposedKing>) {
                return !f.king.color;
            } else if constexpr (is_one_of<T, RookOnOpenFile, RookOnSeventh>) {
                return f.rook.color;
            } else if constexpr (std::is_same_v<T, Outpost>) {
                return f.knight.color;
            } else if constexpr (std::is_same_v<T, PawnBreak>) {
                return f.pawn.color;
            } else if constexpr (std::is_same_v<T, LongDiagonal>) {
                return f.bishop.color;
            } else if constexpr (std::is_same_v<T, ActiveKing>) {
                return f.king.color;
            } else if constexpr (is_one_of<T, PerpetualCheck, Stalemate, Endgame, DevelopmentLead, Compensation,
                                           Initiative, Counterplay, KingAttack, MoreActive, CentreOpens, CentreLocks>) {
                return f.side;
            } else if constexpr (std::is_same_v<T, PawnCentre>) {
                return f.color;
            } else if constexpr (std::is_same_v<T, Hole>) {
                return !f.color;
            } else if constexpr (is_one_of<T, Deflection, Attraction, Neglects, Unguards>) {
                return !f.piece.color;
            } else if constexpr (std::is_same_v<T, RemovesDefender>) {
                return !f.defender.color;
            } else if constexpr (std::is_same_v<T, Overloaded>) {
                return !f.piece.color;
            } else if constexpr (std::is_same_v<T, Clearance>) {
                return f.piece.color;
            } else if constexpr (std::is_same_v<T, Breakthrough>) {
                return f.pawn.color;
            } else if constexpr (std::is_same_v<T, Race>) {
                return f.side;
            } else if constexpr (std::is_same_v<T, OpensLine>) {
                return f.slider.color;
            } else if constexpr (std::is_same_v<T, Guards>) {
                return f.guarded.color;
            } else if constexpr (is_one_of<T, Develops, Pressure, Blockade>) {
                return f.piece.color;
            } else if constexpr (std::is_same_v<T, MovesAgain>) {
                return !f.piece.color;
            } else if constexpr (is_one_of<T, ProtectedPasser, OutsidePasser>) {
                return f.pawn.color;
            } else if constexpr (std::is_same_v<T, RookBehindPasser>) {
                return f.rook.color;
            } else if constexpr (std::is_same_v<T, ConnectedPassers>) {
                return f.color;
            } else if constexpr (is_one_of<T, Simplifies, PawnStorm, MinorityAttack, Space, fact::Formation,
                                           Opposition>) {
                return f.side;
            } else if constexpr (is_one_of<T, GainsSpace, Outruns>) {
                return f.pawn.color;
            } else if constexpr (is_one_of<T, EntersSquare, KeySquare>) {
                return f.king.color;
            } else if constexpr (is_one_of<T, CutsOff, Philidor>) {
                return f.rook.color;
            } else if constexpr (is_one_of<T, Majority, Luft, Lucena>) {
                return f.side;
            } else if constexpr (std::is_same_v<T, WeakColour>) {
                return !f.side;
            } else if constexpr (std::is_same_v<T, WrongBishop>) {
                return !f.bishop.color;
            } else if constexpr (std::is_same_v<T, Saves>) {
                return f.piece.color;
            } else if constexpr (std::is_same_v<T, TakesWith>) {
                return f.by.color;
            } else if constexpr (is_one_of<T, Intermezzo, Covers, Retakes>) {
                return f.side;
            } else if constexpr (is_one_of<T, Zugzwang, NotRetaken>) {
                return !f.side;
            } else if constexpr (std::is_same_v<T, HangingPawns>) {
                return !f.color;
            } else if constexpr (std::is_same_v<T, LosesCastling>) {
                return !f.side;
            } else if constexpr (is_one_of<T, WinsMaterial, Sacrifice, Mate, ThreatensMate, ThreatensCapture, Promotes,
                                           fact::Castles>) {
                return f.side;
            } else if constexpr (std::is_same_v<T, PassedPawn>) {
                return f.pawn.color;
            } else if constexpr (std::is_same_v<T, DoubledPawns>) {
                return !f.color;
            } else if constexpr (std::is_same_v<T, BishopPair>) {
                return f.color;
            } else if constexpr (std::is_same_v<T, Tablebase>) {
                if (f.verdict == Verdict::Win) return f.side;
                if (f.verdict == Verdict::Loss) return !f.side;
                return std::nullopt;
            } else if constexpr (is_one_of<T, SpoilsWin, SpoilsDraw>) {
                return !f.side;
            } else if constexpr (std::is_same_v<T, OnlyMoveToKeep>) {
                return f.side;
            } else {
                static_assert(always_false<T>, "a fact without its beneficiary");
            }
        },
        fact);
}

bool isPlan(const Fact& fact) {
    using namespace fact;
    if (const auto* p = std::get_if<PawnBreak>(&fact)) return p->lever.has_value();
    return std::holds_alternative<fact::Formation>(fact) || std::holds_alternative<Space>(fact) ||
           std::holds_alternative<MinorityAttack>(fact) || std::holds_alternative<PawnStorm>(fact) ||
           std::holds_alternative<Simplifies>(fact) || std::holds_alternative<Opposition>(fact) ||
           std::holds_alternative<CutsOff>(fact) || std::holds_alternative<EntersSquare>(fact) ||
           std::holds_alternative<Majority>(fact) || std::holds_alternative<KeySquare>(fact) ||
           std::holds_alternative<Lucena>(fact) || std::holds_alternative<Philidor>(fact);
}

uint32_t salience(const Fact& fact) {
    using namespace fact;
    return std::visit(
        [](const auto& f) -> uint32_t {
            using T = std::decay_t<decltype(f)>;
            if constexpr (std::is_same_v<T, Mate>) {
                return 100;
            } else if constexpr (std::is_same_v<T, ThreatensMate>) {
                return 90;
            } else if constexpr (std::is_same_v<T, WinsMaterial>) {
                // Below the tactics that win it: the mechanism tells more than the count.
                return 55 + std::min<uint32_t>(static_cast<uint32_t>(f.pawns), 9);
            } else if constexpr (is_one_of<T, Fork, Skewer, DoubleCheck>) {
                return 70;
            } else if constexpr (std::is_same_v<T, Trapped>) {
                return 68;
            } else if constexpr (std::is_same_v<T, DiscoveredAttack>) {
                return 66;
            } else if constexpr (std::is_same_v<T, Pin>) {
                return 64;
            } else if constexpr (std::is_same_v<T, Hanging>) {
                return 62;
            } else if constexpr (std::is_same_v<T, Zugzwang>) {
                return 60;
            } else if constexpr (std::is_same_v<T, Sacrifice>) {
                return 58;
            } else if constexpr (std::is_same_v<T, ThreatensCapture>) {
                return 50;
            } else if constexpr (std::is_same_v<T, Promotes>) {
                return 45;
            } else if constexpr (std::is_same_v<T, ExposedKing>) {
                return 32;
            } else if constexpr (std::is_same_v<T, PassedPawn>) {
                return 30;
            } else if constexpr (std::is_same_v<T, RookOnSeventh>) {
                return 28;
            } else if constexpr (std::is_same_v<T, Outpost>) {
                return 26;
            } else if constexpr (std::is_same_v<T, RookOnOpenFile>) {
                return 24;
            } else if constexpr (std::is_same_v<T, LosesCastling>) {
                return 18;
            } else if constexpr (std::is_same_v<T, fact::Castles>) {
                return 20;
            } else if constexpr (is_one_of<T, BishopPair, IsolatedPawn, DoubledPawns>) {
                return 15;
            } else if constexpr (is_one_of<T, PerpetualCheck, Stalemate>) {
                return 74;
            } else if constexpr (std::is_same_v<T, Compensation>) {
                return 42;
            } else if constexpr (std::is_same_v<T, Initiative>) {
                return 38;
            } else if constexpr (std::is_same_v<T, KingAttack>) {
                return 34;
            } else if constexpr (std::is_same_v<T, MoreActive>) {
                return 24;
            } else if constexpr (std::is_same_v<T, LongDiagonal>) {
                return 20;
            } else if constexpr (is_one_of<T, CentreOpens, CentreLocks>) {
                return 19;
            } else if constexpr (std::is_same_v<T, Hole>) {
                return 18;
            } else if constexpr (std::is_same_v<T, Counterplay>) {
                return 36;
            } else if constexpr (std::is_same_v<T, DevelopmentLead>) {
                return 30;
            } else if constexpr (std::is_same_v<T, ActiveKing>) {
                return 25;
            } else if constexpr (is_one_of<T, PawnBreak, PawnCentre>) {
                return 22;
            } else if constexpr (std::is_same_v<T, Endgame>) {
                return 21;
            } else if constexpr (std::is_same_v<T, BadBishop>) {
                return 17;
            } else if constexpr (is_one_of<T, BackwardPawn, HangingPawns>) {
                return 16;
            } else if constexpr (std::is_same_v<T, SpoilsWin>) {
                return 85;
            } else if constexpr (std::is_same_v<T, SpoilsDraw>) {
                return 80;
            } else if constexpr (std::is_same_v<T, OnlyMoveToKeep>) {
                return 75;
            } else if constexpr (std::is_same_v<T, Tablebase>) {
                return 40;
            } else if constexpr (is_one_of<T, Deflection, Attraction, Overloaded>) {
                return 69;
            } else if constexpr (std::is_same_v<T, Clearance>) {
                return 65;
            } else if constexpr (std::is_same_v<T, Breakthrough>) {
                return 66;  // more than the pawn it gives
            } else if constexpr (std::is_same_v<T, Race>) {
                return 47;
            } else if constexpr (std::is_same_v<T, RemovesDefender>) {
                return 67;
            } else if constexpr (std::is_same_v<T, NotRetaken>) {
                return 72;  // not taking back is the whole story of a move
            } else if constexpr (std::is_same_v<T, TakesWith>) {
                return 65;
            } else if constexpr (is_one_of<T, Unguards, Neglects>) {
                return 63;  // why the answer works: told before what it wins
            } else if constexpr (is_one_of<T, Guards, Saves>) {
                return 61;
            } else if constexpr (std::is_same_v<T, Intermezzo>) {
                return 60;
            } else if constexpr (std::is_same_v<T, Covers>) {
                return 59;
            } else if constexpr (std::is_same_v<T, OpensLine>) {
                return 52;
            } else if constexpr (std::is_same_v<T, Retakes>) {
                return 40;
            } else if constexpr (std::is_same_v<T, ConnectedPassers>) {
                return 33;
            } else if constexpr (is_one_of<T, ProtectedPasser, OutsidePasser, PawnStorm>) {
                return 31;
            } else if constexpr (std::is_same_v<T, MinorityAttack>) {
                return 30;
            } else if constexpr (std::is_same_v<T, Simplifies>) {
                return 29;
            } else if constexpr (std::is_same_v<T, MovesAgain>) {
                return 28;
            } else if constexpr (is_one_of<T, Pressure, RookBehindPasser, Blockade>) {
                return 27;
            } else if constexpr (std::is_same_v<T, Develops>) {
                return 23;
            } else if constexpr (std::is_same_v<T, WrongBishop>) {
                return 60;  // a draw the books know by heart, as good as a perpetual check
            } else if constexpr (is_one_of<T, KeySquare, Lucena, Philidor>) {
                return 49;  // what decides an endgame of few pieces
            } else if constexpr (std::is_same_v<T, Luft>) {
                return 33;
            } else if constexpr (std::is_same_v<T, Majority>) {
                return 26;
            } else if constexpr (std::is_same_v<T, WeakColour>) {
                return 24;
            } else if constexpr (std::is_same_v<T, Outruns>) {
                return 48;
            } else if constexpr (is_one_of<T, Opposition, EntersSquare>) {
                return 46;
            } else if constexpr (std::is_same_v<T, CutsOff>) {
                return 44;
            } else if constexpr (std::is_same_v<T, fact::Formation>) {
                return 27;  // the formation says more than the space it gives
            } else if constexpr (std::is_same_v<T, Space>) {
                return 26;
            } else if constexpr (std::is_same_v<T, GainsSpace>) {
                return 21;
            } else {
                static_assert(always_false<T>, "a fact without its salience");
            }
        },
        fact);
}

}  // namespace fcg::commentary
