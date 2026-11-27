// Guessing the moves of a game (#81, mockup M12, board 2), a module of its own out of App (#107; guess_page.hpp): the
// Training page's part (a game drawn at random from a database and a filter, or the Board page's game), and the game
// replayed on the Board page from the chosen side, a move to guess at each of its turns: as the game 3 points, as good
// by the engine 2, playable 1. The moves of theory of the user's databases pass by themselves; the analysis engine
// scores, one thread, a second a move.
//
// The keys: a move played on the board, or typed; after the score, Enter goes on (or the game goes on by itself after a
// moment, as a right answer of the repertoires' training does); Escape stops.

#include "guess_page.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <random>

#include <fmt/include/fmt/format.h>
#include <imgui.h>
#include <imgui_internal.h>  // SetKeyOwner
#include <commentary/theory.hpp>

#include <study/background.hpp>
#include <study/game_tree.hpp>
#include <study/import.hpp>

#include "analysis.hpp"
#include "board_text.hpp"
#include "board_view.hpp"
#include "console.hpp"
#include "databases.hpp"
#include "guide.hpp"
#include "i18n.hpp"
#include "review_bridge.hpp"
#include "training.hpp"
#include "ui.hpp"

namespace fcg {

namespace {

float px(float v) { return v * ImGui::GetFontSize() / 16.0f; }

using Clock      = std::chrono::steady_clock;
using GuessPhase = GuessRun::Phase;

double since(Clock::time_point t) { return std::chrono::duration<double>(Clock::now() - t).count(); }

// The engine's time a move.
constexpr int64_t GUESS_SEARCH_MS = 1000;
// A game shorter than this is drawn again.
constexpr std::size_t GUESS_SHORTEST = 16;

ImU32 mix(ImU32 a, ImU32 b, float t) {
    const auto fa = ImGui::ColorConvertU32ToFloat4(a), fb = ImGui::ColorConvertU32ToFloat4(b);
    return ImGui::ColorConvertFloat4ToU32(
        ImVec4(fa.x + (fb.x - fa.x) * t, fa.y + (fb.y - fa.y) * t, fa.z + (fb.z - fa.z) * t, 1.0f));
}

void beginTinted(const char* id, ImU32 tone) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ColorConvertU32ToFloat4(mix(ui::CARD_BG, tone, 0.08f)));
    ImGui::PushStyleColor(ImGuiCol_Border, ImGui::ColorConvertU32ToFloat4(mix(ui::CARD_BD, tone, 0.55f)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(16), px(14)));
    ImGui::BeginChild(id, ImVec2(0, 0),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_AutoResizeY);
}

void endTinted() {
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
}

bool primaryButton(const char* label, bool enabled = true) {
    ImGui::BeginDisabled(!enabled);
    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(ui::ACCENT));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                          ImGui::ColorConvertU32ToFloat4(mix(ui::ACCENT, IM_COL32(255, 255, 255, 255), 0.15f)));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                          ImGui::ColorConvertU32ToFloat4(mix(ui::ACCENT, IM_COL32(0, 0, 0, 255), 0.15f)));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
    const bool clicked = ImGui::Button(label);
    ImGui::PopStyleColor(4);
    ImGui::EndDisabled();
    return clicked;
}

void bigCount(const std::string& n, const std::string& what) {
    ImGui::BeginGroup();
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.9f);
    ImGui::TextUnformatted(n.c_str());
    ImGui::PopFont();
    ImGui::TextDisabled("%s", what.c_str());
    ImGui::EndGroup();
}

void wrapped(const std::string& text, bool grey = false) {
    if (grey) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", text.c_str());
    if (grey) ImGui::PopStyleColor();
}

void tip(const std::string& text) {
    if (!ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled)) return;
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
    ImGui::TextUnformatted(text.c_str());
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

bool chip(const char* label, bool chosen, const char* id) {
    const auto& style = ImGui::GetStyle();
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, px(12));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(px(10), style.FramePadding.y * 0.8f));
    if (chosen) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(mix(ui::CARD_BG, ui::ACCENT, 0.30f)));
        ImGui::PushStyleColor(ImGuiCol_Border, ImGui::ColorConvertU32ToFloat4(ui::ACCENT));
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(ui::CARD_BG));
        ImGui::PushStyleColor(ImGuiCol_Border, ImGui::ColorConvertU32ToFloat4(ui::CARD_BD));
    }
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    const bool clicked = ImGui::Button(fmt::format("{}##{}", label, id).c_str());
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);
    return clicked;
}

std::string numbered(const chess::Board& before, chess::Move move) {
    return fmt::format("{}{}{}", before.fullMoveNumber(), before.sideToMove() == chess::Color::WHITE ? "." : "…",
                       localSan(chess::uci::moveToSan(before, move)));
}

// Pawns for White, from pawns for the side that played: "+0.28".
std::string whitePawnsText(double pawns, chess::Color mover) {
    const double white = mover == chess::Color::WHITE ? pawns : -pawns;
    if (std::abs(white) >= 50) return white > 0 ? "#+" : "#-";
    return fmt::format("{:+.2f}", white);
}

// The game up to `ply` (its moves after it left out: not shown before they are guessed).
ReplayGame gameUpTo(const ReplayGame& game, int ply) {
    ReplayGame out;
    out.headers   = game.headers;
    out.start_fen = game.start_fen;
    out.moves.assign(game.moves.begin(),
                     game.moves.begin() + std::clamp<int>(ply, 0, static_cast<int>(game.moves.size())));
    return out;
}

