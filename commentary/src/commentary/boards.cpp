// What a sentence says of the boards it speaks of (#64): the pieces it names on squares, the pawn structure, the
// tactics and the material it tells, read against the lines the prompt shows; and a position called lost, and words
// that say something else than what is meant. The reread of November 18 and the stories of #62 found sentences that
// were true of some board of the prompt, but not of the one they spoke of: a rook on d3 two moves before it gets
// there, doubled pawns that the recapture undid, a queen taken where a knight stood, a discovered attack told right
// after a move that only prepared it.

#include <algorithm>
#include <array>
#include <iterator>
#include <span>

#include "brief.hpp"
#include "facts.hpp"
#include "material.hpp"
#include "structure.hpp"
#include "text.hpp"

namespace fcg::commentary::brief_detail {

namespace {

// Plies of a line its material is counted over, as the facts count it (line.cpp: MATERIAL_PLIES).
constexpr std::size_t COUNTED_PLIES = 10;
// Words between a verb and the piece it takes: "takes the black queen on f3", « prendre la dame en f3 ».
constexpr std::size_t TAKEN_WITHIN = 3;
// Words around "doubled", "backward" and the others where the file or the pawn they speak of is looked for, and where
// the side whose pawns they are is.
constexpr std::size_t CLAIM_WITHIN  = 4;
constexpr std::size_t COLOUR_WITHIN = 8;

bool in(std::string_view word, std::span<const std::string_view> list) {
    return std::find(list.begin(), list.end(), word) != list.end();
}

bool isSquareWord(std::string_view w) {
    return w.size() == 2 && w[0] >= 'a' && w[0] <= 'h' && w[1] >= '1' && w[1] <= '8';
}

Square squareOf(std::string_view w) { return square(static_cast<File>(w[0] - 'a'), static_cast<Rank>(w[1] - '1')); }

// ---- A sentence, word by word

struct Token {
    std::string raw;     // as written, without the punctuation around it
    std::string word;    // in lower case
    bool move  = false;  // a move the prompt shows
    bool pause = false;  // a comma, a semicolon or a colon follows it
};
using Sentence = std::vector<Token>;

// The sentences of a comment: a word that ends with a full stop, an exclamation or a question mark ends one; moves
// keep their dots. A word with an apostrophe is its parts: « l'échange », "White's".
std::vector<Sentence> sentencesOf(std::string_view text, const Allowed& allowed) {
    std::vector<Sentence> out(1);
    for (const auto piece : text::splitWhitespace(text)) {
        const bool ends  = piece.ends_with('.') || piece.ends_with('!') || piece.ends_with('?');
        const bool pause = piece.ends_with(',') || piece.ends_with(';') || piece.ends_with(':');
        const auto core  = text::trimMatches(piece, ",;:!?.()«»“”\"");
        if (!core.empty()) {
            const bool move = !isSquareWord(core) &&
                              std::find(allowed.moves.begin(), allowed.moves.end(), core) != allowed.moves.end();
            if (move) {
                out.back().push_back({std::string(core), std::string(core), true, pause});
            } else {
                for (const auto part : text::split(core, [](char32_t c) { return c == U'\'' || c == U'’'; })) {
                    if (!part.empty()) out.back().push_back({std::string(part), text::toLower(part), false, false});
                }
                if (!out.back().empty()) out.back().back().pause = pause;
            }
        } else if (pause && !out.back().empty()) {
            out.back().back().pause = true;  // « ... e4 : »
        }
        if (ends && !out.back().empty()) out.emplace_back();
    }
    if (out.back().empty()) out.pop_back();
    return out;
}

// The words of `s` from `from` to `to`, as written.
std::string said(const Sentence& s, std::size_t from, std::size_t to) {
    std::string out;
    for (std::size_t i = from; i <= to && i < s.size(); ++i) out += (i > from ? " " : "") + s[i].raw;
    return out;
}

std::optional<Role> roleOf(std::string_view w) {
    if (w == "roi" || w == "king") return Role::King;
    if (w == "dame" || w == "queen") return Role::Queen;
    if (w == "tour" || w == "rook") return Role::Rook;
    if (w == "fou" || w == "bishop") return Role::Bishop;
    if (w == "cavalier" || w == "knight") return Role::Knight;
    if (w == "pion" || w == "pawn") return Role::Pawn;
    return std::nullopt;
}

std::optional<Color> colourOf(std::string_view w) {
    static constexpr std::string_view WHITE[] = {"white", "blanc", "blanche", "blancs", "blanches"};
    static constexpr std::string_view BLACK[] = {"black", "noir", "noire", "noirs", "noires"};
    if (in(w, WHITE)) return Color::White;
    if (in(w, BLACK)) return Color::Black;
    return std::nullopt;
}

// A piece the sentence puts on a square, as placedIn() reads one, with the words that do.
struct Mention {
    Role role = Role::Pawn;
    std::optional<Color> colour;
    Square square     = Square::A1;
    std::size_t first = 0, last = 0;
    std::string said;
};

std::vector<Mention> mentionsIn(const Sentence& s) {
    const auto word = [&](std::size_t i) -> std::string_view {
        return i < s.size() && !s[i].move ? std::string_view(s[i].word) : std::string_view();
    };
    // The colour just before a word, past the "s" of "White's".
    const auto colourBefore = [&](std::size_t i) -> std::optional<Color> {
        if (i >= 2 && word(i - 1) == "s") return colourOf(word(i - 2));
        return i >= 1 ? colourOf(word(i - 1)) : std::nullopt;
    };
    std::vector<Mention> out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const auto role = roleOf(word(i));
        const bool verb = word(i) == "queen" && i > 0 && word(i - 1) == "to";
        if (role && !verb) {
            // English gives the colour first, French after the piece.
            auto colour      = colourBefore(i);
            std::size_t next = i + 1;
            if (const auto after = colourOf(word(next))) {
                colour = after;
                ++next;
            }
            if (const auto w = word(next); w == "on" || w == "in" || w == "at" || w == "en") ++next;
            if (isSquareWord(word(next)))
                out.push_back({*role, colour, squareOf(word(next)), i, next, said(s, i, next)});
        } else if (isSquareWord(word(i))) {
            if (const auto r = roleOf(word(i + 1)))
                out.push_back({*r, colourBefore(i), squareOf(word(i)), i, i + 1, said(s, i, i + 1)});
        }
    }
    // A pawn on its last rank is a promotion square.
    std::erase_if(out, [](const Mention& m) {
        return m.role == Role::Pawn && (rankOf(m.square) == Rank::First || rankOf(m.square) == Rank::Eighth);
    });
    return out;
}

bool stands(const Mention& m, const Position& pos) {
    const auto there = Placed::at(pos.board(), m.square);
    return there && there->role == m.role && (!m.colour || there->color == *m.colour);
}

// ---- The moves a sentence quotes, where they are in the lines shown

// The `ply`th move of the `line`th line shown.
struct At {
    std::size_t line = 0, ply = 0;
};

std::vector<At> occurrences(const Token& t, const Allowed& a) {
    std::vector<At> out;
    if (!t.move) return out;
    for (std::size_t l = 0; l < a.lines.size(); ++l) {
        const auto& texts = a.lines[l].texts;
        for (std::size_t p = 0; p < texts.size(); ++p) {
            if (std::find(texts[p].begin(), texts[p].end(), t.raw) != texts[p].end()) out.push_back({l, p});
        }
    }
    return out;
}

bool isPlayed(const At& at, const Allowed& a) { return a.lines[at.line].kind == Shown::Kind::Played && at.ply == 0; }

// The quoted moves of a sentence, by the token that quotes them: the move played, or moves of a line.
struct Anchor {
    std::size_t token = 0;
    std::vector<At> at;
    bool played = false;  // the move played: the sentence speaks of the position around it
    // "after 27…Qf7", « après 27…Df7 »: what follows in the sentence is the board there. A move quoted otherwise ("7…e5
    // was better: ...", « 29.Cd4, qui menaçait ... ») is told with what its line goes on to do.
    bool after = false;
};

std::vector<Anchor> anchorsOf(const Sentence& s, const Allowed& a) {
    std::vector<Anchor> out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        auto at = occurrences(s[i], a);
        if (at.empty()) continue;
        const bool played = std::any_of(at.begin(), at.end(), [&](const At& x) { return isPlayed(x, a); });
        const bool after  = i > 0 && !s[i - 1].move && (s[i - 1].word == "after" || s[i - 1].word == "après");
        out.push_back({i, std::move(at), played, after});
    }
    return out;
}

