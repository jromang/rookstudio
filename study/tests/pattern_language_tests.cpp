#include <doctest/doctest.hpp>

#include <random>
#include <string>
#include <vector>

#include <study/patterns.hpp>

using namespace fcg::study;

namespace {

uint64_t bit(int square) { return uint64_t{1} << square; }
int sq(const char* name) { return (name[1] - '1') * 8 + (name[0] - 'a'); }
uint16_t W(int kind) { return Men::man(WHITE_SIDE, kind); }
uint16_t B(int kind) { return Men::man(BLACK_SIDE, kind); }

Men men(uint16_t pieces, uint64_t squares = ~uint64_t{0}) {
    Men m;
    m.pieces  = pieces;
    m.squares = squares;
    return m;
}
Subject of(Men m) { return Subject{m, -1}; }
Subject named(int v) {
    Subject s;
    s.variable = v;
    return s;
}

Pattern read(const std::string& text) {
    const auto r = readPattern(text);
    if (r.error)
        FAIL_CHECK("“" << text << "” does not read: line " << r.error->line << ", “" << r.error->word << "”, kind "
                       << static_cast<int>(r.error->kind));
    REQUIRE(r.pattern);
    return *r.pattern;
}

// The text written from a pattern reads back to the same pattern, and is written the same again.
void roundTrip(const Pattern& p) {
    const auto text = writePattern(p);
    CAPTURE(text);
    const auto again = read(text);
    CHECK(again == p);
    CHECK(writePattern(again) == text);
}

Condition only(const std::string& text) {
    const auto p = read(text);
    REQUIRE(p.conditions.size() == 1);
    return p.conditions[0];
}

}  // namespace

TEST_CASE("pattern language: men on squares, every letter and every form of squares") {
    struct Case {
        const char* text;
        Men men;
    };
    const Case cases[] = {
        {"K", men(W(KING))},
        {"Q", men(W(QUEEN))},
        {"R", men(W(ROOK))},
        {"B", men(W(BISHOP))},
        {"N", men(W(KNIGHT))},
        {"P", men(W(PAWN))},
        {"k", men(B(KING))},
        {"q", men(B(QUEEN))},
        {"r", men(B(ROOK))},
        {"b", men(B(BISHOP))},
        {"n", men(B(KNIGHT))},
        {"p", men(B(PAWN))},
        {"A", men(Men::WHITE)},
        {"a", men(Men::BLACK)},
        {"_f6", men(Men::EMPTY, bit(sq("f6")))},
        {"Nf3", men(W(KNIGHT), bit(sq("f3")))},
        {"bb4", men(B(BISHOP), bit(sq("b4")))},
        {"aa1", men(Men::BLACK, bit(sq("a1")))},
        {"[qr]", men(B(QUEEN) | B(ROOK))},
        {"[QR]d1", men(W(QUEEN) | W(ROOK), bit(sq("d1")))},
        {"[Aa]", men(Men::WHITE | Men::BLACK)},
        {"R[a1,h1]", men(W(ROOK), bit(sq("a1")) | bit(sq("h1")))},
        {"p[a-h7]", men(B(PAWN), 0x00ff000000000000ull)},
        {"k[f-h7-8]",
         men(B(KING), bit(sq("f7")) | bit(sq("g7")) | bit(sq("h7")) | bit(sq("f8")) | bit(sq("g8")) | bit(sq("h8")))},
        {"N[c1-2]", men(W(KNIGHT), bit(sq("c1")) | bit(sq("c2")))},
    };
    for (const auto& c : cases) {
        CAPTURE(c.text);
        const auto cond = only(c.text);
        CHECK(cond.relation == Relation::On);
        CHECK(cond.a.men == c.men);
        CHECK_FALSE(cond.negated);
    }
    // Side by side, each a condition; the men on squares are the small board's.
    const auto p = read("Bd3 Nf3 Qd1 Pe5 kg8 ph7 _f6");
    REQUIRE(p.conditions.size() == 7);
    for (const auto& c : p.conditions) CHECK(manOnSquare(c));
    CHECK_FALSE(manOnSquare(only("N[f3,g5]")));
    CHECK_FALSE(manOnSquare(only("not Nf3")));
    CHECK_FALSE(manOnSquare(only("[qr]d8")));
    // Squares alone, where a word wants squares: a move's target.
    const auto line = read("line --> move to [g5,h5]");
    REQUIRE(line.line.size() == 1);
    REQUIRE(line.line[0].move);
    REQUIRE(line.line[0].move->to);
    CHECK(line.line[0].move->to->men == men(0, bit(sq("g5")) | bit(sq("h5"))));
}

