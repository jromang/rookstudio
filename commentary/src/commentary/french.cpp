// The French phrasebook (the prototype's crates/commentary/src/phrases.rs, French).

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

// (name, feminine)
std::pair<const char*, bool> noun(Role role) {
    switch (role) {
        case Role::King:
            return {"roi", false};
        case Role::Queen:
            return {"dame", true};
        case Role::Rook:
            return {"tour", true};
        case Role::Bishop:
            return {"fou", false};
        case Role::Knight:
            return {"cavalier", false};
        default:
            return {"pion", false};
    }
}

// « le cavalier en e6 », « la dame en d8 »; the king needs no square.
std::string piece(const Placed& p) {
    const auto [n, feminine] = noun(p.role);
    const char* article      = feminine ? "la" : "le";
    if (p.role == Role::King) return fmt::format("{} {}", article, n);
    return fmt::format("{} {} en {}", article, n, name(p.square));
}

// « du cavalier en e6 », « de la dame en d8 », « du roi ».
std::string ofPiece(const Placed& p) {
    const auto text = piece(p);
    if (text.starts_with("le ")) return "du " + text.substr(3);
    return "de " + text;
}

// « au cavalier en e6 », « à la dame en d8 », « au roi ».
std::string toPiece(const Placed& p) {
    const auto text = piece(p);
    if (text.starts_with("le ")) return "au " + text.substr(3);
    return "à " + text;
}

// « le cavalier », « la dame »: the piece without its square.
std::string nameOf(Role role) {
    const auto [n, feminine] = noun(role);
    return fmt::format("{} {}", feminine ? "la" : "le", n);
}

// « du fou », « de la tour »: what one takes back with.
std::string with(Role role) {
    const auto [n, feminine] = noun(role);
    return fmt::format("{} {}", feminine ? "de la" : "du", n);
}

// « enfermé », « enfermée »: a participle agreed with the piece.
std::string agreed(const Placed& p, std::string_view word) {
    return noun(p.role).second ? std::string(word) + "e" : std::string(word);
}

// The second thing an overloaded piece guarded: a piece of its side, or the square an enemy piece then mates from.
std::string second(const Placed& guard, const Placed& other) {
    if (guard.color == other.color) return piece(other);
    return fmt::format("la case {}", name(other.square));
}

std::string pieces(const std::vector<Placed>& list) {
    if (list.empty()) return {};
    if (list.size() == 1) return piece(list.front());
    std::string out;
    for (std::size_t i = 0; i + 1 < list.size(); ++i) out += (i ? ", " : "") + piece(list[i]);
    return out + " et " + piece(list.back());
}

const char* kind(Kind k, uint8_t n) {
    switch (k) {
        case Kind::Exchange:
            return n == 1 ? "la qualité" : nullptr;
        case Kind::Queen:
            return n == 1 ? "la dame" : nullptr;
        case Kind::Rook:
            return n == 1 ? "une tour" : n == 2 ? "deux tours" : nullptr;
        case Kind::Piece:
            return n == 1 ? "une pièce" : n == 2 ? "deux pièces" : n == 3 ? "trois pièces" : nullptr;
        default:
            return n == 1   ? "un pion"
                   : n == 2 ? "deux pions"
                   : n == 3 ? "trois pions"
                   : n == 4 ? "quatre pions"
                            : nullptr;
    }
}

std::string material(int32_t pawns, std::optional<Trade> trade) {
    if (trade) {
        if (auto words = phrases_detail::tradeWords(*trade, kind, " et ", " contre ")) return *words;
    }
    switch (pawns) {
        case 1:
            return "un pion";
        case 2:
            return "deux pions";
        case 3:
            return "une pièce";
        case 4:
            return "une pièce et un pion";
        case 5:
            return "l'équivalent d'une tour";
        default:
            return "beaucoup de matériel";
    }
}

// What a line wins: the piece it comes down to when there is one, « le fou en h6 contre un pion », else the count.
std::string won(int32_t pawns, std::optional<Trade> trade, std::optional<Placed> taken) {
    std::optional<std::pair<std::string, std::optional<std::string>>> parts;
    if (trade) parts = phrases_detail::tradeParts(*trade, kind, " et ");
    if (taken && parts && parts->second) return fmt::format("{} contre {}", piece(*taken), *parts->second);
    if (taken) return piece(*taken);
    return material(pawns, trade);
}

std::string won(const fact::WinsMaterial& w) { return won(w.pawns, w.trade, w.taken); }

const char* lineName(LineKind line) {
    switch (line) {
        case LineKind::File:
            return "colonne";
        case LineKind::Rank:
            return "rangée";
        default:
            return "diagonale";
    }
}

const char* endingWords(Ending ending) {
    switch (ending) {
        case Ending::Queenless:
            return "échangeait les dames";
        case Ending::Pawn:
            return "passait en finale de pions";
        case Ending::Rook:
            return "passait en finale de tours";
        case Ending::Queen:
            return "passait en finale de dames";
        case Ending::Minor:
            return "passait en finale de pièces mineures";
        default:
            return "passait en finale de fous de couleurs opposées";
    }
}

const char* mateKind(std::optional<MatePattern> pattern) {
    if (pattern == MatePattern::BackRank) return " du couloir";
    if (pattern == MatePattern::Smothered) return " étouffé";
    return "";
}

std::string mate(uint32_t moves, std::optional<MatePattern> pattern) {
    if (moves == 1) return fmt::format("un mat{} immédiat", mateKind(pattern));
    return fmt::format("un mat{} en {} coups", mateKind(pattern), moves);
}

