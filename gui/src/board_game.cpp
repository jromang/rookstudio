#include "board_game.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <deque>

#include <engine/uci_engine.hpp>
#include <tour/engine_text.hpp>
#include <tour/pgn_file.hpp>

#include <fmt/include/fmt/format.h>

#include "board_text.hpp"
#include "engines.hpp"
#include "i18n.hpp"

namespace fcg {

namespace {

using Clock = std::chrono::steady_clock;

// An engine playing a game (EnginePlayer), from the game's thread; or, with a `seat`, the user, whose moves the window
// gives there.
class Player {
   public:
    Player(const EngineEntry& entry, const std::atomic<bool>& stop, const std::atomic<bool>& resign,
           const std::atomic<bool>& takeback, BoardSeat* seat = nullptr)
        : stop_(stop), resign_(resign), takeback_(takeback), seat_(seat), engine_(entry, stop, [&resign, &takeback] {
              return resign || takeback;
          }) {}

    // Started, given the library's option values (and Chess960 when asked), and ready. False with the reason.
    bool start(bool chess960, std::string& error) { return seat_ || engine_.start(chess960, error); }

    bool newGame() { return seat_ || engine_.newGame(); }

    // Its move for the position, within `budget` (its clock and the margin): the move in UCI, or nullopt (out of
    // time, stopped, resigned, a move taken back, or the engine ended). `info` gets each info line with a score.
    std::optional<std::string> go(const std::string& position, const std::string& go, std::chrono::milliseconds budget,
                                  const std::function<void(const std::string&)>& info) {
        if (seat_) return userMove(budget);
        return engine_.go(position, go, budget, info);
    }

    [[nodiscard]] bool alive() const { return seat_ || engine_.alive(); }
    void quit() {
        if (!seat_) engine_.quit();
    }

   private:
    // The user's move, given by the window while their turn lasts: nullopt when their clock ran out, the games were
    // stopped, they resigned or took a move back. A move given before their turn is not taken.
    std::optional<std::string> userMove(std::chrono::milliseconds budget) {
        std::unique_lock lock(seat_->mutex);
        seat_->move.reset();
        seat_->turn = true;
        seat_->cv.wait_until(lock, Clock::now() + budget, [&] { return seat_->move || stop_ || resign_ || takeback_; });
        seat_->turn = false;
        return std::exchange(seat_->move, std::nullopt);
    }

