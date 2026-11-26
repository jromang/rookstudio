#pragma once

// Futures whose result nobody wants any more, kept until their task ends (#98). A future made by std::async waits for
// its task when it is destroyed: one dropped on the window's thread would freeze the window until the task ends. So
// the task is cancelled first, then its future parked here: poll() lets go of those whose task ended, once a frame,
// and wait() waits for the others, at quit, every window hidden. A future may come with what to do with its result
// once its task ended (a failure said in the console): done by poll() or wait(), on their thread. Without ImGui.

#include <chrono>
#include <cstddef>
#include <future>
#include <memory>
#include <type_traits>
#include <utility>
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
    // The same, `then` called with its result once its task ended (with nothing for a std::future<void>); not when the
    // task threw.
    template <typename T, typename F>
    void park(std::future<T>&& future, F&& then) {
        if (future.valid())
            parked_.push_back(std::make_unique<Then<T, std::decay_t<F>>>(std::move(future), std::forward<F>(then)));
    }

    // Lets go of the futures whose task ended, what was to be done with their result done; how many are still running.
    std::size_t poll() {
        std::vector<std::unique_ptr<Grave>> ended;
        for (auto it = parked_.begin(); it != parked_.end();) {
            if ((*it)->ended()) {
                ended.push_back(std::move(*it));
                it = parked_.erase(it);
            } else {
                ++it;
            }
        }
        // Out of the loop: what is done with a result may park another future.
        for (const auto& grave : ended) grave->settle();
        return parked_.size();
    }

    // Waits for every task parked.
    void wait() {
        while (!parked_.empty()) {
            auto graves = std::move(parked_);
            parked_.clear();
            for (const auto& grave : graves) {
                grave->wait();
                grave->settle();
            }
        }
    }

    [[nodiscard]] std::size_t size() const noexcept { return parked_.size(); }
    [[nodiscard]] bool empty() const noexcept { return parked_.empty(); }

   private:
    struct Grave {
        virtual ~Grave()                         = default;
        [[nodiscard]] virtual bool ended() const = 0;
        virtual void wait() const                = 0;
        virtual void settle() {}  // its task ended: what is to be done with its result
    };
    template <typename T>
    struct Parked : Grave {
        explicit Parked(std::future<T>&& f) : future(std::move(f)) {}
        [[nodiscard]] bool ended() const override {
            return future.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
        }
        void wait() const override { future.wait(); }
        std::future<T> future;
    };
    template <typename T, typename F>
    struct Then final : Parked<T> {
        Then(std::future<T>&& f, F t) : Parked<T>(std::move(f)), then(std::move(t)) {}
        void settle() override {
            try {
                if constexpr (std::is_void_v<T>) {
                    this->future.get();
                    then();
                } else {
                    then(this->future.get());
                }
            } catch (...) {  // the task's failure, or what was to be done with its result: nobody to tell
            }
        }
        F then;
    };
    std::vector<std::unique_ptr<Grave>> parked_;
};

}  // namespace fcg
