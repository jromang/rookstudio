// The English phrasebook (the prototype's crates/commentary/src/phrases.rs, English).

#include <algorithm>

#include <fmt/include/fmt/format.h>

#include "phrases.hpp"

namespace fcg::commentary {

using phrases_detail::Kind;

namespace {

template <typename T>
const T* as(const Fact& f) {
    return std::get_if<T>(&f);
}

const char* noun(Role role) {
    switch (role) {
        case Role::King:
            return "king";
        case Role::Queen:
            return "queen";
        case Role::Rook:
            return "rook";
        case Role::Bishop:
            return "bishop";
        case Role::Knight:
            return "knight";
        default:
            return "pawn";
    }
}

std::string piece(const Placed& p) {
    if (p.role == Role::King) return "the king";
    return fmt::format("the {} on {}", noun(p.role), name(p.square));
}

// The second thing an overloaded piece guarded: a piece of its side, or the square an enemy piece then mates from.
std::string second(const Placed& guard, const Placed& other) {
    if (guard.color == other.color) return piece(other);
    return fmt::format("the {} square", name(other.square));
}

std::string pieces(const std::vector<Placed>& list) {
    if (list.empty()) return {};
    if (list.size() == 1) return piece(list.front());
    std::string out;
    for (std::size_t i = 0; i + 1 < list.size(); ++i) out += (i ? ", " : "") + piece(list[i]);
    return out + " and " + piece(list.back());
}

// "the knight": the piece without its square.
std::string nameOf(Role role) { return fmt::format("the {}", noun(role)); }

const char* kind(Kind k, uint8_t n) {
    switch (k) {
        case Kind::Exchange:
            return n == 1 ? "the exchange" : nullptr;
        case Kind::Queen:
            return n == 1 ? "the queen" : nullptr;
        case Kind::Rook:
            return n == 1 ? "a rook" : n == 2 ? "two rooks" : nullptr;
        case Kind::Piece:
            return n == 1 ? "a piece" : n == 2 ? "two pieces" : n == 3 ? "three pieces" : nullptr;
        default:
            return n == 1 ? "a pawn" : n == 2 ? "two pawns" : n == 3 ? "three pawns" : n == 4 ? "four pawns" : nullptr;
    }
}

std::string material(int32_t pawns, std::optional<Trade> trade) {
    if (trade) {
        if (auto words = phrases_detail::tradeWords(*trade, kind, " and ", " for ")) return *words;
    }
    switch (pawns) {
        case 1:
            return "a pawn";
        case 2:
            return "two pawns";
        case 3:
            return "a piece";
        case 4:
            return "a piece and a pawn";
        case 5:
            return "the equivalent of a rook";
        default:
            return "a lot of material";
    }
}

// What a line wins: the piece it comes down to when there is one, "the bishop on h6 for a pawn", else the count.
std::string won(int32_t pawns, std::optional<Trade> trade, std::optional<Placed> taken) {
    std::optional<std::pair<std::string, std::optional<std::string>>> parts;
    if (trade) parts = phrases_detail::tradeParts(*trade, kind, " and ");
    if (taken && parts && parts->second) return fmt::format("{} for {}", piece(*taken), *parts->second);
    if (taken) return piece(*taken);
    return material(pawns, trade);
}

std::string won(const fact::WinsMaterial& w) { return won(w.pawns, w.trade, w.taken); }

const char* lineName(LineKind line) {
    switch (line) {
        case LineKind::File:
            return "file";
        case LineKind::Rank:
            return "rank";
        default:
            return "diagonal";
    }
}

const char* endingWords(Ending ending) {
    switch (ending) {
        case Ending::Queenless:
            return "trades queens";
        case Ending::Pawn:
            return "goes into a pawn endgame";
        case Ending::Rook:
            return "goes into a rook endgame";
        case Ending::Queen:
            return "goes into a queen endgame";
        case Ending::Minor:
            return "goes into a minor-piece endgame";
        default:
            return "goes into an endgame with bishops of opposite colours";
    }
}

const char* mateKind(std::optional<MatePattern> pattern) {
    if (pattern == MatePattern::BackRank) return "back-rank ";
    if (pattern == MatePattern::Smothered) return "smothered ";
    return "";
}

std::string mate(uint32_t moves, std::optional<MatePattern> pattern) {
    if (moves == 1) return fmt::format("an immediate {}mate", mateKind(pattern));
    return fmt::format("a {}mate in {}", mateKind(pattern), moves);
}

// The tactic that wins something, as a phrase after what it wins: "with a fork".
std::optional<std::string> by(const Fact& fact) {
    if (as<fact::Fork>(fact)) return "with a fork";
    if (const auto* f = as<fact::Pin>(fact)) return fmt::format("thanks to the pin on {}", piece(f->pinned));
    if (as<fact::Skewer>(fact)) return "with a skewer";
    if (const auto* f = as<fact::DiscoveredAttack>(fact))
        return f->target.role == Role::King ? "with a discovered check" : "with a discovered attack";
    if (as<fact::DoubleCheck>(fact)) return "with a double check";
    if (const auto* f = as<fact::Deflection>(fact)) return fmt::format("by drawing {} away", piece(f->piece));
    if (const auto* f = as<fact::Overloaded>(fact)) return fmt::format("by overloading {}", piece(f->piece));
    if (const auto* f = as<fact::Clearance>(fact)) {
        if (f->user.role == Role::Pawn) return "by clearing the promotion square";
        if (f->line) return fmt::format("by clearing the {} for {}", lineName(*f->line), piece(f->user));
        return fmt::format("by clearing a square for {}", piece(f->user));
    }
    if (as<fact::Breakthrough>(fact)) return "with a breakthrough";
    if (const auto* f = as<fact::Attraction>(fact))
        return fmt::format("by luring {} to {}", nameOf(f->piece.role), name(f->piece.square));
    if (const auto* f = as<fact::RemovesDefender>(fact))
        return fmt::format("by removing {}, the defender", piece(f->defender));
    if (as<fact::Intermezzo>(fact)) return "with an in-between move";
    return std::nullopt;
}

// What a better move does, as a verb phrase: "wins a piece".
std::optional<std::string> would(const Fact& fact) {
    if (const auto* f = as<fact::Mate>(fact)) return fmt::format("mates in {}", f->moves);
    if (const auto* f = as<fact::WinsMaterial>(fact)) return fmt::format("wins {}", won(*f));
    if (const auto* f = as<fact::Fork>(fact)) return fmt::format("forks {}", pieces(f->targets));
    if (const auto* f = as<fact::Pin>(fact)) return fmt::format("pins {}", piece(f->pinned));
    if (const auto* f = as<fact::Skewer>(fact))
        return fmt::format("skewers {} and {}", piece(f->front), piece(f->behind));
    if (const auto* f = as<fact::DiscoveredAttack>(fact)) {
        if (f->target.role == Role::King) return "gives a discovered check";
        return fmt::format("uncovers an attack on {}", piece(f->target));
    }
    if (as<fact::DoubleCheck>(fact)) return "gives a double check";
    if (const auto* f = as<fact::Trapped>(fact)) return fmt::format("traps {}", piece(f->piece));
    if (const auto* f = as<fact::Hanging>(fact)) return fmt::format("takes {}, left en prise", piece(f->piece));
    if (as<fact::ThreatensMate>(fact)) return "threatens mate";
    if (const auto* f = as<fact::ThreatensCapture>(fact)) return fmt::format("attacks {}", piece(f->target));
    if (as<fact::Promotes>(fact)) return "promotes a pawn";
    if (as<fact::PerpetualCheck>(fact)) return "forces a perpetual check";
    if (as<fact::Stalemate>(fact)) return "saves the game by stalemate";
    if (as<fact::Zugzwang>(fact)) return "puts the opponent in zugzwang";
    if (const auto* f = as<fact::Deflection>(fact)) {
        if (f->piece.color == f->guarded.color)
            return fmt::format("draws {} away from the defence of {}", piece(f->piece), piece(f->guarded));
        return fmt::format("draws {} away from the {} square", piece(f->piece), name(f->guarded.square));
    }
    if (const auto* f = as<fact::Overloaded>(fact))
        return fmt::format("overloads {}, which guarded both {} and {}", piece(f->piece), piece(f->first),
                           second(f->piece, f->second));
    if (const auto* f = as<fact::Clearance>(fact)) {
        if (f->user.role == Role::Pawn) return fmt::format("clears the promotion square for {}", piece(f->user));
        if (f->line) return fmt::format("clears the {} for {}", lineName(*f->line), piece(f->user));
        return fmt::format("clears {} for {}", name(f->piece.square), piece(f->user));
    }
    if (const auto* f = as<fact::Breakthrough>(fact))
        return fmt::format("gets {} through with a breakthrough", piece(f->passer));
    if (const auto* f = as<fact::Race>(fact))
        return f->check ? "wins the race to promote, with check" : "wins the race to promote";
    if (const auto* f = as<fact::Attraction>(fact))
        return fmt::format("lures {} to {}", nameOf(f->piece.role), name(f->piece.square));
    if (const auto* f = as<fact::RemovesDefender>(fact)) {
        if (f->defender.color == f->guarded.color)
            return fmt::format("removes {}, which guarded {}", piece(f->defender), piece(f->guarded));
        return fmt::format("removes {}, the defender", piece(f->defender));
    }
    if (as<fact::Intermezzo>(fact)) return "slips in a move before taking back";
    if (const auto* f = as<fact::Guards>(fact)) return fmt::format("guards {}", piece(f->guarded));
    if (const auto* f = as<fact::Develops>(fact)) return fmt::format("develops {}", nameOf(f->piece.role));
    if (const auto* f = as<fact::Pressure>(fact)) return fmt::format("puts {} under pressure", piece(f->target));
    if (const auto* f = as<fact::ProtectedPasser>(fact))
        return fmt::format("creates a protected passed pawn on {}", name(f->pawn.square));
    if (as<fact::ConnectedPassers>(fact)) return "creates connected passed pawns";
    if (const auto* f = as<fact::OutsidePasser>(fact))
        return fmt::format("creates an outside passed pawn on {}", name(f->pawn.square));
    if (as<fact::RookBehindPasser>(fact)) return "puts the rook behind the passed pawn";
    if (const auto* f = as<fact::Blockade>(fact)) return fmt::format("blockades {}", piece(f->pawn));
    if (as<fact::Simplifies>(fact)) return "simplifies while ahead in material";
    if (as<fact::PawnStorm>(fact)) return "storms the enemy king with pawns";
    if (as<fact::MinorityAttack>(fact)) return "starts the minority attack";
    if (const auto* f = as<fact::Saves>(fact)) return fmt::format("moves {} out of danger", piece(f->piece));
    if (const auto* f = as<fact::Covers>(fact)) return fmt::format("covers the {} square", name(f->square));
    if (as<fact::Castles>(fact)) return "castles the king into safety";
    if (const auto* f = as<fact::PassedPawn>(fact))
        return fmt::format("creates a passed pawn on {}", name(f->pawn.square));
    if (as<fact::RookOnSeventh>(fact)) return "brings a rook to the seventh rank";
    if (as<fact::RookOnOpenFile>(fact)) return "takes the open file with the rook";
    if (const auto* f = as<fact::Outpost>(fact))
        return fmt::format("settles the knight on the {} outpost", name(f->knight.square));
    if (as<fact::BishopPair>(fact)) return "gets the bishop pair";
    if (const auto* f = as<fact::IsolatedPawn>(fact)) return fmt::format("isolates {}", piece(f->pawn));
    if (const auto* f = as<fact::DoubledPawns>(fact))
        return fmt::format("doubles the enemy pawns on the {}-file", letter(f->file));
    if (as<fact::ExposedKing>(fact)) return "strips the enemy king of its cover";
    if (as<fact::LosesCastling>(fact)) return "takes away the opponent's right to castle";
    if (const auto* f = as<fact::PawnBreak>(fact)) {
        if (f->lever && (f->lever->kind == Lever::Kind::ChainBase || f->lever->kind == Lever::Kind::ChainHead))
            return fmt::format("starts {}, on {}", leverWords(*f->lever, Lang::En), name(f->target.square));
        if (f->lever) return fmt::format("starts {}", leverWords(*f->lever, Lang::En));
        return fmt::format("starts the pawn break against {}", piece(f->target));
    }
    if (as<fact::Space>(fact)) return "takes more space";
    if (const auto* f = as<fact::GainsSpace>(fact))
        return fmt::format("gains space {}", wingWords(f->pawn.square, Lang::En));
    if (const auto* f = as<fact::Formation>(fact))
        return fmt::format("sets up {}", formationName(f->kind, f->side, Lang::En));
    if (const auto* f = as<fact::Opposition>(fact))
        return f->distant ? "takes the distant opposition" : "takes the opposition";
    if (as<fact::Outruns>(fact)) return "leaves the enemy king outside the square of the pawn";
    if (const auto* f = as<fact::EntersSquare>(fact)) return fmt::format("steps into the square of {}", piece(f->pawn));
    if (const auto* f = as<fact::CutsOff>(fact))
        return fmt::format("cuts the enemy king off along {}", barrierWords(f->line, f->rook.square, Lang::En));
    if (const auto* f = as<fact::Majority>(fact))
        return fmt::format("gets the pawn majority {}", ofWing(f->wing, Lang::En));
    if (const auto* f = as<fact::WeakColour>(fact)) {
        std::string_view squares = colourSquares(f->dark, Lang::En);
        if (squares.starts_with("the ")) squares.remove_prefix(4);
        return fmt::format("weakens the opponent's {}", squares);
    }
    if (as<fact::Luft>(fact)) return "gives the king luft";
    if (const auto* f = as<fact::KeySquare>(fact))
        return fmt::format("brings the king to a key square of {}", piece(f->pawn));
    if (as<fact::Lucena>(fact)) return "reaches the Lucena position";
    if (const auto* f = as<fact::Philidor>(fact))
        return fmt::format("sets up Philidor's defence on {}", barrierWords(LineKind::Rank, f->rook.square, Lang::En));
    if (as<fact::WrongBishop>(fact)) return "reaches a drawn endgame: the bishop does not control the promotion square";
    if (as<fact::DevelopmentLead>(fact)) return "gives a lead in development";
    if (as<fact::PawnCentre>(fact)) return "builds a pawn centre";
    if (as<fact::Compensation>(fact)) return "gives compensation for the material";
    if (as<fact::Initiative>(fact)) return "keeps the initiative";
    if (as<fact::Counterplay>(fact)) return "gives counterplay";
    if (const auto* f = as<fact::BackwardPawn>(fact)) return fmt::format("leaves {} backward", piece(f->pawn));
    if (const auto* f = as<fact::BadBishop>(fact))
        return fmt::format("leaves the opponent a bad bishop on {}", name(f->bishop.square));
    if (as<fact::ActiveKing>(fact)) return "activates the king";
    if (as<fact::KingAttack>(fact)) return "starts an attack on the king";
    if (as<fact::MoreActive>(fact)) return "gives more active pieces";
    if (const auto* f = as<fact::LongDiagonal>(fact))
        return fmt::format("gives {} the long diagonal", piece(f->bishop));
    if (as<fact::CentreOpens>(fact)) return "opens the centre";
    if (as<fact::CentreLocks>(fact)) return "locks the centre";
    if (const auto* f = as<fact::Hole>(fact)) return fmt::format("weakens the {} square", name(f->square));
    if (const auto* f = as<fact::Endgame>(fact)) return std::string(endingWords(f->ending));
    return std::nullopt;
}

std::string trimEndDots(std::string_view s) {
    while (!s.empty() && s.back() == '.') s.remove_suffix(1);
    return std::string(s);
}

class English final : public Phrasebook {
   public:
    std::string novelty(std::optional<std::pair<std::string_view, Theory::Source>> usual) const override {
        if (usual && usual->second == Theory::Source::Surveys)
            return fmt::format("A novelty; the main line is {}.", usual->first);
        if (usual) return fmt::format("A novelty; the usual move is {}.", usual->first);
        return "A novelty.";
    }

