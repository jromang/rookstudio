#include "lichess_playing.hpp"

#include <algorithm>
#include <chrono>
#include <string_view>

#include <fmt/include/fmt/format.h>
#include <chess.hpp>
#include <study/import.hpp>
#include <study/store.hpp>

#include "https.hpp"
#include "lichess_links.hpp"

namespace fcg::lichess {

namespace {

int intOf(const nlohmann::json& j, const char* key, int otherwise = 0) {
    const auto it = j.find(key);
    return it != j.end() && it->is_number_integer() ? it->get<int>() : otherwise;
}

// A variant's key, whether Lichess writes it as {"key": ...} or as the key itself.
std::string variantOf(const nlohmann::json& j) {
    const auto it = j.find("variant");
    if (it == j.end()) return {};
    if (it->is_string()) return it->get<std::string>();
    return it->is_object() ? textOf(*it, "key") : std::string();
}

}  // namespace

Men menOf(const chess::Board& board) {
    Men m;
    m.white       = board.sideToMove() == chess::Color::WHITE;
    std::size_t i = 0;
    for (const auto type : {chess::PieceType::PAWN, chess::PieceType::KNIGHT, chess::PieceType::BISHOP,
                            chess::PieceType::ROOK, chess::PieceType::QUEEN, chess::PieceType::KING}) {
        m.bb[i++] = board.pieces(type, chess::Color::WHITE).getBits();
        m.bb[i++] = board.pieces(type, chess::Color::BLACK).getBits();
    }
    return m;
}

std::optional<Men> menOf(std::string_view fen) {
    Men m;
    int rank = 7, file = 0;
    std::size_t at = 0;
    for (; at < fen.size() && fen[at] != ' ' && fen[at] != '['; ++at) {
        const char c = fen[at];
        if (c == '/') {
            if (file != 8 || rank == 0) return std::nullopt;
            --rank;
            file = 0;
        } else if (c >= '1' && c <= '8') {
            file += c - '0';
        } else {
            static constexpr std::string_view LETTERS = "PNBRQKpnbrqk";
            const auto k                              = LETTERS.find(c);
            if (k == std::string_view::npos || file > 7) return std::nullopt;
            const std::size_t type = k % 6, black = k / 6;
            m.bb[type * 2 + black] |= uint64_t{1} << (rank * 8 + file);
            ++file;
        }
        if (file > 8) return std::nullopt;
    }
    if (rank != 0 || file != 8) return std::nullopt;
    while (at < fen.size() && fen[at] != ' ') ++at;
    if (at + 1 >= fen.size()) return std::nullopt;
    m.white = fen[at + 1] != 'b';
    return m;
}

std::optional<PlayingGame> readPlayingGame(const nlohmann::json& j) {
    if (!j.is_object()) return std::nullopt;
    PlayingGame g;
    g.id      = textOf(j, "gameId");
    g.full_id = textOf(j, "fullId");
    if (g.id.empty()) return std::nullopt;
    if (g.full_id.empty()) g.full_id = g.id;
    g.white = textOf(j, "color") != "black";
    g.fen   = textOf(j, "fen");
    if (const auto it = j.find("isMyTurn"); it != j.end() && it->is_boolean()) g.my_turn = it->get<bool>();
    g.speed = textOf(j, "speed");
    if (g.speed.empty()) g.speed = textOf(j, "perf");
    if (const auto it = j.find("rated"); it != j.end() && it->is_boolean()) g.rated = it->get<bool>();
    if (const auto it = j.find("secondsLeft"); it != j.end() && it->is_number_integer())
        g.seconds_left = it->get<int64_t>();
    if (const auto it = j.find("opponent"); it != j.end() && it->is_object()) {
        g.opponent = textOf(*it, "username");
        if (const auto ai = it->find("ai"); g.opponent.empty() && ai != it->end() && ai->is_number_integer())
            g.opponent = fmt::format("AI level {}", ai->get<int>());
        if (const auto r = it->find("rating"); r != it->end() && r->is_number_integer())
            g.opponent_rating = r->get<int>();
    }
    if (const auto it = j.find("compat"); it != j.end() && it->is_object())
        if (const auto b = it->find("board"); b != it->end() && b->is_boolean()) g.board = b->get<bool>();
    g.source  = textOf(j, "source");
    g.variant = variantOf(j);
    if (const auto it = j.find("status"); it != j.end())
        g.status = it->is_object() ? textOf(*it, "name") : it->is_string() ? it->get<std::string>() : std::string();
    g.winner      = textOf(j, "winner");
    g.rating_diff = intOf(j, "ratingDiff");
    g.tournament  = textOf(j, "tournamentId");
    g.swiss       = textOf(j, "swissId");
    return g;
}

bool playableHere(const PlayingGame& game) {
    if (!game.variant.empty() && game.variant != "standard" && game.variant != "chess960" &&
        game.variant != "fromPosition")
        return false;
    if (game.board) return *game.board;
    if (game.speed == "rapid" || game.speed == "classical" || game.speed == "correspondence") return true;
    return game.speed == "blitz" && (game.source == "friend" || game.source == "ai" || game.source == "api");
}

std::optional<bool> Challenge::userWhite() const {
    if (final_colour != "white" && final_colour != "black") {
        if (colour != "white" && colour != "black") return std::nullopt;
        return incoming ? colour == "black" : colour == "white";
    }
    return incoming ? final_colour == "black" : final_colour == "white";
}

std::optional<Challenge> readChallenge(const nlohmann::json& j, const nlohmann::json* compat) {
    if (!j.is_object()) return std::nullopt;
    Challenge c;
    c.id = textOf(j, "id");
    if (c.id.empty()) return std::nullopt;
    c.incoming = textOf(j, "direction") != "out";
    if (const auto it = j.find("challenger"); it != j.end() && it->is_object()) {
        c.challenger             = textOf(*it, "name");
        c.challenger_title       = textOf(*it, "title");
        c.challenger_rating      = intOf(*it, "rating");
        c.challenger_provisional = boolOf(*it, "provisional");
    }
    if (const auto it = j.find("destUser"); it != j.end() && it->is_object()) {
        c.dest        = textOf(*it, "name");
        c.dest_rating = intOf(*it, "rating");
    }
    c.rated = boolOf(j, "rated");
    c.speed = textOf(j, "speed");
    if (auto v = variantOf(j); !v.empty()) c.variant = std::move(v);
    if (const auto it = j.find("timeControl"); it != j.end() && it->is_object()) {
        const auto type = textOf(*it, "type");
        if (type == "clock") {
            const int limit   = intOf(*it, "limit");
            c.clock.minutes   = limit / 60;
            c.clock.increment = intOf(*it, "increment");
            c.clock.days      = 0;
            c.limit_s         = limit;
        } else if (type == "correspondence") {
            c.clock.days = std::max(1, intOf(*it, "daysPerTurn", 1));
        } else {
            c.unlimited = true;
            c.clock     = ChallengeClock{0, 0, 0};
        }
    }
    c.colour       = textOf(j, "color");
    c.final_colour = textOf(j, "finalColor");
    c.fen          = textOf(j, "initialFen");
    if (compat && compat->is_object()) c.board = boolOf(*compat, "board", true);
    if (const auto it = j.find("compat"); it != j.end() && it->is_object()) c.board = boolOf(*it, "board", true);
    c.decline_reason = textOf(j, "declineReason");
    c.decline_key    = textOf(j, "declineReasonKey");
    return c;
}

bool PlayingGame::atStart() const {
    // The men and the side to move; castling and the counters may be written otherwise.
    constexpr std::string_view START = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w";
    return fen.size() >= START.size() && std::string_view(fen).substr(0, START.size()) == START &&
           (fen.size() == START.size() || fen[START.size()] == ' ');
}

int PlayingGame::move() const {
    // The FEN's sixth field.
    std::size_t at = 0;
    for (int field = 0; field < 5; ++field) {
        at = fen.find(' ', at);
        if (at == std::string::npos) return 0;
        ++at;
    }
    int n = 0;
    for (; at < fen.size() && fen[at] >= '0' && fen[at] <= '9' && n < 100000; ++at) n = n * 10 + (fen[at] - '0');
    return n;
}

std::vector<PlayingGame> readPlaying(const nlohmann::json& j) {
    std::vector<PlayingGame> out;
    if (!j.is_object()) return out;
    const auto it = j.find("nowPlaying");
    if (it == j.end() || !it->is_array()) return out;
    for (const auto& g : *it)
        if (auto game = readPlayingGame(g)) out.push_back(std::move(*game));
    return out;
}

std::vector<PlayingGame> endedGames(const std::vector<PlayingGame>& before, const std::vector<PlayingGame>& after) {
    std::vector<PlayingGame> out;
    for (const auto& g : before)
        if (std::none_of(after.begin(), after.end(), [&](const auto& a) { return a.id == g.id; })) out.push_back(g);
    return out;
}

Arrival bringGame(const std::string& id, const std::string& token, study::Database* db,
                  const std::atomic<bool>* cancel) {
    Arrival out;
    auto answer = gamePgn(id, token, cancel);
    if (!answer.value) {
        out.failure       = answer;
        out.failure.asked = Asked::Game;  // the user's game, not a link's page
        return out;
    }
    out.pgn      = std::move(*answer.value);
    out.analysed = out.pgn.find("[%eval ") != std::string::npos;
    study::ImportedGame g;
    if (!study::readImportedGame(out.pgn, g)) {  // not the PGN Lichess sends
        out.failure.net    = NetError::BadAnswer;
        out.failure.detail = "the game's PGN could not be read";
        return out;
    }
    out.plies  = g.plies;
    out.result = g.result;
    if (!db || g.plies < 2) return out;  // an aborted game stays out
    try {
        std::vector<study::ImportedGame> one;
        one.push_back(std::move(g));
        out.added = db->append(std::move(one)).added > 0;
    } catch (const std::exception& e) {
        out.failure = Failure::own(e.what());
    }
    return out;
}

Answer<std::vector<PlayingGame>> playingGames(const std::string& token, const std::atomic<bool>* cancel) {
    return call<std::vector<PlayingGame>>(request("GET", servers().api + "/api/account/playing?nb=50", token),
                                          readPlaying, cancel);
}

std::optional<GameEvent> readGameEvent(std::string_view line) {
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.remove_suffix(1);
    if (line.empty()) return std::nullopt;
    try {
        const auto j    = nlohmann::json::parse(line);
        const auto type = textOf(j, "type");
        if (type == "challenge" || type == "challengeCanceled" || type == "challengeDeclined") {
            const auto it = j.find("challenge");
            if (it == j.end()) return std::nullopt;
            const auto compat = j.find("compat");
            auto challenge    = readChallenge(*it, compat != j.end() ? &*compat : nullptr);
            if (!challenge) return std::nullopt;
            GameEvent e;
            e.kind      = type == "challenge"           ? GameEvent::Kind::Challenge
                          : type == "challengeCanceled" ? GameEvent::Kind::ChallengeCanceled
                                                        : GameEvent::Kind::ChallengeDeclined;
            e.challenge = std::move(*challenge);
            return e;
        }
        if (type != "gameStart" && type != "gameFinish") return std::nullopt;
        const auto it = j.find("game");
        if (it == j.end()) return std::nullopt;
        auto game = readPlayingGame(*it);
        if (!game) return std::nullopt;
        return GameEvent{
            type == "gameStart" ? GameEvent::Kind::Started : GameEvent::Kind::Finished, std::move(*game), {}};
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

EventListener::EventListener(std::string token) : token_(std::move(token)), thread_([this] { run(); }) {}

EventListener::~EventListener() {
    stop_ = true;
    if (thread_.joinable()) thread_.join();
}

std::vector<GameEvent> EventListener::take() {
    std::lock_guard lock(mutex_);
    std::vector<GameEvent> out(events_.begin(), events_.end());
    events_.clear();
    return out;
}

Failure EventListener::failure() const {
    std::lock_guard lock(mutex_);
    return failure_;
}

void EventListener::run() {
    int breaks = 0;  // in a row: the wait before the next try grows with them
    while (!stop_) {
        const auto asked = request("GET", servers().api + "/api/stream/event", token_, "application/x-ndjson");
        std::string pending;
        HttpStream stream;
        stream.on_head = [&](int s, std::string_view) {
            if (s == 200) {
                std::lock_guard lock(mutex_);
                failure_ = {};
                events_.push_back(GameEvent{GameEvent::Kind::Opened, {}, {}});
                connected_ = true;
                breaks     = 0;
            }
            return s == 200;
        };
        stream.on_data = [&](std::string_view data) {
            pending.append(data);
            std::size_t at = 0;
            for (auto end = pending.find('\n', at); end != std::string::npos; end = pending.find('\n', at)) {
                if (auto e = readGameEvent(std::string_view(pending).substr(at, end - at))) {
                    std::lock_guard lock(mutex_);
                    events_.push_back(std::move(*e));
                }
                at = end + 1;
            }
            pending.erase(0, at);
            return !stop_;
        };
        stream.cancel    = &stop_;
        stream.idle_s    = 30;  // Lichess sends an empty line every 7 s
        const auto broke = responseOf(asked, politeStream(asked, stream));
        connected_       = false;
        if (stop_) break;
        {
            std::lock_guard lock(mutex_);
            failure_ = broke;  // none when Lichess closed it
        }
        if (broke.net == NetError::Unauthorized || broke.net == NetError::Forbidden) {
            refused_ = true;
            break;
        }
        // Opened again after 5 s, then 15 s, 30 s, and a minute from the fourth break in a row on, the stop heard
        // meanwhile: a cut comes back soon, a stream refused again and again waits.
        static constexpr int WAIT_S[] = {5, 15, 30, 60};
        const int wait_s              = WAIT_S[std::min(breaks, 3)];
        ++breaks;
        for (int i = 0; i < wait_s * 4 && !stop_; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
}

}  // namespace fcg::lichess
