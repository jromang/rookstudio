#pragma once

// A small language model running in this process with llama.cpp: load a GGUF file, then answer chat messages, held to
// a GBNF grammar when one is given (the prototype's crates/llm). The assistant runs it in a child process
// (runtime.hpp), so that a graphics driver that fails takes only that process down.

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

#include <commentary/commentator.hpp>

namespace fcg::assistant {

class Generator final : public commentary::Model {
   public:
    // Loads a GGUF file, `gpu_layers` of it on the GPU (0 for the CPU only, a large number for all); none with `error`
    // when it cannot.
    [[nodiscard]] static std::unique_ptr<Generator> load(const std::filesystem::path& path, int gpu_layers,
                                                         std::string& error);
    ~Generator() override;
    Generator(const Generator&)            = delete;
    Generator& operator=(const Generator&) = delete;

    // The prompt the model's own chat template makes of the messages, rendered as the Jinja it is, thinking off: what a
    // fine-tuning library makes of them. llama.cpp's built-in renderings, which only approximate the templates they
    // recognize, are the fallback.
    [[nodiscard]] std::optional<std::string> prompt(const commentary::Request& request, std::string* error) const;

    // Generation is not reentrant: one request at a time.
    [[nodiscard]] std::optional<commentary::Generation> generate(const commentary::Request& request,
                                                                 std::string* error) override;

    // The devices the model runs on, as llama.cpp names them: "Vulkan0 (NVIDIA GeForce RTX 4070)", "CPU".
    [[nodiscard]] std::string devices() const;

   private:
    struct State;
    explicit Generator(std::unique_ptr<State> state);
    std::unique_ptr<State> state_;
};

}  // namespace fcg::assistant
