// The puzzles' module (#107), built and driven without App: a context of the test's own answers what it asks of the
// program (a Board that only keeps the position it is given and the moves played on it, a Training page and a day's
// review that only remember what they were told), Lichess is a server on this machine, and the module's pages are drawn
// in an ImGui context of the test's, a key pressed as the user presses it. Lichess's database prepared from a small
// file of puzzles of our own (random games); a series of it on the Board, as its --focus word starts it: a puzzle set,
// a right answer and a wrong one, both kept in the profile (the rating, the results, the missed one's card of the day's
// review), the Board given back at the end as it was taken; a batch of the Lichess account solved, its answers sent
// back in one request; a token Lichess refuses: the answers kept, sent again only when the user asks.
#include <doctest/doctest.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <random>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <fmt/include/fmt/format.h>
#include <imgui.h>
#include <chess.hpp>
#include <json.hpp>
#include <study/game.hpp>
#include <study/game_tree.hpp>

#include "app_context.hpp"
#include "board_view.hpp"
#include "databases.hpp"
#include "focus_words.hpp"
#include "free_ports.hpp"
#include "guide.hpp"
#include "https.hpp"
#include "jobs.hpp"
#include "lichess_account.hpp"
#include "lichess_puzzles.hpp"
#include "puzzles.hpp"
#include "puzzles_page.hpp"
#include "scripted_server.hpp"
#include "test_support.hpp"
#include "training.hpp"

using namespace fcg;
using namespace fcg::test;
namespace fs = std::filesystem;

namespace {

// What the module asks of App, answered by the test: the profile of a scratch folder, a Board that keeps the position
// the module gives it and plays the moves it is told on it, a Training page, a day's review and file dialogs that only
// count what they were told, the user's account set by the test, a right answer going on by Enter alone.
class FakeApp final : public PuzzlesContext {
   public:
    explicit FakeApp(const fs::path& profile) : profile_(profile), shelf_(jobs_, profile) { settings_.advance_ms = 0; }

    // The program.
    int frame() const override { return frame_; }
    double seconds() const override { return 100; }
    bool shortcutPressed(int) const override { return false; }
    bool scripted() const override { return false; }
    fs::path profileDir() const override { return profile_; }
    DatabaseShelf& shelf() override { return shelf_; }
    void toast(const std::string& text) override { toasts.push_back(text); }
    JobGroup& jobs() override { return jobs_; }

    // The Board taken by a session: what it was before, as the test made it, given back at the end.
    std::string sessionLock() const override { return live_game ? "Locked" : ""; }
    BoardTaken takeBoard(const std::string& title, bool tree) override {
        ++taken;
        board_title    = title;
        board_tree     = tree;
        game           = ReplayGame{};
        game.start_fen = std::string(chess::constants::STARTPOS);
        ply            = 0;
        return before;
    }
    void giveBoardBack(const BoardTaken& board, bool to_training) override {
        given_back.emplace_back(board, to_training);
    }
    void leaveBoard(const BoardTaken& board) override { left.push_back(board); }
    void showSession() override { ++board_page; }
    const ReplayGame& sessionGame() const override { return game; }
    int sessionPly() const override { return ply; }
    void showSessionGame(ReplayGame g) override {
        game = std::move(g);
        ply  = static_cast<int>(game.moves.size());
    }
    void playSessionMove(chess::Move move) override { ply = playOnGame(game, false, ply, move); }
    void setFlip(bool f) override { flip = f; }
    void dropPick() override { ++picks_dropped; }
    void dropTypedMove() override { ++typed_dropped; }
    void beginSessionPage() override { ImGui::TextUnformatted("Board"); }
    void drawBoardMessage() override {}
    void drawSessionBoard(float size, const BoardDecor& marks, bool moves) override {
        board_moves = moves;
        arrows      = marks.arrows.size();
        ImGui::Dummy(ImVec2(size, size));
    }
    void drawMoveField() override {}
    void moveKeys() override {}
    void drawBoardTabs() override { ++tabs_drawn; }
    void drawFairPlayBanner() override {}
    void analyse(bool on) override { analysing = on; }
    bool analysisKey() override { return false; }