// Whether a move quoted with "after" comes before the token `k`: the sentence speaks of the board there.
bool afterBefore(const std::vector<Anchor>& anchors, std::size_t k) {
    return std::any_of(anchors.begin(), anchors.end(), [&](const Anchor& x) { return x.after && x.token < k; });
}

// The last move quoted before the token `k`, if one is.
const Anchor* lastBefore(const std::vector<Anchor>& anchors, std::size_t k) {
    const Anchor* found = nullptr;
    for (const auto& x : anchors) {
        if (x.token < k) found = &x;
    }
    return found;
}

// Words telling that what follows them comes later in the line, or may: "then", "allows", "threatens", « ensuite »,
// « laisse », « préparait ».
constexpr std::string_view LATER[] = {
    "then",       "next",       "later",       "afterwards", "eventually", "soon",       "follows",    "followed",
    "following",  "follow",     "can",         "could",      "will",       "would",      "may",        "might",
    "threatens",  "threatened", "threatening", "threaten",   "threat",     "allows",     "allowed",    "allowing",
    "allow",      "lets",       "letting",     "permits",    "prepares",   "prepared",   "preparing",  "intending",
    "aiming",     "heading",    "leaves",      "leaving",    "left",       "until",      "becomes",    "become",
    "became",     "becoming",   "devient",     "deviennent", "devenait",   "ensuite",    "puis",       "suit",
    "suivent",    "suivait",    "suivi",       "suivie",     "suivra",     "tard",       "bientôt",    "peut",
    "peuvent",    "pouvait",    "pouvaient",   "pourra",     "pourront",   "pourrait",   "pourraient", "va",
    "vont",       "allait",     "menace",      "menacent",   "menaçait",   "menaçaient", "menaçant",   "permet",
    "permettent", "permettait", "laisse",      "laissent",   "laissait",   "laissant",   "laissé",     "prépare",
    "préparent",  "préparait",  "préparant",   "vise",       "visait",     "conduit",    "jusqu"};

bool laterBetween(const Sentence& s, std::size_t from, std::size_t to) {
    for (std::size_t i = from + 1; i < to && i < s.size(); ++i) {
        if (!s[i].move && in(s[i].word, LATER)) return true;
    }
    return false;
}

// Whether the words right after the token `k` tell a piece's coming there: "the king on e4 becomes active", « le
// cavalier en d4 s'installe », "the knight on e6 can blockade".
bool comesAfter(const Sentence& s, std::size_t k) {
    static constexpr std::string_view COMES[] = {"settles", "installe", "arrives", "arrive", "reaches", "atteint",
                                                 "comes",   "vient",    "lands",   "gets",   "goes"};
    for (std::size_t i = k + 1; i < s.size() && i <= k + 2; ++i) {
        if (s[i].move) return false;
        if (in(s[i].word, LATER) || in(s[i].word, COMES)) return true;
        if (s[i].word != "s" && s[i].word != "then") return false;  // « s'installe »
    }
    return false;
}

// ---- The pieces a sentence names

// The boards a sentence speaks of at once: around each move it quotes, and around the move played unless `played` is
// false.
std::vector<const Position*> presentBoards(const std::vector<Anchor>& anchors, const Allowed& a, bool played = true) {
    const auto& line = a.lines.front();
    std::vector<const Position*> out;
    if (played) {
        out.push_back(&line.boards.front());
        if (line.boards.size() > 1) out.push_back(&line.boards[1]);
    }
    for (const auto& x : anchors) {
        for (const auto& at : x.at) {
            const auto& boards = a.lines[at.line].boards;
            out.push_back(&boards[at.ply]);
            if (at.ply + 1 < boards.size()) out.push_back(&boards[at.ply + 1]);
        }
    }
    return out;
}

