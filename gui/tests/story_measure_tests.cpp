// The story of a game told by the assistant's model, measured by hand on real games (#62): each game analysed as the
// study Board analyses it (pass after pass up to a ceiling, each position for a number of nodes on one thread from an
// empty hash, the second line where a move can be an only move: #61, #63), its review's story written by the model as
// the review tab has it written, and what the checker made of each passage counted. Not a test of the CI: it runs only
// when its files are given, the model on the processor.
//
//     FCG_STORY_GAMES=games.pgn FCG_STORY_ENGINE=/path/to/uci-engine FCG_STORY_MODEL=model.gguf \
//     FCG_STORY_PROGRAM=build/rookstudio [FCG_STORY_CEILING_MS=320] [FCG_STORY_LANG=en|fr] [FCG_STORY_OUT=file] \
//     [FCG_STORY_ANALYSES=dir] [FCG_REVIEW_PROCESSES=1] build/fastchess-gui-tests -tc='story by the model: measured*'
//
// FCG_STORY_ANALYSES keeps each game's analysis in a folder (as the profile's analyses/ keeps them), read back at the
// next run: the measures before and after a change of the checker then put the same facts to the model (#64). With it:
// - -tc='story by the model: the passages*' and FCG_STORY_RECHECK=an earlier FCG_STORY_OUT puts that measure's
//   passages to the checker as it is now, without the model (FCG_STORY_PLIES="3:21,4:50" prints those moves' positions
//   and lines, for tests);
// - -tc='comments by the model*' has the model comment the key moments as the Board page has it do;
// - -tc='templates*' and FCG_STORY_TEMPLATES=1 counts the templates' sentences the checks of the boards leave out.
// FCG_STORY_PROMPTS=1 prints each prompt, the position and the engine's lines.

#include <doctest/doctest.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <sstream>
#include <thread>

#include <json.hpp>

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
#include "topology.hpp"

using namespace fcg;
using namespace std::chrono_literals;

