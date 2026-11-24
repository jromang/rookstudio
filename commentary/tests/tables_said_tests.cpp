// What the endgame tables say of a move, in the assistant's words (#42, mockup M13, board 7), and the checker that
// keeps a model from naming the tables, or a theoretical draw, without them.

#include <doctest/doctest.hpp>

#include <string>

#include <commentary/brief.hpp>
#include <commentary/tables_said.hpp>

using namespace fcg::commentary;
using namespace fcg::commentary::brief_detail;

TEST_CASE("tables said: a move that loses a drawn position, its refutation, the mate, the move that kept the draw") {
    TablesSaid s;
    s.move       = "52.Rxh7";
    s.mover      = Color::White;
    s.before     = Verdict::Draw;
    s.after      = Verdict::Loss;
    s.refutation = "52…Kxh7";
    s.mate       = 19;
    s.kept       = "52.g5";
    s.men        = 7;
    CHECK(tablesSentences(s, Lang::En) ==
          "52.Rxh7 loses: after 52…Kxh7, the tables give Black mate in 19 moves. 52.g5 kept the theoretical draw "
          "(7-piece tables).");
    s.move       = "52.Txh7";
    s.refutation = "52…Rxh7";
    CHECK(tablesSentences(s, Lang::Fr) ==
          "52.Txh7 perd : après 52…Rxh7, les tables donnent le mat aux Noirs en 19 coups. 52.g5 gardait la nulle "
          "théorique (tables à 7 pièces).");
    // Without the refutation nor the mate known yet.
    s.refutation.reset();
    s.mate.reset();
    s.kept.reset();
    CHECK(tablesSentences(s, Lang::En) == "52.Txh7 loses: the tables give Black the win.");
}

TEST_CASE("tables said: a win thrown away, a win kept, a lost position") {
    TablesSaid s;
    s.move   = "61…Kd5";
    s.mover  = Color::Black;
    s.before = Verdict::Win;
    s.after  = Verdict::Draw;
    s.kept   = "61…Kc4";
    s.men    = 6;
    CHECK(tablesSentences(s, Lang::En) ==
          "61…Kd5 throws the win away: the tables say draw. 61…Kc4 kept the win (6-piece tables).");
    CHECK(tablesSentences(s, Lang::Fr) ==
          "61…Kd5 laisse filer le gain : les tables disent nulle. 61…Kc4 gardait le gain (tables à 6 pièces).");
    s.after = Verdict::Win;
    s.mate  = 12;
    CHECK(tablesSentences(s, Lang::En) == "61…Kd5 keeps the win: mate in 12 moves (6-piece tables).");
    s.before = s.after = Verdict::Draw;
    CHECK(tablesSentences(s, Lang::Fr) == "61…Kd5 garde la nulle théorique (tables à 6 pièces).");
    s.before = s.after = Verdict::Loss;
    CHECK(tablesSentences(s, Lang::En) == "After 61…Kd5, the tables still give White the win (6-piece tables).");
}

TEST_CASE("tables said: the checker refuses the tables, or a theoretical draw, named without their facts") {
    MoveFacts facts;
    Allowed allowed;
    const auto says = [&](std::string_view text) {
        const auto r = check(text, facts, allowed, Color::White);
        return r ? debug(*r) : std::string("Ok");
    };
    CHECK(says("Throws the win away, according to the tablebases.") == R"(Unsupported("tables"))");
    CHECK(says("Les tables disent nulle.") == R"(Unsupported("tables"))");
    CHECK(says("A theoretical draw now.") == R"(Unsupported("nulle théorique"))");
    CHECK(says("C'est une nulle théorique.") == R"(Unsupported("nulle théorique"))");
    // The opening's theory is no table's.
    CHECK(says("The theoretical main move.") == "Ok");
    facts.punished_by.push_back(fact::SpoilsWin{Color::White, Verdict::Draw});
    CHECK(says("Throws the win away, according to the tablebases.") == "Ok");
    CHECK(says("A theoretical draw now.") == "Ok");
}
