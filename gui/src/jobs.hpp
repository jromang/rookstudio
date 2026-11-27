#pragma once

// Background jobs (#105): a task on a thread of its own, the request to stop it, what it tells as it goes and its
// result, in one type, Job<T, P>. A job replaced or destroyed while its task runs cancels it and parks its future in
// its group's graveyard (graveyard.hpp): the window's thread never waits for a task it no longer wants. The task
// shares nothing with its owner but its state (TaskState, held by both) and what it captured, so that it may outlive
// the job, its owner too. The jobs of an owner are in a JobGroup (App's one, which the shelf and the background
// readings share): cancelled at once at shutdown, their tasks parked let go of once a frame, waited for at the end.
// Used from one thread, the owner's (the window's); without ImGui.

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <future>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

#include "graveyard.hpp"

namespace fcg {

// Whether a future (std::future, std::shared_future) has its result or its exception, without waiting: false for one
// without a task (not valid()).
template <typename Future>
[[nodiscard]] bool isReady(const Future& future) {
    return future.valid() && future.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}

// What a task that tells nothing as it goes tells.
struct NoProgress {};

// What a job's task and its owner share, held by both: P, what the task tells as it goes (atomics), and the request to
// stop. P's other fields are the owner's (what it shows of the job, never touched by the task), or the task's
// (written before it ends, read once the job is ready()).
template <typename P = NoProgress>
struct TaskState : P {
    std::atomic<bool> cancel{false};
};

// The jobs of an owner, cancelled together, and the graveyard of the tasks it no longer wants. It outlives its jobs
// (App declares it first): a job destroyed while its task runs parks it here, and the group waits for it when it goes.
class JobGroup {
   public:
    JobGroup()                           = default;
    JobGroup(const JobGroup&)            = delete;
    JobGroup& operator=(const JobGroup&) = delete;

    // Every task of the group's jobs asked to stop, at once: at shutdown, before what waits for them.
    void cancelAll() noexcept {
        for (const auto& weak : cancels_)
            if (const auto cancel = weak.lock()) *cancel = true;
    }

    // A future nobody wants any more, its task asked to stop first by the caller when it can be, kept until the task
    // ends; with `then`, what to do with its result then (graveyard.hpp), which holds what it uses: it may be done
    // when its owner is gone, at the group's end.
    template <typename T>
    void park(std::future<T>&& future) {
        graveyard_.park(std::move(future));
    }
    template <typename T, typename F>
    void park(std::future<T>&& future, F&& then) {
        graveyard_.park(std::move(future), std::forward<F>(then));
    }

    // Once a frame: the tasks parked that ended let go of; how many still run.
    std::size_t poll() { return graveyard_.poll(); }
    // Waits for every task parked: at quit, every window hidden.
    void wait() { graveyard_.wait(); }
    [[nodiscard]] std::size_t parked() const noexcept { return graveyard_.size(); }
    // The jobs whose state is still held (by the job, or by its task): what cancelAll() reaches.
    [[nodiscard]] std::size_t enrolled() const noexcept {
        std::size_t n = 0;
        for (const auto& weak : cancels_) n += weak.expired() ? 0 : 1;
        return n;
    }

   private:
    template <typename T, typename P>
    friend class Job;
    void enrol(std::weak_ptr<std::atomic<bool>> cancel) {
        std::erase_if(cancels_, [](const auto& weak) { return weak.expired(); });
        cancels_.push_back(std::move(cancel));
    }
    std::vector<std::weak_ptr<std::atomic<bool>>> cancels_;  // of the jobs' states
    Graveyard graveyard_;                                    // waits for the tasks parked when it goes
};

// A task in the background, started again at will: started, followed (state(), running, ready), its result taken
// once, or cancelled. Replacing it (start, a job moved onto it) or destroying it while its task runs cancels the task
// and parks it in the group; the task ends on its own, out of sight.
template <typename T, typename P = NoProgress>
class Job {
   public:
    using State = TaskState<P>;

    explicit Job(JobGroup& group) : group_(&group), state_(std::make_shared<State>()) {}
    ~Job() { drop(); }
    // A job moved keeps its task; the one it was moved from shares its state and has none.
    Job(Job&& other) noexcept : group_(other.group_), state_(other.state_), future_(std::move(other.future_)) {}
    Job& operator=(Job&& other) noexcept {
        if (this != &other) {
            drop();
            group_  = other.group_;
            state_  = other.state_;
            future_ = std::move(other.future_);
        }
        return *this;
    }
    Job(const Job&)            = delete;
    Job& operator=(const Job&) = delete;

    // Starts `task` on a thread of its own, given its state (State&, or nothing): the task running before is cancelled
    // and parked, never waited for, a result not taken forgotten. The new state, for the owner's fields.
    template <typename F>
    State& start(F&& task) {
        drop();
        auto state = std::make_shared<State>();
        group_->enrol(std::shared_ptr<std::atomic<bool>>(state, &state->cancel));
        future_ = std::async(std::launch::async, [state, task = std::forward<F>(task)]() mutable -> T {
            if constexpr (std::is_invocable_v<decltype(task)&, State&>)
                return task(*state);
            else
                return task();
        });
        state_  = std::move(state);
        return *state_;
    }

    // Its task runs: started, not ended.
    [[nodiscard]] bool running() const noexcept { return future_.valid() && !ended(); }
    // Its task ended, its result not taken yet.
    [[nodiscard]] bool ready() const noexcept { return future_.valid() && ended(); }
    // Nothing to follow: never started, its result taken, or let go of.
    [[nodiscard]] bool idle() const noexcept { return !future_.valid(); }
    // Its result, once ready(): what the task threw, thrown again. Idle after.
    T take() {
        assert(ready());
        return future_.get();
    }

    // Asks its task to stop; it ends on its own (ready() then).
    void cancel() noexcept { state_->cancel = true; }
    [[nodiscard]] bool cancelled() const noexcept { return state_->cancel; }
    // What its task tells, and the owner's fields: the last task's, kept once it ended.
    [[nodiscard]] State& state() noexcept { return *state_; }
    [[nodiscard]] const State& state() const noexcept { return *state_; }

    // Let go of: its task cancelled and parked, never waited for; a result not taken forgotten. Idle after.
    void drop() {
        if (!future_.valid()) return;
        state_->cancel = true;
        group_->park(std::move(future_));
    }

   private:
    [[nodiscard]] bool ended() const noexcept {
        return future_.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
    }
    JobGroup* group_;
    std::shared_ptr<State> state_;
    std::future<T> future_;
};

}  // namespace fcg