    const std::atomic<bool>& stop_;
    const std::atomic<bool>& resign_;
    const std::atomic<bool>& takeback_;
    BoardSeat* seat_ = nullptr;
    EnginePlayer engine_;
};

// The game as the user sees it while they play it: the engine's scores, depths and lines left out.
ReplayGame withoutEvaluations(ReplayGame game) {
    for (auto& m : game.moves) {
        m.score.reset();
        m.wdl.reset();
        m.depth    = 0;
        m.seldepth = 0;
        m.nodes    = 0;
        m.nps      = 0;
        m.tbhits   = 0;
        m.pv.clear();
    }
    return game;
}

std::string today() {
    const auto now = std::time(nullptr);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    char text[16];
    std::strftime(text, sizeof(text), "%Y.%m.%d", &local);
    return text;
}

// The PGN TimeControl of a clock: "60+0.6".
std::string pgnTimeControl(const GameClock& c) { return fmt::format("{:g}+{:g}", c.base_s, c.inc_s); }

}  // namespace

EnginePlayer::EnginePlayer(EngineEntry entry, const std::atomic<bool>& stop, std::function<bool()> interrupt)
    : entry_(std::move(entry)), stop_(stop), interrupt_(std::move(interrupt)) {}

EnginePlayer::~EnginePlayer() { quit(); }

bool EnginePlayer::start(bool chess960, std::string& error,
                         const std::vector<std::pair<std::string, std::string>>& more) {
    std::string why;
    if (!process_.start(engineLaunch(entry_), why)) {
        error = trf("Cannot start {}: {}", entry_.name, toUtf8(why));
        return false;
    }
    const auto timeout = entry_.start_timeout > 0
                             ? std::chrono::milliseconds(entry_.start_timeout * 1000LL)
                             : std::chrono::duration_cast<std::chrono::milliseconds>(ENGINE_STARTUP_TIME);
    (void)process_.send("uci");
    std::vector<UciOptionInfo> options;
    if (!waitFor("uciok", Clock::now() + timeout, [&](const std::string& line) {
            if (auto o = parseUciOption(line)) options.push_back(std::move(*o));
        })) {
        error = trf("{} does not answer uci: is it a UCI engine?", entry_.name);
        return false;
    }
    options_.clear();
    for (const auto& o : options)
        if (o.type != "button") options_.push_back(o.name);
    for (const auto& [name, value] : tour::forEngine(entry_.options)) {
        if (!has(name)) continue;
        if (std::any_of(more.begin(), more.end(), [&](const auto& m) { return m.first == name; })) continue;
        (void)process_.send("setoption name " + name + " value " + value);
    }
    if (chess960 && has("UCI_Chess960")) (void)process_.send("setoption name UCI_Chess960 value true");
    for (const auto& [name, value] : more)
        if (has(name)) (void)process_.send("setoption name " + name + " value " + value);
    if (!ready(timeout)) {
        error = trf("{} does not answer isready.", entry_.name);
        return false;
    }
    return true;
}

bool EnginePlayer::newGame() {
    (void)process_.send("ucinewgame");
    return ready(std::chrono::seconds(60));
}

std::optional<std::string> EnginePlayer::go(const std::string& position, const std::string& go,
                                            std::chrono::milliseconds budget,
                                            const std::function<void(const std::string&)>& info, bool stop_late) {
    (void)process_.send(position);
    (void)process_.send(go);
    std::optional<std::string> move;
    const auto each = [&](const std::string& line) {
        if (line.rfind("info", 0) == 0 && line.find(" score ") != std::string::npos) info(line);
    };
    if (waitFor("bestmove", Clock::now() + budget, each, &move, true)) return move;
    // Interrupted while it searched: stopped and its bestmove read, ready for the next search. Late, with
    // `stop_late`: stopped, and its move taken if it comes soon.
    const bool interrupted = interrupt_ && interrupt_();
    if (interrupted && !stop_ && process_.running()) {
        (void)process_.send("stop");
        (void)waitFor("bestmove", Clock::now() + std::chrono::seconds(2), [](const std::string&) {});
    } else if (stop_late && !stop_ && process_.running()) {
        (void)process_.send("stop");
        if (waitFor("bestmove", Clock::now() + std::chrono::seconds(1), each, &move)) return move;
    }
    return std::nullopt;
}

bool EnginePlayer::alive() const { return process_.running(); }

bool EnginePlayer::has(const std::string& option) const {
    return std::find(options_.begin(), options_.end(), option) != options_.end();
}

void EnginePlayer::quit() {
    if (process_.started()) process_.stop(std::chrono::milliseconds(500));
}

bool EnginePlayer::ready(std::chrono::milliseconds timeout) {
    (void)process_.send("isready");
    return waitFor("readyok", Clock::now() + timeout, [](const std::string&) {});
}

bool EnginePlayer::waitFor(std::string_view word, Clock::time_point deadline,
                           const std::function<void(const std::string&)>& each, std::optional<std::string>* move,
                           bool interruptible) {
    while (true) {
        for (auto& line : process_.take()) {
            if (line.kind != EngineProcess::Line::Kind::Out) continue;
            const auto& text = line.text;
            if (text.rfind(word, 0) == 0 && (text.size() == word.size() || text[word.size()] == ' ')) {
                if (move) {
                    auto rest = text.substr(std::min(text.size(), word.size() + 1));
                    *move     = rest.substr(0, rest.find(' '));
                }
                return true;
            }
            each(text);
        }
        if (stop_ || (interruptible && interrupt_ && interrupt_()) || !process_.running() || Clock::now() >= deadline)
            return false;
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now());
        process_.wait(std::clamp(left, std::chrono::milliseconds(1), std::chrono::milliseconds(50)));
    }
}

bool engineAcceptsDraw(const std::optional<fastchess::Score>& score, int plies) {
    if (!score) return false;
    if (score->type == fastchess::ScoreType::MATE) return score->value < 0;
    if (score->value <= -150) return true;
    return plies >= 40 && score->value <= 10;
}