// The tactic that wins something, as a phrase after what it wins: « par une fourchette ».
std::optional<std::string> by(const Fact& fact) {
    if (as<fact::Fork>(fact)) return "par une fourchette";
    if (const auto* f = as<fact::Pin>(fact)) return fmt::format("grâce au clouage {}", ofPiece(f->pinned));
    if (as<fact::Skewer>(fact)) return "par une enfilade";
    if (const auto* f = as<fact::DiscoveredAttack>(fact))
        return f->target.role == Role::King ? "par un échec à la découverte" : "par une attaque à la découverte";
    if (as<fact::DoubleCheck>(fact)) return "par un échec double";
    if (const auto* f = as<fact::Deflection>(fact)) return fmt::format("en détournant {}", piece(f->piece));
    if (const auto* f = as<fact::Overloaded>(fact)) return fmt::format("grâce à la surcharge {}", ofPiece(f->piece));
    if (const auto* f = as<fact::Clearance>(fact)) {
        if (f->user.role == Role::Pawn) return "en libérant la case de promotion";
        if (f->line) return fmt::format("en dégageant la {} {}", lineName(*f->line), ofPiece(f->user));
        return fmt::format("en dégageant une case pour {}", piece(f->user));
    }
    if (as<fact::Breakthrough>(fact)) return "par une percée";
    if (const auto* f = as<fact::Attraction>(fact))
        return fmt::format("en attirant {} en {}", nameOf(f->piece.role), name(f->piece.square));
    if (const auto* f = as<fact::RemovesDefender>(fact))
        return fmt::format("en éliminant {}, le défenseur", piece(f->defender));
    if (as<fact::Intermezzo>(fact)) return "par un coup intermédiaire";
    return std::nullopt;
}

