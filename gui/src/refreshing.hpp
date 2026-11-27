#pragma once

// When to ask again (#111), as plain code a test drives with a clock of its own, without ImGui. Refreshing<T> is a
// value read from Lichess in the background and read again: the answer shown (the last value, kept when a later answer
// fails, and why the last one gave nothing), the job that asks, its maximum age and its host's pause after a 429
// (HostGate); it says whether to ask now and whether what is shown is stale. The helpers below say the checks the pages
// make alike: a time never set (never asked yet), a request due again after so long, a card drawn in the last frames
// (still shown, whatever the order of the frame's polls and draws).
//
// Used from one thread, its owner's (the window's).

#include <chrono>
#include <functional>
#include <utility>

#include "jobs.hpp"
#include "lichess_api.hpp"  // Answer, Failure, apiPause()

namespace fcg {

// A time never set: never asked yet.
[[nodiscard]] constexpr bool never(std::chrono::steady_clock::time_point t) noexcept {
    return t == std::chrono::steady_clock::time_point{};
}

// Due again: never asked, or asked `every` ago or more.
[[nodiscard]] constexpr bool dueAfter(std::chrono::steady_clock::time_point asked,
                                      std::chrono::steady_clock::duration every,
                                      std::chrono::steady_clock::time_point now) noexcept {
    return never(asked) || now - asked >= every;
}

// Drawn in the last `frames` frames, the one being drawn among them: `drawn` is the frame that drew it last, `frame`
// the frame now (ImGui's count). What was drawn so lately still shows.
[[nodiscard]] constexpr bool drawnLately(int drawn, int frame, int frames = 3) noexcept {
    return frame - drawn < frames;
}

// The time a schedule reads: the steady clock's, or a test's.
using Now = std::function<std::chrono::steady_clock::time_point()>;

// When a value is asked again: at its pace, whatever its age (a list read every minute while it shows); or only once
// what is shown is stale, so that the age of a value kept elsewhere counts (a profile kept on disk, read again when ten
// minutes old, not at each start of the program).
enum class Refresh { Every, WhenStale };

// A value read from Lichess in the background and read again. What it is, Answer<T>, is the answer shown: the last
// value an answer gave, kept when a later one fails, and why the last one gave nothing (or gave only part), cleared by
// one that gave all. Asked one request at a time; `max_age`: what is shown is stale once that old, and asked again
// then, not sooner; after a 429 of its own, nothing is asked before its host's pause is over. `now`, the clock its
// schedule reads, and `pause`, how long the host pauses after a 429, are the program's by default (the steady clock,
// HostGate's pause of Lichess's API host), a test's otherwise.
template <typename T>
class Refreshing : public lichess::Answer<T> {
   public:
    using Clock = std::chrono::steady_clock;
    using Pause = std::function<Clock::duration()>;

    Refreshing(JobGroup& jobs, Clock::duration max_age, Refresh refresh = Refresh::Every, Now now = Clock::now,
               Pause pause = lichess::apiPause)
        : job_(jobs), max_age_(max_age), refresh_(refresh), now_(std::move(now)), pause_(std::move(pause)) {}

    // Its request under way, or its answer not taken yet.
    [[nodiscard]] bool asking() const noexcept { return !job_.idle(); }
    // When it was asked last (never(): not yet in this run), and until when its host pauses after its last 429.
    [[nodiscard]] Clock::time_point askedAt() const noexcept { return asked_at_; }
    [[nodiscard]] Clock::time_point pausedUntil() const noexcept { return paused_until_; }

    // What is shown is stale: no value, or one `max_age` old (counted from when it came, or as old as it was given).
    [[nodiscard]] bool stale() const { return !this->value || now_() - got_at_ >= max_age_; }
    // To ask now: nothing under way, its host's pause after a 429 over, never asked or asked `max_age` ago; and, asked
    // again only once stale, what is shown stale.
    [[nodiscard]] bool due() const {
        const auto now = now_();
        if (asking() || now < paused_until_ || !dueAfter(asked_at_, max_age_, now)) return false;
        return refresh_ == Refresh::Every || stale();
    }

    // Asked: `fetch` run on a thread of its own, given the job's state (TaskState<>, whose `cancel` it may watch) or
    // nothing, returning a lichess::Answer<T>; its answer taken by poll().
    template <typename F>
    void ask(F&& fetch) {
        asked_at_ = now_();
        job_.start(std::forward<F>(fetch));
    }

    // Once a frame: its answer taken when it came. A value replaces the one shown; a failure leaves it as it was, and
    // is said; a 429 holds its asks until its host's pause is over. True when a new value came.
    bool poll() {
        if (!job_.ready()) return false;
        auto answer    = job_.take();
        const auto now = now_();
        // Why it gave nothing, or nothing to say once it gave all.
        static_cast<lichess::Failure&>(*this) = static_cast<const lichess::Failure&>(answer);
        if (answer.net == lichess::NetError::RateLimited) paused_until_ = now + pause_();
        if (!answer.value) return false;
        this->value = std::move(answer.value);
        got_at_     = now;
        return true;
    }

    // Asked again at the next due(), whenever it was asked last: what changed shows in its answer.
    void again() noexcept { asked_at_ = {}; }
    // A value from elsewhere (a copy kept on disk), `age` old: shown until an answer replaces it.
    void keep(T value, Clock::duration age = {}) {
        this->value = std::move(value);
        got_at_     = now_() - age;
    }
    // Forgotten: its value, its failure, when it was asked; its request under way let go of (asked to stop, parked).
    void reset() {
        job_.drop();
        static_cast<lichess::Answer<T>&>(*this) = {};
        asked_at_ = got_at_ = paused_until_ = {};
    }

   private:
    Job<lichess::Answer<T>> job_;
    Clock::duration max_age_;
    Refresh refresh_;
    Now now_;
    Pause pause_;
    Clock::time_point asked_at_{}, got_at_{}, paused_until_{};
};

}  // namespace fcg
