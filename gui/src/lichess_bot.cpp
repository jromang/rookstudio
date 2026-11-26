#include "lichess_bot.hpp"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <fstream>
#include <memory>
#include <random>

#include <fmt/include/fmt/format.h>
#include <engine/uci_engine.hpp>
#include <tour/polyglot.hpp>

#include "board_game.hpp"
#include "engine_play.hpp"
#include "https.hpp"
#include "i18n.hpp"
#include "storage.hpp"

namespace fcg::lichess {

namespace fs = std::filesystem;

namespace {

using Clock = std::chrono::steady_clock;

int64_t intOf(const nlohmann::json& j, const char* key, int64_t otherwise = 0) {
    const auto it = j.find(key);
    return it != j.end() && it->is_number_integer() ? it->get<int64_t>() : otherwise;
}

std::string lower(std::string text) {
    for (auto& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

void sleepFor(std::chrono::milliseconds wait, const std::atomic<bool>& stop) {
    const auto until = Clock::now() + wait;
    while (!stop && Clock::now() < until) std::this_thread::sleep_for(std::chrono::milliseconds(10));
}

// The side to move in a game's state: the side of the first move, then each move.
bool whiteToMove(const BoardState& s) {
    const auto& fen    = s.initial_fen;
    const auto space   = fen.find(' ');
    const bool first_w = space == std::string::npos || space + 1 >= fen.size() || fen[space + 1] != 'b';
    return first_w == (s.moves.size() % 2 == 0);
}

std::string dayOf(int64_t seconds) {
    const auto t = static_cast<std::time_t>(seconds);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &t);
#else
    localtime_r(&t, &local);
#endif
    char text[16];
    std::strftime(text, sizeof(text), "%Y-%m-%d", &local);
    return text;
}

}  // namespace

// ---------------------------------------------------------------------------
// The account

std::optional<BotAccountFacts> readBotAccount(const nlohmann::json& j) {
    if (!j.is_object()) return std::nullopt;
    BotAccountFacts f;
    f.id       = textOf(j, "id");
    f.username = textOf(j, "username");
    if (f.username.empty()) return std::nullopt;
    if (f.id.empty()) f.id = lower(f.username);
    f.title      = textOf(j, "title");
    f.created_ms = intOf(j, "createdAt");
    if (const auto it = j.find("count"); it != j.end() && it->is_object()) f.games = intOf(*it, "all");
    f.disabled      = boolOf(j, "disabled");
    f.tos_violation = boolOf(j, "tosViolation");
    return f;
}

Answer<BotAccountFacts> botAccount(const std::string& token, const std::atomic<bool>* cancel) {
    return call<BotAccountFacts>(request("GET", servers().api + "/api/account", token), readBotAccount, cancel);
}

BotTokenCheck checkBotToken(const std::string& token, const std::atomic<bool>* cancel) {
    BotTokenCheck c;
    const auto tested = testToken(token, cancel);
    static_cast<Failure&>(c) = tested;
    if (!tested.value) {
        if (!c.failed()) c.net = NetError::Unauthorized;  // answered, but no longer a token (revoked, or ended)
        return c;
    }
    auto facts               = botAccount(token, cancel);
    static_cast<Failure&>(c) = facts;
    if (!facts.value) return c;
    Account a;
    a.username = facts.value->username;
    a.token    = token;
    a.scopes   = tested.value->scopes;
    a.expires  = tested.value->expires;
    c.account  = std::move(a);
    c.facts    = std::move(facts.value);
    return c;
}

BotVerdict botVerdict(const BotAccountFacts& facts, std::string_view own_account, bool may_play) {
    if (!own_account.empty() &&
        (lower(facts.username) == lower(std::string(own_account)) || facts.id == lower(std::string(own_account))))
        return BotVerdict::OwnAccount;
    if (facts.disabled || facts.tos_violation) return BotVerdict::Closed;
    if (!may_play) return BotVerdict::NoScope;
    if (facts.bot()) return BotVerdict::Ready;
    if (facts.games > 0) return BotVerdict::Played;
    return BotVerdict::Upgradable;
}

Reply upgradeToBot(const std::string& token, const std::atomic<bool>* cancel) {
    Reply out;
    static_cast<Failure&>(out) = send(request("POST", servers().api + "/api/bot/account/upgrade", token), cancel);
    out.ok                     = !out.failed();
    return out;
}

// ---------------------------------------------------------------------------
// The challenges

int speedRank(std::string_view speed) {
    static constexpr std::string_view SPEEDS[] = {"ultraBullet", "bullet",    "blitz",
                                                  "rapid",       "classical", "correspondence"};
    for (int i = 0; i < 6; ++i)
        if (SPEEDS[i] == speed) return i;
    return -1;
}

std::string declineReason(const Challenge& c, const BotRules& rules, const BotSituation& now) {
    // Its variant: standard chess (or from a position), Chess960 when the engine knows it and the rules take it.
    const bool standard = c.variant.empty() || c.variant == "standard" || c.variant == "fromPosition";
    const bool chess960 = c.variant == "chess960" && rules.chess960 && now.engine_chess960;
    if (!standard && !chess960) return rules.chess960 && now.engine_chess960 ? "variant" : "standard";
    // Its speed: too fast, too slow, or one between those taken; a game without a clock is not for an engine.
    if (c.unlimited) return "timeControl";
    const auto speed    = c.speed.empty() ? speedOf(c.clock) : c.speed;
    const int rank      = speedRank(speed);
    const bool taken[6] = {false, rules.bullet, rules.blitz, rules.rapid, rules.classical, rules.correspondence};
    if (rank < 0 || !taken[rank]) {
        int fastest = -1, slowest = -1;
        for (int i = 0; i < 6; ++i)
            if (taken[i]) {
                if (fastest < 0) fastest = i;
                slowest = i;
            }
        if (fastest < 0) return "timeControl";
        if (rank >= 0 && rank < fastest) return "tooFast";
        if (rank > slowest) return "tooSlow";
        return "timeControl";
    }
    // Rated: casual only unless the rules open rated games; never rated against the user's own account (Lichess
    // forbids rated games between one person's accounts).
    const bool own = !now.own_account.empty() && lower(c.challenger) == lower(now.own_account);
    if (c.rated && (!rules.rated || own)) return "casual";
    // Who sends it: a person or a bot, within the ratings chosen.
    const bool bot = c.challenger_title == "BOT";
    if (bot && !rules.bots) return "noBot";
    if (!bot && !rules.humans) return "onlyBot";
    if ((rules.rating_min > 0 && c.challenger_rating < rules.rating_min) ||
        (rules.rating_max > 0 && c.challenger_rating > rules.rating_max))
        return "generic";
    // Then whether it can play now: one game at a time, not while it goes offline, the tournaments first.
    if (now.busy || now.closing || now.cores_needed) return "later";
    return {};
}

// ---------------------------------------------------------------------------
// The clocks

int64_t clockMargin(int64_t clock_ms) { return std::clamp<int64_t>(clock_ms / 10, 200, 1000); }

std::string botGo(const BoardState& s, bool bot_white, Clock::time_point now) {
    if (!s.live()) return "go movetime 15000";
    if (s.clock_ms < 0) return "go movetime 5000";  // no clock at all
    // Before both sides moved, the clocks wait; Lichess gives the first move a time of its own.
    if (s.moves.size() < 2) {
        int64_t left = 30000;
        if (s.first_move_ms >= 0)
            left = s.first_move_ms - std::chrono::duration_cast<std::chrono::milliseconds>(now - s.told).count();
        return fmt::format("go movetime {}", std::clamp<int64_t>(left / 4, 100, 2000));
    }
    const int64_t mine   = s.clockNow(bot_white, now);
    const int64_t theirs = s.clockNow(!bot_white, now);
    const int64_t given  = std::max<int64_t>(50, mine - clockMargin(mine));
    const int64_t inc    = std::max<int64_t>(0, s.increment_ms);
    return fmt::format("go wtime {} btime {} winc {} binc {}", bot_white ? given : std::max<int64_t>(1, theirs),
                       bot_white ? std::max<int64_t>(1, theirs) : given, inc, inc);
}

std::chrono::milliseconds botBudget(const BoardState& s, bool bot_white, Clock::time_point now) {
    using std::chrono::milliseconds;
    if (!s.live()) return milliseconds(15000 + 3000);
    if (s.clock_ms < 0) return milliseconds(5000 + 3000);
    if (s.moves.size() < 2) {
        int64_t left = 30000;
        if (s.first_move_ms >= 0)
            left = s.first_move_ms - std::chrono::duration_cast<milliseconds>(now - s.told).count();
        return milliseconds(std::clamp<int64_t>(left / 4, 100, 2000) + 1000);
    }
    const int64_t mine = s.clockNow(bot_white, now);
    return milliseconds(std::max<int64_t>(100, mine - clockMargin(mine) / 2));
}

// ---------------------------------------------------------------------------
// A game

BotGame::BotGame(std::string token, std::string id, std::string bot, bool white, BotPlay play)
    : token_(std::move(token)), id_(std::move(id)), bot_(lower(std::move(bot))), play_(std::move(play)) {
    threads_       = play_.engine.threads();
    state_.white   = white;
    state_.game.id = id_;
    thread_        = std::thread([this] { run(); });
}

BotGame::~BotGame() {
    stop_ = true;
    if (thread_.joinable()) thread_.join();
}

bool BotGame::state(State& out, uint64_t& version) const {
    std::lock_guard lock(mutex_);
    if (state_.version == version) return false;
    out     = state_;
    version = state_.version;
    return true;
}

template <typename F>
void BotGame::update(F f) {
    std::lock_guard lock(mutex_);
    f(state_);
    ++state_.version;
}

void BotGame::run() {
    GameStream stream(token_, id_, PlayApi::Bot);
    // The book while it has a move.
    std::unique_ptr<OpeningBook> book;
    if (!play_.book.empty()) {
        try {
            auto polyglot = std::make_shared<tour::PolyglotBook>(fs::path(play_.book));
            book          = std::make_unique<OpeningBook>(
                [polyglot](const chess::Board& board) {
                    std::vector<OpeningChoice> out;
                    for (const auto& c : polyglot->moves(board)) out.push_back({c.move, c.weight, false});
                    return out;
                },
                true, 1, std::random_device{}());
        } catch (const std::exception& e) {
            update([&](State& st) { st.error = trf("The book cannot be read: {}", e.what()); });
        }
    }
    BoardState s;
    uint64_t version = 0;
    bool white = state_.white, sided = false;
    bool greeted = false, thanked = false, claimed = false;
    std::size_t moved_at = SIZE_MAX;  // the ply the bot's move was taken at
    int draw_answered = -1, takeback_answered = -1;
    int claims    = 0;
    auto claim_at = Clock::now();  // a claim refused: tried again then
    auto move_at  = Clock::now();  // a move refused, or none found in time: thought again then
    // The game ended, as its stream may say it while the engine searches.
    const auto over = [&] {
        BoardState peek;
        uint64_t v = version;
        if (stream.state(peek, v)) return peek.over() || peek.ended;
        return false;
    };
    const auto say = [&](const std::string& text) {
        if (!text.empty()) (void)boardChat(token_, id_, text, &stop_, PlayApi::Bot);
    };
    // The engine cannot play: the game resigned, or aborted before both sides moved; said.
    const auto giveUp = [&](const std::string& why) {
        update([&](State& st) { st.error = why; });
        (void)boardAction(token_, id_, s.moves.size() < 2 ? "abort" : "resign", &stop_, PlayApi::Bot);
    };
    std::unique_ptr<EnginePlayer> engine;  // started at its first move
    bool engine_new = true;
    while (!stop_) {
        if (stream.state(s, version)) {
            if (s.full && !sided) {
                if (lower(s.white.id) == bot_ || lower(s.white.name) == bot_) white = true;
                if (lower(s.black.id) == bot_ || lower(s.black.name) == bot_) white = false;
                sided = true;
            }
            update([&](State& st) {
                st.game  = s;
                st.white = white;
            });
        }
        if (s.over() || s.ended) {
            // Its words at the end, once: not for a game aborted, nor one Lichess does not know.
            if (s.over() && !thanked && s.status != "aborted" && s.status != "noStart") {
                thanked = true;
                say(play_.thanks);
            }
            break;
        }
        if (resign_) {
            resign_ = false;
            (void)boardAction(token_, id_, s.moves.size() < 2 ? "abort" : "resign", &stop_, PlayApi::Bot);
        }
        if (!s.full) {
            sleepFor(std::chrono::milliseconds(20), stop_);
            continue;
        }
        // Its words at the start, unless it said them already (the game resumed).
        if (!greeted) {
            greeted = true;
            const bool again =
                std::any_of(s.chat.begin(), s.chat.end(), [&](const ChatLine& l) { return lower(l.user) == bot_; });
            if (!again && s.moves.size() < 2) say(play_.greeting);
        }
        const auto ply = static_cast<int>(s.moves.size());
        // A takeback asked by the opponent: declined, once a ply.
        if ((white ? s.black_takeback : s.white_takeback) && takeback_answered != ply) {
            takeback_answered = ply;
            (void)boardAction(token_, id_, "takeback/no", &stop_, PlayApi::Bot);
        }
        // The opponent gone: the victory claimed once Lichess allows it.
        if (s.opponent_gone && s.claim_in_s >= 0 && !claimed && claims < 3 && Clock::now() >= claim_at) {
            const auto since = std::chrono::duration_cast<std::chrono::seconds>(Clock::now() - s.gone_told).count();
            if (since >= s.claim_in_s + 1) {
                ++claims;
                const auto r = boardAction(token_, id_, "claim-victory", &stop_, PlayApi::Bot);
                claimed      = r.ok;
                claim_at     = Clock::now() + std::chrono::seconds(5);
            }
        }
        const bool turn = whiteToMove(s) == white;
        if (!turn || moved_at == s.moves.size() || !s.connected || Clock::now() < move_at) {
            sleepFor(std::chrono::milliseconds(20), stop_);
            continue;
        }
        // The bot's turn: the position.
        const bool chess960 = s.variant == "chess960";
        const auto start    = s.initial_fen.empty() ? std::string(chess::constants::STARTPOS) : s.initial_fen;
        chess::Board board(start, chess960);
        chess::Board for_book(start, chess960);
        std::string moves;
        bool legal = true;
        for (const auto& uci : s.moves) {
            const auto m = legalUci(board, uci);
            if (!m) {
                legal = false;
                break;
            }
            moves += " " + chess::uci::moveToUci(*m, chess960);
            board.makeMove<true>(*m);
            tour::playForBook(for_book, *m);
        }
        if (!legal) {  // a move this board cannot play: waited for the stream to say more
            update([&](State& st) { st.error = TR("A move of the game cannot be played on this board."); });
            move_at = Clock::now() + std::chrono::seconds(1);
            continue;
        }
        bool draw_offered = white ? s.black_draw : s.white_draw;
        std::optional<std::string> move;
        bool from_book = false;
        if (book) {
            move = book->move(for_book);
            if (move)
                from_book = true;
            else
                book.reset();  // out of it: the engine from here
        }
        std::string last_info;
        if (!move) {
            if (!engine || !engine->alive()) {
                std::vector<std::pair<std::string, std::string>> more{{"Ponder", "false"}};
                if (!play_.syzygy.empty()) more.emplace_back("SyzygyPath", play_.syzygy);
                engine = std::make_unique<EnginePlayer>(play_.engine, stop_, [&] { return over(); });
                std::string error;
                if (!engine->start(chess960, error, more)) {
                    if (!stop_) giveUp(error);
                    break;
                }
                engine_new = true;
            }
            if (engine_new) {
                engine_new = false;
                if (!engine->newGame()) {
                    if (!stop_) giveUp(trf("{} does not answer isready.", play_.engine.name));
                    break;
                }
            }
            const auto now = Clock::now();
            update([&](State& st) {
                st.thinking    = true;
                st.think_start = now;
                st.info.clear();
                st.info_ply = ply;
            });
            const auto position =
                (s.initial_fen.empty() ? std::string("position startpos") : "position fen " + s.initial_fen) +
                (moves.empty() ? std::string() : " moves" + moves);
            move = engine->go(
                position, botGo(s, white, now), botBudget(s, white, now),
                [&](const std::string& line) {
                    last_info = line;
                    update([&](State& st) { st.info = line; });
                },
                true);
            update([&](State& st) { st.thinking = false; });
            if (stop_) break;
            if (!move) {
                if (over()) continue;  // the game ended meanwhile
                if (!engine->alive()) {
                    giveUp(trf("{}'s engine stopped", play_.engine.name));
                    break;
                }
                // No move in time: the next search tries again, the clock permitting.
                move_at = Clock::now() + std::chrono::milliseconds(100);
                continue;
            }
        }
        if (!legalUci(board, *move)) {
            giveUp(trf("illegal move {} by {}", *move, play_.engine.name));
            break;
        }
        // An offer made while the engine searched counts too.
        {
            BoardState now;
            uint64_t v = version;
            if (stream.state(now, v)) {
                if (now.over()) continue;
                draw_offered = draw_offered || (white ? now.black_draw : now.white_draw);
            }
        }
        // The opponent's draw offer: taken when the engine stands clearly worse, or level late in the game; else
        // declined, once.
        if (draw_offered && draw_answered != ply) {
            draw_answered = ply;
            std::optional<fastchess::Score> score;
            if (!last_info.empty())
                if (const auto info = fastchess::engine::UciEngine::parseInfo(last_info); info.score)
                    score = *info.score;
            if (!from_book && engineAcceptsDraw(score, ply)) {
                if (boardAction(token_, id_, "draw/yes", &stop_, PlayApi::Bot).ok) {
                    moved_at = s.moves.size();
                    continue;
                }
            } else {
                (void)boardAction(token_, id_, "draw/no", &stop_, PlayApi::Bot);
            }
        }
        update([&](State& st) {
            st.from_book = from_book;
            if (from_book) ++st.book_moves;
        });
        // Sent; after a cut, again while it is still the bot's turn at this ply.
        for (int tries = 0; tries < 20 && !stop_; ++tries) {
            const auto r = boardMove(token_, id_, *move, false, &stop_, PlayApi::Bot);
            if (r.ok) {
                moved_at = s.moves.size();
                update([&](State& st) { st.error.clear(); });
                break;
            }
            if (r.net != NetError::Offline && r.net != NetError::Cancelled) {
                // Refused: the stream says why (the game over, a takeback); tried again later.
                update([&](State& st) { st.error = trf("Lichess refused the move {}: {}", *move, describe(r)); });
                move_at = Clock::now() + std::chrono::seconds(1);
                break;
            }
            sleepFor(std::chrono::milliseconds(300), stop_);
            BoardState now;
            uint64_t v = version;
            if (stream.state(now, v) && (now.over() || now.moves.size() != s.moves.size())) break;
        }
    }
    if (engine) engine->quit();
    update([&](State& st) {
        st.thinking = false;
        st.done     = true;
    });
}

// ---------------------------------------------------------------------------
// The profile

std::optional<int> botScore(const BotRecord& game) {
    if (game.result == "1-0") return game.white ? 1 : -1;
    if (game.result == "0-1") return game.white ? -1 : 1;
    if (game.result == "1/2-1/2") return 0;
    return std::nullopt;
}

BotSettings readBotSettings(const fs::path& folder) {
    std::ifstream in(folder / "bot.json", std::ios::binary);
    if (!in) return {};
    const auto j = nlohmann::json::parse(in, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return {};
    try {
        return j.get<BotSettings>();
    } catch (const std::exception&) {
        return {};
    }
}

bool writeBotSettings(const fs::path& folder, const BotSettings& settings) {
    std::error_code ec;
    fs::create_directories(folder, ec);
    return writeJson(folder / "bot.json", nlohmann::json(settings));
}

void addBotRecord(BotSettings& settings, BotRecord record, const std::string& today) {
    if (settings.day != today) {
        settings.day      = today;
        settings.declined = 0;
    }
    std::erase_if(settings.games, [&](const BotRecord& g) { return g.id == record.id; });
    settings.games.insert(settings.games.begin(), std::move(record));
    if (settings.games.size() > 20) settings.games.resize(20);
}

void countDeclined(BotSettings& settings, const std::string& today) {
    if (settings.day != today) {
        settings.day      = today;
        settings.declined = 0;
    }
    ++settings.declined;
}

BotDay botDay(const BotSettings& settings, const std::string& today) {
    BotDay d;
    for (const auto& g : settings.games) {
        if (dayOf(g.at) != today) continue;
        const auto score = botScore(g);
        if (!score) continue;
        ++d.games;
        if (*score > 0) ++d.won;
        if (*score == 0) ++d.drawn;
        if (*score < 0) ++d.lost;
    }
    return d;
}

}  // namespace fcg::lichess
