// fcg-assistant-eval: a model measured on commentary cases the way the program uses it (the prototype's
// `rookstudio-dataset eval`). Each case goes through the program's own path, attempts, checker and template fallback;
// what counts is how often the model's text stands, how soon, and how fast. Not built by default:
// -DFCG_ASSISTANT_EVAL=ON.
//
//     fcg-assistant-eval CASES MODEL [--out FILE] [--lang en|fr|both] [--limit N] [--gpu-layers N]

#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include <json.hpp>

#include <assistant/generator.hpp>
#include <commentary/case.hpp>
#include <commentary/commentator.hpp>
#include <commentary/openings.hpp>
#include <study/sources.hpp>

using namespace fcg;

int main(int argc, char** argv) {
    std::vector<std::string_view> args(argv + 1, argv + argc);
    std::string cases_path, model_path, out_path, lang = "en";
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
        else if (cases_path.empty())
            cases_path = std::string(args[i]);
        else
            model_path = std::string(args[i]);
    }
    if (cases_path.empty() || model_path.empty()) {
        std::fprintf(stderr,
                     "usage: fcg-assistant-eval CASES MODEL [--out FILE] [--lang en|fr|both] [--limit N] "
                     "[--gpu-layers N]\n");
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
