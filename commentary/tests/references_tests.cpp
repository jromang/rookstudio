// What the databases know of a position, and the questions about it answered from them.

#include <doctest/doctest.hpp>

#include <atomic>
#include <deque>
#include <filesystem>
#include <string>
#include <variant>
#include <vector>

#include <commentary/question.hpp>
#include <commentary/references.hpp>
#include <study/import.hpp>
#include <study/store.hpp>
#include <study/tree.hpp>

#include "helpers.hpp"

using namespace fcg;
using namespace fcg::commentary;
namespace fs = std::filesystem;

namespace {

struct Played {
    const char* white;
    const char* black;
    int white_elo, black_elo;
    const char* event;
    const char* date;
    const char* result;
    const char* moves;
};

// A database of those games, and its tree.
struct Base {
    Base(const std::string& name, const std::vector<Played>& games) {
        const auto dir = fs::temp_directory_path() / name;
        fs::remove_all(dir);
        db = study::Database::create(dir);
        std::vector<study::ImportedGame> imported;
        for (const auto& p : games) {
            std::string text = std::string("[Event \"") + p.event + "\"]\n[Date \"" + p.date + "\"]\n[White \"" +
                               p.white + "\"]\n[Black \"" + p.black + "\"]\n[Result \"" + p.result + "\"]\n";
            if (p.white_elo) text += "[WhiteElo \"" + std::to_string(p.white_elo) + "\"]\n";
            if (p.black_elo) text += "[BlackElo \"" + std::to_string(p.black_elo) + "\"]\n";
            text += std::string("\n") + p.moves + " " + p.result + "\n";
            study::ImportedGame g;
            REQUIRE(study::readImportedGame(text, g));
            imported.push_back(std::move(g));
        }
        db->append(std::move(imported), true);
        study::TreeProgress progress;
        std::atomic<bool> cancel{false};
        REQUIRE(study::buildTree(*db, *db->snapshot(), {}, progress, cancel).error.empty());
        tree = study::Tree::open(db->folder());
        REQUIRE(tree);
    }
    [[nodiscard]] ReferenceBase reference(std::string name, bool with_tree = true) const {
        return {std::move(name), db->snapshot(), with_tree ? tree : nullptr};
    }
    std::shared_ptr<study::Database> db;
    std::shared_ptr<const study::Tree> tree;
};

const std::vector<Played> GAMES = {
    {"Carlsen, Magnus", "Caruana, Fabiano", 2830, 2800, "Norway Chess 2024", "2024.05.30", "1-0",
     "1. e4 e5 2. Nf3 Nc6 3. Bb5 a6"},
    {"Kasparov, Garry", "Karpov, Anatoly", 2800, 2750, "World Championship", "1985.10.15", "1/2-1/2",
     "1. e4 e5 2. Nf3 Nc6 3. Bb5 Nf6"},
    {"Amateur, A", "Amateur, B", 0, 0, "Club", "2020.01.10", "0-1", "1. e4 e5 2. Nf3 Nc6 3. Bc4"},
    {"Alpha, A", "Bravo, B", 2600, 2650, "Open", "2010.07.01", "1-0", "1. e4 e5 2. Nf3 Nf6"},
    {"Charlie, C", "Delta, D", 2500, 2500, "Open", "1999.03.03", "1-0", "1. d4 d5"},
};

Position after(std::initializer_list<std::string_view> ucis) {
    auto pos = Position::initial();
    for (const auto& mv : test::moves(pos, ucis)) pos.playUnchecked(mv);
    return pos;
}

// A model that gives its answers in turn.
class Scripted final : public Model {
   public:
    explicit Scripted(std::deque<std::string> answers) : answers_(std::move(answers)) {}
    std::optional<Generation> generate(const Request&, std::string*) override {
        if (answers_.empty()) return std::nullopt;
        Generation g;
        g.text = answers_.front();
        answers_.pop_front();
        return g;
    }

   private:
    std::deque<std::string> answers_;
};

}  // namespace