TEST_CASE("pattern language: every relation, not, or, only, names") {
    {
        const auto c = only("N attacks k");
        CHECK(c.relation == Relation::Attacks);
        CHECK(c.a == of(men(W(KNIGHT))));
        CHECK(c.b == of(men(B(KING))));
    }
    {
        const auto c = only("P defends Nd5");
        CHECK(c.relation == Relation::Defends);
        CHECK(c.b == of(men(W(KNIGHT), bit(sq("d5")))));
    }
    {
        const auto c = only("N attacks f7");  // a square, whatever stands there
        CHECK(c.b == of(men(0, bit(sq("f7")))));
    }
    {
        const auto c = only("pin");
        CHECK(c.relation == Relation::Pins);
        CHECK(c.a.men.pieces == (W(BISHOP) | W(ROOK) | W(QUEEN) | B(BISHOP) | B(ROOK) | B(QUEEN)));
        CHECK(c.c.men.pieces == (W(KING) | B(KING)));
        CHECK(conditionText(Pattern{}, c) == "pin");
    }
    {
        const auto c = only("pin from B through n to q");
        CHECK(c.a == of(men(W(BISHOP))));
        CHECK(c.b == of(men(B(KNIGHT))));
        CHECK(c.c == of(men(B(QUEEN))));
        const auto again = only("pin to q from B through n");  // in any order
        CHECK(again == c);
    }
    CHECK(only("q en prise").relation == Relation::EnPrise);
    CHECK(only("q enprise") == only("q en prise"));
    CHECK(only("P passed").relation == Relation::Passed);
    CHECK(only("check").relation == Relation::Check);
    CHECK(only("mate").relation == Relation::Mate);
    CHECK(only("stalemate").relation == Relation::Stalemate);
    CHECK(only("wtm").relation == Relation::WhiteToMove);
    CHECK(only("btm").relation == Relation::BlackToMove);
    {
        const auto c = only("not check");
        CHECK(c.negated);
        CHECK(only("not not check") == only("check"));
    }
    {
        const auto c = only("Nf3 or Ng5 or check");
        CHECK(c.relation == Relation::On);
        REQUIRE(c.alternatives.size() == 2);
        CHECK(c.alternatives[0].a == of(men(W(KNIGHT), bit(sq("g5")))));
        CHECK(c.alternatives[1].relation == Relation::Check);
    }
    {
        const auto c = only("only P");
        CHECK(c.relation == Relation::OnlyPawns);
        CHECK(c.side == WHITE_SIDE);
        CHECK(only("only p").side == BLACK_SIDE);
    }
    {
        // A name for one man, declared before or after its use.
        const auto p = read("$c attacks k\npiece $c in N\n$c attacks [qr]");
        REQUIRE(p.variables.size() == 1);
        CHECK(p.variables[0].name == "c");
        CHECK(p.variables[0].men == men(W(KNIGHT)));
        REQUIRE(p.conditions.size() == 2);
        CHECK(p.conditions[0].a == named(0));
        CHECK(p.conditions[1].b == of(men(B(QUEEN) | B(ROOK))));
    }
}