AnalysisPosition positionAfter(const ReplayGame& game, bool chess960, int ply, std::optional<chess::Move> then) {
    AnalysisPosition p;
    p.fen      = game.start_fen.empty() ? std::string(chess::constants::STARTPOS) : game.start_fen;
    p.chess960 = chess960;
    for (int i = 0; i < ply && i < static_cast<int>(game.moves.size()); ++i)
        p.moves.push_back(chess::uci::moveToUci(game.moves[static_cast<std::size_t>(i)].move, chess960));
    if (then) p.moves.push_back(chess::uci::moveToUci(*then, chess960));
    return p;
}

}  // namespace

GuessPage::GuessPage(GuessContext& app) : app_(app) {}

GuessPage::~GuessPage() = default;

bool GuessPage::hidesAids() const {
    // A guess, until the game's move is shown.
    return guess_ && guess_->phase != GuessPhase::Shown && guess_->phase != GuessPhase::End;
}

const char* GuessPage::guideTopic() const { return guide::topics::GuessMoves; }

const std::optional<GuessRecord>& GuessPage::lastRecord() {
    if (!guess_last_read_) {
        guess_last_read_ = true;
        guess_last_      = lastGuessRecord(guessFolder());
    }
    return guess_last_;
}

// ---------------------------------------------------------------------------
// The Home page, the palette, the --focus words

void GuessPage::drawHomeLine() {
    if (!guess_) return;
    ImGui::TextUnformatted(
        trf("Guessing “{}”: {} points of {}", guess_->title, guess_->points, 3 * guess_->guesses.size()).c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton(TR("Back to the game guessed"))) app_.showSession();
}

void GuessPage::addCommands(const PaletteAdd& add, const std::string& training_view) {
    add(TR("Guess the moves"), training_view, [this] {
        // The Board page's game, from the position shown; else the Training page's part.
        const auto shown = app_.shownGame();
        if (shown.page && !shown.session && shown.ply < static_cast<int>(shown.game.moves.size()) &&
            app_.sessionLock().empty()) {
            startGuessBoard();
            return;
        }
        app_.showTrainingPage(3);
    });
    if (app_.sessionLock().empty())
        add(TR("Guess the moves: a random game"), TR("Training"), [this] { (void)startGuessRandom(); });
}

void GuessPage::addFocusWords(FocusWords& words, const std::string& w) {
    words.on({"guess-board", "guess-random"}, [&] {
        // Guessing the moves of the Board page's game from the position shown, or of a random game (#81).
        if (w == "guess-board")
            startGuessBoard();
        else
            (void)startGuessRandom();
    });
}

// ---------------------------------------------------------------------------
// The Training page's part

TrainingKind::Part GuessPage::part() const { return {3, TR("Guess the moves"), "guess", guide::topics::GuessMoves}; }

void GuessPage::drawTodayCards(TodayCards& cards) {
    const auto& last = lastRecord();
    cards.card("##training_kind_guess", ui::Icon::Eye, TR("Guess the moves"),
               TR("A game of a database, a side, move by move; the engine scores."),
               last ? trf("the last: {} points of {}", last->points, last->max) : std::string(TR("not tried yet")));
}

