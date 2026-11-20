// The checker: what a model's comment may say, given the facts (the prototype's crates/commentary/src/brief.rs, from
// "The checker" on).

#include <algorithm>
#include <array>
#include <span>

#include <fmt/include/fmt/format.h>

#include "brief.hpp"
#include "text.hpp"

namespace fcg::commentary {

std::string debug(const Refusal& refusal) {
    static constexpr const char* NAMES[] = {
        "Notation", "Motif",   "Colour", "Piece", "Unsupported", "TooManyMoves", "Aside",    "Verdict", "Evaluation",
        "Elision",  "Grammar", "Empty",  "Name",  "Number",      "Board",        "Material", "Reason",  "Wording"};
    const auto* name                     = NAMES[static_cast<int>(refusal.kind)];
    switch (refusal.kind) {
        case Refusal::Kind::TooManyMoves:
        case Refusal::Kind::Grammar:
        case Refusal::Kind::Empty:
            return name;
        default:
            break;
    }
    std::string quoted;
    for (const char c : refusal.what) {
        if (c == '"' || c == '\\') quoted += '\\';
        quoted += c;
    }
    return fmt::format("{}(\"{}\")", name, quoted);
}

namespace brief_detail {

namespace {

template <typename... T>
bool is(const Fact& f) {
    return (std::holds_alternative<T>(f) || ...);
}

bool formationIs(const Fact& f, std::initializer_list<Formation> kinds) {
    const auto* formation = std::get_if<fact::Formation>(&f);
    return formation && std::find(kinds.begin(), kinds.end(), formation->kind) != kinds.end();
}

bool leverIs(const Fact& f, Lever::Kind kind, std::optional<Formation> freeing = std::nullopt) {
    const auto* brk = std::get_if<fact::PawnBreak>(&f);
    if (!brk || !brk->lever || brk->lever->kind != kind) return false;
    return !freeing || brk->lever->formation == *freeing;
}

Refusal refusal(Refusal::Kind kind, std::string what = {}) { return Refusal{kind, std::move(what)}; }

bool listed(std::string_view word, std::initializer_list<std::string_view> list) {
    return std::find(list.begin(), list.end(), word) != list.end();
}

bool in(std::string_view word, std::span<const std::string_view> list) {
    return std::find(list.begin(), list.end(), word) != list.end();
}

// Words of a comment that talks about its sources or its writer: "the facts give no assessment", "I'd note", « je ».
constexpr std::array<std::string_view, 13> ASIDES = {"facts",  "engine",  "engines", "i", "me",  "my", "faits",
                                                     "moteur", "moteurs", "je",      "j", "moi", "mon"};

// The line a pin, a skewer or a discovered attack works along: the file or the diagonal a comment may then name.
std::optional<LineKind> along(const Fact& fact) {
    if (const auto* f = std::get_if<fact::Pin>(&fact)) return lineKindOf(f->pinned.square, f->behind.square);
    if (const auto* f = std::get_if<fact::Skewer>(&fact)) return lineKindOf(f->front.square, f->behind.square);
    if (const auto* f = std::get_if<fact::DiscoveredAttack>(&fact))
        return lineKindOf(f->attacker.square, f->target.square);
    if (const auto* f = std::get_if<fact::Clearance>(&fact)) return f->line;
    return std::nullopt;
}

// Words naming a motif, in both languages, and the test for the facts that allow them. A word ending in `*` stands for
// every word it starts: "clou*" says « cloue », « cloué », « clouage ».
struct Motif {
    std::vector<std::string_view> words;
    bool (*supports)(const Fact&);
};

// Strategic notions, in both languages, and the facts that support them: a comment using one without them is making
// it up. Some no fact supports yet.
const std::vector<Motif>& notions() {
    static const std::vector<Motif> list = {
        {{"colonne*", "file", "files"},
         [](const Fact& f) {
             const auto* opens = std::get_if<fact::OpensLine>(&f);
             const auto* cuts  = std::get_if<fact::CutsOff>(&f);
             return along(f) == LineKind::File ||
                    is<fact::RookOnOpenFile, fact::DoubledPawns, fact::HangingPawns, fact::Pressure,
                       fact::RookBehindPasser>(f) ||
                    (opens && opens->line == LineKind::File) || (cuts && cuts->line == LineKind::File) ||
                    formationIs(f, {Formation::Carlsbad});
         }},
        {{"zugzwang*"}, [](const Fact& f) { return is<fact::Zugzwang>(f); }},
        {{"structure", "structur*"},
         [](const Fact& f) {
             return is<fact::IsolatedPawn, fact::DoubledPawns, fact::PassedPawn, fact::BackwardPawn, fact::HangingPawns,
                       fact::PawnCentre, fact::BadBishop, fact::ProtectedPasser, fact::ConnectedPassers,
                       fact::OutsidePasser, fact::Blockade, fact::Formation, fact::MinorityAttack>(f);
         }},
        {{"faiblesse*", "faible", "faibles", "affaibli*", "weak*"},
         [](const Fact& f) {
             return is<fact::IsolatedPawn, fact::DoubledPawns, fact::ExposedKing, fact::BackwardPawn,
                       fact::HangingPawns, fact::BadBishop, fact::Hole, fact::Pressure, fact::Blockade,
                       fact::WeakColour>(f) ||
                    formationIs(f, {Formation::Stonewall});
         }},
        {{"sécurité", "safety"}, [](const Fact& f) { return is<fact::ExposedKing, fact::LosesCastling>(f); }},
        {{"abri*", "shelter*"},
         [](const Fact& f) {
             return is<fact::ExposedKing, fact::LosesCastling, fact::Castles, fact::Mate, fact::ThreatensMate,
                       fact::Saves>(f);
         }},
        {{"centre", "centr*", "center*"},
         [](const Fact& f) {
             if (is<fact::PawnCentre, fact::ActiveKing, fact::CentreOpens, fact::CentreLocks, fact::Space>(f) ||
                 formationIs(f, {Formation::Benoni}))
                 return true;
             std::optional<Square> pawn;
             if (const auto* b = std::get_if<fact::PawnBreak>(&f)) pawn = b->pawn.square;
             if (const auto* g = std::get_if<fact::GainsSpace>(&f)) pawn = g->pawn.square;
             return pawn && (fileOf(*pawn) == File::D || fileOf(*pawn) == File::E);
         }},
        {{"initiative*"}, [](const Fact& f) { return is<fact::Initiative>(f); }},
        {{"contre-jeu*", "counterplay*", "counter-play*"}, [](const Fact& f) { return is<fact::Counterplay>(f); }},
        {{"compensation*"}, [](const Fact& f) { return is<fact::Compensation, fact::Sacrifice>(f); }},
        {{"développement*", "development*"},
         [](const Fact& f) { return is<fact::DevelopmentLead, fact::Develops, fact::MovesAgain>(f); }},
        {{"actif", "active", "activ*"},
         [](const Fact& f) {
             return is<fact::MoreActive, fact::ActiveKing, fact::Initiative, fact::Counterplay, fact::RookOnOpenFile,
                       fact::RookOnSeventh, fact::Outpost>(f);
         }},
        {{"passif", "passive", "passi*"}, [](const Fact& f) { return is<fact::MoreActive>(f); }},
        {{"diagonale", "diagonal*"},
         [](const Fact& f) {
             const auto* opens = std::get_if<fact::OpensLine>(&f);
             return along(f) == LineKind::Diagonal || is<fact::LongDiagonal>(f) ||
                    (opens && opens->line == LineKind::Diagonal);
         }},
        {{"espace", "espaces", "space"},
         [](const Fact& f) {
             return is<fact::Space, fact::GainsSpace>(f) ||
                    formationIs(f, {Formation::Maroczy, Formation::KingsideChain, Formation::QueensideChain});
         }},
        {{"étroit", "cramped"}, [](const Fact& f) { return is<fact::Space>(f); }},
        {{"complexe*", "complex"}, [](const Fact& f) { return is<fact::WeakColour>(f); }},
        {{"pression*", "pressure*"}, [](const Fact& f) { return is<fact::Pressure, fact::KingAttack>(f); }},
        {{"dynami*", "positionn*", "positional*", "stratég*", "strateg*"}, [](const Fact&) { return false; }},
    };
    return list;
}

// Motif words, in both languages, and the facts that allow them.
const std::vector<Motif>& motifs() {
    static const std::vector<Motif> list = {
        {{"fourchette*", "fork", "forks", "forked", "forking"}, [](const Fact& f) { return is<fact::Fork>(f); }},
        {{"clouage", "clou*", "pin", "pins", "pinned", "pinning"}, [](const Fact& f) { return is<fact::Pin>(f); }},
        {{"enfilade*", "skewer*"}, [](const Fact& f) { return is<fact::Skewer>(f); }},
        {{"découverte", "découvertes", "discovered", "discovery"},
         [](const Fact& f) { return is<fact::DiscoveredAttack>(f); }},
        {{"sacrifice", "sacrifi*", "sac", "sacs"}, [](const Fact& f) { return is<fact::Sacrifice>(f); }},
        {{"mat", "mats", "mater", "matent", "matait", "mate", "mates", "mated", "mating", "checkmate*"},
         [](const Fact& f) { return is<fact::Mate, fact::ThreatensMate>(f); }},
        {{"enfermé", "enferm*", "trap", "traps", "trapped", "trapping"},
         [](const Fact& f) { return is<fact::Trapped>(f); }},
        {{"isolé*", "isole", "isolent", "isolated", "isolani"},
         [](const Fact& f) { return is<fact::IsolatedPawn>(f) || leverIs(f, Lever::Kind::Isolani); }},
        {{"doublé*", "doubled"}, [](const Fact& f) { return is<fact::DoubledPawns>(f); }},
        {{"passé", "passés", "passée", "passées", "passed"},
         [](const Fact& f) {
             return is<fact::PassedPawn, fact::ProtectedPasser, fact::ConnectedPassers, fact::OutsidePasser,
                       fact::RookBehindPasser, fact::Blockade, fact::Outruns, fact::EntersSquare, fact::CutsOff,
                       fact::Majority, fact::Promotes, fact::Breakthrough, fact::Race>(f);
         }},
        {{"avant-poste*", "outpost*"}, [](const Fact& f) { return is<fact::Outpost>(f); }},
        {{"septième", "seventh"},
         [](const Fact& f) {
             const auto* cuts = std::get_if<fact::CutsOff>(&f);
             return is<fact::RookOnSeventh, fact::Lucena>(f) || (cuts && cuts->line == LineKind::Rank);
         }},
        {{"paire", "pair"}, [](const Fact& f) { return is<fact::BishopPair>(f); }},
        {{"roque", "roqu*", "castl*"},
         [](const Fact& f) { return is<fact::Castles, fact::LosesCastling, fact::ExposedKing, fact::PawnStorm>(f); }},
        {{"promotion", "promot*", "promeut", "promouv*", "promu", "promue", "promus"},
         [](const Fact& f) {
             return is<fact::Promotes, fact::KeySquare, fact::WrongBishop, fact::Outruns, fact::EntersSquare,
                       fact::Race, fact::Clearance>(f);
         }},
        {{"perpétuel*", "perpetual*", "répétition*", "repetition*"},
         [](const Fact& f) { return is<fact::PerpetualCheck>(f); }},
        {{"pat", "stalemate*"}, [](const Fact& f) { return is<fact::Stalemate>(f); }},
        {{"rupture*"},
         [](const Fact& f) {
             return is<fact::PawnBreak>(f) || formationIs(f, {Formation::Maroczy, Formation::Hedgehog});
         }},
        {{"trou", "trous", "hole", "holes"}, [](const Fact& f) { return is<fact::Hole>(f); }},
        {{"arriéré*", "backward"}, [](const Fact& f) { return is<fact::BackwardPawn>(f); }},
        // An overloaded piece is drawn away from the second thing it guarded.
        {{"déviation*", "dévi*", "détourn*", "deflect*"},
         [](const Fact& f) { return is<fact::Deflection, fact::Overloaded>(f); }},
        {{"attraction*", "attir*", "lure", "lures", "lured", "luring", "decoy*"},
         [](const Fact& f) { return is<fact::Attraction>(f); }},
        {{"défenseur*", "defender*"},
         [](const Fact& f) {
             return is<fact::RemovesDefender, fact::Deflection, fact::Overloaded, fact::Unguards, fact::Guards>(f);
         }},
        {{"intermédiaire*", "zwischenzug*", "intermezzo*"}, [](const Fact& f) { return is<fact::Intermezzo>(f); }},
        {{"surcharge", "surcharg*", "overload*", "overwork*"}, [](const Fact& f) { return is<fact::Overloaded>(f); }},
        {{"dégagement", "dégag*", "clearance*", "clears", "cleared", "clearing"},
         [](const Fact& f) { return is<fact::Clearance>(f); }},
        {{"percée*", "breakthrough*"}, [](const Fact& f) { return is<fact::Breakthrough>(f); }},
        {{"course", "courses", "race", "races", "racing"}, [](const Fact& f) { return is<fact::Race>(f); }},
        {{"minorité", "minority", "carlsbad"},
         [](const Fact& f) { return is<fact::MinorityAttack>(f) || formationIs(f, {Formation::Carlsbad}); }},
        {{"maroczy", "maróczy", "étau", "bind"},
         [](const Fact& f) {
             return formationIs(f, {Formation::Maroczy}) || leverIs(f, Lever::Kind::Freeing, Formation::Maroczy);
         }},
        {{"hérisson", "hedgehog"},
         [](const Fact& f) {
             return formationIs(f, {Formation::Hedgehog}) || leverIs(f, Lever::Kind::Freeing, Formation::Hedgehog);
         }},
        {{"stonewall"}, [](const Fact& f) { return formationIs(f, {Formation::Stonewall}); }},
        {{"chaîne*", "chain", "chains"},
         [](const Fact& f) {
             return formationIs(f, {Formation::KingsideChain, Formation::QueensideChain}) ||
                    leverIs(f, Lever::Kind::ChainBase) || leverIs(f, Lever::Kind::ChainHead);
         }},
        {{"benoni"}, [](const Fact& f) { return formationIs(f, {Formation::Benoni}); }},
        {{"majorité*", "majority", "majorities"},
         [](const Fact& f) { return is<fact::Majority>(f) || formationIs(f, {Formation::Benoni}); }},
        {{"air", "luft"}, [](const Fact& f) { return is<fact::Luft>(f); }},
        {{"lucena"}, [](const Fact& f) { return is<fact::Lucena>(f); }},
        {{"couloir"},
         [](const Fact& f) {
             const auto* mate = std::get_if<fact::Mate>(&f);
             return (mate && mate->pattern == MatePattern::BackRank) || is<fact::Luft>(f);
         }},
        {{"étouff*", "smother*"},
         [](const Fact& f) {
             const auto* mate = std::get_if<fact::Mate>(&f);
             return mate && mate->pattern == MatePattern::Smothered;
         }},
        {{"philidor"}, [](const Fact& f) { return is<fact::Philidor>(f); }},
        {{"opposition*"}, [](const Fact& f) { return is<fact::Opposition>(f); }},
        {{"carré"}, [](const Fact& f) { return is<fact::Outruns, fact::EntersSquare>(f); }},
        {{"coupe", "coupé*", "coupent", "coupait", "cut", "cuts", "cutting"},
         [](const Fact& f) { return is<fact::CutsOff, fact::Lucena>(f); }},
        {{"blocus", "blockad*"}, [](const Fact& f) { return is<fact::Blockade>(f); }},
        {{"simplif*"}, [](const Fact& f) { return is<fact::Simplifies, fact::Endgame>(f); }},
        {{"assaut*", "storm*"}, [](const Fact& f) { return is<fact::PawnStorm>(f); }},
    };
    return list;
}

// Whether `word` is one of `list`, or starts one of its stems.
bool says(std::string_view word, const std::vector<std::string_view>& list) {
    return std::any_of(list.begin(), list.end(), [&](std::string_view l) {
        if (l.ends_with('*')) return word.starts_with(l.substr(0, l.size() - 1));
        return word == l;
    });
}

std::string_view stem(std::string_view word) { return word.ends_with('*') ? word.substr(0, word.size() - 1) : word; }

// After one item at the start of `text`: an allowed move or square (the longest), else a word as the grammar spells
// one.
std::optional<std::string_view> grammarItem(std::string_view text, const Allowed& allowed) {
    const std::string* token = nullptr;
    for (const auto* list : {&allowed.moves, &allowed.squares, &allowed.names}) {
        for (const auto& t : *list) {
            // max_by_key keeps the last of the longest.
            if (text.starts_with(t) && (!token || t.size() >= token->size())) token = &t;
        }
    }
    if (token) return text.substr(token->size());
    const auto lower = [](char32_t c) {
        return (text::isLowercase(c) && text::isAlphabetic(c)) || text::holds("'’-", c);
    };
    const auto end_of = [&](std::size_t from) {
        for (std::size_t at = from; at < text.size();) {
            const auto [c, n] = text::decode(text, at);
            if (!lower(c)) return at;
            at += n;
        }
        return text.size();
    };
    if (text.empty()) return std::nullopt;
    const auto [first, first_n] = text::decode(text);
    std::size_t end;
    if (lower(first)) {
        end = end_of(0);
    } else if (text::holds("ABCDEFGHIJKLMNOPQRSTUVWXYZÀÂÇÉÈÊÎÔÛŒ", first)) {
        // A capital, then optionally letters, the first of them an "x" only before three more: no word reads like a
        // capture such as "Nxe", but "Exchange" is a word.
        const std::size_t at = first_n;
        if (at >= text.size()) {
            end = text.size();
        } else {
            const auto [second, second_n] = text::decode(text, at);
            if (lower(second)) {
                const auto word_end = end_of(at);
                end = second != U'x' || text::length(text.substr(at + 1, word_end - at - 1)) >= 3 ? word_end : at;
            } else {
                end = at;
            }
        }
    } else {
        return std::nullopt;
    }
    return text.substr(end);
}

// "f", "cxc", "bx": what is left of a square or a pawn capture without its rank.
bool isCutNotation(std::string_view word) {
    const auto file = [](char c) { return c >= 'a' && c <= 'h'; };
    // "a" is an English word.
    if (word.size() == 1) return word[0] != 'a' && file(word[0]);
    if (word.size() >= 2 && word[1] == 'x') {
        const auto rest = word.substr(2);
        return file(word[0]) && rest.size() <= 1 && std::all_of(rest.begin(), rest.end(), file);
    }
    return false;
}

// The verdict a word names, if it names one. « Grosse erreur » is a blunder, « erreur » alone a mistake.
std::optional<std::pair<Judgment, const char*>> verdictOf(std::string_view word, const std::string* next) {
    if (word == "grosse" && next && *next == "erreur") return std::pair{Judgment::Blunder, "grosse erreur"};
    if (listed(word, {"blunder", "blunders", "gaffe", "gaffes"})) return std::pair{Judgment::Blunder, "blunder"};
    if (listed(word, {"mistake", "mistakes", "error", "errors", "erreur", "erreurs"}))
        return std::pair{Judgment::Mistake, "mistake"};
    if (listed(word, {"inaccuracy", "inaccuracies", "inaccurate", "imprecise", "imprécision", "imprécisions",
                      "imprécis", "imprécise"}))
        return std::pair{Judgment::Inaccuracy, "inaccuracy"};
    return std::nullopt;
}

bool isSquare(std::string_view w) { return w.size() == 2 && w[0] >= 'a' && w[0] <= 'h' && w[1] >= '1' && w[1] <= '8'; }

std::optional<Square> parseSquare(std::string_view w) {
    if (!isSquare(w)) return std::nullopt;
    return square(static_cast<File>(w[0] - 'a'), static_cast<Rank>(w[1] - '1'));
}

std::vector<std::string> lowered(const std::vector<std::string_view>& words) {
    std::vector<std::string> out;
    out.reserve(words.size());
    for (const auto w : words) out.push_back(text::toLower(w));
    return out;
}

}  // namespace

std::vector<std::string> wordsOf(std::string_view text) {
    std::vector<std::string> out;
    for (auto piece :
         text::split(text, [](char32_t c) { return text::isWhitespace(c) || text::holds(",;:!?()'’", c); })) {
        while (!piece.empty() && piece.back() == '.') piece.remove_suffix(1);
        auto w = text::toLower(piece);
        if (w.empty()) continue;
        const bool compound = w.find('-') != std::string::npos;
        out.push_back(w);
        if (compound) {
            for (const auto part : text::split(w, [](char32_t c) { return c == U'-'; })) out.emplace_back(part);
        }
    }
    return out;
}

bool fitsGrammar(std::string_view text, const Allowed& allowed, Lang lang) {
    static constexpr std::array<std::string_view, 4> SEPARATORS_EN = {", ", ": ", "; ", " "};
    static constexpr std::array<std::string_view, 4> SEPARATORS_FR = {", ", " : ", " ; ", " "};
    static constexpr std::array<std::string_view, 2> ENDS_EN       = {".", "!"};
    static constexpr std::array<std::string_view, 2> ENDS_FR       = {".", " !"};
    const auto& separators                                         = lang == Lang::En ? SEPARATORS_EN : SEPARATORS_FR;
    const auto& ends                                               = lang == Lang::En ? ENDS_EN : ENDS_FR;
    const auto strip                                               = [](std::string_view& rest, const auto& list) {
        for (const auto p : list) {
            if (rest.starts_with(p)) {
                rest.remove_prefix(p.size());
                return true;
            }
        }
        return false;
    };
    auto rest = text;
    // Sentences in a comment, at most: how the move fails, what the answer does, what was better.
    for (int phrase = 0; phrase < 3; ++phrase) {
        if (phrase > 0) {
            if (!rest.starts_with(' ')) break;
            rest.remove_prefix(1);
        }
        for (;;) {
            const auto next = grammarItem(rest, allowed);
            if (!next) return false;
            rest = *next;
            if (strip(rest, ends)) break;
            if (!strip(rest, separators)) return false;
        }
    }
    return rest.empty();
}

std::optional<Refusal> checkOnly(std::string_view text, bool only) {
    if (only) return std::nullopt;
    // What "only" and « seul » may call the only one when the facts make a move so: "the only way", "only good move",
    // « seul moyen », « la seule défense ».
    static constexpr std::string_view ONLY_EN[] = {"way",    "move",  "defence", "defense",  "try", "option",
                                                   "chance", "reply", "answer",  "resource", "path"};
    static constexpr std::string_view ONLY_FR[] = {"moyen",   "coup",   "façon",     "défense", "manière",
                                                   "réponse", "chance", "ressource", "option",  "essai"};
    const auto words                            = wordsOf(text);
    for (std::size_t i = 0; i < words.size(); ++i) {
        const char* said = nullptr;
        std::span<const std::string_view> nouns;
        if (words[i] == "only") {
            said  = "only move";
            nouns = ONLY_EN;
        } else if (words[i] == "seul" || words[i] == "seule") {
            said  = "seul coup";
            nouns = ONLY_FR;
        } else {
            continue;
        }
        // The noun follows at once or after a qualifier: "only good move", « seul bon coup ».
        for (std::size_t j = i + 1; j < words.size() && j <= i + 2; ++j) {
            if (in(words[j], nouns)) return refusal(Refusal::Kind::Verdict, said);
        }
    }
    return std::nullopt;
}

std::optional<Refusal> checkRetreat(std::string_view text, const std::optional<std::string>& forward_to) {
    if (!forward_to) return std::nullopt;
    static constexpr std::string_view RETREAT[]    = {"retreat", "retreats", "retreated", "retreating", "recule",
                                                      "reculer", "recul",    "repli",     "replie"};
    static constexpr std::string_view RETREATING[] = {"bishop", "knight",   "rook", "queen", "king",
                                                      "fou",    "cavalier", "tour", "dame",  "roi"};
    const auto words                               = wordsOf(text);
    for (std::size_t i = 0; i < words.size(); ++i) {
        const auto& word = words[i];
        if (!in(word, RETREAT)) continue;
        bool to_there = false;
        for (std::size_t j = i + 1; j < words.size() && j <= i + 3; ++j) to_there = to_there || words[j] == *forward_to;
        bool noun_of_piece = false;
        if (word == "retreat") noun_of_piece = i > 0 && in(words[i - 1], RETREATING);
        if (word == "recul" || word == "repli")
            noun_of_piece =
                i + 1 < words.size() && words[i + 1] == "du" && i + 2 < words.size() && in(words[i + 2], RETREATING);
        if (to_there || noun_of_piece)
            return refusal(Refusal::Kind::Unsupported, word.starts_with("retreat") ? "retreat" : "recul");
    }
    return std::nullopt;
}

std::optional<Refusal> checkCentre(std::string_view text, std::span<const Fact* const> present) {
    const bool opens =
        std::any_of(present.begin(), present.end(), [](const Fact* f) { return is<fact::CentreOpens>(*f); });
    const bool locks =
        std::any_of(present.begin(), present.end(), [](const Fact* f) { return is<fact::CentreLocks>(*f); });
    if (opens == locks) return std::nullopt;
    static constexpr std::string_view OPENS[] = {"open",   "opens",   "opened", "ouvre",   "ouvrent",
                                                 "ouvrir", "ouvrant", "ouvert", "ouverte", "ouvrait"};
    static constexpr std::string_view SHUTS[] = {
        "lock",    "locks",  "locked",   "locking", "block",  "blocks",  "blocked", "blocking", "closes", "closed",
        "closing", "bloque", "bloquent", "bloquer", "bloqué", "bloquée", "ferme",   "fermé",    "fermée"};
    // Words that turn such a claim around: "without opening", « ne s'ouvre pas ».
    static constexpr std::string_view NEGATIONS[] = {"not",  "never", "without", "doesn", "didn",
                                                     "sans", "ne",    "n",       "pas",   "jamais"};
    const auto words                              = wordsOf(text);
    for (std::size_t i = 0; i < words.size(); ++i) {
        const auto& word   = words[i];
        const bool opening = in(word, OPENS);
        if ((!opening && !in(word, SHUTS)) || opening == opens) continue;
        // The centre after the verb or just before it: "opens the centre", "the centre opens".
        const std::size_t from = i >= 2 ? i - 2 : 0, to = std::min(words.size(), i + 4);
        bool centre = false, negated = false;
        for (std::size_t j = from; j < to; ++j) {
            if (j == i) continue;
            centre = centre || words[j] == "centre" || words[j] == "center";
        }
        for (std::size_t j = from; j < i; ++j) negated = negated || in(words[j], NEGATIONS);
        if (i + 1 < to) negated = negated || in(words[i + 1], NEGATIONS);
        if (centre && !negated) {
            const bool fr = word.starts_with("ouv") || word.starts_with("bloqu") || word.starts_with("ferm");
            return refusal(Refusal::Kind::Unsupported, opening ? (fr ? "ouvre le centre" : "opens the centre")
                                                               : (fr ? "bloque le centre" : "locks the centre"));
        }
    }
    return std::nullopt;
}

std::optional<Refusal> checkFiller(std::string_view text) {
    // Words that prepare nothing in particular; and a verb French chess writing does not have, for « faire une
    // fourchette ».
    static constexpr std::array<std::string_view, 13> FILLERS = {"fourche",
                                                                 "fourchent",
                                                                 "fourchait",
                                                                 "fourcher",
                                                                 "fourché",
                                                                 "prépare la suite",
                                                                 "préparent la suite",
                                                                 "prépare le terrain",
                                                                 "préparent le terrain",
                                                                 "prepares the ground",
                                                                 "prepare the ground",
                                                                 "prepares what follows",
                                                                 "prepare what follows"};
    std::string plain                                         = " ";
    const auto words                                          = wordsOf(text);
    for (std::size_t i = 0; i < words.size(); ++i) plain += (i ? " " : "") + words[i];
    plain += ' ';
    for (const auto filler : FILLERS) {
        if (plain.find(fmt::format(" {} ", filler)) != std::string::npos)
            return refusal(Refusal::Kind::Unsupported, std::string(filler));
    }
    return std::nullopt;
}

std::optional<Refusal> checkStanding(std::string_view text, std::optional<std::pair<Standing, Standing>> standing) {
    static constexpr std::string_view BALANCE[]   = {"équilibre", "équilibré", "équilibrée", "équilibrées", "égal",
                                                     "égale",     "égalité",   "balance",    "balanced",    "equal",
                                                     "equality",  "level",     "parity"};
    static constexpr std::string_view ADVANTAGE[] = {"avantage", "advantage", "edge", "upper"};
    static constexpr std::string_view KEEPS[]     = {
        "keep",       "keeps",     "kept",    "keeping",  "maintain",   "maintains", "maintained",
        "hold",       "holds",     "held",    "preserve", "preserves",  "garde",     "gardait",
        "garder",     "gardé",     "gardant", "conserve", "conservait", "conserver", "maintient",
        "maintenait", "maintenir", "tient",   "tenait",   "tenir"};
    static constexpr std::string_view LOSES[] = {"lose",    "loses",  "lost",    "losing", "throws",   "squanders",
                                                 "slip",    "slips",  "slipped", "gone",   "perd",     "perdent",
                                                 "perdait", "perdre", "perdu",   "filer",  "échapper", "échappe"};
    const auto words                          = wordsOf(text);
    const auto near                           = [&](std::size_t i, std::span<const std::string_view> verbs) {
        const std::size_t from = i >= 4 ? i - 4 : 0, to = std::min(words.size(), i + 5);
        for (std::size_t j = from; j < to; ++j) {
            if (in(words[j], verbs)) return true;
        }
        return false;
    };
    for (std::size_t i = 0; i < words.size(); ++i) {
        const bool balance = in(words[i], BALANCE), advantage = in(words[i], ADVANTAGE);
        if (!balance && !advantage) continue;
        if (!standing) return refusal(Refusal::Kind::Evaluation, words[i]);
        const auto [before, after] = *standing;
        const auto even            = Standing::Balanced;
        const bool agrees =
            balance ? (before == even || after == even) && !(near(i, KEEPS) && before != even)
                    : !(near(i, KEEPS) && before <= even) && !(near(i, LOSES) && !(before > even && after <= even));
        if (!agrees) return refusal(Refusal::Kind::Evaluation, words[i]);
    }
    return std::nullopt;
}

std::optional<Refusal> checkElision(std::string_view text) {
    // Little words French elides before a vowel: « l'échec », « d'un », « qu'il ».
    static constexpr std::string_view ELIDED[] = {"le", "la", "de", "du", "que", "ne", "se", "je", "me", "te", "ce"};
    std::vector<std::string_view> words;
    for (const auto w : text::splitWhitespace(text)) words.push_back(text::trimMatches(w, ",;:!?.()«»"));
    for (std::size_t i = 0; i + 1 < words.size(); ++i) {
        const auto first  = text::toLower(words[i]);
        const auto next   = words[i + 1];
        const bool vowel  = !next.empty() && text::holds("aeiouéèêàâîïôû", text::decode(next).first);
        const bool mute_h = next.starts_with("habit") || next.starts_with("homme");
        bool word         = text::length(next) > 1;
        for (std::size_t at = 0; word && at < next.size();) {
            const auto [c, n] = text::decode(next, at);
            word              = text::isAlphabetic(c);
            at += n;
        }
        if (in(first, ELIDED) && word && (vowel || mute_h))
            return refusal(Refusal::Kind::Elision, fmt::format("{} {}", words[i], next));
    }
    return std::nullopt;
}

std::optional<Refusal> checkVerdict(std::string_view text, std::optional<Judgment> judgment) {
    std::vector<std::string> words;
    for (const auto piece :
         text::split(text, [](char32_t c) { return text::isWhitespace(c) || text::holds(",;:!?()'’.", c); })) {
        if (!piece.empty()) words.push_back(text::toLower(piece));
    }
    for (std::size_t i = 0; i < words.size();) {
        const auto* next = i + 1 < words.size() ? &words[i + 1] : nullptr;
        if (const auto named = verdictOf(words[i], next)) {
            if (named->first != judgment) return refusal(Refusal::Kind::Verdict, named->second);
            // « grosse erreur » is one verdict, not a blunder and a mistake.
            i += std::string_view(named->second) == "grosse erreur" ? 2 : 1;
        } else {
            ++i;
        }
    }
    return std::nullopt;
}

std::vector<PlacedIn> placedIn(std::string_view text) {
    std::vector<std::string> words;
    for (const auto w : text::splitWhitespace(text))
        words.push_back(text::toLower(text::trimMatches(w, ",;:!?.()«»“”\"")));
    const auto role = [](const std::string* w) -> std::optional<Role> {
        if (!w) return std::nullopt;
        if (*w == "roi" || *w == "king") return Role::King;
        if (*w == "dame" || *w == "queen") return Role::Queen;
        if (*w == "tour" || *w == "rook") return Role::Rook;
        if (*w == "fou" || *w == "bishop") return Role::Bishop;
        if (*w == "cavalier" || *w == "knight") return Role::Knight;
        if (*w == "pion" || *w == "pawn") return Role::Pawn;
        return std::nullopt;
    };
    const auto colour_of = [](const std::string* w) -> std::optional<Color> {
        if (!w) return std::nullopt;
        if (*w == "blanc" || *w == "blanche" || *w == "white") return Color::White;
        if (*w == "noir" || *w == "noire" || *w == "black") return Color::Black;
        return std::nullopt;
    };
    const auto at        = [&](std::size_t i) -> const std::string* { return i < words.size() ? &words[i] : nullptr; };
    const auto square_of = [](const std::string* w) -> std::optional<Square> {
        return w ? parseSquare(*w) : std::nullopt;
    };
    const auto joined = [&](std::size_t from, std::size_t to) {
        std::string out;
        for (std::size_t j = from; j <= to; ++j) out += (j > from ? " " : "") + words[j];
        return out;
    };
    std::vector<PlacedIn> placed;
    for (std::size_t i = 0; i < words.size(); ++i) {
        const auto* before = i > 0 ? &words[i - 1] : nullptr;
        const bool verb    = words[i] == "queen" && before && *before == "to";
        if (const auto r = role(&words[i]); r && !verb) {
            // English gives the colour first, French after the piece.
            auto colour      = colour_of(before);
            std::size_t next = i + 1;
            if (const auto after = colour_of(at(next))) {
                colour = after;
                ++next;
            }
            if (const auto* w = at(next); w && (*w == "on" || *w == "in" || *w == "at" || *w == "en")) ++next;
            if (const auto s = square_of(at(next))) placed.push_back({*r, colour, *s, joined(i, next)});
        } else if (const auto s = square_of(&words[i])) {
            if (const auto r2 = role(at(i + 1))) placed.push_back({*r2, colour_of(before), *s, joined(i, i + 1)});
        }
    }
    std::erase_if(placed, [](const PlacedIn& p) {
        return p.role == Role::Pawn && (rankOf(p.square) == Rank::First || rankOf(p.square) == Rank::Eighth);
    });
    return placed;
}

std::optional<Refusal> check(std::string_view text, const MoveFacts& facts, const Allowed& allowed, Color mover) {
    if (text::trim(text).empty()) return refusal(Refusal::Kind::Empty);
    std::vector<std::string> words;
    for (auto& w : wordsOf(text)) {
        if (std::find(allowed.name_words.begin(), allowed.name_words.end(), w) == allowed.name_words.end())
            words.push_back(std::move(w));
    }
    const auto contains = [](const std::vector<std::string>& list, std::string_view w) {
        return std::find(list.begin(), list.end(), w) != list.end();
    };
    const auto spaced = text::splitWhitespace(text);
    // Besides the move commented, which a comment naturally names. A bare square is a square, though a pawn's move is
    // written the same: "attacked on g5" quotes no move.
    std::size_t quoted = 0;
    for (const auto raw : spaced) {
        const auto w = text::trimEndMatches(raw, ".,;!");
        if (!isSquare(w) && contains(allowed.moves, w) && !contains(allowed.played, w)) ++quoted;
    }
    if (quoted > 2) return refusal(Refusal::Kind::TooManyMoves);
    // A square or a capture cut short where its digit was not allowed: « en f », "cxc". A file is named by its letter,
    // though: « la colonne h ».
    std::vector<std::string_view> trimmed;
    for (const auto raw : spaced) trimmed.push_back(text::trimMatches(raw, ",;:!?()."));
    const auto bare = lowered(trimmed);
    for (std::size_t i = 0; i < bare.size(); ++i) {
        const bool names_a_file = i > 0 && listed(bare[i - 1], {"colonne", "colonnes", "pion", "pions"});
        if (isCutNotation(bare[i]) && !names_a_file) return refusal(Refusal::Kind::Notation, bare[i]);
    }
    // Something shaped like a move (« Cbxb »: a piece letter, then files, captures, ranks) must be an allowed one. A
    // capital that opens a sentence is only a word: "The", "Check".
    bool opens_sentence = true;
    for (const auto raw : spaced) {
        const auto w = text::trimEndMatches(raw, ".,;:!");
        const bool looks_like_move =
            w.size() >= 2 && std::string_view("RDTFCKQBN").find(w[0]) != std::string_view::npos &&
            std::all_of(w.begin() + 1, w.end(), [](char c) {
                return (c >= 'a' && c <= 'h') || c == 'x' || (c >= '1' && c <= '8') || c == '+' || c == '#';
            });
        if (!opens_sentence && looks_like_move && !contains(allowed.moves, w))
            return refusal(Refusal::Kind::Notation, std::string(w));
        opens_sentence = raw.ends_with('.') || raw.ends_with('!') || raw.ends_with('?');
    }
    for (const auto& w : words) {
        if (std::find(ASIDES.begin(), ASIDES.end(), w) != ASIDES.end()) return refusal(Refusal::Kind::Aside, w);
    }
    std::vector<const Fact*> present;
    for (const auto* list : {&facts.achieves, &facts.punished_by, &facts.missed, &facts.otherwise}) {
        for (const auto& f : *list) present.push_back(&f);
    }
    const auto supported = [&](const Motif& m) {
        return std::any_of(present.begin(), present.end(), [&](const Fact* f) { return m.supports(*f); });
    };
    // What the theory note names is supported too: its words are those of the surveys.
    const auto noted = [&](const Motif& m) {
        return std::any_of(allowed.noted.begin(), allowed.noted.end(),
                           [&](const std::string& w) { return says(w, m.words); });
    };
    const auto used = [&](const Motif& m) {
        return std::any_of(words.begin(), words.end(), [&](const std::string& w) { return says(w, m.words); });
    };
    for (const auto& notion : notions()) {
        if (used(notion) && !supported(notion) && !noted(notion))
            return refusal(Refusal::Kind::Unsupported, std::string(stem(notion.words[0])));
    }
    if (auto r = checkCentre(text, present)) return r;
    // A castling move among those the prompt shows may be called what it is.
    const bool castles = std::any_of(allowed.moves.begin(), allowed.moves.end(),
                                     [](const std::string& m) { return m.find("O-O") != std::string::npos; });
    for (const auto& motif : motifs()) {
        if (used(motif) && !supported(motif) && !noted(motif) && !(castles && motif.words[0] == "roque"))
            return refusal(Refusal::Kind::Motif, std::string(stem(motif.words[0])));
    }
    // A key square is the endgame books' own term; « à la clé » is only an idiom.
    std::string plain = " ";
    for (std::size_t i = 0; i < words.size(); ++i) plain += (i ? " " : "") + words[i];
    plain += ' ';
    static constexpr std::string_view KEY_SQUARE[] = {"case clé", "cases clés", "key square", "key squares"};
    const bool key_square = std::any_of(std::begin(KEY_SQUARE), std::end(KEY_SQUARE), [&](std::string_view phrase) {
        return plain.find(fmt::format(" {} ", phrase)) != std::string::npos;
    });
    if (key_square &&
        std::none_of(present.begin(), present.end(), [](const Fact* f) { return is<fact::KeySquare>(*f); }))
        return refusal(Refusal::Kind::Motif, "case clé");
    // « dame noire », "black queen": a coloured piece must be one the facts know, or the mover's.
    std::vector<Placed> pieces;
    for (const auto* f : present) {
        for (const auto& p : factPieces(*f)) pieces.push_back(p);
    }
    for (std::size_t i = 0; i < words.size(); ++i) {
        std::optional<Color> colour;
        if (listed(words[i], {"noir", "noire", "black"})) colour = Color::Black;
        if (listed(words[i], {"blanc", "blanche", "white"})) colour = Color::White;
        if (!colour) continue;
        std::vector<const std::string*> neighbours;
        if (i > 0) neighbours.push_back(&words[i - 1]);
        if (i + 1 < words.size()) neighbours.push_back(&words[i + 1]);
        for (const auto* n : neighbours) {
            std::optional<Role> role;
            if (*n == "dame" || *n == "queen") role = Role::Queen;
            if (*n == "tour" || *n == "rook") role = Role::Rook;
            if (*n == "fou" || *n == "bishop") role = Role::Bishop;
            if (*n == "cavalier" || *n == "knight") role = Role::Knight;
            if (!role) continue;
            const bool known = std::any_of(pieces.begin(), pieces.end(),
                                           [&](const Placed& p) { return p.role == *role && p.color == *colour; });
            if (!known && *colour != mover) return refusal(Refusal::Kind::Colour, fmt::format("{} {}", *n, words[i]));
        }
    }
    for (const auto& p : placedIn(text)) {
        const bool there = std::any_of(allowed.pieces.begin(), allowed.pieces.end(), [&](const Placed& q) {
            return q.role == p.role && q.square == p.square && (!p.colour || q.color == *p.colour);
        });
        if (!there) return refusal(Refusal::Kind::Piece, p.said);
    }
    return std::nullopt;
}

}  // namespace brief_detail

}  // namespace fcg::commentary