    std::string noveltyTakenUp() const override { return "A novelty, since become the main line."; }

    std::optional<std::string> punishment(const Fact& fact) const override {
        if (const auto* f = as<fact::Mate>(fact)) return fmt::format("Allows {}.", mate(f->moves, f->pattern));
        if (const auto* f = as<fact::WinsMaterial>(fact)) return fmt::format("Loses {}.", won(*f));
        if (const auto* f = as<fact::Hanging>(fact)) return fmt::format("Leaves {} hanging.", piece(f->piece));
        if (const auto* f = as<fact::Deflection>(fact))
            return fmt::format("Allows {} to be drawn away.", piece(f->piece));
        if (const auto* f = as<fact::Overloaded>(fact))
            return fmt::format("Allows {} to be overloaded.", piece(f->piece));
        if (const auto* f = as<fact::Clearance>(fact)) {
            if (f->user.role == Role::Pawn)
                return fmt::format("Allows the promotion square to be cleared for {}.", piece(f->user));
            if (f->line) return fmt::format("Allows the {} to be cleared for {}.", lineName(*f->line), piece(f->user));
            return fmt::format("Allows a square to be cleared for {}.", piece(f->user));
        }
        if (const auto* f = as<fact::Breakthrough>(fact))
            return fmt::format("Allows a breakthrough: {} gets through.", piece(f->passer));
        if (as<fact::Race>(fact)) return "Loses the race to promote.";
        if (const auto* f = as<fact::Attraction>(fact))
            return fmt::format("Allows {} to be lured to {}.", nameOf(f->piece.role), name(f->piece.square));
        if (const auto* f = as<fact::RemovesDefender>(fact))
            return fmt::format("Allows {}, the defender, to be removed.", piece(f->defender));
        if (as<fact::Intermezzo>(fact)) return "Allows an in-between move.";
        if (const auto* f = as<fact::NotRetaken>(fact))
            return fmt::format("Does not take back on {}.", name(f->square));
        if (const auto* f = as<fact::Pressure>(fact))
            return fmt::format("Leaves {} under pressure from {}.", piece(f->target), piece(f->piece));
        if (const auto* f = as<fact::ProtectedPasser>(fact))
            return fmt::format("Concedes a protected passed pawn on {}.", name(f->pawn.square));
        if (as<fact::ConnectedPassers>(fact)) return "Concedes connected passed pawns.";
        if (const auto* f = as<fact::OutsidePasser>(fact))
            return fmt::format("Concedes an outside passed pawn on {}.", name(f->pawn.square));
        if (as<fact::RookBehindPasser>(fact)) return "Lets the enemy rook get behind its passed pawn.";
        if (const auto* f = as<fact::Blockade>(fact))
            return fmt::format("Lets {} blockade {}.", piece(f->piece), piece(f->pawn));
        if (as<fact::Simplifies>(fact)) return "Lets the opponent simplify while ahead in material.";
        if (as<fact::PawnStorm>(fact)) return "Lets the opponent storm the king with pawns.";
        if (as<fact::MinorityAttack>(fact)) return "Allows the minority attack.";
        if (const auto* f = as<fact::MovesAgain>(fact))
            return fmt::format("Moves {} again with development unfinished.", nameOf(f->piece.role));
        if (const auto* f = as<fact::Sacrifice>(fact))
            return fmt::format("Allows a {} sacrifice.", noun(f->piece.role));
        if (const auto* f = as<fact::Fork>(fact))
            return fmt::format("Allows a fork: {} attacks {}.", piece(f->attacker), pieces(f->targets));
        if (const auto* f = as<fact::Pin>(fact)) return fmt::format("Allows {} to be pinned.", piece(f->pinned));
        if (const auto* f = as<fact::Skewer>(fact))
            return fmt::format("Allows a skewer: {} must step aside and {} falls.", piece(f->front), piece(f->behind));
        if (const auto* f = as<fact::DiscoveredAttack>(fact))
            return fmt::format("Allows a discovered attack on {}.", piece(f->target));
        if (as<fact::DoubleCheck>(fact)) return "Allows a double check.";
        if (const auto* f = as<fact::Trapped>(fact))
            return fmt::format("{} gets trapped.", capitalize(piece(f->piece)));
        if (as<fact::ThreatensMate>(fact)) return "Leaves a mating threat.";
        if (const auto* f = as<fact::SpoilsWin>(fact)) {
            if (f->now == Verdict::Draw) return "Throws away the win: the tablebases say draw.";
            return "Turns a win into a loss, according to the tablebases.";
        }
        if (as<fact::SpoilsDraw>(fact)) return "Loses a drawn position, according to the tablebases.";
        if (const auto* f = as<fact::Promotes>(fact)) return fmt::format("Allows a promotion on {}.", name(f->square));
        if (as<fact::ExposedKing>(fact)) return "Leaves the king without pawn cover.";
        if (const auto* f = as<fact::PassedPawn>(fact))
            return fmt::format("Concedes a passed pawn on {}.", name(f->pawn.square));
        if (as<fact::RookOnSeventh>(fact)) return "Lets an enemy rook onto the seventh rank.";
        if (const auto* f = as<fact::Outpost>(fact))
            return fmt::format("Gives the enemy knight an outpost on {}.", name(f->knight.square));
        if (as<fact::RookOnOpenFile>(fact)) return "Hands the open file to the enemy rook.";
        if (as<fact::LosesCastling>(fact)) return "Loses the right to castle.";
        if (const auto* f = as<fact::IsolatedPawn>(fact)) return fmt::format("Leaves {} isolated.", piece(f->pawn));
        if (const auto* f = as<fact::DoubledPawns>(fact))
            return fmt::format("Leaves doubled pawns on the {}-file.", letter(f->file));
        if (as<fact::BishopPair>(fact)) return "Gives up the bishop pair.";
        if (as<fact::PerpetualCheck>(fact)) return "Allows a perpetual check.";
        if (as<fact::Stalemate>(fact)) return "Allows stalemate.";
        if (const auto* f = as<fact::PawnBreak>(fact)) {
            if (f->lever) return fmt::format("Allows {}.", leverWords(*f->lever, Lang::En));
            return fmt::format("Allows a pawn break against {}.", piece(f->target));
        }
        if (as<fact::Space>(fact)) return "Lets the opponent take more space.";
        if (const auto* f = as<fact::GainsSpace>(fact))
            return fmt::format("Lets the opponent gain space {}.", wingWords(f->pawn.square, Lang::En));
        if (const auto* f = as<fact::Formation>(fact))
            return fmt::format("Lets the opponent set up {}.", formationName(f->kind, f->side, Lang::En));
        if (as<fact::Opposition>(fact)) return "Gives up the opposition.";
        if (const auto* f = as<fact::Outruns>(fact))
            return fmt::format("Lets {} run: the king is outside its square.", piece(f->pawn));
        if (const auto* f = as<fact::EntersSquare>(fact))
            return fmt::format("Lets the enemy king step into the square of {}.", piece(f->pawn));
        if (as<fact::CutsOff>(fact)) return "Lets the enemy rook cut the king off.";
        if (const auto* f = as<fact::Majority>(fact))
            return fmt::format("Concedes the pawn majority {}.", ofWing(f->wing, Lang::En));
        if (const auto* f = as<fact::WeakColour>(fact))
            return capitalize(fmt::format("{} are weakened.", colourSquares(f->dark, Lang::En)));
        if (const auto* f = as<fact::KeySquare>(fact))
            return fmt::format("Lets the enemy king reach a key square of {}.", piece(f->pawn));
        if (as<fact::Lucena>(fact)) return "Lets the opponent reach the Lucena position.";
        if (as<fact::Philidor>(fact)) return "Lets the opponent set up Philidor's defence.";
        if (as<fact::WrongBishop>(fact))
            return "Leaves a drawn endgame: the bishop does not control the promotion square.";
        if (as<fact::DevelopmentLead>(fact)) return "Gives the opponent a lead in development.";
        if (as<fact::PawnCentre>(fact)) return "Lets the opponent build a pawn centre.";
        if (as<fact::Compensation>(fact)) return "Gives the opponent compensation.";
        if (as<fact::Initiative>(fact)) return "Hands the initiative to the opponent.";
        if (as<fact::Counterplay>(fact)) return "Allows counterplay.";
        if (const auto* f = as<fact::BackwardPawn>(fact)) return fmt::format("Leaves {} backward.", piece(f->pawn));
        if (as<fact::HangingPawns>(fact)) return "Leaves hanging pawns.";
        if (as<fact::BadBishop>(fact)) return "Leaves a bad bishop.";
        if (as<fact::ActiveKing>(fact)) return "Lets the enemy king become active.";
        if (as<fact::KingAttack>(fact)) return "Lets the opponent attack the king.";
        if (as<fact::MoreActive>(fact)) return "Leaves the opponent's pieces more active.";
        if (const auto* f = as<fact::LongDiagonal>(fact))
            return fmt::format("Lets {} sweep the long diagonal.", piece(f->bishop));
        if (as<fact::CentreOpens>(fact)) return "Opens the centre for the opponent.";
        if (const auto* f = as<fact::Hole>(fact)) return fmt::format("Weakens the {} square.", name(f->square));
        if (as<fact::Zugzwang>(fact)) return "Runs into zugzwang.";
        return std::nullopt;
    }

