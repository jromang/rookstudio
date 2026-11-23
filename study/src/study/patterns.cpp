#include "patterns.hpp"

#include <algorithm>
#include <bit>
#include <cctype>
#include <charconv>
#include <limits>

namespace fcg::study {

namespace {

inline uint64_t bit(int square) noexcept { return uint64_t{1} << square; }
inline int lowest(uint64_t b) noexcept { return __builtin_ctzll(b); }
inline int kindOn(const ScanBoard& b, int square) noexcept { return (b.on(square) - 1) % 6; }
inline int sideOn(const ScanBoard& b, int square) noexcept { return (b.on(square) - 1) / 6; }

inline uint64_t rookRays(int square, uint64_t occupied) noexcept {
    return chess::attacks::rook(chess::Square(square), chess::Bitboard(occupied)).getBits();
}
inline uint64_t bishopRays(int square, uint64_t occupied) noexcept {
    return chess::attacks::bishop(chess::Square(square), chess::Bitboard(occupied)).getBits();
}

// The squares a man on `square` attacks.
uint64_t attacksOf(const ScanBoard& b, int square) noexcept {
    const auto sq = chess::Square(square);
    switch (kindOn(b, square)) {
        case PAWN:
            return chess::attacks::pawn(sideOn(b, square) == WHITE_SIDE ? chess::Color::WHITE : chess::Color::BLACK, sq)
                .getBits();
        case KNIGHT:
            return chess::attacks::knight(sq).getBits();
        case BISHOP:
            return bishopRays(square, b.occupied());
        case ROOK:
            return rookRays(square, b.occupied());
        case QUEEN:
            return bishopRays(square, b.occupied()) | rookRays(square, b.occupied());
        default:
            return chess::attacks::king(sq).getBits();
    }
}

uint64_t sideMen(const ScanBoard& b, int side) noexcept {
    uint64_t men = 0;
    for (int kind = PAWN; kind <= KING; ++kind) men |= b.pieces(side, kind);
    return men;
}

// The squares strictly between two squares on a line, none when they are not on one.
const std::array<std::array<uint64_t, 64>, 64>& betweenTable() {
    static const auto table = [] {
        std::array<std::array<uint64_t, 64>, 64> t{};
        for (int a = 0; a < 64; ++a)
            for (int b = 0; b < 64; ++b) {
                if (a == b) continue;
                if (rookRays(a, 0) & bit(b))
                    t[a][b] = rookRays(a, bit(b)) & rookRays(b, bit(a));
                else if (bishopRays(a, 0) & bit(b))
                    t[a][b] = bishopRays(a, bit(b)) & bishopRays(b, bit(a));
            }
        return t;
    }();
    return table;
}

constexpr int VALUE[6] = {1, 3, 3, 5, 9, 100};

// The squares in front of a pawn of `side` on `square`, on its file and those beside it.
uint64_t frontSpan(int side, int square) noexcept {
    const int file = square & 7, rank = square >> 3;
    uint64_t span = 0;
    for (int r = side == WHITE_SIDE ? rank + 1 : rank - 1; r >= 0 && r < 8; r += side == WHITE_SIDE ? 1 : -1)
        for (int f = std::max(0, file - 1); f <= std::min(7, file + 1); ++f) span |= bit(r * 8 + f);
    return span;
}

// ---------------------------------------------------------------------------------------------------------------
// Words

constexpr std::string_view LETTERS = "PNBRQKpnbrqk";

int letterBit(char c) noexcept {
    const auto at = LETTERS.find(c);
    if (at != std::string_view::npos) return static_cast<int>(at);
    return -1;
}

const std::vector<std::string_view>& keywords() {
    static const std::vector<std::string_view> words{"piece",   "in",   "attacks",   "defends", "pin",     "from",
                                                     "through", "to",   "en",        "prise",   "enprise", "passed",
                                                     "check",   "mate", "stalemate", "wtm",     "btm",     "not",
                                                     "or",      "only", "line",      "move",    "capture", "promote"};
    return words;
}

bool keyword(std::string_view w) {
    const auto& k = keywords();
    return std::find(k.begin(), k.end(), w) != k.end();
}

// CQL's words that the language does not read: told as not understood rather than unknown.
constexpr std::string_view CQL_WORDS[] = {"abs",
                                          "alldistinct",
                                          "ancestor",
                                          "and",
                                          "anydirection",
                                          "ascii",
                                          "assert",
                                          "atomic",
                                          "attackedby",
                                          "attackarrow",
                                          "between",
                                          "black",
                                          "castle",
                                          "child",
                                          "colortype",
                                          "comment",
                                          "connectedpawns",
                                          "consecutivemoves",
                                          "count",
                                          "cql",
                                          "currentmove",
                                          "currentposition",
                                          "dark",
                                          "depth",
                                          "descendant",
                                          "diagonal",
                                          "dictionary",
                                          "distance",
                                          "doubledpawns",
                                          "down",
                                          "echo",
                                          "eco",
                                          "elo",
                                          "else",
                                          "event",
                                          "false",
                                          "file",
                                          "find",
                                          "fen",
                                          "flip",
                                          "flipcolor",
                                          "fliphorizontal",
                                          "flipvertical",
                                          "function",
                                          "gamenumber",
                                          "hascomment",
                                          "horizontal",
                                          "if",
                                          "initial",
                                          "int",
                                          "isolatedpawns",
                                          "isbound",
                                          "isunbound",
                                          "lastgamenumber",
                                          "lca",
                                          "left",
                                          "legal",
                                          "light",
                                          "local",
                                          "lowercase",
                                          "mainline",
                                          "makesquare",
                                          "max",
                                          "message",
                                          "min",
                                          "modelmate",
                                          "movenumber",
                                          "mutual",
                                          "notransform",
                                          "null",
                                          "orthogonal",
                                          "parent",
                                          "passedpawns",
                                          "path",
                                          "persistent",
                                          "pgn",
                                          "pieceid",
                                          "player",
                                          "ply",
                                          "position",
                                          "positionid",
                                          "power",
                                          "previous",
                                          "primary",
                                          "pseudolegal",
                                          "puremate",
                                          "purestalemate",
                                          "quiet",
                                          "rank",
                                          "ray",
                                          "readfile",
                                          "result",
                                          "reversecolor",
                                          "right",
                                          "rotate45",
                                          "rotate90",
                                          "secondary",
                                          "settag",
                                          "shift",
                                          "shifthorizontal",
                                          "shiftvertical",
                                          "sidetomove",
                                          "singlecolor",
                                          "site",
                                          "sort",
                                          "sqrt",
                                          "square",
                                          "str",
                                          "string",
                                          "then",
                                          "true",
                                          "type",
                                          "typename",
                                          "up",
                                          "variation",
                                          "variations",
                                          "vertical",
                                          "virtualmainline",
                                          "white",
                                          "writefile",
                                          "xray",
                                          "year",
                                          "firstmatch",
                                          "lastposition",
                                          "nestban",
                                          "flipcolor"};

// Damerau-Levenshtein's distance (adjacent letters swapped count one).
int distance(std::string_view a, std::string_view b) {
    std::vector<std::vector<int>> d(a.size() + 1, std::vector<int>(b.size() + 1));
    for (std::size_t i = 0; i <= a.size(); ++i) d[i][0] = static_cast<int>(i);
    for (std::size_t j = 0; j <= b.size(); ++j) d[0][j] = static_cast<int>(j);
    for (std::size_t i = 1; i <= a.size(); ++i)
        for (std::size_t j = 1; j <= b.size(); ++j) {
            const int cost = a[i - 1] == b[j - 1] ? 0 : 1;
            d[i][j]        = std::min({d[i - 1][j] + 1, d[i][j - 1] + 1, d[i - 1][j - 1] + cost});
            if (i > 1 && j > 1 && a[i - 1] == b[j - 2] && a[i - 2] == b[j - 1])
                d[i][j] = std::min(d[i][j], d[i - 2][j - 2] + 1);
        }
    return d[a.size()][b.size()];
}

std::string lower(std::string_view w) {
    std::string out(w);
    for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

// The word of the language a mistyped word looks like: one letter off for a short word, two for a longer one.
std::string suggestionFor(std::string_view word) {
    const auto w   = lower(word);
    const int most = w.size() <= 4 ? 1 : 2;
    int best       = most + 1;
    std::string_view found;
    for (const auto k : keywords()) {
        if (k == "en" || k == "prise") continue;
        const int d = distance(w, k);
        if (d < best) {
            best  = d;
            found = k;
        }
    }
    if (w == "en prise") return "en prise";
    return best <= most ? std::string(found) : std::string();
}

// ---------------------------------------------------------------------------------------------------------------
// Squares and men

// "e4", "a-h2", "c1-8", "a-c1-3": the squares of one item.
std::optional<uint64_t> squareItem(std::string_view s) {
    const auto file = [](char c) { return c >= 'a' && c <= 'h'; };
    const auto rank = [](char c) { return c >= '1' && c <= '8'; };
    std::size_t at  = 0;
    if (at >= s.size() || !file(s[at])) return std::nullopt;
    int f1 = s[at] - 'a', f2 = f1;
    ++at;
    if (at + 1 < s.size() && s[at] == '-' && file(s[at + 1])) {
        f2 = s[at + 1] - 'a';
        at += 2;
    }
    if (at >= s.size() || !rank(s[at])) return std::nullopt;
    int r1 = s[at] - '1', r2 = r1;
    ++at;
    if (at + 1 < s.size() && s[at] == '-' && rank(s[at + 1])) {
        r2 = s[at + 1] - '1';
        at += 2;
    }
    if (at != s.size() || f1 > f2 || r1 > r2) return std::nullopt;
    uint64_t out = 0;
    for (int r = r1; r <= r2; ++r)
        for (int f = f1; f <= f2; ++f) out |= bit(r * 8 + f);
    return out;
}

// Squares: one item, or several in brackets ("[a1,h8]", "[a-h7]").
std::optional<uint64_t> squareSet(std::string_view s) {
    if (s.empty()) return std::nullopt;
    if (s.front() != '[') return squareItem(s);
    if (s.back() != ']' || s.size() < 3) return std::nullopt;
    s            = s.substr(1, s.size() - 2);
    uint64_t out = 0;
    while (!s.empty()) {
        const auto comma = s.find(',');
        const auto item  = squareItem(s.substr(0, comma));
        if (!item) return std::nullopt;
        out |= *item;
        if (comma == std::string_view::npos) break;
        s = s.substr(comma + 1);
        if (s.empty()) return std::nullopt;
    }
    return out;
}

// One letter of men (K … p, A, a, _): its bits.
uint16_t letterMen(char c) noexcept {
    if (c == 'A') return Men::WHITE;
    if (c == 'a') return Men::BLACK;
    if (c == '_') return Men::EMPTY;
    const int b = letterBit(c);
    return b < 0 ? 0 : static_cast<uint16_t>(1u << b);
}

enum class MenRead { Ok, NotMen, BadSquares };

MenRead readMen(std::string_view w, Men& men) {
    men = Men{};
    if (w.empty()) return MenRead::NotMen;
    if (w.front() == '[') {
        const auto close = w.find(']');
        if (close == std::string_view::npos) return MenRead::BadSquares;
        const auto inside    = w.substr(1, close - 1);
        const bool has_digit = std::any_of(inside.begin(), inside.end(), [](char c) { return c >= '0' && c <= '9'; });
        if (has_digit) {  // squares alone
            if (close + 1 != w.size()) return MenRead::BadSquares;
            const auto s = squareSet(w);
            if (!s) return MenRead::BadSquares;
            men.squares = *s;
            return MenRead::Ok;
        }
        if (inside.empty()) return MenRead::NotMen;
        for (const char c : inside) {
            const auto m = letterMen(c);
            if (!m) return MenRead::NotMen;
            men.pieces |= m;
        }
        const auto rest = w.substr(close + 1);
        if (rest.empty()) return MenRead::Ok;
        const auto s = squareSet(rest);
        if (!s) return MenRead::BadSquares;
        men.squares = *s;
        return MenRead::Ok;
    }
    if (const auto s = squareSet(w)) {  // squares alone: "h7"
        men.squares = *s;
        return MenRead::Ok;
    }
    const auto m = letterMen(w.front());
    if (!m) return MenRead::NotMen;
    const auto rest = w.substr(1);
    if (rest.empty()) {
        men.pieces = m;
        return MenRead::Ok;
    }
    const bool squarish = (rest.front() == '[' || std::isalpha(static_cast<unsigned char>(rest.front()))) &&
                          std::any_of(rest.begin(), rest.end(), [](char c) { return c >= '0' && c <= '9'; });
    const auto s        = squareSet(rest);
    if (!s) return squarish ? MenRead::BadSquares : MenRead::NotMen;
    men.pieces  = m;
    men.squares = *s;
    return MenRead::Ok;
}

// ---------------------------------------------------------------------------------------------------------------
// Tokens

struct Token {
    enum Kind { Word, Arrow, Gap, Bad } kind = Word;
    std::string text;
};

struct LineTokens {
    std::vector<Token> tokens;
    std::string comment;
    bool has_comment = false;
};

bool wordChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '$' || c == '-' || c == ',';
}

LineTokens tokenize(std::string_view line) {
    LineTokens out;
    std::size_t i = 0;
    while (i < line.size()) {
        const char c = line[i];
        if (c == ' ' || c == '\t' || c == '\r') {
            ++i;
            continue;
        }
        if (c == ';' || (c == '/' && i + 1 < line.size() && line[i + 1] == '/')) {
            auto text = line.substr(i + (c == ';' ? 1 : 2));
            while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) text.remove_prefix(1);
            while (!text.empty() && (text.back() == ' ' || text.back() == '\r' || text.back() == '\t'))
                text.remove_suffix(1);
            out.comment     = std::string(text);
            out.has_comment = true;
            break;
        }
        if (line.substr(i, 3) == "-->") {
            out.tokens.push_back({Token::Arrow, "-->"});
            i += 3;
            continue;
        }
        if (c == '.') {
            std::size_t j = i + 1;
            if (j < line.size() && line[j] == '{') {
                const auto close = line.find('}', j);
                j                = close == std::string_view::npos ? line.size() : close + 1;
            } else if (j < line.size() && (line[j] == '*' || line[j] == '+' || line[j] == '?')) {
                ++j;
            }
            out.tokens.push_back({Token::Gap, std::string(line.substr(i, j - i))});
            i = j;
            continue;
        }
        if (wordChar(c) || c == '[') {
            std::size_t j = i;
            while (j < line.size()) {
                if (line[j] == '[') {
                    const auto close = line.find(']', j);
                    j                = close == std::string_view::npos ? line.size() : close + 1;
                    continue;
                }
                if (line.substr(j, 3) == "-->") break;
                if (!wordChar(line[j]) || line[j] == ',') break;
                ++j;
            }
            if (j == i) ++j;
            out.tokens.push_back({Token::Word, std::string(line.substr(i, j - i))});
            i = j;
            continue;
        }
        // CQL's other signs: braces, parentheses, & | # = < > ~ " …
        std::size_t j = i + 1;
        while (j < line.size() && !wordChar(line[j]) && line[j] != ' ' && line[j] != '[' && line[j] != ';' &&
               line[j] != '.')
            ++j;
        out.tokens.push_back({Token::Bad, std::string(line.substr(i, j - i))});
        i = j;
    }
    return out;
}

// ---------------------------------------------------------------------------------------------------------------
// The reader

struct Stop {
    PatternError error;
};

[[noreturn]] void stop(PatternErrorKind kind, int line, std::string word, std::string suggestion = {}) {
    throw Stop{PatternError{kind, line, std::move(word), std::move(suggestion)}};
}

Men defaultSliders() {
    Men m;
    m.pieces =
        static_cast<uint16_t>(Men::man(WHITE_SIDE, BISHOP) | Men::man(WHITE_SIDE, ROOK) | Men::man(WHITE_SIDE, QUEEN) |
                              Men::man(BLACK_SIDE, BISHOP) | Men::man(BLACK_SIDE, ROOK) | Men::man(BLACK_SIDE, QUEEN));
    return m;
}
Men defaultPinned() {
    Men m;
    m.pieces =
        static_cast<uint16_t>((Men::WHITE | Men::BLACK) & ~(Men::man(WHITE_SIDE, KING) | Men::man(BLACK_SIDE, KING)));
    return m;
}
Men defaultKings() {
    Men m;
    m.pieces = static_cast<uint16_t>(Men::man(WHITE_SIDE, KING) | Men::man(BLACK_SIDE, KING));
    return m;
}

class Reader {
   public:
    Reader(std::vector<std::string_view> lines, std::vector<std::string> names) : names_(std::move(names)) {
        for (auto l : lines) lines_.push_back(tokenize(l));
    }