void GuessPage::drawPart() {
    const ui::TopicRegion topic(guide::topics::GuessMoves);
    bool enter = !ImGui::GetIO().WantTextInput && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId) &&
                 (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false));
    if (enter) {
        const auto owner = ImGui::GetID("##training_enter");
        ImGui::SetKeyOwner(ImGuiKey_Enter, owner);
        ImGui::SetKeyOwner(ImGuiKey_KeypadEnter, owner);
    }
    wrapped(
        TR("A game of your databases replayed from one side: at each of its moves, guess the one played. The engine "
           "scores the guess: as the game 3 points, as good 2, playable 1."),
        true);
    if (const auto& last = lastRecord())
        ImGui::TextDisabled("%s", trf("The last: {} points of {} · {}", last->points, last->max, last->title).c_str());
    ImGui::Dummy(ImVec2(0, px(2)));
    const auto lock = app_.sessionLock();
    auto& s         = app_.shelf();
    ui::beginCard("##guess_random");
    ImGui::PushTextWrapPos();
    ImGui::PushFont(ui::titleFont(), ImGui::GetStyle().FontSizeBase);
    ImGui::TextUnformatted(TR("A game at random"));
    ImGui::PopFont();
    if (s.entries().empty()) {
        wrapped(TR("No database yet: make one or add one on the Databases page."), true);
    } else {
        if (guess_source_ < 0 || guess_source_ >= static_cast<int>(s.entries().size())) {
            guess_source_ = 0;
            for (std::size_t i = 0; i < s.entries().size(); ++i)
                if (!s.entries()[i].source) {
                    guess_source_ = static_cast<int>(i);
                    break;
                }
        }
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(TR("From"));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(px(260));
        if (ImGui::BeginCombo("##guess_source", s.entries()[static_cast<std::size_t>(guess_source_)].name.c_str())) {
            for (std::size_t i = 0; i < s.entries().size(); ++i)
                if (ImGui::Selectable(s.entries()[i].name.c_str(), static_cast<int>(i) == guess_source_))
                    guess_source_ = static_cast<int>(i);
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(px(260));
        ImGui::InputTextWithHint("##guess_words", TR("Players, event, ECO code, year"), guess_words_.data(),
                                 guess_words_.size());
        tip(
            TR("The game list's quick filter: a year (2024), an ECO code or its start (B90, B9), a result (1-0), and "
               "words of the players, the event or the site."));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(px(120));
        if (ImGui::InputInt(TR("Elo at least##guess_elo"), &guess_elo_, 100, 200))
            guess_elo_ = std::clamp(guess_elo_, 0, 3500);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(TR("Your side"));
        ImGui::SameLine();
        if (chip(TR("White"), guess_side_ == 0, "guess_white")) guess_side_ = 0;
        ImGui::SameLine(0, px(4));
        if (chip(TR("Black"), guess_side_ == 1, "guess_black")) guess_side_ = 1;
        ImGui::SameLine(0, px(16));
        bool theory = app_.trainingSettings().guess_theory;
        if (ImGui::Checkbox(TR("The moves of theory pass by themselves"), &theory)) {
            auto settings         = app_.trainingSettings();
            settings.guess_theory = theory;
            app_.setTrainingSettings(settings);
        }
        tip(
            TR("The moves played by many games of your databases (their opening tree): the guessing starts where the "
               "game leaves them."));
        if (!guess_error_.empty())
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s", guess_error_.c_str());
        const bool drawing = guess_pick_.valid();
        if (primaryButton(TR("Start · a random game"), lock.empty() && !drawing) ||
            (enter && lock.empty() && !drawing)) {
            enter = false;
            (void)startGuessRandom();
        }
        tip(lock.empty() ? std::string(TR("On the Board page, its column becoming the guessing's; the game is not "
                                          "changed."))
                         : lock);
        ImGui::SameLine(0, px(8));
        ImGui::TextDisabled("%s", drawing ? TR("Drawing a game…") : TR("Enter"));
    }
    ImGui::PopTextWrapPos();
    ui::endCard();
    ImGui::Dummy(ImVec2(0, px(4)));
    // The Board page's game, from the position shown.
    ui::beginCard("##guess_board");
    ImGui::PushTextWrapPos();
    ImGui::PushFont(ui::titleFont(), ImGui::GetStyle().FontSizeBase);
    ImGui::TextUnformatted(TR("The Board page's game"));
    ImGui::PopFont();
    const auto b       = app_.shownGame();
    const bool game_on = !b.session && b.ply < static_cast<int>(b.game.moves.size());
    if (game_on) {
        const auto title = b.title.empty() ? std::string(TR("the game shown")) : b.title;
        wrapped(trf("{}: from the position shown, for the side to move there.", title), true);
        if (ImGui::Button(TR("Guess its moves"))) startGuessBoard();
        if (!lock.empty()) tip(lock);
        ImGui::SameLine(0, px(8));
        ImGui::TextDisabled("%s", TR("Ctrl+K “Guess the moves”"));
    } else {
        wrapped(TR("A game on the Board page, at the position where the guessing starts: Ctrl+K “Guess the moves” "
                   "there."),
                true);
    }
    ImGui::PopTextWrapPos();
    ui::endCard();
    ImGui::Dummy(ImVec2(0, px(4)));
    wrapped(
        TR("As the game: 3 points · as good by the engine (within 0.2 pawn): 2 · playable (within 0.5): 1 · else 0. "
           "The analysis engine, one thread, a second a move. What leaves the machine: nothing."),
        true);
}

// ---------------------------------------------------------------------------
// A game guessed

void GuessPage::poll() {
    // A game drawn at random for guessing.
    if (guess_pick_.valid() && guess_pick_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        auto pick = guess_pick_.get();
        if (pick.game)
            startGuess(std::move(*pick.game), pick.chess960, std::move(pick.title), guess_pick_side_, 0,
                       std::make_pair(pick.folder, pick.id));
        else
            guess_error_ = pick.error == "none" ? TR("No game of this database passes the filter.")
                                                : TR("No game long enough passes the filter.");
    }
    if (guess_) pollGuess();
}

bool GuessPage::startGuessRandom() {
    guess_error_.clear();
    if (!app_.sessionLock().empty() || guess_pick_.valid()) return false;
    auto& s = app_.shelf();
    if (guess_source_ < 0 || guess_source_ >= static_cast<int>(s.entries().size())) return false;
    const auto index = static_cast<std::size_t>(guess_source_);
    const auto db    = s.open(index);
    if (!db) {
        guess_error_ = trf("“{}” cannot be opened: {}", s.entries()[index].name, s.facts(index).error);
        return false;
    }
    auto filter      = quickFilter(guess_words_.data());
    filter.elo_min   = static_cast<uint16_t>(std::clamp(guess_elo_, 0, 3500));
    guess_pick_side_ = guess_side_ == 0 ? chess::Color::WHITE : chess::Color::BLACK;
    // The filter and the draw in the background: a large database's filter takes a moment.
    guess_pick_ =
        std::async(std::launch::async, [snapshot = db->snapshot(), filter, folder = s.entries()[index].folder,
                                        seed = static_cast<uint64_t>(Clock::now().time_since_epoch().count())] {
            study::enterBackground();
            GuessPick pick;
            const auto view = study::makeView(snapshot, filter, study::SortBy::Number);
            if (view.ids.empty()) {
                pick.error = "none";  // said in the interface's words on its thread
                return pick;
            }
            std::mt19937_64 random(seed);
            for (int attempt = 0; attempt < 40; ++attempt) {
                const auto id = view.ids[random() % view.ids.size()];
                if (snapshot->deleted(id) || snapshot->row(id).plies < GUESS_SHORTEST) continue;
                std::string error;
                auto game = readPgnGame(study::importedPgn(snapshot->game(id)), error);
                if (!game || game->moves.size() < GUESS_SHORTEST) continue;
                pick.chess960 = gameIsChess960(*game);
                pick.title    = fmt::format("{} – {}", game->header("White"), game->header("Black"));
                if (const auto event = game->header("Event"); !event.empty() && event != "?")
                    pick.title += " · " + event;
                pick.game   = std::move(*game);
                pick.folder = folder;
                pick.id     = id;
                return pick;
            }
            pick.error = "short";
            return pick;
        });
    return true;
}