// Whether the piece stands on a board of the line of a quoted move, from that move to the end of the line shown.
bool standsLater(const Mention& m, const Anchor& since, const Allowed& a) {
    for (const auto& at : since.at) {
        const auto& sh = a.lines[at.line];
        for (std::size_t b = at.ply; b <= sh.shown && b < sh.boards.size(); ++b) {
            if (stands(m, sh.boards[b])) return true;
        }
    }
    return false;
}

std::optional<Refusal> checkPieces(const Sentence& s, const std::vector<Anchor>& anchors, const Allowed& a) {
    const auto present = presentBoards(anchors, a);
    for (const auto& m : mentionsIn(s)) {
        const auto* since = lastBefore(anchors, m.first);
        bool there        = false;
        if (afterBefore(anchors, m.first)) {
            // After "after 27…Qf7", the board there, and those of the moves quoted from it on; or later in the line,
            // when the sentence tells it as what follows. "After 21…Rc1, the bishop on e3 hangs" speaks of a bishop
            // taken the move before.
            const auto latest = std::find_if(anchors.rbegin(), anchors.rend(),
                                             [&](const Anchor& x) { return x.after && x.token < m.first; });
            const std::vector<Anchor> from(std::prev(latest.base()), anchors.end());
            const auto boards = presentBoards(from, a, false);
            there = std::any_of(boards.begin(), boards.end(), [&](const Position* p) { return stands(m, *p); }) ||
                    ((laterBetween(s, since->token, m.first) || comesAfter(s, m.last)) && standsLater(m, *since, a));
            if (!there) return Refusal{Refusal::Kind::Piece, m.said};
            continue;
        }
        if (std::any_of(present.begin(), present.end(), [&](const Position* p) { return stands(m, *p); })) continue;
        if (since && !since->played) {
            // Told with a move of a line it quotes, the piece stands somewhere along that line.
            there = standsLater(m, *since, a);
        } else if (laterBetween(s, since ? since->token : std::size_t(-1), m.first) || comesAfter(s, m.last)) {
            // Told with the move played, or quoting no move before it, as what follows ("27.Rg1 lets the knight on e6
            // blockade"): where a line shown takes it. Told as present, it stands around the move played.
            for (const auto& sh : a.lines) {
                for (std::size_t b = 0; b <= sh.shown && b < sh.boards.size() && !there; ++b)
                    there = stands(m, sh.boards[b]);
            }
        }
        if (!there) return Refusal{Refusal::Kind::Piece, m.said};
    }
    return std::nullopt;
}

// ---- Pawn structure

enum class Feature : uint8_t { Doubled, Backward, Isolated, Passed };

std::optional<Feature> featureOf(std::string_view w) {
    if (w == "doubled" || w.starts_with("doublé")) return Feature::Doubled;
    if (w == "backward" || w.starts_with("arriéré")) return Feature::Backward;
    if (w == "isolated" || w == "isolani" || w.starts_with("isolé")) return Feature::Isolated;
    static constexpr std::string_view PASSED[] = {"passed", "passé", "passés", "passée", "passées"};
    if (in(w, PASSED)) return Feature::Passed;
    return std::nullopt;
}

// The file a word names: "c-file", "c-pawns", or the letter after « colonne ».
std::optional<File> fileNamed(const Sentence& s, std::size_t i) {
    const auto& w = s[i].word;
    if (s[i].move) return std::nullopt;
    if (w.size() >= 3 && w[0] >= 'a' && w[0] <= 'h' && w[1] == '-' &&
        (w.substr(2) == "file" || w.substr(2) == "pawn" || w.substr(2) == "pawns"))
        return static_cast<File>(w[0] - 'a');
    if ((w == "colonne" || w == "colonnes") && i + 1 < s.size()) {
        const auto& n = s[i + 1].word;
        if (n.size() == 1 && n[0] >= 'a' && n[0] <= 'h') return static_cast<File>(n[0] - 'a');
    }
    return std::nullopt;
}

bool hasFeature(const Position& pos, Feature feature, std::optional<Color> colour, std::optional<File> file,
                const Mention* pawn) {
    const auto& board = pos.board();
    for (const auto side : {Color::White, Color::Black}) {
        if (colour && side != *colour) continue;
        if (feature == Feature::Doubled) {
            if (file && (pawns(board, side) & Bitboard::file(*file)).count() >= 2) return true;
            continue;
        }
        if (!pawn) continue;
        const Placed placed{side, Role::Pawn, pawn->square};
        if (Placed::at(board, pawn->square) != placed) continue;
        if (feature == Feature::Passed) {
            if (isPassed(board, pawn->square, side)) return true;
            continue;
        }
        for (const auto& f : positionFacts(pos)) {
            const auto* b = std::get_if<fact::BackwardPawn>(&f);
            const auto* i = std::get_if<fact::IsolatedPawn>(&f);
            if (feature == Feature::Backward && b && b->pawn == placed) return true;
            if (feature == Feature::Isolated && i && i->pawn == placed) return true;
        }
    }
    return false;
}