    Pattern read() {
        bool started   = false;
        bool line_open = false;
        bool line_seen = false;
        std::optional<std::pair<uint32_t, uint32_t>> gap;
        int gap_line = 0;
        std::vector<std::string> declared;
        for (std::size_t n = 0; n < lines_.size(); ++n) {
            line_          = static_cast<int>(n) + 1;
            const auto& lt = lines_[n];
            toks_          = &lt.tokens;
            pos_           = 0;
            if (toks_->empty()) {
                if (lt.has_comment && !started) pattern_.title.push_back(lt.comment);
                continue;
            }
            started           = true;
            const auto& first = (*toks_)[0];
            const auto before = pattern_.conditions.size();
            if (first.kind == Token::Arrow) {
                if (!line_open) stop(PatternErrorKind::StepWithoutLine, line_, "-->");
                steps(gap, gap_line);
                continue;
            }
            line_open = false;
            if (first.kind == Token::Word && first.text == "line") {
                if (line_seen) stop(PatternErrorKind::SecondLine, line_, "line");
                line_seen = true;
                line_open = true;
                ++pos_;
                if (pos_ >= toks_->size()) stop(PatternErrorKind::EmptyLine, line_, "line");
                steps(gap, gap_line);
                continue;
            }
            if (first.kind == Token::Word && first.text == "piece") {
                declaration(declared);
                continue;
            }
            if (first.kind == Token::Word && first.text == "only") {
                ++pos_;
                Men men;
                if (pos_ >= toks_->size() || (*toks_)[pos_].kind != Token::Word ||
                    readMen((*toks_)[pos_].text, men) != MenRead::Ok || !men.anywhere() ||
                    (men.pieces != Men::man(WHITE_SIDE, PAWN) && men.pieces != Men::man(BLACK_SIDE, PAWN)))
                    stop(PatternErrorKind::NeedPawns, line_, "only");
                Condition c;
                c.relation = Relation::OnlyPawns;
                c.side     = men.pieces == Men::man(WHITE_SIDE, PAWN) ? WHITE_SIDE : BLACK_SIDE;
                c.line     = line_;
                ++pos_;
                endOfLine();
                pattern_.conditions.push_back(std::move(c));
            } else {
                auto found = conjunction(toks_->size());
                for (auto& c : found) pattern_.conditions.push_back(std::move(c));
            }
            if (lt.has_comment && pattern_.conditions.size() > before) pattern_.conditions.back().comment = lt.comment;
        }
        if (gap) stop(PatternErrorKind::GapAtEnd, gap_line, ".");
        return std::move(pattern_);
    }

