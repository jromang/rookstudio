#include "lichess_puzzles.hpp"

#include <algorithm>
#include <cctype>

#include <fmt/include/fmt/format.h>
#include <chess.hpp>

#include "https.hpp"
#include "profile_entries.hpp"
#include "storage.hpp"

namespace fcg::lichess {

namespace fs = std::filesystem;

namespace {

int intOf(const nlohmann::json& j, const char* key, int otherwise = 0) {
    const auto it = j.find(key);
    if (it == j.end() || !it->is_number()) return otherwise;
    return it->is_number_float() ? static_cast<int>(it->get<double>() + 0.5) : it->get<int>();
}

std::optional<PuzzleGlicko> glickoOf(const nlohmann::json& j) {
    const auto it = j.find("glicko");
    if (it == j.end() || !it->is_object()) return std::nullopt;
    PuzzleGlicko g;
    if (const auto r = it->find("rating"); r != it->end() && r->is_number()) g.rating = r->get<double>();
    if (const auto d = it->find("deviation"); d != it->end() && d->is_number()) g.deviation = d->get<double>();
    if (const auto p = it->find("provisional"); p != it->end() && p->is_boolean()) g.provisional = p->get<bool>();
    return g;
}

// An object of Lichess's read by `read`; none when the answer is not an object.
template <typename T, typename Read>
auto objectOf(Read read) {
    return [read](const nlohmann::json& j) { return j.is_object() ? std::optional<T>(read(j)) : std::nullopt; };
}

std::string query(const std::string& angle) {
    return servers().api + "/api/puzzle/batch/" + urlEncode(angle.empty() ? std::string("mix") : angle);
}

nlohmann::json puzzleJson(const study::Puzzle& p) {
    return {{"id", p.id},         {"fen", p.fen},           {"moves", p.moves},
            {"rating", p.rating}, {"rd", p.deviation},      {"popularity", p.popularity},
            {"themes", p.themes}, {"openings", p.openings}, {"game", p.game}};
}

study::Puzzle puzzleFromJson(const nlohmann::json& j) {
    study::Puzzle p;
    p.id         = textOf(j, "id");
    p.fen        = textOf(j, "fen");
    p.rating     = intOf(j, "rating", 1500);
    p.deviation  = intOf(j, "rd", 75);
    p.popularity = intOf(j, "popularity", 0);
    p.game       = textOf(j, "game");
    for (const char* key : {"moves", "themes", "openings"}) {
        const auto it = j.find(key);
        if (it == j.end() || !it->is_array()) continue;
        auto& out = std::string_view(key) == "moves"    ? p.moves
                    : std::string_view(key) == "themes" ? p.themes
                                                        : p.openings;
        for (const auto& v : *it)
            if (v.is_string()) out.push_back(v.get<std::string>());
    }
    return p;
}

}  // namespace

std::optional<study::Puzzle> readPuzzleAndGame(const nlohmann::json& j) {
    if (!j.is_object()) return std::nullopt;
    const auto game   = j.find("game");
    const auto puzzle = j.find("puzzle");
    if (game == j.end() || !game->is_object() || puzzle == j.end() || !puzzle->is_object()) return std::nullopt;
    study::Puzzle out;
    out.id        = textOf(*puzzle, "id");
    out.rating    = intOf(*puzzle, "rating", 1500);
    out.deviation = 75;  // the API does not give it
    out.game      = textOf(*game, "id");
    if (out.id.empty()) return std::nullopt;
    if (const auto it = puzzle->find("themes"); it != puzzle->end() && it->is_array())
        for (const auto& t : *it)
            if (t.is_string()) out.themes.push_back(t.get<std::string>());
    // The game's moves from the start: the last one is the opponent's, the solution follows.
    const auto pgn = textOf(*game, "pgn");
    std::vector<std::string> sans;
    for (std::size_t from = 0; from < pgn.size();) {
        const auto space = pgn.find(' ', from);
        auto word        = pgn.substr(from, space == std::string::npos ? std::string::npos : space - from);
        if (!word.empty() && !std::isdigit(static_cast<unsigned char>(word[0]))) sans.push_back(std::move(word));
        if (space == std::string::npos) break;
        from = space + 1;
    }
    if (sans.empty()) return std::nullopt;
    try {
        chess::Board board;
        for (std::size_t i = 0; i + 1 < sans.size(); ++i) {
            const auto m = chess::uci::parseSan(board, sans[i]);
            if (m == chess::Move::NO_MOVE) return std::nullopt;
            board.makeMove(m);
        }
        out.fen      = board.getFen();
        const auto m = chess::uci::parseSan(board, sans.back());
        if (m == chess::Move::NO_MOVE) return std::nullopt;
        out.moves.push_back(chess::uci::moveToUci(m));
        board.makeMove(m);
        const auto solution = puzzle->find("solution");
        if (solution == puzzle->end() || !solution->is_array() || solution->empty()) return std::nullopt;
        for (const auto& s : *solution) {
            if (!s.is_string()) return std::nullopt;
            const auto move = chess::uci::uciToMove(board, s.get<std::string>());
            chess::Movelist legal;
            chess::movegen::legalmoves(legal, board);
            if (std::find(legal.begin(), legal.end(), move) == legal.end()) return std::nullopt;
            out.moves.push_back(chess::uci::moveToUci(move));
            board.makeMove(move);
        }
    } catch (const std::exception&) {
        return std::nullopt;
    }
    return out;
}

PuzzleBatch readPuzzleBatch(const nlohmann::json& j) {
    PuzzleBatch out;
    if (!j.is_object()) return out;
    out.glicko = glickoOf(j);
    if (const auto it = j.find("puzzles"); it != j.end() && it->is_array()) {
        for (const auto& p : *it) {
            if (auto puzzle = readPuzzleAndGame(p))
                out.puzzles.push_back(std::move(*puzzle));
            else
                ++out.unread;
        }
    }
    return out;
}

PuzzleSolved readPuzzleSolved(const nlohmann::json& j) {
    PuzzleSolved out;
    out.next = readPuzzleBatch(j);
    if (const auto it = j.find("rounds"); it != j.end() && it->is_array()) {
        for (const auto& r : *it) {
            if (!r.is_object()) continue;
            PuzzleRound round;
            round.id = textOf(r, "id");
            if (const auto w = r.find("win"); w != r.end() && w->is_boolean()) round.win = w->get<bool>();
            round.rating_diff = intOf(r, "ratingDiff");
            if (!round.id.empty()) out.rounds.push_back(std::move(round));
        }
    }
    return out;
}

Answer<PuzzleBatch> puzzleBatch(const std::string& token, const std::string& angle, int nb,
                                const std::string& difficulty, const std::atomic<bool>* cancel) {
    const auto url = fmt::format("{}?nb={}&difficulty={}", query(angle), std::clamp(nb, 1, PUZZLE_BATCH),
                                 urlEncode(difficulty.empty() ? std::string("normal") : difficulty));
    return call<PuzzleBatch>(request("GET", url, token), objectOf<PuzzleBatch>(readPuzzleBatch), cancel);
}

Answer<PuzzleSolved> solvePuzzles(const std::string& token, const std::string& angle,
                                  const std::vector<PuzzleSolution>& solutions, int nb, const std::string& difficulty,
                                  const std::atomic<bool>* cancel) {
    auto list = nlohmann::json::array();
    for (const auto& s : solutions) list.push_back({{"id", s.id}, {"win", s.win}, {"rated", s.rated}});
    const auto url = fmt::format("{}?nb={}&difficulty={}", query(angle), std::clamp(nb, 0, PUZZLE_BATCH),
                                 urlEncode(difficulty.empty() ? std::string("normal") : difficulty));
    return call<PuzzleSolved>(withJson(request("POST", url, token), {{"solutions", std::move(list)}}),
                              objectOf<PuzzleSolved>(readPuzzleSolved), cancel);
}

PuzzleDashboard readPuzzleDashboard(const nlohmann::json& j) {
    PuzzleDashboard out;
    if (!j.is_object()) return out;
    out.days = intOf(j, "days");
    if (const auto g = j.find("global"); g != j.end() && g->is_object()) {
        out.nb         = intOf(*g, "nb");
        out.first_wins = intOf(*g, "firstWins");
    }
    if (const auto t = j.find("themes"); t != j.end() && t->is_object()) {
        for (const auto& [key, theme] : t->items()) {
            if (!theme.is_object()) continue;
            const auto r = theme.find("results");
            if (r == theme.end() || !r->is_object()) continue;
            out.themes.push_back({key, intOf(*r, "nb"), intOf(*r, "firstWins")});
        }
    }
    std::stable_sort(out.themes.begin(), out.themes.end(), [](const auto& a, const auto& b) { return a.nb > b.nb; });
    return out;
}

Answer<PuzzleDashboard> puzzleDashboard(const std::string& token, int days, const std::atomic<bool>* cancel) {
    return call<PuzzleDashboard>(
        request("GET", fmt::format("{}/api/puzzle/dashboard/{}", servers().api, std::max(1, days)), token),
        objectOf<PuzzleDashboard>(readPuzzleDashboard), cancel);
}

void OnlineBatch::answer(const std::string& id, bool win) {
    if (answered(id)) return;
    results.push_back({id, win, rated});
}

bool OnlineBatch::answered(const std::string& id) const {
    return std::any_of(results.begin(), results.end(), [&](const auto& r) { return r.id == id; });
}

std::optional<OnlineBatch> loadOnlineBatch(const fs::path& folder) {
    // Read with its types checked (#136): a value of another type than written (another version, a hand edit) is set
    // aside, the readable ones kept, said in the console with the file as it was kept beside it; a file cut short is
    // kept aside and said, a new batch then asked of Lichess.
    const auto file = folder / "online.json";
    const auto read = readProfileJson(file);
    if (!read) return std::nullopt;
    const auto& j = *read;
    ProfileEntries entries(file);
    OnlineBatch b;
    entries.entry("angle", [&] { b.angle = j.value("angle", "mix"); });
    entries.entry("difficulty", [&] { b.difficulty = j.value("difficulty", "normal"); });
    entries.entry("rated", [&] { b.rated = j.value("rated", true); });
    entries.entry("received", [&] { b.received = j.value("received", int64_t{0}); });
    if (const auto it = j.find("puzzles"); it != j.end() && it->is_array())
        for (const auto& p : *it) {
            auto puzzle = puzzleFromJson(p);
            if (!puzzle.id.empty() && puzzle.moves.size() >= 2) b.puzzles.push_back(std::move(puzzle));
        }
    if (const auto it = j.find("results"); it != j.end() && it->is_array())
        for (std::size_t i = 0; i < it->size(); ++i) {
            const auto& r = (*it)[i];
            if (!r.is_object()) continue;
            entries.entry(fmt::format("results/{}", i + 1), [&] {
                PuzzleSolution s;
                s.id    = textOf(r, "id");
                s.win   = r.value("win", false);
                s.rated = r.value("rated", true);
                if (!s.id.empty()) b.results.push_back(std::move(s));
            });
        }
    if (const auto it = j.find("glicko"); it != j.end() && it->is_object())
        entries.entry("glicko", [&] {
            b.glicko =
                PuzzleGlicko{it->value("rating", 0.0), it->value("deviation", 0.0), it->value("provisional", false)};
        });
    entries.entry("sent_at", [&] { b.sent_at = j.value("sent_at", int64_t{0}); });
    entries.entry("sent_count", [&] { b.sent_count = j.value("sent_count", 0); });
    entries.entry("sent_status", [&] { b.sent_status = j.value("sent_status", ""); });
    entries.sayUnread();
    return b;
}

bool saveOnlineBatch(const fs::path& folder, const OnlineBatch& b) {
    std::error_code ec;
    fs::create_directories(folder, ec);
    auto puzzles = nlohmann::json::array();
    for (const auto& p : b.puzzles) puzzles.push_back(puzzleJson(p));
    auto results = nlohmann::json::array();
    for (const auto& r : b.results) results.push_back({{"id", r.id}, {"win", r.win}, {"rated", r.rated}});
    nlohmann::json j = {{"angle", b.angle},       {"difficulty", b.difficulty},    {"rated", b.rated},
                        {"received", b.received}, {"puzzles", std::move(puzzles)}, {"results", std::move(results)},
                        {"sent_at", b.sent_at},   {"sent_count", b.sent_count},    {"sent_status", b.sent_status}};
    if (b.glicko)
        j["glicko"] = {
            {"rating", b.glicko->rating}, {"deviation", b.glicko->deviation}, {"provisional", b.glicko->provisional}};
    return writeJson(folder / "online.json", j);
}

void forgetOnlineBatch(const fs::path& folder) {
    std::error_code ec;
    fs::remove(folder / "online.json", ec);
}

}  // namespace fcg::lichess