void GuessPage::startGuessBoard() {
    if (!app_.sessionLock().empty()) return;
    const auto b = app_.shownGame();
    if (b.ply >= static_cast<int>(b.game.moves.size())) {
        app_.toast(TR("No move left to guess after the position shown."));
        return;
    }
    const auto side = b.game.moves[static_cast<std::size_t>(b.ply)].mover;
    auto title = b.title.empty() ? fmt::format("{} – {}", b.game.header("White"), b.game.header("Black")) : b.title;
    startGuess(b.game, b.chess960, std::move(title), side, b.ply, b.origin);
}

void GuessPage::startGuess(ReplayGame game, bool chess960, std::string title, chess::Color side, int from,
                           std::optional<std::pair<std::filesystem::path, uint64_t>> origin) {
    if (!app_.sessionLock().empty()) return;
    const auto* engine = app_.analysisEngine();
    if (!engine) {
        guess_error_ = TR("No engine to score with: add one on the Engines page.");
        app_.toast(guess_error_);
        return;
    }
    auto run      = std::make_unique<GuessRun>();
    run->game     = std::move(game);
    run->chess960 = chess960;
    run->title    = std::move(title);
    run->side     = side;
    run->from     = std::clamp(from, 0, static_cast<int>(run->game.moves.size()));
    // The Board taken, the session that holds it closed: the game on the board goes among the recent games (Ctrl+Tab),
    // given back after the game guessed; the column at the tree rather than the notation's editing.
    const auto taken = app_.takeBoard(trf("Guessing · {}", run->title), true);
    run->aside       = taken.aside;
    run->flip        = taken.flip;
    run->tab         = taken.tab;
    // The engine: the analysis's, one thread, a second a move.
    AnalysisSettings settings;
    settings.engine  = engine->name;
    settings.threads = 1;
    settings.hash    = app_.analysisSettings().hash;
    settings.multipv = 1;
    run->engine.start(*engine, settings);
    run->engine_name = engine->name;
    // The moves of theory of the user's databases, from the start of a game that starts at the usual position.
    const bool usual = run->game.start_fen.empty() || run->game.start_fen == std::string(chess::constants::STARTPOS);
    if (app_.trainingSettings().guess_theory && run->from == 0 && usual && !chess960) {
        auto& s        = app_.shelf();
        const int tree = app_.treeDatabase();
        std::optional<std::size_t> base;
        if (tree >= 0 && static_cast<std::size_t>(tree) < s.entries().size() && !s.entries()[tree].source)
            base = static_cast<std::size_t>(tree);
        for (std::size_t i = 0; i < s.entries().size() && !base; ++i)
            if (!s.entries()[i].source && s.facts(i).tree) base = i;
        if (base) {
            if (auto source = s.treeSource(*base); source && source->tree) {
                commentary::TheoryOptions options;
                options.confirm = false;
                if (origin && origin->first == s.entries()[*base].folder) options.itself = origin->second;
                run->theory = std::async(std::launch::async, [source = *source, main = run->game, options] {
                    study::enterBackground();
                    const auto line = mainLineOf(main, false);
                    return line ? commentary::theoryOf(*source.tree, source.snapshot, *line, options).book_plies
                                : std::size_t{0};
                });
            }
        }
    }
    // On the Board, the game up to the position it starts from.
    app_.replaySessionGame(gameUpTo(run->game, run->from), run->from, chess960);
    app_.setFlip(side == chess::Color::BLACK);
    run->started = Clock::now();
    guess_       = std::move(run);
    if (!guess_->theory.valid()) askGuess();
}

void GuessPage::askGuess() {
    auto& g = *guess_;
    int ply = std::max(g.from, static_cast<int>(g.book));
    if (g.phase != GuessPhase::Preparing) ply = g.ply + 1;
    while (ply < static_cast<int>(g.game.moves.size()) && g.game.moves[static_cast<std::size_t>(ply)].mover != g.side)
        ++ply;
    if (ply >= static_cast<int>(g.game.moves.size())) return finishGuess(false);
    g.ply = ply;
    // The board at the question, the game's moves before it.
    app_.replaySessionGame(gameUpTo(g.game, ply), ply, g.chess960);
    g.game_score.reset();
    g.guess_score.reset();
    g.guess_asked = false;
    g.guess       = chess::Move::NO_MOVE;
    g.hovered     = false;
    g.error.clear();
    // The game's move searched while the guess is thought over; a move that ends the game needs no engine.
    const auto& move = g.game.moves[static_cast<std::size_t>(ply)].move;
    g.game_position  = positionAfter(g.game, g.chess960, ply, move);
    auto after       = boardAt(g.game, g.chess960, ply + 1);
    if (const auto end = endScore(after))
        g.game_score = end;
    else
        g.engine.analyse(g.game_position, SearchLimit{GUESS_SEARCH_MS, 0, 0}, g.guesses.empty());
    g.phase = GuessPhase::Asking;
}

