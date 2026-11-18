#pragma once

#ifdef FCG_ASSISTANT

// The program's end of the assistant's child process (assistant/runtime.hpp): the language model as a
// commentary::Model, run in a process of its own, so that a graphics driver that fails takes only that process down,
// and unloading the model is ending it. A thread reads what the child says; a request waits for its answer.

#    include <chrono>
#    include <condition_variable>
#    include <deque>
#    include <filesystem>
#    include <memory>
#    include <mutex>
#    include <optional>
#    include <string>
#    include <thread>

#    include <commentary/commentator.hpp>

#    include "child_process.hpp"

namespace fcg {

class AssistantProcess final : public commentary::Model {
   public:
    AssistantProcess() = default;
    ~AssistantProcess() override;
    AssistantProcess(const AssistantProcess&)            = delete;
    AssistantProcess& operator=(const AssistantProcess&) = delete;

    // Starts `program --assistant --model MODEL --gpu-layers N` and waits, at most `load_within`, for it to say the
    // model loaded. False with `error` when it cannot start, the model does not load, or it takes too long. A child
    // that falls while it loads the model on the GPU is started again on the CPU (gpuFailed() then says so).
    bool start(const std::string& program, const std::filesystem::path& model, int gpu_layers, std::string& error,
               std::chrono::milliseconds load_within = std::chrono::minutes(5));

    // The model's answer. A child that does not answer within `answer_within` is taken for stuck and ended. One that
    // falls while it answers on the GPU, its driver failing, is started again on the CPU and asked again, once.
    [[nodiscard]] std::optional<commentary::Generation> generate(const commentary::Request& request,
                                                                 std::string* error) override;

    // Ends the child: the model is unloaded.
    void stop();
    // The child is there to answer.
    [[nodiscard]] bool running();
    // The devices the model runs on, as the child named them.
    [[nodiscard]] const std::string& devices() const noexcept { return devices_; }
    // The GPU failed the model: it runs on the CPU since.
    [[nodiscard]] bool gpuFailed() const noexcept { return gpu_failed_; }

    std::chrono::milliseconds answer_within = std::chrono::minutes(2);

   private:
    // One start of the child, without falling back: false with `error`, `fell` when it ended before it was ready.
    bool startOnce(int gpu_layers, std::string& error, bool& fell);
    void readLoop();
    // The child's next line, waiting at most `wait`; none when it ended or the time is up (`timed_out` then says so).
    std::optional<std::string> nextLine(std::chrono::milliseconds wait, bool& timed_out);

    std::unique_ptr<ChildProcess> child_;
    std::thread reader_;
    std::mutex mutex_;
    std::condition_variable arrived_;
    std::deque<std::string> lines_;
    bool ended_ = false;
    std::string devices_;
    std::string program_;
    std::filesystem::path model_;
    std::chrono::milliseconds load_within_{};
    int gpu_layers_  = 0;
    bool gpu_failed_ = false;
};

}  // namespace fcg

#endif  // FCG_ASSISTANT