    // The Training page and its day's review.
    const TrainingSettings& trainingSettings() override { return settings_; }
    void showTrainingPage(int part) override { training_part = part; }
    void setEnterStartsPuzzles(bool puzzles) override { enter_puzzles = puzzles; }
    bool reviewRunning() const override { return false; }
    bool nextReview() override { return false; }
    void stopReview() override { ++review_stopped; }
    void drawReviewNext(bool) override {}

    // The user's mistakes; the file dialogs.
    std::map<std::string, std::size_t> mistakeKinds(uint32_t) override { return {}; }
    void choosePuzzleFile() override { ++file_dialogs; }

    // The user's Lichess account, its link; fair play.
    const lichess::Account* lichessAccount() const override { return own ? &*own : nullptr; }
    bool lichessLinking() const override { return false; }
    const lichess::Failure& lichessLinkError() const override { return link_error_; }
    void startLichessLink(std::vector<std::string> scopes, std::string) override { links.push_back(std::move(scopes)); }
    std::string scopeAsked() const override { return {}; }
    void askScope(const std::string&) override {}
    void dropScopeAsk() override {}
    const lichess::Profile* lichessProfile() const override { return nullptr; }
    void askLichessProfile() override {}
    void askLichessProfileOnce() override {}
    bool liveGameOn() const override { return live_game; }

    // What the module was told, and what the test sets.
    int frame_ = 0;
    std::vector<std::string> toasts;
    BoardTaken before{true, true, 3};  // the Board before a series: a game put aside, Black below, the Tree's tab
    int taken = 0, board_page = 0, picks_dropped = 0, typed_dropped = 0, tabs_drawn = 0;
    std::string board_title;
    bool board_tree = false, board_moves = false, flip = false, analysing = false;
    std::size_t arrows = 0;
    ReplayGame game;
    int ply = 0;
    std::vector<std::pair<BoardTaken, bool>> given_back;
    std::vector<BoardTaken> left;
    int training_part = -1, review_stopped = 0, file_dialogs = 0;
    bool enter_puzzles = false;
    std::vector<std::vector<std::string>> links;
    std::optional<lichess::Account> own;
    bool live_game = false;

   private:
    fs::path profile_;
    JobGroup jobs_;
    DatabaseShelf shelf_;  // after its jobs: gone before them
    TrainingSettings settings_;
    lichess::Failure link_error_;
};

// An ImGui context of the test's own, without a window: a frame draws a page of the module, a key pressed before it
// (and let go after it, in a frame of its own: Dear ImGui takes one change of a key a frame).
class Frames {
   public:
    Frames() : context_(ImGui::CreateContext()) {
        auto& io = ImGui::GetIO();
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
        io.IniFilename = nullptr;
        io.DisplaySize = ImVec2(1280, 900);
        io.DeltaTime   = 1.0f / 60.0f;
    }
    ~Frames() { ImGui::DestroyContext(context_); }
    Frames(const Frames&)            = delete;
    Frames& operator=(const Frames&) = delete;

    void draw(FakeApp& app, const std::function<void()>& page, ImGuiKey key = ImGuiKey_None) {
        if (key != ImGuiKey_None) ImGui::GetIO().AddKeyEvent(key, true);
        frame(app, page);
        if (key != ImGuiKey_None) {
            ImGui::GetIO().AddKeyEvent(key, false);
            frame(app, page);
        }
    }

