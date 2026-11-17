#include "phrases.hpp"

#include <algorithm>
#include <array>

#include <fmt/include/fmt/format.h>

#include "formation.hpp"

namespace fcg::commentary {

// The phrasebooks, each in a file of its own.
const Phrasebook& englishPhrasebook();
const Phrasebook& frenchPhrasebook();
std::string englishMaterial(int32_t pawns, std::optional<Trade> trade);
std::string frenchMaterial(int32_t pawns, std::optional<Trade> trade);
const char* englishKind(phrases_detail::Kind kind, uint8_t n);
const char* frenchKind(phrases_detail::Kind kind, uint8_t n);

const Phrasebook& phrasebook(Lang lang) { return lang == Lang::Fr ? frenchPhrasebook() : englishPhrasebook(); }

namespace {

// One code point of UTF-8 text from `at`, and its length.
std::pair<char32_t, std::size_t> decode(std::string_view s, std::size_t at = 0) {
    const auto c = static_cast<unsigned char>(s[at]);
    if (c < 0x80) return {c, 1};
    if ((c >> 5) == 6 && at + 1 < s.size())
        return {static_cast<char32_t>(((c & 0x1F) << 6) | (static_cast<unsigned char>(s[at + 1]) & 0x3F)), 2};
    if ((c >> 4) == 14 && at + 2 < s.size())
        return {static_cast<char32_t>(((c & 0x0F) << 12) | ((static_cast<unsigned char>(s[at + 1]) & 0x3F) << 6) |
                                      (static_cast<unsigned char>(s[at + 2]) & 0x3F)),
                3};
    return {c, 1};
}

std::string encode(char32_t c) {
    std::string out;
    if (c < 0x80) {
        out += static_cast<char>(c);
    } else if (c < 0x800) {
        out += static_cast<char>(0xC0 | (c >> 6));
        out += static_cast<char>(0x80 | (c & 0x3F));
    } else {
        out += static_cast<char>(0xE0 | (c >> 12));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
    }
    return out;
}

char32_t upper(char32_t c) {
    if (c >= 'a' && c <= 'z') return c - 32;
    if (c >= 0xE0 && c <= 0xFE && c != 0xF7) return c - 0x20;
    if (c == 0x153) return 0x152;  // œ
    return c;
}

char32_t lower(char32_t c) {
    if (c >= 'A' && c <= 'Z') return c + 32;
    if (c >= 0xC0 && c <= 0xDE && c != 0xD7) return c + 0x20;
    if (c == 0x152) return 0x153;
    return c;
}

std::array<std::pair<phrases_detail::Kind, uint8_t>, 5> wonItems(const Trade& t, uint8_t exchanges) {
    using phrases_detail::Kind;
    return {std::pair{Kind::Exchange, exchanges}, std::pair{Kind::Queen, t.won[3]},
            std::pair{Kind::Rook, static_cast<uint8_t>(t.won[2] - exchanges)}, std::pair{Kind::Piece, t.won[1]},
            std::pair{Kind::Pawn, t.won[0]}};
}

std::array<std::pair<phrases_detail::Kind, uint8_t>, 5> gaveItems(const Trade& t, uint8_t exchanges) {
    using phrases_detail::Kind;
    return {std::pair{Kind::Exchange, uint8_t{0}}, std::pair{Kind::Queen, t.gave[3]}, std::pair{Kind::Rook, t.gave[2]},
            std::pair{Kind::Piece, static_cast<uint8_t>(t.gave[1] - exchanges)}, std::pair{Kind::Pawn, t.gave[0]}};
}

}  // namespace

std::string capitalize(std::string text) {
    if (text.empty()) return text;
    const auto [c, n] = decode(text);
    return encode(upper(c)) + text.substr(n);
}

std::string lowerFirst(std::string_view sentence) {
    if (sentence.empty()) return {};
    const auto [c, n] = decode(sentence);
    return encode(lower(c)) + std::string(sentence.substr(n));
}

const char* sideName(Color color, Lang lang) {
    if (lang == Lang::En) return color == Color::White ? "White" : "Black";
    return color == Color::White ? "les Blancs" : "les Noirs";
}

std::string formationName(Formation kind, Color side, Lang lang) {
    const auto sq = [&](File file, uint32_t steps) { return name(squareFor(side, file, steps)); };
    const bool fr = lang == Lang::Fr;
    switch (kind) {
        case Formation::Carlsbad:
            return fr ? "la structure de Carlsbad" : "the Carlsbad structure";
        case Formation::Maroczy:
            return fr ? "l'étau de Maroczy" : "the Maroczy bind";
        case Formation::Hedgehog:
            return fr ? "le hérisson" : "the hedgehog";
        case Formation::Stonewall:
            return fr ? "le Stonewall" : "the Stonewall";
        case Formation::KingsideChain:
            return fr ? fmt::format("la chaîne de pions {} et {}", sq(File::D, 3), sq(File::E, 4))
                      : fmt::format("the pawn chain on {} and {}", sq(File::D, 3), sq(File::E, 4));
        case Formation::QueensideChain:
            return fr ? fmt::format("la chaîne de pions {} et {}", sq(File::E, 3), sq(File::D, 4))
                      : fmt::format("the pawn chain on {} and {}", sq(File::E, 3), sq(File::D, 4));
        case Formation::Benoni:
            return fr ? "la structure Benoni" : "the Benoni structure";
        default:
            return fr ? "le pion dame isolé" : "the isolated queen's pawn";
    }
}

const char* leverWords(const Lever& lever, Lang lang) {
    const bool fr = lang == Lang::Fr;
    switch (lever.kind) {
        case Lever::Kind::ChainBase:
            return fr ? "l'attaque de la chaîne de pions à sa base" : "the attack on the base of the pawn chain";
        case Lever::Kind::ChainHead:
            return fr ? "l'attaque de la tête de la chaîne de pions" : "the attack on the head of the pawn chain";
        case Lever::Kind::Isolani:
            return fr ? "la poussée thématique du pion dame isolé"
                      : "the thematic advance of the isolated queen's pawn";
        default:
            if (lever.formation == Formation::Hedgehog)
                return fr ? "la rupture thématique du hérisson" : "the hedgehog's thematic break";
            return fr ? "la rupture libératrice contre l'étau de Maroczy"
                      : "the freeing break against the Maroczy bind";
    }
}

const char* wingWords(Square square, Lang lang) {
    const auto f  = fileOf(square);
    const bool fr = lang == Lang::Fr;
    if (f == File::A || f == File::B || f == File::C) return fr ? "à l'aile dame" : "on the queenside";
    if (f == File::D || f == File::E) return fr ? "au centre" : "in the centre";
    return fr ? "à l'aile roi" : "on the kingside";
}

const char* ofWing(Wing wing, Lang lang) {
    const bool fr = lang == Lang::Fr;
    if (wing == Wing::Queenside) return fr ? "à l'aile dame" : "on the queenside";
    return fr ? "à l'aile roi" : "on the kingside";
}

const char* colourSquares(bool dark, Lang lang) {
    const bool fr = lang == Lang::Fr;
    if (dark) return fr ? "les cases noires" : "the dark squares";
    return fr ? "les cases blanches" : "the light squares";
}

std::string barrierWords(LineKind line, Square square, Lang lang) {
    static constexpr const char* ranks_fr[] = {"première",  "deuxième", "troisième", "quatrième",
                                               "cinquième", "sixième",  "septième",  "huitième"};
    static constexpr const char* ranks_en[] = {"first", "second", "third",   "fourth",
                                               "fifth", "sixth",  "seventh", "eighth"};
    const auto rank                         = static_cast<int>(rankOf(square));
    const char file                         = letter(fileOf(square));
    if (line == LineKind::File)
        return lang == Lang::Fr ? fmt::format("la colonne {}", file) : fmt::format("the {}-file", file);
    return lang == Lang::Fr ? fmt::format("la {} rangée", ranks_fr[rank]) : fmt::format("the {} rank", ranks_en[rank]);
}

std::string materialWords(int32_t pawns, std::optional<Trade> trade, Lang lang) {
    return lang == Lang::Fr ? frenchMaterial(pawns, trade) : englishMaterial(pawns, trade);
}

std::optional<std::string> givenWords(std::optional<Trade> trade, Lang lang) {
    if (!trade) return std::nullopt;
    const auto parts = lang == Lang::Fr ? phrases_detail::tradeParts(*trade, frenchKind, " et ")
                                        : phrases_detail::tradeParts(*trade, englishKind, " and ");
    if (!parts) return std::nullopt;
    return parts->second;
}

std::string formationSentence(Formation kind, Color side, Lang lang) {
    const bool fr     = lang == Lang::Fr;
    const auto owner  = sideName(side, lang);
    const auto other  = sideName(!side, lang);
    const auto mine   = [&](File file, uint32_t steps) { return name(squareFor(side, file, steps)); };
    const auto theirs = [&](File file, uint32_t steps) { return name(squareFor(!side, file, steps)); };
    using enum File;
    switch (kind) {
        case Formation::Carlsbad:
            return fr ? fmt::format(
                            "structure de Carlsbad : {} ont l'attaque de minorité à l'aile dame, {} la colonne e "
                            "et le jeu à l'aile roi",
                            owner, other)
                      : fmt::format(
                            "Carlsbad structure: {} has the minority attack on the queenside, {} the e-file and "
                            "play on the kingside",
                            owner, other);
        case Formation::Maroczy:
            return fr ? fmt::format(
                            "étau de Maroczy : {} ont de l'espace avec leurs pions en {} et {}, {} cherchent les "
                            "ruptures {} ou {}",
                            owner, mine(C, 3), mine(E, 3), other, theirs(B, 3), theirs(D, 3))
                      : fmt::format(
                            "Maroczy bind: the pawns on {} and {} give {} space, {} looks for the breaks {} or {}",
                            mine(C, 3), mine(E, 3), owner, other, theirs(B, 3), theirs(D, 3));
        case Formation::Hedgehog:
            return fr ? fmt::format(
                            "hérisson : {} tiennent leurs pions en {}, {}, {} et {} et préparent les ruptures {} "
                            "ou {}",
                            owner, mine(A, 2), mine(B, 2), mine(D, 2), mine(E, 2), mine(B, 3), mine(D, 3))
                      : fmt::format(
                            "hedgehog: {} keeps its pawns on {}, {}, {} and {} and prepares the breaks {} or {}", owner,
                            mine(A, 2), mine(B, 2), mine(D, 2), mine(E, 2), mine(B, 3), mine(D, 3));
        case Formation::Stonewall:
            return fr ? fmt::format(
                            "Stonewall : {} tiennent la case {} avec les pions en {}, {} et {}, mais la case {} "
                            "est affaiblie",
                            owner, mine(E, 4), mine(D, 3), mine(E, 2), mine(F, 3), mine(E, 3))
                      : fmt::format(
                            "Stonewall: {} holds the {} square with the pawns on {}, {} and {}, but the {} square "
                            "is weakened",
                            owner, mine(E, 4), mine(D, 3), mine(E, 2), mine(F, 3), mine(E, 3));
        case Formation::KingsideChain:
            return fr ? fmt::format(
                            "chaîne de pions {} et {} : {} ont de l'espace à l'aile roi, {} s'en prennent à sa "
                            "base en {} ou à sa tête en {}",
                            mine(D, 3), mine(E, 4), owner, other, mine(D, 3), mine(E, 4))
                      : fmt::format(
                            "pawn chain on {} and {}: {} has space on the kingside, {} goes for its base on {} or "
                            "its head on {}",
                            mine(D, 3), mine(E, 4), owner, other, mine(D, 3), mine(E, 4));
        case Formation::QueensideChain:
            return fr ? fmt::format(
                            "chaîne de pions {} et {} : {} ont de l'espace à l'aile dame, {} s'en prennent à sa "
                            "base en {}",
                            mine(E, 3), mine(D, 4), owner, other, mine(E, 3))
                      : fmt::format(
                            "pawn chain on {} and {}: {} has space on the queenside, {} goes for its base on {}",
                            mine(E, 3), mine(D, 4), owner, other, mine(E, 3));
        case Formation::Benoni:
            return fr ? fmt::format("structure Benoni : {} ont la majorité de pions au centre, {} à l'aile dame", owner,
                                    other)
                      : fmt::format("Benoni structure: {} has the pawn majority in the centre, {} on the queenside",
                                    owner, other);
        default:
            return fr ? fmt::format("{} ont un pion dame isolé", owner)
                      : fmt::format("{} has an isolated queen's pawn", owner);
    }
}

namespace phrases_detail {

std::optional<std::pair<std::string, std::optional<std::string>>> tradeParts(const Trade& trade, KindWords kind,
                                                                             std::string_view and_) {
    const uint8_t exchanges = std::min(trade.won[2], trade.gave[1]);
    const auto say          = [&](const auto& items) -> std::optional<std::string> {
        std::vector<std::string> words;
        for (const auto& [k, n] : items) {
            if (n == 0) continue;
            const char* w = kind(k, n);
            if (!w) return std::nullopt;
            words.emplace_back(w);
        }
        if (words.empty()) return std::string();
        if (words.size() == 1) return words.front();
        std::string out;
        for (std::size_t i = 0; i + 1 < words.size(); ++i) out += (i ? ", " : "") + words[i];
        return out + std::string(and_) + words.back();
    };
    const auto won_items  = wonItems(trade, exchanges);
    const auto gave_items = gaveItems(trade, exchanges);
    const auto won        = say(won_items);
    if (!won || won->empty()) return std::nullopt;
    const bool nothing_given =
        std::all_of(gave_items.begin(), gave_items.end(), [](const auto& i) { return i.second == 0; });
    if (nothing_given) return std::pair{*won, std::optional<std::string>{}};
    const auto gave = say(gave_items);
    if (!gave) return std::nullopt;
    return std::pair{*won, std::optional<std::string>{*gave}};
}

std::optional<std::string> tradeWords(const Trade& trade, KindWords kind, std::string_view and_,
                                      std::string_view against) {
    const auto parts = tradeParts(trade, kind, and_);
    if (!parts) return std::nullopt;
    if (parts->second) return parts->first + std::string(against) + *parts->second;
    return parts->first;
}

bool isMechanism(const Fact& fact) {
    return !(std::holds_alternative<fact::WinsMaterial>(fact) || std::holds_alternative<fact::Mate>(fact) ||
             std::holds_alternative<fact::Hanging>(fact));
}

std::size_t standing(double win_after) noexcept {
    if (win_after >= 70.0) return 0;
    if (win_after >= 40.0) return 1;
    return 2;
}

}  // namespace phrases_detail

}  // namespace fcg::commentary