    std::optional<std::string> refuted(const Fact& fact, std::string_view mv, bool at_once) const override {
        // "Allows the fork 17…e4" when the answer does it, "After 17…e4, a fork follows" when it comes later in the
        // line.
        const auto lead = [&](std::string_view named, std::string_view later) {
            return at_once ? fmt::format("Allows {} {}", named, mv) : fmt::format("After {}, {} follows", mv, later);
        };
        if (const auto* f = as<fact::Mate>(fact)) {
            if (f->moves == 1) {
                if (f->pattern) return fmt::format("Allows a {}mate: {}.", mateKind(f->pattern), mv);
                return fmt::format("Allows mate: {}.", mv);
            }
            return fmt::format("Allows {} starting with {}.", mate(f->moves, f->pattern), mv);
        }
        if (const auto* f = as<fact::WinsMaterial>(fact)) return fmt::format("Loses {} after {}.", won(*f), mv);
        if (const auto* f = as<fact::Fork>(fact)) {
            const auto who = at_once ? nameOf(f->attacker.role) : piece(f->attacker);
            return fmt::format("{}: {} attacks {}.", lead("the fork", "a fork"), who, pieces(f->targets));
        }
        if (const auto* f = as<fact::Pin>(fact)) {
            const auto to = f->behind.role == Role::King ? std::string() : fmt::format(" to {}", piece(f->behind));
            if (at_once) return fmt::format("Allows {}, pinning {}{}.", mv, piece(f->pinned), to);
            return fmt::format("After {}, {} is pinned{}.", mv, piece(f->pinned), to);
        }
        if (const auto* f = as<fact::Skewer>(fact))
            return fmt::format("{}: {} must step aside and {} falls.", lead("the skewer", "a skewer"), piece(f->front),
                               piece(f->behind));
        if (const auto* f = as<fact::DiscoveredAttack>(fact)) {
            if (f->target.role == Role::King) {
                if (at_once) return fmt::format("Allows {}, a discovered check.", mv);
                return fmt::format("After {}, a discovered check follows.", mv);
            }
            if (at_once) return fmt::format("Allows {}, uncovering an attack on {}.", mv, piece(f->target));
            return fmt::format("After {}, a discovered attack hits {}.", mv, piece(f->target));
        }
        if (as<fact::DoubleCheck>(fact)) {
            if (at_once) return fmt::format("Allows {}, a double check.", mv);
            return fmt::format("After {}, a double check follows.", mv);
        }
        if (const auto* f = as<fact::Trapped>(fact))
            return fmt::format("After {}, {} is trapped.", mv, piece(f->piece));
        if (as<fact::ThreatensMate>(fact)) return fmt::format("After {}, mate is threatened.", mv);
        if (const auto* f = as<fact::Deflection>(fact)) {
            const auto from = f->piece.color == f->guarded.color
                                  ? fmt::format("the defence of {}", piece(f->guarded))
                                  : fmt::format("the {} square", name(f->guarded.square));
            return fmt::format("After {}, {} is drawn away from {}.", mv, piece(f->piece), from);
        }
        if (const auto* f = as<fact::Overloaded>(fact))
            return fmt::format("After {}, {}, which guarded both {} and {}, is overloaded.", mv, piece(f->piece),
                               piece(f->first), second(f->piece, f->second));
        if (const auto* f = as<fact::Clearance>(fact)) {
            std::string what;
            if (f->user.role == Role::Pawn)
                what = fmt::format("the promotion square for {}", piece(f->user));
            else if (f->line)
                what = fmt::format("the {} for {}", lineName(*f->line), piece(f->user));
            else
                what = fmt::format("{} for {}", name(f->piece.square), piece(f->user));
            if (at_once) return fmt::format("Allows {}, which clears {}.", mv, what);
            return fmt::format("After {}, {} clears {}.", mv, piece(f->piece), what);
        }
        if (const auto* f = as<fact::Breakthrough>(fact))
            return fmt::format("{}: {} gets through.", lead("the breakthrough", "a breakthrough"), piece(f->passer));
        if (const auto* f = as<fact::Race>(fact))
            return fmt::format("Loses the race to promote after {}: the opponent queens first{}.", mv,
                               f->check ? ", with check" : "");
        if (const auto* f = as<fact::Attraction>(fact))
            return fmt::format("After {}, {} is lured to {}.", mv, nameOf(f->piece.role), name(f->piece.square));
        if (const auto* f = as<fact::RemovesDefender>(fact)) {
            const auto left = f->defender.color == f->guarded.color
                                  ? piece(f->guarded)
                                  : fmt::format("the {} square", name(f->guarded.square));
            return fmt::format("After {}, {} loses its defender, {}.", mv, left, piece(f->defender));
        }
        if (as<fact::Intermezzo>(fact)) return fmt::format("Allows the in-between move {}.", mv);
        if (const auto* f = as<fact::Sacrifice>(fact)) {
            if (at_once) return fmt::format("Allows the sacrifice {}.", mv);
            return fmt::format("After {}, a {} sacrifice follows.", mv, noun(f->piece.role));
        }
        if (as<fact::Zugzwang>(fact)) return fmt::format("After {}, it is zugzwang.", mv);
        if (const auto* f = as<fact::Promotes>(fact))
            return fmt::format("After {}, a pawn promotes on {}.", mv, name(f->square));
        if (as<fact::PerpetualCheck>(fact)) return fmt::format("Allows a perpetual check starting with {}.", mv);
        if (const auto* f = as<fact::PawnBreak>(fact); f && at_once) {
            if (f->lever) return fmt::format("Allows {}, {}.", mv, leverWords(*f->lever, Lang::En));
            return fmt::format("Allows the pawn break {} against {}.", mv, piece(f->target));
        }
        if (const auto* f = as<fact::GainsSpace>(fact); f && at_once)
            return fmt::format("Allows {}, gaining space {}.", mv, wingWords(f->pawn.square, Lang::En));
        if (const auto* f = as<fact::Opposition>(fact); f && at_once)
            return fmt::format("Allows {}, taking the {}opposition.", mv, f->distant ? "distant " : "");
        if (as<fact::Outruns>(fact))
            return fmt::format("After {}, the pawn can no longer be caught: the king is outside its square.", mv);
        if (const auto* f = as<fact::EntersSquare>(fact); f && at_once)
            return fmt::format("Allows {}: the king steps into the square of {}.", mv, piece(f->pawn));
        if (const auto* f = as<fact::CutsOff>(fact); f && at_once)
            return fmt::format("Allows {}, cutting the king off along {}.", mv,
                               barrierWords(f->line, f->rook.square, Lang::En));
        if (const auto* f = as<fact::KeySquare>(fact); f && at_once)
            return fmt::format("Allows {}: the king reaches a key square of {}.", mv, piece(f->pawn));
        if (as<fact::Lucena>(fact) && at_once) return fmt::format("Allows {}, reaching the Lucena position.", mv);
        if (as<fact::Philidor>(fact) && at_once) return fmt::format("Allows {}, Philidor's defence.", mv);
        if (as<fact::WrongBishop>(fact))
            return fmt::format("After {}, the endgame is drawn: the bishop does not control the promotion square.", mv);
        return std::nullopt;
    }

