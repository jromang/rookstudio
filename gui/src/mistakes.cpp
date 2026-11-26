#include "mistakes.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <fstream>
#include <set>
#include <unordered_set>

#include <fmt/include/fmt/format.h>
#include <commentary/review.hpp>
#include <commentary/story.hpp>
#include <commentary/writing.hpp>
#include <json.hpp>
#include <study/import.hpp>
#include <study/query.hpp>

#include "board_text.hpp"
#include "console.hpp"
#include "i18n.hpp"
#include "portable.hpp"
#include "review_bridge.hpp"
#include "storage.hpp"

namespace fs = std::filesystem;

namespace fcg {

namespace {

std::string hexKey(uint64_t key) { return fmt::format("{:016x}", key); }

std::optional<uint64_t> keyOfHex(const std::string& text) {
    try {
        std::size_t used = 0;
        const auto k     = std::stoull(text, &used, 16);
        if (used == text.size()) return k;
    } catch (...) {
    }
    return std::nullopt;
}

std::string uciOf(const chess::Move& move) { return chess::uci::moveToUci(move, false); }

// A copy of a profile file that did not read whole, beside it (".unreadable", then ".unreadable2"…): what was set aside
// stays there when the file is written again; none when it could not be made.
std::optional<fs::path> keptAside(const fs::path& file) {
    std::error_code ec;
    for (int n = 1; n < 100; ++n) {
        const auto aside = fs::path(file).concat(n == 1 ? std::string(".unreadable") : fmt::format(".unreadable{}", n));
        if (fs::exists(aside, ec)) continue;
        if (fs::copy_file(file, aside, ec) && !ec) return aside;
        return std::nullopt;
    }
    return std::nullopt;
}

// A score as the profile keeps it: {"type": "cp" or "mate", "value": n}.
nlohmann::json scoreJson(const fastchess::Score& score) {
    return {{"type", score.type == fastchess::ScoreType::MATE ? "mate" : "cp"}, {"value", score.value}};
}

std::optional<fastchess::Score> scoreOf(const nlohmann::json& j) {
    if (!j.is_object()) return std::nullopt;
    const auto type = j.value("type", "");
    if (type != "cp" && type != "mate") return std::nullopt;
    return fastchess::Score{type == "cp" ? fastchess::ScoreType::CP : fastchess::ScoreType::MATE,
                            j.value("value", int64_t{0})};
}

std::optional<std::array<int64_t, 3>> wdlOf(const nlohmann::json& j) {
    if (!j.is_array() || j.size() != 3) return std::nullopt;
    try {
        return std::array<int64_t, 3>{j[0].get<int64_t>(), j[1].get<int64_t>(), j[2].get<int64_t>()};
    } catch (...) {
        return std::nullopt;
    }
}

nlohmann::json cardJson(const study::fsrs::Card& c) {
    return {{"state", static_cast<int>(c.state)},
            {"step", c.step},
            {"stability", c.stability},
            {"difficulty", c.difficulty},
            {"due", c.due},
            {"last", c.last},
            {"reps", c.reps},
            {"lapses", c.lapses}};
}

study::fsrs::Card cardOf(const nlohmann::json& j) {
    study::fsrs::Card c;
    if (!j.is_object()) return c;
    const int state = j.value("state", 1);
    c.state         = state == 2   ? study::fsrs::State::Review
                      : state == 3 ? study::fsrs::State::Relearning
                                   : study::fsrs::State::Learning;
    c.step          = j.value("step", 0);
    c.stability     = j.value("stability", 0.0);
    c.difficulty    = j.value("difficulty", 0.0);
    c.due           = j.value("due", int64_t{0});
    c.last          = j.value("last", int64_t{0});
    c.reps          = j.value("reps", 0);
    c.lapses        = j.value("lapses", 0);
    return c;
}

nlohmann::json exerciseJson(const MistakeExercise& e) {
    nlohmann::json j = {{"start", e.start_fen},
                        {"moves", e.moves},
                        {"game", e.game_key},
                        {"white", e.white},
                        {"black", e.black},
                        {"event", e.event},
                        {"site", e.site},
                        {"date", e.date},
                        {"time_control", e.time_control},
                        {"database", e.database},
                        {"folder", portable::stored(e.folder.string())},
                        {"id", e.game_id},
                        {"packed_date", e.packed_date},
                        {"played", e.played},
                        {"judgement", static_cast<int>(e.judgement)},
                        {"kind", e.kind},
                        {"engine", e.engine},
                        {"nodes", e.nodes},
                        {"movetime_ms", e.movetime_ms},
                        {"reviewed", e.reviewed},
                        {"best", e.best},
                        {"line", e.line},
                        {"reply", e.reply},
                        {"comment", e.comment},
                        {"comment_lang", e.comment_lang},
                        {"card", cardJson(e.card)},
                        {"since", e.since}};
    if (e.visit) j["visit"] = true;
    if (e.score) j["score"] = scoreJson(*e.score);
    if (e.wdl) j["wdl"] = *e.wdl;
    if (e.after) j["after"] = scoreJson(*e.after);
    if (e.after_wdl) j["after_wdl"] = *e.after_wdl;
    return j;
}

std::optional<MistakeExercise> exerciseOf(uint64_t key, const nlohmann::json& j) {
    if (!j.is_object()) return std::nullopt;
    MistakeExercise e;
    e.key       = key;
    e.start_fen = j.value("start", std::string());
    if (const auto it = j.find("moves"); it != j.end() && it->is_array())
        for (const auto& m : *it)
            if (m.is_string()) e.moves.push_back(m.get<std::string>());
    e.game_key     = j.value("game", std::string());
    e.white        = j.value("white", std::string());
    e.black        = j.value("black", std::string());
    e.event        = j.value("event", std::string());
    e.site         = j.value("site", std::string());
    e.date         = j.value("date", std::string());
    e.time_control = j.value("time_control", std::string());
    e.database     = j.value("database", std::string());
    e.folder       = fs::path(portable::resolved(j.value("folder", std::string())));
    e.game_id      = j.value("id", uint64_t{0});
    e.packed_date  = j.value("packed_date", uint32_t{0});
    e.played       = j.value("played", std::string());
    const int jd   = j.value("judgement", 2);
    e.judgement    = jd >= 3 ? Judgement::Blunder : Judgement::Mistake;
    e.kind         = j.value("kind", std::string());
    e.engine       = j.value("engine", std::string());
    e.nodes        = j.value("nodes", uint64_t{0});
    e.movetime_ms  = j.value("movetime_ms", int64_t{0});
    e.reviewed     = j.value("reviewed", int64_t{0});
    e.best         = j.value("best", std::string());
    e.line         = j.value("line", std::string());
    e.reply        = j.value("reply", std::string());
    e.comment      = j.value("comment", std::string());
    e.comment_lang = j.value("comment_lang", std::string());
    if (const auto it = j.find("score"); it != j.end()) e.score = scoreOf(*it);
    if (const auto it = j.find("wdl"); it != j.end()) e.wdl = wdlOf(*it);
    if (const auto it = j.find("after"); it != j.end()) e.after = scoreOf(*it);
    if (const auto it = j.find("after_wdl"); it != j.end()) e.after_wdl = wdlOf(*it);
    if (const auto it = j.find("card"); it != j.end()) e.card = cardOf(*it);
    e.since = j.value("since", int64_t{0});
    e.visit = j.value("visit", false);
    // An exercise must play: its position and its best move.
    try {
        const auto b = e.board();
        if (b.hash() != key || chess::uci::uciToMove(b, e.best) == chess::Move::NO_MOVE || e.played.empty())
            return std::nullopt;
    } catch (const std::exception&) {
        return std::nullopt;
    }
    return e;
}

// Whether a review searched deep enough to name a best move.
bool deepEnough(const GameReview& review) {
    if (review.nodes > 0) return review.nodes >= EXERCISE_NODES;
    if (review.depth > 0) return review.depth >= 14;
    return review.movetime_ms >= EXERCISE_MOVETIME_MS;
}

bool legalMove(const chess::Board& board, const std::string& uci) {
    if (uci.empty()) return false;
    try {
        const auto m = chess::uci::uciToMove(board, uci);
        chess::Movelist legal;
        chess::movegen::legalmoves(legal, board);
        return m != chess::Move::NO_MOVE && std::find(legal.begin(), legal.end(), m) != legal.end();
    } catch (const std::exception&) {
        return false;
    }
}

int64_t fileSeconds(const fs::path& file) {
    std::error_code ec;
    const auto written = fs::last_write_time(file, ec);
    if (ec) return 0;
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::file_clock::to_sys(written).time_since_epoch())
        .count();
}

// The game of a database, as the Board page reads it (its review's key is the same).
std::optional<ReplayGame> gameOf(const study::Snapshot& snapshot, uint64_t id) {
    std::string error;
    return readPgnGame(study::importedPgn(snapshot.game(id)), error);
}

}  // namespace

