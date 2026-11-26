#include "lichess_evals.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <map>

#include <fmt/include/fmt/format.h>
#include <chess.hpp>
#include <json.hpp>
#include <study/sources.hpp>
#include <tour/files.hpp>

#include "https.hpp"
#include "zstd_reading.hpp"

namespace fcg {

namespace fs = std::filesystem;

namespace {

// The positions after each of `pgn`'s moves; none when a move cannot be read.
std::optional<std::vector<chess::Board>> boardsOf(std::string_view pgn) {
    std::vector<chess::Board> out;
    chess::Board board;
    std::size_t at = 0;
    while (at < pgn.size()) {
        while (at < pgn.size() && pgn[at] == ' ') ++at;
        auto end = pgn.find(' ', at);
        if (end == std::string_view::npos) end = pgn.size();
        const auto token = pgn.substr(at, end - at);
        at               = end;
        if (token.empty() || token.back() == '.') continue;  // "1." and "1..."
        chess::Move move = chess::Move::NO_MOVE;
        try {
            move = chess::uci::parseSan(board, std::string(token));
        } catch (const std::exception&) {
            return std::nullopt;
        }
        if (move == chess::Move::NO_MOVE) return std::nullopt;
        board.makeMove(move);
        out.push_back(board);
    }
    return out;
}

std::string withoutQuotes(std::string_view text) {
    std::string out;
    for (const char c : text)
        if (c != '"' && c != ';') out += c;
    return out;
}

template <typename T>
bool number(std::string_view text, T& out) {
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), out);
    return ec == std::errc() && end == text.data() + text.size();
}

std::vector<std::string_view> split(std::string_view text, char by) {
    std::vector<std::string_view> out;
    for (std::size_t at = 0;;) {
        const auto end = text.find(by, at);
        out.push_back(text.substr(at, end == std::string_view::npos ? std::string_view::npos : end - at));
        if (end == std::string_view::npos) return out;
        at = end + 1;
    }
}