TEST_CASE("pattern language: a line of moves and positions, its gaps") {
    const auto p = read(
        "; The sacrifice on h7\n"
        "Bd3 Nf3 Qd1 Pe5 kg8 ph7 _f6\n"
        "line --> move from B to h7 capture p check\n"
        "     --> move from k to h7\n"
        "     --> .{0,2} --> move from N to g5 check   // in three plies at most\n"
        "     --> . --> Qh5\n"
        "     --> move capture promote Q\n");
    CHECK(p.title == std::vector<std::string>{"The sacrifice on h7"});
    CHECK(p.conditions.size() == 7);
    REQUIRE(p.line.size() == 5);
    const auto& first = p.line[0];
    REQUIRE(first.move);
    CHECK(first.move->from == std::optional<Subject>(of(men(W(BISHOP)))));
    CHECK(first.move->to == std::optional<Subject>(of(men(0, bit(sq("h7"))))));
    CHECK(first.move->captures);
    CHECK(first.move->captured == std::optional<Subject>(of(men(B(PAWN)))));
    REQUIRE(first.conditions.size() == 1);
    CHECK(first.conditions[0].relation == Relation::Check);
    CHECK(first.min_gap == 0);
    CHECK(first.max_gap == 0);
    CHECK(p.line[2].min_gap == 0);
    CHECK(p.line[2].max_gap == 2);
    CHECK(p.line[3].min_gap == 1);
    CHECK(p.line[3].max_gap == 1);
    CHECK_FALSE(p.line[3].move);
    REQUIRE(p.line[3].conditions.size() == 1);
    CHECK(p.line[4].move->captures);
    CHECK_FALSE(p.line[4].move->captured);
    CHECK(p.line[4].move->promote == QUEEN);
    roundTrip(p);
}

TEST_CASE("pattern language: what is written reads back to the same pattern") {
    const char* texts[] = {
        "Nf3",
        "not Nf3 or _g5 or check",
        "piece $a in [RQ]\npiece $b in [rq]\n$a attacks $b\nnot $b defends $a\n$a en prise or $b passed",
        "k[f-h7-8]\np[f-h7]\nR[a-h8] attacks k\nmate",
        "pin from [QRB] through [qrbn] to k\nbtm",
        "pin through n\nstalemate",
        "Kg1 Rf1 Pf2 Pg2 Ph2 _f3 _g3 _h3\nonly P",
        "line --> move from P capture to [a-h8] promote N --> check",
        "N[a1,c3,e5,g7] attacks [a-c1-3]",
        "[QR]d1 attacks [qr]d8",
    };
    for (const auto* text : texts) {
        CAPTURE(text);
        roundTrip(read(text));
    }
    for (const auto& preset : patternPresets()) {
        CAPTURE(preset.key);
        roundTrip(read(std::string(preset.text)));
    }
    // A comment kept with its condition; the men put on one line, kings first.
    const auto p = read("ph7\nKg1 ; the white king\nN attacks k   ; a check");
    CHECK(writePattern(p) == "Kg1 ph7   ; the white king\nN attacks k   ; a check\n");
}

TEST_CASE("pattern language: the builder's pattern and the text are the same search") {
    // What the search panel's builder makes for the knight fork, word by word (the mockup's three rows).
    Pattern built;
    built.variables.push_back(Variable{"n", men(W(KNIGHT)), 0});
    Condition king;
    king.relation   = Relation::Attacks;
    king.a          = named(0);
    king.b          = of(men(B(KING)));
    Condition other = king;
    other.b         = of(men(B(QUEEN) | B(ROOK)));
    Condition to_move;
    to_move.relation = Relation::BlackToMove;
    built.conditions = {king, other, to_move};
    const auto text  = writePattern(built);
    CHECK(text == "piece $n in N\n$n attacks k\n$n attacks [qr]\nbtm\n");
    CHECK(read(text) == built);
    CHECK(read(std::string(patternPresets()[0].text)) == built);
    // A word changed on the builder (the verb, the men) is the text changed.
    built.conditions[1].relation = Relation::Defends;
    CHECK(writePattern(built) == "piece $n in N\n$n attacks k\n$n defends [qr]\nbtm\n");
    built.conditions[1].negated = true;
    CHECK(conditionText(built, built.conditions[1]) == "not $n defends [qr]");
}

