#pragma once

// The study's work beside the tournaments: a thread that enters the background runs at the lowest priority, and so do
// the threads the study starts from it (imports, the opening tree, searches, lists), so that the engines of a
// tournament, processes of their own, keep the machine first; and what such a task tells as it goes.

#include <atomic>
#include <cstdint>

namespace fcg::study {

// What a long task tells the window as it goes (#117): its work done, of its total (0 while not known).
struct JobProgress {
    std::atomic<uint64_t> done{0};
    std::atomic<uint64_t> total{0};
};

// Lowers the calling thread, and marks it: the study's threads it starts are lowered too.
void enterBackground() noexcept;
// The calling thread entered the background, or was started by one that had.
[[nodiscard]] bool inBackground() noexcept;
// The calling thread's priority is the lowest the study asks for, as the system tells it: for the tests.
[[nodiscard]] bool loweredBySystem() noexcept;

// What a thread the study starts runs first: the background of the thread that starts it, kept.
class Inherited {
   public:
    Inherited() noexcept : background_(inBackground()) {}
    void apply() const noexcept {
        if (background_) enterBackground();
    }

   private:
    bool background_;
};

}  // namespace fcg::study
