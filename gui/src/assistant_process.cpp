#ifdef FCG_ASSISTANT

#    include "assistant_process.hpp"

#    include <assistant/protocol.hpp>
#    include <assistant/runtime.hpp>

namespace fcg {

AssistantProcess::~AssistantProcess() { stop(); }

bool AssistantProcess::start(const std::string& program, const std::filesystem::path& model, int gpu_layers,
                             std::string& error, std::chrono::milliseconds load_within) {
    stop();
    {
        std::lock_guard lock(mutex_);
        lines_.clear();
        ended_ = false;
    }
    child_ = std::make_unique<ChildProcess>();
    if (!child_->start(
            program,
            {std::string(assistant::FLAG), "--model", model.string(), "--gpu-layers", std::to_string(gpu_layers)},
            error)) {
        child_.reset();
        return false;
    }
    reader_         = std::thread([this] { readLoop(); });
    bool timed_out  = false;
    const auto line = nextLine(load_within, timed_out);
    if (!line) {
        error = timed_out ? "the assistant took too long to load its model" : "the assistant ended before it was ready";
        stop();
        return false;
    }
    const auto answer = assistant::parseAnswer(*line);
    if (!answer.ready) {
        error = answer.error.value_or("the assistant did not say it was ready");
        stop();
        return false;
    }
    devices_ = *answer.ready;
    return true;
}

std::optional<commentary::Generation> AssistantProcess::generate(const commentary::Request& request,
                                                                 std::string* error) {
    const auto fail = [&](const std::string& why) -> std::optional<commentary::Generation> {
        if (error) *error = why;
        return std::nullopt;
    };
    if (!child_) return fail("the assistant is not running");
    if (!child_->write(assistant::requestLine(request))) return fail("the assistant went away");
    bool timed_out  = false;
    const auto line = nextLine(answer_within, timed_out);
    if (!line) {
        // A child that takes too long is stuck, or its driver is: it is ended, the next start begins anew.
        if (timed_out) stop();
        return fail(timed_out ? "the assistant did not answer in time" : "the assistant ended");
    }
    auto answer = assistant::parseAnswer(*line);
    if (answer.generation) return std::move(answer.generation);
    return fail(answer.error.value_or("the assistant gave no answer"));
}

void AssistantProcess::stop() {
    if (child_) {
        // Its input closed, the child ends on its own; one that does not, at once.
        child_->closeInput();
        const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (child_->running() && std::chrono::steady_clock::now() < until)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        if (child_->running()) child_->kill();
        child_->closeOutput();
    }
    if (reader_.joinable()) reader_.join();
    if (child_) {
        child_->wait();
        child_.reset();
    }
    devices_.clear();
}

bool AssistantProcess::running() {
    if (!child_ || !child_->running()) return false;
    std::lock_guard lock(mutex_);
    return !ended_;
}

void AssistantProcess::readLoop() {
    std::string pending;
    char buffer[4096];
    for (;;) {
        const long n = child_->read(buffer, sizeof buffer);
        if (n <= 0) break;
        pending.append(buffer, static_cast<std::size_t>(n));
        for (std::size_t end; (end = pending.find('\n')) != std::string::npos;) {
            std::string line = pending.substr(0, end);
            pending.erase(0, end + 1);
            std::lock_guard lock(mutex_);
            lines_.push_back(std::move(line));
            arrived_.notify_all();
        }
    }
    std::lock_guard lock(mutex_);
    ended_ = true;
    arrived_.notify_all();
}

std::optional<std::string> AssistantProcess::nextLine(std::chrono::milliseconds wait, bool& timed_out) {
    std::unique_lock lock(mutex_);
    timed_out = !arrived_.wait_for(lock, wait, [&] { return !lines_.empty() || ended_; });
    if (lines_.empty()) return std::nullopt;
    auto line = std::move(lines_.front());
    lines_.pop_front();
    return line;
}

}  // namespace fcg

#endif  // FCG_ASSISTANT
