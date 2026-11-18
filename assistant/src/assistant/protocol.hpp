#pragma once

// What the assistant's child process and the program say to each other: one JSON object per line. The program sends
// commentary::Model's requests; the child answers each with its generation or why there is none, and says first
// whether the model loaded.

#include <cstdio>
#include <optional>
#include <string>
#include <string_view>

#include <commentary/commentator.hpp>

namespace fcg::assistant {

[[nodiscard]] std::string requestLine(const commentary::Request& request);
// None for a line that is no request, `error` then saying why.
[[nodiscard]] std::optional<commentary::Request> parseRequest(std::string_view line, std::string* error);

[[nodiscard]] std::string generationLine(const commentary::Generation& generation);
[[nodiscard]] std::string errorLine(std::string_view error);
// The model loaded, on these devices.
[[nodiscard]] std::string readyLine(std::string_view devices);

// One of the child's lines read back: its readiness, a generation, or an error (a line it could not have written
// included).
struct Answer {
    std::optional<std::string> ready;  // the devices the model runs on
    std::optional<commentary::Generation> generation;
    std::optional<std::string> error;
};
[[nodiscard]] Answer parseAnswer(std::string_view line);

// Answers the requests read from `in`, one per line, with `model`, until `in` ends.
void serve(std::FILE* in, std::FILE* out, commentary::Model& model);

}  // namespace fcg::assistant