// ---------------------------------------------------------------------------
// Your names

std::vector<std::string> nameWords(std::string_view name) {
    const auto folded = study::foldText(name);
    std::vector<std::string> out;
    std::string word;
    for (const char c : folded) {
        const auto u = static_cast<unsigned char>(c);
        if ((u >= 'a' && u <= 'z') || (u >= '0' && u <= '9') || u >= 0x80) {
            word += c;
        } else if (!word.empty()) {
            out.push_back(std::move(word));
            word.clear();
        }
    }
    if (!word.empty()) out.push_back(std::move(word));
    return out;
}

bool isUserName(std::string_view player, const std::vector<std::string>& names) {
    const auto have = nameWords(player);
    if (have.empty()) return false;
    for (const auto& name : names) {
        const auto want = nameWords(name);
        if (want.empty()) continue;
        if (std::all_of(want.begin(), want.end(),
                        [&](const auto& w) { return std::find(have.begin(), have.end(), w) != have.end(); }))
            return true;
    }
    return false;
}

std::optional<chess::Color> userSide(std::string_view white, std::string_view black,
                                     const std::vector<std::string>& names) {
    const bool w = isUserName(white, names), b = isUserName(black, names);
    if (w == b) return std::nullopt;
    return w ? chess::Color::WHITE : chess::Color::BLACK;
}

