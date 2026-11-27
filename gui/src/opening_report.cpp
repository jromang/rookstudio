#include "opening_report.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <map>
#include <stdexcept>

#include <fmt/include/fmt/format.h>
#include <json.hpp>
#include <tour/files.hpp>
#include <tour/util.hpp>

#include <commentary/position.hpp>
#include <study/background.hpp>
#include <study/scan_board.hpp>

#include "board_text.hpp"
#include "dossier.hpp"
#include "engine_oracle.hpp"
#include "i18n.hpp"
#include "mistakes.hpp"
#include "offline_evals.hpp"
#include "profile_entries.hpp"

namespace fcg {

const study::StructureNamer& reportStructures() { return dossierStructures(); }

const study::PlanNamer& reportPlans() {
    static const study::PlanNamer namer = [](const study::ScanBoard& before, const study::CodedMove& push) -> int {
        const auto pos = commentary::Position::fromFen(before.fen());
        if (!pos) return 0;
        for (const auto& m : pos->legalMoves()) {
            if (static_cast<int>(m.from()) != push.from || static_cast<int>(m.to()) != push.to) continue;
            if (push.promotion != study::NO_KIND &&
                (!m.promotion() || static_cast<int>(*m.promotion()) != static_cast<int>(push.promotion)))
                continue;
            return static_cast<int>(commentary::planOf(*pos, m));
        }
        return 0;
    };
    return namer;
}

void makeReport(const std::string& fen, const std::vector<ReportBase>& bases,
                const std::vector<std::string>& user_names, int threads, study::ReportProgress& progress,
                const std::atomic<bool>& cancel, ReportShared& shared) {
    study::enterBackground();  // the lowest priority: a tournament beside it loses no time
    study::ScanBoard position;
    if (!position.setFen(fen, false)) {
        const std::lock_guard lock(shared.mutex);
        shared.report.emplace();
        shared.report->made = study::ReportSection::Done;
        ++shared.serial;
        return;
    }
    std::vector<study::ReportSource> sources;
    for (const auto& b : bases) sources.push_back({b.name, b.snapshot, b.tree});
    study::ReportOptions options;
    options.structures  = reportStructures();
    options.plans_named = reportPlans();
    options.threads     = threads;
    if (!user_names.empty())
        options.user = [names = user_names](std::string_view name) { return isUserName(name, names); };
    auto report =
        study::makeOpeningReport(sources, position, options, progress, cancel, [&](const study::OpeningReport& r) {
            const std::lock_guard lock(shared.mutex);
            shared.report = r;
            ++shared.serial;
        });
    const std::lock_guard lock(shared.mutex);
    if (report.cancelled) {
        if (shared.report)
            shared.report->cancelled = true;
        else
            shared.report = std::move(report);
        ++shared.serial;
    }
}

int scoreDifference(double score) {
    static constexpr int DP[51] = {0,   7,   14,  21,  29,  36,  43,  50,  57,  65,  72,  80,  87,  95,  102, 110, 117,
                                   125, 133, 141, 149, 158, 166, 175, 184, 193, 202, 211, 220, 230, 240, 251, 262, 273,
                                   284, 296, 309, 322, 336, 351, 366, 383, 401, 422, 444, 470, 501, 538, 589, 677, 800};
    const bool below            = score < 0.5;
    const int at                = static_cast<int>(std::lround(std::abs((below ? 1 - score : score) - 0.5) * 100));
    const int dp                = DP[std::clamp(at, 0, 50)];
    return below ? -dp : dp;
}

std::vector<ReportRow> reportRows(const chess::Board& board, const study::ReportMoves& moves,
                                  const std::vector<std::string>& base_names,
                                  const std::vector<std::pair<std::string, std::vector<ReportLichessMove>>>& lichess) {
    struct Sum {
        ReportRow row;
        uint64_t white = 0, draws = 0, black = 0;
        uint64_t white_rated = 0, white_elo = 0, black_rated = 0, black_elo = 0;
        uint32_t last = 0;
    };
    std::vector<Sum> sums;
    study::ScanBoard scan;
    if (!scan.set(board)) return {};
    for (std::size_t i = 0; i < moves.moves.size(); ++i) {
        const auto& m    = moves.moves[i];
        const auto moved = study::moveOfCode(board, scan, m.code);
        if (!moved) continue;
        Sum s;
        s.row.move    = *moved;
        s.row.san     = chess::uci::moveToSan(board, *moved);
        s.row.code    = m.code;
        s.row.games   = m.stats.games;
        s.white       = m.stats.white;
        s.draws       = m.stats.draws;
        s.black       = m.stats.black;
        s.white_rated = m.stats.white_rated;
        s.white_elo   = m.stats.white_elo;
        s.black_rated = m.stats.black_rated;
        s.black_elo   = m.stats.black_elo;
        s.last        = m.stats.last;
        if (i < moves.move_per_source.size())
            for (std::size_t b = 0; b < moves.move_per_source[i].size() && b < base_names.size(); ++b)
                if (moves.move_per_source[i][b])
                    s.row.by_source.emplace_back(base_names[b], moves.move_per_source[i][b]);
        sums.push_back(std::move(s));
    }
    for (const auto& [source, list] : lichess) {
        for (const auto& lm : list) {
            chess::Move move = chess::Move::NO_MOVE;
            try {
                move = chess::uci::uciToMove(board, lm.uci);
            } catch (...) {
            }
            if (move == chess::Move::NO_MOVE) continue;
            auto it = std::find_if(sums.begin(), sums.end(), [&](const Sum& s) { return s.row.move == move; });
            if (it == sums.end()) {
                Sum s;
                s.row.move = move;
                s.row.san  = chess::uci::moveToSan(board, move);
                sums.push_back(std::move(s));
                it = std::prev(sums.end());
            }
            const auto n = lm.white + lm.draws + lm.black;
            it->row.games += n;
            it->white += lm.white;
            it->draws += lm.draws;
            it->black += lm.black;
            // The explorer gives one mean rating for both players: it stands for each side's.
            if (lm.rating > 0) {
                it->white_rated += n;
                it->white_elo += n * static_cast<uint64_t>(lm.rating);
                it->black_rated += n;
                it->black_elo += n * static_cast<uint64_t>(lm.rating);
            }
            if (n) it->row.by_source.emplace_back(source, n);
        }
    }
    uint64_t total = 0;
    for (const auto& s : sums) total += s.row.games;
    const bool white_side = board.sideToMove() == chess::Color::WHITE;
    std::vector<ReportRow> out;
    for (auto& s : sums) {
        auto& row          = s.row;
        const auto decided = s.white + s.draws + s.black;
        if (decided > 0) {
            row.white = static_cast<double>(s.white) / static_cast<double>(decided);
            row.draws = static_cast<double>(s.draws) / static_cast<double>(decided);
            row.black = static_cast<double>(s.black) / static_cast<double>(decided);
            const double score =
                (static_cast<double>(white_side ? s.white : s.black) + s.draws / 2.0) / static_cast<double>(decided);
            row.score            = static_cast<int>(std::lround(100 * score));
            const auto own_rated = white_side ? s.white_rated : s.black_rated;
            const auto own_sum   = white_side ? s.white_elo : s.black_elo;
            const auto opp_rated = white_side ? s.black_rated : s.white_rated;
            const auto opp_sum   = white_side ? s.black_elo : s.white_elo;
            if (own_rated) row.elo = static_cast<int>(own_sum / own_rated);
            if (opp_rated) row.perf = static_cast<int>(opp_sum / opp_rated) + scoreDifference(score);
        }
        row.last = static_cast<int>(s.last >> 9);
        out.push_back(std::move(row));
    }
    std::stable_sort(out.begin(), out.end(), [](const ReportRow& a, const ReportRow& b) { return a.games > b.games; });
    // The moves of a hundredth of the games at least, ten of them always.
    std::vector<ReportRow> kept;
    for (auto& r : out)
        if (kept.size() < 10 || r.games * 100 >= total) kept.push_back(std::move(r));
    return kept;
}

std::vector<std::optional<ReportEval>> evaluateReport(const ReportEvalAsk& ask, std::atomic<int>& done,
                                                      const std::atomic<bool>& cancel, std::string* error) {
    study::enterBackground();  // the engine started from here runs at the lowest priority too
    std::vector<std::optional<ReportEval>> out(ask.fens.size());
    std::unique_ptr<EngineOracle> oracle;
    std::size_t searched = 0;
    for (std::size_t i = 0; i < ask.fens.size() && !cancel; ++i) {
        chess::Board board(ask.fens[i]);
        if (ask.lookup)
            if (const auto e = findEval(*ask.lookup, board)) {
                ReportEval r;
                if (e->mate)
                    r.mate = e->mate;
                else
                    r.cp = e->cp;
                r.depth = e->depth;
                out[i]  = r;
                ++done;
                continue;
            }
        if (ask.engine && searched < ask.engine_most) {
            if (!oracle) oracle = std::make_unique<EngineOracle>(*ask.engine, ask.nodes);
            const auto pos = commentary::Position::fromFen(ask.fens[i]);
            if (pos) {
                ++searched;
                if (const auto s = oracle->search(*pos, {})) {
                    ReportEval r;
                    r.engine         = true;
                    r.depth          = static_cast<int>(s->depth);
                    const bool white = board.sideToMove() == chess::Color::WHITE;
                    if (s->score.kind == commentary::Score::Kind::Mate)
                        r.mate = white ? s->score.value : -s->score.value;
                    else
                        r.cp = white ? s->score.value : -s->score.value;
                    out[i] = r;
                } else if (error && error->empty()) {
                    *error = oracle->error();
                }
            }
        }
        ++done;
    }
    return out;
}

std::string numberedSan(const chess::Board& before, const chess::Move& move, commentary::Lang lang) {
    auto san = chess::uci::moveToSan(before, move);
    if (lang == commentary::Lang::Fr) san = frenchSan(std::move(san));
    const auto n = before.fullMoveNumber();
    return before.sideToMove() == chess::Color::WHITE ? fmt::format("{}.{}", n, san) : fmt::format("{}…{}", n, san);
}

std::vector<chess::Move> movesOfCodes(const chess::Board& from, const std::vector<int>& codes) {
    std::vector<chess::Move> out;
    auto board = from;
    for (const auto code : codes) {
        study::ScanBoard scan;
        if (!scan.set(board)) break;
        const auto m = study::moveOfCode(board, scan, code);
        if (!m) break;
        out.push_back(*m);
        board.makeMove(*m);
    }
    return out;
}

namespace {

int percentOf(uint64_t part, uint64_t whole) {
    return whole ? static_cast<int>(std::lround(100.0 * static_cast<double>(part) / static_cast<double>(whole))) : 0;
}

std::optional<int> scoreOf(const study::TreeStats& s, bool white) {
    const auto decided = s.white + s.draws + s.black;
    if (!decided) return std::nullopt;
    return percentOf(2 * (white ? s.white : s.black) + s.draws, 2 * decided);
}

std::string squareName(uint8_t sq) {
    return fmt::format("{}{}", static_cast<char>('a' + sq % 8), static_cast<char>('1' + sq / 8));
}

// A man's kind in words, with its article.
std::string manWords(uint8_t kind, commentary::Lang lang) {
    const bool fr = lang == commentary::Lang::Fr;
    switch (kind) {
        case study::KNIGHT:
            return fr ? "le cavalier" : "the knight";
        case study::BISHOP:
            return fr ? "le fou" : "the bishop";
        case study::ROOK:
            return fr ? "la tour" : "the rook";
        case study::QUEEN:
            return fr ? "la dame" : "the queen";
        default:
            return fr ? "le roi" : "the king";
    }
}

}  // namespace

commentary::SummaryFigures summaryFigures(const ReportView& view, commentary::Lang lang) {
    commentary::SummaryFigures f;
    f.opening = view.opening;
    f.mover   = view.board.sideToMove() == chess::Color::WHITE ? commentary::Color::White : commentary::Color::Black;
    if (!view.path.empty()) {
        chess::Board before;
        for (std::size_t i = 0; i + 1 < view.path.size(); ++i) before.makeMove(view.path[i]);
        f.after = numberedSan(before, view.path.back(), lang);
    }
    uint64_t total = 0;
    if (view.rows)
        for (const auto& r : *view.rows) total += r.games;
    f.games = total;
    if (view.rows) {
        for (std::size_t i = 0; i < view.rows->size(); ++i) {
            const auto& r = (*view.rows)[i];
            commentary::SummaryFigures::PlayedMove m;
            m.san   = numberedSan(view.board, r.move, lang);
            m.games = r.games;
            m.share = percentOf(r.games, total);
            if (r.score >= 0) m.score = r.score;
            if (view.evals && i < view.evals->size() && (*view.evals)[i]) {
                const auto& e = *(*view.evals)[i];
                m.cp          = e.cp;
                m.mate        = e.mate;
                m.engine      = e.engine;
            }
            f.moves.push_back(std::move(m));
        }
    }
    if (!view.report) return f;
    const auto& rep = *view.report;
    for (const auto& line : rep.lines) {
        commentary::SummaryFigures::Line l;
        auto board = view.board;
        for (const auto& m : movesOfCodes(view.board, line.codes)) {
            const bool white = board.sideToMove() == chess::Color::WHITE;
            if (white || l.moves.empty()) {
                l.moves.push_back(numberedSan(board, m, lang));
            } else {
                auto san = chess::uci::moveToSan(board, m);
                if (lang == commentary::Lang::Fr) san = frenchSan(std::move(san));
                l.moves.push_back(std::move(san));
            }
            board.makeMove(m);
        }
        l.games       = line.stats.games;
        l.white_score = scoreOf(line.stats, true);
        if (!l.moves.empty()) f.lines.push_back(std::move(l));
    }
    const auto& shapes = rep.shapes;
    if (!shapes.enough) return f;
    const int ply    = (view.board.fullMoveNumber() - 1) * 2 + (view.board.sideToMove() == chess::Color::WHITE ? 0 : 1);
    f.structure_move = ply + static_cast<int>(shapes.ply) == 40 ? 20 : 0;
    for (const auto& s : shapes.structures) {
        if (s.kind < 0 || f.structures.size() >= 3) continue;
        const int share = percentOf(s.stats.games, shapes.counted);
        if (share < 1) continue;
        f.structures.push_back({static_cast<commentary::Formation>(s.kind),
                                s.owner == 0 ? commentary::Color::White : commentary::Color::Black, s.kept, share});
    }
    for (const auto& p : shapes.plans) {
        commentary::SummaryFigures::Plan plan;
        plan.side  = p.side == 0 ? commentary::Color::White : commentary::Color::Black;
        plan.name  = static_cast<commentary::PlanName>(p.named);
        plan.route = p.kind == study::ReportPlan::Kind::Route;
        for (const auto sq : p.squares) plan.squares.push_back(squareName(sq));
        if (plan.route) plan.man = manWords(p.man, lang);
        plan.share = percentOf(p.stats.games, shapes.replayed);
        plan.score = scoreOf(p.stats, p.side == 0);
        f.plans.push_back(std::move(plan));
    }
    return f;
}

namespace {

std::filesystem::path explorerFile(const std::filesystem::path& profile, const std::string& key) {
    return profile / "lichess-cache" / "explorer" / fmt::format("{:016x}.json", fnv1a(key));
}

}  // namespace

void keepExplorer(const std::filesystem::path& profile, const std::string& key, const lichess::Explorer& answer) {
    nlohmann::json moves = nlohmann::json::array();
    for (const auto& m : answer.moves)
        moves.push_back({{"uci", m.uci},
                         {"san", m.san},
                         {"white", m.white},
                         {"draws", m.draws},
                         {"black", m.black},
                         {"rating", m.average_rating}});
    const auto now =
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    const nlohmann::json j{{"key", key},
                           {"when", now},
                           {"white", answer.white},
                           {"draws", answer.draws},
                           {"black", answer.black},
                           {"eco", answer.eco},
                           {"opening", answer.opening},
                           {"moves", moves}};
    const auto file = explorerFile(profile, key);
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    try {
        tour::replaceFile(file, j.dump());
    } catch (const std::exception&) {
    }
}

std::optional<std::pair<lichess::Explorer, int64_t>> keptExplorer(const std::filesystem::path& profile,
                                                                  const std::string& key) {
    // Entry by entry (#143): a figure or a move of another type is set aside and said, the others kept, the file as it
    // was kept beside it; a file cut short is kept aside and said, and the explorer asked again.
    const auto file = explorerFile(profile, key);
    const auto j    = readProfileJson(file);
    if (!j) return std::nullopt;
    if (const auto kept = j->find("key"); kept == j->end() || !kept->is_string() || *kept != key) return std::nullopt;
    ProfileEntries entries(file);
    lichess::Explorer e;
    int64_t when = 0;
    entries.entry("white", [&] { e.white = j->value("white", uint64_t{0}); });
    entries.entry("draws", [&] { e.draws = j->value("draws", uint64_t{0}); });
    entries.entry("black", [&] { e.black = j->value("black", uint64_t{0}); });
    entries.entry("eco", [&] { e.eco = j->value("eco", std::string()); });
    entries.entry("opening", [&] { e.opening = j->value("opening", std::string()); });
    entries.entry("when", [&] { when = j->value("when", int64_t{0}); });
    if (const auto moves = j->find("moves"); moves != j->end()) {
        if (!moves->is_array()) entries.entry("moves", [] { throw std::runtime_error("not a list"); });
        for (std::size_t i = 0; moves->is_array() && i < moves->size(); ++i) {
            entries.entry(fmt::format("moves/#{}", i + 1), [&] {
                const auto& m = (*moves)[i];
                lichess::ExplorerMove move;
                move.uci            = m.value("uci", std::string());
                move.san            = m.value("san", std::string());
                move.white          = m.value("white", uint64_t{0});
                move.draws          = m.value("draws", uint64_t{0});
                move.black          = m.value("black", uint64_t{0});
                move.average_rating = m.value("rating", 0);
                e.moves.push_back(std::move(move));
            });
        }
    }
    entries.sayUnread();
    return std::pair{std::move(e), when};
}

std::string reportUnanswered(const lichess::Failure& failure) {
    if (failure.net == lichess::NetError::Offline) return TR("Offline");
    if (!failure.failed()) {  // no value and no failure: not what the explorer sends
        lichess::Failure unread = failure;
        unread.net              = lichess::NetError::BadAnswer;
        return lichess::gaveNothing(unread);
    }
    return lichess::gaveNothing(failure);
}

ReportSummary writeSummary(commentary::Model* model, const commentary::SummaryFigures& figures, commentary::Lang lang,
                           uint32_t round, const std::atomic<bool>& cancel, std::atomic<int>& done) {
    study::enterBackground();
    const auto started = std::chrono::steady_clock::now();
    ReportSummary out;
    if (model) {
        out.by_model = true;
        out.passages = commentary::writtenSummary(*model, figures, lang, round, &cancel, &done, &out.error);
    } else {
        out.passages = commentary::templateSummary(figures, lang);
    }
    out.cancelled = cancel.load();
    out.seconds   = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    return out;
}

}  // namespace fcg