   private:
    static void frame(FakeApp& app, const std::function<void()>& page) {
        ++app.frame_;
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
        ImGui::Begin("page", nullptr, ImGuiWindowFlags_NoDecoration);
        page();
        ImGui::End();
        ImGui::Render();
    }
    ImGuiContext* context_;
};

// A move that does not mate: Lichess takes any mate as the solution.
bool mates(const chess::Board& board, chess::Move move) {
    auto after = board;
    after.makeMove(move);
    chess::Movelist replies;
    chess::movegen::legalmoves(replies, after);
    return replies.empty() && after.inCheck();
}

// A position of a random game of our own, its side to move having another move than `solution` that does not mate:
// the game's moves, the board, the solution.
struct Made {
    std::vector<chess::Move> moves;  // from the start, the last one the opponent's
    chess::Board board;              // after them
    chess::Move solution;
};
Made madePosition(std::mt19937& rng) {
    for (;;) {
        Made made;
        bool over = false;
        for (int ply = 0, plies = 12 + static_cast<int>(rng() % 30); ply < plies; ++ply) {
            chess::Movelist moves;
            chess::movegen::legalmoves(moves, made.board);
            if (moves.empty()) {
                over = true;
                break;
            }
            const auto m = moves[static_cast<int>(rng() % static_cast<uint32_t>(moves.size()))];
            made.moves.push_back(m);
            made.board.makeMove(m);
        }
        if (over) continue;
        chess::Movelist moves;
        chess::movegen::legalmoves(moves, made.board);
        if (moves.size() < 2) continue;
        made.solution = moves[static_cast<int>(rng() % static_cast<uint32_t>(moves.size()))];
        if (mates(made.board, made.solution)) continue;
        const bool other = std::any_of(moves.begin(), moves.end(), [&](const chess::Move& m) {
            return m != made.solution && !mates(made.board, m);
        });
        if (other) return made;
    }
}

constexpr char DIGITS[] = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";

// Puzzles of our own for the database: the position before the opponent's move, the move, then a solution of one move;
// rated about 1500, where a new player's rating starts.
std::vector<study::Puzzle> madePuzzles(std::size_t n, uint32_t seed) {
    std::mt19937 rng(seed);
    std::vector<study::Puzzle> out;
    std::set<std::string> ids;
    while (out.size() < n) {
        const auto made = madePosition(rng);
        chess::Board before;
        for (std::size_t i = 0; i + 1 < made.moves.size(); ++i) before.makeMove(made.moves[i]);
        study::Puzzle p;
        p.fen   = before.getFen();
        p.moves = {chess::uci::moveToUci(made.moves.back()), chess::uci::moveToUci(made.solution)};
        for (int i = 0; i < 5; ++i) p.id += DIGITS[rng() % 62];
        if (!ids.insert(p.id).second) continue;
        for (int i = 0; i < 8; ++i) p.game += DIGITS[rng() % 62];
        p.rating     = 1450 + static_cast<int>(rng() % 100);
        p.deviation  = 75;
        p.popularity = 90;
        p.themes     = {"fork", "short"};
        out.push_back(std::move(p));
    }
    return out;
}

// A zstd frame of raw blocks, as the format allows: what a test writes without a compressor.
std::string rawFrame(std::string_view text) {
    std::string out;
    const auto put32 = [&](uint32_t v) {
        for (int i = 0; i < 4; ++i) out += static_cast<char>((v >> (8 * i)) & 0xFF);
    };
    put32(0xFD2FB528);
    out += static_cast<char>(0xA0);  // its content's size on 4 bytes, a single segment
    put32(static_cast<uint32_t>(text.size()));
    constexpr std::size_t BLOCK = 1 << 17;
    for (std::size_t at = 0;;) {
        const auto n      = std::min(BLOCK, text.size() - at);
        const bool last   = at + n >= text.size();
        const auto header = (last ? 1u : 0u) | (static_cast<uint32_t>(n) << 3);  // a raw block
        out += static_cast<char>(header & 0xFF);
        out += static_cast<char>((header >> 8) & 0xFF);
        out += static_cast<char>((header >> 16) & 0xFF);
        out.append(text.substr(at, n));
        at += n;
        if (last) return out;
    }
}

// Lichess's puzzle database of these puzzles, as database.lichess.org writes it.
std::string database(const std::vector<study::Puzzle>& all) {
    std::string text =
        "PuzzleId,FEN,Moves,Rating,RatingDeviation,Popularity,NbPlays,Themes,GameUrl,OpeningTags,DailyDate\n";
    for (const auto& p : all)
        text += fmt::format("{},{},{} {},{},{},{},{},fork short,{},,\n", p.id, p.fen, p.moves[0], p.moves[1], p.rating,
                            p.deviation, p.popularity, 500, p.gameUrl());
    return rawFrame(text);
}

// The move `uci` at the Board's position; a move there other than `uci` that does not mate.
chess::Move moveAt(const FakeApp& app, const std::string& uci) {
    return chess::uci::uciToMove(boardAt(app.game, false, app.ply), uci);
}
chess::Move wrongMove(const FakeApp& app, const std::string& uci) {
    const auto board = boardAt(app.game, false, app.ply);
    chess::Movelist moves;
    chess::movegen::legalmoves(moves, board);
    for (const auto m : moves)
        if (chess::uci::moveToUci(m) != uci && !mates(board, m)) return m;
    return chess::Move::NO_MOVE;
}

std::string reply(int status, const std::string& body) {
    return fmt::format(
        "HTTP/1.1 {} X\r\nContent-Type: application/json\r\nContent-Length: {}\r\nConnection: close\r\n\r\n{}", status,
        body.size(), body);
}

// Lichess on this machine, for the user's token: a batch of two puzzles of our own asked for, its results taken in one
// request that brings the next batch, or refused as a revoked token's while the test says so; the last 30 days by
// theme. Every name invented.
class FakeLichess {
   public:
    FakeLichess()
        : port_(freePort()),
          server_(port_, [this](const std::string& head, net::TcpConnection& c) { serve(head, c); }) {
        const auto base    = fmt::format("http://127.0.0.1:{}", port_);
        lichess::servers() = lichess::Servers{base, base + "/tb", base + "/explorer", base + "/engine"};
    }
    ~FakeLichess() { lichess::servers() = lichess::Servers{}; }
    FakeLichess(const FakeLichess&)            = delete;
    FakeLichess& operator=(const FakeLichess&) = delete;