// What a better move would have done, as a verb phrase: « gagnait une pièce ».
std::optional<std::string> would(const Fact& fact) {
    if (const auto* f = as<fact::Mate>(fact)) return fmt::format("matait en {}", f->moves);
    if (const auto* f = as<fact::WinsMaterial>(fact)) return fmt::format("gagnait {}", won(*f));
    if (const auto* f = as<fact::Fork>(fact)) return fmt::format("faisait une fourchette sur {}", pieces(f->targets));
    if (const auto* f = as<fact::Pin>(fact)) return fmt::format("clouait {}", piece(f->pinned));
    if (const auto* f = as<fact::Skewer>(fact))
        return fmt::format("prenait {} et {} en enfilade", piece(f->front), piece(f->behind));
    if (const auto* f = as<fact::DiscoveredAttack>(fact)) {
        if (f->target.role == Role::King) return "donnait un échec à la découverte";
        return fmt::format("découvrait une attaque sur {}", piece(f->target));
    }
    if (as<fact::DoubleCheck>(fact)) return "donnait un échec double";
    if (const auto* f = as<fact::Trapped>(fact)) return fmt::format("enfermait {}", piece(f->piece));
    if (const auto* f = as<fact::Hanging>(fact)) return fmt::format("prenait {}, en prise", piece(f->piece));
    if (as<fact::ThreatensMate>(fact)) return "menaçait mat";
    if (const auto* f = as<fact::ThreatensCapture>(fact)) return fmt::format("attaquait {}", piece(f->target));
    if (as<fact::Promotes>(fact)) return "menait un pion à promotion";
    if (as<fact::PerpetualCheck>(fact)) return "forçait l'échec perpétuel";
    if (as<fact::Stalemate>(fact)) return "sauvait la partie par le pat";
    if (as<fact::Zugzwang>(fact)) return "mettait l'adversaire en zugzwang";
    if (const auto* f = as<fact::Deflection>(fact)) {
        if (f->piece.color == f->guarded.color)
            return fmt::format("détournait {} de la défense {}", piece(f->piece), ofPiece(f->guarded));
        return fmt::format("détournait {} de la case {}", piece(f->piece), name(f->guarded.square));
    }
    if (const auto* f = as<fact::Overloaded>(fact))
        return fmt::format("surchargeait {}, qui défendait à la fois {} et {}", piece(f->piece), piece(f->first),
                           second(f->piece, f->second));
    if (const auto* f = as<fact::Clearance>(fact)) {
        if (f->user.role == Role::Pawn) return fmt::format("libérait la case de promotion {}", ofPiece(f->user));
        if (f->line) return fmt::format("dégageait la {} pour {}", lineName(*f->line), piece(f->user));
        return fmt::format("dégageait la case {} pour {}", name(f->piece.square), piece(f->user));
    }
    if (const auto* f = as<fact::Breakthrough>(fact))
        return fmt::format("forçait le passage {} par une percée", ofPiece(f->passer));
    if (const auto* f = as<fact::Race>(fact))
        return f->check ? "gagnait la course à la promotion, avec échec" : "gagnait la course à la promotion";
    if (const auto* f = as<fact::Attraction>(fact))
        return fmt::format("attirait {} en {}", nameOf(f->piece.role), name(f->piece.square));
    if (const auto* f = as<fact::RemovesDefender>(fact)) {
        if (f->defender.color == f->guarded.color)
            return fmt::format("éliminait {}, qui défendait {}", piece(f->defender), piece(f->guarded));
        return fmt::format("éliminait {}, le défenseur", piece(f->defender));
    }
    if (as<fact::Intermezzo>(fact)) return "intercalait un coup avant de reprendre";
    if (const auto* f = as<fact::Guards>(fact)) return fmt::format("défendait {}", piece(f->guarded));
    if (const auto* f = as<fact::Develops>(fact)) return fmt::format("développait {}", nameOf(f->piece.role));
    if (const auto* f = as<fact::Pressure>(fact)) return fmt::format("mettait {} sous pression", piece(f->target));
    if (const auto* f = as<fact::ProtectedPasser>(fact))
        return fmt::format("créait un pion passé protégé en {}", name(f->pawn.square));
    if (as<fact::ConnectedPassers>(fact)) return "créait des pions passés liés";
    if (const auto* f = as<fact::OutsidePasser>(fact))
        return fmt::format("créait un pion passé éloigné en {}", name(f->pawn.square));
    if (as<fact::RookBehindPasser>(fact)) return "plaçait la tour derrière le pion passé";
    if (const auto* f = as<fact::Blockade>(fact)) return fmt::format("bloquait {}", piece(f->pawn));
    if (as<fact::Simplifies>(fact)) return "simplifiait avec du matériel en plus";
    if (as<fact::PawnStorm>(fact)) return "lançait les pions contre le roque adverse";
    if (as<fact::MinorityAttack>(fact)) return "lançait l'attaque de minorité";
    if (const auto* f = as<fact::Saves>(fact)) return fmt::format("mettait {} à l'abri", piece(f->piece));
    if (const auto* f = as<fact::Covers>(fact)) return fmt::format("contrôlait la case {}", name(f->square));
    if (as<fact::Castles>(fact)) return "mettait le roi à l'abri par le roque";
    if (const auto* f = as<fact::PassedPawn>(fact))
        return fmt::format("créait un pion passé en {}", name(f->pawn.square));
    if (as<fact::RookOnSeventh>(fact)) return "amenait une tour en septième rangée";
    if (as<fact::RookOnOpenFile>(fact)) return "donnait la colonne ouverte à la tour";
    if (const auto* f = as<fact::Outpost>(fact))
        return fmt::format("installait le cavalier sur l'avant-poste {}", name(f->knight.square));
    if (as<fact::BishopPair>(fact)) return "obtenait la paire de fous";
    if (const auto* f = as<fact::IsolatedPawn>(fact)) return fmt::format("isolait {}", piece(f->pawn));
    if (const auto* f = as<fact::DoubledPawns>(fact))
        return fmt::format("doublait les pions adverses sur la colonne {}", letter(f->file));
    if (as<fact::ExposedKing>(fact)) return "privait le roi adverse de son abri";
    if (as<fact::LosesCastling>(fact)) return "privait l'adversaire du roque";
    if (const auto* f = as<fact::PawnBreak>(fact)) {
        if (f->lever && (f->lever->kind == Lever::Kind::ChainBase || f->lever->kind == Lever::Kind::ChainHead))
            return fmt::format("lançait {}, en {}", leverWords(*f->lever, Lang::Fr), name(f->target.square));
        if (f->lever) return fmt::format("lançait {}", leverWords(*f->lever, Lang::Fr));
        return fmt::format("lançait la rupture contre {}", piece(f->target));
    }
    if (as<fact::Space>(fact)) return "prenait plus d'espace";
    if (const auto* f = as<fact::GainsSpace>(fact))
        return fmt::format("gagnait de l'espace {}", wingWords(f->pawn.square, Lang::Fr));
    if (const auto* f = as<fact::Formation>(fact))
        return fmt::format("mettait en place {}", formationName(f->kind, f->side, Lang::Fr));
    if (const auto* f = as<fact::Opposition>(fact))
        return f->distant ? "prenait l'opposition éloignée" : "prenait l'opposition";
    if (as<fact::Outruns>(fact)) return "laissait le roi adverse hors du carré du pion";
    if (const auto* f = as<fact::EntersSquare>(fact)) return fmt::format("entrait dans le carré {}", ofPiece(f->pawn));
    if (const auto* f = as<fact::CutsOff>(fact))
        return fmt::format("coupait le roi adverse le long de {}", barrierWords(f->line, f->rook.square, Lang::Fr));
    if (const auto* f = as<fact::Majority>(fact))
        return fmt::format("donnait la majorité de pions {}", ofWing(f->wing, Lang::Fr));
    if (const auto* f = as<fact::WeakColour>(fact))
        return fmt::format("affaiblissait {} adverses", colourSquares(f->dark, Lang::Fr));
    if (as<fact::Luft>(fact)) return "donnait de l'air au roi";
    if (const auto* f = as<fact::KeySquare>(fact))
        return fmt::format("amenait le roi sur une case clé {}", ofPiece(f->pawn));
    if (as<fact::Lucena>(fact)) return "atteignait la position de Lucena";
    if (const auto* f = as<fact::Philidor>(fact))
        return fmt::format("installait la défense de Philidor sur {}",
                           barrierWords(LineKind::Rank, f->rook.square, Lang::Fr));
    if (as<fact::WrongBishop>(fact)) return "menait à une finale nulle : le fou ne contrôle pas la case de promotion";
    if (as<fact::DevelopmentLead>(fact)) return "donnait une avance de développement";
    if (as<fact::PawnCentre>(fact)) return "bâtissait un centre de pions";
    if (as<fact::Compensation>(fact)) return "donnait de la compensation pour le matériel";
    if (as<fact::Initiative>(fact)) return "gardait l'initiative";
    if (as<fact::Counterplay>(fact)) return "donnait du contre-jeu";
    if (const auto* f = as<fact::BackwardPawn>(fact)) return fmt::format("laissait {} arriéré", piece(f->pawn));
    if (const auto* f = as<fact::BadBishop>(fact))
        return fmt::format("laissait à l'adversaire un mauvais fou en {}", name(f->bishop.square));
    if (as<fact::ActiveKing>(fact)) return "activait le roi";
    if (as<fact::KingAttack>(fact)) return "lançait l'attaque sur le roi";
    if (as<fact::MoreActive>(fact)) return "donnait des pièces plus actives";
    if (const auto* f = as<fact::LongDiagonal>(fact))
        return fmt::format("donnait la grande diagonale {}", toPiece(f->bishop));
    if (as<fact::CentreOpens>(fact)) return "ouvrait le centre";
    if (as<fact::CentreLocks>(fact)) return "bloquait le centre";
    if (const auto* f = as<fact::Hole>(fact)) return fmt::format("affaiblissait la case {}", name(f->square));
    if (const auto* f = as<fact::Endgame>(fact)) return std::string(endingWords(f->ending));
    return std::nullopt;
}

std::string trimEndDots(std::string_view s) {
    while (!s.empty() && s.back() == '.') s.remove_suffix(1);
    return std::string(s);
}

class French final : public Phrasebook {
   public:
    std::string novelty(std::optional<std::pair<std::string_view, Theory::Source>> usual) const override {
        if (usual && usual->second == Theory::Source::Surveys)
            return fmt::format("Nouveauté ; la ligne principale est {}.", usual->first);
        if (usual) return fmt::format("Nouveauté ; le coup habituel est {}.", usual->first);
        return "Nouveauté.";
    }

    std::string noveltyTakenUp() const override { return "Nouveauté, devenue depuis la ligne principale."; }