std::optional<Refusal> checkStructure(const Sentence& s, const std::vector<Anchor>& anchors, const Allowed& a) {
    const auto mentions = mentionsIn(s);
    const auto distance = [](std::size_t x, std::size_t y) { return x > y ? x - y : y - x; };
    for (std::size_t k = 0; k < s.size(); ++k) {
        const auto feature = s[k].move ? std::nullopt : featureOf(s[k].word);
        if (!feature) continue;
        // What it is said of: a file for doubled pawns, a pawn on its square for the others; nothing to check without.
        std::optional<File> file;
        std::optional<std::size_t> file_at;
        const Mention* pawn = nullptr;
        if (*feature == Feature::Doubled) {
            // Rooks or queens doubled on a file are not pawns.
            static constexpr std::string_view HEAVY[] = {"rook", "rooks", "tour", "tours", "queen", "dame"};
            bool heavy                                = false;
            for (std::size_t i = 0; i < s.size(); ++i) {
                if (distance(i, k) > CLAIM_WITHIN) continue;
                heavy = heavy || (!s[i].move && in(s[i].word, HEAVY));
                if (const auto f = fileNamed(s, i); f && (!file_at || distance(i, k) < distance(*file_at, k))) {
                    file    = f;
                    file_at = i;
                }
            }
            if (!file || heavy) continue;
        } else {
            for (const auto& m : mentions) {
                const auto d = std::min(distance(m.first, k), distance(m.last, k));
                if (m.role == Role::Pawn && d <= CLAIM_WITHIN &&
                    (!pawn || d < std::min(distance(pawn->first, k), distance(pawn->last, k))))
                    pawn = &m;
            }
            if (!pawn) continue;
        }
        // Whose: the colour named nearest before the word, or the pawn's.
        std::optional<Color> colour = pawn ? pawn->colour : std::nullopt;
        if (!colour) {
            for (std::size_t i = k; i-- > 0 && k - i <= COLOUR_WITHIN;) {
                if (const auto c = s[i].move ? std::nullopt : colourOf(s[i].word)) {
                    colour = c;
                    break;
                }
            }
        }
        // A lasting feature: so where the line of the last move quoted before the word (the first one after it, else;
        // the move played's, when it quotes none) settles, as far as the prompt shows it, as the facts count them.
        // Doubled pawns that the recapture undoes at once are none.
        const Anchor* since = lastBefore(anchors, k);
        if (!since) {
            for (const auto& x : anchors) {
                if (x.token > k) {
                    since = &x;
                    break;
                }
            }
        }
        const std::vector<At> played{At{0, 0}};
        bool holds = false;
        for (const auto& at : since ? since->at : played) {
            const auto& sh = a.lines[at.line];
            // The pawn the quoted move takes is no longer there to be anything: "25…Qxf3, taking the pawn, ... White's
            // f3 pawn is backward" speaks of another pawn that comes to f3 later.
            const auto& mv = sh.moves[at.ply];
            if (pawn && mv.isCapture() && mv.to() == pawn->square && stands(*pawn, sh.boards[at.ply])) continue;
            holds =
                holds || hasFeature(sh.boards[std::min(sh.shown, sh.boards.size() - 1)], *feature, colour, file, pawn);
        }
        if (!holds) {
            // « doublés sur la colonne c »: the file's letter after « colonne ».
            const auto file_end = file_at ? *file_at + (s[*file_at].word.starts_with("colonne") ? 1 : 0) : k;
            const auto first    = std::min({k, pawn ? pawn->first : k, file_at.value_or(k)});
            const auto last     = std::max({k, pawn ? pawn->last : k, file_end});
            return Refusal{Refusal::Kind::Board, said(s, first, last)};
        }
    }
    return std::nullopt;
}

// ---- Tactics

enum class Tactic : uint8_t { Fork, Pin, Skewer, Discovered, Overload, Clearance, DoubleCheck };

// The tactic the token `k` names: "forks", « clouage », "double check".
std::optional<Tactic> tacticAt(const Sentence& s, std::size_t k) {
    if (s[k].move) return std::nullopt;
    const auto& w   = s[k].word;
    const auto word = [&](std::size_t i) { return i < s.size() && !s[i].move ? std::string_view(s[i].word) : ""; };
    if ((w == "double" && (word(k + 1) == "check" || word(k + 1) == "échec")) ||
        (w == "échec" && word(k + 1) == "double"))
        return Tactic::DoubleCheck;
    static constexpr std::string_view FORK[]  = {"fork", "forks", "forked", "forking"};
    static constexpr std::string_view PIN[]   = {"pin", "pins", "pinned", "pinning"};
    static constexpr std::string_view CLEAR[] = {"clears", "cleared", "clearing"};
    if (w.starts_with("fourchette") || w.starts_with("fourche") || in(w, FORK)) return Tactic::Fork;
    if (w.starts_with("clou") || in(w, PIN)) return Tactic::Pin;
    if (w.starts_with("enfilade") || w.starts_with("skewer")) return Tactic::Skewer;
    if (w.starts_with("découv") || w.starts_with("discover") || w.starts_with("uncover")) return Tactic::Discovered;
    if (w.starts_with("surcharg") || w.starts_with("overload")) return Tactic::Overload;
    if (w.starts_with("dégag") || w.starts_with("clearance") || in(w, CLEAR)) return Tactic::Clearance;
    return std::nullopt;
}

bool isTactic(const Fact& f, Tactic t) {
    switch (t) {
        case Tactic::Fork:
            return std::holds_alternative<fact::Fork>(f);
        case Tactic::Pin:
            return std::holds_alternative<fact::Pin>(f);
        case Tactic::Skewer:
            return std::holds_alternative<fact::Skewer>(f);
        case Tactic::Discovered:
            return std::holds_alternative<fact::DiscoveredAttack>(f);
        case Tactic::Overload:
            return std::holds_alternative<fact::Overloaded>(f);
        case Tactic::DoubleCheck:
            return std::holds_alternative<fact::DoubleCheck>(f);
        default:
            return std::holds_alternative<fact::Clearance>(f);
    }
}

// The ply of a line shown whose move does the tactic `fact` tells, by the side it is good for (brief.cpp's doneAt).
std::optional<std::size_t> doneIn(const Fact& fact, const Shown& line) {
    const auto side = beneficiary(fact);
    for (std::size_t i = 0; i < line.shown && i + 1 < line.boards.size(); ++i) {
        if (side == line.boards[i].turn() && atOnce(fact, line.boards[i], line.moves[i])) return i;
    }
    return std::nullopt;
}