    struct Seen {
        std::string method, path, authorization, body;
    };

    [[nodiscard]] bool ok() const { return server_.ok(); }
    [[nodiscard]] std::vector<Seen> seen(const std::string& method, const std::string& prefix) {
        std::lock_guard lock(mutex_);
        std::vector<Seen> out;
        for (const auto& s : seen_)
            if (s.method == method && s.path.rfind(prefix, 0) == 0) out.push_back(s);
        return out;
    }
    void refuse(bool refused) {
        std::lock_guard lock(mutex_);
        refused_ = refused;
    }

   private:
    // A batch as the API gives one: each puzzle's game in SAN up to the opponent's move, its solution in UCI.
    std::string batch(uint32_t seed) {
        std::mt19937 rng(seed);
        auto puzzles = nlohmann::json::array();
        for (int i = 0; i < 2; ++i) {
            const auto made = madePosition(rng);
            chess::Board board;
            std::string pgn;
            for (const auto m : made.moves) {
                pgn += (pgn.empty() ? "" : " ") + chess::uci::moveToSan(board, m);
                board.makeMove(m);
            }
            const auto id = fmt::format("pz{:03}", seed + static_cast<uint32_t>(i));
            puzzles.push_back({{"game",
                                {{"id", "Gm" + id + "x"},
                                 {"perf", {{"key", "rapid"}, {"name", "Rapid"}}},
                                 {"rated", true},
                                 {"players",
                                  {{{"name", "Player_A"}, {"id", "player_a"}, {"color", "white"}, {"rating", 1600}},
                                   {{"name", "Player_B"}, {"id", "player_b"}, {"color", "black"}, {"rating", 1620}}}},
                                 {"pgn", pgn},
                                 {"clock", "10+0"}}},
                               {"puzzle",
                                {{"id", id},
                                 {"rating", 1900 + i},
                                 {"plays", 1234},
                                 {"solution", {chess::uci::moveToUci(made.solution)}},
                                 {"themes", {"fork", "short"}},
                                 {"initialPly", static_cast<int>(made.moves.size()) - 1}}}});
        }
        return nlohmann::json{{"puzzles", puzzles}, {"glicko", {{"rating", 1905.4}, {"deviation", 62.1}}}}.dump();
    }
    void serve(const std::string& head, net::TcpConnection& c) {
        Seen s;
        s.method        = head.substr(0, head.find(' '));
        s.body          = requestBody(head, c);
        s.path          = requestPath(head);
        s.authorization = httpHeader(head, "Authorization").value_or("");
        bool refused    = false;
        {
            std::lock_guard lock(mutex_);
            seen_.push_back(s);
            refused = refused_;
        }
        if (s.path.rfind("/api/puzzle/dashboard/", 0) == 0) {
            c.sendAll(reply(200, R"({"days": 30, "global": {"nb": 40, "firstWins": 30},
                "themes": {"fork": {"results": {"nb": 12, "firstWins": 9}}}})"));
        } else if (s.path.rfind("/api/puzzle/batch/", 0) == 0 && s.method == "GET") {
            c.sendAll(reply(200, batch(100)));
        } else if (s.path.rfind("/api/puzzle/batch/", 0) == 0 && s.method == "POST") {
            if (refused)
                c.sendAll(reply(401, R"({"error": "No such token"})"));
            else
                c.sendAll(reply(200, batch(200)));
        } else {
            c.sendAll(reply(404, R"({"error": "Not found"})"));
        }
    }