    std::optional<std::string> punishment(const Fact& fact) const override {
        if (const auto* f = as<fact::Mate>(fact)) return fmt::format("Permet {}.", mate(f->moves, f->pattern));
        if (const auto* f = as<fact::WinsMaterial>(fact)) return fmt::format("Perd {}.", won(*f));
        if (const auto* f = as<fact::Hanging>(fact)) return fmt::format("Laisse {} en prise.", piece(f->piece));
        if (const auto* f = as<fact::Deflection>(fact))
            return fmt::format("Permet une déviation {}.", ofPiece(f->piece));
        if (const auto* f = as<fact::Overloaded>(fact)) return fmt::format("Permet de surcharger {}.", piece(f->piece));
        if (const auto* f = as<fact::Clearance>(fact)) {
            if (f->user.role == Role::Pawn)
                return fmt::format("Permet de libérer la case de promotion {}.", ofPiece(f->user));
            if (f->line) return fmt::format("Permet de dégager la {} {}.", lineName(*f->line), ofPiece(f->user));
            return fmt::format("Permet de dégager une case pour {}.", piece(f->user));
        }
        if (const auto* f = as<fact::Breakthrough>(fact))
            return fmt::format("Permet une percée : {} passe.", piece(f->passer));
        if (as<fact::Race>(fact)) return "Perd la course à la promotion.";
        if (const auto* f = as<fact::Attraction>(fact))
            return fmt::format("Permet d'attirer {} en {}.", nameOf(f->piece.role), name(f->piece.square));
        if (const auto* f = as<fact::RemovesDefender>(fact))
            return fmt::format("Permet d'éliminer {}, le défenseur.", piece(f->defender));
        if (as<fact::Intermezzo>(fact)) return "Permet un coup intermédiaire.";
        if (const auto* f = as<fact::NotRetaken>(fact)) return fmt::format("Ne reprend pas en {}.", name(f->square));
        if (const auto* f = as<fact::Pressure>(fact))
            return fmt::format("Laisse {} sous la pression {}.", piece(f->target), ofPiece(f->piece));
        if (const auto* f = as<fact::ProtectedPasser>(fact))
            return fmt::format("Concède un pion passé protégé en {}.", name(f->pawn.square));
        if (as<fact::ConnectedPassers>(fact)) return "Concède des pions passés liés.";
        if (const auto* f = as<fact::OutsidePasser>(fact))
            return fmt::format("Concède un pion passé éloigné en {}.", name(f->pawn.square));
        if (as<fact::RookBehindPasser>(fact)) return "Laisse la tour adverse se placer derrière son pion passé.";
        if (const auto* f = as<fact::Blockade>(fact))
            return fmt::format("Laisse {} bloquer {}.", piece(f->piece), piece(f->pawn));
        if (as<fact::Simplifies>(fact)) return "Permet à l'adversaire de simplifier avec du matériel en plus.";
        if (as<fact::PawnStorm>(fact)) return "Laisse l'adversaire lancer ses pions contre le roque.";
        if (as<fact::MinorityAttack>(fact)) return "Permet l'attaque de minorité.";
        if (const auto* f = as<fact::MovesAgain>(fact))
            return fmt::format("Rejoue {} alors que le développement n'est pas terminé.", nameOf(f->piece.role));
        if (const auto* f = as<fact::Sacrifice>(fact))
            return fmt::format("Permet un sacrifice de {}.", noun(f->piece.role).first);
        if (const auto* f = as<fact::Fork>(fact))
            return fmt::format("Permet une fourchette : {} attaque {}.", piece(f->attacker), pieces(f->targets));
        if (const auto* f = as<fact::Pin>(fact)) return fmt::format("Permet le clouage {}.", ofPiece(f->pinned));
        if (const auto* f = as<fact::Skewer>(fact))
            return fmt::format("Permet une enfilade : {} doit s'écarter et {} tombe.", piece(f->front),
                               piece(f->behind));
        if (const auto* f = as<fact::DiscoveredAttack>(fact))
            return fmt::format("Permet une attaque à la découverte sur {}.", piece(f->target));
        if (as<fact::DoubleCheck>(fact)) return "Permet un échec double.";
        if (const auto* f = as<fact::Trapped>(fact))
            return capitalize(fmt::format("{} se retrouve {}.", piece(f->piece),
                                          noun(f->piece.role).second ? "enfermée" : "enfermé"));
        if (as<fact::ThreatensMate>(fact)) return "Laisse une menace de mat.";
        if (const auto* f = as<fact::SpoilsWin>(fact)) {
            if (f->now == Verdict::Draw) return "Gâche le gain : la position est nulle selon les tables.";
            return "Transforme le gain en perte, selon les tables.";
        }
        if (as<fact::SpoilsDraw>(fact)) return "Perd une position nulle selon les tables.";
        if (const auto* f = as<fact::Promotes>(fact))
            return fmt::format("Permet une promotion en {}.", name(f->square));
        if (as<fact::ExposedKing>(fact)) return "Laisse le roi sans abri.";
        if (const auto* f = as<fact::PassedPawn>(fact))
            return fmt::format("Concède un pion passé en {}.", name(f->pawn.square));
        if (as<fact::RookOnSeventh>(fact)) return "Laisse la tour adverse entrer en septième rangée.";
        if (const auto* f = as<fact::Outpost>(fact))
            return fmt::format("Offre un avant-poste en {} au cavalier adverse.", name(f->knight.square));
        if (as<fact::RookOnOpenFile>(fact)) return "Laisse la colonne ouverte à la tour adverse.";
        if (as<fact::LosesCastling>(fact)) return "Perd le droit de roquer.";
        if (const auto* f = as<fact::IsolatedPawn>(fact)) return fmt::format("Laisse {} isolé.", piece(f->pawn));
        if (const auto* f = as<fact::DoubledPawns>(fact))
            return fmt::format("Laisse des pions doublés sur la colonne {}.", letter(f->file));
        if (as<fact::BishopPair>(fact)) return "Cède la paire de fous.";
        if (as<fact::PerpetualCheck>(fact)) return "Permet un échec perpétuel.";
        if (as<fact::Stalemate>(fact)) return "Permet le pat.";
        if (const auto* f = as<fact::PawnBreak>(fact)) {
            if (f->lever) return fmt::format("Permet {}.", leverWords(*f->lever, Lang::Fr));
            return fmt::format("Permet une rupture contre {}.", piece(f->target));
        }
        if (as<fact::Space>(fact)) return "Laisse l'adversaire prendre plus d'espace.";
        if (const auto* f = as<fact::GainsSpace>(fact))
            return fmt::format("Laisse l'adversaire gagner de l'espace {}.", wingWords(f->pawn.square, Lang::Fr));
        if (const auto* f = as<fact::Formation>(fact))
            return fmt::format("Laisse l'adversaire mettre en place {}.", formationName(f->kind, f->side, Lang::Fr));
        if (as<fact::Opposition>(fact)) return "Cède l'opposition.";
        if (const auto* f = as<fact::Outruns>(fact))
            return fmt::format("Laisse filer {} : le roi est hors du carré.", piece(f->pawn));
        if (const auto* f = as<fact::EntersSquare>(fact))
            return fmt::format("Laisse le roi adverse entrer dans le carré {}.", ofPiece(f->pawn));
        if (as<fact::CutsOff>(fact)) return "Laisse la tour adverse couper le roi.";
        if (const auto* f = as<fact::Majority>(fact))
            return fmt::format("Concède la majorité de pions {}.", ofWing(f->wing, Lang::Fr));
        if (const auto* f = as<fact::WeakColour>(fact))
            return capitalize(fmt::format("{} sont affaiblies.", colourSquares(f->dark, Lang::Fr)));
        if (const auto* f = as<fact::KeySquare>(fact))
            return fmt::format("Laisse le roi adverse atteindre une case clé {}.", ofPiece(f->pawn));
        if (as<fact::Lucena>(fact)) return "Laisse l'adversaire atteindre la position de Lucena.";
        if (as<fact::Philidor>(fact)) return "Laisse l'adversaire installer la défense de Philidor.";
        if (as<fact::WrongBishop>(fact))
            return "Laisse une finale nulle : le fou ne contrôle pas la case de promotion.";
        if (as<fact::DevelopmentLead>(fact)) return "Donne une avance de développement à l'adversaire.";
        if (as<fact::PawnCentre>(fact)) return "Laisse l'adversaire bâtir un centre de pions.";
        if (as<fact::Compensation>(fact)) return "Donne de la compensation à l'adversaire.";
        if (as<fact::Initiative>(fact)) return "Donne l'initiative à l'adversaire.";
        if (as<fact::Counterplay>(fact)) return "Permet du contre-jeu.";
        if (const auto* f = as<fact::BackwardPawn>(fact)) return fmt::format("Laisse {} arriéré.", piece(f->pawn));
        if (as<fact::HangingPawns>(fact)) return "Laisse des pions pendants.";
        if (as<fact::BadBishop>(fact)) return "Laisse un mauvais fou.";
        if (as<fact::ActiveKing>(fact)) return "Laisse le roi adverse s'activer.";
        if (as<fact::KingAttack>(fact)) return "Laisse l'adversaire attaquer le roi.";
        if (as<fact::MoreActive>(fact)) return "Laisse les pièces adverses plus actives.";
        if (const auto* f = as<fact::LongDiagonal>(fact))
            return fmt::format("Laisse {} balayer la grande diagonale.", piece(f->bishop));
        if (as<fact::CentreOpens>(fact)) return "Ouvre le centre pour l'adversaire.";
        if (const auto* f = as<fact::Hole>(fact)) return fmt::format("Affaiblit la case {}.", name(f->square));
        if (as<fact::Zugzwang>(fact)) return "Mène à un zugzwang.";
        return std::nullopt;
    }

