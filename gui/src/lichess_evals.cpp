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
    HttpRequest request;
    request.url       = url;
    request.method    = "GET";
    request.timeout_s = 30;
    request.headers.emplace_back("Range", "bytes=0-0");
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
        } else {
            out.error = fmt::format("the server answered {}", status);
        }
        if (const auto date = httpHeader(head, "Last-Modified")) out.date = httpDate(*date);
        return false;  // the head is all that is wanted
    };
    stream.on_data    = [](std::string_view) { return false; };
    const auto result = politeStream(request, stream);
    if (out.error.empty() && out.bytes == 0)
        out.error = result.error.empty() ? std::string("the server did not give the size") : result.error;
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

EvalsReading::~EvalsReading() {
    cancel_ = true;
    if (thread_.joinable()) thread_.join();
}

void EvalsReading::start(std::string source, fs::path folder) {
    if (busy_) return;
    if (thread_.joinable()) thread_.join();
    busy_   = true;
    cancel_ = false;
    {
        std::lock_guard lock(mutex_);
        finished_.reset();
    }
    progress_.bytes   = 0;
    progress_.size    = 0;
    progress_.lines   = 0;
    progress_.kept    = 0;
    progress_.resumed = false;
    thread_           = std::thread([this, source = std::move(source), folder = std::move(folder)] {
        std::string error;
        try {
            run(source, folder);
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

std::optional<std::string> EvalsReading::finished() {
    if (busy_) return std::nullopt;
    std::lock_guard lock(mutex_);
    auto out = std::move(finished_);
    finished_.reset();
    return out;
}

void EvalsReading::run(std::string source, fs::path folder) {
    const auto finish = [this](std::string why) {
        std::lock_guard lock(mutex_);
        finished_ = std::move(why);
    };
    std::error_code ec;
    fs::create_directories(folder, ec);
    const auto rows_file = folder / "reading.tsv", state_file = folder / "reading.json";
    const bool remote = source.rfind("http://", 0) == 0 || source.rfind("https://", 0) == 0;
    EvalScanner scanner(evalCandidates(namedOpenings()));

    // Where the reading is: at a frame's end, the line begun there, the database's lines read before it.
    struct Mark {
        uint64_t offset = 0;
        std::string pending;
        uint64_t lines = 0;
    } mark;
    std::string validator;  // that the file has not changed: the server's ETag or date, a file's size and time
    uint64_t size = 0;
    std::string date;
    if (!remote) {  // a file downloaded before: its date is not the database's
        size      = fs::file_size(source, ec);
        validator = fmt::format("{}:{}", size, fs::last_write_time(source, ec).time_since_epoch().count());
    }
    // A reading of the same source cut short: on from where it stopped.
    if (const auto j = readJsonFile(state_file);
        j && j->value("source", "") == source && (remote || j->value("validator", "") == validator)) {
        mark.offset  = j->value("offset", uint64_t{0});
        mark.pending = j->value("pending", "");
        mark.lines   = j->value("lines", uint64_t{0});
        if (remote) {
            validator = j->value("validator", "");
            size      = j->value("size", uint64_t{0});
            date      = j->value("date", "");
        }
        for (const auto& found : readRows(rows_file)) scanner.restore(found);
        progress_.resumed = true;
    } else {
        fs::remove(rows_file, ec);
    }
    std::unique_ptr<std::FILE, int (*)(std::FILE*)> rows(tour::openFile(rows_file, "ab"), &std::fclose);
    if (!rows) return finish("cannot write " + rows_file.string());
    const auto saveState = [&] {
        std::fflush(rows.get());
        nlohmann::json j{{"source", source},      {"validator", validator},  {"size", size},       {"date", date},
                         {"offset", mark.offset}, {"pending", mark.pending}, {"lines", mark.lines}};
        tour::replaceFile(state_file, j.dump());
    };
    const auto startAgain = [&] {
        scanner.forget();
        rows.reset(tour::openFile(rows_file, "wb"));
        mark = {};
    };

    study::ZstdStream zstd;
    std::string pending;
    uint64_t lines = 0, at = 0;  // at: the compressed bytes read, the next one's offset
    std::string error;
    auto last_saved = std::chrono::steady_clock::now();
    const auto text = [&](std::string_view piece) {
        for (std::size_t from = 0;;) {
            const auto end = piece.find('\n', from);
            if (end == std::string_view::npos) {
                pending.append(piece.substr(from));
                break;
            }
            std::string_view line = piece.substr(from, end - from);
            if (!pending.empty()) {
                pending.append(line);
                line = pending;
            }
            if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
            if (!line.empty()) {
                ++lines;
                if (const auto* found = scanner.line(line)) {
                    const auto row = evaluatedRow(*found) + '\n';
                    if (std::fwrite(row.data(), 1, row.size(), rows.get()) != row.size()) {
                        error = "cannot write " + rows_file.string() + " (is the disk full?)";
                        return false;
                    }
                }
            }
            pending.clear();
            from = end + 1;
        }
        return !cancel_.load();
    };
    uint64_t chunk_start = 0;  // the offset of the piece being decoded
    const auto frameEnd  = [&](std::size_t read) {
        mark = {chunk_start + read, pending, lines};
        if (const auto now = std::chrono::steady_clock::now();
            std::chrono::duration<double>(now - last_saved).count() >= checkpoint_every_) {
            saveState();
            last_saved = now;
        }
    };
    const std::function<bool(std::string_view)> on_text = text;
    const std::function<void(std::size_t)> on_frame_end = frameEnd;
    const auto feed                                     = [&](std::string_view data) {
        chunk_start     = at;
        const bool more = zstd.feed(data, on_text, on_frame_end);
        at += data.size();
        progress_.bytes = at;
        progress_.lines = lines;
        progress_.kept  = scanner.kept();
        return more && error.empty() && !cancel_;
    };
    // From the mark: the decoder at a frame's start, the line begun there.
    const auto fromMark = [&] {
        zstd.restart();
        pending = mark.pending;
        lines   = mark.lines;
        at      = mark.offset;
    };
    // The end: what was found kept, the reading's files removed.
    const auto done = [&] {
        if (!pending.empty()) (void)text("\n");
        if (!error.empty()) return finish(error);
        nlohmann::json j{{"source", source}, {"date", date}, {"lines", lines}, {"kept", scanner.kept()}};
        std::string all;
        for (const auto& found : scanner.found()) all += evaluatedRow(found) + '\n';
        tour::replaceFile(folder / "evaluations.tsv", all);
        tour::replaceFile(folder / "evaluations.json", j.dump());
        rows.reset();
        fs::remove(rows_file, ec);
        fs::remove(state_file, ec);
        finish("");
    };

    if (!remote) {
        progress_.size = size;
        std::ifstream in(source, std::ios::binary);
        if (!in) return finish("cannot open " + source);
        fromMark();
        in.seekg(static_cast<std::streamoff>(mark.offset));
        std::vector<char> buffer(1 << 20);
        bool more = true;
        while (more) {
            in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            const auto n = static_cast<std::size_t>(in.gcount());
            if (n == 0) break;
            more = feed(std::string_view(buffer.data(), n));
        }
        if (cancel_) {
            saveState();
            return finish("cancelled");
        }
        if (!error.empty()) return finish(error);
        if (!zstd.betweenFrames()) {
            saveState();
            return finish(source + " is cut short");
        }
        return done();
    }

    for (int attempt = 0;; ++attempt) {
        fromMark();
        HttpRequest request;
        request.url       = source;
        request.method    = "GET";
        request.timeout_s = 30;
        if (mark.offset > 0) {
            request.headers.emplace_back("Range", fmt::format("bytes={}-", mark.offset));
            if (!validator.empty()) request.headers.emplace_back("If-Range", validator);
        }
        std::string refused;
        HttpStream stream;
        stream.cancel  = &cancel_;
        stream.on_head = [&](int status, std::string_view head) {
            uint64_t from = 0, total = 0;
            bool ranged = false;
            if (const auto range = httpHeader(head, "Content-Range"); range && range->rfind("bytes ", 0) == 0) {
                if (const auto slash = range->find('/'); slash != std::string::npos) {
                    ranged = true;
                    if ((*range)[6] != '*') std::from_chars(range->data() + 6, range->data() + slash, from);
                    std::from_chars(range->data() + slash + 1, range->data() + range->size(), total);
                }
            }
            if (status == 206 && mark.offset > 0 && ranged && from == mark.offset) {
                size = total;
            } else if (status == 200) {
                if (mark.offset > 0) {  // the file has changed, or the server sends it whole: from its start
                    startAgain();
                    fromMark();
                    progress_.resumed = false;
                }
                size = 0;
                if (const auto length = httpHeader(head, "Content-Length")) (void)number(*length, size);
                validator.clear();  // a strong ETag, or the file's date
                if (const auto etag = httpHeader(head, "ETag"); etag && etag->rfind("W/", 0) != 0) {
                    validator = *etag;
                } else if (const auto modified = httpHeader(head, "Last-Modified")) {
                    validator = *modified;
                }
                if (const auto modified = httpHeader(head, "Last-Modified")) date = httpDate(*modified);
            } else if (status == 416 || status == 206) {
                refused = "the server does not send the rest of the file as asked";
                return false;
            } else {
                refused = fmt::format("the server answered {} for {}", status, source);
                return false;
            }
            if (!rows) {
                error = "cannot write " + rows_file.string();
                return false;
            }
            progress_.size = size;
            return true;
        };
        stream.on_data    = feed;
        const auto result = politeStream(request, stream);
        if (cancel_) {
            saveState();
            return finish("cancelled");
        }
        if (!error.empty()) {
            saveState();
            return finish(error);
        }
        if (!refused.empty()) {
            if (mark.offset > 0 && attempt == 0) {  // read again from the start
                startAgain();
                saveState();
                continue;
            }
            return finish(refused);
        }
        if (result.error.empty() && result.status != 0 && zstd.betweenFrames() && (size == 0 || at == size))
            return done();
        // Cut short: on from the last frame's end, a few times, waiting longer each time.
        saveState();
        if (attempt >= 5)
            return finish(result.error.empty() ? std::string("the download was cut short") : result.error);
        const auto wait = std::chrono::steady_clock::now() + retry_wait_ * (1 << attempt);
        while (std::chrono::steady_clock::now() < wait) {
            if (cancel_) return finish("cancelled");
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

}  // namespace fcg