    uint16_t port_;
    std::mutex mutex_;
    std::vector<Seen> seen_;
    bool refused_ = false;
    ScriptedServer server_;  // last: stopped first, its threads using what is above
};

// The user's account, linked with the puzzles' permissions.
lichess::Account puzzleAccount() {
    lichess::Account account;
    account.username = "RS_Sample";
    account.token    = "tok-puzzles";
    account.scopes   = {lichess::SCOPE_PUZZLE_READ, lichess::SCOPE_PUZZLE_WRITE};
    return account;
}

// The Board page, drawn by the series while it holds the Board, as App's hook does (App::drawBoardSession()).
std::function<void()> boardPage(PuzzlesPage& page) {
    return [&page] {
        if (page.onBoard()) page.drawSessionPage();
    };
}

// The palette's labels.
std::vector<std::string> commands(PuzzlesPage& page) {
    std::vector<std::string> labels;
    page.addCommands([&](std::string label, std::string, std::function<void()>) { labels.push_back(std::move(label)); },
                     "Study");
    return labels;
}
bool has(const std::vector<std::string>& labels, const std::string& label) {
    return std::find(labels.begin(), labels.end(), label) != labels.end();
}

// The batch online as the profile keeps it (an empty object while there is none).
nlohmann::json keptBatch(const fs::path& profile) {
    std::ifstream in(profile / "puzzles" / "online.json", std::ios::binary);
    auto j = nlohmann::json::parse(in, nullptr, false);
    return j.is_object() ? j : nlohmann::json::object();
}

// The online choice (puzzles-online), then Enter on the Training page's Puzzles part: a batch asked for, its series
// started once it comes; its two puzzles answered, right then wrong, which completes the batch.
void solveBatch(FakeApp& app, PuzzlesPage& page, Frames& frames) {
    FocusWords words("puzzles-online");
    page.addFocusWords(words, "puzzles-online");
    REQUIRE(words.applied());
    CHECK(app.training_part == 1);
    frames.draw(app, [&] { page.drawPart(); }, ImGuiKey_Enter);
    REQUIRE(waitUntil([&] {
        page.poll();
        return page.onBoard();
    }));
    const auto* run = page.series();
    REQUIRE(run);
    CHECK(run->source == PuzzleRun::Source::Online);
    REQUIRE(run->total == 2);
    page.sessionMove(moveAt(app, run->puzzles[run->current].moves[1]));
    CHECK(run->phase == PuzzleRun::Phase::Right);
    frames.draw(app, boardPage(page), ImGuiKey_Enter);
    REQUIRE(run->phase == PuzzleRun::Phase::Asking);
    page.sessionMove(wrongMove(app, run->puzzles[run->current].moves[1]));
    CHECK(run->phase == PuzzleRun::Phase::Wrong);
}

}  // namespace