void GuessPage::guessAnswer(chess::Move move) {
    auto& g = *guess_;
    if (g.phase != GuessPhase::Asking || app_.liveGameOn() || app_.sessionPly() != g.ply) return;
    const auto& played = g.game.moves[static_cast<std::size_t>(g.ply)].move;
    g.guess            = move;
    app_.dropPick();
    app_.dropTypedMove();
    if (move == played) {
        GuessRun::Guess r;
        r.ply   = g.ply;
        r.guess = r.game = chess::uci::moveToUci(move, g.chess960);
        r.points         = 3;
        r.same           = true;
        if (g.game_score) r.game_pawns = r.guess_pawns = -scorePawns(*g.game_score);
        g.guesses.push_back(r);
        g.points += 3;
        g.phase    = GuessPhase::Shown;
        g.shown_at = Clock::now();
        return;
    }
    g.guess_position = positionAfter(g.game, g.chess960, g.ply, move);
    auto after       = boardAt(g.game, g.chess960, g.ply);
    after.makeMove(move);
    if (const auto end = endScore(after)) {
        g.guess_score = end;
        g.guess_asked = true;
    }
    g.phase = GuessPhase::Checking;
    pollGuess();
}

void GuessPage::pollGuess() {
    auto& g = *guess_;
    g.engine.poll();
    if (g.phase == GuessPhase::Preparing) {
        if (g.theory.valid() && g.theory.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            g.book = g.theory.get();
            askGuess();
        }
        return;
    }
    if (g.engine.state() == Analyser::State::Failed &&
        (g.phase == GuessPhase::Asking || g.phase == GuessPhase::Checking)) {
        g.error = g.engine.error();
        return;
    }
    if (g.engine.finished() && !g.engine.lines().empty() && g.engine.lines().front().score) {
        if (!g.game_score && g.engine.position() == g.game_position) g.game_score = g.engine.lines().front().score;
        if (!g.guess_score && g.guess_asked && g.engine.position() == g.guess_position)
            g.guess_score = g.engine.lines().front().score;
    }
    // The guess searched once the game's move is.
    if (g.phase == GuessPhase::Checking && g.game_score && !g.guess_asked) {
        g.guess_asked = true;
        g.engine.analyse(g.guess_position, SearchLimit{GUESS_SEARCH_MS, 0, 0}, false);
    }
    if (g.phase == GuessPhase::Checking && g.game_score && g.guess_score) guessScored();
    // The game goes on by itself after a moment, unless the mouse is on the score.
    const int advance = app_.trainingSettings().advance_ms;
    if (g.phase == GuessPhase::Shown && advance > 0 && !g.hovered && since(g.shown_at) * 1000 >= advance + 1500)
        guessAdvance();
}

void GuessPage::guessScored() {
    auto& g = *guess_;
    GuessRun::Guess r;
    r.ply         = g.ply;
    r.guess       = chess::uci::moveToUci(g.guess, g.chess960);
    r.game        = chess::uci::moveToUci(g.game.moves[static_cast<std::size_t>(g.ply)].move, g.chess960);
    r.game_pawns  = -scorePawns(*g.game_score);  // the scores are the opponent's, to move after each
    r.guess_pawns = -scorePawns(*g.guess_score);
    r.points      = guessPoints(false, r.game_pawns, r.guess_pawns);
    g.guesses.push_back(r);
    g.points += r.points;
    g.phase    = GuessPhase::Shown;
    g.shown_at = Clock::now();
}

void GuessPage::guessAdvance() {
    auto& g = *guess_;
    if (g.phase != GuessPhase::Shown) return;
    askGuess();
}

void GuessPage::finishGuess(bool stopped) {
    auto& g   = *guess_;
    g.phase   = GuessPhase::End;
    g.stopped = stopped;
    g.took    = since(g.started);
    g.engine.stop();
    // The whole game on the board, to read it again.
    app_.replaySessionGame(g.game, std::min<int>(g.ply, static_cast<int>(g.game.moves.size())), g.chess960);
    if (g.guesses.empty()) return;
    GuessRecord r;
    r.at    = epochNow();
    r.title = g.title;
    r.white = g.side == chess::Color::WHITE;
    for (const auto& x : g.guesses) {
        r.points += x.points;
        r.max += 3;
        r.same += x.same ? 1 : 0;
        r.loss += std::max(0.0, x.game_pawns - x.guess_pawns);
    }
    r.moves = static_cast<int>(g.guesses.size());
    r.loss /= std::max(1, r.moves);
    guess_last_      = r;
    guess_last_read_ = true;
    if (!app_.scripted()) appendGuessRecord(guessFolder(), r);
}

void GuessPage::closeGuess(bool to_training) {
    if (!guess_) return;
    if (to_training) app_.stopReview();
    BoardTaken taken;
    taken.aside = guess_->aside;
    taken.flip  = guess_->flip;
    taken.tab   = guess_->tab;
    guess_.reset();
    app_.giveBoardBack(taken, to_training);  // the game's position is no game to remember
}

void GuessPage::dropGuess() {
    BoardTaken taken;
    taken.aside = guess_->aside;
    taken.flip  = guess_->flip;
    taken.tab   = guess_->tab;
    guess_.reset();
    app_.leaveBoard(taken);  // the game that comes takes the board; the one put aside stays among the recent ones
}

// ---------------------------------------------------------------------------
// The Board page during a game guessed

