#pragma once

// One of the user's engines offered to lichess.org's analysis board through Lichess's external engine API (#96, mockup
// M13, board 9), without ImGui. The API is in alpha, "subject to change" (spec 2.0.180):
// - the registration, with the user's token (engine:write; engine:read to read it): the engine's name at Lichess (3 to
//   200 characters), its threads and memory at most, the variants it serves ("chess", an engine always in UCI_Chess960
//   mode), and a secret drawn here (16 to 1024 characters) that Lichess never shows again. Each engine offered has its
//   own registration and its own secret, kept in the profile (lichess-engines.json), readable by its owner alone;
// - the service, on a thread of its own: it waits for work at the work host (engine.lichess.ovh) with the secret alone
//   (a long poll, renewed; 204: nothing to do, asked again), and answers a job (a starting FEN and moves, threads,
//   memory, 1 to 5 lines, a time, a depth or nodes): the engine set (UCI_Chess960, UCI_AnalyseMode when it has it,
//   Threads, Hash, MultiPV), its info lines (depth, multipv, score, nodes, time, pv) streamed as the body of the
//   answer, then its bestmove. The work host closing the answer (the one who asked went away) stops the engine;
// - its guards, given by the window each frame (R6, R17): the threads a tournament leaves free (none: paused, the
//   requests unanswered), the user's live game on Lichess (paused until its end), the positions of the user's
//   correspondence games (never analysed: an empty answer), the bot's threads first.
// What leaves the machine: at the registration, the name, the limits and the secret, with the token; then the engine's
// lines for the positions Lichess sends (they come from the user's browser, through Lichess, which sees the lines).
// The secret is never logged, shown, nor put in an error message.

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <json.hpp>

#include "lichess_api.hpp"
#include "model.hpp"