   private:
    const Token* at() const { return pos_ < toks_->size() ? &(*toks_)[pos_] : nullptr; }
    bool isWord(std::string_view w) const {
        const auto* t = at();
        return t && t->kind == Token::Word && t->text == w;
    }

    void endOfLine() {
        if (const auto* t = at()) wrong(*t, PatternErrorKind::Unexpected);
    }

    // The error for a token that is not what is wanted: what kind of word it is, else `otherwise`.
    [[noreturn]] void wrong(const Token& t, PatternErrorKind otherwise) {
        if (t.kind == Token::Bad) stop(PatternErrorKind::NotUnderstood, line_, t.text);
        if (t.kind == Token::Gap) stop(otherwise, line_, t.text);
        if (t.kind == Token::Arrow) stop(otherwise, line_, t.text);
        if (keyword(t.text) || t.text.front() == '$') stop(otherwise, line_, t.text);
        Men men;
        const auto r = readMen(t.text, men);
        if (r == MenRead::Ok) stop(otherwise, line_, t.text);
        if (cqlWord(lower(t.text))) stop(PatternErrorKind::NotUnderstood, line_, t.text);
        if (r == MenRead::BadSquares) stop(PatternErrorKind::BadSquares, line_, t.text);
        stop(PatternErrorKind::UnknownWord, line_, t.text, suggestionFor(t.text));
    }

