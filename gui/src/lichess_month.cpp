#include "lichess_month.hpp"

#include <charconv>
#include <vector>

#include <study/import.hpp>

namespace fcg::lichess {

namespace {

// A header's value in a game's text, empty when it has none.
std::string_view header(std::string_view game, std::string_view name) {
    const std::string key = "[" + std::string(name) + " \"";
    const auto at         = game.find(key);
    if (at == std::string_view::npos) return {};
    const auto from = at + key.size();
    const auto end  = game.find('"', from);
    return end == std::string_view::npos ? std::string_view{} : game.substr(from, end - from);
}

int numberOf(std::string_view text) {
    int n = 0;
    if (std::from_chars(text.data(), text.data() + text.size(), n).ec != std::errc{}) return 0;
    return n;
}

// Its games one after the other: a game ends where the next one's [Event begins.
class MonthConsumer final : public ZstdConsumer {
   public:
    MonthConsumer(std::shared_ptr<study::Database> db, MonthFilter filter, MonthReading::Progress& progress)
        : db_(std::move(db)), filter_(filter), progress_(progress) {}

    bool text(std::string_view piece) override {
        for (std::size_t from = 0;;) {
            const auto end = piece.find('\n', from);
            if (end == std::string_view::npos) {
                line_.append(piece.substr(from));
                break;
            }
            line_.append(piece.substr(from, end - from));
            if (line_.rfind("[Event ", 0) == 0 && moves_) {
                game(game_);
                game_.clear();
                moves_ = false;
            }
            if (!line_.empty() && line_.front() != '[' && line_ != "\r") moves_ = true;
            game_ += line_;
            game_ += '\n';
            line_.clear();
            from = end + 1;
            if (!error_.empty()) return false;
        }
        return true;
    }
    nlohmann::json mark() override {
        return {{"game", game_}, {"line", line_}, {"moves", moves_}, {"games", games_}, {"kept", kept_}};
    }
    void commit() override {
        if (batch_.empty() || !error_.empty()) return;
        try {
            const auto appended = db_->append(std::move(batch_));
            added_ += appended.added;
            progress_.added = added_;
        } catch (const std::exception& e) {
            error_ = e.what();
        }
        batch_.clear();
    }
    void restore(const nlohmann::json& state) override {
        game_  = state.value("game", "");
        line_  = state.value("line", "");
        moves_ = state.value("moves", false);
        games_ = state.value("games", uint64_t{0});
        kept_  = state.value("kept", uint64_t{0});
        batch_.clear();
    }
    void startAgain() override {
        game_.clear();
        line_.clear();
        moves_ = false;
        games_ = kept_ = 0;
        batch_.clear();
    }
    std::string finish(const std::string&) override {
        if (!line_.empty()) (void)text("\n");
        if (moves_) game(game_);
        game_.clear();
        commit();
        return error_;
    }
    [[nodiscard]] std::string error() const override { return error_; }

   private:
    void game(const std::string& text) {
        ++games_;
        progress_.games = games_;
        if (!keepGame(text, filter_)) return;
        study::ImportedGame g;
        if (!study::readImportedGame(text, g)) return;
        batch_.push_back(std::move(g));
        ++kept_;
        progress_.kept = kept_;
        if (batch_.size() >= MonthReading::BATCH) commit();
    }

    std::shared_ptr<study::Database> db_;
    MonthFilter filter_;
    MonthReading::Progress& progress_;
    std::string game_, line_;
    bool moves_     = false;  // the game begun has its moves
    uint64_t games_ = 0, kept_ = 0, added_ = 0;
    std::vector<study::ImportedGame> batch_;
    std::string error_;
};

}  // namespace

bool keepGame(std::string_view game, const MonthFilter& filter) {
    if (filter.min_elo > 0) {
        if (numberOf(header(game, "WhiteElo")) < filter.min_elo) return false;
        if (numberOf(header(game, "BlackElo")) < filter.min_elo) return false;
    }
    if (!filter.bullet) {
        const auto tc   = header(game, "TimeControl");  // "180+2", "-" by correspondence
        const auto plus = tc.find('+');
        if (plus != std::string_view::npos) {
            const int base = numberOf(tc.substr(0, plus)), increment = numberOf(tc.substr(plus + 1));
            if (base + 40 * increment < 180) return false;
        }
    }
    return true;
}

void MonthReading::start(std::string source, std::shared_ptr<study::Database> db, MonthFilter filter,
                         std::filesystem::path state_file) {
    if (busy() || !db) return;
    // The task holds what it reads and what it tells: it may outlive this object, parked.
    job_.start([source = std::move(source), db = std::move(db), filter, state_file = std::move(state_file),
                options = options_](TaskState<Progress>& job) {
        try {
            MonthConsumer consumer(db, filter, job);
            return readZstd(source, state_file, consumer, job, job.cancel, options);
        } catch (const std::exception& e) {
            return TaskEnd::of(e.what());
        }
    });
}

std::optional<TaskEnd> MonthReading::finished() {
    if (!job_.ready()) return std::nullopt;
    return job_.take();
}

}  // namespace fcg::lichess