TEST_CASE("puzzles page: a series of the database on the Board, a right answer and a wrong one kept in the profile") {
    ScratchDir profile("puzzles-page");
    ScratchDir files("puzzles-page-files");
    const auto file = files / "lichess_db_puzzle.csv.zst";
    std::ofstream(file, std::ios::binary) << database(madePuzzles(12, 107));
    FakeApp app(profile);
    PuzzlesPage page(app);

    // Before the database: its download offered, no puzzle to start.
    CHECK_FALSE(page.storeState().ready);
    CHECK(has(commands(page), "Download Lichess's puzzles"));
    CHECK_FALSE(has(commands(page), "Puzzles: random"));
    // A file downloaded before, read into the store; then each theme's puzzles in the palette.
    page.startOffline(file.string());
    REQUIRE(waitUntil(
        [&] {
            page.poll();
            return page.storeState().ready;
        },
        60s));
    CHECK(page.storeState().puzzles == 12);
    const auto labels = commands(page);
    CHECK(has(labels, "Puzzles: random"));
    CHECK(has(labels, "Puzzles: Fork"));
    CHECK(has(labels, "Remove Lichess's puzzles…"));

    // A series, as its focus word starts it: the Board taken, the first puzzle set on it, from the solver's side.
    FocusWords words("puzzles-series");
    page.addFocusWords(words, "puzzles-series");
    REQUIRE(words.applied());
    REQUIRE(page.onBoard());
    CHECK(app.taken == 1);
    CHECK(app.board_title == "Puzzles · Random");
    CHECK(app.board_tree);
    const auto* run = page.series();
    REQUIRE(run);
    CHECK(run->source == PuzzleRun::Source::Offline);
    CHECK(run->total == 12);
    const auto first = run->puzzles[run->current];
    CHECK(app.game.start_fen == first.fen);
    REQUIRE(app.game.moves.size() == 1);  // the opponent's move, played
    CHECK(chess::uci::moveToUci(app.game.moves[0].move) == first.moves[0]);
    CHECK(app.ply == 1);
    CHECK(app.flip == (chess::Board(first.fen).sideToMove() == chess::Color::WHITE));
    CHECK(page.hidesAids());
    CHECK(std::string(page.guideTopic()) == guide::topics::Puzzles);
    Frames frames;
    frames.draw(app, boardPage(page));
    CHECK(app.board_moves);  // the question takes the user's moves
    CHECK(app.tabs_drawn == 0);

    // The right move: solved, won against the puzzle's rating, kept in the profile.
    page.sessionMove(moveAt(app, first.moves[1]));
    CHECK(run->phase == PuzzleRun::Phase::Right);
    CHECK(app.ply == 2);
    CHECK_FALSE(page.hidesAids());
    auto& record = page.puzzleRecord();
    REQUIRE(record.results.size() == 1);
    CHECK(record.results[0].id == first.id);
    CHECK(record.results[0].win);
    REQUIRE(record.rating);
    CHECK(record.rating->glicko.rating > 1500);
    frames.draw(app, boardPage(page));
    CHECK(app.tabs_drawn > 0);  // with the answer, the tree and the engine come back
    CHECK_FALSE(app.board_moves);

    // Enter: the next puzzle; a wrong move: missed, its card of the day's review back tomorrow.
    frames.draw(app, boardPage(page), ImGuiKey_Enter);
    REQUIRE(run->phase == PuzzleRun::Phase::Asking);
    const auto second = run->puzzles[run->current];
    CHECK(second.id != first.id);
    CHECK(app.game.start_fen == second.fen);
    const auto wrong = wrongMove(app, second.moves[1]);
    REQUIRE(wrong != chess::Move::NO_MOVE);
    page.sessionMove(wrong);
    CHECK(run->phase == PuzzleRun::Phase::Wrong);
    REQUIRE(record.results.size() == 2);
    CHECK(record.results[1].id == second.id);
    CHECK_FALSE(record.results[1].win);
    CHECK(record.cards.count(second.id) == 1);
    const auto due = page.reviewCount();
    CHECK(due.kind == ReviewKind::Puzzles);
    CHECK(due.due == 0);
    CHECK(due.tomorrow == 1);
    frames.draw(app, boardPage(page));
    CHECK(app.arrows == 1);  // the solution's arrow

    // What the profile keeps, read back: the rating, both results, the missed one's card.
    const auto kept = readPuzzleRecord(profile / "puzzles");
    REQUIRE(kept.rating);
    CHECK(kept.rating->glicko.rating == doctest::Approx(record.rating->glicko.rating));
    CHECK(kept.results == record.results);
    CHECK(kept.cards.count(second.id) == 1);
    CHECK(kept.seen.count(first.id) == 1);

    // Escape: the series ends; Escape again: back to the Training page, the Board given back as it was taken.
    frames.draw(app, boardPage(page), ImGuiKey_Escape);
    CHECK(run->phase == PuzzleRun::Phase::End);
    CHECK(run->stopped);
    app.enter_puzzles = true;  // a choice of theirs clicked before
    frames.draw(app, boardPage(page), ImGuiKey_Escape);
    CHECK_FALSE(page.onBoard());
    REQUIRE(app.given_back.size() == 1);
    CHECK(app.given_back[0].second);  // to the Training page
    CHECK(app.given_back[0].first.aside == app.before.aside);
    CHECK(app.given_back[0].first.flip == app.before.flip);
    CHECK(app.given_back[0].first.tab == app.before.tab);
    CHECK(app.review_stopped == 1);
    CHECK_FALSE(app.enter_puzzles);
    CHECK(app.left.empty());
}

