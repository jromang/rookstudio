#pragma once

// The fields of the Databases page's search panel, as a search kept in the profile holds them (M1, board 6;
// searches.json), and what reads and writes them without the window (#117): the pattern's men and language put
// together and taken apart, a kept search as JSON and back.

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include <json.hpp>
#include <study/patterns.hpp>

namespace fcg {

// What a pattern's square holds when it must be empty (the other values: a man, side * 6 + kind; -1 anything).
inline constexpr int8_t kEmptySquare = 12;

struct SearchFields {
    std::array<char, 64> player{}, opponent{}, event{};
    int side = 0;  // the player: 0 either colour, 1 White, 2 Black
    std::array<char, 5> year_from{}, year_to{};
    std::array<char, 4> eco_from{}, eco_to{};
    std::array<char, 5> elo_min{};
    char result   = 0;
    bool position = false;  // the games through the Board page's position, in the list's filter
    // Material: for White then Black, pawns, knights, bishops, rooks, queens: "" any, "2", or "0-2".
    std::array<std::array<char, 8>, 10> material{};
    int material_plies = 4;  // that long at least: an exchange under way is not a material
    // A pattern: on each square (a1 = 0), -1 anything, a man (side * 6 + kind: pawn, knight, bishop, rook,
    // queen, king) or kEmptySquare; and a side's pawns only those it shows.
    std::array<int8_t, 64> pattern = [] {
        std::array<int8_t, 64> squares{};
        squares.fill(-1);
        return squares;
    }();
    std::array<bool, 2> only_pawns{};
    bool damaged = false;      // only the games a move of which could not be read
    std::string position_fen;  // the position a kept search brought; empty: the Board page's
    // The Pattern tab beyond the men on squares (#89, mockup M13, board 2): the language's conditions, names and
    // line (study/patterns.hpp), the men of `pattern` and `only_pawns` left out of it.
    study::Pattern language;
    // The Manoeuvre tab: a man's route, from anywhere or from the Board page's position.
    struct Route {
        int side = 0, kind = 1;        // a white knight
        std::vector<uint8_t> squares;  // where it starts, then each square it goes to
        bool others_between = true;
        bool same_man       = true;
        int within          = 15;     // moves of its side at most
        bool from_board     = false;  // from the Board page's position, else from anywhere
        std::string board_fen;        // that position
    } route;
    // The Structure tab: pawns, exactly or one or two pawns away.
    struct Structure {
        bool set   = false;  // in the filter
        bool taken = false;  // its pawns were taken (else the Board page's, once the tab shows)
        std::array<uint64_t, 2> pawns{};
        int distance    = 0;
        bool both_sides = true;
        int side        = 0;  // the side compared when not both
        bool reversed   = false;
        int plies       = 6;  // held that long at least
        std::string said;     // where it was taken: "after 10…Nf8"
    } structure;
};
// The searches kept (M1, board 6): their criteria, for every database, in the profile's searches.json.
struct KeptSearch {
    std::string name;
    SearchFields fields;
};

// The pattern of the fields: their men on squares and their language together.
[[nodiscard]] study::Pattern fullPattern(const SearchFields& fields);
// A pattern put into the fields: its men on squares, the rest into their language.
void setPattern(SearchFields& fields, const study::Pattern& pattern);

// A kept search as searches.json holds it, and back: a field missing or out of range reads as the panel's default; a
// search kept by preview 1 has its pattern's men alone.
[[nodiscard]] nlohmann::json keptSearchJson(const KeptSearch& kept);
[[nodiscard]] KeptSearch keptSearchFrom(const nlohmann::json& json);

}  // namespace fcg