namespace fcg::lichess {

// Registering an engine for the account, reading it, withdrawing it: asked together, the first time.
inline constexpr const char* SCOPE_ENGINE_READ  = "engine:read";
inline constexpr const char* SCOPE_ENGINE_WRITE = "engine:write";

// ---------------------------------------------------------------------------
// The registration

// What Lichess's registration takes (ExternalEngineRegistration).
struct EngineRegistration {
    std::string name;             // 3 to 200 characters
    int max_threads = 1;          // 1 to 65536
    int max_hash    = 16;         // MiB, 1 to 1048576
    std::string provider_secret;  // 16 to 1024 characters
    std::string provider_data;    // empty: not sent
};
inline constexpr int NAME_CHARS_MIN = 3, NAME_CHARS_MAX = 200;
inline constexpr int SECRET_CHARS_MIN = 16, SECRET_CHARS_MAX = 1024;
inline constexpr int THREADS_MOST = 65536, HASH_MOST_MB = 1048576;

// What Lichess keeps of a registration (ExternalEngine): the secret is not in it.
struct ExternalEngine {
    std::string id;  // "eei_…"
    std::string name;
    std::string client_secret;  // what the user's browser asks with ("ees_…"), not the provider's
    std::string user_id;
    int max_threads = 0, max_hash = 0;
    std::vector<std::string> variants;
    std::string provider_data;
};
// nullopt when `j` is not one (a field it requires missing).
[[nodiscard]] std::optional<ExternalEngine> readExternalEngine(const nlohmann::json& j);

// The registration's body (the secret in it: never logged).
[[nodiscard]] std::string registrationJson(const EngineRegistration& r);
// Why Lichess would refuse it, in English for the log; empty when it is fine: the name's length, the limits, the
// secret's length.
[[nodiscard]] std::string registrationProblem(const EngineRegistration& r);
// The name's length as Lichess counts it: in characters, not bytes.
[[nodiscard]] int nameLength(std::string_view utf8);
// A secret drawn here: 48 letters and digits. Throws std::runtime_error when there are no random numbers.
[[nodiscard]] std::string newProviderSecret();

// The registration's requests, with the user's token, one at a time to lichess.org: their failures worded as the
// external engines' (Asked::Engines), a refusal in Lichess's words; a registration Lichess would refuse, in
// registrationProblem's words (Failure::error).
[[nodiscard]] Answer<ExternalEngine> registerEngine(const std::string& token, const EngineRegistration& r,
                                                    const std::atomic<bool>* cancel = nullptr);
[[nodiscard]] Answer<std::vector<ExternalEngine>> listEngines(const std::string& token,
                                                              const std::atomic<bool>* cancel = nullptr);
// The registration `id`: no value and no error when Lichess has it no more (404: withdrawn on lichess.org).
[[nodiscard]] Answer<ExternalEngine> engineRegistration(const std::string& token, const std::string& id,
                                                        const std::atomic<bool>* cancel = nullptr);
// A registration changed (its name, limits, or a new secret).
[[nodiscard]] Answer<ExternalEngine> updateEngine(const std::string& token, const std::string& id,
                                                  const EngineRegistration& r,
                                                  const std::atomic<bool>* cancel = nullptr);
// The registration deleted: no failure when Lichess took it (or has it no more).
[[nodiscard]] Failure withdrawEngine(const std::string& token, const std::string& id,
                                     const std::atomic<bool>* cancel = nullptr);

// ---------------------------------------------------------------------------
// The work

// A job as the work host sends it: ExternalEngineWork and the registration it is for.
struct EngineWork {
    std::string id;         // the job's: its answer goes to /api/external-engine/work/{id}
    std::string engine_id;  // the registration's
    std::string session;    // sessionId: another one, another game for the engine (ucinewgame)
    int threads = 1, hash = 16, multipv = 1;
    std::string variant = "chess";
    std::string initial_fen;
    std::vector<std::string> moves;  // UCI, in UCI_Chess960's form
    // One of them, as the spec's oneOf has it: milliseconds, plies, nodes.
    int64_t movetime = 0;
    int depth        = 0;
    int64_t nodes    = 0;
};
// The work host's answer read; nullopt, with why in `error`, when it is not one this program reads (a field missing or
// out of its bounds, no limit): the API changed.
[[nodiscard]] std::optional<EngineWork> readEngineWork(std::string_view json, std::string* error = nullptr);
// "position fen <initialFen> moves …", and the go: "go movetime 5000", "go depth 20", "go nodes 1000000".
[[nodiscard]] std::string workPosition(const EngineWork& w);
[[nodiscard]] std::string workGo(const EngineWork& w);
// The position asked, after its moves, as a FEN; empty when its moves are not legal there.
[[nodiscard]] std::string workFen(const EngineWork& w);

// A move as UCI writes it (e2e4, e7e8q; castling as e1h1 or e1g1): what a job's moves and an engine's lines must be
// before a board or an engine reads them.
[[nodiscard]] bool uciMoveText(std::string_view move);

// An engine's line as the work host takes it: an info line with a depth, a score and a pv (multipv 1 added when it has
// none, as the engine's time since its go when it tells none, and nodes when it tells none), or its bestmove; nullopt
// for the other lines (info string, currmove, a refutation…).
[[nodiscard]] std::optional<std::string> lineForLichess(std::string_view line, int64_t elapsed_ms);

// Whether a job is never analysed (R17): its line passes through the position of one of the user's correspondence
// games (`locked`, FENs; compared by the men and the side to move, as #48's lock does), or reaches one with a single
// move from its end (the engine's line would give the answer to it); `start_locked`: a correspondence game has had no
// move yet, which locks the start position alone.
[[nodiscard]] bool workLocked(const EngineWork& w, const std::vector<std::string>& locked, bool start_locked);

// The threads the service may take now (R6): its own limit, within what the machine's other engines leave of its
// `capacity`: the tournaments playing on it (`tournaments`, each at its concurrency), and here the bot's game and the
// Board page's engines (`others`). 0 pauses it.
[[nodiscard]] int serviceThreads(int limit, int capacity, int others, int tournaments);

// ---------------------------------------------------------------------------
// What the profile keeps: lichess-engines.json, readable by its owner alone (the secrets are in it)

inline constexpr const char* ENGINES_FILE = "lichess-engines.json";

struct OfferedEngine {
    std::string engine;    // the library's name
    std::string name;      // its name at Lichess
    std::string machine;   // empty: this computer; else a machine of Settings > Machines, by its id
    int threads = 0;       // at most
    int hash_mb = 0;       // at most
    std::string id;        // the registration's at Lichess; empty: not registered
    std::string user;      // the account it was registered for (Lichess's userId)
    std::string secret;    // the provider's: never shown, never logged
    bool serving = false;  // in service when RookStudio closed: resumed at the next start
    // Today's (the day as YYYY-MM-DD, local): the requests received (the one being answered too) and the
    // milliseconds of analysis.
    std::string day;
    int requests                                = 0;
    int64_t analysis_ms                         = 0;
    bool operator==(const OfferedEngine&) const = default;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(OfferedEngine, engine, name, machine, threads, hash_mb, id, user,
                                                secret, serving, day, requests, analysis_ms)

[[nodiscard]] std::vector<OfferedEngine> loadOffers(const std::filesystem::path& profile);
// False, with why in `error`, when it cannot be written.
bool saveOffers(const std::filesystem::path& profile, const std::vector<OfferedEngine>& offers,
                std::string* error = nullptr);
// Today's counts: requests received, milliseconds of analysis (the day turned: they start again).
void countRequest(OfferedEngine& offer, int requests, int64_t ms, const std::string& today);
// The registrations to serve again at the start: those in service when RookStudio closed, their engine in the library.
[[nodiscard]] std::vector<std::string> offersToResume(const std::vector<OfferedEngine>& offers,
                                                      const std::vector<EngineEntry>& library);

// ---------------------------------------------------------------------------
// The service

enum class ServiceState {
    Waiting,       // for a request, at the work host
    Busy,          // answering one
    Paused,        // a guard holds it (pause() says which): the requests unanswered
    Offline,       // the work host does not answer: asked again by itself
    Refused,       // the work host refuses the secret (401): to register again with a new one
    ApiChanged,    // an answer this program does not read: it stops rather than answer wrongly
    EngineFailed,  // the engine could not start, or cannot serve (no UCI_Chess960)
};
enum class ServicePause { None, Tournament, LiveGame };

// What the window gives the service each frame.
struct ServiceGuards {
    int threads    = 1;                                   // at most, now: 0 pauses it (a tournament takes them all)
    bool live_game = false;                               // the user's live game on Lichess: paused until its end
    std::vector<std::string> locked;                      // the positions of the user's correspondence games (FENs)
    bool start_locked                           = false;  // one of them has had no move yet
    bool operator==(const ServiceGuards&) const = default;
};

// A request answered, or being answered.
struct ServiceJob {
    std::string fen;  // the position asked, after its moves
    int multipv = 1, threads = 1, hash = 16;
    int64_t movetime    = 0;  // its limit, as the work said it
    int limit_depth     = 0;
    int64_t limit_nodes = 0;
    int depth           = 0;  // what the engine reached
    int64_t nps         = 0;
    std::string best;     // the best line's first move (UCI)
    bool locked = false;  // a position of a correspondence game's: answered empty
    bool cut    = false;  // the one who asked went away before its end
    std::chrono::steady_clock::time_point since{};
};

struct ServiceStatus {
    uint64_t version   = 0;
    ServiceState state = ServiceState::Waiting;
    ServicePause pause = ServicePause::None;
    Failure failure;    // Offline: the work host unanswered (Offline), too many requests (RateLimited), in trouble
                        // (Server); Refused: the secret refused (Unauthorized, Forbidden)
    std::string error;  // why, in the program's words: the API's change, the engine's failure
    bool chess960_missing = false;  // EngineFailed: the engine does not know the Chess960 mode, which Lichess asks for
    std::optional<ServiceJob> job;                     // being answered
    std::optional<ServiceJob> last;                    // the last one answered
    int threads_now = 0;                               // the engine's threads while it answers, in the core budget
    std::chrono::steady_clock::time_point retry_at{};  // offline: when the host is asked again
};

class EngineService {
   public:
    struct Setup {
        EngineEntry engine;     // as it runs: its program here, or on a machine (`machine` set)
        std::string secret;     // the provider's: never logged
        std::string engine_id;  // the registration it serves
        int max_threads = 1, max_hash = 16;
        std::string host;  // the work host: servers().engine
        // Offline: the waits before the host is asked again, the last one repeated.
        std::vector<std::chrono::milliseconds> retry{std::chrono::seconds(5), std::chrono::seconds(15),
                                                     std::chrono::seconds(30), std::chrono::seconds(60)};
        std::chrono::milliseconds idle_quit{std::chrono::minutes(5)};   // the engine quits after so long unasked
        std::chrono::milliseconds stop_grace{std::chrono::seconds(5)};  // its bestmove awaited after a stop
        int poll_idle_s = 90;                          // the long poll given up after so long without a word
        std::function<void(const std::string&)> sent;  // each line sent to the engine (the tests')
    };
    explicit EngineService(Setup setup, ServiceGuards guards = {});
    ~EngineService();
    EngineService(const EngineService&)            = delete;
    EngineService& operator=(const EngineService&) = delete;