std::optional<Refusal> checkTactics(const Sentence& s, const std::vector<Anchor>& anchors, const MoveFacts& facts,
                                    const Allowed& a) {
    for (std::size_t k = 0; k < s.size(); ++k) {
        const auto tactic = tacticAt(s, k);
        if (!tactic) continue;
        // Told after a move of a line ("after 29…Qf7, Black uncovers"), or as that move's doing, in its clause ("29…Qf7
        // uncovers", "25…Qxb1+ wins it with a double check"), the tactic is that move's, or an earlier one's.
        const Anchor* since = lastBefore(anchors, k);
        if (!since || since->played || laterBetween(s, since->token, k)) continue;
        static constexpr std::string_view JOINS[] = {"and", "et", "while", "alors", "but", "mais", "since", "car"};
        bool clause                               = true;
        for (std::size_t i = since->token; i < k && clause; ++i) clause = !s[i].pause && !in(s[i + 1].word, JOINS);
        if (!afterBefore(anchors, k) && !clause) continue;
        bool sooner = false, later = false;
        for (const auto* list : {&facts.achieves, &facts.punished_by, &facts.missed, &facts.otherwise}) {
            for (const auto& f : *list) {
                if (!isTactic(f, *tactic)) continue;
                for (const auto& at : since->at) {
                    const auto done = doneIn(f, a.lines[at.line]);
                    if (!done) continue;
                    (*done <= at.ply ? sooner : later) = true;
                }
            }
        }
        if (later && !sooner) return Refusal{Refusal::Kind::Board, s[k].raw};
    }
    return std::nullopt;
}

// ---- Material

// Material by kind: pawns, knights and bishops, rooks, queens.
using Kinds = std::array<int32_t, 4>;

Kinds kindsOf(const Board& board, Color c) {
    const auto n = [&](Role r) { return (board.byRole(r) & board.byColor(c)).count(); };
    return {n(Role::Pawn), n(Role::Knight) + n(Role::Bishop), n(Role::Rook), n(Role::Queen)};
}

// Material a sentence names: by kind, with pieces of any kind but pawns ("a piece") and exchanges (a rook for a minor
// piece) apart; and its worth in pawns.
struct Named {
    Kinds kinds{};
    int32_t pieces = 0, exchanges = 0, worth = 0;
    Named& operator+=(const Named& o) {
        for (std::size_t k = 0; k < 4; ++k) kinds[k] += o.kinds[k];
        pieces += o.pieces;
        exchanges += o.exchanges;
        worth += o.worth;
        return *this;
    }
};

// `count` pieces of the kind a word names: "pawns", « fou », "exchange".
std::optional<Named> nounOf(std::string_view w, int32_t count) {
    static constexpr std::string_view PAWNS[]    = {"pawn", "pawns", "pion", "pions"};
    static constexpr std::string_view MINORS[]   = {"knight", "knights", "cavalier", "cavaliers",
                                                    "bishop", "bishops", "fou",      "fous"};
    static constexpr std::string_view ROOKS[]    = {"rook", "rooks", "tour", "tours"};
    static constexpr std::string_view QUEENS[]   = {"queen", "queens", "dame", "dames"};
    static constexpr std::string_view PIECES[]   = {"piece", "pieces", "pièce", "pièces"};
    static constexpr std::string_view EXCHANGE[] = {"exchange", "qualité"};
    Named n;
    const auto named = [&](int32_t& field, int32_t worth) {
        field   = count;
        n.worth = worth * count;
        return n;
    };
    if (in(w, PAWNS)) return named(n.kinds[0], 1);
    if (in(w, MINORS)) return named(n.kinds[1], 3);
    if (in(w, ROOKS)) return named(n.kinds[2], 5);
    if (in(w, QUEENS)) return named(n.kinds[3], 9);
    if (in(w, PIECES)) return named(n.pieces, 3);
    if (in(w, EXCHANGE)) return named(n.exchanges, 2);
    return std::nullopt;
}

std::optional<int32_t> countOf(std::string_view w) {
    static constexpr std::pair<std::string_view, int32_t> COUNTS[] = {
        {"a", 1},   {"an", 1},    {"one", 1},  {"the", 1},  {"his", 1},   {"her", 1},   {"its", 1}, {"their", 1},
        {"two", 2}, {"three", 3}, {"four", 4}, {"un", 1},   {"une", 1},   {"le", 1},    {"la", 1},  {"l", 1},
        {"sa", 1},  {"son", 1},   {"leur", 1}, {"deux", 2}, {"trois", 3}, {"quatre", 4}};
    for (const auto& [word, count] : COUNTS) {
        if (w == word) return count;
    }
    return std::nullopt;
}

// One piece or pawn, or several of a kind, from the token `i`: "the white queen on d1", « deux pions »; and the token
// after it.
std::optional<std::pair<Named, std::size_t>> item(const Sentence& s, std::size_t i) {
    if (i >= s.size() || s[i].move || s[i].pause) return std::nullopt;
    const auto count = countOf(s[i].word);
    if (!count) return std::nullopt;
    std::size_t n = i + 1;
    if (n < s.size() && !s[n].pause && colourOf(s[n].word)) ++n;  // "the white queen"
    if (n >= s.size() || s[n].move) return std::nullopt;
    const auto named = nounOf(s[n].word, *count);
    // "the bishop pair" is no bishop.
    if (!named || (!s[n].pause && n + 1 < s.size() && (s[n + 1].word == "pair" || s[n + 1].word == "paire")))
        return std::nullopt;
    // The colour after the piece in French, and its square: « la dame blanche en d1 », "the queen on d1".
    std::size_t end = n;
    if (!s[end].pause && end + 1 < s.size() && colourOf(s[end + 1].word)) ++end;
    if (!s[end].pause && end + 2 < s.size() && (s[end + 1].word == "on" || s[end + 1].word == "en") &&
        isSquareWord(s[end + 2].word))
        end += 2;
    return std::pair{*named, end + 1};
}

// Pieces and pawns joined by "and", « et »: one named after "and" and followed by more words is the subject of another
// clause (« un pion et la dame en d1 fait pression »), not one more thing won.
std::optional<std::pair<Named, std::size_t>> items(const Sentence& s, std::size_t i) {
    const auto first = item(s, i);
    if (!first) return std::nullopt;
    auto [total, at] = *first;
    const auto joins = [&](std::size_t k) {
        return k < s.size() && !s[k].move && (s[k].word == "and" || s[k].word == "et" || s[k].word == "plus");
    };
    const auto ends = [&](std::size_t k) {
        return k >= s.size() || s[k - 1].pause || joins(k) || s[k].word == "for" || s[k].word == "contre";
    };
    while (!s[at - 1].pause && joins(at)) {
        const auto next = item(s, at + 1);
        if (!next || !ends(next->second)) break;
        total += next->first;
        at = next->second;
    }
    return std::pair{total, at};
}