namespace {

std::string env(const char* name) {
    const char* v = std::getenv(name);
    return v ? std::string(v) : std::string();
}

// The review's engine processes: FCG_REVIEW_PROCESSES, else the Chess space's default on this machine, the analysis
// on one thread and no tournament playing.
int processesOf() {
    if (!env("FCG_REVIEW_PROCESSES").empty()) return std::stoi(env("FCG_REVIEW_PROCESSES"));
    const auto cpu = detectTopology();
    return reviewProcesses(1, cpu.physical, cpu.logical, 0);
}

// The game's analysis: the one kept in `dir` when there is one, else the study Board's run here, then kept there.
GameReview analysed(const ReplayGame& game, const std::string& engine, int64_t ceiling, const std::string& dir) {
    if (!dir.empty()) {
        if (auto kept = loadReview(dir, game); kept && kept->complete(game.moves.size())) return *kept;
    }
    ReviewRunner runner;
    ReviewRunner::Setup setup;
    setup.engine.name     = "engine";
    setup.engine.cmd      = engine;
    setup.engine.protocol = "uci";
    setup.game            = game;
    setup.ceiling_ms      = ceiling;
    setup.reproducible    = true;
    setup.processes       = processesOf();
    setup.second_line     = secondLines(game, false);
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

#ifdef FCG_ASSISTANT  // the model runs in the assistant's process
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
#endif  // FCG_ASSISTANT

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

// The templates' sentences the checks of the boards leave out (#64): the key moments' comments as the Review tab and
// "Comment the key moments" without the model have them, and the story's passages, each sentence read against the
// facts it is told from. No model is asked. FCG_STORY_TEMPLATES=1 runs it; each sentence left out is written with the
// prompt the model would have had for it and the engine's lines, to be read against the board.
TEST_CASE("templates: the sentences the checks of the boards leave out, by hand") {
    const auto measure = Measure::fromEnv(false);
    if (!measure || env("FCG_STORY_TEMPLATES").empty()) return;  // by hand only
    const auto lang    = measure->lang;
    const auto letters = measure->letters;
    std::ofstream file;
    if (!env("FCG_STORY_OUT").empty()) file.open(env("FCG_STORY_OUT"), std::ios::binary);
    std::ostream& out = file.is_open() ? static_cast<std::ostream&>(file) : std::cout;

    struct Count {
        int texts = 0, sentences = 0, refused = 0, emptied = 0;
    };
    Count comments, passages;
    std::map<std::string, int> kinds;
    const auto read = [&](const std::string& what, const std::string& text, const auto& brief, Count& count,
                          const std::string& context) {
        ++count.texts;
        int kept = 0;
        for (const auto sentence : commentary::brief_detail::sentenceTexts(text)) {
            ++count.sentences;
            const auto why = brief.checkTemplate(sentence);
            if (!why) {
                ++kept;
                continue;
            }
            ++count.refused;
            ++kinds[commentary::debug(*why).substr(0, commentary::debug(*why).find('('))];
            out << "  [" << what << "] LEFT OUT " << commentary::debug(*why) << ": " << sentence
                << "\n      in: " << text << "\n"
                << context << "\n";
        }
        if (kept == 0) ++count.emptied;
    };
    int n = 0;
    for (const auto pgn : study::splitGames(measure->text)) {
        const auto game_read = study::readPgnGame(pgn);
        if (!game_read.tree || game_read.tree->main.moves.empty()) continue;
        const auto& game = game_read.tree->main;
        ++n;
        const auto settled = analysed(game, measure->engine, measure->ceiling, measure->analyses);
        const auto review  = commentaryReview(game, false, settled);
        REQUIRE(review);
        const auto result = game.result.empty() ? std::string("*") : game.result;
        const commentary::Players players{game.header("White"), game.header("Black")};
        out << "\n=== game " << n << ": " << game.header("White") << " - " << game.header("Black") << ", " << result
            << "\n";
        for (const auto& [ply, templ] : commentary::templateComments(review->line, review->report, review->analyses, {},
                                                                     nullptr, nullptr, lang, letters)) {
            const auto* m = review->report.get(ply);
            REQUIRE(m);
            const auto ctx = commentary::moveContext(review->line, *m, review->analyses, nullptr, nullptr, nullptr);
            const commentary::Brief brief(ctx, lang, letters);
            read("comment " + commentary::moveText(*ctx.before, ctx.played, letters), templ, brief, comments,
                 "    prompt:\n" + brief.prompt + "\n" + linesAt(review->line, ply, review->analyses));
        }
        for (const auto& t : commentary::story_detail::told(review->line, result, review->report, review->analyses,
                                                            nullptr, lang, letters)) {
            if (t.passage.lesson) continue;
            const commentary::StoryBrief brief(t, review->line, review->analyses, nullptr, lang, letters, players);
            read("passage", t.passage.text, brief, passages,
                 "    prompt:\n" + brief.prompt + "\n" +
                     (t.source.move ? linesAt(review->line, t.source.move->ply, review->analyses) : std::string()));
        }
    }
    out << "\n"
        << n << " games. Key moments' comments: " << comments.texts << ", " << comments.sentences << " sentences, "
        << comments.refused << " left out, " << comments.emptied
        << " comments left with none. Story passages: " << passages.texts << ", " << passages.sentences
        << " sentences, " << passages.refused << " left out, " << passages.emptied
        << " passages left with none.\nrefusals:";
    for (const auto& [kind, count] : kinds) out << " " << kind << " " << count;
    out << "\n";
    CHECK(n > 0);
}

#ifdef FCG_ASSISTANT  // the model runs in the assistant's process
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
#endif  // FCG_ASSISTANT

// ---------------------------------------------------------------------------
// The review itself, measured by hand (#61, #63): each game of FCG_STORY_GAMES analysed as the Board page analyses it,
// each pass kept in FCG_REVIEW_OUT/pass<k> (as the profile's analyses/ keeps them) with the time it took; then
// compared with a deeper analysis, or with another run of the same method.
//
//     FCG_STORY_GAMES=games.pgn FCG_STORY_ENGINE=/path/to/uci-engine FCG_REVIEW_OUT=dir [FCG_STORY_CEILING_MS=5120]
//     FCG_REVIEW_METHOD=time|nodes|depth:D|nodes:N [FCG_REVIEW_THREADS=1] [FCG_REVIEW_PROCESSES=1]
//     [FCG_REVIEW_SECOND=none|matters|all] build/fastchess-gui-tests -tc='review: measured*'
//
// time: the Engines space's review, passTime() up to the ceiling, FCG_REVIEW_THREADS threads, the hash kept from one
// position to the next; nodes: the Chess space's, passNodes() up to the ceiling, one thread, an empty hash for each
// position, FCG_REVIEW_PROCESSES processes (the Chess space's default on this machine without it, reviewProcesses());
// depth:D and nodes:N, one pass so, as nodes otherwise. FCG_REVIEW_SECOND: the second line where the Chess space asks
// for it (matters), everywhere (all), or nowhere.
//
//     FCG_STORY_GAMES=games.pgn FCG_REVIEW_REFERENCE=dir FCG_REVIEW_COMPARE="dir/pass2 other/pass2 ..."
//     build/fastchess-gui-tests -tc='review: compared*'
//
// The reference: a deeper analysis written by a script, <n>.json for the n-th game (GameReview's format, positions
// only). Each analysis's verdicts, key moments, best moves, expected scores and only moves against it; two analyses
// of the same games against each other with FCG_REVIEW_SAME=1: what differs between them, position by position.

namespace {

struct Method {
    enum class Kind { Time, Nodes, Depth, OnePass } kind = Kind::Time;
    uint64_t value                                       = 0;  // the depth, or the nodes of the one pass
    int threads                                          = 1;
    int processes                                        = 1;
    std::string second;  // none, matters, all
    int64_t ceiling = 5120;

    static Method fromEnv() {
        Method m;
        const auto method = env("FCG_REVIEW_METHOD");
        if (method == "nodes") m.kind = Kind::Nodes;
        if (method.starts_with("depth:")) {
            m.kind  = Kind::Depth;
            m.value = std::stoull(method.substr(6));
        }
        if (method.starts_with("nodes:")) {
            m.kind  = Kind::OnePass;
            m.value = std::stoull(method.substr(6));
        }
        if (!env("FCG_REVIEW_THREADS").empty()) m.threads = std::stoi(env("FCG_REVIEW_THREADS"));
        m.processes = processesOf();
        m.second = env("FCG_REVIEW_SECOND").empty() ? "none" : env("FCG_REVIEW_SECOND");
        if (!env("FCG_STORY_CEILING_MS").empty()) m.ceiling = std::stoll(env("FCG_STORY_CEILING_MS"));
        if (m.kind == Kind::Depth || m.kind == Kind::OnePass) m.ceiling = FIRST_PASS_MS;  // one pass
        return m;
    }
};

// The game's analysis by `method`, each pass completed kept in out/pass<k>: the seconds each pass ended at, and the
// second searches made.
std::vector<double> measuredReview(const ReplayGame& game, const std::string& engine, const Method& method,
                                   const std::string& out, std::atomic<int>& seconds_asked) {
    ReviewRunner runner;
    ReviewRunner::Setup setup;
    setup.engine.name      = "engine";
    setup.engine.cmd       = engine;
    setup.engine.protocol  = "uci";
    setup.settings.threads = method.threads;
    setup.game             = game;
    setup.ceiling_ms       = method.ceiling;
    if (method.kind != Method::Kind::Time) {
        setup.reproducible = true;
        setup.processes    = method.processes;
        if (method.kind == Method::Kind::Depth)
            setup.limit = [d = static_cast<int>(method.value)](int) { return SearchLimit{0, d, 0}; };
        if (method.kind == Method::Kind::OnePass)
            setup.limit = [n = method.value](int) { return SearchLimit{0, 0, n}; };
        if (method.second == "matters")
            setup.second_line = [&, wanted = secondLines(game, false)](int ply, const PositionEval& first) {
                const bool asked = wanted(ply, first);
                seconds_asked += asked;
                return asked;
            };
        if (method.second == "all")
            setup.second_line = [&, plies = static_cast<int>(game.moves.size())](int ply, const PositionEval&) {
                seconds_asked += ply < plies;
                return ply < plies;
            };
    }
    const auto start = std::chrono::steady_clock::now();
    runner.start(setup);
    ReviewRunner::State st;
    uint64_t version = 0;
    std::vector<double> ends;
    for (;;) {
        const bool changed = runner.state(st, version);
        if (changed && st.settled.passes > static_cast<int>(ends.size())) {
            ends.push_back(std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
            const auto dir = out + "/pass" + std::to_string(st.settled.passes);
            std::filesystem::create_directories(dir);
            CHECK(saveReview(dir, game, st.settled));
        }
        if (!st.running && !changed) break;
        std::this_thread::sleep_for(20ms);
    }
    REQUIRE_MESSAGE(st.error.empty(), st.error);
    return ends;
}

bool sameScore(const std::optional<fastchess::Score>& a, const std::optional<fastchess::Score>& b) {
    if (!a || !b) return !a && !b;
    return a->type == b->type && a->value == b->value;
}

// The n-th game's reference analysis (<dir>/<n>.json, positions only), as loadReview() reads one.
std::optional<GameReview> referenceOf(const std::string& dir, int n, const ReplayGame& game) {
    std::ifstream in(dir + "/" + std::to_string(n) + ".json", std::ios::binary);
    if (!in) return std::nullopt;
    auto j = nlohmann::json::parse(in, nullptr, false);
    if (j.is_discarded()) return std::nullopt;
    j.erase("moves");
    const auto tmp = std::filesystem::temp_directory_path() / ("reference-" + std::to_string(n));
    std::filesystem::create_directories(tmp);
    nlohmann::json file;
    file["key"]      = reviewKey(game);
    file["analyses"] = nlohmann::json::array({j});
    {
        std::ofstream f(tmp / (reviewKey(game) + ".json"), std::ios::binary);
        f << file.dump();
    }
    return loadReview(tmp, game);
}

}  // namespace

TEST_CASE("review: measured on the games of FCG_STORY_GAMES, by hand") {
    const auto measure = Measure::fromEnv(false);
    const auto out     = env("FCG_REVIEW_OUT");
    if (!measure || out.empty()) return;  // by hand only
    const auto method = Method::fromEnv();
    std::ofstream log(out + "/times.txt", std::ios::app);
    int n         = 0;
    double total  = 0;
    int positions = 0;
    std::atomic<int> asked{0};
    for (const auto pgn : study::splitGames(measure->text)) {
        const auto read = study::readPgnGame(pgn);
        if (!read.tree || read.tree->main.moves.empty()) continue;
        const auto& game = read.tree->main;
        ++n;
        const auto ends = measuredReview(game, measure->engine, method, out, asked);
        REQUIRE_FALSE(ends.empty());
        total += ends.back();
        positions += static_cast<int>(game.moves.size()) + 1;
        log << "game " << n << " (" << game.moves.size() << " plies):";
        for (std::size_t k = 0; k < ends.size(); ++k) log << " pass " << k + 1 << " at " << ends[k] << " s;";
        log << "\n";
        log.flush();
    }
    log << n << " games, " << positions << " positions, " << total << " s; second lines asked " << asked.load() << "\n";
    CHECK(n > 0);
}

TEST_CASE("review: compared with a reference, by hand") {
    const auto measure = Measure::fromEnv(false);
    const auto compare = env("FCG_REVIEW_COMPARE");
    const auto refdir  = env("FCG_REVIEW_REFERENCE");
    if (!measure || compare.empty() || (refdir.empty() && env("FCG_REVIEW_SAME").empty())) return;  // by hand only
    std::vector<std::string> dirs;
    {
        std::istringstream in(compare);
        for (std::string d; in >> d;) dirs.push_back(d);
    }
    std::vector<ReplayGame> games;
    for (const auto pgn : study::splitGames(measure->text)) {
        const auto read = study::readPgnGame(pgn);
        if (read.tree && !read.tree->main.moves.empty()) games.push_back(read.tree->main);
    }
    const auto judged_of = [](const std::shared_ptr<const CommentaryReview>& r) {
        return r ? judgedMoves(*r) : std::vector<MoveReview>{};
    };
    if (!env("FCG_REVIEW_SAME").empty()) {
        // Two runs of a method: what differs, position by position, and in what the review makes of them.
        REQUIRE(dirs.size() == 2);
        int positions = 0, scores = 0, bests = 0, lines = 0, verdicts = 0, moves = 0, keys = 0;
        for (std::size_t g = 0; g < games.size(); ++g) {
            const auto a = loadReview(dirs[0], games[g]), b = loadReview(dirs[1], games[g]);
            if (!a || !b) continue;
            for (std::size_t p = 0; p < a->positions.size() && p < b->positions.size(); ++p) {
                const auto &x = a->positions[p], &y = b->positions[p];
                ++positions;
                scores += !sameScore(x.score, y.score);
                bests += x.best != y.best;
                lines += x.pv != y.pv || x.second_pv != y.second_pv || !sameScore(x.second_score, y.second_score);
            }
            const auto ra = commentaryReview(games[g], false, *a), rb = commentaryReview(games[g], false, *b);
            const auto ja = judged_of(ra), jb = judged_of(rb);
            for (std::size_t i = 0; i < ja.size() && i < jb.size(); ++i) {
                ++moves;
                verdicts += ja[i].judgement != jb[i].judgement;
            }
            keys += ra && rb && ra->report.key_moments != rb->report.key_moments;
        }
        std::cout << dirs[0] << " against " << dirs[1] << ": " << positions << " positions, " << scores
                  << " scores differ, " << bests << " best moves, " << lines << " lines; " << moves << " moves, "
                  << verdicts << " verdicts differ; " << keys << " of " << games.size()
                  << " games with other key moments\n";
        return;
    }
    for (const auto& dir : dirs) {
        int moves = 0, same = 0, ref_errors = 0, found = 0, false_errors = 0, best_n = 0, best_same = 0;
        int only = 0, keys_inter = 0, keys_union = 0, errors = 0;
        double err   = 0;
        int err_n    = 0;
        int compared = 0;
        for (std::size_t g = 0; g < games.size(); ++g) {
            const auto ref  = referenceOf(refdir, static_cast<int>(g + 1), games[g]);
            const auto mine = loadReview(dir, games[g]);
            if (!ref || !mine) continue;  // not analysed (yet)
            ++compared;
            const auto rr = commentaryReview(games[g], false, *ref), rm = commentaryReview(games[g], false, *mine);
            REQUIRE(rr);
            REQUIRE(rm);
            const auto jr = judgedMoves(*rr), jm = judgedMoves(*rm);
            for (std::size_t i = 0; i < jr.size() && i < jm.size(); ++i) {
                ++moves;
                same += jr[i].judgement == jm[i].judgement;
                const bool r_err = jr[i].judgement >= Judgement::Mistake, m_err = jm[i].judgement >= Judgement::Mistake;
                ref_errors += r_err;
                errors += m_err;
                found += r_err && m_err;
                false_errors += m_err && jr[i].judgement == Judgement::None;
            }
            for (const auto& m : rm->report.moves) only += m.only_move;
            std::vector<std::size_t> inter;
            std::set_intersection(rr->report.key_moments.begin(), rr->report.key_moments.end(),
                                  rm->report.key_moments.begin(), rm->report.key_moments.end(),
                                  std::back_inserter(inter));
            keys_inter += static_cast<int>(inter.size());
            keys_union +=
                static_cast<int>(rr->report.key_moments.size() + rm->report.key_moments.size() - inter.size());
            for (std::size_t p = 0; p < ref->positions.size() && p < mine->positions.size(); ++p) {
                const auto &x = ref->positions[p], &y = mine->positions[p];
                if (!x.best.empty() && !y.best.empty()) {
                    ++best_n;
                    best_same += x.best == y.best;
                }
                const auto ex = rr->analyses[p] ? rr->analyses[p]->whiteExpected() : std::nullopt;
                const auto ey = rm->analyses[p] ? rm->analyses[p]->whiteExpected() : std::nullopt;
                if (ex && ey) {
                    err += std::abs(*ex - *ey);
                    ++err_n;
                }
            }
        }
        std::cout << dir << " (" << compared << " games): " << moves << " moves, verdicts as the reference's "
                  << 100.0 * same / std::max(1, moves) << " %; mistakes and blunders " << errors << " (the reference's "
                  << ref_errors << ", " << found << " found, " << false_errors
                  << " on moves it does not mark); key moments in common "
                  << 100.0 * keys_inter / std::max(1, keys_union) << " %; best move as the reference's "
                  << 100.0 * best_same / std::max(1, best_n) << " %; expected score off by " << err / std::max(1, err_n)
                  << " points; " << only << " only moves\n";
    }
}

// The facts and prompts the assistant gets for the key moments of FCG_STORY_GAMES, as "Comment the key moments" puts
// them to it, written to FCG_STORY_OUT without the model: two reviews of the same games give the same file (#61).
TEST_CASE("comments by the model: the prompts of the key moments, by hand") {
    const auto measure = Measure::fromEnv(false);
    if (!measure || env("FCG_STORY_OUT").empty()) return;  // by hand only
    std::ofstream out(env("FCG_STORY_OUT"), std::ios::binary);
    EngineEntry engine;
    engine.name     = "engine";
    engine.cmd      = measure->engine;
    engine.protocol = "uci";
    const EngineOracle oracle(engine);
    int n = 0;
    for (const auto pgn : study::splitGames(measure->text)) {
        const auto read = study::readPgnGame(pgn);
        if (!read.tree || read.tree->main.moves.empty()) continue;
        const auto& game = read.tree->main;
        ++n;
        const auto settled = analysed(game, measure->engine, measure->ceiling, measure->analyses);
        const auto review  = commentaryReview(game, false, settled);
        REQUIRE(review);
        out << "=== game " << n << "\nkey moments:";
        for (const auto ply : review->report.key_moments) out << " " << ply;
        out << "\n";
        for (const auto& [ply, templ] :
             commentary::templateComments(review->line, review->report, review->analyses, {}, nullptr, nullptr,
                                          measure->lang, measure->letters)) {
            const auto* m = review->report.get(ply);
            REQUIRE(m);
            const auto ctx = commentary::moveContext(review->line, *m, review->analyses, nullptr, nullptr, &oracle);
            const commentary::Brief brief(ctx, measure->lang, measure->letters);
            out << "\n--- " << commentary::moveText(*ctx.before, ctx.played, measure->letters)
                << "\ntemplate: " << templ << "\nprompt:\n"
                << brief.prompt << "\n"
                << linesAt(review->line, ply, review->analyses) << "\n";
        }
    }
    CHECK(n > 0);
}