TEST_CASE("pattern language: every error names its line and its word, with the word it looks like") {
    struct Case {
        const char* text;
        PatternErrorKind kind;
        int line;
        const char* word;
        const char* suggestion = "";
    };
    const Case cases[] = {
        {"piece $c in N\n$c atacks k", PatternErrorKind::UnknownWord, 2, "atacks", "attacks"},
        {"chek", PatternErrorKind::UnknownWord, 1, "chek", "check"},
        {"N defned k", PatternErrorKind::UnknownWord, 1, "defned", "defends"},
        {"q en garde", PatternErrorKind::UnknownWord, 1, "garde", "prise"},
        {"zebra", PatternErrorKind::UnknownWord, 1, "zebra", ""},
        {"N attackedby k", PatternErrorKind::NotUnderstood, 1, "attackedby"},
        {"xray", PatternErrorKind::NotUnderstood, 1, "xray"},
        {"flipcolor { check }", PatternErrorKind::NotUnderstood, 1, "flipcolor"},
        {"{ check }", PatternErrorKind::NotUnderstood, 1, "{"},
        {"Nf3 & Ng5", PatternErrorKind::NotUnderstood, 1, "&"},
        {"line --> .* --> check", PatternErrorKind::NotUnderstood, 1, ".*"},
        {"Ni9", PatternErrorKind::BadSquares, 1, "Ni9"},
        {"R[a1,j1]", PatternErrorKind::BadSquares, 1, "R[a1,j1]"},
        {"N attacks", PatternErrorKind::NeedMen, 1, "attacks"},
        {"pin from", PatternErrorKind::NeedMen, 1, "from"},
        {"piece $c in", PatternErrorKind::NeedMen, 1, "in"},
        {"line --> move promote K", PatternErrorKind::NeedKind, 1, "promote"},
        {"only N", PatternErrorKind::NeedPawns, 1, "only"},
        {"piece c in N", PatternErrorKind::NeedName, 1, "piece"},
        {"$x attacks k", PatternErrorKind::NotNamed, 1, "$x"},
        {"piece $c in N\npiece $c in B", PatternErrorKind::NamedTwice, 2, "$c"},
        {"piece $a in N\npiece $b in N\npiece $c in N\npiece $d in N\npiece $e in N", PatternErrorKind::TooManyNames, 5,
         "$e"},
        {"line --> check\nline --> mate", PatternErrorKind::SecondLine, 2, "line"},
        {"--> check", PatternErrorKind::StepWithoutLine, 1, "-->"},
        {"line", PatternErrorKind::EmptyLine, 1, "line"},
        {"line -->", PatternErrorKind::EmptyLine, 1, "-->"},
        {"line --> check --> check --> check --> check --> check --> check --> check --> check --> check",
         PatternErrorKind::TooManySteps, 1, "-->"},
        {"line --> check\n     --> .{0,2}", PatternErrorKind::GapAtEnd, 2, "."},
        {"line --> .{3,1} --> check", PatternErrorKind::BadGap, 1, ".{3,1}"},
        {"line --> .{0,25} --> check", PatternErrorKind::BadGap, 1, ".{0,25}"},
        {"check attacks", PatternErrorKind::NeedCondition, 1, "attacks"},
        {"not", PatternErrorKind::NeedCondition, 1, "not"},
        {"Nf3 or", PatternErrorKind::NeedCondition, 1, "or"},
        {"h7", PatternErrorKind::NeedCondition, 1, "h7"},
        {"piece $c in N N", PatternErrorKind::Unexpected, 1, "N"},
        {"line check", PatternErrorKind::Unexpected, 1, "check"},
    };
    for (const auto& c : cases) {
        CAPTURE(c.text);
        const auto r = readPattern(c.text);
        REQUIRE(r.error);
        CHECK_FALSE(r.pattern);
        CHECK(static_cast<int>(r.error->kind) == static_cast<int>(c.kind));
        CHECK(r.error->line == c.line);
        CHECK(r.error->word == c.word);
        CHECK(r.error->suggestion == c.suggestion);
    }
    // The first error of the text is the one told.
    const auto r = readPattern("Nf3\nN atacks k\nchek");
    REQUIRE(r.error);
    CHECK(r.error->line == 2);
    // CQL's words are known as such; the language's own are not CQL that is not understood.
    CHECK(cqlWord("sort"));
    CHECK_FALSE(cqlWord("attacks"));
    CHECK_FALSE(cqlWord("zebra"));
}