// Material said to change hands: what is won, and what is given for it when the sentence says so ("for", « contre »).
struct Amount {
    Named won;
    int32_t net     = 0;  // in pawns, what is given taken off
    std::size_t end = 0;
    bool traded     = false;
};

std::optional<Amount> amountAt(const Sentence& s, std::size_t i) {
    const auto first = items(s, i);
    if (!first) return std::nullopt;
    Amount a{first->first, first->first.worth, first->second, false};
    if (a.end < s.size() && !s[a.end - 1].pause && (s[a.end].word == "for" || s[a.end].word == "contre")) {
        if (const auto given = items(s, a.end + 1)) {
            a.net -= given->first.worth;
            a.end    = given->second;
            a.traded = true;
        }
    }
    return a;
}

// Verbs of material won or lost, after which the material is named: "wins", "gives up", « gagne », « coûte ».
constexpr std::string_view GAINS[]  = {"win",    "wins",    "won",       "winning", "gagne", "gagnent",
                                       "gagner", "gagnait", "gagnaient", "gagnant", "gagné", "gagnée"};
constexpr std::string_view LOSSES[] = {"lose",    "loses",  "lost",    "losing",    "costs",   "cost",
                                       "costing", "perd",   "perdent", "perdre",    "perdait", "perdaient",
                                       "perdant", "coûte",  "coûtent", "coûtait",   "coûtant", "donne",
                                       "donnent", "donner", "donnait", "donnaient", "donnant"};
constexpr std::string_view GIVES[]  = {"give", "gives", "gave", "giving", "given"};

// The material said to change hands from the token `k`: its verb's, from the token after it ("up" past "gives").
std::optional<Amount> materialAt(const Sentence& s, std::size_t k) {
    if (s[k].move) return std::nullopt;
    const auto& w = s[k].word;
    if (in(w, GAINS) || in(w, LOSSES)) return amountAt(s, k + 1);
    if (in(w, GIVES) && k + 1 < s.size() && s[k + 1].word == "up") return amountAt(s, k + 2);
    return std::nullopt;
}

// Whether one side, along a line from the ply `from`, comes to have won at least what is named, kind by kind, what it
// lost of each kind taken off: "wins the bishop and two pawns" needs the pawns as well as the bishop.
bool wonAlong(const Shown& line, std::size_t from, const Named& named) {
    if (from >= line.boards.size()) return false;
    const auto& base = line.boards[from].board();
    for (std::size_t b = from + 1; b < line.boards.size() && b <= from + COUNTED_PLIES; ++b) {
        const auto& now = line.boards[b].board();
        for (const auto side : {Color::White, Color::Black}) {
            const auto mine = kindsOf(base, side), theirs = kindsOf(base, !side);
            const auto mine_now = kindsOf(now, side), theirs_now = kindsOf(now, !side);
            Kinds gain{};
            for (std::size_t k = 0; k < 4; ++k) gain[k] = (theirs[k] - theirs_now[k]) - (mine[k] - mine_now[k]);
            const auto& k = named.kinds;
            if (gain[0] >= k[0] && gain[1] >= k[1] && gain[2] >= k[2] + named.exchanges && gain[3] >= k[3] &&
                gain[1] + gain[2] + gain[3] >= k[1] + k[2] + k[3] + named.pieces)
                return true;
        }
    }
    return false;
}

// What a line wins once its captures settle, from the ply `from`, for whichever side, in pawns.
int32_t swing(const Shown& line, std::size_t from) {
    if (from >= line.moves.size() || from >= line.boards.size()) return 0;
    const auto rest   = std::span<const Move>(line.moves).subspan(from);
    const auto& start = line.boards[from];
    return settledSwing(start, rest.first(std::min(rest.size(), COUNTED_PLIES)), start.turn());
}

