#pragma once

// Written by tools/port/gen_facts.py from the Rust prototype's enum Fact (crates/facts/src/lib.rs): do not edit.
// The facts the detectors find, which the comments may speak of and nothing else.

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "fact_parts.hpp"

namespace fcg::commentary {

namespace fact {

// `attacker` hits two or more pieces at once.
struct Fork {
    commentary::Placed attacker{};
    std::vector<commentary::Placed> targets{};
    bool operator==(const Fork&) const = default;
};

// `pinned` cannot move without exposing `behind` to `pinner`.
struct Pin {
    commentary::Placed pinner{};
    commentary::Placed pinned{};
    commentary::Placed behind{};
    bool operator==(const Pin&) const = default;
};

// `front` is attacked and, when it steps aside, `behind` falls.
struct Skewer {
    commentary::Placed attacker{};
    commentary::Placed front{};
    commentary::Placed behind{};
    bool operator==(const Skewer&) const = default;
};

// A piece moved out of `attacker`'s way, uncovering an attack on `target`.
struct DiscoveredAttack {
    commentary::Placed attacker{};
    commentary::Placed target{};
    bool operator==(const DiscoveredAttack&) const = default;
};

struct DoubleCheck {
    commentary::Color by{};
    bool operator==(const DoubleCheck&) const = default;
};

// `piece` can be won: taken for nothing, or for less than it is worth.
struct Hanging {
    commentary::Placed piece{};
    bool operator==(const Hanging&) const = default;
};

// `piece` is attacked and has nowhere safe to go.
struct Trapped {
    commentary::Placed piece{};
    bool operator==(const Trapped&) const = default;
};

// Once the captures settle, `side` is this many pawns ahead of where it started, and
// `trade` says in what, when the line shows it. `taken` is the piece it comes down to,
// where it stands when the line starts, when the gain is that one piece.
struct WinsMaterial {
    commentary::Color side{};
    int32_t pawns = 0;
    std::optional<commentary::Trade> trade{};
    std::optional<commentary::Placed> taken{};
    bool operator==(const WinsMaterial&) const = default;
};

// `side` gives up `piece` on purpose, the line going on to justify it.
struct Sacrifice {
    commentary::Color side{};
    commentary::Placed piece{};
    bool operator==(const Sacrifice&) const = default;
};

// `side` mates in `moves` moves.
struct Mate {
    commentary::Color side{};
    uint32_t moves = 0;
    std::optional<commentary::MatePattern> pattern{};
    bool operator==(const Mate&) const = default;
};

// If it were their turn again, `side` would mate with `mv`.
struct ThreatensMate {
    commentary::Color side{};
    commentary::Move mv{};
    bool operator==(const ThreatensMate&) const = default;
};

// If it were their turn again, `side` would win `target` with `mv`.
struct ThreatensCapture {
    commentary::Color side{};
    commentary::Move mv{};
    commentary::Placed target{};
    bool operator==(const ThreatensCapture&) const = default;
};

struct Promotes {
    commentary::Color side{};
    commentary::Role role{};
    commentary::Square square{};
    bool operator==(const Promotes&) const = default;
};

struct Castles {
    commentary::Color side{};
    commentary::CastlingSide wing{};
    bool operator==(const Castles&) const = default;
};

struct PassedPawn {
    commentary::Placed pawn{};
    bool operator==(const PassedPawn&) const = default;
};

struct IsolatedPawn {
    commentary::Placed pawn{};
    bool operator==(const IsolatedPawn&) const = default;
};

struct DoubledPawns {
    commentary::Color color{};
    commentary::File file{};
    bool operator==(const DoubledPawns&) const = default;
};

struct BishopPair {
    commentary::Color color{};
    bool operator==(const BishopPair&) const = default;
};

// `rook` stands on a file without pawns.
struct RookOnOpenFile {
    commentary::Placed rook{};
    bool operator==(const RookOnOpenFile&) const = default;
};

// `rook` stands on its seventh rank, where the enemy king or pawns are.
struct RookOnSeventh {
    commentary::Placed rook{};
    bool operator==(const RookOnSeventh&) const = default;
};

// `knight` holds a square in the enemy half, guarded by a pawn of its own, that no enemy
// pawn can ever attack.
struct Outpost {
    commentary::Placed knight{};
    bool operator==(const Outpost&) const = default;
};

// `king`, queens still on the board, has lost the pawns that sheltered it.
struct ExposedKing {
    commentary::Placed king{};
    bool operator==(const ExposedKing&) const = default;
};

// `side` can no longer castle, without having castled.
struct LosesCastling {
    commentary::Color side{};
    bool operator==(const LosesCastling&) const = default;
};

// `side` checks again and again until a position comes back: a draw it forces.
struct PerpetualCheck {
    commentary::Color side{};
    bool operator==(const PerpetualCheck&) const = default;
};

// `side` has no legal move and is not in check: a draw that saves it.
struct Stalemate {
    commentary::Color side{};
    bool operator==(const Stalemate&) const = default;
};

// The line goes into `ending`, `side` steering it there.
struct Endgame {
    commentary::Color side{};
    commentary::Ending ending{};
    bool operator==(const Endgame&) const = default;
};

// `pawn` advances to strike at `target`, an enemy pawn; `lever` is what the break means
// in the structure, when the books have a name for it.
struct PawnBreak {
    commentary::Placed pawn{};
    commentary::Placed target{};
    std::optional<commentary::Lever> lever{};
    bool operator==(const PawnBreak&) const = default;
};

// `side` has developed two pieces more than the opponent, who keeps pieces at home.
struct DevelopmentLead {
    commentary::Color side{};
    bool operator==(const DevelopmentLead&) const = default;
};

// `color` has pawns side by side in the centre on its fourth rank, the opponent one
// central pawn at most.
struct PawnCentre {
    commentary::Color color{};
    bool operator==(const PawnCentre&) const = default;
};

// `side` is `pawns` behind in material, `deficit` when it is one plain thing, and the
// engine still gives it the balance at least.
struct Compensation {
    commentary::Color side{};
    int32_t pawns = 0;
    std::optional<commentary::Trade> deficit{};
    bool operator==(const Compensation&) const = default;
};

// `side` checks or makes a new threat move after move, standing at least as well.
struct Initiative {
    commentary::Color side{};
    bool operator==(const Initiative&) const = default;
};

// The same, from the side standing worse.
struct Counterplay {
    commentary::Color side{};
    bool operator==(const Counterplay&) const = default;
};

// `pawn` can no longer be guarded by a neighbour, an enemy pawn holds the square in front
// of it, and no enemy pawn stands before it on its file.
struct BackwardPawn {
    commentary::Placed pawn{};
    bool operator==(const BackwardPawn&) const = default;
};

// `color`'s pawns on `file` and the next file side by side on its fourth rank, with no
// pawn of its own beside them nor an enemy pawn in front.
struct HangingPawns {
    commentary::Color color{};
    commentary::File file{};
    bool operator==(const HangingPawns&) const = default;
};

// `bishop` shares its colour of squares with its own pawns, two of them blocked.
struct BadBishop {
    commentary::Placed bishop{};
    bool operator==(const BadBishop&) const = default;
};

// In an endgame, `king` has come up to its fourth rank or beyond.
struct ActiveKing {
    commentary::Placed king{};
    bool operator==(const ActiveKing&) const = default;
};

// `side`, queen on, aims three pieces or more at the squares around the enemy king, more
// than defend them.
struct KingAttack {
    commentary::Color side{};
    bool operator==(const KingAttack&) const = default;
};

// `side`'s pieces have half as many moves again as the opponent's, and ten more at least.
struct MoreActive {
    commentary::Color side{};
    bool operator==(const MoreActive&) const = default;
};

// `bishop` stands on a long diagonal and sees five squares of it or more.
struct LongDiagonal {
    commentary::Placed bishop{};
    bool operator==(const LongDiagonal&) const = default;
};

// The line clears the d- and e-files of their pawns but one, `side` opening the centre.
struct CentreOpens {
    commentary::Color side{};
    bool operator==(const CentreOpens&) const = default;
};

// The line blocks the d- and e-pawns head to head, `side` locking the centre.
struct CentreLocks {
    commentary::Color side{};
    bool operator==(const CentreLocks&) const = default;
};

// `square`, in the centre of `color`'s half, can no longer be guarded by its pawns, and an
// enemy piece already eyes it.
struct Hole {
    commentary::Color color{};
    commentary::Square square{};
    bool operator==(const Hole&) const = default;
};

// The tables' exact outcome for `side`.
struct Tablebase {
    commentary::Color side{};
    commentary::Verdict verdict{};
    bool operator==(const Tablebase&) const = default;
};

// `side`'s move turns a won position into `now`, a draw or a loss.
struct SpoilsWin {
    commentary::Color side{};
    commentary::Verdict now{};
    bool operator==(const SpoilsWin&) const = default;
};

// `side`'s move turns a drawn position into a loss.
struct SpoilsDraw {
    commentary::Color side{};
    bool operator==(const SpoilsDraw&) const = default;
};

// The only move keeping `verdict` (a win or a draw) for `side`.
struct OnlyMoveToKeep {
    commentary::Color side{};
    commentary::Verdict verdict{};
    bool operator==(const OnlyMoveToKeep&) const = default;
};

// `side`, to move, can only make its position worse: it would rather pass.
struct Zugzwang {
    commentary::Color side{};
    bool operator==(const Zugzwang&) const = default;
};

// The line draws `piece` away from `guarded`, which it defended, and takes it; or from
// the square it mates on, `guarded` then being the piece that mates.
struct Deflection {
    commentary::Placed piece{};
    commentary::Placed guarded{};
    bool operator==(const Deflection&) const = default;
};

// The line lures `piece` onto the square it is named on, where a tactic hits it.
struct Attraction {
    commentary::Placed piece{};
    bool operator==(const Attraction&) const = default;
};

// The line takes `defender` first, then `guarded`, which it defended.
struct RemovesDefender {
    commentary::Placed defender{};
    commentary::Placed guarded{};
    bool operator==(const RemovesDefender&) const = default;
};

// `piece` guards both `first` and `second`: the line takes `first`, `piece` takes back,
// and `second` falls; or `second` is the piece that then mates from a square `piece`
// watched.
struct Overloaded {
    commentary::Placed piece{};
    commentary::Placed first{};
    commentary::Placed second{};
    bool operator==(const Overloaded&) const = default;
};

// `piece` leaves the square it is named on so that `user`, a bishop, a rook or a queen of
// its side, can go there, or through it along `line`.
struct Clearance {
    commentary::Placed piece{};
    commentary::Placed user{};
    std::optional<commentary::LineKind> line{};
    bool operator==(const Clearance&) const = default;
};

// `pawn`, named where it lands, is offered to an enemy pawn, and a few moves on its side
// has given a pawn or more to get `passer` through, a passed pawn named where it stands
// when the line starts.
struct Breakthrough {
    commentary::Placed pawn{};
    commentary::Placed passer{};
    bool operator==(const Breakthrough&) const = default;
};

// Both sides run a passed pawn, and `side` queens first, with check when `check`; the
// other side queens too, later in the line, when `both`.
struct Race {
    commentary::Color side{};
    bool check                         = false;
    bool both                          = false;
    bool operator==(const Race&) const = default;
};

// Instead of taking back at once, `side` first plays a stronger move.
struct Intermezzo {
    commentary::Color side{};
    bool operator==(const Intermezzo&) const = default;
};

// The move played takes `piece`, named where it lands, away from the defence of `left`,
// which the answer takes.
struct Unguards {
    commentary::Placed piece{};
    commentary::Placed left{};
    bool operator==(const Unguards&) const = default;
};

// `piece` could already be won before the move, which does nothing about it.
struct Neglects {
    commentary::Placed piece{};
    bool operator==(const Neglects&) const = default;
};

// The move played opens a line for `slider`, which the answer uses.
struct OpensLine {
    commentary::Placed slider{};
    commentary::LineKind line{};
    bool operator==(const OpensLine&) const = default;
};

// The better move guards `guarded`, which the answer to the move played takes.
struct Guards {
    commentary::Placed guarded{};
    bool operator==(const Guards&) const = default;
};

// The better move takes `piece` out of the attack the answer to the move played carries
// out.
struct Saves {
    commentary::Placed piece{};
    bool operator==(const Saves&) const = default;
};

// The better move covers `square`, where the answer to the move played lands.
struct Covers {
    commentary::Color side{};
    commentary::Square square{};
    bool operator==(const Covers&) const = default;
};

// The better move takes on the same square as the move played, with `by` instead; `back`
// when both take back what was just taken.
struct TakesWith {
    commentary::Placed by{};
    bool back                               = false;
    bool operator==(const TakesWith&) const = default;
};

// The move brings `piece`, a knight or a bishop named where it lands, off its first
// square.
struct Develops {
    commentary::Placed piece{};
    bool operator==(const Develops&) const = default;
};

// In the opening, the move plays `piece` again, already developed, while two of its
// side's knights and bishops still stand at home and the better move brings one out.
struct MovesAgain {
    commentary::Placed piece{};
    bool operator==(const MovesAgain&) const = default;
};

// `piece`, a rook or a queen on a file without a pawn of its own, attacks `target`, an
// enemy pawn there that no pawn guards.
struct Pressure {
    commentary::Placed piece{};
    commentary::Placed target{};
    bool operator==(const Pressure&) const = default;
};

// `pawn` is passed and guarded by a pawn, on its fourth rank or beyond.
struct ProtectedPasser {
    commentary::Placed pawn{};
    bool operator==(const ProtectedPasser&) const = default;
};

// `color` has two passed pawns side by side, or one step apart, on neighbouring files.
struct ConnectedPassers {
    commentary::Color color{};
    bool operator==(const ConnectedPassers&) const = default;
};

// In an endgame, `pawn` is passed near the edge, every other pawn three files away or
// more: it draws the enemy king away.
struct OutsidePasser {
    commentary::Placed pawn{};
    bool operator==(const OutsidePasser&) const = default;
};

// `rook` stands behind `pawn`, a passed pawn of its own, on its file.
struct RookBehindPasser {
    commentary::Placed rook{};
    commentary::Placed pawn{};
    bool operator==(const RookBehindPasser&) const = default;
};

// `piece`, a knight or a bishop, stands right in front of `pawn`, an enemy pawn that is
// passed or isolated.
struct Blockade {
    commentary::Placed piece{};
    commentary::Placed pawn{};
    bool operator==(const Blockade&) const = default;
};

// The line trades the queens while `side` is two pawns or more ahead, and stays so.
struct Simplifies {
    commentary::Color side{};
    bool operator==(const Simplifies&) const = default;
};

// The kings on opposite wings, `side` advances its pawns on the enemy king's.
struct PawnStorm {
    commentary::Color side{};
    bool operator==(const PawnStorm&) const = default;
};

// `side`, with two pawns against three on the queen's wing, advances its b-pawn against
// them.
struct MinorityAttack {
    commentary::Color side{};
    bool operator==(const MinorityAttack&) const = default;
};

// A pawn advance leaves `side`'s pawns in the centre further up than the opponent's,
// with more room behind them for its pieces, while there are pieces enough to feel it.
struct Space {
    commentary::Color side{};
    bool operator==(const Space&) const = default;
};

// `pawn` advances into the enemy half, where it is safe and strikes at no pawn: it
// takes squares away from the opponent.
struct GainsSpace {
    commentary::Placed pawn{};
    bool operator==(const GainsSpace&) const = default;
};

// A move, or the exchange it starts, gives the pawns the shape of `kind`, a formation
// with a name; `side` is its owner.
struct Formation {
    commentary::Formation kind{};
    commentary::Color side{};
    bool operator==(const Formation&) const = default;
};

// In a pawn ending, `side`'s king faces the enemy king on a file or a rank, one square
// between them, or three or five when `distant`: the other king has to give way.
struct Opposition {
    commentary::Color side{};
    bool distant                             = false;
    bool operator==(const Opposition&) const = default;
};

// `pawn`, passed, its path clear, can no longer be caught by the enemy king, which has
// no piece to help: the king is outside the pawn's square.
struct Outruns {
    commentary::Placed pawn{};
    bool operator==(const Outruns&) const = default;
};

// `king` steps into the square of `pawn`, an enemy passed pawn it now catches.
struct EntersSquare {
    commentary::Placed king{};
    commentary::Placed pawn{};
    bool operator==(const EntersSquare&) const = default;
};

// In an endgame, `rook` goes to a file or a rank, `line`, that `king` cannot cross, a
// passed pawn of the rook's side standing beyond it.
struct CutsOff {
    commentary::Placed rook{};
    commentary::Placed king{};
    commentary::LineKind line{};
    bool operator==(const CutsOff&) const = default;
};

// A move, or the exchange it starts, leaves `side` with more pawns than the opponent on
// `wing`, each on a file of its own and none passed yet: a majority that can make a
// passed pawn.
struct Majority {
    commentary::Color side{};
    commentary::Wing wing{};
    bool operator==(const Majority&) const = default;
};

// With the queens on, `side` has no bishop on the dark squares (the light ones unless
// `dark`) while its pawns stand on the other colour: the squares of that colour are
// weak in its camp.
struct WeakColour {
    commentary::Color side{};
    bool dark                                = false;
    bool operator==(const WeakColour&) const = default;
};

// A pawn move gives `side`'s king, shut in on its first rank by its own men while the
// opponent has a rook or a queen, a square to step to.
struct Luft {
    commentary::Color side{};
    bool operator==(const Luft&) const = default;
};

// King and pawn against king: `king` steps onto a key square of `pawn`, which then
// promotes whoever is to move.
struct KeySquare {
    commentary::Placed king{};
    commentary::Placed pawn{};
    bool operator==(const KeySquare&) const = default;
};

// Rook and pawn against rook: `side`'s king reaches the promotion square of its pawn on
// the seventh rank, the enemy king cut off from it: the Lucena position.
struct Lucena {
    commentary::Color side{};
    bool operator==(const Lucena&) const = default;
};

// Rook against rook and pawn: `rook`, the defender's, goes to its third rank before the
// enemy king and pawn reach it, its own king in front of the pawn: Philidor's defence.
struct Philidor {
    commentary::Placed rook{};
    bool operator==(const Philidor&) const = default;
};

// `bishop` and pawns on a rook's file against a bare king that holds the promotion
// corner, a square the bishop cannot attack: a draw. `pawn` is the most advanced.
struct WrongBishop {
    commentary::Placed bishop{};
    commentary::Placed pawn{};
    bool operator==(const WrongBishop&) const = default;
};

// `side`'s move takes back on `square` what was just taken there: no gain, the balance
// restored.
struct Retakes {
    commentary::Color side{};
    commentary::Square square{};
    bool operator==(const Retakes&) const = default;
};

// `side`'s move does not take back on `square` what was just taken there, as the better
// move does.
struct NotRetaken {
    commentary::Color side{};
    commentary::Square square{};
    bool operator==(const NotRetaken&) const = default;
};

}  // namespace fact

using Fact = std::variant<
    fact::Fork, fact::Pin, fact::Skewer, fact::DiscoveredAttack, fact::DoubleCheck, fact::Hanging, fact::Trapped,
    fact::WinsMaterial, fact::Sacrifice, fact::Mate, fact::ThreatensMate, fact::ThreatensCapture, fact::Promotes,
    fact::Castles, fact::PassedPawn, fact::IsolatedPawn, fact::DoubledPawns, fact::BishopPair, fact::RookOnOpenFile,
    fact::RookOnSeventh, fact::Outpost, fact::ExposedKing, fact::LosesCastling, fact::PerpetualCheck, fact::Stalemate,
    fact::Endgame, fact::PawnBreak, fact::DevelopmentLead, fact::PawnCentre, fact::Compensation, fact::Initiative,
    fact::Counterplay, fact::BackwardPawn, fact::HangingPawns, fact::BadBishop, fact::ActiveKing, fact::KingAttack,
    fact::MoreActive, fact::LongDiagonal, fact::CentreOpens, fact::CentreLocks, fact::Hole, fact::Tablebase,
    fact::SpoilsWin, fact::SpoilsDraw, fact::OnlyMoveToKeep, fact::Zugzwang, fact::Deflection, fact::Attraction,
    fact::RemovesDefender, fact::Overloaded, fact::Clearance, fact::Breakthrough, fact::Race, fact::Intermezzo,
    fact::Unguards, fact::Neglects, fact::OpensLine, fact::Guards, fact::Saves, fact::Covers, fact::TakesWith,
    fact::Develops, fact::MovesAgain, fact::Pressure, fact::ProtectedPasser, fact::ConnectedPassers,
    fact::OutsidePasser, fact::RookBehindPasser, fact::Blockade, fact::Simplifies, fact::PawnStorm,
    fact::MinorityAttack, fact::Space, fact::GainsSpace, fact::Formation, fact::Opposition, fact::Outruns,
    fact::EntersSquare, fact::CutsOff, fact::Majority, fact::WeakColour, fact::Luft, fact::KeySquare, fact::Lucena,
    fact::Philidor, fact::WrongBishop, fact::Retakes, fact::NotRetaken>;

// The fact's kind, its variant's name in the prototype: "Fork", "WinsMaterial".
[[nodiscard]] const char* kindName(const Fact& fact) noexcept;
// Rust's debug text of the fact: "Develops { piece: Placed { color: White, role: Bishop, square: D3 } }".
[[nodiscard]] std::string debug(const Fact& fact);

}  // namespace fcg::commentary