std::string humanName() {
    for (const char* var : {"USER", "USERNAME", "LOGNAME"}) {
        if (const char* value = std::getenv(var); value && *value) return toUtf8(value);
    }
    return "Human";
}

std::string clockText(const GameClock& c) {
    if (c.base_s >= 60 && std::fmod(c.base_s, 60.0) == 0) return fmt::format("{:g}+{:g}", c.base_s / 60, c.inc_s);
    const auto whole = static_cast<int>(c.base_s);
    return fmt::format("{}:{:02}+{:g}", whole / 60, whole % 60, c.inc_s);
}

template <typename F>
void BoardMatch::update(F f) {
    std::lock_guard lock(mutex_);
    f(state_);
    ++state_.version;
}

bool BoardMatch::start(const EngineEntry& first, const EngineEntry& second, const BoardMatchSetup& setup,
                       const std::filesystem::path& pgn, std::string& error) {
    stop();
    for (int i = 0; i < 2; ++i) {
        if (setup.human == i) continue;
        const auto* e = i == 0 ? &first : &second;
        std::error_code ec;
        if (e->machine.empty() && !std::filesystem::is_regular_file(e->cmd, ec)) {
            error = trf("File not found: {}", e->cmd);
            return false;
        }
    }
    {
        std::lock_guard lock(mutex_);
        const auto version = state_.version;
        state_             = State{};
        state_.version     = version + 1;
        state_.games       = setup.pair ? 2 : 1;
        state_.running     = true;
        state_.pgn         = pgn;
    }
    stop_       = false;
    resign_     = false;
    takeback_   = false;
    draw_offer_ = false;
    claim_.set(std::max(setup.human == 0 ? 0 : first.threads(), setup.human == 1 ? 0 : second.threads()));
    thread_ = std::thread([this, first, second, setup, pgn] { run(first, second, setup, pgn); });
    return true;
}

void BoardMatch::stop() {
    {
        // Under the seat's lock: the user's wait cannot miss it.
        std::lock_guard lock(seat_.mutex);
        stop_ = true;
    }
    seat_.cv.notify_all();
    if (thread_.joinable()) thread_.join();
    claim_.set(0);
}

void BoardMatch::play(const std::string& uci) {
    {
        std::lock_guard lock(seat_.mutex);
        if (!seat_.turn) return;
        seat_.move = uci;
    }
    seat_.cv.notify_all();
}

void BoardMatch::resign() {
    {
        std::lock_guard lock(mutex_);
        if (!state_.running || !state_.you) return;
    }
    {
        std::lock_guard lock(seat_.mutex);
        resign_ = true;
    }
    seat_.cv.notify_all();
}

void BoardMatch::takeBack() {
    {
        std::lock_guard lock(mutex_);
        if (!state_.running || !state_.you) return;
        const auto you = *state_.you;
        if (std::none_of(state_.current.moves.begin(), state_.current.moves.end(),
                         [&](const ReplayMove& m) { return m.mover == you; }))
            return;
    }
    {
        std::lock_guard lock(seat_.mutex);
        takeback_ = true;
    }
    seat_.cv.notify_all();
}

void BoardMatch::offerDraw() {
    std::lock_guard lock(mutex_);
    if (!state_.running || !state_.you || state_.draw_offered) return;
    draw_offer_         = true;
    state_.draw_offered = true;
    ++state_.version;
}

bool BoardMatch::state(State& out, uint64_t& version) const {
    std::lock_guard lock(mutex_);
    if (state_.version == version) return false;
    out     = state_;
    version = state_.version;
    return true;
}

bool BoardMatch::running() const {
    std::lock_guard lock(mutex_);
    return state_.running;
}