std::optional<Refusal> checkMaterial(const Sentence& s, const std::vector<Anchor>& anchors, const MoveFacts& facts,
                                     const Allowed& a) {
    // A piece said to be taken or won on its square: a move of the lines takes it there, or it stands there to be
    // taken.
    static constexpr std::string_view TAKES[] = {
        "take",    "takes",   "took",      "taking",  "taken",     "capture",  "captures",  "captured", "capturing",
        "win",     "wins",    "won",       "winning", "prend",     "prennent", "prendre",   "prenait",  "prenaient",
        "prenant", "pris",    "prise",     "prises",  "capturent", "capturer", "capturait", "gagne",    "gagnent",
        "gagner",  "gagnait", "gagnaient", "gagnant", "gagné",     "gagnée"};
    static constexpr std::string_view ARTICLES[] = {"the", "a",   "an",  "le", "la",  "les",  "l",     "un",
                                                    "une", "his", "its", "sa", "son", "leur", "their", "enemy"};
    const auto present                           = presentBoards(anchors, a);
    for (const auto& m : mentionsIn(s)) {
        if (m.first == 0) continue;
        std::optional<std::size_t> verb;
        for (std::size_t j = m.first; j-- > 0 && m.first - j <= TAKEN_WITHIN;) {
            if (s[j].move) break;
            if (in(s[j].word, TAKES)) {
                verb = j;
                break;
            }
            if (!in(s[j].word, ARTICLES) && !colourOf(s[j].word) && s[j].word != "s") break;  // "White's"
        }
        if (!verb) continue;
        if (std::any_of(present.begin(), present.end(), [&](const Position* p) { return stands(m, *p); })) continue;
        bool taken = false;
        for (const auto& sh : a.lines) {
            for (std::size_t p = 0; p < sh.moves.size() && p + 1 < sh.boards.size(); ++p) {
                const auto& mv = sh.moves[p];
                if (mv.isCapture() && !mv.isEnPassant() && mv.to() == m.square && stands(m, sh.boards[p])) taken = true;
            }
        }
        if (!taken) return Refusal{Refusal::Kind::Material, m.said};
    }
    // What a quoted capture takes, named by its kind: "25…Qxf3 was the move, taking the pawn" where it takes the queen.
    static constexpr std::string_view CAPTURES[] = {"take",     "takes",    "took",      "taking",   "capture",
                                                    "captures", "captured", "capturing", "prend",    "prennent",
                                                    "prendre",  "prenait",  "prenant",   "capturent"};
    for (std::size_t k = 0; k + 2 < s.size(); ++k) {
        if (s[k].move || !in(s[k].word, CAPTURES) || s[k].pause) continue;
        const auto* since = lastBefore(anchors, k);
        if (!since || k > since->token + 6 || s[k + 1].move) continue;
        const auto count = countOf(s[k + 1].word);
        const auto noun  = !count || s[k + 2].move ? std::nullopt : nounOf(s[k + 2].word, 1);
        // "A piece" may be any piece, "the exchange" two of them; "took the queens off" traded them.
        if (!noun || noun->pieces || noun->exchanges || (k + 3 < s.size() && s[k + 3].word == "off")) continue;
        bool same = false, capture = false;
        for (const auto& at : since->at) {
            const auto taken = a.lines[at.line].moves[at.ply].capture();
            if (!taken) continue;
            capture = true;
            same    = same || value(*taken) == noun->worth;
        }
        if (capture && !same) return Refusal{Refusal::Kind::Material, said(s, k + 1, k + 2)};
    }
    // The material said to change hands: what is won, kind by kind, as one side comes to have won it along a line
    // shown, from its start or from a move the sentence quotes; or no more than the facts count. Traded ("a piece for
    // two pawns"), what it comes to, as the lines' captures settle.
    int32_t net = 0;
    for (const auto* list : {&facts.achieves, &facts.punished_by, &facts.missed, &facts.otherwise}) {
        for (const auto& f : *list) {
            if (const auto* w = std::get_if<fact::WinsMaterial>(&f)) net = std::max(net, w->pawns);
        }
    }
    const int32_t told = net;
    std::vector<std::pair<const Shown*, std::size_t>> from;
    for (const auto& sh : a.lines) from.emplace_back(&sh, 0);
    for (const auto& x : anchors) {
        for (const auto& at : x.at) from.emplace_back(&a.lines[at.line], at.ply);
    }
    for (const auto& [sh, ply] : from) net = std::max(net, std::abs(swing(*sh, ply)));
    for (std::size_t k = 0; k < s.size(); ++k) {
        const auto amount = materialAt(s, k);
        if (!amount || amount->net <= 0) continue;
        const bool counted =
            amount->traded ? amount->net <= net
                           : amount->won.worth <= told || std::any_of(from.begin(), from.end(), [&](const auto& f) {
                                 return wonAlong(*f.first, f.second, amount->won);
                             });
        if (!counted) return Refusal{Refusal::Kind::Material, said(s, k + 1, amount->end - 1)};
    }
    return std::nullopt;
}

// The better move praised for material: it must win more than the move played takes itself, unless the answer takes
// it back at once. 30.Bxh7 takes the pawn that 30.Rg7 wins two moves later; 27.Rxe8 is taken back, and 27.Kxd4 wins a
// pawn.
std::optional<Refusal> checkReason(const Sentence& s, const std::vector<Anchor>& anchors, const Allowed& a) {
    const auto best =
        std::find_if(a.lines.begin(), a.lines.end(), [](const Shown& sh) { return sh.kind == Shown::Kind::Best; });
    if (best == a.lines.end()) return std::nullopt;
    const auto& played = a.lines.front();
    const Color mover  = played.start.turn();
    const auto won     = [&](const Shown& sh, std::size_t plies) {
        const auto line = std::span<const Move>(sh.moves);
        return settledSwing(sh.start, line.first(std::min(line.size(), plies)), mover);
    };
    const int32_t better = won(*best, COUNTED_PLIES);
    const int32_t taken  = won(played, 2);
    for (std::size_t i = 0; i < anchors.size(); ++i) {
        const auto& x       = anchors[i];
        const bool the_best = !x.played && std::any_of(x.at.begin(), x.at.end(), [&](const At& at) {
            return &a.lines[at.line] == &*best && at.ply == 0;
        });
        if (!the_best) continue;
        const auto until = i + 1 < anchors.size() ? anchors[i + 1].token : s.size();
        for (std::size_t k = x.token + 1; k < until; ++k) {
            if (s[k].move || !in(s[k].word, GAINS)) continue;
            const auto amount = amountAt(s, k + 1);
            if (amount && amount->net > 0 && better <= taken)
                return Refusal{Refusal::Kind::Reason, said(s, k, amount->end - 1)};
        }
    }
    return std::nullopt;
}

// ---- Plain sentences, for the checks that need no line

std::vector<std::vector<std::string>> plainSentences(std::string_view text) {
    std::vector<std::vector<std::string>> out(1);
    for (const auto piece : text::splitWhitespace(text)) {
        const bool ends = piece.ends_with('.') || piece.ends_with('!') || piece.ends_with('?');
        for (auto& w : wordsOf(piece)) out.back().push_back(std::move(w));
        if (ends && !out.back().empty()) out.emplace_back();
    }
    if (out.back().empty()) out.pop_back();
    return out;
}

}  // namespace

Shown Shown::of(Kind kind, const Position& start, std::span<const Move> moves, std::size_t shown,
                PieceLetters letters) {
    Shown out;
    out.kind  = kind;
    out.start = start;
    out.moves.assign(moves.begin(), moves.end());
    out.shown = std::min(shown, moves.size());
    // The boards as far as the material is counted, the texts of the moves shown.
    auto pos = start;
    out.boards.push_back(pos);
    const auto boards = std::min(moves.size(), std::max(out.shown, COUNTED_PLIES));
    for (std::size_t i = 0; i < boards; ++i) {
        if (i < out.shown) out.texts.push_back(forms(moveText(pos, moves[i], letters)));
        pos.playUnchecked(moves[i]);
        out.boards.push_back(pos);
    }
    return out;
}

