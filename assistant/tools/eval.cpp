// fcg-assistant-eval: a model measured on commentary cases the way the program uses it (the prototype's
// `rookstudio-dataset eval`). Each case goes through the program's own path, attempts, checker and template fallback;
// what counts is how often the model's text stands, how soon, and how fast. Not built by default:
// -DFCG_ASSISTANT_EVAL=ON.
//
//     fcg-assistant-eval CASES MODEL [--out FILE] [--lang en|fr|both] [--limit N] [--gpu-layers N]
//     fcg-assistant-eval --questions QUESTIONS MODEL [--gpu-layers N]
//
// With --questions, each line of QUESTIONS ({"fen", "played", "lang", "question", "expect"}, assistant/tools/
// questions.jsonl) is a question about a position and the action it asks for: read as the program reads it, a
// suggested question without the model, any other by the model held to the action grammar.

#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include <json.hpp>

#include <assistant/generator.hpp>
#include <commentary/case.hpp>
#include <commentary/commentator.hpp>
#include <commentary/openings.hpp>
#include <commentary/question.hpp>
#include <study/sources.hpp>

using namespace fcg;

namespace {

const char* kindName(commentary::Action::Kind kind) {
    using K = commentary::Action::Kind;
    switch (kind) {
        case K::Explain:
            return "explain";
        case K::Best:
            return "best";
        case K::Threat:
            return "threat";
        case K::Try:
            return "try";
        case K::Compare:
            return "compare";
        case K::Games:
            return "games";
        case K::Played:
            return "played";
        case K::Opening:
            return "opening";
        default:
            return "tablebase";
    }
}

int questions(const std::string& text, commentary::Model& model) {
    struct Tally {
        std::size_t asked = 0, right = 0;
    };
    std::map<std::string, Tally> by_kind;
    std::size_t total = 0, plain = 0, right = 0, bad = 0;
    double seconds = 0.0;
    for (std::size_t at = 0; at < text.size();) {
        const auto end  = std::min(text.find('\n', at), text.size());
        const auto line = text.substr(at, end - at);
        at              = end + 1;
        if (line.empty()) continue;
        const auto j        = nlohmann::json::parse(line);
        const auto question = j.at("question").get<std::string>();
        const auto pos      = commentary::Position::fromFen(j.at("fen").get<std::string>());
        const auto lang     = j.value("lang", "en") == "fr" ? commentary::Lang::Fr : commentary::Lang::En;
        if (!pos) {
            std::fprintf(stderr, "bad position: %s\n", line.c_str());
            ++bad;
            continue;
        }
        const auto names    = commentary::moveNames(*pos, lang);
        const auto expected = commentary::readAction(j.at("expect").get<std::string>(), question, names);
        if (!expected) {
            std::fprintf(stderr, "bad expectation: %s\n", line.c_str());
            ++bad;
            continue;
        }
        ++total;
        auto action = commentary::plainAction(question, names);
        if (!action) action = commentary::namedAction(question, names);
        std::string said;
        if (action) {
            ++plain;
            said = "(plain)";
        } else {
            const auto played = j.value("played", std::string());
            std::optional<std::string_view> just;
            if (!played.empty()) just = played;
            const auto started = std::chrono::steady_clock::now();
            std::string error;
            const auto g = model.generate(commentary::actionRequest(question, *pos, just, names), &error);
            seconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
            said   = g ? g->text : "<" + error + ">";
            action = commentary::readAction(said, question, names);
        }
        const bool ok = action == expected;
        auto& tally   = by_kind[kindName(expected->kind)];
        ++tally.asked;
        tally.right += ok;
        right += ok;
        if (!ok)
            std::printf("MISS  %-60s expected %-22s got %s\n", question.c_str(),
                        j.at("expect").get<std::string>().c_str(), said.c_str());
    }
    const auto modelled = total - plain;
    std::printf("%zu questions (%zu bad cases): %zu right; %zu read without the model; the model %.2f s each\n", total,
                bad, right, plain, modelled ? seconds / static_cast<double>(modelled) : 0.0);
    for (const auto& [kind, tally] : by_kind) std::printf("  %-10s %zu/%zu\n", kind.c_str(), tally.right, tally.asked);
    return right == total && bad == 0 ? 0 : 3;
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string_view> args(argv + 1, argv + argc);
    std::string cases_path, model_path, out_path, lang = "en", questions_path;
    std::size_t limit = SIZE_MAX;
    int gpu_layers    = 0;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const auto value = [&]() -> std::string {
            return i + 1 < args.size() ? std::string(args[++i]) : std::string();
        };
        if (args[i] == "--out")
            out_path = value();
        else if (args[i] == "--lang")
            lang = value();
        else if (args[i] == "--limit")
            limit = std::stoul(value());
        else if (args[i] == "--gpu-layers")
            gpu_layers = std::stoi(value());
        else if (args[i] == "--questions")
            questions_path = value();
        else if (cases_path.empty())
            cases_path = std::string(args[i]);
        else
            model_path = std::string(args[i]);
    }
    if (!questions_path.empty() && !cases_path.empty() && model_path.empty()) std::swap(cases_path, model_path);
    if (!questions_path.empty()) cases_path = questions_path;
    if (cases_path.empty() || model_path.empty()) {
        std::fprintf(stderr,
                     "usage: fcg-assistant-eval CASES MODEL [--out FILE] [--lang en|fr|both] [--limit N] "
                     "[--gpu-layers N]\n       fcg-assistant-eval --questions QUESTIONS MODEL [--gpu-layers N]\n");
        return 2;
    }
    std::string error;
    auto model = assistant::Generator::load(model_path, gpu_layers, error);
    if (!model) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
    std::fprintf(stderr, "model on %s\n", model->devices().c_str());
    std::string text;
    std::vector<char> buffer(1 << 16);
    for (auto& source : study::openSources(cases_path)) {
        while (const auto n = source->read(buffer.data(), buffer.size())) text.append(buffer.data(), n);
    }
    if (!questions_path.empty()) return questions(text, *model);
    std::ofstream out;
    if (!out_path.empty()) out.open(out_path);
    std::vector<std::pair<commentary::Lang, commentary::PieceLetters>> langs;
    if (lang == "en" || lang == "both") langs.emplace_back(commentary::Lang::En, commentary::PieceLetters::English);
    if (lang == "fr" || lang == "both") langs.emplace_back(commentary::Lang::Fr, commentary::PieceLetters::French);
    std::size_t read = 0, comments = 0, unasked = 0, first = 0, by_model = 0;
    double seconds = 0.0;
    for (std::size_t at = 0; at < text.size() && read < limit;) {
        const auto end  = std::min(text.find('\n', at), text.size());
        const auto line = text.substr(at, end - at);
        at              = end + 1;
        if (line.empty()) continue;
        ++read;
        const auto c        = commentary::Case::fromJson(nlohmann::json::parse(line));
        const auto replayed = commentary::replay(c, &error, &commentary::openingName);
        if (!replayed) {
            std::fprintf(stderr, "%s: %s\n", c.id.c_str(), error.c_str());
            return 1;
        }
        const auto ctx = replayed->context();
        for (const auto& [l, letters] : langs) {
            const auto started = std::chrono::steady_clock::now();
            const auto written = commentary::writtenComment(*model, ctx, l, letters);
            if (!written) continue;
            const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
            ++comments;
            if (written->attempts == 0) {
                ++unasked;
            } else {
                first += written->by_model && written->attempts == 1;
                by_model += written->by_model;
                seconds += took;
            }
            if (out) {
                nlohmann::json j = {{"id", c.id},
                                    {"lang", l == commentary::Lang::Fr ? "fr" : "en"},
                                    {"text", written->text},
                                    {"by_model", written->by_model},
                                    {"attempts", written->attempts},
                                    {"seconds", took}};
                out << j.dump() << '\n';
            }
            const auto asked = comments - unasked;
            std::fprintf(stderr,
                         "\r%zu comments, %zu with nothing to explain; of the others: model %.1f %% (first "
                         "attempt %.1f %%), %.2f s each   ",
                         comments, unasked, asked ? 100.0 * static_cast<double>(by_model) / asked : 0.0,
                         asked ? 100.0 * static_cast<double>(first) / asked : 0.0,
                         asked ? seconds / static_cast<double>(asked) : 0.0);
        }
    }
    std::fprintf(stderr, "\n");
    return 0;
}
