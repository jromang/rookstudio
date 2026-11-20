// The story of a game told by the assistant's model, measured by hand on real games (#62): each game analysed as the
// Board page analyses it (the continuous analysis, pass after pass up to a ceiling, one line, one thread), its review's
// story written by the model as the review tab has it written, and what the checker made of each passage counted. Not a
// test of the CI: it runs only when its files are given, the model on the processor.
//
//     FCG_STORY_GAMES=games.pgn FCG_STORY_ENGINE=/path/to/uci-engine FCG_STORY_MODEL=model.gguf \
//     FCG_STORY_PROGRAM=build/rookstudio [FCG_STORY_CEILING_MS=320] [FCG_STORY_LANG=en|fr] [FCG_STORY_OUT=file] \
//         build/fastchess-gui-tests -tc='story by the model*'

#include <doctest/doctest.hpp>

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <thread>

#include <commentary/story_model.hpp>
#include <study/import.hpp>
#include <study/pgn_reader.hpp>

#include "assistant_process.hpp"
#include "game_analysis.hpp"
#include "review_bridge.hpp"
#include "review_runner.hpp"

using namespace fcg;
using namespace std::chrono_literals;

namespace {

std::string env(const char* name) {
    const char* v = std::getenv(name);
    return v ? std::string(v) : std::string();
}

}  // namespace

TEST_CASE("story by the model: measured on the games of FCG_STORY_GAMES, by hand") {
    const auto games = env("FCG_STORY_GAMES"), engine = env("FCG_STORY_ENGINE"), model = env("FCG_STORY_MODEL"),
               program = env("FCG_STORY_PROGRAM");
    if (games.empty() || engine.empty() || model.empty() || program.empty()) return;  // by hand only
    const auto ceiling = env("FCG_STORY_CEILING_MS").empty() ? passTime(2) : std::stoll(env("FCG_STORY_CEILING_MS"));
    const auto lang    = env("FCG_STORY_LANG") == "fr" ? commentary::Lang::Fr : commentary::Lang::En;
    const auto letters =
        lang == commentary::Lang::Fr ? commentary::PieceLetters::French : commentary::PieceLetters::English;
    std::ifstream in(games, std::ios::binary);
    REQUIRE(in);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::ofstream file;
    if (!env("FCG_STORY_OUT").empty()) file.open(env("FCG_STORY_OUT"), std::ios::binary);
    std::ostream& out = file.is_open() ? static_cast<std::ostream&>(file) : std::cout;

    AssistantProcess assistant;  // the model on the processor: no layer on a GPU
    std::string error;
    REQUIRE_MESSAGE(assistant.start(program, model, 0, error), error);
    out << "model: " << model << " on " << assistant.devices() << "\n";

    int passages = 0, first = 0, second = 0, fallen = 0, requests = 0;
    double seconds = 0;
    std::map<std::string, int> refusals;
    int n = 0;
    for (const auto pgn : study::splitGames(text)) {
        const auto read = study::readPgnGame(pgn);
        if (!read.tree || read.tree->main.moves.empty()) continue;
        const auto& game = read.tree->main;
        ++n;
        ReviewRunner runner;
        ReviewRunner::Setup setup;
        setup.engine.name      = "engine";
        setup.engine.cmd       = engine;
        setup.engine.protocol  = "uci";
        setup.settings.threads = 1;
        setup.settings.hash    = 64;
        setup.game             = game;
        setup.ceiling_ms       = ceiling;
        runner.start(setup);
        ReviewRunner::State st;
        uint64_t version = 0;
        for (;;) {
            (void)runner.state(st, version);
            if (!st.running) break;
            std::this_thread::sleep_for(50ms);
        }
        REQUIRE_MESSAGE(st.error.empty(), st.error);
        const auto review = commentaryReview(game, false, st.settled);
        REQUIRE(review);
        const auto result = game.result.empty() ? std::string("*") : game.result;
        const commentary::Players players{game.header("White"), game.header("Black")};
        const auto start  = std::chrono::steady_clock::now();
        const auto story  = commentary::writtenStory(assistant, review->line, result, review->report, review->analyses,
                                                     nullptr, lang, letters, players, 0, nullptr, nullptr, &error);
        const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        seconds += took;
        out << "\n=== game " << n << ": " << game.header("White") << " - " << game.header("Black") << ", " << result
            << ", " << game.moves.size() << " plies, " << game.header("Opening") << " (" << took << " s)\n";
        if (!error.empty()) out << "  the model gave no answer: " << error << "\n";
        for (const auto& p : story) {
            ++passages;
            requests += static_cast<int>(p.attempts);
            const char* how = !p.by_model       ? (p.attempts > 0 ? "TEMPLATE (refused twice)" : "TEMPLATE (not asked)")
                              : p.attempts <= 1 ? "model, 1st try"
                                                : "model, 2nd try";
            if (p.by_model && p.attempts <= 1) ++first;
            if (p.by_model && p.attempts >= 2) ++second;
            if (!p.by_model && p.attempts > 0) ++fallen;
            out << "  [" << how << "] " << p.passage.text << "\n";
            for (const auto& [said, why] : p.refused) {
                ++refusals[commentary::debug(why).substr(0, commentary::debug(why).find('('))];
                out << "      refused " << commentary::debug(why) << ": " << said << "\n";
            }
            if (p.by_model) out << "      template: " << p.templ << "\n";
        }
    }
    out << "\n"
        << n << " games, " << passages << " passages: " << first << " at the first try, " << second
        << " at the second, " << fallen << " the templates' (refused twice); " << requests << " requests, " << seconds
        << " s for the stories\nrefusals:";
    for (const auto& [kind, count] : refusals) out << " " << kind << " " << count;
    out << "\n";
    CHECK(n > 0);
}