    void declaration(std::vector<std::string>& declared) {
        ++pos_;
        const auto* name = at();
        if (!name || name->kind != Token::Word || name->text.size() < 2 || name->text.front() != '$')
            stop(PatternErrorKind::NeedName, line_, "piece");
        ++pos_;
        if (!isWord("in")) stop(PatternErrorKind::NeedName, line_, "piece");
        ++pos_;
        const auto* m = at();
        Men men;
        if (!m || m->kind != Token::Word || readMen(m->text, men) != MenRead::Ok || men.pieces == 0) {
            if (m && m->kind == Token::Word && !keyword(m->text) && readMen(m->text, men) != MenRead::Ok)
                wrong(*m, PatternErrorKind::NeedMen);
            stop(PatternErrorKind::NeedMen, line_, "in");
        }
        ++pos_;
        endOfLine();
        const auto bare = name->text.substr(1);
        if (std::find(declared.begin(), declared.end(), bare) != declared.end())
            stop(PatternErrorKind::NamedTwice, line_, name->text);
        declared.push_back(bare);
        if (declared.size() > Pattern::MAX_VARIABLES) stop(PatternErrorKind::TooManyNames, line_, name->text);
        Variable v;
        v.name = bare;
        v.men  = men;
        v.line = line_;
        pattern_.variables.push_back(std::move(v));
    }

    int variableIndex(const std::string& word) {
        const auto bare = word.substr(1);
        for (std::size_t i = 0; i < names_.size(); ++i)
            if (names_[i] == bare) return static_cast<int>(i);
        stop(PatternErrorKind::NotNamed, line_, word);
    }

    // Men or a name, if the token is one; nothing otherwise.
    std::optional<Subject> maybeSubject() {
        const auto* t = at();
        if (!t || t->kind != Token::Word || keyword(t->text)) return std::nullopt;
        if (t->text.front() == '$') {
            Subject s;
            s.variable = variableIndex(t->text);
            ++pos_;
            return s;
        }
        Men men;
        if (readMen(t->text, men) != MenRead::Ok) return std::nullopt;
        ++pos_;
        return Subject{men, -1};
    }

    Subject subjectAfter(const std::string& after) {
        if (auto s = maybeSubject()) return *s;
        const auto* t = at();
        if (!t || t->kind == Token::Arrow || (t->kind == Token::Word && keyword(t->text)))
            stop(PatternErrorKind::NeedMen, line_, after);
        wrong(*t, PatternErrorKind::NeedMen);
    }

    std::vector<Condition> conjunction(std::size_t end) {
        std::vector<Condition> out;
        while (pos_ < end && (*toks_)[pos_].kind != Token::Arrow) out.push_back(disjunction(end));
        return out;
    }

    Condition disjunction(std::size_t end) {
        auto c = unary(end);
        while (pos_ < end && isWord("or")) {
            ++pos_;
            if (pos_ >= end || (*toks_)[pos_].kind == Token::Arrow) stop(PatternErrorKind::NeedCondition, line_, "or");
            auto alt  = unary(end);
            auto more = std::move(alt.alternatives);
            alt.alternatives.clear();
            c.alternatives.push_back(std::move(alt));
            for (auto& m : more) c.alternatives.push_back(std::move(m));
        }
        return c;
    }

    Condition unary(std::size_t end) {
        if (isWord("not")) {
            ++pos_;
            if (pos_ >= end || (*toks_)[pos_].kind == Token::Arrow) stop(PatternErrorKind::NeedCondition, line_, "not");
            auto c    = unary(end);
            c.negated = !c.negated;
            return c;
        }
        return atom(end);
    }

    Condition atom(std::size_t end) {
        const auto* t = at();
        Condition c;
        c.line = line_;
        if (!t) stop(PatternErrorKind::NeedCondition, line_, "");
        if (t->kind == Token::Word) {
            const auto& w     = t->text;
            const auto simple = [&](Relation r) {
                c.relation = r;
                ++pos_;
                return c;
            };
            if (w == "check") return simple(Relation::Check);
            if (w == "mate") return simple(Relation::Mate);
            if (w == "stalemate") return simple(Relation::Stalemate);
            if (w == "wtm") return simple(Relation::WhiteToMove);
            if (w == "btm") return simple(Relation::BlackToMove);
            if (w == "pin") {
                ++pos_;
                c.relation = Relation::Pins;
                c.a.men    = defaultSliders();
                c.b.men    = defaultPinned();
                c.c.men    = defaultKings();
                bool from = false, through = false, to = false;
                while (pos_ < end) {
                    if (isWord("from") && !from) {
                        ++pos_;
                        c.a  = subjectAfter("from");
                        from = true;
                    } else if (isWord("through") && !through) {
                        ++pos_;
                        c.b     = subjectAfter("through");
                        through = true;
                    } else if (isWord("to") && !to) {
                        ++pos_;
                        c.c = subjectAfter("to");
                        to  = true;
                    } else {
                        break;
                    }
                }
                return c;
            }
            if (auto s = maybeSubject()) {
                c.a = *s;
                if (pos_ < end && (isWord("attacks") || isWord("defends"))) {
                    const auto word = (*toks_)[pos_].text;
                    c.relation      = word == "attacks" ? Relation::Attacks : Relation::Defends;
                    ++pos_;
                    c.b = subjectAfter(word);
                    return c;
                }
                if (pos_ < end && isWord("en")) {
                    ++pos_;
                    if (!isWord("prise")) {
                        if (const auto* n = at()) stop(PatternErrorKind::UnknownWord, line_, n->text, "prise");
                        stop(PatternErrorKind::NeedCondition, line_, "en");
                    }
                    ++pos_;
                    c.relation = Relation::EnPrise;
                    return c;
                }
                if (pos_ < end && isWord("enprise")) {
                    ++pos_;
                    c.relation = Relation::EnPrise;
                    return c;
                }
                if (pos_ < end && isWord("passed")) {
                    ++pos_;
                    c.relation = Relation::Passed;
                    return c;
                }
                // Squares alone are no condition: what stands there must be said.
                if (c.a.variable < 0 && c.a.men.pieces == 0) stop(PatternErrorKind::NeedCondition, line_, w);
                c.relation = Relation::On;
                return c;
            }
            if (keyword(w)) stop(PatternErrorKind::NeedCondition, line_, w);
        }
        wrong(*t, PatternErrorKind::NeedCondition);
    }