    std::optional<std::string> refuted(const Fact& fact, std::string_view mv, bool at_once) const override {
        // « Permet la fourchette 17…e4 » when the answer does it, « Après 17…e4, une fourchette suit » when it comes
        // later in the line.
        const auto lead = [&](std::string_view named, std::string_view later) {
            return at_once ? fmt::format("Permet {} {}", named, mv) : fmt::format("Après {}, {} suit", mv, later);
        };
        if (const auto* f = as<fact::Mate>(fact)) {
            if (f->moves == 1) return fmt::format("Permet le mat{} : {}.", mateKind(f->pattern), mv);
            return fmt::format("Permet {} par {}.", mate(f->moves, f->pattern), mv);
        }
        if (const auto* f = as<fact::WinsMaterial>(fact)) return fmt::format("Perd {} après {}.", won(*f), mv);
        if (const auto* f = as<fact::Fork>(fact)) {
            const auto who = at_once ? nameOf(f->attacker.role) : piece(f->attacker);
            return fmt::format("{} : {} attaque {}.", lead("la fourchette", "une fourchette"), who, pieces(f->targets));
        }
        if (const auto* f = as<fact::Pin>(fact)) {
            const auto on = f->behind.role == Role::King ? std::string() : fmt::format(" sur {}", piece(f->behind));
            if (at_once) return fmt::format("Permet {}, qui cloue {}{}.", mv, piece(f->pinned), on);
            return fmt::format("Après {}, {} est {}{}.", mv, piece(f->pinned), agreed(f->pinned, "cloué"), on);
        }
        if (const auto* f = as<fact::Skewer>(fact))
            return fmt::format("{} : {} doit s'écarter et {} tombe.", lead("l'enfilade", "une enfilade"),
                               piece(f->front), piece(f->behind));
        if (const auto* f = as<fact::DiscoveredAttack>(fact)) {
            if (f->target.role == Role::King) {
                if (at_once) return fmt::format("Permet {}, un échec à la découverte.", mv);
                return fmt::format("Après {}, un échec à la découverte suit.", mv);
            }
            if (at_once) return fmt::format("Permet {}, qui découvre une attaque sur {}.", mv, piece(f->target));
            return fmt::format("Après {}, une attaque à la découverte vise {}.", mv, piece(f->target));
        }
        if (as<fact::DoubleCheck>(fact)) {
            if (at_once) return fmt::format("Permet {}, un échec double.", mv);
            return fmt::format("Après {}, un échec double suit.", mv);
        }
        if (const auto* f = as<fact::Trapped>(fact))
            return fmt::format("Après {}, {} est {}.", mv, piece(f->piece), agreed(f->piece, "enfermé"));
        if (as<fact::ThreatensMate>(fact)) return fmt::format("Après {}, le mat menace.", mv);
        if (const auto* f = as<fact::Deflection>(fact)) {
            const auto from = f->piece.color == f->guarded.color ? fmt::format("la défense {}", ofPiece(f->guarded))
                                                                 : fmt::format("la case {}", name(f->guarded.square));
            return fmt::format("Après {}, {} est {} de {}.", mv, piece(f->piece), agreed(f->piece, "détourné"), from);
        }
        if (const auto* f = as<fact::Overloaded>(fact))
            return fmt::format("Après {}, {}, qui défendait à la fois {} et {}, est {}.", mv, piece(f->piece),
                               piece(f->first), second(f->piece, f->second), agreed(f->piece, "surchargé"));
        if (const auto* f = as<fact::Clearance>(fact)) {
            std::string what;
            if (f->user.role == Role::Pawn)
                what = fmt::format("la case de promotion {}", ofPiece(f->user));
            else if (f->line)
                what = fmt::format("la {} pour {}", lineName(*f->line), piece(f->user));
            else
                what = fmt::format("la case {} pour {}", name(f->piece.square), piece(f->user));
            if (at_once) return fmt::format("Permet {}, qui dégage {}.", mv, what);
            return fmt::format("Après {}, {} dégage {}.", mv, piece(f->piece), what);
        }
        if (const auto* f = as<fact::Breakthrough>(fact))
            return fmt::format("{} : {} passe.", lead("la percée", "une percée"), piece(f->passer));
        if (const auto* f = as<fact::Race>(fact))
            return fmt::format("Perd la course à la promotion après {} : l'adversaire fait dame le premier{}.", mv,
                               f->check ? ", avec échec" : "");
        if (const auto* f = as<fact::Attraction>(fact))
            return fmt::format("Après {}, {} est {} en {}.", mv, nameOf(f->piece.role), agreed(f->piece, "attiré"),
                               name(f->piece.square));
        if (const auto* f = as<fact::RemovesDefender>(fact)) {
            const auto left = f->defender.color == f->guarded.color
                                  ? piece(f->guarded)
                                  : fmt::format("la case {}", name(f->guarded.square));
            return fmt::format("Après {}, {} perd son défenseur, {}.", mv, left, piece(f->defender));
        }
        if (as<fact::Intermezzo>(fact)) return fmt::format("Permet le coup intermédiaire {}.", mv);
        if (const auto* f = as<fact::Sacrifice>(fact)) {
            if (at_once) return fmt::format("Permet le sacrifice {}.", mv);
            return fmt::format("Après {}, un sacrifice de {} suit.", mv, noun(f->piece.role).first);
        }
        if (as<fact::Zugzwang>(fact)) return fmt::format("Après {}, c'est le zugzwang.", mv);
        if (const auto* f = as<fact::Promotes>(fact))
            return fmt::format("Après {}, un pion va à promotion en {}.", mv, name(f->square));
        if (as<fact::PerpetualCheck>(fact)) return fmt::format("Permet l'échec perpétuel par {}.", mv);
        if (const auto* f = as<fact::PawnBreak>(fact); f && at_once) {
            if (f->lever) return fmt::format("Permet {}, {}.", mv, leverWords(*f->lever, Lang::Fr));
            return fmt::format("Permet la rupture {} contre {}.", mv, piece(f->target));
        }
        if (const auto* f = as<fact::GainsSpace>(fact); f && at_once)
            return fmt::format("Permet {}, qui gagne de l'espace {}.", mv, wingWords(f->pawn.square, Lang::Fr));
        if (const auto* f = as<fact::Opposition>(fact); f && at_once)
            return fmt::format("Permet {}, qui prend l'opposition{}.", mv, f->distant ? " éloignée" : "");
        if (as<fact::Outruns>(fact))
            return fmt::format("Après {}, le pion ne peut plus être rattrapé : le roi est hors du carré.", mv);
        if (const auto* f = as<fact::EntersSquare>(fact); f && at_once)
            return fmt::format("Permet {} : le roi entre dans le carré {}.", mv, ofPiece(f->pawn));
        if (const auto* f = as<fact::CutsOff>(fact); f && at_once)
            return fmt::format("Permet {}, qui coupe le roi le long de {}.", mv,
                               barrierWords(f->line, f->rook.square, Lang::Fr));
        if (const auto* f = as<fact::KeySquare>(fact); f && at_once)
            return fmt::format("Permet {} : le roi atteint une case clé {}.", mv, ofPiece(f->pawn));
        if (as<fact::Lucena>(fact) && at_once) return fmt::format("Permet {}, qui atteint la position de Lucena.", mv);
        if (as<fact::Philidor>(fact) && at_once) return fmt::format("Permet {}, la défense de Philidor.", mv);
        if (as<fact::WrongBishop>(fact))
            return fmt::format("Après {}, la finale est nulle : le fou ne contrôle pas la case de promotion.", mv);
        return std::nullopt;
    }