TEST_CASE("references: the games through a position, the moves they played, the strongest of them") {
    const Base base("references-games", GAMES);
    const std::vector<ReferenceBase> bases = {base.reference("Ref")};
    const auto pos                         = after({"e2e4", "e7e5", "g1f3"});
    const auto refs                        = references(pos, bases, 3);
    CHECK(refs.total.games == 4);
    CHECK(refs.total.white == 2);
    CHECK(refs.total.draws == 1);
    CHECK(refs.total.black == 1);
    REQUIRE(refs.moves.size() == 2);
    CHECK(moveText(pos, refs.moves[0].move, PieceLetters::English) == "2…Nc6");
    CHECK(refs.moves[0].stats.games == 3);
    CHECK(moveText(pos, refs.moves[1].move, PieceLetters::English) == "2…Nf6");
    // The strongest by the players' mean rating.
    REQUIRE(refs.games.size() == 3);
    CHECK(refs.games[0].white == "Carlsen, Magnus");
    CHECK(refs.games[0].year == 2024);
    CHECK(refs.games[1].white == "Kasparov, Garry");
    CHECK(refs.games[2].white == "Alpha, A");

    CHECK(gamesAnswer(refs, bases, Lang::En) ==
          "4 games of Ref reached this position (+2 =1 −1 for White). The strongest: Carlsen – Caruana, Norway Chess "
          "2024, 1-0; Kasparov – Karpov, World Championship 1985, ½-½; Alpha – Bravo, Open 2010, 1-0.");
    CHECK(gamesAnswer(refs, bases, Lang::Fr)
              .starts_with("4 parties de Ref ont atteint cette position (+2 =1 −1 pour les Blancs). Les plus fortes : "
                           "Carlsen – Caruana"));
    // The moves, each scored for the side that played it: Black here.
    CHECK(movesAnswer(pos, refs, bases, std::nullopt, Lang::En, PieceLetters::English) ==
          "In the 4 games of Ref: 2…Nc6 (3 games, Black scored 50%), 2…Nf6 (1 game, 0%).");
    CHECK(movesAnswer(pos, refs, bases, std::nullopt, Lang::Fr, PieceLetters::French) ==
          "Dans les 4 parties de Ref : 2…Cc6 (3 parties, les Noirs y ont marqué 50 %), 2…Cf6 (1 partie, "
          "0 %).");
    const auto nc6 = *pos.parseUci("b8c6");
    CHECK(movesAnswer(pos, refs, bases, nc6, Lang::En, PieceLetters::English) ==
          "2…Nc6 was played in 3 games of Ref: Black scored 50%, its players rated 2775 on average, last in 2024.");
    CHECK(movesAnswer(pos, refs, bases, *pos.parseUci("d7d6"), Lang::En, PieceLetters::English) ==
          "No game of Ref played 2…d6 here.");
}

TEST_CASE("references: a position the tree does not keep, a database without a tree, two databases") {
    const Base base("references-search", GAMES);
    // 1.d4 d5: a single game, which the tree does not keep: the search finds it, and that it ended there.
    const std::vector<ReferenceBase> one = {base.reference("Ref")};
    const auto d5                        = after({"d2d4", "d7d5"});
    const auto single                    = references(d5, one);
    CHECK(single.total.games == 1);
    CHECK(single.moves.empty());
    REQUIRE(single.games.size() == 1);
    CHECK(gamesAnswer(single, one, Lang::En) ==
          "One game of Ref reached this position: Charlie – Delta, Open 1999, 1-0.");
    CHECK(movesAnswer(d5, single, one, std::nullopt, Lang::En, PieceLetters::English) ==
          "The games of Ref that reached this position ended there.");
    // No tree: the same numbers from the search.
    const auto pos                        = after({"e2e4", "e7e5", "g1f3"});
    const auto with_tree                  = references(pos, one);
    const std::vector<ReferenceBase> bare = {base.reference("Ref", false)};
    const auto searched                   = references(pos, bare);
    CHECK(searched.total == with_tree.total);
    CHECK(searched.moves == with_tree.moves);
    CHECK(searched.games == with_tree.games);
    // Two databases: counted together, named together.
    const std::vector<ReferenceBase> two = {base.reference("Ref"), base.reference("Copy", false)};
    const auto both                      = references(pos, two);
    CHECK(both.total.games == 8);
    CHECK(both.moves[0].stats.games == 6);
    CHECK(gamesAnswer(both, two, Lang::En).starts_with("8 games of Ref and Copy reached this position"));
    // Nowhere.
    const auto nowhere = references(after({"g1f3"}), one);
    CHECK(nowhere.total.games == 0);
    CHECK(gamesAnswer(nowhere, one, Lang::Fr) == "Aucune partie de Ref n'a atteint cette position.");
    CHECK(counted(12345678, Lang::En) == "12,345,678");
    CHECK(counted(12345678, Lang::Fr) == "12 345 678");
    CHECK(counted(999, Lang::En) == "999");
}