    // The steps of a line from an arrow on, as far as the text line goes.
    void steps(std::optional<std::pair<uint32_t, uint32_t>>& gap, int& gap_line) {
        const auto n = toks_->size();
        while (pos_ < n) {
            const auto& arrow = (*toks_)[pos_];
            if (arrow.kind != Token::Arrow) wrong(arrow, PatternErrorKind::Unexpected);
            ++pos_;
            if (pos_ >= n) stop(PatternErrorKind::EmptyLine, line_, "-->");
            std::size_t end = pos_;
            while (end < n && (*toks_)[end].kind != Token::Arrow) ++end;
            const auto& t = (*toks_)[pos_];
            if (t.kind == Token::Gap) {
                const auto g = readGap(t.text);
                if (!g) {
                    if (t.text.size() == 2 && (t.text[1] == '*' || t.text[1] == '+' || t.text[1] == '?'))
                        stop(PatternErrorKind::NotUnderstood, line_, t.text);
                    stop(PatternErrorKind::BadGap, line_, t.text);
                }
                if (!gap) gap = std::pair<uint32_t, uint32_t>{0, 0};
                gap->first += g->first;
                gap->second += g->second;
                if (gap->second > Pattern::MAX_GAP) stop(PatternErrorKind::BadGap, line_, t.text);
                gap_line = line_;
                ++pos_;
                if (pos_ < end) wrong((*toks_)[pos_], PatternErrorKind::Unexpected);
                continue;
            }
            Step step;
            step.line = line_;
            if (gap) {
                step.min_gap = gap->first;
                step.max_gap = gap->second;
                gap.reset();
            }
            if (isWord("move")) {
                ++pos_;
                PatternMove m;
                bool from = false, to = false, capture = false, promote = false;
                while (pos_ < end) {
                    if (isWord("from") && !from) {
                        ++pos_;
                        m.from = subjectAfter("from");
                        from   = true;
                    } else if (isWord("to") && !to) {
                        ++pos_;
                        m.to = subjectAfter("to");
                        to   = true;
                    } else if (isWord("capture") && !capture) {
                        ++pos_;
                        m.captures = true;
                        capture    = true;
                        if (pos_ < end) m.captured = maybeSubject();
                    } else if (isWord("promote") && !promote) {
                        ++pos_;
                        Men men;
                        const auto* k    = at();
                        const auto kinds = Men::man(WHITE_SIDE, KNIGHT) | Men::man(WHITE_SIDE, BISHOP) |
                                           Men::man(WHITE_SIDE, ROOK) | Men::man(WHITE_SIDE, QUEEN);
                        if (!k || k->kind != Token::Word || readMen(k->text, men) != MenRead::Ok || !men.anywhere() ||
                            std::popcount(men.pieces) != 1)
                            stop(PatternErrorKind::NeedKind, line_, "promote");
                        const int b = std::countr_zero(men.pieces) % 6;
                        if (!(Men::man(WHITE_SIDE, b) & kinds)) stop(PatternErrorKind::NeedKind, line_, "promote");
                        m.promote = b;
                        promote   = true;
                        ++pos_;
                    } else {
                        break;
                    }
                }
                step.move = std::move(m);
            }
            step.conditions = conjunction(end);
            if (!step.move && step.conditions.empty()) stop(PatternErrorKind::EmptyLine, line_, "-->");
            if (pattern_.line.size() >= Pattern::MAX_STEPS) stop(PatternErrorKind::TooManySteps, line_, "-->");
            pattern_.line.push_back(std::move(step));
        }
    }

    static std::optional<std::pair<uint32_t, uint32_t>> readGap(std::string_view g) {
        if (g == ".") return std::pair<uint32_t, uint32_t>{1, 1};
        if (g.size() < 4 || g[1] != '{' || g.back() != '}') return std::nullopt;
        const auto inside = g.substr(2, g.size() - 3);
        const auto number = [](std::string_view s, uint32_t& v) {
            if (s.empty()) return false;
            const auto r = std::from_chars(s.data(), s.data() + s.size(), v);
            return r.ec == std::errc{} && r.ptr == s.data() + s.size();
        };
        const auto comma = inside.find(',');
        uint32_t lo = 0, hi = 0;
        if (comma == std::string_view::npos) {
            if (!number(inside, lo)) return std::nullopt;
            hi = lo;
        } else {
            const auto a = inside.substr(0, comma), b = inside.substr(comma + 1);
            if (!a.empty() && !number(a, lo)) return std::nullopt;
            if (!number(b, hi)) return std::nullopt;
        }
        if (lo > hi || hi > Pattern::MAX_GAP) return std::nullopt;
        return std::pair{lo, hi};
    }

