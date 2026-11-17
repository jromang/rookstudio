// The facts as plain statements for the prompt, and the pieces they name (the prototype's
// crates/commentary/src/brief.rs: fact_sentence and its helpers).

#include <fmt/include/fmt/format.h>

#include "brief.hpp"

namespace fcg::commentary::brief_detail {

namespace {

template <typename T>
const T* as(const Fact& f) {
    return std::get_if<T>(&f);
}

const char* englishRole(Role role) {
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

bool feminine(Role role) { return role == Role::Queen || role == Role::Rook; }

std::pair<const char*, const char*> frenchRole(Role role) {
    switch (role) {
        case Role::King:
            return {"le", "roi"};
        case Role::Queen:
            return {"la", "dame"};
        case Role::Rook:
            return {"la", "tour"};
        case Role::Bishop:
            return {"le", "fou"};
        case Role::Knight:
            return {"le", "cavalier"};
        default:
            return {"le", "pion"};
    }
}

// A piece by its colour and kind alone: "the black king", « le roi noir ».
std::string theKind(const Placed& p, Lang lang) {
    const auto the = thePiece(p, lang);
    const auto cut = the.rfind(lang == Lang::En ? " on " : " en ");
    return cut == std::string::npos ? the : the.substr(0, cut);
}

// « au fou blanc en c4 », « à la dame noire en d8 »: French contracts « à le ».
std::string toThePiece(const Placed& p, Lang lang) {
    const auto the = thePiece(p, lang);
    if (lang == Lang::En) return "for " + the;
    if (the.starts_with("le ")) return "au " + the.substr(3);
    return "à " + the;
}

// "a, b and c".
std::string enumerate(const std::vector<std::string>& items, std::string_view and_) {
    if (items.empty()) return {};
    if (items.size() == 1) return items.front();
    std::string out;
    for (std::size_t i = 0; i + 1 < items.size(); ++i) out += (i ? ", " : "") + items[i];
    return fmt::format("{} {} {}", out, and_, items.back());
}

// The endgame a line goes into, as a statement.
const char* endingSentence(Ending ending, Lang lang) {
    const bool en = lang == Lang::En;
    switch (ending) {
        case Ending::Queenless:
            return en ? "the queens come off" : "les dames sont échangées";
        case Ending::Pawn:
            return en ? "the game goes into a pawn endgame" : "la partie passe en finale de pions";
        case Ending::Rook:
            return en ? "the game goes into a rook endgame" : "la partie passe en finale de tours";
        case Ending::Queen:
            return en ? "the game goes into a queen endgame" : "la partie passe en finale de dames";
        case Ending::Minor:
            return en ? "the game goes into a minor-piece endgame" : "la partie passe en finale de pièces mineures";
        default:
            return en ? "the game goes into an endgame with bishops of opposite colours"
                      : "la partie passe en finale de fous de couleurs opposées";
    }
}

// "white", « blanc »: the colour as an adjective.
const char* colorWord(Color color, Lang lang) {
    if (lang == Lang::En) return color == Color::White ? "white" : "black";
    return color == Color::White ? "blanc" : "noir";
}

// Which way a standing leans, and how far from balance.
int side(Standing s) {
    const int d = static_cast<int>(s) - static_cast<int>(Standing::Balanced);
    return (d > 0) - (d < 0);
}
int distance(Standing s) {
    const int d = static_cast<int>(s) - static_cast<int>(Standing::Balanced);
    return d < 0 ? -d : d;
}

}  // namespace

// How a side stands, in words: the model is given words, no numbers to repeat.
const char* standingWords(Standing s, Lang lang) {
    const bool fr = lang == Lang::Fr;
    switch (s) {
        case Standing::Lost:
            return fr ? "perdue" : "lost";
        case Standing::ClearlyWorse:
            return fr ? "nettement moins bonne" : "clearly worse";
        case Standing::SlightlyWorse:
            return fr ? "un peu moins bonne" : "slightly worse";
        case Standing::Balanced:
            return fr ? "équilibrée" : "balanced";
        case Standing::SlightlyBetter:
            return fr ? "un peu meilleure" : "slightly better";
        case Standing::ClearlyBetter:
            return fr ? "nettement meilleure" : "clearly better";
        default:
            return fr ? "gagnante" : "winning";
    }
}

std::string thePiece(const Placed& p, Lang lang) {
    if (lang == Lang::En)
        return fmt::format("the {} {} on {}", p.color == Color::White ? "white" : "black", englishRole(p.role),
                           name(p.square));
    const auto [article, role] = frenchRole(p.role);
    const char* color =
        p.color == Color::White ? (feminine(p.role) ? "blanche" : "blanc") : (feminine(p.role) ? "noire" : "noir");
    return fmt::format("{} {} {} en {}", article, role, color, name(p.square));
}

std::string ofThePiece(const Placed& p, Lang lang) {
    const auto the = thePiece(p, lang);
    if (lang == Lang::En) return "of " + the;
    if (the.starts_with("le ")) return "du " + the.substr(3);
    return "de " + the;
}

std::string change(Standing before, Standing after, std::string_view movers, Lang lang) {
    const auto was = standingWords(before, lang), now = standingWords(after, lang);
    // "Still" only for an edge that shrinks on the same side: one that grows has changed.
    const bool still = side(after) == side(before) && distance(after) < distance(before);
    if (lang == Lang::Fr) {
        if (before == after) return fmt::format("La position {} reste {}.", movers, now);
        if (still) return fmt::format("La position {} reste {}, mais n'est plus {}.", movers, now, was);
        return fmt::format("La position {} était {} ; elle est maintenant {}.", movers, was, now);
    }
    if (before == after) return fmt::format("{} position stays {}.", movers, now);
    if (still) return fmt::format("{} position is still {}, no longer {}.", movers, now, was);
    return fmt::format("{} position was {} and is now {}.", movers, was, now);
}

std::optional<std::string> factSentence(const Fact& fact, Lang lang) {
    const bool fr     = lang == Lang::Fr;
    const auto the    = [&](const Placed& p) { return thePiece(p, lang); };
    const auto of_the = [&](const Placed& p) { return ofThePiece(p, lang); };
    // « enfermé », « enfermée »: the participle agrees with the piece.
    const auto agreed = [](const Placed& p, std::string_view word) {
        return feminine(p.role) ? std::string(word) + "e" : std::string(word);
    };
    const auto side_name = [&](Color c) { return sideName(c, lang); };

    if (const auto* f = as<fact::Mate>(fact)) {
        // The mate by its name, when it has one.
        if (f->pattern == MatePattern::BackRank)
            return fr ? fmt::format("mat du couloir en {}", f->moves) : fmt::format("back-rank mate in {}", f->moves);
        if (f->pattern == MatePattern::Smothered)
            return fr ? fmt::format("mat étouffé en {}", f->moves) : fmt::format("smothered mate in {}", f->moves);
        return fr ? fmt::format("mat en {}", f->moves) : fmt::format("mate in {}", f->moves);
    }
    if (const auto* f = as<fact::WinsMaterial>(fact)) {
        // The piece the gain comes down to, with what was given for it.
        const auto given = givenWords(f->trade, lang);
        std::string what;
        if (f->taken && given)
            what = (fr ? fmt::format("{} contre {}", the(*f->taken), *given)
                       : fmt::format("{} for {}", the(*f->taken), *given));
        else if (f->taken)
            what = the(*f->taken);
        else
            what = materialWords(f->pawns, f->trade, lang);
        return (fr ? fmt::format("gagne {}", what) : fmt::format("wins {}", what));
    }
    if (const auto* f = as<fact::Deflection>(fact)) {
        const bool own = f->piece.color == f->guarded.color;
        if (fr && own)
            return fmt::format("déviation : {} est {} de la défense {}", the(f->piece), agreed(f->piece, "détourné"),
                               of_the(f->guarded));
        if (fr)
            return fmt::format("déviation : {} est {} de la case {}", the(f->piece), agreed(f->piece, "détourné"),
                               name(f->guarded.square));
        if (own)
            return fmt::format("deflection: {} is drawn away from the defence {}", the(f->piece), of_the(f->guarded));
        return fmt::format("deflection: {} is drawn away from the {} square", the(f->piece), name(f->guarded.square));
    }
    if (const auto* f = as<fact::Overloaded>(fact)) {
        // A piece of its side, or the square an enemy piece then mates from.
        std::string other;
        if (f->piece.color == f->second.color)
            other = the(f->second);
        else
            other = (fr ? fmt::format("la case {}", name(f->second.square))
                        : fmt::format("the {} square", name(f->second.square)));
        if (fr) return fmt::format("surcharge : {} défendait à la fois {} et {}", the(f->piece), the(f->first), other);
        return fmt::format("overloaded piece: {} guarded both {} and {}", the(f->piece), the(f->first), other);
    }
    if (const auto* f = as<fact::Clearance>(fact)) {
        const char* line_fr = "sa case";
        const char* line_en = "its square";
        if (f->line == LineKind::File) line_fr = "la colonne", line_en = "the file";
        if (f->line == LineKind::Rank) line_fr = "la rangée", line_en = "the rank";
        if (f->line == LineKind::Diagonal) line_fr = "la diagonale", line_en = "the diagonal";
        const bool pawn = f->user.role == Role::Pawn;
        if (fr && pawn)
            return fmt::format("dégagement : {} libère la case de promotion {}", the(f->piece), of_the(f->user));
        if (fr) return fmt::format("dégagement : {} libère {} pour {}", the(f->piece), line_fr, the(f->user));
        if (pawn) return fmt::format("clearance: {} vacates the promotion square {}", the(f->piece), of_the(f->user));
        return fmt::format("clearance: {} clears {} for {}", the(f->piece), line_en, the(f->user));
    }
    if (const auto* f = as<fact::Breakthrough>(fact)) {
        if (fr) return fmt::format("percée : {} est offert pour faire passer {}", the(f->pawn), the(f->passer));
        return fmt::format("breakthrough: {} is offered so that {} gets through", the(f->pawn), the(f->passer));
    }
    if (const auto* f = as<fact::Race>(fact)) {
        const auto who = side_name(f->side);
        std::string said;
        if (fr && f->both)
            said = fmt::format("course à la promotion : les deux camps font dame, {} les premiers", who);
        else if (fr)
            said = fmt::format("course à la promotion : {} font dame les premiers", who);
        else if (f->both)
            said = fmt::format("race to promote: both sides queen, {} first", who);
        else
            said = fmt::format("race to promote: {} queens first", who);
        if (f->check) return said + (fr ? ", avec échec" : ", with check");
        return said;
    }
    if (const auto* f = as<fact::Attraction>(fact)) {
        if (fr)
            return fmt::format("attraction : {} est {} en {}", theKind(f->piece, lang), agreed(f->piece, "attiré"),
                               name(f->piece.square));
        return fmt::format("decoy: {} is lured to {}", theKind(f->piece, lang), name(f->piece.square));
    }
    if (const auto* f = as<fact::RemovesDefender>(fact)) {
        const bool own = f->defender.color == f->guarded.color;
        if (fr && own)
            return fmt::format("élimination du défenseur : {} défendait {}", the(f->defender), the(f->guarded));
        if (fr)
            return fmt::format("élimination du défenseur : {} défendait la case {}", the(f->defender),
                               name(f->guarded.square));
        if (own) return fmt::format("removing the defender: {} guarded {}", the(f->defender), the(f->guarded));
        return fmt::format("removing the defender: {} guarded the {} square", the(f->defender),
                           name(f->guarded.square));
    }
    if (as<fact::Intermezzo>(fact))
        return std::string(fr ? "coup intermédiaire avant de reprendre" : "an in-between move before taking back");
    if (const auto* f = as<fact::Unguards>(fact))
        return (fr ? fmt::format("{} ne défend plus {}", the(f->piece), the(f->left))
                   : fmt::format("{} no longer guards {}", the(f->piece), the(f->left)));
    if (const auto* f = as<fact::Neglects>(fact)) {
        if (fr) return fmt::format("{} était déjà {} avant le coup", the(f->piece), agreed(f->piece, "attaqué"));
        return fmt::format("{} was already attacked before the move", the(f->piece));
    }
    if (const auto* f = as<fact::OpensLine>(fact)) {
        const char* line_fr = f->line == LineKind::File   ? "colonne"
                              : f->line == LineKind::Rank ? "rangée"
                                                          : "diagonale";
        const char* line_en = f->line == LineKind::File ? "file" : f->line == LineKind::Rank ? "rank" : "diagonal";
        if (fr) return fmt::format("le coup ouvre la {} {}", line_fr, toThePiece(f->slider, lang));
        return fmt::format("the move opens the {} {}", line_en, toThePiece(f->slider, lang));
    }
    if (const auto* f = as<fact::Guards>(fact))
        return (fr ? fmt::format("défend {}", the(f->guarded)) : fmt::format("guards {}", the(f->guarded)));
    if (const auto* f = as<fact::Saves>(fact))
        return (fr ? fmt::format("met {} à l'abri", the(f->piece))
                   : fmt::format("moves {} out of danger", the(f->piece)));
    if (const auto* f = as<fact::Covers>(fact))
        return (fr ? fmt::format("contrôle la case {}", name(f->square))
                   : fmt::format("covers the {} square", name(f->square)));
    if (const auto* f = as<fact::Develops>(fact)) {
        if (fr) return fmt::format("{} est {}", the(f->piece), agreed(f->piece, "développé"));
        return fmt::format("{} is developed", the(f->piece));
    }
    if (const auto* f = as<fact::MovesAgain>(fact)) {
        if (fr) return fmt::format("{} rejoue alors que des pièces attendent leur développement", the(f->piece));
        return fmt::format("{} moves again while pieces wait to be developed", the(f->piece));
    }
    if (const auto* f = as<fact::Pressure>(fact)) {
        if (fr)
            return fmt::format("{} fait pression sur {} par la colonne semi-ouverte", the(f->piece), the(f->target));
        return fmt::format("{} presses on {} along the half-open file", the(f->piece), the(f->target));
    }
    if (const auto* f = as<fact::ProtectedPasser>(fact))
        return (fr ? fmt::format("{} est un pion passé protégé", the(f->pawn))
                   : fmt::format("{} is a protected passed pawn", the(f->pawn)));
    if (const auto* f = as<fact::ConnectedPassers>(fact))
        return (fr ? fmt::format("{} ont des pions passés liés", side_name(f->color))
                   : fmt::format("{} has connected passed pawns", side_name(f->color)));
    if (const auto* f = as<fact::OutsidePasser>(fact))
        return (fr ? fmt::format("{} est un pion passé éloigné", the(f->pawn))
                   : fmt::format("{} is an outside passed pawn", the(f->pawn)));
    if (const auto* f = as<fact::RookBehindPasser>(fact)) {
        if (fr) return fmt::format("{} soutient par derrière {}, passé", the(f->rook), the(f->pawn));
        return fmt::format("{} stands behind {}, a passed pawn", the(f->rook), the(f->pawn));
    }
    if (const auto* f = as<fact::Blockade>(fact))
        return (fr ? fmt::format("{} bloque {}", the(f->piece), the(f->pawn))
                   : fmt::format("{} blockades {}", the(f->piece), the(f->pawn)));
    if (const auto* f = as<fact::Simplifies>(fact)) {
        if (fr) return fmt::format("{} simplifient avec du matériel en plus", side_name(f->side));
        return fmt::format("{} simplifies while ahead in material", side_name(f->side));
    }
    if (const auto* f = as<fact::PawnStorm>(fact)) {
        if (fr) return fmt::format("{} lancent leurs pions contre le roque adverse", side_name(f->side));
        return fmt::format("{} storms the enemy king with pawns", side_name(f->side));
    }
    if (const auto* f = as<fact::MinorityAttack>(fact))
        return (fr ? fmt::format("{} lancent l'attaque de minorité", side_name(f->side))
                   : fmt::format("{} starts the minority attack", side_name(f->side)));
    if (const auto* f = as<fact::Retakes>(fact)) {
        if (fr) return fmt::format("reprend en {} ce qui venait d'être pris", name(f->square));
        return fmt::format("takes back on {} what was just taken", name(f->square));
    }
    if (const auto* f = as<fact::NotRetaken>(fact)) {
        if (fr) return fmt::format("le coup ne reprend pas en {}", name(f->square));
        return fmt::format("the move does not take back on {}", name(f->square));
    }
    if (const auto* f = as<fact::TakesWith>(fact)) {
        if (fr)
            return (f->back ? fmt::format("reprend avec {}", the(f->by)) : fmt::format("prend avec {}", the(f->by)));
        return (f->back ? fmt::format("takes back with {}", the(f->by)) : fmt::format("takes with {}", the(f->by)));
    }
    if (const auto* f = as<fact::Fork>(fact)) {
        std::vector<std::string> targets;
        for (const auto& t : f->targets) targets.push_back(the(t));
        if (fr) return fmt::format("fourchette : {} attaque {}", the(f->attacker), enumerate(targets, "et"));
        return fmt::format("fork: {} attacks {}", the(f->attacker), enumerate(targets, "and"));
    }
    if (const auto* f = as<fact::Pin>(fact)) {
        if (fr) return fmt::format("clouage {} devant {}", of_the(f->pinned), the(f->behind));
        return fmt::format("pin {} against {}", of_the(f->pinned), the(f->behind));
    }
    if (const auto* f = as<fact::Skewer>(fact)) {
        if (fr) return fmt::format("enfilade : {} puis {}", the(f->front), the(f->behind));
        return fmt::format("skewer: {} then {}", the(f->front), the(f->behind));
    }
    if (const auto* f = as<fact::DiscoveredAttack>(fact))
        return (fr ? fmt::format("attaque à la découverte sur {}", the(f->target))
                   : fmt::format("discovered attack on {}", the(f->target)));
    if (as<fact::DoubleCheck>(fact)) return std::string(fr ? "échec double" : "double check");
    if (const auto* f = as<fact::Hanging>(fact))
        return (fr ? fmt::format("{} est en prise", the(f->piece)) : fmt::format("{} is hanging", the(f->piece)));
    if (const auto* f = as<fact::Trapped>(fact)) {
        if (fr) return fmt::format("{} est {}", the(f->piece), agreed(f->piece, "enfermé"));
        return fmt::format("{} is trapped", the(f->piece));
    }
    if (const auto* f = as<fact::Sacrifice>(fact)) return fmt::format("sacrifice {}", of_the(f->piece));
    if (as<fact::ThreatensMate>(fact)) return std::string(fr ? "menace de mat" : "threat of mate");
    if (const auto* f = as<fact::ThreatensCapture>(fact))
        return (fr ? fmt::format("menace de prendre {}", the(f->target))
                   : fmt::format("threatens to take {}", the(f->target)));
    if (as<fact::SpoilsWin>(fact))
        return std::string(fr ? "les tables disent que le gain est gâché" : "the tablebases say the win is gone");
    if (as<fact::SpoilsDraw>(fact))
        return std::string(fr ? "les tables disent que la nulle est perdue" : "the tablebases say the draw is lost");
    if (as<fact::OnlyMoveToKeep>(fact))
        return std::string(fr ? "seul coup selon les tables" : "only move according to the tablebases");
    if (const auto* f = as<fact::PassedPawn>(fact))
        return (fr ? fmt::format("{} devient passé", the(f->pawn)) : fmt::format("{} becomes passed", the(f->pawn)));
    if (const auto* f = as<fact::IsolatedPawn>(fact)) {
        const bool d = fileOf(f->pawn.square) == File::D;
        if (fr && d) return fmt::format("{} devient un pion dame isolé", the(f->pawn));
        if (d) return fmt::format("{} becomes an isolated queen's pawn", the(f->pawn));
        if (fr) return fmt::format("{} se retrouve isolé", the(f->pawn));
        return fmt::format("{} is left isolated", the(f->pawn));
    }
    if (const auto* f = as<fact::DoubledPawns>(fact)) {
        const char file = letter(f->file);
        if (fr)
            return fmt::format("les pions {} sont doublés sur la colonne {}",
                               f->color == Color::White ? "blancs" : "noirs", file);
        return fmt::format("the {} pawns are doubled on the {}-file", f->color == Color::White ? "white" : "black",
                           file);
    }
    if (const auto* f = as<fact::BishopPair>(fact))
        return (fr ? fmt::format("{} obtiennent la paire de fous", side_name(f->color))
                   : fmt::format("{} gets the bishop pair", side_name(f->color)));
    if (const auto* f = as<fact::Castles>(fact)) {
        const bool king_side = f->wing == CastlingSide::KingSide;
        if (fr)
            return (king_side ? fmt::format("{} roquent côté roi", side_name(f->side))
                              : fmt::format("{} roquent côté dame", side_name(f->side)));
        return (king_side ? fmt::format("{} castles kingside", side_name(f->side))
                          : fmt::format("{} castles queenside", side_name(f->side)));
    }
    if (const auto* f = as<fact::Promotes>(fact)) {
        if (fr) return fmt::format("promotion en {} en {}", frenchRole(f->role).second, name(f->square));
        return fmt::format("promotion to a {} on {}", englishRole(f->role), name(f->square));
    }
    if (const auto* f = as<fact::RookOnOpenFile>(fact))
        return (fr ? fmt::format("{} tient la colonne ouverte", the(f->rook))
                   : fmt::format("{} holds the open file", the(f->rook)));
    if (const auto* f = as<fact::RookOnSeventh>(fact))
        return (fr ? fmt::format("{} atteint la septième rangée", the(f->rook))
                   : fmt::format("{} reaches the seventh rank", the(f->rook)));
    if (const auto* f = as<fact::Outpost>(fact))
        return (fr ? fmt::format("{} s'installe sur un avant-poste", the(f->knight))
                   : fmt::format("{} settles on an outpost", the(f->knight)));
    if (const auto* f = as<fact::ExposedKing>(fact))
        return (fr ? fmt::format("{} se retrouve sans abri", the(f->king))
                   : fmt::format("{} is left without pawn cover", the(f->king)));
    if (const auto* f = as<fact::LosesCastling>(fact))
        return (fr ? fmt::format("{} ne peuvent plus roquer", side_name(f->side))
                   : fmt::format("{} can no longer castle", side_name(f->side)));
    if (const auto* f = as<fact::PerpetualCheck>(fact))
        return (fr ? fmt::format("{} donnent l'échec perpétuel", side_name(f->side))
                   : fmt::format("{} gives perpetual check", side_name(f->side)));
    if (const auto* f = as<fact::Stalemate>(fact))
        return (fr ? fmt::format("{} sont pat", side_name(f->side))
                   : fmt::format("{} is stalemated", side_name(f->side)));
    if (const auto* f = as<fact::Endgame>(fact)) return std::string(endingSentence(f->ending, lang));
    if (const auto* f = as<fact::PawnBreak>(fact)) {
        // What the break means in the structure, when the books name it.
        if (f->lever) {
            const auto named = leverWords(*f->lever, lang);
            if (fr) return fmt::format("{} : {} attaque {}", named, the(f->pawn), the(f->target));
            return fmt::format("{}: {} strikes at {}", named, the(f->pawn), the(f->target));
        }
        if (fr) return fmt::format("rupture : {} attaque {}", the(f->pawn), the(f->target));
        return fmt::format("pawn break: {} strikes at {}", the(f->pawn), the(f->target));
    }
    if (const auto* f = as<fact::Space>(fact))
        return (fr ? fmt::format("{} ont plus d'espace", side_name(f->side))
                   : fmt::format("{} has more space", side_name(f->side)));
    if (const auto* f = as<fact::GainsSpace>(fact)) {
        const auto wing = wingWords(f->pawn.square, lang);
        return (fr ? fmt::format("{} gagne de l'espace {}", the(f->pawn), wing)
                   : fmt::format("{} gains space {}", the(f->pawn), wing));
    }
    if (const auto* f = as<fact::Formation>(fact)) return formationSentence(f->kind, f->side, lang);
    if (const auto* f = as<fact::Opposition>(fact)) {
        if (fr) return std::string(f->distant ? "le roi prend l'opposition éloignée" : "le roi prend l'opposition");
        return std::string(f->distant ? "the king takes the distant opposition" : "the king takes the opposition");
    }
    if (const auto* f = as<fact::Outruns>(fact)) {
        if (fr)
            return fmt::format("{} ne peut plus être rattrapé : le roi adverse est hors du carré du pion",
                               the(f->pawn));
        return fmt::format("{} can no longer be caught: the enemy king is outside the square of the pawn",
                           the(f->pawn));
    }
    if (const auto* f = as<fact::EntersSquare>(fact)) {
        if (fr) return fmt::format("{} entre dans le carré {}", theKind(f->king, lang), of_the(f->pawn));
        return fmt::format("{} steps into the square {}", theKind(f->king, lang), of_the(f->pawn));
    }
    if (const auto* f = as<fact::Majority>(fact)) {
        const auto wing = ofWing(f->wing, lang);
        if (fr) return fmt::format("{} obtiennent la majorité de pions {}", side_name(f->side), wing);
        return fmt::format("{} gets the pawn majority {}", side_name(f->side), wing);
    }
    if (const auto* f = as<fact::WeakColour>(fact)) {
        const auto squares = colourSquares(f->dark, lang);
        if (fr)
            return fmt::format("{} sont affaiblies chez {}, qui n'ont plus le fou de cette couleur", squares,
                               side_name(f->side));
        return fmt::format("{} are weak in {}'s camp, which has no bishop of that colour left", squares,
                           side_name(f->side));
    }
    if (as<fact::Luft>(fact)) {
        if (fr) return std::string("le coup donne de l'air au roi, que ses pions bloquaient sur sa première rangée");
        return std::string("the move gives the king luft: its own pawns shut it in on its first rank");
    }
    if (const auto* f = as<fact::KeySquare>(fact)) {
        if (fr)
            return fmt::format("{} atteint une case clé {} : le pion ira à promotion", theKind(f->king, lang),
                               of_the(f->pawn));
        return fmt::format("{} reaches a key square {}: the pawn will promote", theKind(f->king, lang),
                           of_the(f->pawn));
    }
    if (as<fact::Lucena>(fact)) {
        if (fr)
            return std::string(
                "position de Lucena : le roi est devant son pion en septième rangée, le roi adverse coupé");
        return std::string(
            "the Lucena position: the king stands in front of its pawn on the seventh rank, the enemy king cut off");
    }
    if (const auto* f = as<fact::Philidor>(fact)) {
        const auto barrier = barrierWords(LineKind::Rank, f->rook.square, lang);
        if (fr)
            return fmt::format("défense de Philidor : {} tient {} avant que le roi et le pion adverses n'y arrivent",
                               the(f->rook), barrier);
        return fmt::format("Philidor's defence: {} holds {} before the enemy king and pawn get there", the(f->rook),
                           barrier);
    }
    if (const auto* f = as<fact::WrongBishop>(fact)) {
        if (fr)
            return fmt::format(
                "finale nulle : {} ne contrôle pas la case de promotion {}, et le roi adverse tient le coin",
                the(f->bishop), of_the(f->pawn));
        return fmt::format(
            "a drawn endgame: {} does not control the promotion square {}, and the enemy king holds the corner",
            the(f->bishop), of_the(f->pawn));
    }
    if (const auto* f = as<fact::CutsOff>(fact)) {
        const auto barrier = barrierWords(f->line, f->rook.square, lang);
        if (fr)
            return fmt::format("{} coupe {} du pion passé, le long de {}", the(f->rook), theKind(f->king, lang),
                               barrier);
        return fmt::format("{} cuts {} off from the passed pawn, along {}", the(f->rook), theKind(f->king, lang),
                           barrier);
    }
    if (const auto* f = as<fact::DevelopmentLead>(fact))
        return (fr ? fmt::format("{} ont une avance de développement", side_name(f->side))
                   : fmt::format("{} leads in development", side_name(f->side)));
    if (const auto* f = as<fact::PawnCentre>(fact))
        return (fr ? fmt::format("{} ont un centre de pions", side_name(f->color))
                   : fmt::format("{} has a pawn centre", side_name(f->color)));
    if (const auto* f = as<fact::Compensation>(fact)) {
        std::string what;
        if (f->deficit)
            what = materialWords(f->pawns, f->deficit, lang);
        else
            what = fr ? "le matériel" : "the material";
        if (fr) return fmt::format("{} ont de la compensation pour {}", side_name(f->side), what);
        return fmt::format("{} has compensation for {}", side_name(f->side), what);
    }
    if (const auto* f = as<fact::Initiative>(fact))
        return (fr ? fmt::format("{} ont l'initiative", side_name(f->side))
                   : fmt::format("{} has the initiative", side_name(f->side)));
    if (const auto* f = as<fact::Counterplay>(fact))
        return (fr ? fmt::format("{} obtiennent du contre-jeu", side_name(f->side))
                   : fmt::format("{} gets counterplay", side_name(f->side)));
    if (const auto* f = as<fact::BackwardPawn>(fact))
        return (fr ? fmt::format("{} est arriéré", the(f->pawn)) : fmt::format("{} is backward", the(f->pawn)));
    if (const auto* f = as<fact::HangingPawns>(fact)) {
        const auto next_file = static_cast<int>(f->file) + 1;
        const char next      = next_file <= 7 ? letter(static_cast<File>(next_file)) : '?';
        const char left      = letter(f->file);
        const auto colour    = colorWord(f->color, lang);
        if (fr) return fmt::format("les pions {}s des colonnes {} et {} sont des pions pendants", colour, left, next);
        return fmt::format("the {} pawns on the {}- and {}-files are hanging pawns", colour, left, next);
    }
    if (const auto* f = as<fact::BadBishop>(fact)) {
        if (fr) return fmt::format("{} est un mauvais fou, gêné par ses propres pions", the(f->bishop));
        return fmt::format("{} is a bad bishop, hemmed in by its own pawns", the(f->bishop));
    }
    if (const auto* f = as<fact::ActiveKing>(fact))
        return (fr ? fmt::format("{} devient actif", the(f->king)) : fmt::format("{} becomes active", the(f->king)));
    if (const auto* f = as<fact::KingAttack>(fact)) {
        const auto king = colorWord(!f->side, lang);
        if (fr) return fmt::format("{} attaquent le roi {} avec plusieurs pièces", side_name(f->side), king);
        return fmt::format("{} attacks the {} king with several pieces", side_name(f->side), king);
    }
    if (const auto* f = as<fact::LongDiagonal>(fact))
        return (fr ? fmt::format("{} balaie la grande diagonale", the(f->bishop))
                   : fmt::format("{} sweeps the long diagonal", the(f->bishop)));
    if (as<fact::CentreOpens>(fact)) return std::string(fr ? "le centre s'ouvre" : "the centre opens");
    if (as<fact::CentreLocks>(fact)) return std::string(fr ? "le centre se bloque" : "the centre is locked");
    if (const auto* f = as<fact::MoreActive>(fact)) {
        if (fr)
            return std::string(f->side == Color::White ? "les pièces blanches sont plus actives"
                                                       : "les pièces noires sont plus actives");
        return fmt::format("the {} pieces are more active", colorWord(f->side, lang));
    }
    if (const auto* f = as<fact::Hole>(fact)) {
        if (fr) return fmt::format("la case {} devient faible chez {}", name(f->square), side_name(f->color));
        return fmt::format("the {} square becomes a hole in {}'s camp", name(f->square), side_name(f->color));
    }
    if (const auto* f = as<fact::Zugzwang>(fact)) {
        if (fr)
            return fmt::format("zugzwang : {}, au trait, ne peuvent que détériorer leur position", side_name(f->side));
        return fmt::format("zugzwang: {}, to move, can only make their position worse", side_name(f->side));
    }
    return std::nullopt;
}

}  // namespace fcg::commentary::brief_detail