// ---------------------------------------------------------------------------
// The exercises

chess::Board MistakeExercise::board() const {
    chess::Board b(start_fen.empty() ? std::string(chess::constants::STARTPOS) : start_fen);
    for (const auto& m : moves) b.makeMove(chess::uci::uciToMove(b, m));
    return b;
}

chess::Color MistakeExercise::side() const {
    // From the start's side to move and the moves after it, without playing them.
    const std::string_view fen = start_fen.empty() ? chess::constants::STARTPOS : std::string_view(start_fen);
    const auto space           = fen.find(' ');
    const bool white_starts    = space == std::string_view::npos || space + 1 >= fen.size() || fen[space + 1] != 'b';
    return white_starts == (moves.size() % 2 == 0) ? chess::Color::WHITE : chess::Color::BLACK;
}

AnalysisPosition MistakeExercise::position() const {
    return {start_fen.empty() ? std::string(chess::constants::STARTPOS) : start_fen, moves, false};
}

SearchLimit MistakeExercise::limit() const {
    if (nodes > 0) return {0, 0, nodes};
    return {std::max<int64_t>(movetime_ms, EXERCISE_MOVETIME_MS), 0, 0};
}

PositionEval MistakeExercise::eval() const {
    PositionEval e;
    e.score = score;
    e.wdl   = wdl;
    e.best  = best;
    e.pv    = line.empty() ? best : line;
    return e;
}

std::string reviewSignature(const GameReview& review) {
    return fmt::format("{}|{}|{}|{}|{}|{}", review.engine, review.passes, review.nodes, review.movetime_ms,
                       review.depth, review.positions.size());
}

namespace {

struct KindInfo {
    std::string_view key;
    const char* name;
    std::string_view theme;
};

constexpr KindInfo KINDS[] = {
    {"en_prise", N_("Pieces left en prise"), "hangingPiece"},
    {"fork", N_("Forks"), "fork"},
    {"pin", N_("Pins"), "pin"},
    {"skewer", N_("Skewers"), "skewer"},
    {"discovery", N_("Discovered attacks"), "discoveredAttack"},
    {"guard", N_("Defenders drawn away"), "deflection"},
    {"trapped", N_("Trapped pieces"), "trappedPiece"},
    {"back_rank", N_("Back-rank mates"), "backRankMate"},
    {"tactics", N_("The opponent's tactics"), ""},
    {"king", N_("King safety"), "exposedKing"},
    {"missed", N_("Missed chances"), ""},
    {"conversion", N_("Converting an advantage"), ""},
    {"pawn_endgame", N_("Pawn endgames"), "pawnEndgame"},
    {"rook_endgame", N_("Rook endgames"), "rookEndgame"},
    {"queen_endgame", N_("Queen endgames"), "queenEndgame"},
    {"minor_endgame", N_("Minor-piece endgames"), "bishopEndgame"},
    {"bishops_endgame", N_("Opposite-coloured bishops"), "bishopEndgame"},
    {"endgame", N_("Endgames"), "endgame"},
    {"opening", N_("The opening"), "opening"},
};

}  // namespace

const char* mistakeKindName(std::string_view key) {
    for (const auto& k : KINDS)
        if (k.key == key) return k.name;
    return key.empty() ? N_("Others") : "";
}

std::string_view mistakeKindTheme(std::string_view key) {
    for (const auto& k : KINDS)
        if (k.key == key) return k.theme;
    return {};
}