void GuessPage::guessKeys() {
    auto& g        = *guess_;
    const auto& io = ImGui::GetIO();
    if (io.WantTextInput || ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) return;
    const auto owner = ImGui::GetID("##guess_keys");
    for (const auto key : {ImGuiKey_Enter, ImGuiKey_KeypadEnter, ImGuiKey_Escape, ImGuiKey_LeftArrow,
                           ImGuiKey_RightArrow, ImGuiKey_UpArrow, ImGuiKey_DownArrow, ImGuiKey_Home, ImGuiKey_End})
        ImGui::SetKeyOwner(key, owner);
    const bool plain = !io.KeyCtrl && !io.KeyAlt && !io.KeySuper;
    const bool enter =
        plain && (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false));
    const bool escape = ImGui::IsKeyPressed(ImGuiKey_Escape, false);
    switch (g.phase) {
        case GuessPhase::Preparing:
        case GuessPhase::Checking:
            if (escape) finishGuess(true);
            break;
        case GuessPhase::Asking:
            if (escape)
                finishGuess(true);
            else if (!app_.liveGameOn())
                app_.moveKeys();
            break;
        case GuessPhase::Shown:
            if (escape)
                finishGuess(true);
            else if (enter)
                guessAdvance();
            break;
        case GuessPhase::End: {
            // The whole game, a move at a time.
            const int n   = static_cast<int>(app_.sessionGame().moves.size());
            const int ply = app_.sessionPly();
            if (escape || enter) {
                closeGuess(true);
            } else if (plain && ImGui::IsKeyPressed(ImGuiKey_LeftArrow, true)) {
                app_.showSessionPly(std::max(0, ply - 1));
            } else if (plain && ImGui::IsKeyPressed(ImGuiKey_RightArrow, true)) {
                app_.showSessionPly(std::min(n, ply + 1));
            } else if (plain && ImGui::IsKeyPressed(ImGuiKey_Home, false)) {
                app_.showSessionPly(0);
            } else if (plain && ImGui::IsKeyPressed(ImGuiKey_End, false)) {
                app_.showSessionPly(n);
            }
            break;
        }
    }
}

void GuessPage::drawGuessPage() {
    app_.beginSessionPage();
    guessKeys();
    if (!guess_) return;
    auto& g            = *guess_;
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float side   = std::clamp(avail.x * 0.42f, px(340), px(620));
    const float under  = ImGui::GetTextLineHeightWithSpacing() * 3;
    const float size   = std::floor(
        std::max(px(200), std::min(avail.x - side - ImGui::GetStyle().ItemSpacing.x - px(20), avail.y - under)));
    ImGui::BeginGroup();
    {
        const auto start = ImGui::GetCursorPosX();
        ImGui::TextDisabled("%s", g.title.c_str());
        const auto where =
            g.side == chess::Color::WHITE ? std::string(TR("you are White")) : std::string(TR("you are Black"));
        ImGui::SameLine(std::max(ImGui::GetCursorPosX() + px(10), start + size - ImGui::CalcTextSize(where.c_str()).x));
        ImGui::TextDisabled("%s", where.c_str());
    }
    drawGuessBoard(size);
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + size);
    if (g.phase == GuessPhase::Shown)
        ImGui::TextDisabled("%s", TR("Your move in blue, the game's in green."));
    else if (g.phase == GuessPhase::End && g.shown_guess >= 0)
        ImGui::TextDisabled("%s", TR("Your move in blue, the game's in green."));
    else if (g.book > 0 && g.guesses.empty() && g.phase == GuessPhase::Asking)
        ImGui::TextDisabled("%s", trn("{} opening move, theory in your databases, went by itself.",
                                      "{} opening moves, theory in your databases, went by themselves.",
                                      (g.book + 1) / 2, (g.book + 1) / 2)
                                      .c_str());
    ImGui::PopTextWrapPos();
    ImGui::EndGroup();
    ImGui::SameLine(0, px(18));
    ImGui::BeginChild("##guess_side", ImVec2(0, 0));
    drawGuessColumn();
    ImGui::EndChild();
}

void GuessPage::drawGuessBoard(float size) {
    auto& g          = *guess_;
    const int ply    = app_.sessionPly();
    const auto board = boardAt(app_.sessionGame(), g.chess960, ply);
    BoardDecor marks;  // the arrows of the guess's: the Board adds the rest
    const GuessRun::Guess* shown = nullptr;
    if (g.phase == GuessPhase::Shown && !g.guesses.empty()) shown = &g.guesses.back();
    if (g.phase == GuessPhase::End && g.shown_guess >= 0 && g.shown_guess < static_cast<int>(g.guesses.size()))
        shown = &g.guesses[static_cast<std::size_t>(g.shown_guess)];
    if (g.phase == GuessPhase::Checking && g.guess != chess::Move::NO_MOVE)
        marks.arrows.push_back({g.guess, markColour('B', false)});
    if (shown && ply == shown->ply) {
        try {
            const auto game = chess::uci::uciToMove(board, shown->game);
            if (!shown->same)
                marks.arrows.push_back({chess::uci::uciToMove(board, shown->guess), markColour('B', false)});
            marks.arrows.push_back({game, markColour('G', false)});
        } catch (const std::exception&) {
        }
    }
    const bool moves = g.phase == GuessPhase::Asking && ply == g.ply && !app_.liveGameOn();
    app_.drawSessionBoard(size, marks, moves);
}

