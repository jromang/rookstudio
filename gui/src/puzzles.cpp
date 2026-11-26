#include "puzzles.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <utility>

#include <json.hpp>

#include <tour/files.hpp>

#include "i18n.hpp"
#include "storage.hpp"

namespace fcg {

namespace fs = std::filesystem;

namespace {

std::optional<nlohmann::json> readJsonFile(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return std::nullopt;
    auto j = nlohmann::json::parse(in, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return std::nullopt;
    return j;
}

// The database's text given to the builder as it comes, a line at a time.
class StoreConsumer final : public ZstdConsumer {
   public:
    StoreConsumer(fs::path folder, OfflinePuzzles::Progress& progress)
        : folder_(std::move(folder)), builder_(folder_ / "build"), progress_(progress) {}

    bool text(std::string_view piece) override {
        for (std::size_t from = 0;;) {
            const auto end = piece.find('\n', from);
            if (end == std::string_view::npos) {
                pending_.append(piece.substr(from));
                break;
            }
            if (pending_.empty()) {
                builder_.line(piece.substr(from, end - from));
            } else {
                pending_.append(piece.substr(from, end - from));
                builder_.line(pending_);
                pending_.clear();
            }
            from = end + 1;
        }
        counts();
        return builder_.error().empty();
    }
    nlohmann::json mark() override {
        auto j    = builder_.mark();
        j["text"] = pending_;  // a line begun in the frame, ended in the next
        return j;
    }
    void commit() override { builder_.commit(); }
    void restore(const nlohmann::json& state) override {
        builder_.restore(state);
        pending_ = state.value("text", "");
        counts();
    }
    void startAgain() override {
        builder_.startAgain();
        pending_.clear();
        counts();
    }
    std::string finish(const std::string& date) override {
        if (!pending_.empty()) builder_.line(pending_);
        pending_.clear();
        counts();
        if (!builder_.error().empty()) return builder_.error();
        progress_.writing = true;
        auto why = builder_.finish(study::nextPuzzleStore(folder_), date, &progress_.written, &progress_.total);
        progress_.writing = false;
        return why;
    }
    [[nodiscard]] std::string error() const override { return builder_.error(); }

   private:
    void counts() {
        progress_.lines = builder_.lines();
        progress_.kept  = builder_.kept();
    }

    fs::path folder_;
    study::PuzzleBuilder builder_;
    OfflinePuzzles::Progress& progress_;
    std::string pending_;
};

nlohmann::json puzzleJson(const study::Puzzle& p) {
    return {{"id", p.id},         {"fen", p.fen},           {"moves", p.moves},
            {"rating", p.rating}, {"rd", p.deviation},      {"popularity", p.popularity},
            {"themes", p.themes}, {"openings", p.openings}, {"game", p.game}};
}

study::Puzzle puzzleOf(const nlohmann::json& j) {
    study::Puzzle p;
    p.id         = j.value("id", "");
    p.fen        = j.value("fen", "");
    p.rating     = j.value("rating", 1500);
    p.deviation  = j.value("rd", 75);
    p.popularity = j.value("popularity", 0);
    p.game       = j.value("game", "");
    if (const auto it = j.find("moves"); it != j.end() && it->is_array())
        for (const auto& m : *it)
            if (m.is_string()) p.moves.push_back(m.get<std::string>());
    if (const auto it = j.find("themes"); it != j.end() && it->is_array())
        for (const auto& t : *it)
            if (t.is_string()) p.themes.push_back(t.get<std::string>());
    if (const auto it = j.find("openings"); it != j.end() && it->is_array())
        for (const auto& o : *it)
            if (o.is_string()) p.openings.push_back(o.get<std::string>());
    return p;
}

}  // namespace

// ---------------------------------------------------------------------------
// The store

OfflinePuzzles::OfflinePuzzles(JobGroup& jobs, fs::path folder) : folder_(std::move(folder)), job_(jobs) {
    study::tidyPuzzleFolder(folder_);  // a store being written when the program stopped, older ones
    reload();
}

std::shared_ptr<const study::PuzzleStore> OfflinePuzzles::store() const {
    std::lock_guard lock(mutex_);
    return store_;
}

OfflinePuzzles::State OfflinePuzzles::state() const {
    std::lock_guard lock(mutex_);
    return state_;
}

void OfflinePuzzles::reload() {
    State st;
    std::shared_ptr<const study::PuzzleStore> store;
    if (const auto newest = study::newestPuzzleStore(folder_); !newest.empty()) {
        try {
            store = std::make_shared<const study::PuzzleStore>(newest);
        } catch (const std::exception&) {
            store.reset();  // not a store (a disk's fault): as if none
        }
    }
    if (store) {
        st.ready   = true;
        st.puzzles = store->size();
        st.bytes   = store->bytes();
        st.date    = store->date();
    }
    std::error_code ec;
    if (const auto j = readJsonFile(build() / "reading.json")) {
        st.stopped       = true;
        st.stopped_at    = j->value("offset", uint64_t{0});
        st.size          = j->value("size", uint64_t{0});
        st.source        = j->value("source", "");
        const auto state = j->value("state", nlohmann::json::object());
        st.kept          = state.value("kept", uint64_t{0});
    } else if (!busy() && fs::exists(build(), ec)) {
        fs::remove_all(build(), ec);  // a preparation stopped before its first place was kept: nothing to go on from
    }
    std::lock_guard lock(mutex_);
    state_ = std::move(st);
    store_ = std::move(store);
    ++generation_;
}

void OfflinePuzzles::start(std::string source) {
    if (job_.ready() && remove_when_ended_) (void)finished();  // the removal asked for during the last one, first
    if (busy()) return;
    if (const auto st = state(); st.stopped && st.source != source) forgetStopped();
    ZstdReadingOptions o;
    o.checkpoint_every = checkpoint_every_;
    o.retry_wait       = retry_wait_;
    o.on_saved         = on_saved_;
    // The task holds what it reads and what it tells: it may outlive this object, parked.
    job_.start([source = std::move(source), folder = folder_, o](TaskState<Progress>& job) -> std::string {
        try {
            const auto build = folder / "build";
            std::error_code ec;
            fs::create_directories(build, ec);
            StoreConsumer consumer(folder, job);
            auto why = readZstd(source, build / "reading.json", consumer, job, job.cancel, o);
            if (why.empty()) fs::remove_all(build, ec);  // the store made: its files and its state are gone
            return why;
        } catch (const std::exception& e) {
            const std::string why = e.what();
            return why.empty() ? std::string("stopped") : why;
        }
    });
}

std::optional<std::string> OfflinePuzzles::finished() {
    if (!job_.ready()) return std::nullopt;
    auto out = job_.take();
    if (std::exchange(remove_when_ended_, false)) {
        removeNow();
        return std::nullopt;  // cancelled by the removal
    }
    {
        std::lock_guard lock(mutex_);
        if (out.empty()) store_.reset();  // the store it replaced, closed before it is removed
    }
    if (out.empty()) study::tidyPuzzleFolder(folder_);
    reload();
    return out;
}

void OfflinePuzzles::remove() {
    {
        std::lock_guard lock(mutex_);
        store_.reset();
        ++generation_;
    }
    if (busy()) {
        job_.cancel();
        remove_when_ended_ = true;
        std::lock_guard lock(mutex_);
        state_ = {};
        return;
    }
    job_.drop();  // a result not taken: forgotten
    remove_when_ended_ = false;
    removeNow();
}

void OfflinePuzzles::removeNow() {
    std::error_code ec;
    fs::remove_all(folder_, ec);
    reload();
}

void OfflinePuzzles::forgetStopped() {
    if (busy()) return;
    std::error_code ec;
    fs::remove_all(build(), ec);
    reload();
}

// ---------------------------------------------------------------------------
// The user's puzzles

PuzzleRating startingRating(std::optional<int> lichess_rating, std::optional<int> lichess_deviation) {
    PuzzleRating out;
    if (lichess_rating && *lichess_rating > 0) {
        out.glicko.rating    = *lichess_rating;
        out.glicko.deviation = std::clamp(lichess_deviation && *lichess_deviation > 0 ? *lichess_deviation : 100,
                                          static_cast<int>(study::glicko2::MIN_DEVIATION),
                                          static_cast<int>(study::glicko2::MAX_DEVIATION));
        out.from             = "lichess";
    }
    return out;
}

PuzzleRating ratedAfter(const PuzzleRating& before, int puzzle_rating, int puzzle_deviation, bool win, int64_t now) {
    PuzzleRating out = before;
    auto glicko      = before.glicko;
    if (before.last > 0 && now > before.last)
        glicko = study::glicko2::idle(
            glicko, static_cast<double>(now - before.last) / 86400.0 * study::glicko2::PERIODS_PER_DAY);
    const study::glicko2::Game game{static_cast<double>(puzzle_rating),
                                    static_cast<double>(std::max(puzzle_deviation, 45)), win ? 1.0 : 0.0};
    out.glicko = study::glicko2::rate(glicko, std::span(&game, 1));
    ++out.games;
    out.last = now;
    return out;
}

void PuzzleRecord::add(const PuzzleResult& result) {
    results.push_back(result);
    seen.insert(result.id);
    if (result.review) return;  // asked again: its first answer counted already
    for (const auto& t : result.themes) {
        auto& r = themes[t];
        ++r.tried;
        r.won += result.win ? 1 : 0;
    }
}

PuzzleRecord readPuzzleRecord(const fs::path& folder) {
    PuzzleRecord out;
    if (const auto j = readJsonFile(folder / "rating.json")) {
        PuzzleRating r;
        r.glicko.rating     = j->value("rating", 1500.0);
        r.glicko.deviation  = j->value("deviation", 350.0);
        r.glicko.volatility = j->value("volatility", 0.06);
        r.games             = j->value("games", 0);
        r.from              = j->value("from", "default");
        r.last              = j->value("last", int64_t{0});
        out.rating          = r;
    }
    {
        std::ifstream in(folder / "results.jsonl", std::ios::binary);
        std::string line;
        while (std::getline(in, line)) {
            const auto j = nlohmann::json::parse(line, nullptr, false);
            if (j.is_discarded() || !j.is_object()) continue;
            PuzzleResult r;
            r.id     = j.value("id", "");
            r.at     = j.value("at", int64_t{0});
            r.win    = j.value("win", false);
            r.rating = j.value("rating", 0);
            r.before = j.value("before", 0.0);
            r.after  = j.value("after", 0.0);
            r.ms     = j.value("ms", int64_t{0});
            r.online = j.value("online", false);
            r.review = j.value("review", false);
            if (const auto it = j.find("themes"); it != j.end() && it->is_array())
                for (const auto& t : *it)
                    if (t.is_string()) r.themes.push_back(t.get<std::string>());
            if (!r.id.empty()) out.add(r);
        }
    }
    if (const auto j = readJsonFile(folder / "review.json")) {
        if (const auto cards = j->find("cards"); cards != j->end() && cards->is_object()) {
            for (const auto& [id, c] : cards->items()) {
                if (!c.is_object()) continue;
                PuzzleCard card;
                card.puzzle = puzzleOf(c.value("puzzle", nlohmann::json::object()));
                if (card.puzzle.id.empty()) card.puzzle.id = id;
                card.since           = c.value("since", int64_t{0});
                const int state      = c.value("state", 1);
                card.card.state      = state == 2   ? study::fsrs::State::Review
                                       : state == 3 ? study::fsrs::State::Relearning
                                                    : study::fsrs::State::Learning;
                card.card.step       = c.value("step", 0);
                card.card.stability  = c.value("stability", 0.0);
                card.card.difficulty = c.value("difficulty", 0.0);
                card.card.due        = c.value("due", int64_t{0});
                card.card.last       = c.value("last", int64_t{0});
                card.card.reps       = c.value("reps", 0);
                card.card.lapses     = c.value("lapses", 0);
                if (card.puzzle.moves.size() >= 2) out.cards[id] = std::move(card);
            }
        }
    }
    return out;
}

void appendPuzzleResult(const fs::path& folder, const PuzzleResult& r) {
    std::error_code ec;
    fs::create_directories(folder, ec);
    const nlohmann::json line = {{"id", r.id},         {"at", r.at},        {"win", r.win},       {"rating", r.rating},
                                 {"before", r.before}, {"after", r.after},  {"themes", r.themes}, {"ms", r.ms},
                                 {"online", r.online}, {"review", r.review}};
    std::ofstream out(folder / "results.jsonl", std::ios::binary | std::ios::app);
    out << line.dump() << '\n';
}

bool writePuzzleRating(const fs::path& folder, const PuzzleRating& r) {
    std::error_code ec;
    fs::create_directories(folder, ec);
    return writeJson(folder / "rating.json", {{"rating", r.glicko.rating},
                                              {"deviation", r.glicko.deviation},
                                              {"volatility", r.glicko.volatility},
                                              {"games", r.games},
                                              {"from", r.from},
                                              {"last", r.last}});
}

bool writePuzzleCards(const fs::path& folder, const std::map<std::string, PuzzleCard>& cards) {
    std::error_code ec;
    fs::create_directories(folder, ec);
    nlohmann::json out = nlohmann::json::object();
    for (const auto& [id, c] : cards) {
        out[id] = {{"puzzle", puzzleJson(c.puzzle)},
                   {"since", c.since},
                   {"state", static_cast<int>(c.card.state)},
                   {"step", c.card.step},
                   {"stability", c.card.stability},
                   {"difficulty", c.card.difficulty},
                   {"due", c.card.due},
                   {"last", c.card.last},
                   {"reps", c.card.reps},
                   {"lapses", c.card.lapses}};
    }
    return writeJson(folder / "review.json", {{"version", 1}, {"cards", std::move(out)}});
}

PuzzleCard missedCard(const study::Puzzle& puzzle, int64_t now, int64_t day_end,
                      const study::fsrs::Scheduler& scheduler) {
    PuzzleCard out;
    out.puzzle   = puzzle;
    out.since    = now;
    out.card     = scheduler.review(study::fsrs::Card{}, study::fsrs::Rating::Again, now);
    out.card.due = std::max(out.card.due, day_end + 1);  // not again in the series: the next training day
    return out;
}

std::size_t cardsDue(const std::map<std::string, PuzzleCard>& cards, int64_t limit) {
    return static_cast<std::size_t>(
        std::count_if(cards.begin(), cards.end(), [&](const auto& c) { return c.second.card.due <= limit; }));
}

// ---------------------------------------------------------------------------
// Solving

std::optional<PuzzlePlay> puzzlePlay(const study::Puzzle& puzzle) {
    if (puzzle.moves.size() < 2) return std::nullopt;
    try {
        PuzzlePlay out;
        out.before.setFen(puzzle.fen);
        auto board = out.before;
        for (std::size_t i = 0; i < puzzle.moves.size(); ++i) {
            const auto m = chess::uci::uciToMove(board, puzzle.moves[i]);
            chess::Movelist legal;
            chess::movegen::legalmoves(legal, board);
            if (m == chess::Move::NO_MOVE || std::find(legal.begin(), legal.end(), m) == legal.end())
                return std::nullopt;
            (i == 0 ? out.opponent : out.solution.emplace_back()) = m;
            board.makeMove<true>(m);
        }
        return out;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

PuzzleAnswer judgeMove(const PuzzlePlay& play, std::size_t step, const chess::Board& board, chess::Move move) {
    if (step >= play.solution.size()) return PuzzleAnswer::Wrong;
    if (move == play.solution[step])
        return step + 1 >= play.solution.size() ? PuzzleAnswer::Solved : PuzzleAnswer::Right;
    // Another move that mates: Lichess takes it.
    auto after = board;
    after.makeMove<true>(move);
    chess::Movelist replies;
    chess::movegen::legalmoves(replies, after);
    if (replies.empty() && after.inCheck()) return PuzzleAnswer::Solved;
    return PuzzleAnswer::Wrong;
}

namespace {

struct ThemeName {
    std::string_view key;
    const char* name;
};
// Lichess's themes (its puzzleTheme words), as the interface names them.
constexpr ThemeName THEME_NAMES[] = {
    {"fork", N_("Fork")},
    {"pin", N_("Pin")},
    {"skewer", N_("Skewer")},
    {"discoveredAttack", N_("Discovered attack")},
    {"discoveredCheck", N_("Discovered check")},
    {"doubleCheck", N_("Double check")},
    {"hangingPiece", N_("Hanging piece")},
    {"trappedPiece", N_("Trapped piece")},
    {"sacrifice", N_("Sacrifice")},
    {"deflection", N_("Deflection")},
    {"attraction", N_("Attraction")},
    {"clearance", N_("Clearance")},
    {"interference", N_("Interference")},
    {"intermezzo", N_("Intermezzo")},
    {"capturingDefender", N_("Capture the defender")},
    {"xRayAttack", N_("X-ray attack")},
    {"quietMove", N_("Quiet move")},
    {"defensiveMove", N_("Defensive move")},
    {"zugzwang", N_("Zugzwang")},
    {"advancedPawn", N_("Advanced pawn")},
    {"promotion", N_("Promotion")},
    {"underPromotion", N_("Underpromotion")},
    {"enPassant", N_("En passant")},
    {"castling", N_("Castling")},
    {"exposedKing", N_("Exposed king")},
    {"kingsideAttack", N_("Kingside attack")},
    {"queensideAttack", N_("Queenside attack")},
    {"attackingF2F7", N_("Attacking f2 or f7")},
    {"collinearMove", N_("Collinear move")},
    {"mate", N_("Checkmate")},
    {"mateIn1", N_("Mate in 1")},
    {"mateIn2", N_("Mate in 2")},
    {"mateIn3", N_("Mate in 3")},
    {"mateIn4", N_("Mate in 4")},
    {"mateIn5", N_("Mate in 5 or more")},
    {"backRankMate", N_("Back rank mate")},
    {"smotheredMate", N_("Smothered mate")},
    {"anastasiaMate", N_("Anastasia's mate")},
    {"arabianMate", N_("Arabian mate")},
    {"bodenMate", N_("Boden's mate")},
    {"doubleBishopMate", N_("Double bishop mate")},
    {"dovetailMate", N_("Dovetail mate")},
    {"epauletteMate", N_("Epaulette mate")},
    {"hookMate", N_("Hook mate")},
    {"cornerMate", N_("Corner mate")},
    {"killBoxMate", N_("Kill box mate")},
    {"morphysMate", N_("Morphy's mate")},
    {"operaMate", N_("Opera mate")},
    {"pillsburysMate", N_("Pillsbury's mate")},
    {"balestraMate", N_("Balestra mate")},
    {"blindSwineMate", N_("Blind swine mate")},
    {"swallowstailMate", N_("Swallow's tail mate")},
    {"triangleMate", N_("Triangle mate")},
    {"vukovicMate", N_("Vukovic mate")},
    {"opening", N_("Opening")},
    {"middlegame", N_("Middlegame")},
    {"endgame", N_("Endgame")},
    {"pawnEndgame", N_("Pawn endgame")},
    {"knightEndgame", N_("Knight endgame")},
    {"bishopEndgame", N_("Bishop endgame")},
    {"rookEndgame", N_("Rook endgame")},
    {"queenEndgame", N_("Queen endgame")},
    {"queenRookEndgame", N_("Queen and rook endgame")},
    {"advantage", N_("Advantage")},
    {"crushing", N_("Crushing")},
    {"equality", N_("Equality")},
    {"oneMove", N_("One-move puzzle")},
    {"short", N_("Short puzzle")},
    {"long", N_("Long puzzle")},
    {"veryLong", N_("Very long puzzle")},
    {"master", N_("Master games")},
    {"masterVsMaster", N_("Master vs master games")},
    {"superGM", N_("Super GM games")},
};

}  // namespace

std::string puzzleThemeName(std::string_view key) {
    for (const auto& t : THEME_NAMES)
        if (t.key == key) return TR(t.name);
    return std::string(key);
}

const std::vector<std::string_view>& puzzleThemesShown() {
    static const std::vector<std::string_view> order = [] {
        std::vector<std::string_view> out;
        for (const auto& t : THEME_NAMES) out.push_back(t.key);
        return out;
    }();
    return order;
}

std::string puzzleOpeningName(std::string_view key) {
    std::string out(key);
    std::replace(out.begin(), out.end(), '_', ' ');
    return out;
}

int difficultyOffset(PuzzleDifficulty difficulty) {
    switch (difficulty) {
        case PuzzleDifficulty::Easier:
            return -300;
        case PuzzleDifficulty::Harder:
            return 300;
        case PuzzleDifficulty::Normal:
            break;
    }
    return 0;
}

const char* difficultyKey(PuzzleDifficulty difficulty) {
    switch (difficulty) {
        case PuzzleDifficulty::Easier:
            return "easier";
        case PuzzleDifficulty::Harder:
            return "harder";
        case PuzzleDifficulty::Normal:
            break;
    }
    return "normal";
}

}  // namespace fcg
