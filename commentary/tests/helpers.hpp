#pragma once

// What the prototype's tests build positions and lines with (crates/facts/src/lib.rs, mod tests).

#include <doctest/doctest.hpp>

#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

#include <commentary/position.hpp>

namespace fcg::commentary::test {

inline Position pos(std::string_view fen) {
    std::string why;
    auto p = Position::fromFen(fen, &why);
    REQUIRE_MESSAGE(p, why);
    return *p;
}

// UCI moves played one after the other from `start`.
inline std::vector<Move> moves(const Position& start, std::initializer_list<std::string_view> ucis) {
    auto p = start;
    std::vector<Move> out;
    for (const auto u : ucis) {
        const auto m = p.parseUci(u);
        REQUIRE_MESSAGE(m, std::string(u));
        p.playUnchecked(*m);
        out.push_back(*m);
    }
    return out;
}

}  // namespace fcg::commentary::test