uint32_t packedDaysBefore(int64_t now, int days) {
    const auto t = static_cast<std::time_t>(now - static_cast<int64_t>(days) * study::fsrs::DAY);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    return static_cast<uint32_t>(tm.tm_year + 1900) << 9 | static_cast<uint32_t>(tm.tm_mon + 1) << 5 |
           static_cast<uint32_t>(tm.tm_mday);
}

std::vector<MistakeExercise> exercisesOf(const ReplayGame& game, const GameReview& review, chess::Color side,
                                         commentary::Lang lang) {
    std::vector<MistakeExercise> out;
    if (!review.complete(game.moves.size()) || !deepEnough(review)) return out;
    const auto cr = commentaryReview(game, false, review);
    if (!cr) return out;
    const auto judged = judgedMoves(*cr);
    const auto mine   = side == chess::Color::WHITE ? commentary::Color::White : commentary::Color::Black;
    // Each mistake's kind: the topic of the review's "To work on" that quotes it.
    std::map<std::size_t, std::string> kinds;
    const std::string result = game.result.empty() ? "*" : game.result;
    for (const auto& w : commentary::workOn(cr->line, result, cr->report, cr->analyses, nullptr, commentary::Lang::En,
                                            commentary::PieceLetters::English)) {
        if (w.side != mine) continue;
        for (const auto& t : w.topics)
            for (const auto p : t.plies) kinds.try_emplace(p, t.key);
    }
    const auto start = game.start_fen.empty() ? std::string(chess::constants::STARTPOS) : game.start_fen;
    chess::Board board(start);
    std::vector<std::string> moves;
    std::vector<std::size_t> plies;  // of the exercises, for their comments
    for (std::size_t i = 0; i < game.moves.size(); ++i) {
        const auto& m      = game.moves[i];
        const auto played  = uciOf(m.move);
        const bool mistake = i < judged.size() && m.mover == side && judged[i].judgement >= Judgement::Mistake &&
                             i + 1 < review.positions.size();
        if (mistake) {
            const auto* report = cr->report.get(i + 1);
            const auto& before = review.positions[i];
            const auto& after  = review.positions[i + 1];
            if (report && before.score && before.best != played && legalMove(board, before.best) &&
                report->win_before >= EXERCISE_LOST_ANYWAY && report->win_after <= EXERCISE_STILL_WINNING) {
                MistakeExercise e;
                e.key          = board.hash();
                e.start_fen    = start;
                e.moves        = moves;
                e.game_key     = reviewKey(game);
                e.white        = game.header("White");
                e.black        = game.header("Black");
                e.event        = game.header("Event");
                e.site         = game.header("Site");
                e.date         = game.header("Date");
                e.time_control = game.header("TimeControl");
                if (e.date.empty() || e.date.find('?') != std::string::npos) {
                    if (const auto utc = game.header("UTCDate"); !utc.empty()) e.date = utc;
                }
                e.packed_date = study::packDate(e.date);
                e.played      = played;
                e.judgement   = judged[i].judgement;
                if (const auto k = kinds.find(i + 1); k != kinds.end()) e.kind = k->second;
                e.engine      = review.engine;
                e.nodes       = review.nodes;
                e.movetime_ms = review.nodes > 0 ? 0 : review.movetime_ms;
                e.best        = before.best;
                e.line        = before.pv.empty() ? before.best : before.pv;
                e.score       = before.score;
                e.wdl         = before.wdl;
                e.reply       = after.pv.empty() ? after.best : after.pv;
                e.after       = after.score;
                e.after_wdl   = after.wdl;
                out.push_back(std::move(e));
                plies.push_back(i + 1);
            }
        }
        moves.push_back(played);
        board.makeMove(m.move);
    }
    // What the review said of each mistake, as the Board page shows it.
    if (!out.empty()) {
        const auto letters =
            lang == commentary::Lang::Fr ? commentary::PieceLetters::French : commentary::PieceLetters::English;
        const auto comments =
            commentary::heldTemplateComments(cr->line, cr->report, cr->analyses, {}, nullptr, nullptr, lang, letters);
        for (std::size_t k = 0; k < out.size(); ++k) {
            for (const auto& [ply, text] : comments)
                if (ply == plies[k]) out[k].comment = text;
            out[k].comment_lang = lang == commentary::Lang::Fr ? "fr" : "en";
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// The answers

AnswerCheck checkAnswer(const MistakeExercise& exercise, const chess::Move& move, const PositionEval& searched) {
    AnswerCheck out;
    const auto board = exercise.board();
    const auto start = commentary::Position::fromFen(board.getFen());
    if (!start) return out;
    const auto mv = start->parseUci(uciOf(move));
    if (!mv) return out;
    const commentary::MainLine line(*start, {*mv});
    GameReview review;
    review.positions    = {exercise.eval(), searched};
    const auto analyses = analysesOf(line, review);
    if (analyses.size() < 2 || !analyses[0] || !analyses[1]) return out;
    const auto mover = board.sideToMove() == chess::Color::WHITE ? commentary::Color::White : commentary::Color::Black;
    const auto w     = commentary::weigh(*analyses[0], *analyses[1], mover, *mv);
    if (!w) return out;
    out.judged  = true;
    out.cp_loss = w->cp_loss;
    out.before  = w->win_before;
    out.after   = w->win_after;
    if (w->judgment)
        out.judgement = *w->judgment == commentary::Judgment::Blunder   ? Judgement::Blunder
                        : *w->judgment == commentary::Judgment::Mistake ? Judgement::Mistake
                                                                        : Judgement::Inaccuracy;
    out.right = w->played_best || w->cp_loss <= ANSWER_CP || (!w->judgment && w->win_after >= ANSWER_WINS);
    return out;
}

std::optional<PositionEval> endedAfter(const MistakeExercise& exercise, const chess::Move& move) {
    auto board = exercise.board();
    board.makeMove(move);
    const auto score = endScore(board);
    if (!score) return std::nullopt;
    PositionEval e;
    e.score = score;
    return e;
}

// ---------------------------------------------------------------------------
// What the profile keeps

void MistakeRecord::keep(const MistakeExercise& exercise) {
    if (!exercise.card.fresh()) kept[exercise.key] = {exercise.card, exercise.since};
}

bool MistakeRecord::giveBack(MistakeExercise& exercise) {
    const auto it = kept.find(exercise.key);
    if (it == kept.end()) return false;
    exercise.card  = it->second.card;
    exercise.since = it->second.since;
    kept.erase(it);
    return true;
}

MistakeRecord readMistakeRecord(const fs::path& folder) {
    MistakeRecord out;
    const auto file = folder / "exercises.json";
    const auto json = readJson<nlohmann::json>(file);
    std::error_code ec;
    if (!json || !json->is_object()) {
        // There but unreadable (cut short, written by hand): kept aside, the exercises found again from the reviews.
        if (fs::exists(file, ec)) {
            const auto aside = keptAside(file);
            Console::instance().add(aside ? trf("{} could not be read: kept as {}", file.string(), aside->string())
                                          : trf("{} could not be read", file.string()),
                                    true);
        }
        return out;
    }
    // An entry of another type than the one expected (written by another version, edited by hand) is set aside, the
    // readable ones kept (#136): said in the console, the file as it was kept beside it, which the next save replaces.
    std::vector<std::string> unread;
    const auto entry = [&](const std::string& what, const auto& read) {
        try {
            read();
        } catch (const std::exception&) {
            unread.push_back(what);
        }
    };
    entry("answer_ms", [&] { out.answer_ms = json->value("answer_ms", int64_t{0}); });
    if (const auto it = json->find("exercises"); it != json->end() && it->is_object())
        for (const auto& [hex, j] : it->items())
            entry("exercises/" + hex, [&] {
                if (const auto key = keyOfHex(hex))
                    if (auto e = exerciseOf(*key, j)) out.exercises.emplace(*key, std::move(*e));
            });
    if (const auto it = json->find("games"); it != json->end() && it->is_object()) {
        for (const auto& [game, j] : it->items()) {
            if (!j.is_object()) continue;
            entry("games/" + game, [&] {
                ScannedGame g;
                g.review = j.value("review", std::string());
                if (const auto keys = j.find("keys"); keys != j.end() && keys->is_array())
                    for (const auto& k : *keys)
                        if (k.is_string())
                            if (const auto key = keyOfHex(k.get<std::string>())) g.keys.push_back(*key);
                out.games.emplace(game, std::move(g));
            });
        }
    }
    if (const auto it = json->find("kept"); it != json->end() && it->is_object())
        for (const auto& [hex, j] : it->items())
            entry("kept/" + hex, [&] {
                if (const auto key = keyOfHex(hex); key && j.is_object()) {
                    const KeptCard kept{cardOf(j.value("card", nlohmann::json::object())),
                                        j.value("since", int64_t{0})};
                    out.kept[*key] = kept;  // read whole first: nothing kept of an entry set aside
                }
            });
    if (!unread.empty()) {
        std::string what;
        for (std::size_t i = 0; i < unread.size() && i < 5; ++i) what += (i > 0 ? ", " : "") + unread[i];
        if (unread.size() > 5) what += ", …";
        const auto aside = keptAside(file);
        Console::instance().add(
            trn("{}: {} entry could not be read and was set aside ({}); the file as it was is kept as {}",
                "{}: {} entries could not be read and were set aside ({}); the file as it was is kept as {}",
                unread.size(), file.string(), unread.size(), what, aside ? aside->string() : std::string("-")),
            true);
    }
    return out;
}

bool writeMistakeRecord(const fs::path& folder, const MistakeRecord& record) {
    std::error_code ec;
    fs::create_directories(folder, ec);
    nlohmann::json exercises = nlohmann::json::object();
    for (const auto& [key, e] : record.exercises) exercises[hexKey(key)] = exerciseJson(e);
    nlohmann::json games = nlohmann::json::object();
    for (const auto& [game, g] : record.games) {
        nlohmann::json keys = nlohmann::json::array();
        for (const auto k : g.keys) keys.push_back(hexKey(k));
        games[game] = {{"review", g.review}, {"keys", std::move(keys)}};
    }
    nlohmann::json kept = nlohmann::json::object();
    for (const auto& [key, k] : record.kept) kept[hexKey(key)] = {{"card", cardJson(k.card)}, {"since", k.since}};
    return writeJson(folder / "exercises.json", {{"version", 1},
                                                 {"answer_ms", record.answer_ms},
                                                 {"exercises", std::move(exercises)},
                                                 {"games", std::move(games)},
                                                 {"kept", std::move(kept)}});
}

void appendMistakeAnswer(const fs::path& folder, uint64_t key, int64_t at, study::fsrs::Rating rating, int64_t ms,
                         const std::string& played) {
    std::error_code ec;
    fs::create_directories(folder, ec);
    const nlohmann::json line = {
        {"key", hexKey(key)}, {"at", at}, {"rating", static_cast<int>(rating)}, {"ms", ms}, {"played", played}};
    std::ofstream out(folder / "answers.jsonl", std::ios::binary | std::ios::app);
    out << line.dump() << '\n';
}

MistakeCounts mistakeCounts(const std::vector<const MistakeExercise*>& exercises, const study::TrainingDay& day,
                            std::size_t new_per_day) {
    MistakeCounts c;
    std::set<std::string> games;
    std::size_t begun = 0;  // new ones answered for the first time today
    for (const auto* e : exercises) {
        ++c.exercises;
        games.insert(e->game_key);
        if (e->card.fresh()) {
            ++c.fresh;
            continue;
        }
        if (e->since > day.end - study::fsrs::DAY) ++begun;
        if (e->card.due <= day.end) {
            ++c.due;
        } else {
            ++c.known;
            if (e->card.due <= day.end + study::fsrs::DAY) ++c.tomorrow;
            if (e->card.due <= day.end + 3 * study::fsrs::DAY) ++c.three_days;
        }
    }
    c.games = games.size();
    c.today = std::min(c.fresh, new_per_day > begun ? new_per_day - begun : 0);
    return c;
}

std::vector<uint64_t> mistakeOrder(const std::vector<const MistakeExercise*>& exercises, const study::TrainingDay& day,
                                   std::size_t fresh, bool with_due) {
    std::vector<const MistakeExercise*> due, fresh_ones;
    for (const auto* e : exercises) {
        if (e->card.fresh())
            fresh_ones.push_back(e);
        else if (with_due && e->card.due <= day.end)
            due.push_back(e);
    }
    std::stable_sort(due.begin(), due.end(), [](const auto* a, const auto* b) {
        return a->card.due != b->card.due ? a->card.due < b->card.due : a->key < b->key;
    });
    std::stable_sort(fresh_ones.begin(), fresh_ones.end(), [](const auto* a, const auto* b) {
        if (a->packed_date != b->packed_date) return a->packed_date > b->packed_date;
        if (a->judgement != b->judgement) return a->judgement > b->judgement;
        if (a->game_key != b->game_key) return a->game_key < b->game_key;
        return a->moves.size() < b->moves.size();
    });
    std::vector<uint64_t> out;
    for (const auto* e : due) out.push_back(e->key);
    for (std::size_t i = 0; i < fresh_ones.size() && i < fresh; ++i) out.push_back(fresh_ones[i]->key);
    return out;
}

// ---------------------------------------------------------------------------
// The user's games, looked for

void putExercises(MistakeRecord& record, const std::string& game, const std::string& signature,
                  std::vector<MistakeExercise> exercises) {
    // Those it gave before and no longer gives go, unless another game gives them too.
    if (const auto it = record.games.find(game); it != record.games.end()) {
        for (const auto k : it->second.keys) {
            if (std::any_of(exercises.begin(), exercises.end(), [&](const auto& e) { return e.key == k; })) continue;
            const bool elsewhere = std::any_of(record.games.begin(), record.games.end(), [&](const auto& g) {
                return g.first != game &&
                       std::find(g.second.keys.begin(), g.second.keys.end(), k) != g.second.keys.end();
            });
            if (elsewhere) continue;
            if (const auto e = record.exercises.find(k); e != record.exercises.end()) {
                record.keep(e->second);
                record.exercises.erase(e);
            }
        }
    }
    ScannedGame scanned;
    scanned.review = signature;
    for (auto& e : exercises) {
        scanned.keys.push_back(e.key);
        const auto it = record.exercises.find(e.key);
        if (it != record.exercises.end()) {
            // The same position from another game: the latest game's, its card kept.
            e.card  = it->second.card;
            e.since = it->second.since;
            if (it->second.game_key != game && it->second.packed_date > e.packed_date) continue;
            it->second = std::move(e);
        } else {
            (void)record.giveBack(e);  // an exercise gone that comes back, with its card
            record.exercises.emplace(e.key, std::move(e));
        }
    }
    record.games[game] = std::move(scanned);
}

MistakeScan scanMistakes(const std::vector<MistakeSource>& sources, const std::vector<std::string>& names,
                         const fs::path& analyses, const MistakeRecord& before, std::size_t recent,
                         commentary::Lang lang, const std::atomic<bool>* cancel) {
    MistakeScan out;
    out.record.answer_ms = before.answer_ms;
    out.record.kept      = before.kept;
    const auto stopped   = [&] { return cancel && cancel->load(); };
    // The games reviewed, by their review's key.
    std::unordered_set<std::string> reviewed;
    {
        std::error_code ec;
        for (fs::directory_iterator it(analyses, ec), end; !ec && it != end; it.increment(ec)) {
            const auto& p = it->path();
            if (p.extension() == ".json" && p.stem().string().size() == 16) reviewed.insert(p.stem().string());
        }
    }
    struct Found {
        std::size_t source = 0;
        uint64_t id        = 0;
        std::string key;
        chess::Color side = chess::Color::WHITE;
        uint32_t date     = 0;
    };
    std::vector<Found> found;
    std::unordered_set<std::string> seen;
    for (std::size_t s = 0; s < sources.size() && !stopped(); ++s) {
        const auto& snapshot = sources[s].snapshot;
        if (!snapshot) continue;
        // The games where a player has every word of a name: those the names' words find, then each told by its
        // players' whole words.
        std::set<uint64_t> ids;
        for (const auto& name : names) {
            const auto words = nameWords(name);
            if (words.empty()) continue;
            study::Filter filter;
            for (const auto& w : words) filter.player += (filter.player.empty() ? "" : " ") + w;
            const auto view = study::makeView(snapshot, filter, study::SortBy::Number, false, 2, cancel);
            ids.insert(view.ids.begin(), view.ids.end());
        }
        for (const auto id : ids) {
            if (stopped()) break;
            const auto row = snapshot->row(id);
            if (row.flags & study::GAME_CHESS960) continue;
            const auto side = userSide(row.white, row.black, names);
            if (!side) continue;
            const auto game = gameOf(*snapshot, id);
            if (!game || game->moves.empty()) continue;
            auto key = reviewKey(*game);
            if (!seen.insert(key).second) continue;  // the same game in another database
            found.push_back({s, id, std::move(key), *side, row.date});
        }
    }
    out.cancelled = stopped();
    if (out.cancelled) return out;
    out.games = found.size();
    // The reviewed ones give their exercises: found again when their review changed.
    for (const auto& f : found) {
        if (stopped()) break;
        if (!reviewed.contains(f.key)) continue;
        ++out.reviewed;
        const auto& source = sources[f.source];
        const auto game    = gameOf(*source.snapshot, f.id);
        if (!game) continue;
        const auto review = loadReview(analyses, *game);
        if (!review) continue;
        const auto signature = reviewSignature(*review) + "|" + (lang == commentary::Lang::Fr ? "fr" : "en");
        const auto was       = before.games.find(f.key);
        std::vector<MistakeExercise> exercises;
        bool kept = was != before.games.end() && was->second.review == signature;
        if (kept) {
            for (const auto k : was->second.keys) {
                const auto e = before.exercises.find(k);
                if (e == before.exercises.end()) {
                    kept = false;
                    break;
                }
                exercises.push_back(e->second);
            }
        }
        if (!kept) {
            exercises     = exercisesOf(*game, *review, f.side, lang);
            const auto at = fileSeconds(analyses / (f.key + ".json"));
            for (auto& e : exercises) {
                e.database = source.name;
                e.folder   = source.folder;
                e.game_id  = f.id;
                e.reviewed = at;
            }
        }
        putExercises(out.record, f.key, signature, std::move(exercises));
    }
    // The cards of the exercises found before; those no longer found kept apart.
    for (auto& [key, e] : out.record.exercises) {
        if (const auto it = before.exercises.find(key); it != before.exercises.end()) {
            e.card  = it->second.card;
            e.since = it->second.since;
            out.record.kept.erase(key);
        }
    }
    // A guided visit's cards are no game's exercises: they stay (#94).
    for (const auto& [key, e] : before.exercises)
        if (!out.record.exercises.contains(key)) {
            if (e.visit)
                out.record.exercises.emplace(key, e);
            else
                out.record.keep(e);
        }
    // The latest games without a review.
    std::vector<const Found*> latest;
    for (const auto& f : found) latest.push_back(&f);
    std::stable_sort(latest.begin(), latest.end(), [](const Found* a, const Found* b) {
        if (a->date != b->date) return a->date > b->date;
        if (a->source != b->source) return a->source < b->source;
        return a->id > b->id;
    });
    for (std::size_t i = 0; i < latest.size() && i < recent && !stopped(); ++i) {
        const auto& f = *latest[i];
        if (reviewed.contains(f.key)) continue;
        const auto& source = sources[f.source];
        auto game          = gameOf(*source.snapshot, f.id);
        if (!game) continue;
        UnreviewedGame u;
        u.database = source.name;
        u.folder   = source.folder;
        u.id       = f.id;
        u.game     = std::move(*game);
        u.side     = f.side;
        u.date     = f.date;
        out.unreviewed.push_back(std::move(u));
    }
    out.cancelled = stopped();
    return out;
}

// ---------------------------------------------------------------------------
// Guessing the moves

double scorePawns(const fastchess::Score& score) {
    if (score.type == fastchess::ScoreType::MATE) {
        const double moves = static_cast<double>(std::min<int64_t>(std::abs(score.value), 500));
        return score.value > 0 ? 100.0 - moves / 10.0 : -100.0 + moves / 10.0;
    }
    return static_cast<double>(score.value) / 100.0;
}

int guessPoints(bool same, double game_pawns, double guess_pawns) {
    if (same) return 3;
    const double worse = game_pawns - guess_pawns;
    if (worse <= GUESS_AS_GOOD + 1e-9) return 2;
    if (worse <= GUESS_PLAYABLE + 1e-9) return 1;
    return 0;
}

void appendGuessRecord(const fs::path& folder, const GuessRecord& r) {
    std::error_code ec;
    fs::create_directories(folder, ec);
    const nlohmann::json line = {{"at", r.at},   {"title", r.title}, {"white", r.white}, {"points", r.points},
                                 {"max", r.max}, {"same", r.same},   {"moves", r.moves}, {"loss", r.loss}};
    std::ofstream out(folder / "games.jsonl", std::ios::binary | std::ios::app);
    out << line.dump() << '\n';
}

std::optional<GuessRecord> lastGuessRecord(const fs::path& folder) {
    const auto file = folder / "games.jsonl";
    std::ifstream in(file, std::ios::binary);
    std::optional<GuessRecord> out;
    std::size_t unread = 0;  // lines of another type than expected (#136): left out, said in the console
    for (std::string line; std::getline(in, line);) {
        const auto j = nlohmann::json::parse(line, nullptr, false);
        if (j.is_discarded() || !j.is_object()) continue;
        try {
            GuessRecord r;
            r.at     = j.value("at", int64_t{0});
            r.title  = j.value("title", std::string());
            r.white  = j.value("white", true);
            r.points = j.value("points", 0);
            r.max    = j.value("max", 0);
            r.same   = j.value("same", 0);
            r.moves  = j.value("moves", 0);
            r.loss   = j.value("loss", 0.0);
            if (r.max > 0) out = r;
        } catch (const std::exception&) {
            ++unread;
        }
    }
    if (unread > 0)
        Console::instance().add(trn("{}: {} line could not be read and was left out",
                                    "{}: {} lines could not be read and were left out", unread, file.string(), unread),
                                true);
    return out;
}

}  // namespace fcg
