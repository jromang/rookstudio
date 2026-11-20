#include "offline_evals.hpp"

#include <algorithm>
#include <fstream>

#include <json.hpp>

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

// The runs a preparation names, opened; none when one cannot be (removed meanwhile).
std::vector<std::unique_ptr<study::EvalsRun>> openRuns(const std::vector<fs::path>& files) {
    std::vector<std::unique_ptr<study::EvalsRun>> runs;
    try {
        for (const auto& f : files) runs.push_back(std::make_unique<study::EvalsRun>(f));
    } catch (const std::exception&) {
        runs.clear();
    }
    return runs;
}

// The database's lines given to the builder as they come, its runs published as it writes them.
class IndexConsumer final : public ZstdConsumer {
   public:
    IndexConsumer(fs::path folder, study::EvalsBuilder::Options options, OfflineEvals::Progress& progress,
                  std::function<void(std::vector<fs::path>)> publish)
        : folder_(std::move(folder)),
          builder_(folder_ / "build", options),
          progress_(progress),
          publish_(std::move(publish)) {}

    bool text(std::string_view piece) override {
        for (std::size_t from = 0;;) {
            const auto end = piece.find('\n', from);
            if (end == std::string_view::npos) {
                pending_.append(piece.substr(from));
                break;
            }
            if (pending_.empty()) {
                line(piece.substr(from, end - from));
            } else {
                pending_.append(piece.substr(from, end - from));
                line(pending_);
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
        published();
        return j;
    }
    void commit() override { builder_.commit(); }
    void restore(const nlohmann::json& state) override {
        builder_.restore(state);
        pending_ = state.value("text", "");
        counts();
        published();
    }
    void startAgain() override {
        builder_.startAgain();
        pending_.clear();
        counts();
        published();
    }
    std::string finish(const std::string& date) override {
        if (!pending_.empty()) line(pending_);
        pending_.clear();
        counts();
        if (!builder_.error().empty()) return builder_.error();
        progress_.merging = true;
        auto why          = builder_.finish(study::nextEvalsIndex(folder_), date, &progress_.merged, &progress_.total);
        progress_.merging = false;
        return why;
    }
    [[nodiscard]] std::string error() const override { return builder_.error(); }

   private:
    void line(std::string_view text) {
        if (!text.empty() && text.back() == '\r') text.remove_suffix(1);
        if (!text.empty()) builder_.line(text);
    }
    void counts() {
        progress_.lines = builder_.lines();
        progress_.kept  = builder_.kept();
    }
    void published() {
        auto runs = builder_.runs();
        if (runs == published_) return;
        published_ = runs;
        publish_(std::move(runs));
    }

    fs::path folder_;
    study::EvalsBuilder builder_;
    OfflineEvals::Progress& progress_;
    std::function<void(std::vector<fs::path>)> publish_;
    std::vector<fs::path> published_;
    std::string pending_;
};

}  // namespace

PositionEval knownEval(const study::OfflineEval& e, const chess::Board& board) {
    PositionEval out;
    const bool white = board.sideToMove() == chess::Color::WHITE;
    out.score        = e.mate ? fastchess::Score{fastchess::ScoreType::MATE, white ? e.mate : -e.mate}
                              : fastchess::Score{fastchess::ScoreType::CP, white ? e.cp : -e.cp};
    out.best         = e.best;
    out.pv           = e.best;
    out.depth        = e.depth;
    out.known        = true;
    return out;
}

std::optional<study::OfflineEval> findEval(const study::EvalsLookup& lookup, const chess::Board& board) {
    if (board.chess960()) return std::nullopt;
    auto found = lookup.find(study::evalsPosition(board));
    if (found && !found->best.empty()) {
        chess::Movelist moves;
        chess::movegen::legalmoves(moves, board);
        chess::Move move = chess::Move::NO_MOVE;
        try {
            move = chess::uci::uciToMove(board, found->best);
        } catch (const std::exception&) {
        }
        if (std::find(moves.begin(), moves.end(), move) == moves.end()) return std::nullopt;
    }
    return found;
}

std::optional<PositionEval> reviewKnownEval(const study::EvalsLookup& lookup, const chess::Board& board) {
    const auto e = findEval(lookup, board);
    if (!e || e->depth < REVIEW_KNOWN_DEPTH) return std::nullopt;
    return knownEval(*e, board);
}

OfflineEvals::OfflineEvals(fs::path folder) : folder_(std::move(folder)) {
    study::tidyEvalsFolder(folder_);  // an index being written when the program stopped, older ones
    reload();
}

OfflineEvals::~OfflineEvals() {
    cancel_ = true;
    if (thread_.joinable()) thread_.join();
}

std::shared_ptr<const study::EvalsLookup> OfflineEvals::lookup() const {
    std::lock_guard lock(mutex_);
    return lookup_;
}

OfflineEvals::State OfflineEvals::state() const {
    std::lock_guard lock(mutex_);
    return state_;
}

std::optional<study::OfflineEval> OfflineEvals::find(const chess::Board& board) const {
    const auto l = lookup();
    if (!l) return std::nullopt;
    return findEval(*l, board);
}

void OfflineEvals::reload() {
    State st;
    std::unique_ptr<study::EvalsIndex> index;
    if (const auto newest = study::newestEvalsIndex(folder_); !newest.empty()) {
        try {
            index = std::make_unique<study::EvalsIndex>(newest);
        } catch (const std::exception&) {
            index.reset();  // not an index (a disk's fault): as if none
        }
    }
    if (index) {
        st.ready     = true;
        st.positions = index->positions();
        st.bytes     = index->bytes();
        st.lines     = index->lines();
        st.min_men   = index->minMen();
        st.date      = index->date();
    }
    std::vector<std::unique_ptr<study::EvalsRun>> runs;
    // What a preparation stopped by a crash at its very end leaves: a folder without its state, or a state whose runs
    // are gone (the index was made, its runs removed, the state not yet). Nothing to go on from: removed.
    std::error_code ec;
    if (!busy_ && fs::exists(build(), ec)) {
        const auto j  = readJsonFile(build() / "reading.json");
        bool unusable = !j;
        if (j) {
            const auto state = j->value("state", nlohmann::json::object());
            for (const auto& run : study::evalsRuns(build(), state)) unusable |= !fs::exists(run, ec);
            // Its log, as long as the state counts.
            const auto log     = build() / ("log-" + std::to_string(state.value("log", uint64_t{0})) + ".bin");
            const auto pending = state.value("pending", uint64_t{0});
            if (pending > 0) unusable |= !fs::exists(log, ec) || fs::file_size(log, ec) < 12 * pending;
        }
        if (unusable) fs::remove_all(build(), ec);
    }
    if (const auto j = readJsonFile(build() / "reading.json")) {
        st.stopped         = true;
        st.stopped_at      = j->value("offset", uint64_t{0});
        st.size            = j->value("size", uint64_t{0});
        st.source          = j->value("source", "");
        const auto state   = j->value("state", nlohmann::json::object());
        st.answering       = state.value("in_runs", uint64_t{0});
        st.kept            = st.answering + state.value("pending", uint64_t{0});
        st.stopped_min_men = state.value("min_men", 0);
        // A first preparation: the positions it read answer at once; an update leaves the index answering.
        if (!index) runs = openRuns(study::evalsRuns(build(), state));
    }
    std::shared_ptr<const study::EvalsLookup> lookup;
    if (index || !runs.empty()) lookup = std::make_shared<const study::EvalsLookup>(std::move(index), std::move(runs));
    std::lock_guard lock(mutex_);
    state_  = std::move(st);
    lookup_ = std::move(lookup);
    ++generation_;
}

void OfflineEvals::start(std::string source, int min_men) {
    if (busy_) return;
    if (thread_.joinable()) thread_.join();
    if (const auto st = state(); st.stopped && (st.source != source || st.stopped_min_men != min_men)) forgetStopped();
    busy_   = true;
    cancel_ = false;
    {
        std::lock_guard lock(mutex_);
        finished_.reset();
    }
    progress_.bytes   = 0;
    progress_.size    = 0;
    progress_.resumed = false;
    progress_.lines   = 0;
    progress_.kept    = 0;
    progress_.merging = false;
    progress_.merged  = 0;
    progress_.total   = 0;
    thread_           = std::thread([this, source = std::move(source), min_men] {
        std::string error;
        try {
            run(source, min_men);
        } catch (const std::exception& e) {
            error = e.what();
        }
        {
            std::lock_guard lock(mutex_);
            if (!finished_) finished_ = error.empty() ? std::string("stopped") : error;
        }
        busy_ = false;
    });
}

void OfflineEvals::run(std::string source, int min_men) {
    std::error_code ec;
    fs::create_directories(build(), ec);
    study::EvalsBuilder::Options options;
    options.min_men     = min_men;
    options.run_entries = run_entries_;
    IndexConsumer consumer(folder_, options, progress_, [this](std::vector<fs::path> runs) {
        {
            std::lock_guard lock(mutex_);
            runs_ = std::move(runs);
        }
        ++runs_version_;
    });
    ZstdReadingOptions o;
    o.checkpoint_every = checkpoint_every_;
    o.retry_wait       = retry_wait_;
    o.on_saved         = on_saved_;
    auto why           = readZstd(source, build() / "reading.json", consumer, progress_, cancel_, o);
    if (why.empty()) fs::remove_all(build(), ec);  // the index made: its runs and logs are gone, its state too
    std::lock_guard lock(mutex_);
    finished_ = std::move(why);
}

std::optional<std::string> OfflineEvals::finished() {
    if (busy_) return std::nullopt;
    std::optional<std::string> out;
    {
        std::lock_guard lock(mutex_);
        out = std::move(finished_);
        finished_.reset();
    }
    if (out) {
        if (thread_.joinable()) thread_.join();
        if (out->empty()) study::tidyEvalsFolder(folder_);  // the index it replaced
        reload();
        runs_seen_ = runs_version_;
    }
    return out;
}

void OfflineEvals::poll() {
    const auto version = runs_version_.load();
    if (version == runs_seen_) return;
    runs_seen_ = version;
    {
        std::lock_guard lock(mutex_);
        if (lookup_ && lookup_->index()) return;  // an update: the index answers until the new one is made
    }
    std::vector<fs::path> files;
    {
        std::lock_guard lock(mutex_);
        files = runs_;
    }
    auto runs = openRuns(files);
    std::shared_ptr<const study::EvalsLookup> lookup;
    if (!runs.empty()) lookup = std::make_shared<const study::EvalsLookup>(nullptr, std::move(runs));
    std::lock_guard lock(mutex_);
    lookup_ = std::move(lookup);
    ++generation_;
}

void OfflineEvals::remove() {
    cancel_ = true;
    if (thread_.joinable()) thread_.join();
    {
        std::lock_guard lock(mutex_);
        lookup_.reset();
        finished_.reset();
        runs_.clear();
        ++generation_;
    }
    std::error_code ec;
    fs::remove_all(folder_, ec);
    reload();
    runs_seen_ = runs_version_;
}

void OfflineEvals::forgetStopped() {
    if (busy_) return;
    if (thread_.joinable()) thread_.join();
    {
        std::lock_guard lock(mutex_);
        if (lookup_ && !lookup_->index()) lookup_.reset();
        runs_.clear();
        ++generation_;
    }
    std::error_code ec;
    fs::remove_all(build(), ec);
    reload();
    runs_seen_ = runs_version_;
}

}  // namespace fcg