    void setGuards(const ServiceGuards& guards);
    [[nodiscard]] ServiceStatus status() const;
    [[nodiscard]] bool status(ServiceStatus& out, uint64_t& version) const;  // copied when it changed since `version`
    // The requests received (counted as they come) and the milliseconds of analysis of those answered, since the
    // last call.
    struct Counts {
        int requests = 0;
        int64_t ms   = 0;
    };
    [[nodiscard]] Counts takeCounts();
    // Its thread ended: the secret refused, the API changed, the engine failed.
    [[nodiscard]] bool ended() const { return ended_; }
    [[nodiscard]] const std::string& engineName() const { return setup_.engine.name; }

    struct Engine;  // the engine's process and what it said

   private:
    void run();
    void answer(const EngineWork& w);
    [[nodiscard]] ServiceGuards guards() const;
    template <typename F>
    void update(F f);
    // Waits `d`, or less when stopped or when the guards change (true: the wait went to its end).
    bool rest(std::chrono::milliseconds d);

    Setup setup_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;  // the guards changed, or it stops
    ServiceGuards guards_;
    ServiceStatus status_;
    Counts counts_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> quit_{false};         // it ends by itself: its engine failed
    std::atomic<bool> poll_cancel_{false};  // the long poll given up: stopped, or a guard paused it
    std::atomic<bool> ended_{false};
    std::unique_ptr<Engine> engine_;
    std::string session_;  // the last job's: another one, a new game for the engine
    std::chrono::steady_clock::time_point last_job_{};
    std::thread thread_;
};

}  // namespace fcg::lichess