TEST_CASE("question: the games, the moves played, the opening and the tablebases, answered without an engine") {
    const Base base("references-questions", GAMES);
    const std::vector<ReferenceBase> bases = {base.reference("Ref")};
    const auto start                       = Position::initial();
    const auto line                        = test::moves(start, {"e2e4", "e7e5", "g1f3"});
    const auto pos                         = after({"e2e4", "e7e5", "g1f3"});
    Asked asked;
    asked.before = &pos;
    asked.bases  = bases;
    asked.line   = line;
    Scripted silent({});

    // Suggested questions: read without the model.
    asked.question   = "Who played this position?";
    const auto games = answer(asked, silent, nullptr, nullptr);
    REQUIRE(std::holds_alternative<Answered>(games));
    CHECK(std::get<Answered>(games).text.starts_with("4 games of Ref reached this position"));
    CHECK(std::get<Answered>(games).games.size() == 4);
    asked.question    = "What is played here?";
    const auto played = answer(asked, silent, nullptr, nullptr);
    REQUIRE(std::holds_alternative<Answered>(played));
    CHECK(std::get<Answered>(played).moves.size() == 2);
    asked.question     = "What opening is this?";
    const auto opening = answer(asked, silent, nullptr, nullptr);
    REQUIRE(std::holds_alternative<Answered>(opening));
    CHECK(std::get<Answered>(opening).text.starts_with("The opening: King's Knight Opening"));
    asked.lang     = Lang::Fr;
    asked.letters  = PieceLetters::French;
    asked.question = "Quelle est cette ouverture ?";
    CHECK(std::get<Answered>(answer(asked, silent, nullptr, nullptr)).text.starts_with("L'ouverture : "));

    // A move named: the model reads it, the database answers.
    asked.question = "Combien de fois a-t-on joué Cc6 ?";
    Scripted reader({"played Cc6"});
    const auto nc6 = answer(asked, reader, nullptr, nullptr);
    REQUIRE(std::holds_alternative<Answered>(nc6));
    CHECK(std::get<Answered>(nc6).text.starts_with("2…Cc6 a été joué dans 3 parties de Ref"));
    CHECK(std::get<Answered>(nc6).moves == std::vector<Move>{*pos.parseUci("b8c6")});

    // No database; a position of its own has no opening's name.
    Asked lone    = asked;
    lone.bases    = {};
    lone.question = "Qui a joué cette position ?";
    CHECK(std::get<Problem>(answer(lone, silent, nullptr, nullptr)) == Problem::NoDatabase);
    lone.line     = {};
    lone.question = "Quelle est cette ouverture ?";
    CHECK(std::get<Answered>(answer(lone, silent, nullptr, nullptr)).text ==
          "La liste des ouvertures ne nomme pas cette position.");

    // The tablebases: a queen against a bare king wins whoever moves first, here White.
    const auto kqk = test::pos("4k3/8/8/8/8/8/8/3QK3 w - - 0 1");
    const Tablebases tables(TableProbe{3, [](const Position& p) -> std::optional<Verdict> {
                                           const bool white_queen = p.board().occupied().count() == 3;
                                           if (!white_queen) return std::nullopt;
                                           return p.turn() == Color::White ? Verdict::Win : Verdict::Loss;
                                       }});
    Asked endgame;
    endgame.before   = &kqk;
    endgame.question = "What do the tablebases say?";
    const auto told  = answer(endgame, silent, nullptr, &tables);
    REQUIRE(std::holds_alternative<Answered>(told));
    CHECK(std::get<Answered>(told).text == "The tablebases: White wins.");
    CHECK(std::get<Problem>(answer(endgame, silent, nullptr, nullptr)) == Problem::NotInTables);
}
