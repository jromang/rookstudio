// The story of a game told by the assistant's model, measured by hand on real games (#62): each game analysed as the
// Board page analyses it (the continuous analysis, pass after pass up to a ceiling, one line, one thread), its review's
// story written by the model as the review tab has it written, and what the checker made of each passage counted. Not a
// test of the CI: it runs only when its files are given, the model on the processor.
//
//     FCG_STORY_GAMES=games.pgn FCG_STORY_ENGINE=/path/to/uci-engine FCG_STORY_MODEL=model.gguf \
//     FCG_STORY_PROGRAM=build/rookstudio [FCG_STORY_CEILING_MS=320] [FCG_STORY_LANG=en|fr] [FCG_STORY_OUT=file] \
//     [FCG_STORY_ANALYSES=dir] build/fastchess-gui-tests -tc='story by the model: measured*'
//
// FCG_STORY_ANALYSES keeps each game's analysis in a folder (as the profile's analyses/ keeps them), read back at the
// next run: the measures before and after a change of the checker then put the same facts to the model (#64). With it:
// - -tc='story by the model: the passages*' and FCG_STORY_RECHECK=an earlier FCG_STORY_OUT puts that measure's
//   passages to the checker as it is now, without the model (FCG_STORY_PLIES="3:21,4:50" prints those moves' positions
//   and lines, for tests);
// - -tc='comments by the model*' has the model comment the key moments as the Board page has it do.
// FCG_STORY_PROMPTS=1 prints each prompt, the position and the engine's lines.

#include <doctest/doctest.hpp>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <thread>

#include <commentary/commentator.hpp>
#include <commentary/story_model.hpp>
#include <commentary/text.hpp>
#include <commentary/writing.hpp>
#include <study/import.hpp>
#include <study/pgn_reader.hpp>

#include "assistant_process.hpp"
#include "engine_oracle.hpp"
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

// The game's analysis: the one kept in `dir` when there is one, else the continuous analysis of the Board page run
// here, then kept there.
GameReview analysed(const ReplayGame& game, const std::string& engine, int64_t ceiling, const std::string& dir) {
    if (!dir.empty()) {
        if (auto kept = loadReview(dir, game); kept && kept->complete(game.moves.size())) return *kept;
    }
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
    if (!dir.empty()) CHECK(saveReview(dir, game, st.settled));
    return st.settled;
}

// What the measures are given.
struct Measure {
    std::string games, engine, model, program, analyses;
    int64_t ceiling                  = 0;
    commentary::Lang lang            = commentary::Lang::En;
    commentary::PieceLetters letters = commentary::PieceLetters::English;
    std::string text;  // the games' PGN

    static std::optional<Measure> fromEnv(bool model = true) {
        Measure m;
        m.games    = env("FCG_STORY_GAMES");
        m.engine   = env("FCG_STORY_ENGINE");
        m.model    = env("FCG_STORY_MODEL");
        m.program  = env("FCG_STORY_PROGRAM");
        m.analyses = env("FCG_STORY_ANALYSES");
        if (m.games.empty() || m.engine.empty() || (model && (m.model.empty() || m.program.empty())))
            return std::nullopt;
        m.ceiling = env("FCG_STORY_CEILING_MS").empty() ? passTime(2) : std::stoll(env("FCG_STORY_CEILING_MS"));
        m.lang    = env("FCG_STORY_LANG") == "fr" ? commentary::Lang::Fr : commentary::Lang::En;
        m.letters =
            m.lang == commentary::Lang::Fr ? commentary::PieceLetters::French : commentary::PieceLetters::English;
        std::ifstream in(m.games, std::ios::binary);
        REQUIRE(in);
        m.text.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        return m;
    }
};

// The position before the move `ply` and the engine's lines there and after the move, in UCI with White's centipawns:
// what a test of the checker is made of.
std::string linesAt(const commentary::MainLine& line, std::size_t ply, const commentary::Analyses& analyses) {
    if (ply < 1 || ply >= line.positions().size()) return {};
    const auto pv = [](const std::optional<commentary::PositionAnalysis>& a) {
        std::string out;
        if (!a || !a->best()) return out;
        for (const auto& m : a->best()->moves) out += (out.empty() ? "" : " ") + commentary::uci(m);
        return out + " (" + std::to_string(a->best()->whiteCp()) + ")";
    };
    return "fen " + line.positions()[ply - 1].fen() + "\nplayed " + commentary::uci(line.moves()[ply - 1]) + "\nbest " +
           pv(analyses[ply - 1]) + "\nreply " + pv(analyses[ply]);
}

// A passage of a story as an earlier measure wrote it (FCG_STORY_OUT): the model's text or the template's, and the
// template's.
struct Written {
    std::string text, templ;
    bool by_model = false;
};