    std::optional<std::string> cause(const Fact& fact, std::optional<std::string_view> falls) const override {
        if (const auto* f = as<fact::Unguards>(fact)) {
            if (falls)
                return capitalize(
                    fmt::format("{} no longer guards {}, which falls to {}.", piece(f->piece), piece(f->left), *falls));
            return capitalize(fmt::format("{} no longer guards {}.", piece(f->piece), piece(f->left)));
        }
        if (const auto* f = as<fact::Neglects>(fact)) {
            if (falls)
                return capitalize(fmt::format("{}, already attacked, is left en prise: {}.", piece(f->piece), *falls));
            return capitalize(fmt::format("{} was already under attack.", piece(f->piece)));
        }
        if (const auto* f = as<fact::OpensLine>(fact))
            return capitalize(fmt::format("The move opens the {} for {}.", lineName(f->line), piece(f->slider)));
        return std::nullopt;
    }

    std::string costs(std::string_view sentence, const Fact& cost) const override {
        const auto* w = as<fact::WinsMaterial>(cost);
        if (!w) return std::string(sentence);
        return fmt::format("{}, which costs {}.", trimEndDots(sentence), won(*w));
    }

    std::string better(std::string_view mv, std::span<const Fact> why, bool chance) const override {
        for (const auto& f : why) {
            if (const auto* t = as<fact::TakesWith>(f))
                return fmt::format("{} with {} was right: {}.", t->back ? "Taking back" : "Taking", nameOf(t->by.role),
                                   mv);
        }
        if (!why.empty()) {
            if (const auto* m = as<fact::Mate>(why.front())) {
                if (m->moves == 1) return fmt::format("Misses mate: {}.", mv);
                return fmt::format("Misses a mate in {} starting with {}.", m->moves, mv);
            }
        }
        if (std::any_of(why.begin(), why.end(), [](const Fact& f) { return as<fact::Sacrifice>(f) != nullptr; }))
            return fmt::format("{} was the brave choice, a sound sacrifice.", mv);
        const auto said = phrases_detail::reasons(why, would, by);
        std::optional<std::string> what;
        if (said.size() == 1) what = said[0];
        if (said.size() >= 2) what = said[0] + " and " + said[1];
        if (!what) return fmt::format("{} was better.", mv);
        if (chance) return fmt::format("Misses {}, which {}.", mv, *what);
        return fmt::format("{} was better: it {}.", mv, *what);
    }