void GuessPage::drawGuessColumn() {
    auto& g = *guess_;
    const ui::TopicRegion topic(guide::topics::GuessMoves);
    if (app_.liveGameOn()) {  // fair play (#48): the engine is silent during a live game; the guessing waits
        app_.drawFairPlayBanner();
        if (ImGui::Button(TR("Stop · Esc"))) finishGuess(true);
        return;
    }
    if (g.phase == GuessPhase::End) return drawGuessEnd();
    if (g.phase == GuessPhase::Preparing) {
        ImGui::TextDisabled("%s", TR("Looking for the game's theory in your databases…"));
        if (ImGui::Button(TR("Stop · Esc"))) finishGuess(true);
        return;
    }
    // The last guess scored, then the question.
    const auto verdict = [&](const GuessRun::Guess& r, bool next) {
        const auto before = boardAt(g.game, g.chess960, r.ply);
        chess::Move guess = chess::Move::NO_MOVE, game = chess::Move::NO_MOVE;
        try {
            guess = chess::uci::uciToMove(before, r.guess);
            game  = chess::uci::uciToMove(before, r.game);
        } catch (const std::exception&) {
            return;
        }
        beginTinted("##guess_verdict", r.points >= 2 ? ui::ACCENT : ui::WARN);
        ImGui::PushTextWrapPos();
        ImGui::PushFont(ui::titleFont(), ImGui::GetStyle().FontSizeBase * 1.1f);
        ImGui::TextUnformatted(
            r.same ? trf("Move {} · {}, as the game", before.fullMoveNumber(), numbered(before, game)).c_str()
                   : trf("Move {} · you: {} · the game: {}", before.fullMoveNumber(), numbered(before, guess),
                         numbered(before, game))
                         .c_str());
        ImGui::PopFont();
        const auto mover = before.sideToMove();
        if (r.same)
            wrapped(trn("As the game: {} point.", "As the game: {} points.", 3, 3));
        else if (r.points == 2)
            wrapped(trf("As good by the engine ({} against {}): {}.", whitePawnsText(r.guess_pawns, mover),
                        whitePawnsText(r.game_pawns, mover), trn("{} point", "{} points", 2, 2)));
        else if (r.points == 1)
            wrapped(trf("Playable by the engine ({} against {}): {}.", whitePawnsText(r.guess_pawns, mover),
                        whitePawnsText(r.game_pawns, mover), trn("{} point", "{} points", 1, 1)));
        else
            wrapped(trf("Worse by the engine ({} against {}): no point.", whitePawnsText(r.guess_pawns, mover),
                        whitePawnsText(r.game_pawns, mover)));
        if (next) {
            const auto ply = static_cast<std::size_t>(r.ply);
            std::string goes;
            if (ply + 1 < g.game.moves.size()) {
                auto after = before;
                after.makeMove(game);
                const auto reply = numbered(after, g.game.moves[ply + 1].move);
                goes = (mover == chess::Color::WHITE
                            ? trf("The game goes on with {}; Black answers {}.", numbered(before, game), reply)
                            : trf("The game goes on with {}; White answers {}.", numbered(before, game), reply));
            }
            if (!goes.empty()) wrapped(goes, true);
        }
        ImGui::PopTextWrapPos();
        endTinted();
    };
    switch (g.phase) {
        case GuessPhase::Asking:
        case GuessPhase::Checking: {
            if (!g.guesses.empty()) {
                verdict(g.guesses.back(), true);
                ImGui::Dummy(ImVec2(0, px(4)));
            }
            const auto before = boardAt(g.game, g.chess960, g.ply);
            beginTinted("##guess_question", ui::CARD_BD);
            ImGui::PushTextWrapPos();
            ImGui::PushFont(ui::titleFont(), ImGui::GetStyle().FontSizeBase * 1.2f);
            ImGui::TextUnformatted((g.side == chess::Color::WHITE
                                        ? trf("Your move, White: move {}", before.fullMoveNumber())
                                        : trf("Your move, Black: move {}", before.fullMoveNumber()))
                                       .c_str());
            ImGui::PopFont();
            if (!g.error.empty()) {
                ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s", g.error.c_str());
            } else if (g.phase == GuessPhase::Checking) {
                wrapped(trf("{}: {} weighs it against the game's move…", numbered(before, g.guess), g.engine_name));
            } else {
                wrapped(TR("Guess the move played in the game. Play on the board, or type it and press Enter."), true);
                app_.drawMoveField();
            }
            if (ImGui::Button(TR("Stop · Esc"))) finishGuess(true);
            ImGui::PopTextWrapPos();
            endTinted();
            break;
        }
        case GuessPhase::Shown: {
            verdict(g.guesses.back(), false);
            // The mouse on the score holds it.
            g.hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem |
                                             ImGuiHoveredFlags_AllowWhenBlockedByPopup);
            if (primaryButton(TR("Next"))) {
                guessAdvance();
                break;
            }
            ImGui::SameLine(0, px(8));
            const int advance = app_.trainingSettings().advance_ms;
            ImGui::TextDisabled("%s", advance > 0
                                          ? trf("Enter, or on its own after {} s", (advance + 1500) / 1000.0).c_str()
                                          : TR("Enter"));
            break;
        }
        default:
            break;
    }
    if (!guess_ || guess_->phase == GuessPhase::End) return;
    // So far: each guess, its points.
    if (g.guesses.empty()) {
        ImGui::Dummy(ImVec2(0, px(4)));
        wrapped(
            TR("As the game: 3 points · as good by the engine (within 0.2 pawn): 2 · playable (within 0.5): 1 · else "
               "0. The analysis engine, one thread, a second a move. The game is not changed."),
            true);
        return;
    }
    ImGui::Dummy(ImVec2(0, px(6)));
    ui::beginCard("##guess_list");
    const std::size_t first = g.guesses.size() > 8 ? g.guesses.size() - 8 : 0;
    for (std::size_t i = first; i < g.guesses.size(); ++i) {
        const auto& r     = g.guesses[i];
        const auto before = boardAt(g.game, g.chess960, r.ply);
        std::string text;
        try {
            const auto game  = chess::uci::uciToMove(before, r.game);
            const auto guess = chess::uci::uciToMove(before, r.guess);
            text             = r.same ? fmt::format("{}  {}", numbered(before, game), TR("as the game"))
                                      : fmt::format("{}  {}", numbered(before, game),
                                                    trf("you: {}", localSan(chess::uci::moveToSan(before, guess))));
        } catch (const std::exception&) {
        }
        ImGui::TextUnformatted(text.c_str());
        const auto points = std::to_string(r.points);
        ImGui::SameLine(std::max(ImGui::GetCursorPosX() + px(10),
                                 ImGui::GetContentRegionMax().x - ImGui::CalcTextSize(points.c_str()).x));
        ImGui::TextUnformatted(points.c_str());
    }
    const auto so_far = trf("{} of {}", g.points, 3 * g.guesses.size());
    ImGui::TextUnformatted(TR("So far"));
    ImGui::SameLine(std::max(ImGui::GetCursorPosX() + px(10),
                             ImGui::GetContentRegionMax().x - ImGui::CalcTextSize(so_far.c_str()).x));
    ImGui::TextUnformatted(so_far.c_str());
    ui::endCard();
    ImGui::Dummy(ImVec2(0, px(4)));
    wrapped(
        TR("As the game: 3 points · as good by the engine (within 0.2 pawn): 2 · playable (within 0.5): 1 · else 0. "
           "The analysis engine, one thread, a second a move. The game is not changed."),
        true);
}

