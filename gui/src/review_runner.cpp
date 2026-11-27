#include "review_runner.hpp"

#include <algorithm>
#include <chrono>
#include <sstream>

#include <chess.hpp>
#include <tour/engine_text.hpp>

#include "engines.hpp"
#include "i18n.hpp"

namespace fcg {

namespace {

using Clock = std::chrono::steady_clock;

chess::Board boardOf(const AnalysisPosition& position) {
    chess::Board board;
    board.set960(position.chess960);
    board.setFen(position.fen);
    for (const auto& m : position.moves) board.makeMove(chess::uci::uciToMove(board, m));
    return board;
}

}  // namespace

int reviewPasses(int64_t ceiling_ms) {
    if (ceiling_ms <= 0) return 13;
    int n = 0;
    while (n < 13 && passTime(n) <= ceiling_ms) ++n;
    return std::max(1, n);
}

int reviewProcesses(int analysis_threads, int physical, int logical, int tournament_cores) {
    int processes = std::max(analysis_threads, std::min(4, physical / 2));
    if (tournament_cores > 0) processes = std::min(processes, logical - tournament_cores);
    return std::max(1, processes);
}

std::vector<AnalysisPosition> pvPositions(const AnalysisPosition& position, const std::string& pv, std::size_t max) {
    std::vector<AnalysisPosition> out;
    std::vector<std::string> moves;
    std::istringstream in(pv);
    for (std::string m; in >> m && moves.size() < max + 1;) moves.push_back(m);
    if (moves.size() < 2) return out;
    auto board = boardOf(position);
    auto at    = position;
    for (std::size_t k = 0; k + 1 < moves.size() && out.size() < max; ++k) {
        const auto move = chess::uci::uciToMove(board, moves[k]);
        chess::Movelist legal;
        chess::movegen::legalmoves(legal, board);
        if (std::find(legal.begin(), legal.end(), move) == legal.end()) break;
        board.makeMove(move);
        at.moves.push_back(moves[k]);
        out.push_back(at);
        if (board.isGameOver().second != chess::GameResult::NONE) break;
    }
    return out;
}

void ReviewRunner::start(Setup setup) {
    stop();
    stop_       = false;
    paused_     = false;
    pass_limit_ = 1 << 20;
    first_   = -1;
    ceiling_ = setup.ceiling_ms;
    std::vector<AnalysisPosition> positions;
    {
        AnalysisPosition position;
        position.chess960 = setup.chess960;
        chess::Board board;
        board.set960(setup.chess960);
        if (!setup.game.start_fen.empty()) board.setFen(setup.game.start_fen);
        position.fen = board.getFen();
        positions.push_back(position);
        for (const auto& m : setup.game.moves) {
            position.moves.push_back(chess::uci::moveToUci(m.move, setup.chess960));
            positions.push_back(position);
        }
    }
    {
        std::lock_guard lock(mutex_);
        const auto n = positions.size();
        state_       = {};
        state_.version++;
        state_.running        = true;
        state_.pass           = setup.resume.passes;
        state_.settled        = setup.resume;
        state_.settled.engine = setup.engine.name;
        state_.live           = state_.settled;
        state_.live.positions.resize(n);
        state_.level.assign(n, state_.pass - 1);
        state_.done.assign(n, false);
        offers_.clear();
        // Resumed with all the passes its ceiling asks for (a stop that came as its last pass ended): complete as it
        // is, nothing searched again, rather than a pass beyond the ceiling.
        if (state_.pass >= reviewPasses(ceiling_) || state_.pass >= 13) state_.running = false;
    }
    reproducible_ = setup.reproducible;
    {
        std::lock_guard lock(mutex_);
        if (!state_.running) return;
    }
    thread_ = std::thread([this, setup = std::move(setup), positions = std::move(positions)]() mutable {
        if (setup.reproducible)
            runReproducible(std::move(setup), std::move(positions));
        else
            run(std::move(setup), std::move(positions));
    });
}

void ReviewRunner::stop() {
    stop_ = true;
    if (thread_.joinable()) thread_.join();
    claim_.set(0);
    std::lock_guard lock(mutex_);
    if (state_.running) {
        state_.running = false;
        state_.version++;
    }
}

void ReviewRunner::offer(int ply, PositionEval value) {
    if (reproducible_) return;  // a search for a time: another review each time
    std::lock_guard lock(mutex_);
    std::erase_if(offers_, [&](const auto& o) { return o.first == ply; });
    offers_.emplace_back(ply, std::move(value));
}

bool ReviewRunner::state(State& out, uint64_t& version) const {
    std::lock_guard lock(mutex_);
    if (state_.version == version) return false;
    out     = state_;
    version = state_.version;
    return true;
}

bool ReviewRunner::running() const {
    std::lock_guard lock(mutex_);
    return state_.running;
}

template <typename F>
void ReviewRunner::update(F f) {
    std::lock_guard lock(mutex_);
    f(state_);
    state_.version++;
}

void ReviewRunner::run(Setup setup, std::vector<AnalysisPosition> positions) {
    const auto fail = [&](std::string message) {
        claim_.set(0);
        update([&](State& s) {
            s.error   = std::move(message);
            s.running = false;
        });
    };
    EngineProcess process;
    std::string why;
    if (!process.start(engineLaunch(setup.engine), why)) {
        fail(trf("Cannot start {}: {}", setup.engine.name, toUtf8(why)));
        return;
    }
    // Reads the engine's lines until one starting with `word`, passing the others to `each`. False on the deadline, a
    // stop asked (when `stoppable`) or the engine's end.
    const auto wait_for = [&](std::string_view word, Clock::time_point deadline, const auto& each, std::string* rest) {
        while (true) {
            for (auto& line : process.take()) {
                if (line.kind != EngineProcess::Line::Kind::Out) continue;
                const auto& text = line.text;
                if (text.rfind(word, 0) == 0 && (text.size() == word.size() || text[word.size()] == ' ')) {
                    if (rest) *rest = text.substr(std::min(text.size(), word.size() + 1));
                    return true;
                }
                each(text);
            }
            if (!process.running() || Clock::now() >= deadline) return false;
            process.wait(std::chrono::milliseconds(20));
        }
    };
    const auto startup = setup.engine.start_timeout > 0
                             ? std::chrono::milliseconds(setup.engine.start_timeout * 1000LL)
                             : std::chrono::duration_cast<std::chrono::milliseconds>(ENGINE_STARTUP_TIME);
    std::vector<UciOptionInfo> options;
    (void)process.send("uci");
    if (!wait_for(
            "uciok", Clock::now() + startup,
            [&](const std::string& line) {
                if (auto o = parseUciOption(line)) options.push_back(std::move(*o));
            },
            nullptr)) {
        fail(trf("{} does not answer uci: is it a UCI engine?", setup.engine.name));
        return;
    }
    const auto has = [&](const std::string& name) {
        return std::any_of(options.begin(), options.end(), [&](const auto& o) { return o.name == name; });
    };
    const auto set = [&](const std::string& name, const std::string& value) {
        if (has(name)) (void)process.send("setoption name " + name + " value " + value);
    };
    // The library's values, as a game sends them, buttons aside; then the analysis's own.
    for (const auto& [name, value] : tour::forEngine(setup.engine.options)) {
        if (name == "Threads" || name == "Hash" || name == "MultiPV" || name == "UCI_AnalyseMode") continue;
        const auto it = std::find_if(options.begin(), options.end(), [&](const auto& o) { return o.name == name; });
        if (it == options.end() || it->type == "button") continue;
        set(name, value);
    }
    set("UCI_ShowWDL", "true");
    set("UCI_AnalyseMode", "true");
    set("Threads", std::to_string(std::max(1, setup.settings.threads)));
    if (setup.settings.hash > 0) set("Hash", std::to_string(setup.settings.hash));
    set("MultiPV", "1");
    if (setup.chess960) set("UCI_Chess960", "true");
    const auto ready = [&] {
        (void)process.send("isready");
        return wait_for("readyok", Clock::now() + startup, [](const std::string&) {}, nullptr);
    };
    (void)process.send("ucinewgame");
    if (!ready()) {
        fail(trf("{} does not answer isready.", setup.engine.name));
        return;
    }
    const int threads = has("Threads") ? std::max(1, setup.settings.threads) : 1;

    // One search: false when stopped, paused (the search not finished) or the engine failed.
    const auto search = [&](const AnalysisPosition& position, int64_t ms, PositionEval* out) {
        (void)process.send(positionCommand(position));
        (void)process.send(goCommand(SearchLimit{std::max<int64_t>(1, ms), 0}));
        claim_.set(threads);
        std::vector<AnalysisLine> lines;
        AnalysisLine exact;  // the last line without a bound: a fail high or low has a PV of one move
        bool has_exact = false;
        std::string rest;
        const auto deadline = Clock::now() + std::chrono::milliseconds(ms) + std::chrono::seconds(5);
        bool interrupted    = false;
        bool got            = false;
        while (true) {
            got = wait_for(
                "bestmove", std::min(deadline, Clock::now() + std::chrono::milliseconds(50)),
                [&](const std::string& line) {
                    if (takeInfoLine(lines, line, 1) && !lines.empty() && !lines.front().lowerbound &&
                        !lines.front().upperbound && !lines.front().pv.empty()) {
                        exact     = lines.front();
                        has_exact = true;
                    }
                },
                &rest);
            if (got || !process.running()) break;
            if ((stop_ || paused_) && !interrupted) {
                (void)process.send("stop");
                interrupted = true;
            }
            if (Clock::now() >= deadline) {
                if (!interrupted) {
                    (void)process.send("stop");
                    interrupted = true;
                    continue;
                }
                break;
            }
        }
        claim_.set(0);
        if (!got) {
            fail(trf("{} did not give its move in time.", setup.engine.name));
            return false;
        }
        if (interrupted && (stop_ || paused_)) return false;
        if (out) {
            *out         = {};
            out->best    = rest.substr(0, rest.find(' '));
            out->time_ms = ms;
            if (has_exact || !lines.empty()) {
                const auto& line = has_exact ? exact : lines.front();
                out->score       = line.score;
                out->depth       = static_cast<int>(line.depth);
                out->wdl         = line.wdl;
                for (const auto& m : line.pv) out->pv += (out->pv.empty() ? "" : " ") + m;
            }
        }
        return true;
    };

    const int n = static_cast<int>(positions.size());
    std::vector<bool> over(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i)
        over[static_cast<std::size_t>(i)] = endScore(boardOf(positions[static_cast<std::size_t>(i)])).has_value();
    // The positions known already, deep enough: taken as they are at each pass.
    std::vector<std::optional<PositionEval>> known(static_cast<std::size_t>(n));
    if (setup.known)
        for (int i = 0; i < n && !stop_; ++i)
            if (!over[static_cast<std::size_t>(i)])
                known[static_cast<std::size_t>(i)] = setup.known(boardOf(positions[static_cast<std::size_t>(i)]));

    while (!stop_) {
        // What the board's analysis searched for longer, taken also while it pauses the analysis.
        int pass = 0;
        std::vector<bool> done;
        std::string previous_pv;
        int next = -1;
        {
            std::lock_guard lock(mutex_);
            pass = state_.pass;
            for (auto& [ply, value] : offers_) {
                if (ply < 0 || ply >= n) continue;
                auto& live = state_.live.positions[static_cast<std::size_t>(ply)];
                if (value.time_ms <= live.time_ms) continue;
                // A position known already: replaced only by a deeper search.
                if (const auto& k = known[static_cast<std::size_t>(ply)]; k && value.depth <= k->depth) continue;
                int level = 0;
                while (level < 12 && passTime(level + 1) <= value.time_ms) ++level;
                live = std::move(value);
                state_.level[static_cast<std::size_t>(ply)] =
                    std::max(state_.level[static_cast<std::size_t>(ply)], level);
                if (live.time_ms >= passTime(pass)) state_.done[static_cast<std::size_t>(ply)] = true;
                state_.version++;
            }
            offers_.clear();
        }
        // Paused while the board's analysis has the machine, or until the other analyses caught up.
        if (paused_ || pass > pass_limit_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
            continue;
        }
        {
            std::lock_guard lock(mutex_);
            // The positions the game ended in need no engine, nor those known already.
            for (int i = 0; i < n; ++i) {
                const auto at = static_cast<std::size_t>(i);
                if ((!over[at] && !known[at]) || state_.done[at]) continue;
                if (over[at]) {
                    state_.live.positions[at] = {
                        endScore(boardOf(positions[at])), "", 0, passTime(pass), "", std::nullopt};
                } else {
                    state_.live.positions[at]         = *known[at];
                    state_.live.positions[at].time_ms = passTime(pass);
                }
                state_.level[at] = pass;
                state_.done[at]  = true;
            }
            // The board's position first, then the review's order.
            const int first = first_;
            if (first >= 0 && first < n && !state_.done[static_cast<std::size_t>(first)]) {
                next = first;
            } else {
                for (int k = 0; k < n && next < 0; ++k) {
                    const int p = REVIEW_BACKWARD ? n - 1 - k : k;
                    if (!state_.done[static_cast<std::size_t>(p)]) next = p;
                }
            }
            if (next >= 0) previous_pv = state_.live.positions[static_cast<std::size_t>(next)].pv;
        }

        if (next < 0) {
            // The pass is complete: it is what is kept and judged; the next one, unless it was the last.
            bool last = false;
            update([&](State& s) {
                const auto before     = s.settled.passes > 0 ? reviewMoves(setup.game, setup.chess960, s.settled)
                                                             : std::vector<MoveReview>{};
                s.settled.positions   = s.live.positions;
                s.settled.movetime_ms = passTime(s.pass);
                s.settled.depth       = 0;
                s.settled.passes      = s.pass + 1;
                s.settled.previous.clear();
                for (const auto& m : before) s.settled.previous.push_back(static_cast<int8_t>(m.judgement));
                s.pass++;
                s.done.assign(static_cast<std::size_t>(n), false);
                last = s.pass >= reviewPasses(ceiling_) || s.pass >= 13;
                if (last) s.running = false;
            });
            if (last) break;
            continue;
        }

        // The principal variation of the previous pass, from its end back to the position; then the position.
        const int64_t total = passTime(pass);
        int64_t main        = total;
        bool ok             = true;
        if (REVIEW_PV_REPLAY && pass > 0 && !previous_pv.empty()) {
            const auto line = pvPositions(positions[static_cast<std::size_t>(next)], previous_pv, 8);
            if (!line.empty()) {
                const int64_t share = std::max<int64_t>(5, total / 4 / static_cast<int64_t>(line.size()));
                for (auto it = line.rbegin(); it != line.rend() && ok; ++it) ok = search(*it, share, nullptr);
                main = std::max<int64_t>(FIRST_PASS_MS, total - share * static_cast<int64_t>(line.size()));
            }
        }
        PositionEval value;
        if (ok) ok = search(positions[static_cast<std::size_t>(next)], main, &value);
        if (!ok) {
            std::lock_guard lock(mutex_);
            if (!state_.error.empty()) break;  // the engine failed
            continue;                          // stopped or paused: searched again later
        }
        value.time_ms = total;
        update([&](State& s) {
            if (s.pass != pass) return;
            s.live.positions[static_cast<std::size_t>(next)] = std::move(value);
            s.level[static_cast<std::size_t>(next)]          = pass;
            s.done[static_cast<std::size_t>(next)]           = true;
        });
    }
    claim_.set(0);
    process.stop(std::chrono::milliseconds(500));
    update([](State& s) { s.running = false; });
}

void ReviewRunner::runReproducible(Setup setup, std::vector<AnalysisPosition> positions) {
    const int n   = static_cast<int>(positions.size());
    const auto at = [](int i) { return static_cast<std::size_t>(i); };
    std::vector<bool> over(at(n));
    for (int i = 0; i < n; ++i) over[at(i)] = endScore(boardOf(positions[at(i)])).has_value();
    // The positions known already, deep enough: taken as they are at each pass.
    std::vector<std::optional<PositionEval>> known(at(n));
    if (setup.known)
        for (int i = 0; i < n && !stop_; ++i)
            if (!over[at(i)]) known[at(i)] = setup.known(boardOf(positions[at(i)]));
    const auto limit_of = [&](int pass) {
        return setup.limit ? setup.limit(pass) : SearchLimit{0, 0, passNodes(pass)};
    };
    int searched = 0;
    for (int i = 0; i < n; ++i) searched += !over[at(i)] && !known[at(i)];

    std::vector<bool> taken(at(n));  // under mutex_: a process searches it now
    std::atomic<bool> failed{false};
    const auto fail = [&](std::string message) {
        failed = true;
        update([&](State& s) {
            if (s.error.empty()) s.error = std::move(message);
            s.running = false;
        });
    };
    const auto startup = setup.engine.start_timeout > 0
                             ? std::chrono::milliseconds(setup.engine.start_timeout * 1000LL)
                             : std::chrono::duration_cast<std::chrono::milliseconds>(ENGINE_STARTUP_TIME);

    // One engine process: the positions not taken yet, one after the other, until the last pass is complete.
    const auto work = [&] {
        EngineProcess process;
        CoreClaim claim;
        std::string why;
        if (!process.start(engineLaunch(setup.engine), why)) {
            fail(trf("Cannot start {}: {}", setup.engine.name, toUtf8(why)));
            return;
        }
        // Reads the engine's lines until one starting with `word`, passing the others to `each`. False on the
        // deadline or the engine's end.
        const auto wait_for = [&](std::string_view word, Clock::time_point deadline, const auto& each,
                                  std::string* rest) {
            while (true) {
                for (auto& line : process.take()) {
                    if (line.kind != EngineProcess::Line::Kind::Out) continue;
                    const auto& text = line.text;
                    if (text.rfind(word, 0) == 0 && (text.size() == word.size() || text[word.size()] == ' ')) {
                        if (rest) *rest = text.substr(std::min(text.size(), word.size() + 1));
                        return true;
                    }
                    each(text);
                }
                if (!process.running() || Clock::now() >= deadline) return false;
                process.wait(std::chrono::milliseconds(20));
            }
        };
        std::vector<UciOptionInfo> options;
        (void)process.send("uci");
        if (!wait_for(
                "uciok", Clock::now() + startup,
                [&](const std::string& line) {
                    if (auto o = parseUciOption(line)) options.push_back(std::move(*o));
                },
                nullptr)) {
            fail(trf("{} does not answer uci: is it a UCI engine?", setup.engine.name));
            return;
        }
        const auto has = [&](const std::string& name) {
            return std::any_of(options.begin(), options.end(), [&](const auto& o) { return o.name == name; });
        };
        const auto set = [&](const std::string& name, const std::string& value) {
            if (has(name)) (void)process.send("setoption name " + name + " value " + value);
        };
        // The library's values, as a game sends them, buttons aside; then the review's own: one thread, a hash of
        // its fixed size.
        for (const auto& [name, value] : tour::forEngine(setup.engine.options)) {
            if (name == "Threads" || name == "Hash" || name == "MultiPV" || name == "UCI_AnalyseMode") continue;
            const auto it = std::find_if(options.begin(), options.end(), [&](const auto& o) { return o.name == name; });
            if (it == options.end() || it->type == "button") continue;
            set(name, value);
        }
        set("UCI_ShowWDL", "true");
        set("UCI_AnalyseMode", "true");
        set("Threads", "1");
        set("Hash", std::to_string(REVIEW_HASH_MB));
        set("MultiPV", "1");
        if (setup.chess960) set("UCI_Chess960", "true");
        const auto ready = [&] {
            (void)process.send("isready");
            return wait_for("readyok", Clock::now() + startup, [](const std::string&) {}, nullptr);
        };
        if (!ready()) {
            fail(trf("{} does not answer isready.", setup.engine.name));
            return;
        }

        enum class Searched { Done, Interrupted, Failed };
        // One search from an empty hash: the last exact line of each of its `lines` ranks, best first.
        const auto search = [&](const AnalysisPosition& position, const SearchLimit& limit, int lines,
                                std::vector<AnalysisLine>& out, std::string& best) {
            (void)process.send("ucinewgame");
            if (!ready()) {
                fail(trf("{} does not answer isready.", setup.engine.name));
                return Searched::Failed;
            }
            (void)process.send(positionCommand(position));
            (void)process.send(goCommand(limit));
            claim.set(1);
            ++searching_;
            std::vector<AnalysisLine> seen;
            out.assign(static_cast<std::size_t>(lines), AnalysisLine{});
            std::vector<bool> exact(static_cast<std::size_t>(lines));
            std::string rest;
            // A generous deadline: an engine of 20,000 nodes a second at least, or 10 minutes for a depth.
            auto deadline    = Clock::now() + std::chrono::seconds(30) +
                               (limit.nodes > 0 ? std::chrono::milliseconds(static_cast<int64_t>(limit.nodes / 20))
                                                : std::chrono::milliseconds(600000));
            bool interrupted = false, late = false, got = false;
            while (true) {
                got = wait_for(
                    "bestmove", std::min(deadline, Clock::now() + std::chrono::milliseconds(50)),
                    [&](const std::string& line) {
                        if (!takeInfoLine(seen, line, lines)) return;
                        // A fail high or low has a PV of one move: each rank's last line without a bound stays.
                        for (std::size_t r = 0; r < seen.size() && r < out.size(); ++r) {
                            const auto& l = seen[r];
                            if (l.score && !l.pv.empty() && !l.lowerbound && !l.upperbound) {
                                out[r]   = l;
                                exact[r] = true;
                            }
                        }
                    },
                    &rest);
                if (got || !process.running()) break;
                if ((stop_ || paused_ || failed) && !interrupted) {
                    (void)process.send("stop");
                    interrupted = true;
                    deadline    = Clock::now() + std::chrono::seconds(5);
                }
                if (Clock::now() >= deadline) {
                    if (interrupted) break;
                    (void)process.send("stop");  // its move, at least
                    interrupted = late = true;
                    deadline           = Clock::now() + std::chrono::seconds(5);
                }
            }
            claim.set(0);
            --searching_;
            if (!got || late) {
                if (!failed) fail(trf("{} did not give its move in time.", setup.engine.name));
                return Searched::Failed;
            }
            if (interrupted) return Searched::Interrupted;
            // Ranks the engine did not reach (fewer moves than lines) are dropped; a rank without an exact line takes
            // its last one.
            for (std::size_t r = 0; r < out.size(); ++r)
                if (!exact[r] && r < seen.size() && seen[r].score && !seen[r].pv.empty()) out[r] = seen[r];
            while (!out.empty() && !out.back().score) out.pop_back();
            best = rest.substr(0, rest.find(' '));
            return Searched::Done;
        };

        while (!stop_ && !failed) {
            int next = -1, pass = 0;
            bool finished = false;
            {
                std::lock_guard lock(mutex_);
                if (!state_.running) break;
                pass = state_.pass;
                offers_.clear();  // the board's searches, for a time: not taken
                if (!paused_ && pass <= pass_limit_) {
                    // The positions the game ended in need no engine, nor those known already.
                    for (int i = 0; i < n; ++i) {
                        if ((!over[at(i)] && !known[at(i)]) || state_.done[at(i)]) continue;
                        auto& live = state_.live.positions[at(i)];
                        if (over[at(i)]) {
                            live = {endScore(boardOf(positions[at(i)])), "", 0, 0, "", std::nullopt};
                        } else {
                            live         = *known[at(i)];
                            live.time_ms = 0;
                        }
                        state_.level[at(i)] = pass;
                        state_.done[at(i)]  = true;
                    }
                    // The board's position first, then from the last to the first.
                    const auto free = [&](int p) { return !state_.done[at(p)] && !taken[at(p)]; };
                    const int first = first_;
                    if (first >= 0 && first < n && free(first)) next = first;
                    for (int k = 0; k < n && next < 0; ++k)
                        if (free(n - 1 - k)) next = n - 1 - k;
                    if (next >= 0) {
                        taken[at(next)] = true;
                    } else if (std::none_of(taken.begin(), taken.end(), [](bool t) { return t; })) {
                        // The pass is complete: it is what is kept and judged; the next one, unless it was the last.
                        auto& st          = state_;
                        const auto lim    = limit_of(st.pass);
                        const auto before = st.settled.passes > 0 ? reviewMoves(setup.game, setup.chess960, st.settled)
                                                                  : std::vector<MoveReview>{};
                        st.settled.positions   = st.live.positions;
                        st.settled.movetime_ms = 0;
                        st.settled.nodes       = lim.nodes;
                        st.settled.depth       = lim.depth;
                        st.settled.passes      = st.pass + 1;
                        st.settled.previous.clear();
                        for (const auto& m : before) st.settled.previous.push_back(static_cast<int8_t>(m.judgement));
                        st.pass++;
                        st.done.assign(at(n), false);
                        if (st.pass >= reviewPasses(ceiling_) || st.pass >= 13) st.running = false;
                        st.version++;
                        finished = !st.running;
                    }
                }
            }
            if (finished) break;
            if (next < 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                continue;
            }
            // The position, then its second line where it is wanted.
            const auto limit = limit_of(pass);
            std::vector<AnalysisLine> lines;
            std::string best;
            auto outcome = search(positions[at(next)], limit, 1, lines, best);
            PositionEval value;
            if (outcome == Searched::Done) {
                value.best    = best;
                value.time_ms = 0;
                if (!lines.empty()) {
                    const auto& l = lines.front();
                    value.score   = l.score;
                    value.depth   = static_cast<int>(l.depth);
                    value.wdl     = l.wdl;
                    for (const auto& m : l.pv) value.pv += (value.pv.empty() ? "" : " ") + m;
                }
                if (setup.second_line && has("MultiPV") && value.score && setup.second_line(next, value)) {
                    set("MultiPV", "2");
                    outcome = search(positions[at(next)], limit, 2, lines, best);
                    set("MultiPV", "1");
                    // The best line of the other moves: the second, or the first when this search preferred another
                    // move than the first one did.
                    for (const auto& l : lines) {
                        if (l.pv.empty() || l.pv.front() == value.best) continue;
                        value.second_score = l.score;
                        value.second_depth = static_cast<int>(l.depth);
                        value.second_wdl   = l.wdl;
                        for (const auto& m : l.pv) value.second_pv += (value.second_pv.empty() ? "" : " ") + m;
                        break;
                    }
                }
            }
            std::lock_guard lock(mutex_);
            taken[at(next)] = false;
            if (outcome == Searched::Failed) break;
            if (outcome == Searched::Interrupted || state_.pass != pass) continue;  // searched again later
            state_.live.positions[at(next)] = std::move(value);
            state_.level[at(next)]          = pass;
            state_.done[at(next)]           = true;
            state_.version++;
        }
        claim.set(0);
        process.stop(std::chrono::milliseconds(500));
    };

    std::vector<std::thread> others;
    const int count = std::clamp(setup.processes, 1, std::max(1, searched));
    for (int k = 1; k < count; ++k) others.emplace_back(work);
    work();
    for (auto& t : others) t.join();
    update([](State& s) { s.running = false; });
}

}  // namespace fcg