    std::string otherwise(std::string_view alt, std::string_view sentence) const override {
        // "23.Kh1 loses a piece…", but "On 23.Kh1, after 23…Qxf3+, the queen…".
        const bool of_the_line = sentence.starts_with("After ") || sentence.starts_with("The ");
        if (of_the_line) return fmt::format("On {}, {}", alt, lowerFirst(sentence));
        return fmt::format("{} {}", alt, lowerFirst(sentence));
    }

    std::string onlyMove(double win_after, const Fact* why) const override {
        if (why) {
            const auto& fact = *why;
            if (const auto* f = as<fact::OnlyMoveToKeep>(fact)) {
                if (f->verdict == Verdict::Win) return "The only winning move, according to the tablebases.";
                return "The only drawing move, according to the tablebases.";
            }
            if (const auto* f = as<fact::Retakes>(fact))
                return fmt::format("Only move: it has to take back on {}.", name(f->square));
            if (const auto* f = as<fact::Mate>(fact)) {
                if (f->moves == 1) return "Mate.";
                return fmt::format("Only move: it mates in {}.", f->moves);
            }
            if (as<fact::PerpetualCheck>(fact)) return "Only move: it forces a perpetual check.";
            if (as<fact::Stalemate>(fact)) return "Only move: it saves the game by stalemate.";
            if (as<fact::Zugzwang>(fact)) return "Only move: it puts the opponent in zugzwang.";
            if (const auto* f = as<fact::Opposition>(fact))
                return fmt::format("Only move: the king takes the {}opposition.", f->distant ? "distant " : "");
            if (as<fact::Outruns>(fact)) return "Only move: the pawn runs, the enemy king is outside its square.";
            if (const auto* f = as<fact::EntersSquare>(fact))
                return fmt::format("Only move: the king steps into the square of {}.", piece(f->pawn));
            if (const auto* f = as<fact::CutsOff>(fact))
                return fmt::format("Only move: the rook cuts the enemy king off along {}.",
                                   barrierWords(f->line, f->rook.square, Lang::En));
            if (as<fact::Luft>(fact)) return "Only move: it gives the king luft.";
            if (const auto* f = as<fact::Saves>(fact))
                return fmt::format("Only move: the {}, attacked on {}, steps out of danger.", noun(f->piece.role),
                                   name(f->piece.square));
            if (const auto* f = as<fact::KeySquare>(fact))
                return fmt::format("Only move: the king reaches a key square of {}.", piece(f->pawn));
            if (as<fact::Lucena>(fact)) return "Only move: it reaches the Lucena position.";
            if (const auto* f = as<fact::Philidor>(fact))
                return fmt::format("Only move: Philidor's defence, the rook holds {}.",
                                   barrierWords(LineKind::Rank, f->rook.square, Lang::En));
            if (as<fact::WrongBishop>(fact))
                return "Only move: the endgame is drawn, the bishop does not control the promotion square.";
            if (const auto* f = as<fact::WinsMaterial>(fact)) return fmt::format("Only move, and it wins {}.", won(*f));
        }
        static constexpr const char* standing[] = {"Only move to keep the advantage.", "Only move to keep the balance.",
                                                   "Only move to hold on."};
        return standing[phrases_detail::standing(win_after)];
    }