void GuessPage::drawGuessEnd() {
    auto& g = *guess_;
    ui::inlineIcon(ui::Icon::Check, ui::GOOD);
    ImGui::SameLine();
    ImGui::PushFont(ui::titleFont(), ImGui::GetStyle().FontSizeBase * 1.35f);
    if (g.guesses.empty())
        ImGui::TextUnformatted(g.stopped ? TR("Stopped before the first guess") : TR("No move to guess"));
    else
        ImGui::TextUnformatted(
            trf("{} points of {} in {}", g.points, 3 * g.guesses.size(), ui::formatDuration(g.took)).c_str());
    ImGui::PopFont();
    if (!g.guesses.empty()) {
        std::size_t same = 0;
        double loss      = 0;
        for (const auto& r : g.guesses) {
            same += r.same ? 1 : 0;
            loss += std::max(0.0, r.game_pawns - r.guess_pawns);
        }
        ImGui::Dummy(ImVec2(0, px(4)));
        bigCount(fmt::format("{} / {}", same, g.guesses.size()), TR("as the game"));
        ImGui::SameLine(0, px(30));
        bigCount(fmt::format("{:.2f}", loss / static_cast<double>(g.guesses.size())),
                 TR("pawns lost a move, on average"));
        // The moves that cost the most: a click shows their position.
        std::vector<std::size_t> costly;
        for (std::size_t i = 0; i < g.guesses.size(); ++i)
            if (g.guesses[i].game_pawns - g.guesses[i].guess_pawns > 0.2) costly.push_back(i);
        std::stable_sort(costly.begin(), costly.end(), [&](std::size_t a, std::size_t b) {
            return g.guesses[a].game_pawns - g.guesses[a].guess_pawns >
                   g.guesses[b].game_pawns - g.guesses[b].guess_pawns;
        });
        if (costly.size() > 5) costly.resize(5);
        if (!costly.empty()) {
            ImGui::Dummy(ImVec2(0, px(6)));
            ImGui::TextUnformatted(TR("The moves that cost you most"));
            ImGui::SameLine();
            ImGui::TextDisabled("%s", TR("· a click shows the position"));
            ui::beginCard("##guess_costly");
            for (const auto i : costly) {
                const auto& r     = g.guesses[i];
                const auto before = boardAt(g.game, g.chess960, r.ply);
                std::string label, said;
                try {
                    label = trf("you: {} · the game: {}", numbered(before, chess::uci::uciToMove(before, r.guess)),
                                numbered(before, chess::uci::uciToMove(before, r.game)));
                    said  = fmt::format("{:+.2f}", r.guess_pawns - r.game_pawns);
                } catch (const std::exception&) {
                    continue;
                }
                ImGui::PushID(static_cast<int>(i));
                if (ImGui::Selectable(label.c_str(), g.shown_guess == static_cast<int>(i))) {
                    g.shown_guess = static_cast<int>(i);
                    app_.showSessionPly(r.ply);
                }
                ImGui::SameLine(std::max(ImGui::GetCursorPosX() + px(10),
                                         ImGui::GetContentRegionMax().x - ImGui::CalcTextSize(said.c_str()).x));
                ImGui::TextDisabled("%s", said.c_str());
                ImGui::PopID();
            }
            ui::endCard();
        }
    }
    ImGui::Dummy(ImVec2(0, px(6)));
    if (primaryButton(TR("Back to the Training page"))) return closeGuess(true);
    ImGui::SameLine();
    if (ImGui::Button(TR("Another game"))) {
        closeGuess(true);
        (void)startGuessRandom();
        return;
    }
    ImGui::SameLine(0, px(8));
    ImGui::TextDisabled("%s", TR("Enter: back"));
    ImGui::Dummy(ImVec2(0, px(4)));
    wrapped(TR("The whole game is on the board: ← and → go through it. The games guessed are kept in the profile."),
            true);
}

}  // namespace fcg
