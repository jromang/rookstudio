#include "protocol.hpp"

#include <json.hpp>

namespace fcg::assistant {

namespace {

const char* speakerName(commentary::Speaker s) {
    switch (s) {
        case commentary::Speaker::System:
            return "system";
        case commentary::Speaker::User:
            return "user";
        default:
            return "assistant";
    }
}

std::optional<commentary::Speaker> speakerOf(std::string_view name) {
    if (name == "system") return commentary::Speaker::System;
    if (name == "user") return commentary::Speaker::User;
    if (name == "assistant") return commentary::Speaker::Assistant;
    return std::nullopt;
}

// One JSON object on a line of its own: what it holds is escaped, newlines included.
std::string line(const nlohmann::json& j) {
    return j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace) + "\n";
}

}  // namespace

std::string requestLine(const commentary::Request& request) {
    auto messages = nlohmann::json::array();
    for (const auto& [speaker, text] : request.messages) messages.push_back({speakerName(speaker), text});
    nlohmann::json j = {{"messages", messages},
                        {"max_tokens", request.max_tokens},
                        {"temperature", request.temperature},
                        {"seed", request.seed}};
    j["grammar"]     = request.grammar ? nlohmann::json(*request.grammar) : nlohmann::json(nullptr);
    return line(j);
}

std::optional<commentary::Request> parseRequest(std::string_view text, std::string* error) {
    try {
        const auto j = nlohmann::json::parse(text);
        commentary::Request r;
        for (const auto& m : j.at("messages")) {
            const auto speaker = speakerOf(m.at(0).get<std::string>());
            if (!speaker) throw std::runtime_error("unknown speaker " + m.at(0).get<std::string>());
            r.messages.emplace_back(*speaker, m.at(1).get<std::string>());
        }
        if (j.contains("grammar") && !j.at("grammar").is_null()) r.grammar = j.at("grammar").get<std::string>();
        r.max_tokens  = j.at("max_tokens").get<std::size_t>();
        r.temperature = j.at("temperature").get<float>();
        r.seed        = j.at("seed").get<uint32_t>();
        return r;
    } catch (const std::exception& e) {
        if (error) *error = std::string("not a request: ") + e.what();
        return std::nullopt;
    }
}

std::string generationLine(const commentary::Generation& g) {
    return line({{"text", g.text}, {"prompt_tokens", g.prompt_tokens}, {"tokens", g.tokens}, {"seconds", g.seconds}});
}

std::string errorLine(std::string_view error) { return line({{"error", std::string(error)}}); }

std::string readyLine(std::string_view devices) { return line({{"ready", true}, {"devices", std::string(devices)}}); }

Answer parseAnswer(std::string_view text) {
    Answer a;
    try {
        const auto j = nlohmann::json::parse(text);
        if (j.contains("error")) {
            a.error = j.at("error").get<std::string>();
        } else if (j.contains("ready")) {
            a.ready = j.value("devices", std::string());
        } else {
            commentary::Generation g;
            g.text          = j.at("text").get<std::string>();
            g.prompt_tokens = j.value("prompt_tokens", std::size_t{0});
            g.tokens        = j.value("tokens", std::size_t{0});
            g.seconds       = j.value("seconds", 0.0);
            a.generation    = std::move(g);
        }
    } catch (const std::exception& e) {
        a.error = std::string("the assistant wrote no answer: ") + e.what();
    }
    return a;
}

void serve(std::FILE* in, std::FILE* out, commentary::Model& model) {
    std::string pending;
    char buffer[4096];
    while (std::fgets(buffer, sizeof buffer, in)) {
        pending += buffer;
        if (pending.empty() || pending.back() != '\n') continue;
        std::string error;
        const auto request = parseRequest(pending, &error);
        pending.clear();
        std::string answer;
        if (!request) {
            answer = errorLine(error);
        } else if (const auto g = model.generate(*request, &error)) {
            answer = generationLine(*g);
        } else {
            answer = errorLine(error.empty() ? "no answer" : error);
        }
        std::fputs(answer.c_str(), out);
        std::fflush(out);
    }
}

}  // namespace fcg::assistant