    std::optional<std::string> cause(const Fact& fact, std::optional<std::string_view> falls) const override {
        if (const auto* f = as<fact::Unguards>(fact)) {
            if (falls)
                return capitalize(
                    fmt::format("{} ne défend plus {}, qui tombe après {}.", piece(f->piece), piece(f->left), *falls));
            return capitalize(fmt::format("{} ne défend plus {}.", piece(f->piece), piece(f->left)));
        }
        if (const auto* f = as<fact::Neglects>(fact)) {
            if (falls)
                return capitalize(fmt::format("{}, déjà {}, reste en prise : {}.", piece(f->piece),
                                              agreed(f->piece, "attaqué"), *falls));
            return capitalize(fmt::format("{} était déjà {}.", piece(f->piece), agreed(f->piece, "attaqué")));
        }
        if (const auto* f = as<fact::OpensLine>(fact))
            return capitalize(fmt::format("Le coup ouvre la {} {}.", lineName(f->line), toPiece(f->slider)));
        return std::nullopt;
    }

    std::string costs(std::string_view sentence, const Fact& cost) const override {
        const auto* w = as<fact::WinsMaterial>(cost);
        if (!w) return std::string(sentence);
        return fmt::format("{}, ce qui coûte {}.", trimEndDots(sentence), won(*w));
    }