// The passages of each game of a measure's output, by the game's number.
std::map<int, std::vector<Written>> writtenBefore(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    REQUIRE_MESSAGE(in, path);
    std::map<int, std::vector<Written>> out;
    int game = 0;
    for (std::string line; std::getline(in, line);) {
        const auto after = [&](std::string_view mark) { return line.substr(line.find(mark) + mark.size()); };
        if (line.starts_with("=== game ")) {
            game = std::stoi(line.substr(9));
        } else if (line.starts_with("  [model, ")) {
            out[game].push_back({after("] "), {}, true});
        } else if (line.starts_with("  [TEMPLATE")) {
            const auto text = after("] ");
            out[game].push_back({text, text, false});
        } else if (line.starts_with("      template: ") && !out[game].empty()) {
            out[game].back().templ = after("template: ");
        }
    }
    return out;
}

}  // namespace

TEST_CASE("story by the model: measured on the games of FCG_STORY_GAMES, by hand") {
    const auto measure = Measure::fromEnv();
    if (!measure) return;  // by hand only
    const auto lang    = measure->lang;
    const auto letters = measure->letters;
    std::ofstream file;
    if (!env("FCG_STORY_OUT").empty()) file.open(env("FCG_STORY_OUT"), std::ios::binary);
    std::ostream& out = file.is_open() ? static_cast<std::ostream&>(file) : std::cout;

    AssistantProcess assistant;  // the model on the processor: no layer on a GPU
    std::string error;
    REQUIRE_MESSAGE(assistant.start(measure->program, measure->model, 0, error), error);
    out << "model: " << measure->model << " on " << assistant.devices() << "\n";

    int passages = 0, first = 0, second = 0, fallen = 0, requests = 0;
    double seconds = 0;
    std::map<std::string, int> refusals;
    int n = 0;
    for (const auto pgn : study::splitGames(measure->text)) {
        const auto read = study::readPgnGame(pgn);
        if (!read.tree || read.tree->main.moves.empty()) continue;
        const auto& game = read.tree->main;
        ++n;
        const auto settled = analysed(game, measure->engine, measure->ceiling, measure->analyses);
        const auto review  = commentaryReview(game, false, settled);
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

// The passages an earlier measure wrote (FCG_STORY_RECHECK, its FCG_STORY_OUT) put to the checker as it is now, each
// beside the passage of the same template: what a change of the checker would refuse of them. No model is asked; the
// games' analyses are those kept in FCG_STORY_ANALYSES, so that the passages' facts are the ones they were written
// from.
TEST_CASE("story by the model: the passages of FCG_STORY_RECHECK checked again, by hand") {
    const auto measure = Measure::fromEnv(false);
    if (!measure || env("FCG_STORY_RECHECK").empty()) return;  // by hand only
    const auto before  = writtenBefore(env("FCG_STORY_RECHECK"));
    const bool prompts = !env("FCG_STORY_PROMPTS").empty();
    std::ofstream file;
    if (!env("FCG_STORY_OUT").empty()) file.open(env("FCG_STORY_OUT"), std::ios::binary);
    std::ostream& out = file.is_open() ? static_cast<std::ostream&>(file) : std::cout;

    int compared = 0, kept = 0, refused = 0, unmatched = 0, n = 0;
    for (const auto pgn : study::splitGames(measure->text)) {
        const auto read = study::readPgnGame(pgn);
        if (!read.tree || read.tree->main.moves.empty()) continue;
        const auto& game = read.tree->main;
        ++n;
        const auto settled = analysed(game, measure->engine, measure->ceiling, measure->analyses);
        const auto review  = commentaryReview(game, false, settled);
        REQUIRE(review);
        const auto result = game.result.empty() ? std::string("*") : game.result;
        const commentary::Players players{game.header("White"), game.header("Black")};
        out << "\n=== game " << n << ": " << game.header("White") << " - " << game.header("Black") << "\n";
        // FCG_STORY_PLIES="3:21,4:50": the position and the lines of these moves, for tests of the checker.
        for (const auto item : commentary::text::split(env("FCG_STORY_PLIES"), [](char32_t c) { return c == U','; })) {
            const auto colon = item.find(':');
            if (colon == std::string_view::npos || std::stoi(std::string(item.substr(0, colon))) != n) continue;
            out << linesAt(review->line, std::stoul(std::string(item.substr(colon + 1))), review->analyses) << "\n";
        }
        const auto it      = before.find(n);
        const auto written = it == before.end() ? std::vector<Written>{} : it->second;
        for (const auto& t : commentary::story_detail::told(review->line, result, review->report, review->analyses,
                                                            nullptr, measure->lang, measure->letters)) {
            if (t.passage.lesson) continue;
            const auto same = std::find_if(written.begin(), written.end(),
                                           [&](const Written& w) { return w.templ == t.passage.text; });
            if (same == written.end()) {
                ++unmatched;
                out << "  [no passage of this template] " << t.passage.text << "\n";
                continue;
            }
            const commentary::StoryBrief brief(t, review->line, review->analyses, nullptr, measure->lang,
                                               measure->letters, players);
            if (prompts) {
                out << "    prompt:\n" << brief.prompt << "\n";
                if (t.source.move) out << linesAt(review->line, t.source.move->ply, review->analyses) << "\n";
            }
            if (!same->by_model) {
                out << "  [template] " << same->text << "\n";
                continue;
            }
            ++compared;
            if (const auto why = brief.check(same->text)) {
                ++refused;
                out << "  [REFUSED " << commentary::debug(*why) << "] " << same->text << "\n";
            } else {
                ++kept;
                out << "  [kept] " << same->text << "\n";
            }
        }
    }
    out << "\n"
        << n << " games: " << compared << " passages of the model compared, " << kept << " kept, " << refused
        << " refused; " << unmatched << " passages whose template the earlier measure did not have\n";
    CHECK(n > 0);
}

TEST_CASE("comments by the model: the key moments of FCG_STORY_GAMES, by hand") {
    const auto measure = Measure::fromEnv();
    if (!measure) return;  // by hand only
    const auto lang    = measure->lang;
    const auto letters = measure->letters;
    std::ofstream file;
    if (!env("FCG_STORY_OUT").empty()) file.open(env("FCG_STORY_OUT"), std::ios::binary);
    std::ostream& out  = file.is_open() ? static_cast<std::ostream&>(file) : std::cout;
    const bool prompts = !env("FCG_STORY_PROMPTS").empty();

    AssistantProcess assistant;  // the model on the processor: no layer on a GPU
    std::string error;
    REQUIRE_MESSAGE(assistant.start(measure->program, measure->model, 0, error), error);
    out << "model: " << measure->model << " on " << assistant.devices() << "\n";
    // The facts confirmed as the Board page has them confirmed: the analysis engine, 300,000 nodes per position.
    EngineEntry engine;
    engine.name     = "engine";
    engine.cmd      = measure->engine;
    engine.protocol = "uci";
    const EngineOracle oracle(engine);

    int moments = 0, first = 0, later = 0, fallen = 0, requests = 0;
    double seconds = 0;
    std::map<std::string, int> refusals;
    int n = 0;
    for (const auto pgn : study::splitGames(measure->text)) {
        const auto read = study::readPgnGame(pgn);
        if (!read.tree || read.tree->main.moves.empty()) continue;
        const auto& game = read.tree->main;
        ++n;
        const auto settled = analysed(game, measure->engine, measure->ceiling, measure->analyses);
        const auto review  = commentaryReview(game, false, settled);
        REQUIRE(review);
        out << "\n=== game " << n << ": " << game.header("White") << " - " << game.header("Black") << ", "
            << game.moves.size() << " plies\n";
        // The key moments as "Comment the key moments" picks them, each written as the Board page has it written.
        const auto picked = commentary::templateComments(review->line, review->report, review->analyses, {}, nullptr,
                                                         nullptr, lang, letters);
        for (const auto& [ply, templ] : picked) {
            const auto* m = review->report.get(ply);
            REQUIRE(m);
            ++moments;
            const auto ctx = commentary::moveContext(review->line, *m, review->analyses, nullptr, nullptr, &oracle);
            const commentary::Brief brief(ctx, lang, letters);
            const auto mv = commentary::moveText(*ctx.before, ctx.played, letters);
            out << "\n  " << mv << "\n";
            if (prompts)
                out << "    prompt:\n" << brief.prompt << "\n" << linesAt(review->line, ply, review->analyses) << "\n";
            const uint32_t attempts = brief.hasSubstance() ? commentary::ATTEMPTS : 0;
            bool written            = false;
            for (uint32_t attempt = 1; attempt <= attempts && !written; ++attempt) {
                const auto start  = std::chrono::steady_clock::now();
                const auto answer = assistant.generate(commentary::commentRequest(brief, attempt), &error);
                seconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
                ++requests;
                REQUIRE_MESSAGE(answer, error);
                if (const auto refused = brief.check(answer->text)) {
                    ++refusals[commentary::debug(*refused).substr(0, commentary::debug(*refused).find('('))];
                    out << "    refused " << commentary::debug(*refused) << ": " << answer->text << "\n";
                    continue;
                }
                written = true;
                (attempt == 1 ? first : later) += 1;
                out << "    [model, try " << attempt << "] " << answer->text << "\n";
            }
            if (!written) {
                if (attempts > 0) ++fallen;
                out << "    [TEMPLATE" << (attempts > 0 ? ", refused" : ", not asked") << "]\n";
            }
            out << "    template: " << templ << "\n";
        }
    }
    out << "\n"
        << n << " games, " << moments << " key moments: " << first << " at the first try, " << later << " later, "
        << fallen << " the templates' (refused each time); " << requests << " requests, " << seconds << " s\nrefusals:";
    for (const auto& [kind, count] : refusals) out << " " << kind << " " << count;
    out << "\n";
    CHECK(n > 0);
}