void BoardMatch::run(EngineEntry first, EngineEntry second, BoardMatchSetup setup, std::filesystem::path pgn) {
    Player one(first, stop_, resign_, takeback_, setup.human == 0 ? &seat_ : nullptr);
    Player two(second, stop_, resign_, takeback_, setup.human == 1 ? &seat_ : nullptr);
    // While the user plays, what the engine thinks is not shown: its scores come with the finished game.
    const bool hide = setup.human >= 0;
    std::string error;
    if (!one.start(setup.chess960, error) || !two.start(setup.chess960, error)) {
        update([&](State& s) {
            s.error   = stop_ ? std::string() : error;
            s.running = false;
        });
        one.quit();
        two.quit();
        return;
    }

    const int games = setup.pair ? 2 : 1;
    for (int g = 0; g < games && !stop_; ++g) {
        // The first engine has White in the first game, Black in the second.
        Player& white             = g == 0 ? one : two;
        Player& black             = g == 0 ? two : one;
        const auto& white_name    = g == 0 ? first.name : second.name;
        const auto& black_name    = g == 0 ? second.name : first.name;
        const GameClock clocks[2] = {g == 0 ? setup.first_clock : setup.second_clock,
                                     g == 0 ? setup.second_clock : setup.first_clock};
        const std::array<uint64_t, 2> nodes = {g == 0 ? setup.nodes[0] : setup.nodes[1],
                                               g == 0 ? setup.nodes[1] : setup.nodes[0]};

        ReplayGame game;
        game.start_fen = setup.fen;
        game.headers   = {{"Event", setup.event.empty() ? std::string("RookStudio board game") : setup.event},
                          {"Site", "?"},
                          {"Date", today()},
                          {"Round", std::to_string(g + 1)},
                          {"White", white_name},
                          {"Black", black_name}};
        if (pgnTimeControl(clocks[0]) == pgnTimeControl(clocks[1])) {
            game.headers.emplace_back("TimeControl", pgnTimeControl(clocks[0]));
        } else {
            game.headers.emplace_back("WhiteTimeControl", pgnTimeControl(clocks[0]));
            game.headers.emplace_back("BlackTimeControl", pgnTimeControl(clocks[1]));
        }
        for (const auto& tag : setup.tags) game.headers.push_back(tag);
        std::array<int64_t, 2> clock_ms     = {static_cast<int64_t>(clocks[0].base_s * 1000),
                                               static_cast<int64_t>(clocks[1].base_s * 1000)};
        const std::array<int64_t, 2> inc_ms = {static_cast<int64_t>(clocks[0].inc_s * 1000),
                                               static_cast<int64_t>(clocks[1].inc_s * 1000)};
        // The user's side: the first has White in the first game.
        std::optional<chess::Color> you;
        if (setup.human >= 0) you = (setup.human == 0) == (g == 0) ? chess::Color::WHITE : chess::Color::BLACK;
        // The engine's openings, until they have no move (#83); what a move taken back takes back with it.
        bool book_on    = static_cast<bool>(setup.book) && you.has_value();
        int last_book   = -1;
        int book_out_at = -1;  // the ply they had no move at
        std::vector<std::array<int64_t, 2>> clocks_before;
        int takebacks = 0;

        chess::Board board;
        board.set960(setup.chess960);
        board.setFen(setup.fen);
        std::vector<std::string> moves;
        // A game resumed (#83): its moves so far, its clocks and what it had taken back.
        if (setup.resume && g == 0) {
            game = *setup.resume;
            if (game.start_fen.empty()) game.start_fen = setup.fen;
            for (const auto& m : game.moves) {
                moves.push_back(chess::uci::moveToUci(m.move, setup.chess960));
                board.makeMove<true>(m.move);
            }
            clock_ms      = setup.resume_clock_ms;
            clocks_before = setup.resume_clocks_before;
            clocks_before.resize(game.moves.size(), clock_ms);
            takebacks = setup.resume_takebacks;
            last_book = setup.resume_last_book;
            if (setup.resume_book_out && book_on) {
                book_on     = false;
                book_out_at = last_book >= 0 ? last_book + 2 : 0;
            }
        }
        resign_     = false;
        takeback_   = false;
        draw_offer_ = false;
        update([&](State& s) {
            s.game          = g;
            s.current       = hide ? withoutEvaluations(game) : game;
            s.clock_ms      = clock_ms;
            s.clocks_before = clocks_before;
            s.takebacks     = takebacks;
            s.last_book     = last_book;
            s.book_out      = !book_on;
            s.draw_offered  = false;
            s.draw_declined = -1;
            s.thinking.reset();
            s.info.clear();
            s.you = you;
        });

        std::string result = "*", reason;
        if (!white.newGame() || !black.newGame()) {
            reason = TR("an engine does not answer isready");
            stop_  = true;
        }

        while (!stop_) {
            const auto [why, outcome] = board.isGameOver();
            if (outcome != chess::GameResult::NONE) {
                const bool mated = why == chess::GameResultReason::CHECKMATE;
                result           = mated ? (board.sideToMove() == chess::Color::WHITE ? "0-1" : "1-0") : "1/2-1/2";
                reason           = mated                                                   ? TR("checkmate")
                                   : why == chess::GameResultReason::STALEMATE             ? TR("stalemate")
                                   : why == chess::GameResultReason::INSUFFICIENT_MATERIAL ? TR("insufficient material")
                                   : why == chess::GameResultReason::FIFTY_MOVE_RULE       ? TR("50-move rule")
                                                                                           : TR("threefold repetition");
                break;
            }
            if (moves.size() >= 1000) {
                result = "1/2-1/2";
                reason = TR("game too long");
                break;
            }
            const auto stm       = board.sideToMove();
            const int side       = stm == chess::Color::WHITE ? 0 : 1;
            Player& player       = side == 0 ? white : black;
            const auto& name     = side == 0 ? white_name : black_name;
            const bool engine_up = you && stm != *you;

            // The engine's openings first: their move played at once, taking no time (#83).
            std::optional<std::string> book_move;
            if (engine_up && book_on) {
                book_move = setup.book(board);
                if (!book_move) {
                    book_on     = false;
                    book_out_at = static_cast<int>(game.moves.size());
                }
            }
            std::string position = "position fen " + setup.fen;
            if (!moves.empty()) {
                position += " moves";
                for (const auto& m : moves) position += " " + m;
            }
            auto go = fmt::format("go wtime {} btime {} winc {} binc {}", std::max<int64_t>(1, clock_ms[0]),
                                  std::max<int64_t>(1, clock_ms[1]), inc_ms[0], inc_ms[1]);
            if (nodes[static_cast<std::size_t>(side)] > 0)
                go += fmt::format(" nodes {}", nodes[static_cast<std::size_t>(side)]);
            const auto start = Clock::now();
            update([&](State& s) {
                s.thinking    = stm;
                s.think_start = start;
                s.info.clear();
                s.book_out = !book_on;
            });
            std::string last_info;
            const auto move = book_move
                                  ? book_move
                                  : player.go(position, go, std::chrono::milliseconds(clock_ms[side]) + CLOCK_MARGIN,
                                              [&](const std::string& line) {
                                                  last_info = line;
                                                  if (!hide) update([&](State& s) { s.info = line; });
                                              });
            const auto spent =
                book_move ? int64_t{0}
                          : std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
            if (stop_) break;
            if (you && resign_) {
                result  = *you == chess::Color::WHITE ? "0-1" : "1-0";
                reason  = trf("{} resigns", *you == chess::Color::WHITE ? white_name : black_name);
                resign_ = false;
                break;
            }
            // A move taken back (#83): the user's last one, and what followed it, the clocks as they were before it.
            if (you && takeback_) {
                takeback_ = false;
                int last  = -1;
                for (int i = static_cast<int>(game.moves.size()) - 1; i >= 0; --i)
                    if (game.moves[static_cast<std::size_t>(i)].mover == *you) {
                        last = i;
                        break;
                    }
                if (last >= 0) {
                    const auto keep = static_cast<std::size_t>(last);
                    clock_ms        = keep < clocks_before.size() ? clocks_before[keep] : clock_ms;
                    game.moves.resize(keep);
                    moves.resize(keep);
                    clocks_before.resize(std::min(clocks_before.size(), keep));
                    board.setFen(setup.fen);
                    for (const auto& m : game.moves) board.makeMove<true>(m.move);
                    ++takebacks;
                    // Back before its openings had no move: they are asked again.
                    while (last_book >= last) last_book -= 2;
                    if (last_book < 0) last_book = -1;
                    if (book_out_at >= 0 && last <= book_out_at) {
                        book_on     = true;
                        book_out_at = -1;
                    }
                }
                update([&](State& s) {
                    s.current       = hide ? withoutEvaluations(game) : game;
                    s.clock_ms      = clock_ms;
                    s.clocks_before = clocks_before;
                    s.takebacks     = takebacks;
                    s.last_book     = last_book;
                    s.book_out      = !book_on;
                    s.thinking.reset();
                });
                continue;
            }
            const char* winner = side == 0 ? "0-1" : "1-0";
            if (!move) {
                result = winner;
                reason = player.alive() ? trf("{} loses on time", name) : trf("{}'s engine stopped", name);
                break;
            }
            if (spent > clock_ms[side] + CLOCK_MARGIN.count()) {
                result = winner;
                reason = trf("{} loses on time", name);
                break;
            }
            const auto parsed = chess::uci::uciToMove(board, *move);
            chess::Movelist legal;
            chess::movegen::legalmoves(legal, board);
            if (parsed == chess::Move::NO_MOVE || std::find(legal.begin(), legal.end(), parsed) == legal.end()) {
                result = winner;
                reason = trf("illegal move {} by {}", *move, name);
                break;
            }
            std::optional<fastchess::Score> score;
            if (!last_info.empty())
                if (const auto info = fastchess::engine::UciEngine::parseInfo(last_info); info.score)
                    score = *info.score;
            // A draw offered (#83): the engine answers with its search, its move otherwise.
            int declined = -1;
            if (engine_up && draw_offer_) {
                draw_offer_ = false;
                if (!book_move && engineAcceptsDraw(score, static_cast<int>(game.moves.size()))) {
                    result = "1/2-1/2";
                    reason = TR("draw by agreement");
                    update([&](State& s) { s.draw_offered = false; });
                    break;
                }
                declined = static_cast<int>(game.moves.size());
            }
            clocks_before.push_back(clock_ms);
            if (!book_move) clock_ms[side] = clock_ms[side] - spent + inc_ms[side];

            ReplayMove played;
            played.move    = parsed;
            played.mover   = stm;
            played.san     = chess::uci::moveToSan(board, parsed);
            played.time_ms = spent;
            if (!last_info.empty()) {
                const auto info = fastchess::engine::UciEngine::parseInfo(last_info);
                if (info.score) played.score = *info.score;
                played.depth = static_cast<int>(info.depth.value_or(0));
                if (info.nodes) played.nodes = *info.nodes;
                if (info.wdl) played.wdl = *info.wdl;
            }
            if (book_move) last_book = static_cast<int>(game.moves.size());
            moves.push_back(chess::uci::moveToUci(parsed, setup.chess960));
            board.makeMove<true>(parsed);
            game.moves.push_back(played);
            update([&](State& s) {
                s.current       = hide ? withoutEvaluations(game) : game;
                s.clock_ms      = clock_ms;
                s.clocks_before = clocks_before;
                s.last_book     = last_book;
                s.book_out      = !book_on;
                if (declined >= 0) {
                    s.draw_offered  = false;
                    s.draw_declined = declined;
                }
                s.thinking.reset();
            });
        }

        game.result        = result;
        game.final_comment = stop_ && reason.empty() ? std::string(TR("stopped")) : reason;
        if (!game.final_comment.empty()) game.headers.emplace_back("Termination", game.final_comment);
        std::string written;
        if (!pgn.empty()) {
            try {
                tour::appendShared(pgn, gamePgn(game, setup.chess960));
            } catch (const std::exception& e) {
                written = trf("Cannot write {}: {}", pgn.string(), e.what());
            }
        }
        update([&](State& s) {
            s.current = game;
            s.finished.push_back(game);
            s.thinking.reset();
            s.clock_ms      = clock_ms;
            s.clocks_before = clocks_before;
            s.takebacks     = takebacks;
            if (!written.empty()) s.error = written;
        });
    }
    one.quit();
    two.quit();
    update([&](State& s) { s.running = false; });
}

void BoardMatch::reap() {
    if (running() || !thread_.joinable()) return;
    thread_.join();
    claim_.set(0);
}

}  // namespace fcg