    std::string better(std::string_view mv, std::span<const Fact> why, bool chance) const override {
        for (const auto& f : why) {
            if (const auto* t = as<fact::TakesWith>(f))
                return fmt::format("Il fallait {} {} : {}.", t->back ? "reprendre" : "prendre", with(t->by.role), mv);
        }
        if (!why.empty()) {
            if (const auto* m = as<fact::Mate>(why.front())) {
                if (m->moves == 1) return fmt::format("Manque le mat : {}.", mv);
                return fmt::format("Manque un mat en {} coups par {}.", m->moves, mv);
            }
        }
        if (std::any_of(why.begin(), why.end(), [](const Fact& f) { return as<fact::Sacrifice>(f) != nullptr; }))
            return fmt::format("Il fallait oser {}, un sacrifice qui tient.", mv);
        const char* lead = chance ? "Manque" : "Mieux valait";
        const auto said  = phrases_detail::reasons(why, would, by);
        if (said.empty()) return fmt::format("{} {}.", lead, mv);
        if (said.size() == 1) return fmt::format("{} {}, qui {}.", lead, mv, said[0]);
        return fmt::format("{} {}, qui {} et {}.", lead, mv, said[0], said[1]);
    }

    std::string otherwise(std::string_view alt, std::string_view sentence) const override {
        // « 23.Rh1 perd une pièce… », but « Sur 23.Rh1, après 23…Dxf3+, la dame… ».
        const bool of_the_line = sentence.starts_with("Après ") || sentence.starts_with("Le ") ||
                                 sentence.starts_with("La ") || sentence.starts_with("Les ") ||
                                 sentence.starts_with("L'");
        if (of_the_line) return fmt::format("Sur {}, {}", alt, lowerFirst(sentence));
        return fmt::format("{} {}", alt, lowerFirst(sentence));
    }

    std::string onlyMove(double win_after, const Fact* why) const override {
        if (why) {
            const auto& fact = *why;
            if (const auto* f = as<fact::OnlyMoveToKeep>(fact)) {
                if (f->verdict == Verdict::Win) return "Seul coup gagnant selon les tables.";
                return "Seul coup qui annule selon les tables.";
            }
            if (const auto* f = as<fact::Retakes>(fact))
                return fmt::format("Seul coup : il faut reprendre en {}.", name(f->square));
            if (const auto* f = as<fact::Mate>(fact)) {
                if (f->moves == 1) return "Et mat.";
                return fmt::format("Seul coup : il mate en {}.", f->moves);
            }
            if (as<fact::PerpetualCheck>(fact)) return "Seul coup : il force l'échec perpétuel.";
            if (as<fact::Stalemate>(fact)) return "Seul coup : il sauve la partie par le pat.";
            if (as<fact::Zugzwang>(fact)) return "Seul coup : il met l'adversaire en zugzwang.";
            if (const auto* f = as<fact::Opposition>(fact))
                return fmt::format("Seul coup : le roi prend l'opposition{}.", f->distant ? " éloignée" : "");
            if (as<fact::Outruns>(fact)) return "Seul coup : le pion file, le roi adverse est hors du carré.";
            if (const auto* f = as<fact::EntersSquare>(fact))
                return fmt::format("Seul coup : le roi entre dans le carré {}.", ofPiece(f->pawn));
            if (const auto* f = as<fact::CutsOff>(fact))
                return fmt::format("Seul coup : la tour coupe le roi adverse le long de {}.",
                                   barrierWords(f->line, f->rook.square, Lang::Fr));
            if (as<fact::Luft>(fact)) return "Seul coup : il donne de l'air au roi.";
            if (const auto* f = as<fact::Saves>(fact)) {
                const auto [n, feminine] = noun(f->piece.role);
                return fmt::format("Seul coup : {} {}, attaqué{} en {}, se met à l'abri.", feminine ? "la" : "le", n,
                                   feminine ? "e" : "", name(f->piece.square));
            }
            if (const auto* f = as<fact::KeySquare>(fact))
                return fmt::format("Seul coup : le roi atteint une case clé {}.", ofPiece(f->pawn));
            if (as<fact::Lucena>(fact)) return "Seul coup : il atteint la position de Lucena.";
            if (const auto* f = as<fact::Philidor>(fact))
                return fmt::format("Seul coup : la défense de Philidor, la tour tient {}.",
                                   barrierWords(LineKind::Rank, f->rook.square, Lang::Fr));
            if (as<fact::WrongBishop>(fact))
                return "Seul coup : la finale est nulle, le fou ne contrôle pas la case de promotion.";
            if (const auto* f = as<fact::WinsMaterial>(fact)) return fmt::format("Seul coup, et il gagne {}.", won(*f));
        }
        static constexpr const char* standing[] = {"Seul coup pour garder l'avantage.",
                                                   "Seul coup pour maintenir l'équilibre.", "Seul coup pour résister."};
        return standing[phrases_detail::standing(win_after)];
    }

