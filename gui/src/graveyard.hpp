#pragma once

// Futures whose result nobody wants any more, kept until their task ends (#98). A future made by std::async waits for
// its task when it is destroyed: one dropped on the window's thread would freeze the window until the task ends. So
// the task is cancelled first, then its future parked here: poll() lets go of those whose task ended, once a frame,
// and wait() waits for the others, at quit, every window hidden. Without ImGui.

#include <chrono>
#include <cstddef>
#include <future>
#include <memory>
#include <vector>

namespace fcg {

class Graveyard {
   public:
    Graveyard() = default;
    ~Graveyard() { wait(); }
    Graveyard(const Graveyard&)            = delete;
    Graveyard& operator=(const Graveyard&) = delete;

    // Kept until its task ends; a future with no task (none made, or its result taken) is let go at once.
    template <typename T>
    void park(std::future<T>&& future) {
        if (future.valid()) parked_.push_back(std::make_unique<Parked<T>>(std::move(future)));
    }

    // Lets go of the futures whose task ended; how many are still running.
    std::size_t poll() {
        std::erase_if(parked_, [](const auto& p) { return p->ended(); });
        return parked_.size();
    }

    // Waits for every task parked.
    void wait() {
        for (const auto& p : parked_) p->wait();
        parked_.clear();
    }

    [[nodiscard]] std::size_t size() const noexcept { return parked_.size(); }
    [[nodiscard]] bool empty() const noexcept { return parked_.empty(); }

   private:
    struct Grave {
        virtual ~Grave()                         = default;
        [[nodiscard]] virtual bool ended() const = 0;
        virtual void wait() const                = 0;
    };
    template <typename T>
    struct Parked final : Grave {
        explicit Parked(std::future<T>&& f) : future(std::move(f)) {}
        [[nodiscard]] bool ended() const override {
            return future.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
        }
        void wait() const override { future.wait(); }
        std::future<T> future;
    };
    std::vector<std::unique_ptr<Grave>> parked_;
};

}  // namespace fcg