TEST_CASE("pattern language: what cannot both hold is told before searching") {
    const auto said = [](const std::string& text) { return contradiction(read(text)); };
    {
        const auto p = read("Nc7\nN attacks k\n_c7");  // the mockup's: a knight on c7, c7 empty
        const auto c = contradiction(p);
        REQUIRE(c);
        CHECK(conditionText(p, p.conditions[static_cast<std::size_t>(c->first)]) == "Nc7");
        CHECK(conditionText(p, p.conditions[static_cast<std::size_t>(c->second)]) == "_c7");
    }
    CHECK(said("Nc7 Bc7"));
    CHECK(said("wtm\nbtm"));
    CHECK(said("Nf3\nnot Nf3"));
    CHECK(said("check\nnot check"));
    CHECK(said("mate\nnot check"));
    CHECK(said("stalemate\ncheck"));
    CHECK(said("mate\nstalemate"));
    {
        const auto c = said("Pa1");  // a pawn on its first rank
        REQUIRE(c);
        CHECK(c->second == -1);
    }
    CHECK_FALSE(said("Nc7 _c6"));
    CHECK_FALSE(said("mate\ncheck"));
    CHECK_FALSE(said("Nf3 or Bf3\n_f3"));
    for (const auto& preset : patternPresets()) CHECK_FALSE(said(std::string(preset.text)));
}

TEST_CASE("pattern language: texts mutated at random never break the reader; what reads writes back the same") {
    // The presets and texts of every word, mutated a character or a word at a time, as a person types and mistypes.
    std::vector<std::string> seeds;
    for (const auto& preset : patternPresets()) seeds.emplace_back(preset.text);
    seeds.emplace_back("piece $a in [RQ]\npiece $b in [rq]\n$a attacks $b or $b passed\nnot $b defends $a\nonly p");
    seeds.emplace_back("line --> move from P capture to [a-h8] promote N --> .{0,3} --> check\n     --> mate");
    seeds.emplace_back("k[f-h7-8] _[e1,g1] N[a1,c3,e5] attacks [a-c1-3]\npin through n to q\nstalemate // the end");
    const std::string pieces[] = {"$",       "[",  "]",     ",",     "-",  "-->",  ".{", "}",  ";",   "\n",   " ",
                                  "N",       "k",  "_",     "a1",    "h9", "{",    "(",  "or", "not", "line", "move",
                                  "attacks", "en", "prise", "piece", "in", "only", "..", "\t", "x",   "é",    "\xff"};
    std::mt19937 random(89);
    std::size_t read = 0, refused = 0;
    for (int n = 0; n < 20000; ++n) {
        auto text = seeds[random() % seeds.size()];
        for (int edits = 1 + static_cast<int>(random() % 3); edits > 0 && !text.empty(); --edits) {
            const auto at = random() % (text.size() + 1);
            switch (random() % 3) {
                case 0:
                    text.insert(at, pieces[random() % std::size(pieces)]);
                    break;
                case 1:
                    text.erase(std::min<std::size_t>(at, text.size() - 1), 1 + random() % 4);
                    break;
                default:
                    text.replace(std::min<std::size_t>(at, text.size() - 1), 1, pieces[random() % std::size(pieces)]);
                    break;
            }
        }
        const auto r = readPattern(text);
        CHECK(r.pattern.has_value() != r.error.has_value());
        if (!r.pattern) {
            CHECK(r.error->line >= 1);
            ++refused;
            continue;
        }
        ++read;
        const auto written = writePattern(*r.pattern);
        const auto again   = readPattern(written);
        REQUIRE(again.pattern);
        CHECK(*again.pattern == *r.pattern);
        CHECK(writePattern(*again.pattern) == written);
        (void)contradiction(*r.pattern);
    }
    CHECK(read > 300);  // most mutations break a text: a few hundred still read
    CHECK(refused > 10000);
}