Lost Lost::of(const MoveReport& move) {
    const auto mover = static_cast<std::size_t>(move.mover);
    Lost out;
    out.before[mover]     = move.win_before < LOST_BEYOND_DOUBT;
    out.before[1 - mover] = move.win_before > 100.0 - LOST_BEYOND_DOUBT;
    out.after[mover]      = move.win_after < LOST_BEYOND_DOUBT;
    out.after[1 - mover]  = move.win_after > 100.0 - LOST_BEYOND_DOUBT;
    return out;
}

Lost Lost::at(std::optional<double> white_expected) {
    Lost out;
    if (!white_expected) return out;
    out.before = {*white_expected < LOST_BEYOND_DOUBT, *white_expected > 100.0 - LOST_BEYOND_DOUBT};
    out.after  = out.before;
    return out;
}

std::optional<Refusal> checkBoards(std::string_view text, const MoveFacts& facts, const Allowed& allowed) {
    if (allowed.lines.empty() || allowed.lines.front().kind != Shown::Kind::Played) return std::nullopt;
    for (const auto& s : sentencesOf(text, allowed)) {
        const auto anchors = anchorsOf(s, allowed);
        if (auto r = checkPieces(s, anchors, allowed)) return r;
        if (auto r = checkStructure(s, anchors, allowed)) return r;
        if (auto r = checkTactics(s, anchors, facts, allowed)) return r;
        if (auto r = checkMaterial(s, anchors, facts, allowed)) return r;
        if (auto r = checkReason(s, anchors, allowed)) return r;
    }
    return std::nullopt;
}

std::optional<Refusal> checkLost(std::string_view text, const Lost& lost) {
    static constexpr std::string_view LOST_WORDS[] = {"lost", "perdu", "perdue", "perdus", "perdues"};
    static constexpr std::string_view STATES[]     = {
        "is",    "was",   "remains", "remained", "stays",     "stayed", "are",     "were",    "est",
        "était", "reste", "restait", "demeure",  "demeurait", "sont",   "étaient", "restent", "restaient"};
    static constexpr std::string_view SUBJECTS[] = {"position", "game", "partie", "white", "black", "blancs", "noirs"};
    static constexpr std::string_view ALREADY[]  = {"already", "déjà"};
    for (const auto& w : plainSentences(text)) {
        for (std::size_t i = 0; i < w.size(); ++i) {
            if (!in(w[i], LOST_WORDS)) continue;
            // "lost anyway", « perdue de toute façon », "already lost": how things stood before the move.
            const bool anyway = (i + 1 < w.size() && w[i + 1] == "anyway") ||
                                (i + 3 < w.size() && w[i + 1] == "de" && w[i + 2] == "toute" && w[i + 3] == "façon") ||
                                (i > 0 && in(w[i - 1], ALREADY));
            // "the position was lost", « la position des Noirs reste perdue », "White is lost", "a lost position".
            std::size_t verb = i;
            if (verb > 0 && in(w[verb - 1], ALREADY)) --verb;
            const bool state    = verb >= 2 && in(w[verb - 1], STATES) && in(w[verb - 2], SUBJECTS);
            const bool position = (i > 0 && w[i - 1] == "position") || (i + 1 < w.size() && w[i + 1] == "position");
            if (!anyway && !state && !position) continue;
            // Whose: the side named nearest before; the position of either when none is.
            std::optional<Color> side;
            for (std::size_t j = i; j-- > 0;) {
                if (w[j] == "white" || w[j] == "blancs") side = Color::White;
                if (w[j] == "black" || w[j] == "noirs") side = Color::Black;
                if (side) break;
            }
            const auto lost_for = [&](Color c) {
                const auto n = static_cast<std::size_t>(c);
                return anyway ? lost.before[n] : lost.before[n] || lost.after[n];
            };
            const bool so = side ? lost_for(*side) : lost_for(Color::White) || lost_for(Color::Black);
            if (so) continue;
            std::string what = w[i];
            if (i + 1 < w.size() && w[i + 1] == "anyway") what += " anyway";
            if (i + 3 < w.size() && w[i + 1] == "de" && w[i + 2] == "toute") what += " de toute façon";
            return Refusal{Refusal::Kind::Evaluation, what};
        }
    }
    return std::nullopt;
}

std::optional<Refusal> checkWording(std::string_view text, Lang lang) {
    // A king is never taken, won or given away: « 42.Dxe8# donne le roi noir ». « Prendre en enfilade » is a skewer.
    static constexpr std::string_view TAKES_EN[] = {"take",     "takes",     "took", "taking", "capture", "captures",
                                                    "captured", "capturing", "win",  "wins",   "won",     "winning"};
    static constexpr std::string_view TAKES_FR[] = {"prend",     "prennent", "prendre", "prenait",  "capture",
                                                    "capturent", "gagne",    "gagnent", "gagner",   "donne",
                                                    "donnent",   "donner",   "donnait", "donnaient"};
    for (const auto& w : plainSentences(text)) {
        const bool skewer = std::any_of(w.begin(), w.end(), [](const std::string& x) { return x == "enfilade"; });
        for (std::size_t i = 0; i + 2 < w.size(); ++i) {
            const bool en = in(w[i], TAKES_EN) && w[i + 1] == "the";
            const bool fr = in(w[i], TAKES_FR) && w[i + 1] == "le" && !skewer;
            if (!en && !fr) continue;
            std::size_t k = i + 2;
            if (k + 1 < w.size() && colourOf(w[k])) ++k;
            if (w[k] != (en ? "king" : "roi")) continue;
            if (k + 1 < w.size() && w[k + 1] == "s") continue;  // "the king's pawn"
            return Refusal{Refusal::Kind::Wording, w[i] + " " + w[i + 1] + " " + w[k]};
        }
        // « parier » for « parer »: « hors d'état de parier ».
        if (lang == Lang::Fr && std::any_of(w.begin(), w.end(), [](const std::string& x) { return x == "parier"; }))
            return Refusal{Refusal::Kind::Wording, "parier"};
    }
    return std::nullopt;
}

}  // namespace fcg::commentary::brief_detail