// "Sun, 01 Nov 2026 17:23:27 GMT" → "2026-11-01"; as it is when it is not such a date.
std::string httpDate(const std::string& date) {
    static constexpr std::string_view MONTHS[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                                  "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    const auto parts                           = split(date, ' ');
    if (parts.size() < 4) return date;
    for (std::size_t m = 0; m < 12; ++m)
        if (parts[2] == MONTHS[m]) return fmt::format("{}-{:02}-{}", parts[3], m + 1, parts[1]);
    return date;
}

std::optional<nlohmann::json> readJsonFile(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return std::nullopt;
    auto j = nlohmann::json::parse(in, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return std::nullopt;
    return j;
}

std::vector<EvaluatedPosition> readRows(const fs::path& file) {
    std::vector<EvaluatedPosition> out;
    std::ifstream in(file, std::ios::binary);
    std::string row;
    while (std::getline(in, row)) {
        if (!row.empty() && row.back() == '\r') row.pop_back();
        if (auto found = readEvaluatedRow(row)) out.push_back(std::move(*found));
    }
    return out;
}

}  // namespace

std::vector<EvalCandidate> evalCandidates(const std::vector<NamedOpening>& all) {
    std::vector<std::optional<std::vector<chess::Board>>> lines;
    lines.reserve(all.size());
    for (const auto& o : all) lines.push_back(boardsOf(o.pgn));
    // The positions the named lines come to: the first line of the table's order for each.
    std::unordered_map<std::string, std::size_t> named;
    for (std::size_t i = 0; i < all.size(); ++i)
        if (lines[i] && !lines[i]->empty()) named.emplace(std::string(evalKey(lines[i]->back().getFen(false))), i);

    std::vector<EvalCandidate> out;
    std::unordered_map<std::string, std::size_t> index;
    // On the lines: the last named variation on the way, the shortest way first.
    for (std::size_t i = 0; i < all.size(); ++i) {
        if (!lines[i]) continue;
        std::size_t current = i;
        int plies           = 0;
        for (const auto& board : *lines[i]) {
            ++plies;
            const auto fen = board.getFen(false);
            std::string key(evalKey(fen));
            if (const auto n = named.find(key); n != named.end()) current = n->second;
            EvalCandidate c{board.getEpd(), all[current].eco, all[current].name, plies, true};
            if (const auto [at, added] = index.emplace(std::move(key), out.size()); added) {
                out.push_back(std::move(c));
            } else if (plies < out[at->second].plies) {
                out[at->second] = std::move(c);
            }
        }
    }
    // One move off them, of the variation they leave.
    const auto on_line = out.size();
    for (std::size_t i = 0; i < on_line; ++i) {
        chess::Board board;
        board.setEpd(out[i].epd);
        chess::Movelist moves;
        chess::movegen::legalmoves(moves, board);
        for (const auto& move : moves) {
            board.makeMove(move);
            std::string key(evalKey(board.getFen(false)));
            const auto [at, added] = index.emplace(std::move(key), out.size());
            if (added) {
                out.push_back({board.getEpd(), out[i].eco, out[i].name, out[i].plies + 1, false});
            } else if (auto& c = out[at->second]; !c.on_line && out[i].plies + 1 < c.plies) {
                c = {board.getEpd(), out[i].eco, out[i].name, out[i].plies + 1, false};
            }
            board.unmakeMove(move);
        }
    }
    return out;
}

std::string_view evalKey(std::string_view fen) {
    std::size_t at = 0;
    for (int field = 0; field < 3; ++field) {
        at = fen.find(' ', at + (field ? 1 : 0));
        if (at == std::string_view::npos) return fen;
    }
    return fen.substr(0, at);
}

bool readEvalLine(std::string_view line, std::string_view& key, Evaluation& evaluation) {
    static constexpr std::string_view FEN = "\"fen\":\"";
    const auto at                         = line.find(FEN);
    if (at == std::string_view::npos) return false;
    const auto end = line.find('"', at + FEN.size());
    if (end == std::string_view::npos) return false;
    key = evalKey(line.substr(at + FEN.size(), end - at - FEN.size()));
    // A candidate's line only: the whole of it read.
    const auto j = nlohmann::json::parse(line, nullptr, false);
    if (j.is_discarded() || !j.contains("evals") || !j["evals"].is_array()) return false;
    bool any = false;
    for (const auto& e : j["evals"]) {
        if (!e.is_object() || !e.contains("pvs") || !e["pvs"].is_array() || e["pvs"].empty()) continue;
        const auto& pv = e["pvs"][0];
        Evaluation got;
        got.depth  = e.value("depth", 0);
        got.knodes = e.value("knodes", uint64_t{0});
        if (pv.contains("mate") && pv["mate"].is_number_integer()) {
            got.mate = pv["mate"].get<int>();
        } else if (pv.contains("cp") && pv["cp"].is_number_integer()) {
            got.cp = pv["cp"].get<int>();
        } else {
            continue;
        }
        if (!any || got.depth > evaluation.depth || (got.depth == evaluation.depth && got.knodes > evaluation.knodes)) {
            evaluation = got;
            any        = true;
        }
    }
    return any;
}

std::string evaluatedRow(const EvaluatedPosition& found) {
    const auto& p = found.position;
    const auto& e = found.evaluation;
    return fmt::format("{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}", p.epd, p.plies, p.on_line ? 1 : 0, p.eco, p.name, e.depth,
                       e.mate ? fmt::format("#{}", e.mate) : std::to_string(e.cp), e.knodes);
}

std::optional<EvaluatedPosition> readEvaluatedRow(std::string_view row) {
    const auto f = split(row, '\t');
    if (f.size() != 8 || f[0].empty()) return std::nullopt;
    EvaluatedPosition out;
    auto& p  = out.position;
    auto& e  = out.evaluation;
    p.epd    = std::string(f[0]);
    p.eco    = std::string(f[3]);
    p.name   = std::string(f[4]);
    int line = 0;
    if (!number(f[1], p.plies) || !number(f[2], line) || !number(f[5], e.depth) || !number(f[7], e.knodes))
        return std::nullopt;
    p.on_line = line != 0;
    if (!f[6].empty() && f[6][0] == '#') {
        if (!number(f[6].substr(1), e.mate) || e.mate == 0) return std::nullopt;
    } else if (!number(f[6], e.cp)) {
        return std::nullopt;
    }
    return out;
}

EvalScanner::EvalScanner(std::vector<EvalCandidate> candidates) : candidates_(std::move(candidates)) {
    index_.reserve(candidates_.size());
    for (std::size_t i = 0; i < candidates_.size(); ++i) index_.emplace(evalKey(candidates_[i].epd), i);
}

bool EvalScanner::keep(std::size_t index, const Evaluation& evaluation) {
    const auto [at, added] = found_.try_emplace(index, EvaluatedPosition{candidates_[index], evaluation});
    if (added) return true;
    auto& kept = at->second.evaluation;
    if (evaluation.depth < kept.depth || (evaluation.depth == kept.depth && evaluation.knodes <= kept.knodes))
        return false;
    kept = evaluation;
    return true;
}

const EvaluatedPosition* EvalScanner::line(std::string_view line) {
    // Every line begins with its FEN: the key looked for before anything else is read.
    static constexpr std::string_view FEN = "{\"fen\":\"";
    std::string_view key;
    if (line.substr(0, FEN.size()) == FEN) {
        const auto end = line.find('"', FEN.size());
        if (end == std::string_view::npos) return nullptr;
        key = evalKey(line.substr(FEN.size(), end - FEN.size()));
        if (!index_.count(key)) return nullptr;
    }
    Evaluation evaluation;
    if (!readEvalLine(line, key, evaluation)) return nullptr;
    const auto at = index_.find(key);
    if (at == index_.end() || !keep(at->second, evaluation)) return nullptr;
    return &found_.at(at->second);
}

void EvalScanner::restore(const EvaluatedPosition& found) {
    if (const auto at = index_.find(evalKey(found.position.epd)); at != index_.end())
        keep(at->second, found.evaluation);
}

void EvalScanner::forget() { found_.clear(); }

std::vector<EvaluatedPosition> EvalScanner::found() const {
    std::vector<std::size_t> order;
    order.reserve(found_.size());
    for (const auto& [index, position] : found_) order.push_back(index);
    std::sort(order.begin(), order.end());
    std::vector<EvaluatedPosition> out;
    out.reserve(order.size());
    for (const auto index : order) out.push_back(found_.at(index));
    return out;
}

std::vector<SuitePosition> evaluatedSuite(const std::vector<EvaluatedPosition>& all,
                                          const EvaluatedSuiteOptions& options) {
    const int middle = (options.from_cp + options.to_cp) / 2;
    // Of two positions of one variation, the one kept.
    const auto better = [middle](const EvaluatedPosition& a, const EvaluatedPosition& b) {
        if (a.position.on_line != b.position.on_line) return a.position.on_line;
        if (a.evaluation.depth != b.evaluation.depth) return a.evaluation.depth > b.evaluation.depth;
        const auto off_a = std::abs(a.evaluation.cp - middle), off_b = std::abs(b.evaluation.cp - middle);
        if (off_a != off_b) return off_a < off_b;
        if (a.position.plies != b.position.plies) return a.position.plies < b.position.plies;
        return a.position.epd < b.position.epd;
    };
    std::vector<const EvaluatedPosition*> kept;
    std::map<std::string, std::size_t> by_name;  // name → its position in `kept`
    for (const auto& p : all) {
        const auto& e = p.evaluation;
        if (e.mate != 0 || e.depth < options.depth || e.cp < options.from_cp || e.cp > options.to_cp) continue;
        if (p.position.plies < options.min_plies || p.position.plies > options.max_plies) continue;
        if (!options.one_per_name) {
            kept.push_back(&p);
            continue;
        }
        const auto [at, added] = by_name.try_emplace(p.position.name, kept.size());
        if (added) {
            kept.push_back(&p);
        } else if (better(p, *kept[at->second])) {
            kept[at->second] = &p;
        }
    }
    std::sort(kept.begin(), kept.end(), [](const EvaluatedPosition* a, const EvaluatedPosition* b) {
        if (a->position.eco != b->position.eco) return a->position.eco < b->position.eco;
        if (a->position.name != b->position.name) return a->position.name < b->position.name;
        if (a->position.plies != b->position.plies) return a->position.plies < b->position.plies;
        return a->position.epd < b->position.epd;
    });
    std::vector<SuitePosition> out;
    out.reserve(kept.size());
    for (const auto* p : kept) {
        const auto& e    = p->evaluation;
        const bool white = evalKey(p->position.epd).find(" w") != std::string_view::npos;
        SuitePosition s;
        s.epd   = fmt::format("{} ce {}; acd {}; c0 \"{} {}\";", p->position.epd, white ? e.cp : -e.cp, e.depth,
                              p->position.eco, withoutQuotes(p->position.name));
        s.eco   = p->position.eco;
        s.name  = p->position.name;
        s.plies = p->position.plies;
        s.cp    = e.cp;
        s.depth = e.depth;
        out.push_back(std::move(s));
    }
    keepEvenly(out, options.most);
    return out;
}

EvalsSize evalsSize(const std::string& url, const std::atomic<bool>* cancel) {
    EvalsSize out;
    auto asked      = lichess::request("GET", url, {}, "*/*");
    asked.timeout_s = 30;
    asked.headers.emplace_back("Range", "bytes=0-0");
    HttpStream stream;
    stream.cancel  = cancel;
    stream.on_head = [&](int status, std::string_view head) {
        if (status == 206) {
            // "bytes 0-0/22499667845"
            if (const auto range = httpHeader(head, "Content-Range")) {
                if (const auto slash = range->find('/'); slash != std::string::npos)
                    (void)number(std::string_view(*range).substr(slash + 1), out.bytes);
            }
        } else if (status == 200) {
            if (const auto length = httpHeader(head, "Content-Length")) (void)number(*length, out.bytes);
        }
        if (const auto date = httpHeader(head, "Last-Modified")) out.date = httpDate(*date);
        return false;  // the head is all that is wanted
    };
    stream.on_data                      = [](std::string_view) { return false; };
    static_cast<lichess::Failure&>(out) = lichess::responseOf(asked, politeStream(asked, stream));
    if (!out.failed() && out.bytes == 0) {  // an answer without the size
        out.net    = lichess::NetError::BadAnswer;
        out.detail = "the server did not give the size";
    }
    return out;
}

EvalsFolder evalsFolder(const fs::path& folder) {
    EvalsFolder out;
    std::error_code ec;
    if (const auto j = readJsonFile(folder / "evaluations.json"); j && fs::exists(folder / "evaluations.tsv", ec)) {
        out.done  = true;
        out.date  = j->value("date", "");
        out.lines = j->value("lines", uint64_t{0});
        out.kept  = j->value("kept", std::size_t{0});
    }
    if (const auto j = readJsonFile(folder / "reading.json")) {
        out.stopped_at = j->value("offset", uint64_t{0});
        out.size       = j->value("size", uint64_t{0});
        out.source     = j->value("source", "");
    }
    return out;
}

std::vector<EvaluatedPosition> evaluatedPositions(const fs::path& folder) {
    std::error_code ec;
    if (!fs::exists(folder / "evaluations.json", ec)) return {};
    return readRows(folder / "evaluations.tsv");
}

namespace {

// The evaluations database's lines, each scanned for a candidate position: those found written to a file as they come
// (reading.tsv), made final at the end (evaluations.tsv and .json).
class EvalsConsumer final : public ZstdConsumer {
   public:
    EvalsConsumer(fs::path folder, std::string source, EvalsReading::Progress& progress)
        : folder_(std::move(folder)),
          source_(std::move(source)),
          rows_file_(folder_ / "reading.tsv"),
          progress_(progress),
          scanner_(evalCandidates(namedOpenings())) {}

    bool text(std::string_view piece) override {
        for (std::size_t from = 0;;) {
            const auto end = piece.find('\n', from);
            if (end == std::string_view::npos) {
                pending_.append(piece.substr(from));
                break;
            }
            std::string_view line = piece.substr(from, end - from);
            if (!pending_.empty()) {
                pending_.append(line);
                line = pending_;
            }
            if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
            if (!line.empty()) {
                ++lines_;
                if (const auto* found = scanner_.line(line)) {
                    const auto row = evaluatedRow(*found) + '\n';
                    if (!rows_ || std::fwrite(row.data(), 1, row.size(), rows_.get()) != row.size()) {
                        error_ = "cannot write " + rows_file_.string() + " (is the disk full?)";
                        return false;
                    }
                }
            }
            pending_.clear();
            from = end + 1;
        }
        progress_.lines = lines_;
        progress_.kept  = scanner_.kept();
        return true;
    }
    nlohmann::json mark() override { return {{"pending", pending_}, {"lines", lines_}}; }
    void commit() override {
        if (rows_) std::fflush(rows_.get());
    }
    void restore(const nlohmann::json& state) override {
        pending_ = state.value("pending", "");
        lines_   = state.value("lines", uint64_t{0});
        scanner_.forget();
        if (rows_) std::fflush(rows_.get());
        for (const auto& found : readRows(rows_file_)) scanner_.restore(found);
        if (!rows_) rows_.reset(tour::openFile(rows_file_, "ab"));
        if (!rows_) error_ = "cannot write " + rows_file_.string();
    }
    void startAgain() override {
        scanner_.forget();
        rows_.reset(tour::openFile(rows_file_, "wb"));
        if (!rows_) error_ = "cannot write " + rows_file_.string();
        pending_.clear();
        lines_ = 0;
    }
    std::string finish(const std::string& date) override {
        if (!pending_.empty()) (void)text("\n");
        if (!error_.empty()) return error_;
        nlohmann::json j{{"source", source_}, {"date", date}, {"lines", lines_}, {"kept", scanner_.kept()}};
        std::string all;
        for (const auto& found : scanner_.found()) all += evaluatedRow(found) + '\n';
        tour::replaceFile(folder_ / "evaluations.tsv", all);
        tour::replaceFile(folder_ / "evaluations.json", j.dump());
        rows_.reset();
        std::error_code ec;
        fs::remove(rows_file_, ec);
        return {};
    }
    [[nodiscard]] std::string error() const override { return error_; }

   private:
    fs::path folder_;
    std::string source_;
    fs::path rows_file_;
    EvalsReading::Progress& progress_;
    EvalScanner scanner_;
    std::unique_ptr<std::FILE, int (*)(std::FILE*)> rows_{nullptr, &std::fclose};
    std::string pending_;
    uint64_t lines_ = 0;
    std::string error_;
};

}  // namespace

void EvalsReading::start(std::string source, fs::path folder) {
    if (busy()) return;
    ZstdReadingOptions options;
    options.checkpoint_every = checkpoint_every_;
    options.retry_wait       = retry_wait_;
    // The task holds what it reads and what it tells: it may outlive this object, parked.
    job_.start([source = std::move(source), folder = std::move(folder), options](TaskState<Progress>& job) {
        try {
            std::error_code ec;
            fs::create_directories(folder, ec);
            EvalsConsumer consumer(folder, source, job);
            return readZstd(source, folder / "reading.json", consumer, job, job.cancel, options);
        } catch (const std::exception& e) {
            const std::string why = e.what();
            return why.empty() ? std::string("stopped") : why;
        }
    });
}

std::optional<std::string> EvalsReading::finished() {
    if (!job_.ready()) return std::nullopt;
    return job_.take();
}

}  // namespace fcg