    std::optional<std::string> achieves(const Fact& fact) const override {
        if (const auto* f = as<fact::Mate>(fact)) {
            if (f->moves == 1) return "Mate.";
            return fmt::format("{} follows.", capitalize(mate(f->moves, f->pattern)));
        }
        if (const auto* f = as<fact::WinsMaterial>(fact)) return fmt::format("Wins {}.", won(*f));
        if (const auto* f = as<fact::Deflection>(fact); f && f->piece.color == f->guarded.color)
            return capitalize(
                fmt::format("{} is drawn away from the defence of {}.", piece(f->piece), piece(f->guarded)));
        if (const auto* f = as<fact::Overloaded>(fact))
            return capitalize(fmt::format("{}, which guarded both {} and {}, is overloaded.", piece(f->piece),
                                          piece(f->first), second(f->piece, f->second)));
        if (const auto* f = as<fact::Clearance>(fact)) {
            if (f->user.role == Role::Pawn) return fmt::format("Clears the promotion square for {}.", piece(f->user));
            if (f->line) return fmt::format("Clears the {} for {}.", lineName(*f->line), piece(f->user));
            return fmt::format("Clears {} for {}.", name(f->piece.square), piece(f->user));
        }
        if (const auto* f = as<fact::Breakthrough>(fact))
            return fmt::format("A breakthrough: {} is offered to get {} through.", piece(f->pawn), piece(f->passer));
        if (const auto* f = as<fact::Race>(fact))
            return f->check ? "Wins the race to promote, with check." : "Wins the race to promote.";
        if (const auto* f = as<fact::Attraction>(fact))
            return capitalize(fmt::format("{} is lured to {}.", nameOf(f->piece.role), name(f->piece.square)));
        if (const auto* f = as<fact::RemovesDefender>(fact); f && f->defender.color == f->guarded.color)
            return fmt::format("Removes {}, which guarded {}.", piece(f->defender), piece(f->guarded));
        if (as<fact::Intermezzo>(fact)) return "An in-between move before taking back.";
        if (const auto* f = as<fact::Fork>(fact))
            return fmt::format("Fork: {} attacks {}.", piece(f->attacker), pieces(f->targets));
        if (const auto* f = as<fact::Skewer>(fact))
            return fmt::format("Skewer: {} must step aside and {} falls.", piece(f->front), piece(f->behind));
        if (const auto* f = as<fact::DiscoveredAttack>(fact))
            return fmt::format("Discovered attack on {}.", piece(f->target));
        if (const auto* f = as<fact::Pin>(fact)) return fmt::format("Pins {}.", piece(f->pinned));
        if (as<fact::DoubleCheck>(fact)) return "Double check!";
        if (const auto* f = as<fact::Trapped>(fact)) return fmt::format("{} is trapped.", capitalize(piece(f->piece)));
        if (const auto* f = as<fact::Sacrifice>(fact)) return fmt::format("A sound {} sacrifice.", noun(f->piece.role));
        if (as<fact::ThreatensMate>(fact)) return "Threatens mate.";
        if (as<fact::PerpetualCheck>(fact)) return "Forces a perpetual check.";
        if (as<fact::Stalemate>(fact)) return "Saves the game by stalemate.";
        if (const auto* f = as<fact::Hanging>(fact)) return fmt::format("Takes {}, left undefended.", piece(f->piece));
        if (const auto* f = as<fact::OnlyMoveToKeep>(fact)) {
            if (f->verdict == Verdict::Win) return "The only winning move, according to the tablebases.";
            return "The only drawing move, according to the tablebases.";
        }
        if (as<fact::Zugzwang>(fact)) return "Puts the opponent in zugzwang.";
        if (const auto* f = as<fact::Formation>(fact))
            return capitalize(fmt::format("{}.", formationSentence(f->kind, f->side, Lang::En)));
        if (const auto* f = as<fact::PawnBreak>(fact)) {
            if (f->lever && (f->lever->kind == Lever::Kind::ChainBase || f->lever->kind == Lever::Kind::ChainHead))
                return capitalize(fmt::format("{}, on {}.", leverWords(*f->lever, Lang::En), name(f->target.square)));
            if (f->lever) return capitalize(fmt::format("{}.", leverWords(*f->lever, Lang::En)));
        }
        if (as<fact::Space>(fact)) return "Takes more space.";
        if (as<fact::MinorityAttack>(fact)) return "Starts the minority attack.";
        if (as<fact::PawnStorm>(fact)) return "Storms the enemy king with pawns.";
        if (as<fact::Simplifies>(fact)) return "Simplifies while ahead in material.";
        if (const auto* f = as<fact::Opposition>(fact))
            return f->distant ? "Takes the distant opposition." : "Takes the opposition.";
        if (const auto* f = as<fact::CutsOff>(fact))
            return fmt::format("Cuts the enemy king off along {}.", barrierWords(f->line, f->rook.square, Lang::En));
        if (const auto* f = as<fact::EntersSquare>(fact))
            return fmt::format("The king steps into the square of {}.", piece(f->pawn));
        if (const auto* f = as<fact::Majority>(fact))
            return fmt::format("Gets the pawn majority {}.", ofWing(f->wing, Lang::En));
        if (const auto* f = as<fact::KeySquare>(fact))
            return fmt::format("The king reaches a key square of {}.", piece(f->pawn));
        if (as<fact::Lucena>(fact)) return "Reaches the Lucena position.";
        if (const auto* f = as<fact::Philidor>(fact))
            return fmt::format("Philidor's defence: the rook holds {}.",
                               barrierWords(LineKind::Rank, f->rook.square, Lang::En));
        if (as<fact::WrongBishop>(fact)) return "A drawn endgame: the bishop does not control the promotion square.";
        return std::nullopt;
    }
};

}  // namespace

const Phrasebook& englishPhrasebook() {
    static const English book;
    return book;
}

std::string englishMaterial(int32_t pawns, std::optional<Trade> trade) { return material(pawns, trade); }
const char* englishKind(Kind k, uint8_t n) { return kind(k, n); }

}  // namespace fcg::commentary
