#include "piece_letters.hpp"

#include <algorithm>

namespace fcg {

namespace {

constexpr std::string_view ENGLISH = "KQRBN";

// The first code point of a UTF-8 text; 0 for an empty one.
char32_t firstCodePoint(std::string_view s) {
    if (s.empty()) return 0;
    const auto c = static_cast<unsigned char>(s[0]);
    if (c < 0x80) return c;
    if ((c & 0xe0) == 0xc0 && s.size() >= 2) return ((c & 0x1fU) << 6) | (static_cast<unsigned char>(s[1]) & 0x3fU);
    if ((c & 0xf0) == 0xe0 && s.size() >= 3)
        return ((c & 0x0fU) << 12) | ((static_cast<unsigned char>(s[1]) & 0x3fU) << 6) |
               (static_cast<unsigned char>(s[2]) & 0x3fU);
    return 0xfffd;
}

// Where a piece letter can stand in a SAN: at its start (castling aside), and after a promotion's '='.
bool pieceSpot(std::string_view san, std::size_t i) { return i == 0 ? san[0] != 'O' : san[i - 1] == '='; }

// The piece of an English letter, -1 for none.
int englishIndex(char c) {
    const auto at = ENGLISH.find(c);
    return at == std::string_view::npos ? -1 : static_cast<int>(at);
}

// The piece of the language's letter that `text` begins with at `at`, the longest letter first; -1 for none, `length`
// then 0.
int languageIndex(std::string_view text, std::size_t at, i18n::Language language, std::size_t& length) {
    const auto& letters = pieceLetters(language);
    int best            = -1;
    length              = 0;
    for (int k = 0; k < 5; ++k) {
        const auto l = letters[static_cast<std::size_t>(k)];
        if (l.size() > length && text.substr(at).substr(0, l.size()) == l) {
            best   = k;
            length = l.size();
        }
    }
    return best;
}

constexpr chess::PieceType PIECES[5] = {chess::PieceType::KING, chess::PieceType::QUEEN, chess::PieceType::ROOK,
                                        chess::PieceType::BISHOP, chess::PieceType::KNIGHT};

}  // namespace

const std::array<std::string_view, 5>& pieceLetters(i18n::Language language) {
    using L = i18n::Language;
    static const std::array<std::string_view, 5> english{"K", "Q", "R", "B", "N"};
    static const std::array<std::string_view, 5> french{"R", "D", "T", "F", "C"};
    static const std::array<std::string_view, 5> german{"K", "D", "T", "L", "S"};
    static const std::array<std::string_view, 5> spanish{"R", "D", "T", "A", "C"};  // and Italian
    static const std::array<std::string_view, 5> portuguese{"R", "D", "T", "B", "C"};
    static const std::array<std::string_view, 5> russian{"Кр", "Ф", "Л", "С", "К"};
    switch (language) {
        case L::French:
            return french;
        case L::German:
            return german;
        case L::Spanish:
        case L::Italian:
            return spanish;
        case L::Portuguese:
            return portuguese;
        case L::Russian:
            return russian;
        case L::English:
        case L::Chinese:
            break;
    }
    return english;
}

std::string sanIn(std::string_view san, i18n::Language language) {
    const auto& letters = pieceLetters(language);
    std::string out;
    out.reserve(san.size() + 2);
    for (std::size_t i = 0; i < san.size(); ++i) {
        const int k = pieceSpot(san, i) ? englishIndex(san[i]) : -1;
        if (k >= 0)
            out += letters[static_cast<std::size_t>(k)];
        else
            out += san[i];
    }
    return out;
}

std::string englishTyped(std::string_view typed, i18n::Language language) {
    std::string out;
    out.reserve(typed.size());
    for (std::size_t i = 0; i < typed.size();) {
        if (i == 0 || typed[i - 1] == '=') {
            std::size_t length = 0;
            if (const int k = languageIndex(typed, i, language, length); k >= 0 && !(i == 0 && typed[0] == 'O')) {
                out += ENGLISH[static_cast<std::size_t>(k)];
                i += length;
                continue;
            }
        }
        out += typed[i] == '0' ? 'O' : typed[i];
        ++i;
    }
    return out;
}

std::vector<TypedMove> typedMoves(const chess::Board& board, std::string_view typed, i18n::Language language) {
    std::string t(typed);
    while (!t.empty() && (t.back() == '!' || t.back() == '?')) t.pop_back();
    for (auto& c : t)
        if (c == '0') c = 'O';
    if (t.empty()) return {};
    const auto& letters = pieceLetters(language);
    // An English letter typed where a piece goes that the language reads as another piece: the moves it finds in
    // English are marked with the piece it names there.
    std::optional<chess::PieceType> other_piece;
    for (std::size_t i = 0; i < t.size() && !other_piece; ++i) {
        if (!pieceSpot(t, i)) continue;
        const int k = englishIndex(t[i]);
        if (k < 0) continue;
        for (int j = 0; j < 5; ++j)
            if (j != k && firstCodePoint(letters[static_cast<std::size_t>(j)]) == static_cast<unsigned char>(t[i]))
                other_piece = PIECES[k];
    }
    std::vector<TypedMove> exact, longer, english;
    chess::Movelist legal;
    chess::movegen::legalmoves(legal, board);
    for (const auto& m : legal) {
        TypedMove tm;
        tm.move  = m;
        tm.san   = chess::uci::moveToSan(board, m);
        tm.shown = sanIn(tm.san, language);
        if (tm.shown.rfind(t, 0) == 0) {
            // Typed shorter than its piece's letter: "К" of the king's "Кр".
            const int k     = pieceSpot(tm.san, 0) ? englishIndex(tm.san[0]) : -1;
            const bool part = k >= 0 && t.size() < letters[static_cast<std::size_t>(k)].size();
            (part ? longer : exact).push_back(std::move(tm));
        } else if (tm.san.rfind(t, 0) == 0) {
            tm.english = other_piece;
            english.push_back(std::move(tm));
        }
    }
    // Forward first within each: "Sg" offers Sg5 before Sg1, "e" e4 before e3 (a queen first among promotions, as the
    // board makes them).
    const auto forward = [&](std::vector<TypedMove>& list) {
        const auto rank = [&](const TypedMove& m) {
            const int r = static_cast<int>(m.move.to().rank());
            return board.sideToMove() == chess::Color::WHITE ? r : 7 - r;
        };
        std::stable_sort(list.begin(), list.end(),
                         [&](const TypedMove& a, const TypedMove& b) { return rank(a) > rank(b); });
    };
    forward(exact);
    forward(longer);
    forward(english);
    exact.insert(exact.end(), std::make_move_iterator(longer.begin()), std::make_move_iterator(longer.end()));
    exact.insert(exact.end(), std::make_move_iterator(english.begin()), std::make_move_iterator(english.end()));
    return exact;
}

bool beginsMove(char32_t c, i18n::Language language) {
    if ((c >= 'a' && c <= 'h') || c == 'O' || c == '0') return true;
    if (c < 0x80 && englishIndex(static_cast<char>(c)) >= 0) return true;
    const auto& letters = pieceLetters(language);
    return std::any_of(letters.begin(), letters.end(), [&](std::string_view l) { return firstCodePoint(l) == c; });
}

}  // namespace fcg
