#pragma once

// The Chess space's errors with what repairs them (#141): a failure a page keeps (lichess::Failure, NetFailure,
// StoreFailure) made into the notice drawn in place of its line (drawNotice()): its level, its words and its fixes, one
// repair, then Open the folder when a file is concerned. Without ImGui and without App: the page gives what it knows
// (the account's permissions, what its Try again asks, the host's pause), App carries the fix out (carryOutRepair(),
// through RepairActions). What no gesture repairs stays a notice without a button: a pattern's syntax, the fair-play
// locks, a site's 404, a study's quota, a PGN that does not read, and the online games' actions (never replayed).

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "lichess_http.hpp"
#include "net_failure.hpp"
#include "notice.hpp"
#include "store_failure.hpp"

namespace fcg {

// A page's Try again, as it reads it when it draws: what it asks again (Retry::None: nothing, an online game's action
// or a gesture the user makes again), the host asked, whose pause a 429 set, what is left of that pause, and whether a
// click already queued the request for its end.
struct RetryState {
    Retry what = Retry::None;
    std::string host;
    std::chrono::milliseconds pause{0};
    bool queued = false;
};

// What a page knows of the Lichess account for a request: linked or not, the permissions its token has, and the one
// the request needs (empty: none in particular, the token's name only).
struct LichessAccess {
    bool linked = false;
    std::vector<std::string> scopes;
    std::string needs;
};

// A Lichess request's failure as a notice. `text` is the page's sentence, kept for what no fix repairs, at `level`.
// A 401 says the token refused, an error with Link Lichess again… (Link the Lichess account… when none is linked); a
// 403 for a permission the account lacks names it, with the button that asks for it ("Allow writing studies…"); a 429,
// an outage or Lichess in trouble (5xx) offer the page's Try again, counting the pause down.
[[nodiscard]] Notice lichessNotice(const lichess::Failure& failure, std::string text, const LichessAccess& access,
                                   const RetryState& retry, Notice::Level level = Notice::Level::Warning);

// A link to Lichess that failed, on the account's card, in its words (`text`, lichess::linkFailed()): a token Lichess
// refused after a yes says so, an error. Each offers Link Lichess again…, as its Try again did.
[[nodiscard]] Notice linkNotice(const lichess::Failure& failure, std::string text);

// A request about an engine offered to Lichess (#96) that failed, in the page's words (`text`): a 404 says Lichess no
// longer knows the engine, with Register it again while it is in the library (`in_library`); a token refused or the
// permission missing, as lichessNotice() says them.
[[nodiscard]] Notice engineOfferNotice(const lichess::Failure& failure, std::string text, const std::string& engine,
                                       bool in_library, const LichessAccess& access);

// A site's or a download's failure as a notice, in the page's words (`text`, at `level`, a warning for the host's
// pause however asked): an outage, the host's pause,
// a site in trouble (5xx) or a file that did not come whole offer the page's Try again; a file that could not be
// written or read is named in the tooltip of the words (never in them), with Open the folder after the repair; a 404
// or another refusal, no button.
[[nodiscard]] Notice netNotice(const NetFailure& failure, std::string text, const RetryState& retry,
                               Notice::Level level = Notice::Level::Error);

// A database's error as a notice, in its words: a damaged file offers Check the database… (`database`'s care at its
// checks; none when the index is unknown, -1); a full disk, Open the folder (`folder`, the database's, when given).
[[nodiscard]] Notice storeNotice(const StoreFailure& failure, int database, const std::filesystem::path& folder = {},
                                 Notice::Level level = Notice::Level::Error);

// The games of a database found damaged when it was opened (#97): `count`, a file damaged, with Check the database….
[[nodiscard]] Notice damagedGamesNotice(uint64_t count, int database);

// The requests a click asked for during their host's pause: nothing is sent before it ends, and each goes once,
// through the page's own Try again, when it does (App, each frame).
class RetryQueue {
   public:
    // Queued once: a second click before the end changes nothing.
    void queue(Retry what, std::string host);
    [[nodiscard]] bool queued(Retry what) const;
    // The requests whose host no longer pauses, taken out of the queue in the order they were asked.
    template <typename PausedFor>
    [[nodiscard]] std::vector<Retry> due(PausedFor&& paused_for) {
        std::vector<Retry> out;
        for (auto it = queued_.begin(); it != queued_.end();) {
            if (paused_for(it->second).count() > 0) {
                ++it;
                continue;
            }
            out.push_back(it->first);
            it = queued_.erase(it);
        }
        return out;
    }
    [[nodiscard]] bool empty() const noexcept { return queued_.empty(); }

   private:
    std::vector<std::pair<Retry, std::string>> queued_;
};

// What carrying a repair out asks of the program (App::Repairs; a test's fake).
class RepairActions {
   public:
    virtual ~RepairActions() = default;
    // The account linked again, asking also for `scope` (empty: the permissions it has).
    virtual void linkLichess(const std::string& scope) = 0;
    // The page's own Try again, at once.
    virtual void retryNow(Retry what)                                                        = 0;
    [[nodiscard]] virtual std::chrono::milliseconds pausedFor(const std::string& host) const = 0;
    virtual void checkDatabase(int database)                    = 0;  // its care, at the checks
    virtual void chooseEngine()                                 = 0;  // the Board's engine picker
    virtual void registerEngineAgain(const std::string& engine) = 0;
};

// A repair carried out: a Try again during its host's pause queued in `queue` (nothing sent before its end), at once
// otherwise; the others through `actions`. False for a kind that is not a repair (App::applyFix() carries those out).
bool carryOutRepair(const Fix& fix, RepairActions& actions, RetryQueue& queue);

// A host's pause after a 429 as a page reads it (HostGate::pausedFor(), a test's), and a page's own way to ask again
// what a Try again names.
using PausedFor = std::function<std::chrono::milliseconds(const std::string& host)>;
using AskAgain  = std::function<void(Retry what)>;

// A Try again (RetryAfterPause) carried out: during its host's pause (`paused_for`), queued in `queue`, nothing sent
// before its end; at once otherwise (`ask`). Queued already: nothing more is sent.
void carryOutRetry(const Fix& fix, RetryQueue& queue, const PausedFor& paused_for, const AskAgain& ask);

// The Try again of a module out of App (#146), its own as App's are its pages' (App::retryNow(),
// App::runDueRetries()): asked at once outside its host's pause; during it, a click queues it, nothing sent before its
// end, and poll() asks it once the pause is over. `paused_for` reads the host's pause (the program's,
// context::Repairs::pausedFor()); `ask` is the module's own way to ask again what a Retry names.
class OwnRetries {
   public:
    OwnRetries(PausedFor paused_for, AskAgain ask) : paused_for_(std::move(paused_for)), ask_(std::move(ask)) {}

    // What a notice's Try again for `what`, asked of `host`, shows: the pause left, and whether a click queued it.
    [[nodiscard]] RetryState state(Retry what, const std::string& host) const;
    // A fix clicked: a Try again carried out (true); any other kind left to the program (false).
    bool carryOut(const Fix& fix);
    // Once a frame: what was queued and whose host no longer pauses, asked.
    void poll();
    [[nodiscard]] bool queued(Retry what) const { return queue_.queued(what); }

   private:
    PausedFor paused_for_;
    AskAgain ask_;
    RetryQueue queue_;
};

}  // namespace fcg