    std::optional<std::string> achieves(const Fact& fact) const override {
        if (const auto* f = as<fact::Mate>(fact)) {
            if (f->moves == 1) return "Et mat.";
            return capitalize(fmt::format("{} suit.", mate(f->moves, f->pattern)));
        }
        if (const auto* f = as<fact::WinsMaterial>(fact)) return fmt::format("Gagne {}.", won(*f));
        if (const auto* f = as<fact::Deflection>(fact); f && f->piece.color == f->guarded.color)
            return capitalize(fmt::format("{} est {} de la défense {}.", piece(f->piece), agreed(f->piece, "détourné"),
                                          ofPiece(f->guarded)));
        if (const auto* f = as<fact::Overloaded>(fact))
            return capitalize(fmt::format("{}, qui défendait à la fois {} et {}, est {}.", piece(f->piece),
                                          piece(f->first), second(f->piece, f->second), agreed(f->piece, "surchargé")));
        if (const auto* f = as<fact::Clearance>(fact)) {
            if (f->user.role == Role::Pawn) return fmt::format("Libère la case de promotion {}.", ofPiece(f->user));
            if (f->line) return fmt::format("Dégage la {} pour {}.", lineName(*f->line), piece(f->user));
            return fmt::format("Dégage la case {} pour {}.", name(f->piece.square), piece(f->user));
        }
        if (const auto* f = as<fact::Breakthrough>(fact))
            return fmt::format("Une percée : {} est offert pour faire passer {}.", piece(f->pawn), piece(f->passer));
        if (const auto* f = as<fact::Race>(fact))
            return f->check ? "Gagne la course à la promotion, avec échec." : "Gagne la course à la promotion.";
        if (const auto* f = as<fact::Attraction>(fact))
            return capitalize(fmt::format("{} est {} en {}.", nameOf(f->piece.role), agreed(f->piece, "attiré"),
                                          name(f->piece.square)));
        if (const auto* f = as<fact::RemovesDefender>(fact); f && f->defender.color == f->guarded.color)
            return fmt::format("Élimine {}, qui défendait {}.", piece(f->defender), piece(f->guarded));
        if (as<fact::Intermezzo>(fact)) return "Un coup intermédiaire avant de reprendre.";
        if (const auto* f = as<fact::Fork>(fact))
            return fmt::format("Fourchette : {} attaque {}.", piece(f->attacker), pieces(f->targets));
        if (const auto* f = as<fact::Skewer>(fact))
            return fmt::format("Enfilade : {} doit s'écarter et {} tombe.", piece(f->front), piece(f->behind));
        if (const auto* f = as<fact::DiscoveredAttack>(fact))
            return fmt::format("Attaque à la découverte sur {}.", piece(f->target));
        if (const auto* f = as<fact::Pin>(fact)) return fmt::format("Cloue {}.", piece(f->pinned));
        if (as<fact::DoubleCheck>(fact)) return "Échec double !";
        if (const auto* f = as<fact::Trapped>(fact))
            return capitalize(
                fmt::format("{} est {}.", piece(f->piece), noun(f->piece.role).second ? "enfermée" : "enfermé"));
        if (const auto* f = as<fact::Sacrifice>(fact))
            return fmt::format("Un sacrifice de {} qui tient.", noun(f->piece.role).first);
        if (as<fact::ThreatensMate>(fact)) return "Menace de mat.";
        if (as<fact::PerpetualCheck>(fact)) return "Force l'échec perpétuel.";
        if (as<fact::Stalemate>(fact)) return "Sauve la partie par le pat.";
        if (const auto* f = as<fact::Hanging>(fact))
            return fmt::format("Prend {} non défendu{}.", piece(f->piece), noun(f->piece.role).second ? "e" : "");
        if (const auto* f = as<fact::OnlyMoveToKeep>(fact)) {
            if (f->verdict == Verdict::Win) return "Le seul coup gagnant, selon les tables.";
            return "Le seul coup qui annule, selon les tables.";
        }
        if (as<fact::Zugzwang>(fact)) return "Met l'adversaire en zugzwang.";
        if (const auto* f = as<fact::Formation>(fact))
            return capitalize(fmt::format("{}.", formationSentence(f->kind, f->side, Lang::Fr)));
        if (const auto* f = as<fact::PawnBreak>(fact)) {
            if (f->lever && (f->lever->kind == Lever::Kind::ChainBase || f->lever->kind == Lever::Kind::ChainHead))
                return capitalize(fmt::format("{}, en {}.", leverWords(*f->lever, Lang::Fr), name(f->target.square)));
            if (f->lever) return capitalize(fmt::format("{}.", leverWords(*f->lever, Lang::Fr)));
        }
        if (as<fact::Space>(fact)) return "Prend plus d'espace.";
        if (as<fact::MinorityAttack>(fact)) return "Lance l'attaque de minorité.";
        if (as<fact::PawnStorm>(fact)) return "Lance les pions contre le roque adverse.";
        if (as<fact::Simplifies>(fact)) return "Simplifie avec du matériel en plus.";
        if (const auto* f = as<fact::Opposition>(fact))
            return f->distant ? "Prend l'opposition éloignée." : "Prend l'opposition.";
        if (const auto* f = as<fact::CutsOff>(fact))
            return fmt::format("Coupe le roi adverse le long de {}.", barrierWords(f->line, f->rook.square, Lang::Fr));
        if (const auto* f = as<fact::EntersSquare>(fact))
            return fmt::format("Le roi entre dans le carré {}.", ofPiece(f->pawn));
        if (const auto* f = as<fact::Majority>(fact))
            return fmt::format("Obtient la majorité de pions {}.", ofWing(f->wing, Lang::Fr));
        if (const auto* f = as<fact::KeySquare>(fact))
            return fmt::format("Le roi atteint une case clé {}.", ofPiece(f->pawn));
        if (as<fact::Lucena>(fact)) return "Atteint la position de Lucena.";
        if (const auto* f = as<fact::Philidor>(fact))
            return fmt::format("La défense de Philidor : la tour tient {}.",
                               barrierWords(LineKind::Rank, f->rook.square, Lang::Fr));
        if (as<fact::WrongBishop>(fact)) return "Finale nulle : le fou ne contrôle pas la case de promotion.";
        return std::nullopt;
    }
};

}  // namespace

const Phrasebook& frenchPhrasebook() {
    static const French book;
    return book;
}

std::string frenchMaterial(int32_t pawns, std::optional<Trade> trade) { return material(pawns, trade); }
const char* frenchKind(Kind k, uint8_t n) { return kind(k, n); }

}  // namespace fcg::commentary
