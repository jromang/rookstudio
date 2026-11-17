// Written by tools/port/gen_facts.py from the Rust prototype's enum Fact (crates/facts/src/lib.rs): do not edit.

#include "fact_types.hpp"

namespace fcg::commentary {

namespace {

template <class... F>
struct Overloaded : F... {
    using F::operator()...;
};
template <class... F>
Overloaded(F...) -> Overloaded<F...>;

}  // namespace

const char* kindName(const Fact& fact) noexcept {
    return std::visit(Overloaded{[](const fact::Fork&) { return "Fork"; },
                                 [](const fact::Pin&) { return "Pin"; },
                                 [](const fact::Skewer&) { return "Skewer"; },
                                 [](const fact::DiscoveredAttack&) { return "DiscoveredAttack"; },
                                 [](const fact::DoubleCheck&) { return "DoubleCheck"; },
                                 [](const fact::Hanging&) { return "Hanging"; },
                                 [](const fact::Trapped&) { return "Trapped"; },
                                 [](const fact::WinsMaterial&) { return "WinsMaterial"; },
                                 [](const fact::Sacrifice&) { return "Sacrifice"; },
                                 [](const fact::Mate&) { return "Mate"; },
                                 [](const fact::ThreatensMate&) { return "ThreatensMate"; },
                                 [](const fact::ThreatensCapture&) { return "ThreatensCapture"; },
                                 [](const fact::Promotes&) { return "Promotes"; },
                                 [](const fact::Castles&) { return "Castles"; },
                                 [](const fact::PassedPawn&) { return "PassedPawn"; },
                                 [](const fact::IsolatedPawn&) { return "IsolatedPawn"; },
                                 [](const fact::DoubledPawns&) { return "DoubledPawns"; },
                                 [](const fact::BishopPair&) { return "BishopPair"; },
                                 [](const fact::RookOnOpenFile&) { return "RookOnOpenFile"; },
                                 [](const fact::RookOnSeventh&) { return "RookOnSeventh"; },
                                 [](const fact::Outpost&) { return "Outpost"; },
                                 [](const fact::ExposedKing&) { return "ExposedKing"; },
                                 [](const fact::LosesCastling&) { return "LosesCastling"; },
                                 [](const fact::PerpetualCheck&) { return "PerpetualCheck"; },
                                 [](const fact::Stalemate&) { return "Stalemate"; },
                                 [](const fact::Endgame&) { return "Endgame"; },
                                 [](const fact::PawnBreak&) { return "PawnBreak"; },
                                 [](const fact::DevelopmentLead&) { return "DevelopmentLead"; },
                                 [](const fact::PawnCentre&) { return "PawnCentre"; },
                                 [](const fact::Compensation&) { return "Compensation"; },
                                 [](const fact::Initiative&) { return "Initiative"; },
                                 [](const fact::Counterplay&) { return "Counterplay"; },
                                 [](const fact::BackwardPawn&) { return "BackwardPawn"; },
                                 [](const fact::HangingPawns&) { return "HangingPawns"; },
                                 [](const fact::BadBishop&) { return "BadBishop"; },
                                 [](const fact::ActiveKing&) { return "ActiveKing"; },
                                 [](const fact::KingAttack&) { return "KingAttack"; },
                                 [](const fact::MoreActive&) { return "MoreActive"; },
                                 [](const fact::LongDiagonal&) { return "LongDiagonal"; },
                                 [](const fact::CentreOpens&) { return "CentreOpens"; },
                                 [](const fact::CentreLocks&) { return "CentreLocks"; },
                                 [](const fact::Hole&) { return "Hole"; },
                                 [](const fact::Tablebase&) { return "Tablebase"; },
                                 [](const fact::SpoilsWin&) { return "SpoilsWin"; },
                                 [](const fact::SpoilsDraw&) { return "SpoilsDraw"; },
                                 [](const fact::OnlyMoveToKeep&) { return "OnlyMoveToKeep"; },
                                 [](const fact::Zugzwang&) { return "Zugzwang"; },
                                 [](const fact::Deflection&) { return "Deflection"; },
                                 [](const fact::Attraction&) { return "Attraction"; },
                                 [](const fact::RemovesDefender&) { return "RemovesDefender"; },
                                 [](const fact::Overloaded&) { return "Overloaded"; },
                                 [](const fact::Clearance&) { return "Clearance"; },
                                 [](const fact::Breakthrough&) { return "Breakthrough"; },
                                 [](const fact::Race&) { return "Race"; },
                                 [](const fact::Intermezzo&) { return "Intermezzo"; },
                                 [](const fact::Unguards&) { return "Unguards"; },
                                 [](const fact::Neglects&) { return "Neglects"; },
                                 [](const fact::OpensLine&) { return "OpensLine"; },
                                 [](const fact::Guards&) { return "Guards"; },
                                 [](const fact::Saves&) { return "Saves"; },
                                 [](const fact::Covers&) { return "Covers"; },
                                 [](const fact::TakesWith&) { return "TakesWith"; },
                                 [](const fact::Develops&) { return "Develops"; },
                                 [](const fact::MovesAgain&) { return "MovesAgain"; },
                                 [](const fact::Pressure&) { return "Pressure"; },
                                 [](const fact::ProtectedPasser&) { return "ProtectedPasser"; },
                                 [](const fact::ConnectedPassers&) { return "ConnectedPassers"; },
                                 [](const fact::OutsidePasser&) { return "OutsidePasser"; },
                                 [](const fact::RookBehindPasser&) { return "RookBehindPasser"; },
                                 [](const fact::Blockade&) { return "Blockade"; },
                                 [](const fact::Simplifies&) { return "Simplifies"; },
                                 [](const fact::PawnStorm&) { return "PawnStorm"; },
                                 [](const fact::MinorityAttack&) { return "MinorityAttack"; },
                                 [](const fact::Space&) { return "Space"; },
                                 [](const fact::GainsSpace&) { return "GainsSpace"; },
                                 [](const fact::Formation&) { return "Formation"; },
                                 [](const fact::Opposition&) { return "Opposition"; },
                                 [](const fact::Outruns&) { return "Outruns"; },
                                 [](const fact::EntersSquare&) { return "EntersSquare"; },
                                 [](const fact::CutsOff&) { return "CutsOff"; },
                                 [](const fact::Majority&) { return "Majority"; },
                                 [](const fact::WeakColour&) { return "WeakColour"; },
                                 [](const fact::Luft&) { return "Luft"; },
                                 [](const fact::KeySquare&) { return "KeySquare"; },
                                 [](const fact::Lucena&) { return "Lucena"; },
                                 [](const fact::Philidor&) { return "Philidor"; },
                                 [](const fact::WrongBishop&) { return "WrongBishop"; },
                                 [](const fact::Retakes&) { return "Retakes"; },
                                 [](const fact::NotRetaken&) { return "NotRetaken"; }},
                      fact);
}

std::string debug(const Fact& fact) {
    return std::visit(
        Overloaded{
            [](const fact::Fork& f) {
                return std::string("Fork { ") + "attacker: " + debugValue(f.attacker) + ", " +
                       "targets: " + debugValue(f.targets) + " }";
            },
            [](const fact::Pin& f) {
                return std::string("Pin { ") + "pinner: " + debugValue(f.pinner) + ", " +
                       "pinned: " + debugValue(f.pinned) + ", " + "behind: " + debugValue(f.behind) + " }";
            },
            [](const fact::Skewer& f) {
                return std::string("Skewer { ") + "attacker: " + debugValue(f.attacker) + ", " +
                       "front: " + debugValue(f.front) + ", " + "behind: " + debugValue(f.behind) + " }";
            },
            [](const fact::DiscoveredAttack& f) {
                return std::string("DiscoveredAttack { ") + "attacker: " + debugValue(f.attacker) + ", " +
                       "target: " + debugValue(f.target) + " }";
            },
            [](const fact::DoubleCheck& f) { return std::string("DoubleCheck { ") + "by: " + debugValue(f.by) + " }"; },
            [](const fact::Hanging& f) { return std::string("Hanging { ") + "piece: " + debugValue(f.piece) + " }"; },
            [](const fact::Trapped& f) { return std::string("Trapped { ") + "piece: " + debugValue(f.piece) + " }"; },
            [](const fact::WinsMaterial& f) {
                return std::string("WinsMaterial { ") + "side: " + debugValue(f.side) + ", " +
                       "pawns: " + debugValue(f.pawns) + ", " + "trade: " + debugValue(f.trade) + ", " +
                       "taken: " + debugValue(f.taken) + " }";
            },
            [](const fact::Sacrifice& f) {
                return std::string("Sacrifice { ") + "side: " + debugValue(f.side) + ", " +
                       "piece: " + debugValue(f.piece) + " }";
            },
            [](const fact::Mate& f) {
                return std::string("Mate { ") + "side: " + debugValue(f.side) + ", " + "moves: " + debugValue(f.moves) +
                       ", " + "pattern: " + debugValue(f.pattern) + " }";
            },
            [](const fact::ThreatensMate& f) {
                return std::string("ThreatensMate { ") + "side: " + debugValue(f.side) + ", " +
                       "mv: " + debugValue(f.mv) + " }";
            },
            [](const fact::ThreatensCapture& f) {
                return std::string("ThreatensCapture { ") + "side: " + debugValue(f.side) + ", " +
                       "mv: " + debugValue(f.mv) + ", " + "target: " + debugValue(f.target) + " }";
            },
            [](const fact::Promotes& f) {
                return std::string("Promotes { ") + "side: " + debugValue(f.side) + ", " +
                       "role: " + debugValue(f.role) + ", " + "square: " + debugValue(f.square) + " }";
            },
            [](const fact::Castles& f) {
                return std::string("Castles { ") + "side: " + debugValue(f.side) + ", " +
                       "wing: " + debugValue(f.wing) + " }";
            },
            [](const fact::PassedPawn& f) {
                return std::string("PassedPawn { ") + "pawn: " + debugValue(f.pawn) + " }";
            },
            [](const fact::IsolatedPawn& f) {
                return std::string("IsolatedPawn { ") + "pawn: " + debugValue(f.pawn) + " }";
            },
            [](const fact::DoubledPawns& f) {
                return std::string("DoubledPawns { ") + "color: " + debugValue(f.color) + ", " +
                       "file: " + debugValue(f.file) + " }";
            },
            [](const fact::BishopPair& f) {
                return std::string("BishopPair { ") + "color: " + debugValue(f.color) + " }";
            },
            [](const fact::RookOnOpenFile& f) {
                return std::string("RookOnOpenFile { ") + "rook: " + debugValue(f.rook) + " }";
            },
            [](const fact::RookOnSeventh& f) {
                return std::string("RookOnSeventh { ") + "rook: " + debugValue(f.rook) + " }";
            },
            [](const fact::Outpost& f) { return std::string("Outpost { ") + "knight: " + debugValue(f.knight) + " }"; },
            [](const fact::ExposedKing& f) {
                return std::string("ExposedKing { ") + "king: " + debugValue(f.king) + " }";
            },
            [](const fact::LosesCastling& f) {
                return std::string("LosesCastling { ") + "side: " + debugValue(f.side) + " }";
            },
            [](const fact::PerpetualCheck& f) {
                return std::string("PerpetualCheck { ") + "side: " + debugValue(f.side) + " }";
            },
            [](const fact::Stalemate& f) { return std::string("Stalemate { ") + "side: " + debugValue(f.side) + " }"; },
            [](const fact::Endgame& f) {
                return std::string("Endgame { ") + "side: " + debugValue(f.side) + ", " +
                       "ending: " + debugValue(f.ending) + " }";
            },
            [](const fact::PawnBreak& f) {
                return std::string("PawnBreak { ") + "pawn: " + debugValue(f.pawn) + ", " +
                       "target: " + debugValue(f.target) + ", " + "lever: " + debugValue(f.lever) + " }";
            },
            [](const fact::DevelopmentLead& f) {
                return std::string("DevelopmentLead { ") + "side: " + debugValue(f.side) + " }";
            },
            [](const fact::PawnCentre& f) {
                return std::string("PawnCentre { ") + "color: " + debugValue(f.color) + " }";
            },
            [](const fact::Compensation& f) {
                return std::string("Compensation { ") + "side: " + debugValue(f.side) + ", " +
                       "pawns: " + debugValue(f.pawns) + ", " + "deficit: " + debugValue(f.deficit) + " }";
            },
            [](const fact::Initiative& f) {
                return std::string("Initiative { ") + "side: " + debugValue(f.side) + " }";
            },
            [](const fact::Counterplay& f) {
                return std::string("Counterplay { ") + "side: " + debugValue(f.side) + " }";
            },
            [](const fact::BackwardPawn& f) {
                return std::string("BackwardPawn { ") + "pawn: " + debugValue(f.pawn) + " }";
            },
            [](const fact::HangingPawns& f) {
                return std::string("HangingPawns { ") + "color: " + debugValue(f.color) + ", " +
                       "file: " + debugValue(f.file) + " }";
            },
            [](const fact::BadBishop& f) {
                return std::string("BadBishop { ") + "bishop: " + debugValue(f.bishop) + " }";
            },
            [](const fact::ActiveKing& f) {
                return std::string("ActiveKing { ") + "king: " + debugValue(f.king) + " }";
            },
            [](const fact::KingAttack& f) {
                return std::string("KingAttack { ") + "side: " + debugValue(f.side) + " }";
            },
            [](const fact::MoreActive& f) {
                return std::string("MoreActive { ") + "side: " + debugValue(f.side) + " }";
            },
            [](const fact::LongDiagonal& f) {
                return std::string("LongDiagonal { ") + "bishop: " + debugValue(f.bishop) + " }";
            },
            [](const fact::CentreOpens& f) {
                return std::string("CentreOpens { ") + "side: " + debugValue(f.side) + " }";
            },
            [](const fact::CentreLocks& f) {
                return std::string("CentreLocks { ") + "side: " + debugValue(f.side) + " }";
            },
            [](const fact::Hole& f) {
                return std::string("Hole { ") + "color: " + debugValue(f.color) + ", " +
                       "square: " + debugValue(f.square) + " }";
            },
            [](const fact::Tablebase& f) {
                return std::string("Tablebase { ") + "side: " + debugValue(f.side) + ", " +
                       "verdict: " + debugValue(f.verdict) + " }";
            },
            [](const fact::SpoilsWin& f) {
                return std::string("SpoilsWin { ") + "side: " + debugValue(f.side) + ", " +
                       "now: " + debugValue(f.now) + " }";
            },
            [](const fact::SpoilsDraw& f) {
                return std::string("SpoilsDraw { ") + "side: " + debugValue(f.side) + " }";
            },
            [](const fact::OnlyMoveToKeep& f) {
                return std::string("OnlyMoveToKeep { ") + "side: " + debugValue(f.side) + ", " +
                       "verdict: " + debugValue(f.verdict) + " }";
            },
            [](const fact::Zugzwang& f) { return std::string("Zugzwang { ") + "side: " + debugValue(f.side) + " }"; },
            [](const fact::Deflection& f) {
                return std::string("Deflection { ") + "piece: " + debugValue(f.piece) + ", " +
                       "guarded: " + debugValue(f.guarded) + " }";
            },
            [](const fact::Attraction& f) {
                return std::string("Attraction { ") + "piece: " + debugValue(f.piece) + " }";
            },
            [](const fact::RemovesDefender& f) {
                return std::string("RemovesDefender { ") + "defender: " + debugValue(f.defender) + ", " +
                       "guarded: " + debugValue(f.guarded) + " }";
            },
            [](const fact::Overloaded& f) {
                return std::string("Overloaded { ") + "piece: " + debugValue(f.piece) + ", " +
                       "first: " + debugValue(f.first) + ", " + "second: " + debugValue(f.second) + " }";
            },
            [](const fact::Clearance& f) {
                return std::string("Clearance { ") + "piece: " + debugValue(f.piece) + ", " +
                       "user: " + debugValue(f.user) + ", " + "line: " + debugValue(f.line) + " }";
            },
            [](const fact::Breakthrough& f) {
                return std::string("Breakthrough { ") + "pawn: " + debugValue(f.pawn) + ", " +
                       "passer: " + debugValue(f.passer) + " }";
            },
            [](const fact::Race& f) {
                return std::string("Race { ") + "side: " + debugValue(f.side) + ", " + "check: " + debugValue(f.check) +
                       ", " + "both: " + debugValue(f.both) + " }";
            },
            [](const fact::Intermezzo& f) {
                return std::string("Intermezzo { ") + "side: " + debugValue(f.side) + " }";
            },
            [](const fact::Unguards& f) {
                return std::string("Unguards { ") + "piece: " + debugValue(f.piece) + ", " +
                       "left: " + debugValue(f.left) + " }";
            },
            [](const fact::Neglects& f) { return std::string("Neglects { ") + "piece: " + debugValue(f.piece) + " }"; },
            [](const fact::OpensLine& f) {
                return std::string("OpensLine { ") + "slider: " + debugValue(f.slider) + ", " +
                       "line: " + debugValue(f.line) + " }";
            },
            [](const fact::Guards& f) { return std::string("Guards { ") + "guarded: " + debugValue(f.guarded) + " }"; },
            [](const fact::Saves& f) { return std::string("Saves { ") + "piece: " + debugValue(f.piece) + " }"; },
            [](const fact::Covers& f) {
                return std::string("Covers { ") + "side: " + debugValue(f.side) + ", " +
                       "square: " + debugValue(f.square) + " }";
            },
            [](const fact::TakesWith& f) {
                return std::string("TakesWith { ") + "by: " + debugValue(f.by) + ", " + "back: " + debugValue(f.back) +
                       " }";
            },
            [](const fact::Develops& f) { return std::string("Develops { ") + "piece: " + debugValue(f.piece) + " }"; },
            [](const fact::MovesAgain& f) {
                return std::string("MovesAgain { ") + "piece: " + debugValue(f.piece) + " }";
            },
            [](const fact::Pressure& f) {
                return std::string("Pressure { ") + "piece: " + debugValue(f.piece) + ", " +
                       "target: " + debugValue(f.target) + " }";
            },
            [](const fact::ProtectedPasser& f) {
                return std::string("ProtectedPasser { ") + "pawn: " + debugValue(f.pawn) + " }";
            },
            [](const fact::ConnectedPassers& f) {
                return std::string("ConnectedPassers { ") + "color: " + debugValue(f.color) + " }";
            },
            [](const fact::OutsidePasser& f) {
                return std::string("OutsidePasser { ") + "pawn: " + debugValue(f.pawn) + " }";
            },
            [](const fact::RookBehindPasser& f) {
                return std::string("RookBehindPasser { ") + "rook: " + debugValue(f.rook) + ", " +
                       "pawn: " + debugValue(f.pawn) + " }";
            },
            [](const fact::Blockade& f) {
                return std::string("Blockade { ") + "piece: " + debugValue(f.piece) + ", " +
                       "pawn: " + debugValue(f.pawn) + " }";
            },
            [](const fact::Simplifies& f) {
                return std::string("Simplifies { ") + "side: " + debugValue(f.side) + " }";
            },
            [](const fact::PawnStorm& f) { return std::string("PawnStorm { ") + "side: " + debugValue(f.side) + " }"; },
            [](const fact::MinorityAttack& f) {
                return std::string("MinorityAttack { ") + "side: " + debugValue(f.side) + " }";
            },
            [](const fact::Space& f) { return std::string("Space { ") + "side: " + debugValue(f.side) + " }"; },
            [](const fact::GainsSpace& f) {
                return std::string("GainsSpace { ") + "pawn: " + debugValue(f.pawn) + " }";
            },
            [](const fact::Formation& f) {
                return std::string("Formation { ") + "kind: " + debugValue(f.kind) + ", " +
                       "side: " + debugValue(f.side) + " }";
            },
            [](const fact::Opposition& f) {
                return std::string("Opposition { ") + "side: " + debugValue(f.side) + ", " +
                       "distant: " + debugValue(f.distant) + " }";
            },
            [](const fact::Outruns& f) { return std::string("Outruns { ") + "pawn: " + debugValue(f.pawn) + " }"; },
            [](const fact::EntersSquare& f) {
                return std::string("EntersSquare { ") + "king: " + debugValue(f.king) + ", " +
                       "pawn: " + debugValue(f.pawn) + " }";
            },
            [](const fact::CutsOff& f) {
                return std::string("CutsOff { ") + "rook: " + debugValue(f.rook) + ", " +
                       "king: " + debugValue(f.king) + ", " + "line: " + debugValue(f.line) + " }";
            },
            [](const fact::Majority& f) {
                return std::string("Majority { ") + "side: " + debugValue(f.side) + ", " +
                       "wing: " + debugValue(f.wing) + " }";
            },
            [](const fact::WeakColour& f) {
                return std::string("WeakColour { ") + "side: " + debugValue(f.side) + ", " +
                       "dark: " + debugValue(f.dark) + " }";
            },
            [](const fact::Luft& f) { return std::string("Luft { ") + "side: " + debugValue(f.side) + " }"; },
            [](const fact::KeySquare& f) {
                return std::string("KeySquare { ") + "king: " + debugValue(f.king) + ", " +
                       "pawn: " + debugValue(f.pawn) + " }";
            },
            [](const fact::Lucena& f) { return std::string("Lucena { ") + "side: " + debugValue(f.side) + " }"; },
            [](const fact::Philidor& f) { return std::string("Philidor { ") + "rook: " + debugValue(f.rook) + " }"; },
            [](const fact::WrongBishop& f) {
                return std::string("WrongBishop { ") + "bishop: " + debugValue(f.bishop) + ", " +
                       "pawn: " + debugValue(f.pawn) + " }";
            },
            [](const fact::Retakes& f) {
                return std::string("Retakes { ") + "side: " + debugValue(f.side) + ", " +
                       "square: " + debugValue(f.square) + " }";
            },
            [](const fact::NotRetaken& f) {
                return std::string("NotRetaken { ") + "side: " + debugValue(f.side) + ", " +
                       "square: " + debugValue(f.square) + " }";
            }},
        fact);
}

}  // namespace fcg::commentary