TEST_CASE("puzzles page: a batch of the Lichess account solved, its answers sent back in one request") {
    FakeLichess lichess;
    REQUIRE(lichess.ok());
    ScratchDir profile("puzzles-page-online");
    FakeApp app(profile);
    app.own = puzzleAccount();
    PuzzlesPage page(app);
    Frames frames;
    solveBatch(app, page, frames);

    // The batch asked for with the token, of the theme and the difficulty chosen.
    const auto asked = lichess.seen("GET", "/api/puzzle/batch/");
    REQUIRE(asked.size() == 1);
    CHECK(asked[0].path == "/api/puzzle/batch/mix?nb=50&difficulty=normal");
    CHECK(asked[0].authorization == "Bearer tok-puzzles");
    // Complete: its two answers sent in one request, which brings the next batch.
    REQUIRE(waitUntil([&] {
        page.poll();
        return keptBatch(profile).value("sent_status", "") == "accepted";
    }));
    const auto sent = lichess.seen("POST", "/api/puzzle/batch/mix");
    REQUIRE(sent.size() == 1);
    CHECK(sent[0].authorization == "Bearer tok-puzzles");
    const auto body = nlohmann::json::parse(sent[0].body);
    REQUIRE(body["solutions"].size() == 2);
    CHECK(body["solutions"][0]["win"] == true);
    CHECK(body["solutions"][1]["win"] == false);
    CHECK(body["solutions"][0]["rated"] == true);
    const auto kept = keptBatch(profile);
    CHECK(kept["sent_count"] == 2);
    CHECK(kept["results"].empty());
    CHECK(kept["puzzles"].size() == 2);  // the next batch
    CHECK(page.onlineError().empty());
    // The answers are the user's too, online ones, without a RookStudio rating.
    const auto& record = page.puzzleRecord();
    REQUIRE(record.results.size() == 2);
    CHECK(record.results[0].online);
    CHECK_FALSE(record.rating);
    // Another game opened on the Board: the series left where it is, the Board as it was before it.
    page.sessionDropped();
    CHECK_FALSE(page.onBoard());
    REQUIRE(app.left.size() == 1);
    CHECK(app.left[0].tab == app.before.tab);
    CHECK(app.review_stopped == 1);
    CHECK(app.given_back.empty());
}

TEST_CASE("puzzles page: a token Lichess refuses, the batch's answers kept and sent again when the user asks") {
    FakeLichess lichess;
    REQUIRE(lichess.ok());
    lichess.refuse(true);
    ScratchDir profile("puzzles-page-refused");
    FakeApp app(profile);
    app.own = puzzleAccount();
    PuzzlesPage page(app);
    Frames frames;
    solveBatch(app, page, frames);

    // Refused: said, the answers kept in the profile, not taken, and not sent again by themselves.
    REQUIRE(waitUntil([&] {
        page.poll();
        return !page.onlineError().empty();
    }));
    for (int i = 0; i < 3; ++i) page.poll();
    auto kept = keptBatch(profile);
    CHECK(kept["results"].size() == 2);
    CHECK(kept.value("sent_status", "") == "");
    CHECK(lichess.seen("POST", "/api/puzzle/batch/").size() == 1);
    // Lichess takes the token again; Enter on the Training page's Puzzles part: "Send them now".
    lichess.refuse(false);
    frames.draw(app, [&] { page.drawPart(); }, ImGuiKey_Enter);
    REQUIRE(waitUntil([&] {
        page.poll();
        return keptBatch(profile).value("sent_status", "") == "accepted";
    }));
    CHECK(lichess.seen("POST", "/api/puzzle/batch/").size() == 2);
    kept = keptBatch(profile);
    CHECK(kept["sent_count"] == 2);
    CHECK(kept["results"].empty());
}