    std::vector<LineTokens> lines_;
    std::vector<std::string> names_;
    const std::vector<Token>* toks_ = nullptr;
    std::size_t pos_                = 0;
    int line_                       = 0;
    Pattern pattern_;
};

// ---------------------------------------------------------------------------------------------------------------
// The writer

char letterOf(int b) { return b == 12 ? '_' : LETTERS[static_cast<std::size_t>(b)]; }

std::string squaresOf(uint64_t squares) {
    if (std::popcount(squares) == 1) {
        const int s = lowest(squares);
        return std::string{static_cast<char>('a' + (s & 7)), static_cast<char>('1' + (s >> 3))};
    }
    // Runs of files on each rank, joined over ranks that have the same.
    struct Item {
        int f1, f2, r1, r2;
    };
    std::vector<Item> items;
    for (int r = 0; r < 8; ++r) {
        for (int f = 0; f < 8;) {
            if (!(squares & bit(r * 8 + f))) {
                ++f;
                continue;
            }
            int g = f;
            while (g + 1 < 8 && (squares & bit(r * 8 + g + 1))) ++g;
            bool joined = false;
            for (auto& it : items)
                if (it.f1 == f && it.f2 == g && it.r2 == r - 1) {
                    it.r2  = r;
                    joined = true;
                    break;
                }
            if (!joined) items.push_back({f, g, r, r});
            f = g + 1;
        }
    }
    std::string out = "[";
    for (std::size_t i = 0; i < items.size(); ++i) {
        const auto& it = items[i];
        if (i) out += ',';
        out += static_cast<char>('a' + it.f1);
        if (it.f2 != it.f1) out += std::string("-") + static_cast<char>('a' + it.f2);
        out += static_cast<char>('1' + it.r1);
        if (it.r2 != it.r1) out += std::string("-") + static_cast<char>('1' + it.r2);
    }
    return out + "]";
}

std::string gapText(uint32_t lo, uint32_t hi) {
    if (lo == 1 && hi == 1) return ".";
    if (lo == hi) return ".{" + std::to_string(lo) + "}";
    return ".{" + std::to_string(lo) + "," + std::to_string(hi) + "}";
}

std::string subjectText(const Pattern& p, const Subject& s) {
    if (s.variable >= 0 && static_cast<std::size_t>(s.variable) < p.variables.size())
        return "$" + p.variables[static_cast<std::size_t>(s.variable)].name;
    return menText(s.men);
}

std::string body(const Pattern& p, const Condition& c) {
    switch (c.relation) {
        case Relation::On:
            return subjectText(p, c.a);
        case Relation::Attacks:
            return subjectText(p, c.a) + " attacks " + subjectText(p, c.b);
        case Relation::Defends:
            return subjectText(p, c.a) + " defends " + subjectText(p, c.b);
        case Relation::Pins: {
            std::string s = "pin";
            if (!(c.a == Subject{defaultSliders(), -1})) s += " from " + subjectText(p, c.a);
            if (!(c.b == Subject{defaultPinned(), -1})) s += " through " + subjectText(p, c.b);
            if (!(c.c == Subject{defaultKings(), -1})) s += " to " + subjectText(p, c.c);
            return s;
        }
        case Relation::EnPrise:
            return subjectText(p, c.a) + " en prise";
        case Relation::Passed:
            return subjectText(p, c.a) + " passed";
        case Relation::Check:
            return "check";
        case Relation::Mate:
            return "mate";
        case Relation::Stalemate:
            return "stalemate";
        case Relation::WhiteToMove:
            return "wtm";
        case Relation::BlackToMove:
            return "btm";
        case Relation::OnlyPawns:
            return c.side == WHITE_SIDE ? "only P" : "only p";
    }
    return {};
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------------

const std::vector<std::string_view>& patternWords() { return keywords(); }

std::optional<uint64_t> readSquares(std::string_view text) {
    std::string t;
    for (const char c : text)
        if (c != ' ' && c != '\t') t += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (t.empty()) return std::nullopt;
    if (t.front() != '[' && t.find(',') != std::string::npos) t = "[" + t + "]";
    return squareSet(t);
}

std::string squaresText(uint64_t squares) { return squaresOf(squares); }

const std::vector<PatternPreset>& patternPresets() {
    static const std::vector<PatternPreset> presets{
        {"knight-fork", "piece $n in N\n$n attacks k\n$n attacks [qr]\nbtm\n"},
        {"pin-on-king", "pin from [QRB] through [qrbn] to k\n"},
        {"back-rank-mate", "k[a-h8]\n[QR][a-h8] attacks k\nmate\n"},
        {"queen-double-attack", "piece $q in Q\npiece $x in [rbn]\n$q attacks k\n$q attacks $x\nnot a defends $x\n"},
        {"h7-sacrifice",
         "Bd3 Nf3 Qd1 Pe5 kg8 ph7 _f6\nline --> move from B to h7 capture check\n     --> move from k to h7 capture\n"
         "     --> move from N to g5 check\n"},
        {"protected-passed-pawn", "piece $p in P[a-h4-7]\n$p passed\nP defends $p\n"},
    };
    return presets;
}

bool cqlWord(std::string_view word) {
    if (keyword(word)) return false;
    return std::find(std::begin(CQL_WORDS), std::end(CQL_WORDS), word) != std::end(CQL_WORDS);
}

std::string menText(const Men& men) {
    std::string out;
    uint16_t pieces = men.pieces;
    if (pieces) {
        std::string letters;
        if ((pieces & Men::WHITE) == Men::WHITE) {
            letters += 'A';
            pieces = static_cast<uint16_t>(pieces & ~Men::WHITE);
        }
        if ((pieces & Men::BLACK) == Men::BLACK) {
            letters += 'a';
            pieces = static_cast<uint16_t>(pieces & ~Men::BLACK);
        }
        // Kings first, as a player reads them: K Q R B N P.
        for (const int kind : {KING, QUEEN, ROOK, BISHOP, KNIGHT, PAWN})
            if (pieces & Men::man(WHITE_SIDE, kind)) letters += letterOf(kind);
        for (const int kind : {KING, QUEEN, ROOK, BISHOP, KNIGHT, PAWN})
            if (pieces & Men::man(BLACK_SIDE, kind)) letters += letterOf(6 + kind);
        if (pieces & Men::EMPTY) letters += '_';
        out = letters.size() == 1 ? letters : "[" + letters + "]";
    }
    if (!men.anywhere()) out += squaresOf(men.squares);
    return out;
}

std::string conditionText(const Pattern& p, const Condition& c) {
    std::string s = (c.negated ? "not " : "") + body(p, c);
    for (const auto& alt : c.alternatives) s += " or " + conditionText(p, alt);
    return s;
}

std::string stepText(const Pattern& p, const Step& step) {
    std::string s;
    if (step.move) {
        s             = "move";
        const auto& m = *step.move;
        if (m.from) s += " from " + subjectText(p, *m.from);
        if (m.to) s += " to " + subjectText(p, *m.to);
        if (m.captures) s += " capture" + (m.captured ? " " + subjectText(p, *m.captured) : std::string());
        if (m.promote >= 0) s += std::string(" promote ") + letterOf(m.promote);
    }
    for (const auto& c : step.conditions) s += (s.empty() ? "" : " ") + conditionText(p, c);
    return s;
}

bool manOnSquare(const Condition& c) {
    return c.relation == Relation::On && !c.negated && c.alternatives.empty() && c.a.variable < 0 &&
           std::popcount(c.a.men.pieces) == 1 && std::popcount(c.a.men.squares) == 1;
}

void canonicalize(Pattern& p) {
    const auto order = [](const Condition& c) {
        if (c.relation == Relation::OnlyPawns) return 1 << 20;
        if (!manOnSquare(c)) return 1 << 19;
        const int b    = std::countr_zero(c.a.men.pieces);
        const int rank = b == 12 ? 12 : (b / 6) * 6 + (5 - b % 6);
        return rank * 64 + std::countr_zero(c.a.men.squares);
    };
    std::stable_sort(p.conditions.begin(), p.conditions.end(),
                     [&](const Condition& a, const Condition& b) { return order(a) < order(b); });
}

std::string writePattern(const Pattern& p) {
    std::string out;
    for (const auto& t : p.title) out += "; " + t + "\n";
    for (const auto& v : p.variables) out += "piece $" + v.name + " in " + menText(v.men) + "\n";
    // The men on squares on one line, in their order (canonicalize()).
    std::vector<const Condition*> men;
    for (const auto& c : p.conditions)
        if (manOnSquare(c)) men.push_back(&c);
    if (!men.empty()) {
        std::string line, comment;
        for (const auto* c : men) {
            line += (line.empty() ? "" : " ") + menText(c->a.men);
            if (comment.empty()) comment = c->comment;
        }
        out += line + (comment.empty() ? "" : "   ; " + comment) + "\n";
    }
    for (const auto& c : p.conditions) {
        if (manOnSquare(c) || c.relation == Relation::OnlyPawns) continue;
        out += conditionText(p, c) + (c.comment.empty() ? "" : "   ; " + c.comment) + "\n";
    }
    for (const auto& c : p.conditions)
        if (c.relation == Relation::OnlyPawns) out += conditionText(p, c) + "\n";
    for (std::size_t i = 0; i < p.line.size(); ++i) {
        const auto& step = p.line[i];
        out += i == 0 ? "line --> " : "     --> ";
        if (step.max_gap > 0) out += gapText(step.min_gap, step.max_gap) + " --> ";
        out += stepText(p, step) + "\n";
    }
    return out;
}

PatternRead readPattern(std::string_view text) {
    std::vector<std::string_view> lines;
    for (std::size_t at = 0; at <= text.size();) {
        auto end = text.find('\n', at);
        if (end == std::string_view::npos) end = text.size();
        lines.push_back(text.substr(at, end - at));
        at = end + 1;
    }
    // The names first: a line may name a man that a later line declares.
    std::vector<std::string> names;
    for (const auto l : lines) {
        const auto lt = tokenize(l);
        if (lt.tokens.size() >= 2 && lt.tokens[0].kind == Token::Word && lt.tokens[0].text == "piece" &&
            lt.tokens[1].kind == Token::Word && lt.tokens[1].text.size() > 1 && lt.tokens[1].text.front() == '$') {
            const auto bare = lt.tokens[1].text.substr(1);
            if (std::find(names.begin(), names.end(), bare) == names.end()) names.push_back(bare);
        }
    }
    PatternRead out;
    try {
        Reader reader(lines, names);
        auto pattern = reader.read();
        // The declarations in the order of the first pass, the names' indexes kept.
        std::vector<Variable> ordered;
        for (const auto& n : names)
            for (const auto& v : pattern.variables)
                if (v.name == n) ordered.push_back(v);
        pattern.variables = std::move(ordered);
        canonicalize(pattern);
        out.pattern = std::move(pattern);
    } catch (const Stop& s) {
        out.error = s.error;
    }
    return out;
}

std::optional<Contradiction> contradiction(const Pattern& p) {
    const auto& cs   = p.conditions;
    const auto plain = [](const Condition& c) { return !c.negated && c.alternatives.empty(); };
    // One alone: a pawn on its first or last rank, and nothing else it could be.
    const uint64_t edges = 0xff000000000000ffull;
    for (std::size_t i = 0; i < cs.size(); ++i) {
        const auto& c = cs[i];
        if (plain(c) && c.relation == Relation::On && c.a.variable < 0 && c.a.men.pieces != 0 &&
            (c.a.men.pieces & ~(Men::man(WHITE_SIDE, PAWN) | Men::man(BLACK_SIDE, PAWN))) == 0 &&
            (c.a.men.squares & ~edges) == 0)
            return Contradiction{static_cast<int>(i), -1};
    }
    for (std::size_t i = 0; i < cs.size(); ++i) {
        for (std::size_t j = i + 1; j < cs.size(); ++j) {
            const auto& a = cs[i];
            const auto& b = cs[j];
            const Contradiction both{static_cast<int>(i), static_cast<int>(j)};
            // Two men on one square, or a man and an empty square.
            if (manOnSquare(a) && manOnSquare(b) && a.a.men.squares == b.a.men.squares &&
                a.a.men.pieces != b.a.men.pieces)
                return both;
            // A condition and its contrary.
            if (a.alternatives.empty() && b.alternatives.empty() && a.negated != b.negated &&
                a.relation == b.relation && a.a == b.a && a.b == b.b && a.c == b.c && a.side == b.side)
                return both;
            if (!plain(a) || !plain(b)) {
                const auto r = [](const Condition& c, Relation x) {
                    return c.relation == x && c.negated && c.alternatives.empty();
                };
                // mate or stalemate against "not check" or "check".
                if ((plain(a) && a.relation == Relation::Mate && r(b, Relation::Check)) ||
                    (plain(b) && b.relation == Relation::Mate && r(a, Relation::Check)))
                    return both;
                continue;
            }
            const auto pair = [&](Relation x, Relation y) {
                return (a.relation == x && b.relation == y) || (a.relation == y && b.relation == x);
            };
            if (pair(Relation::WhiteToMove, Relation::BlackToMove) || pair(Relation::Mate, Relation::Stalemate) ||
                pair(Relation::Stalemate, Relation::Check))
                return both;
        }
    }
    return std::nullopt;
}

// ---------------------------------------------------------------------------------------------------------------
// The matcher

namespace {

// The cost of a condition, to look at the cheap ones first.
int cost(const Condition& c) {
    if (!c.alternatives.empty()) return 6;
    switch (c.relation) {
        case Relation::WhiteToMove:
        case Relation::BlackToMove:
            return 0;
        case Relation::On:
        case Relation::OnlyPawns:
            return 1;
        case Relation::Check:
            return 2;
        case Relation::Attacks:
        case Relation::Defends:
        case Relation::Passed:
            return 3;
        case Relation::Pins:
        case Relation::EnPrise:
            return 4;
        default:
            return 5;  // mate, stalemate
    }
}

bool named(const Condition& c) {
    if (c.a.variable >= 0 || c.b.variable >= 0 || c.c.variable >= 0) return true;
    return std::any_of(c.alternatives.begin(), c.alternatives.end(), named);
}

}  // namespace

PatternMatcher::PatternMatcher(Pattern pattern) : pattern_(std::move(pattern)) {
    for (const auto& c : pattern_.conditions) (named(c) ? named_ : unnamed_).push_back(c);
    const auto by_cost = [](const Condition& a, const Condition& b) { return cost(a) < cost(b); };
    std::stable_sort(unnamed_.begin(), unnamed_.end(), by_cost);
    std::stable_sort(named_.begin(), named_.end(), by_cost);
    for (const auto& c : pattern_.conditions) {
        const bool plain = !c.negated && c.alternatives.empty();
        if (manOnSquare(c) && std::countr_zero(c.a.men.pieces) <= 12) {
            const int b = std::countr_zero(c.a.men.pieces);
            if (b == 12)
                needs_.empty |= c.a.men.squares;
            else
                needs_.men[static_cast<std::size_t>(b / 6)][static_cast<std::size_t>(b % 6)] |= c.a.men.squares;
            if (b == PAWN) only_pawns_[WHITE_SIDE] |= c.a.men.squares;
            if (b == 6 + PAWN) only_pawns_[BLACK_SIDE] |= c.a.men.squares;
        }
        if (plain && c.relation == Relation::OnlyPawns)
            needs_.only_these_pawns[static_cast<std::size_t>(c.side)] = true;
        if (c.alternatives.empty() && (c.relation == Relation::WhiteToMove || c.relation == Relation::BlackToMove))
            needs_.side_to_move = (c.relation == Relation::WhiteToMove) != c.negated ? WHITE_SIDE : BLACK_SIDE;
        if (plain && (c.relation == Relation::Mate || c.relation == Relation::Stalemate)) asks_mate_ = true;
    }
}

void PatternMatcher::bindingsOf(const ScanBoard& board, bool last, std::vector<Binding>& out) const {
    constexpr std::size_t MAX = Pattern::MAX_VARIABLES;
    const std::size_t count   = std::min(pattern_.variables.size(), MAX);
    std::array<std::array<int8_t, 64>, MAX> choices{};
    std::array<std::size_t, MAX> n{}, at{};
    for (std::size_t v = 0; v < count; ++v) {
        for (uint64_t left = squares(Subject{pattern_.variables[v].men, -1}, board, Binding{}) & board.occupied(); left;
             left &= left - 1)
            choices[v][n[v]++] = static_cast<int8_t>(lowest(left));
        if (n[v] == 0) return;
    }
    // Every choice in turn, as an odometer turns.
    for (;;) {
        Binding b;
        bool distinct = true;
        for (std::size_t v = 0; v < count; ++v) {
            b.square[v] = choices[v][at[v]];
            for (std::size_t u = 0; u < v; ++u) distinct = distinct && b.square[u] != b.square[v];
        }
        if (distinct && holds(named_, board, b, last)) out.push_back(b);
        std::size_t v = 0;
        for (; v < count; ++v) {
            if (++at[v] < n[v]) break;
            at[v] = 0;
        }
        if (v == count) break;
    }
}

uint64_t PatternMatcher::squares(const Subject& s, const ScanBoard& board, const Binding& binding) const {
    if (s.variable >= 0) {
        const int sq = binding.square[static_cast<std::size_t>(s.variable)];
        return sq >= 0 ? bit(sq) : 0;
    }
    const auto pieces = s.men.pieces;
    if (pieces == 0) return s.men.squares;
    uint64_t found = 0;
    for (int b = 0; b < 12; ++b)
        if (pieces & (1u << b)) found |= board.pieces(b / 6, b % 6);
    if (pieces & Men::EMPTY) found |= ~board.occupied();
    return found & s.men.squares;
}

bool PatternMatcher::holds(const std::vector<Condition>& conditions, const ScanBoard& board, const Binding& binding,
                           bool last) const {
    for (const auto& c : conditions)
        if (!holds(c, board, binding, last)) return false;
    return true;
}

bool PatternMatcher::holds(const Condition& c, const ScanBoard& board, const Binding& binding, bool last) const {
    bool value              = false;
    const uint64_t occupied = board.occupied();
    switch (c.relation) {
        case Relation::On:
            value = squares(c.a, board, binding) != 0;
            break;
        case Relation::Attacks:
        case Relation::Defends: {
            const bool attack     = c.relation == Relation::Attacks;
            const bool bare       = c.b.variable < 0 && c.b.men.pieces == 0;
            const uint64_t wanted = squares(c.b, board, binding);
            for (uint64_t from = squares(c.a, board, binding) & occupied; from && !value; from &= from - 1) {
                const int x    = lowest(from);
                const int side = sideOn(board, x);
                uint64_t targets;
                if (bare)
                    targets = attack ? wanted & ~sideMen(board, side) : wanted & sideMen(board, side);
                else
                    targets = wanted & sideMen(board, attack ? side ^ 1 : side);
                targets &= ~bit(x);
                value = (attacksOf(board, x) & targets) != 0;
            }
            break;
        }
        case Relation::Pins: {
            const auto& between = betweenTable();
            const uint64_t from = squares(c.a, board, binding), through = squares(c.b, board, binding);
            for (uint64_t to = squares(c.c, board, binding) & occupied; to && !value; to &= to - 1) {
                const int t    = lowest(to);
                const int side = sideOn(board, t), other = side ^ 1;
                const uint64_t queens = board.pieces(other, QUEEN);
                uint64_t sliders      = (rookRays(t, 0) & (board.pieces(other, ROOK) | queens)) |
                                        (bishopRays(t, 0) & (board.pieces(other, BISHOP) | queens));
                for (sliders &= from; sliders && !value; sliders &= sliders - 1) {
                    const int s = lowest(sliders);
                    const uint64_t blockers =
                        between[static_cast<std::size_t>(s)][static_cast<std::size_t>(t)] & occupied;
                    if (std::popcount(blockers) != 1) continue;
                    const int x = lowest(blockers);
                    value       = (through & blockers) && sideOn(board, x) == side && kindOn(board, x) != KING;
                }
            }
            break;
        }
        case Relation::EnPrise:
            for (uint64_t men = squares(c.a, board, binding) & occupied; men && !value; men &= men - 1) {
                const int x    = lowest(men);
                const int kind = kindOn(board, x);
                if (kind == KING) continue;
                const int side       = sideOn(board, x);
                const uint64_t enemy = board.attackers(x, side ^ 1);
                if (!enemy) continue;
                if (!(board.attackers(x, side) & ~bit(x))) {
                    value = true;
                    break;
                }
                int least = 100;
                for (uint64_t e = enemy; e; e &= e - 1) least = std::min(least, VALUE[kindOn(board, lowest(e))]);
                value = least < VALUE[kind];
            }
            break;
        case Relation::Passed:
            for (uint64_t men = squares(c.a, board, binding) & occupied; men && !value; men &= men - 1) {
                const int x = lowest(men);
                if (kindOn(board, x) != PAWN) continue;
                const int side = sideOn(board, x);
                value          = (frontSpan(side, x) & board.pieces(side ^ 1, PAWN)) == 0;
            }
            break;
        case Relation::Check:
            value = board.inCheck();
            break;
        case Relation::Mate:
            value = last && board.inCheck() && !board.hasLegalMove();
            break;
        case Relation::Stalemate:
            value = last && !board.inCheck() && !board.hasLegalMove();
            break;
        case Relation::WhiteToMove:
            value = board.sideToMove() == WHITE_SIDE;
            break;
        case Relation::BlackToMove:
            value = board.sideToMove() == BLACK_SIDE;
            break;
        case Relation::OnlyPawns:
            value = board.pieces(c.side, PAWN) == only_pawns_[static_cast<std::size_t>(c.side)];
            break;
    }
    if (value != c.negated) return true;
    for (const auto& alt : c.alternatives)
        if (holds(alt, board, binding, last)) return true;
    return false;
}

bool PatternMatcher::moveFits(const PatternMove& wanted, const ScanBoard& before, const CodedMove& move,
                              const Binding& binding) const {
    if (move.flags & MOVE_NULL) return !wanted.from && !wanted.to && !wanted.captures && wanted.promote < 0;
    const bool castle = (move.flags & (MOVE_SHORT_CASTLE | MOVE_LONG_CASTLE)) != 0;
    const int rank    = before.sideToMove() == WHITE_SIDE ? 0 : 7;
    const int to      = castle ? rank * 8 + ((move.flags & MOVE_SHORT_CASTLE) ? 6 : 2) : move.to;
    if (wanted.from && !(squares(*wanted.from, before, binding) & bit(move.from))) return false;
    if (wanted.to && !(squares(*wanted.to, before, binding) & bit(to))) return false;
    if (wanted.captures) {
        int taken = -1;
        if (move.flags & MOVE_EN_PASSANT)
            taken = move.to + (before.sideToMove() == WHITE_SIDE ? -8 : 8);
        else if (!castle && before.on(move.to) != 0)
            taken = move.to;
        if (taken < 0) return false;
        if (wanted.captured && !(squares(*wanted.captured, before, binding) & bit(taken))) return false;
    }
    if (wanted.promote >= 0 && move.promotion != wanted.promote) return false;
    return true;
}

}  // namespace fcg::study
