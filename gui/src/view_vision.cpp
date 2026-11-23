// Board vision (#82, mockup M12, board 3; vision.hpp): the Training page's Vision part and its exercises on the Board
// page, whose column becomes the exercise's: thirty seconds of coordinates (a square to find with a click on a board
// without its letters and figures, or to name with two keys), a game played blindfold against an engine of the library
// (the moves typed, the pieces hidden, shown two seconds on Shift held three times a game, F2 asking where a piece is),
// a line read in one's head (six moves from a position, then the piece on a square they changed).
//
// The keys: Enter starts (and goes on), Escape stops; the coordinates' squares named by their two keys; blindfold, a
// move typed, Shift held, F2; a line's answer as FEN writes the piece ("N" a white knight, "n" a black one, "-" none).

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>

#include <SDL3/SDL.h>
#include <fmt/include/fmt/format.h>
#include <imgui.h>
#include <imgui_internal.h>  // SetKeyOwner

#include <study/game_tree.hpp>

#include "app.hpp"
#include "board_text.hpp"
#include "board_view.hpp"
#include "console.hpp"
#include "guide.hpp"
#include "i18n.hpp"
#include "ui.hpp"

namespace fcg {

namespace {

float px(float v) { return v * ImGui::GetFontSize() / 16.0f; }

using Clock  = std::chrono::steady_clock;
using VKind  = VisionRun::Kind;
using VPhase = VisionRun::Phase;

double since(Clock::time_point t) { return std::chrono::duration<double>(Clock::now() - t).count(); }

// A fault stays shown this long: the square given in orange, the square asked in green.
constexpr double FAULT_SECONDS = 1.2;
// Shift held this long, with no key typed, shows the pieces: a capital letter typed does not.
constexpr double PEEK_DELAY = 0.25;
constexpr std::size_t LINES = 10;

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

void wrapped(const std::string& text, bool grey = false) {
    if (grey) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", text.c_str());
    if (grey) ImGui::PopStyleColor();
}

bool chip(const char* label, bool chosen, const char* id) {
    const auto& style = ImGui::GetStyle();
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, px(12));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(px(10), style.FramePadding.y * 0.8f));
    ImGui::PushStyleColor(ImGuiCol_Button,
                          ImGui::ColorConvertU32ToFloat4(chosen ? mix(ui::CARD_BG, ui::ACCENT, 0.30f) : ui::CARD_BG));
    ImGui::PushStyleColor(ImGuiCol_Border, ImGui::ColorConvertU32ToFloat4(chosen ? ui::ACCENT : ui::CARD_BD));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    const bool clicked = ImGui::Button(fmt::format("{}##{}", label, id).c_str());
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);
    return clicked;
}

// A piece in words: "a white knight", "an empty square".
const char* pieceWords(chess::Piece piece) {
    if (piece == chess::Piece::NONE) return TR("an empty square");
    const bool white = piece.color() == chess::Color::WHITE;
    switch (piece.type().internal()) {
        case chess::PieceType::underlying::KING:
            return white ? TR("the white king") : TR("the black king");
        case chess::PieceType::underlying::QUEEN:
            return white ? TR("a white queen") : TR("a black queen");
        case chess::PieceType::underlying::ROOK:
            return white ? TR("a white rook") : TR("a black rook");
        case chess::PieceType::underlying::BISHOP:
            return white ? TR("a white bishop") : TR("a black bishop");
        case chess::PieceType::underlying::KNIGHT:
            return white ? TR("a white knight") : TR("a black knight");
        default:
            return white ? TR("a white pawn") : TR("a black pawn");
    }
}

// The moves of a line in numbered SAN, as the interface writes them: "8.c3 O-O 9.h3".
std::string movesText(chess::Board board, const std::vector<chess::Move>& moves) {
    std::string out;
    for (std::size_t i = 0; i < moves.size(); ++i) {
        const bool white = board.sideToMove() == chess::Color::WHITE;
        const auto san   = localSan(chess::uci::moveToSan(board, moves[i]));
        if (white)
            out += (out.empty() ? "" : " ") + fmt::format("{}.{}", board.fullMoveNumber(), san);
        else if (i == 0)
            out += fmt::format("{}…{}", board.fullMoveNumber(), san);
        else
            out += " " + san;
        board.makeMove(moves[i]);
    }
    return out;
}

// The pieces of `board` as an array, for the board drawn.
std::array<chess::Piece, 64> piecesOfBoard(const chess::Board& board) { return shownPieces(board, Hidden::All, true); }

}  // namespace

// ---------------------------------------------------------------------------
// The record, the page's part

VisionRecord& App::visionRecord() {
    if (!vision_record_) vision_record_ = readVisionRecord(storage_.root());
    return *vision_record_;
}

void App::saveVisionRecord() {
    if (options_.scripted || !vision_record_) return;
    if (!writeVisionRecord(storage_.root(), *vision_record_))
        Console::instance().add(trf("The board vision's records could not be kept in {}", storage_.root().string()),
                                true);
}

void App::drawTrainingVision() {
    const ui::TopicRegion topic(guide::topics::Vision);
    const bool enter = !ImGui::GetIO().WantTextInput && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId) &&
                       (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false));
    if (enter) {
        const auto owner = ImGui::GetID("##training_enter");
        ImGui::SetKeyOwner(ImGuiKey_Enter, owner);
        ImGui::SetKeyOwner(ImGuiKey_KeypadEnter, owner);
    }
    const auto lock = trainingLock();
    wrapped(TR("Seeing the board in your head: the coordinates, a game played blindfold against an engine, a line of "
               "moves read without the pieces moving. Nothing leaves the machine."),
            true);
    if (!vision_error_.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(ui::WARN));
        ImGui::TextWrapped("%s", vision_error_.c_str());
        ImGui::PopStyleColor();
    }
    ImGui::Dummy(ImVec2(0, px(2)));
    auto& record = visionRecord();

    // The coordinates.
    ui::beginCard("##vision_coordinates");
    ImGui::PushTextWrapPos();
    ui::inlineIcon(ui::Icon::Grid, ui::ACCENT);
    ImGui::SameLine();
    ImGui::PushFont(ui::titleFont(), ImGui::GetStyle().FontSizeBase);
    ImGui::TextUnformatted(TR("Coordinates"));
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::TextDisabled("%s", TR("· 30 seconds, as many squares as you can"));
    if (chip(TR("Find the square (a click)"), vision_mode_ == CoordMode::Find, "coord_find")) {
        vision_mode_  = CoordMode::Find;
        vision_enter_ = 0;
    }
    ImGui::SameLine();
    if (chip(TR("Name the square (two keys)"), vision_mode_ == CoordMode::Name, "coord_name")) {
        vision_mode_  = CoordMode::Name;
        vision_enter_ = 0;
    }
    ImGui::SameLine(0, px(16));
    for (const auto side : {CoordSide::White, CoordSide::Black, CoordSide::Random}) {
        const char* label = side == CoordSide::White   ? TR("Seen from White")
                            : side == CoordSide::Black ? TR("Seen from Black")
                                                       : TR("At random");
        ui::sameLineOrWrap(label);
        if (chip(label, vision_side_ == side, coordKey(CoordMode::Find, side).c_str())) {
            vision_side_  = side;
            vision_enter_ = 0;
        }
    }
    {
        const auto best = [&](CoordSide side) {
            const auto it = record.coordinates.find(coordKey(vision_mode_, side));
            return it == record.coordinates.end() ? 0 : it->second;
        };
        if (best(CoordSide::White) + best(CoordSide::Black) + best(CoordSide::Random) > 0)
            ImGui::TextDisabled("%s", trf("Your records: {} seen from White · {} seen from Black · {} at random",
                                          best(CoordSide::White), best(CoordSide::Black), best(CoordSide::Random))
                                          .c_str());
    }
    if (primaryButton(TR("Start · 30 s"), lock.empty()) || (enter && vision_enter_ == 0 && lock.empty()))
        startCoordinates();
    ImGui::SameLine(0, px(8));
    ImGui::TextDisabled("%s", lock.empty() ? TR("Enter starts, Escape stops; the board without its letters and figures "
                                                "while it lasts.")
                                           : lock.c_str());
    ImGui::PopTextWrapPos();
    ui::endCard();
    ImGui::Dummy(ImVec2(0, px(4)));

    // Blindfold.
    ui::beginCard("##vision_blindfold");
    ImGui::PushTextWrapPos();
    ui::inlineIcon(ui::Icon::Eye, ui::ACCENT);
    ImGui::SameLine();
    ImGui::PushFont(ui::titleFont(), ImGui::GetStyle().FontSizeBase);
    ImGui::TextUnformatted(TR("Blindfold"));
    ImGui::PopFont();
    // The engine, its strength and its openings are the Play page's (#83): one place to choose the opponent.
    const auto& play          = playSettings();
    const EngineEntry* engine = engineNamed(play.engine);
    if (engine && engine->inactive) engine = nullptr;
    if (!engine) engine = analysisEngine();
    if (!engine) {
        wrapped(TR("A game against an engine of your library: add one on the Engines page first."), true);
    } else {
        std::string text, tag, note;
        uint64_t least = 1;
        (void)openingSource(play, vision_colour_ == 1 ? chess::Color::WHITE : chess::Color::BLACK, text, tag, note,
                            least, false);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(
            trf("Against {} · {} · its openings: {}", engine->name, strengthText(strengthFor(play, *engine)), text)
                .c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton(TR("Change on the Play page"))) page_ = Page::Play;
        for (int c = 0; c < 3; ++c) {
            const char* label = c == 0 ? TR("You: White") : c == 1 ? TR("You: Black") : TR("You: either, at random");
            if (c > 0) ui::sameLineOrWrap(label);
            if (chip(label, vision_colour_ == c, fmt::format("colour{}", c).c_str())) {
                vision_colour_ = c;
                vision_enter_  = 1;
            }
        }
        ImGui::SameLine(0, px(16));
        for (const auto h : {Hidden::All, Hidden::AfterTen, Hidden::AllButPawns}) {
            const char* label = h == Hidden::All        ? TR("Every piece hidden")
                                : h == Hidden::AfterTen ? TR("Hidden after the 10th move")
                                                        : TR("All but the pawns");
            ui::sameLineOrWrap(label);
            if (chip(label, vision_hidden_ == h, fmt::format("hidden{}", static_cast<int>(h)).c_str())) {
                vision_hidden_ = h;
                vision_enter_  = 1;
            }
        }
        if (primaryButton(TR("Start the game"), lock.empty()) || (enter && vision_enter_ == 1 && lock.empty()))
            startBlindfold();
        ImGui::SameLine(0, px(8));
        ImGui::TextDisabled("%s",
                            TR("Your moves typed, or on the board; Shift held shows the pieces 2 s, three times a "
                               "game; F2 asks where a piece is."));
        if (record.blind > 0)
            ImGui::TextDisabled(
                "%s", trn("{} game played blindfold", "{} games played blindfold", record.blind, record.blind).c_str());
    }
    ImGui::PopTextWrapPos();
    ui::endCard();
    ImGui::Dummy(ImVec2(0, px(4)));

    // A line read in one's head.
    ui::beginCard("##vision_lines");
    ImGui::PushTextWrapPos();
    ImGui::PushFont(ui::titleFont(), ImGui::GetStyle().FontSizeBase);
    ImGui::TextUnformatted(TR("A line read in your head"));
    ImGui::PopFont();
    wrapped(TR("Six moves read from a position of a well-known opening, without the pieces moving, then a question: "
               "which piece is on a square they changed?"),
            true);
    if (primaryButton(trn("Start · {} line", "Start · {} lines", LINES, LINES).c_str(), lock.empty()) ||
        (enter && vision_enter_ == 2 && lock.empty()))
        startHeadLines();
    if (ImGui::IsItemHovered()) vision_enter_ = 2;
    if (record.lines > 0) {
        ImGui::SameLine(0, px(8));
        ImGui::TextDisabled("%s", trf("{} right of {} so far", record.lines_right, record.lines).c_str());
    }
    ImGui::PopTextWrapPos();
    ui::endCard();
}

// ---------------------------------------------------------------------------
// The sessions

void App::startVisionRun(std::unique_ptr<VisionRun> run) {
    if (training_) closeTraining(false);
    if (puzzle_) closePuzzles(false);
    if (mistake_) closeMistakes(false);
    if (guess_) closeGuess(false);
    if (endgame_) closeEndgame(false);
    if (vision_) closeVision(false);
    vision_error_.clear();
    run->flip     = board_.flip;
    run->tab      = board_side_tab_;
    const auto& b = board_;
    run->aside    = !b.game.moves.empty() || !b.tree.variations.empty() || !b.title.empty();
    toStudyBoard();
    openOnBoard(ReplayGame{}, 0, false, TR("Board vision"), false);
    board_back_.reset();
    run->began = Clock::now();
    vision_    = std::move(run);
}

void App::startCoordinates() {
    if (!trainingLock().empty()) return;
    auto run           = std::make_unique<VisionRun>();
    run->kind          = VKind::Coordinates;
    run->mode          = vision_mode_;
    run->side          = vision_side_;
    run->phase         = VPhase::Ready;
    const auto& record = visionRecord().coordinates;
    const auto it      = record.find(coordKey(run->mode, run->side));
    run->record_before = it == record.end() ? 0 : it->second;
    run->flipped       = run->side == CoordSide::Black;
    startVisionRun(std::move(run));
    board_.flip = vision_->flipped;
}

void App::startBlindfold() {
    if (!trainingLock().empty()) return;
    // The engine, its strength and its openings as the Play page chose them (#83).
    auto play                = playSettings();
    const EngineEntry* found = engineNamed(play.engine);
    if (found && found->inactive) found = nullptr;
    if (!found) found = analysisEngine();
    if (!found) {
        vision_error_ = TR("A game against an engine of your library: add one on the Engines page first.");
        return;
    }
    EngineEntry engine  = *found;
    const auto strength = strengthFor(play, engine);
    applyStrength(engine, strength);
    auto run      = std::make_unique<VisionRun>();
    run->kind     = VKind::Blindfold;
    run->hidden   = vision_hidden_;
    run->engine   = engine.name;
    run->strength = strengthText(strength);
    run->you      = vision_colour_ == 0     ? chess::Color::WHITE
                    : vision_colour_ == 1   ? chess::Color::BLACK
                    : (run->rng() % 2 == 0) ? chess::Color::WHITE
                                            : chess::Color::BLACK;
    EngineEntry you;
    you.name = humanName();
    BoardMatchSetup setup;
    const bool white = run->you == chess::Color::WHITE;
    setup.nodes      = white ? std::array<uint64_t, 2>{0, strength.nodes} : std::array<uint64_t, 2>{strength.nodes, 0};
    {
        std::string text, tag, note;
        uint64_t least = 1;
        if (auto source =
                openingSource(play, white ? chess::Color::BLACK : chess::Color::WHITE, text, tag, note, least)) {
            auto book  = std::make_shared<OpeningBook>(std::move(source), play.vary, least, run->rng());
            setup.book = [book](const chess::Board& board) { return book->move(board); };
        }
    }
    setup.first      = white ? you.name : engine.name;
    setup.second     = white ? engine.name : you.name;
    // The user's clock is long, the engine's short: a game seen in the head is not a race.
    const GameClock slow{3600.0, 30.0}, fast{60.0, 1.0};
    setup.first_clock  = white ? slow : fast;
    setup.second_clock = white ? fast : slow;
    setup.fen          = std::string(chess::constants::STARTPOS);
    setup.human        = white ? 0 : 1;
    std::error_code ec;
    const auto dir = storage_.root() / "games";
    std::filesystem::create_directories(dir, ec);
    run->match = std::make_unique<BoardMatch>();
    std::string error;
    if (!run->match->start(white ? you : engine, white ? engine : you, setup, dir / "blindfold.pgn", error)) {
        vision_error_ = error;
        return;
    }
    run->phase = VPhase::Asking;
    startVisionRun(std::move(run));
    board_.flip  = !white;
    board_.title = trf("Blindfold · {}", engine.name);
}

void App::startHeadLines() {
    if (!trainingLock().empty()) return;
    auto run  = std::make_unique<VisionRun>();
    run->kind = VKind::Line;
    for (std::size_t i = 0; i < LINES; ++i) {
        auto line = headLine(run->rng);
        if (line.fen.empty()) break;
        run->lines.push_back(std::move(line));
    }
    if (run->lines.empty()) return;
    run->phase = VPhase::Asking;
    startVisionRun(std::move(run));
    board_.flip = chess::Board(vision_->lines.front().fen).sideToMove() == chess::Color::BLACK;
}

void App::visionNextSquare() {
    auto& v   = *vision_;
    v.asked   = nextCoordinate(v.rng, v.asked == chess::Square::NO_SQ ? std::nullopt : std::optional(v.asked));
    v.flipped = v.side == CoordSide::Random ? v.rng() % 2 == 1 : v.side == CoordSide::Black;
    v.typed.clear();
    board_.flip = v.flipped;
}

void App::visionSquare(chess::Square square) {
    auto& v = *vision_;
    if (v.kind == VKind::Coordinates && v.phase == VPhase::Asking && v.mode == CoordMode::Find) {
        if (square == v.asked) {
            ++v.right;
        } else {
            ++v.faults;
            v.wrong       = square;
            v.missed      = v.asked;
            v.answered_at = Clock::now();
        }
        visionNextSquare();
        return;
    }
    if (v.kind == VKind::Blindfold && v.where && !v.where_said) {
        v.where_said = square;
        auto& record = visionRecord();
        ++record.where;
        if (square == v.where->square) ++record.where_right;
        saveVisionRecord();
    }
}

void App::visionAnswer(chess::Move move) {
    if (!vision_) return;
    auto& v = *vision_;
    if (v.kind != VKind::Blindfold || !v.match || !v.state.running || !v.state.you || v.state.thinking != v.state.you ||
        v.where)
        return;
    v.match->play(chess::uci::moveToUci(move, false));
    board_.selected.reset();
    board_.move_typed.reset();
}

void App::finishVision(bool stopped) {
    auto& v = *vision_;
    if (v.phase == VPhase::End) return;
    v.phase      = VPhase::End;
    auto& record = visionRecord();
    switch (v.kind) {
        case VKind::Coordinates:
            if (!stopped) {
                auto& best = record.coordinates[coordKey(v.mode, v.side)];
                best       = std::max(best, v.right);
                saveVisionRecord();
            }
            break;
        case VKind::Blindfold:
            if (v.match) v.match->stop();
            if (!v.counted && v.state.current.moves.size() >= 2) {
                v.counted = true;
                ++record.blind;
                saveVisionRecord();
            }
            break;
        case VKind::Line:
            record.lines += static_cast<int>(std::min(v.line + (v.said ? 1 : 0), v.lines.size()));
            record.lines_right += v.lines_right;
            saveVisionRecord();
            break;
    }
    board_.selected.reset();
    board_.move_typed.reset();
}

void App::closeVision(bool to_training) {
    if (!vision_) return;
    if (vision_->match) vision_->match->stop();
    if (to_training) training_tab_ = 5;  // back to the Vision part
    const bool aside = vision_->aside, flip = vision_->flip;
    board_side_tab_ = vision_->tab;
    vision_.reset();
    forget_board_game_ = true;
    if (aside && !recent_games_.empty())
        restoreRecentGame(0);
    else
        newBoardGame();
    forget_board_game_ = false;
    board_.flip        = flip;
    board_back_.reset();
    page_ = to_training ? Page::Training : Page::Board;
}

void App::dropVision() {
    if (vision_->match) vision_->match->stop();
    board_.flip     = vision_->flip;
    board_side_tab_ = vision_->tab;
    vision_.reset();
    forget_board_game_ = true;
}

void App::pollVision() {
    auto& v = *vision_;
    // Fair play (#48): an engine that computes waits for a live game's end; the blindfold game stops there.
    if (v.kind == VKind::Blindfold && liveGameOn() && v.phase != VPhase::End) {
        vision_error_ = TR("The blindfold game stopped: one of your games is played live on Lichess.");
        return finishVision(true);
    }
    if (v.kind == VKind::Coordinates && v.phase == VPhase::Asking && since(v.began) >= COORD_SECONDS)
        return finishVision(false);
    if (v.kind != VKind::Blindfold || !v.match) return;
    if (v.match->state(v.state, v.version)) {
        if (!v.state.running) v.match->reap();
        // The board's game follows the one played: the moves typed are read on its position.
        auto& b = board_;
        b.game  = v.state.current;
        b.tree  = GameTree{b.game, {}};
        b.line  = -1;
        b.ply   = static_cast<int>(b.game.moves.size());
        b.selected.reset();
        if (!v.state.running && v.phase != VPhase::End) finishVision(false);
    }
}

// ---------------------------------------------------------------------------
// The Board page during an exercise

void App::visionKeys() {
    auto& v        = *vision_;
    const auto& io = ImGui::GetIO();
    // A field has the keys (the move typed): Enter and Escape are its own.
    if (io.WantTextInput || ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) return;
    const auto owner = ImGui::GetID("##vision_keys");
    for (const auto key : {ImGuiKey_Enter, ImGuiKey_KeypadEnter, ImGuiKey_Escape, ImGuiKey_F2})
        ImGui::SetKeyOwner(key, owner);
    const bool plain = !io.KeyCtrl && !io.KeyAlt && !io.KeySuper;
    const bool enter = plain && !io.WantTextInput &&
                       (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false));
    const bool escape = ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !io.WantTextInput;
    if (v.phase == VPhase::End) {
        if (escape) return closeVision(true);
        if (enter) {
            const auto kind = v.kind;
            closeVision(false);
            if (kind == VKind::Coordinates)
                startCoordinates();
            else if (kind == VKind::Blindfold)
                startBlindfold();
            else
                startHeadLines();
        }
        return;
    }
    if (escape) return finishVision(true);
    switch (v.kind) {
        case VKind::Coordinates:
            if (v.phase == VPhase::Ready) {
                if (enter) {
                    v.phase = VPhase::Asking;
                    v.began = Clock::now();
                    visionNextSquare();
                }
            } else if (v.mode == CoordMode::Name && !io.WantTextInput) {
                // Two keys: a file, then a rank.
                for (const ImWchar c : io.InputQueueCharacters) {
                    const char k = static_cast<char>(std::tolower(static_cast<int>(c)));
                    if (v.typed.empty() ? (k >= 'a' && k <= 'h') : (k >= '1' && k <= '8')) v.typed += k;
                    if (v.typed.size() == 2) {
                        const auto sq = typedSquare(v.typed);
                        if (sq == v.asked) {
                            ++v.right;
                        } else {
                            ++v.faults;
                            v.wrong       = sq;
                            v.missed      = v.asked;
                            v.answered_at = Clock::now();
                        }
                        visionNextSquare();
                    }
                }
                if (ImGui::IsKeyPressed(ImGuiKey_Backspace)) v.typed.clear();
            }
            break;
        case VKind::Blindfold: {
            // Shift held alone a moment shows the pieces, a few times a game.
            const bool shift = io.KeyShift && plain;
            static Clock::time_point shift_since{};
            static bool typed_since = false;
            if (shift && shift_since == Clock::time_point{}) {
                shift_since = Clock::now();
                typed_since = false;
            }
            if (!io.InputQueueCharacters.empty()) typed_since = true;
            if (!shift) {
                shift_since = {};
                v.peeking   = false;
            } else if (!v.peeking && !typed_since && !io.WantTextInput && v.peeks > 0 &&
                       since(shift_since) >= PEEK_DELAY && Clock::now() > v.peek_until) {
                v.peeking    = true;
                v.peek_until = Clock::now() + std::chrono::milliseconds(static_cast<int>(PEEK_SECONDS * 1000));
                --v.peeks;
            }
            if (v.peeking && Clock::now() >= v.peek_until) v.peeking = false;
            if (ImGui::IsKeyPressed(ImGuiKey_F2, false) && !v.where) {
                v.where = pieceQuestion(boardAt(board_.game, false, board_.ply), v.rng);
                v.where_said.reset();
                v.typed.clear();
                board_.move_typed.reset();
            } else if (v.where && !v.where_said && !io.WantTextInput) {
                for (const ImWchar c : io.InputQueueCharacters) {
                    const char k = static_cast<char>(std::tolower(static_cast<int>(c)));
                    if (v.typed.empty() ? (k >= 'a' && k <= 'h') : (k >= '1' && k <= '8')) v.typed += k;
                    if (v.typed.size() == 2) {
                        if (const auto sq = typedSquare(v.typed)) visionSquare(*sq);
                        v.typed.clear();
                    }
                }
            } else if (v.where && v.where_said && enter) {
                v.where.reset();
                v.where_said.reset();
            } else if (!v.where && v.state.running && v.state.you && v.state.thinking == v.state.you) {
                boardMoveKeys();
            }
            break;
        }
        case VKind::Line: {
            if (v.line >= v.lines.size()) break;
            if (v.phase == VPhase::Asking && !io.WantTextInput) {
                for (const ImWchar c : io.InputQueueCharacters) {
                    if (c > 127) continue;
                    if (const auto p = typedPiece(static_cast<char>(c))) {
                        v.said = *p;
                        if (*p == v.lines[v.line].answer) ++v.lines_right;
                        v.phase = VPhase::Answered;
                        break;
                    }
                }
            } else if (v.phase == VPhase::Answered && enter) {
                ++v.line;
                v.said.reset();
                if (v.line >= v.lines.size()) return finishVision(false);
                v.phase     = VPhase::Asking;
                board_.flip = chess::Board(v.lines[v.line].fen).sideToMove() == chess::Color::BLACK;
            }
            break;
        }
    }
}

void App::drawVisionPage() {
    ui::pageTitle(TR("Board"), guideSection());
    if (!ImGui::GetIO().WantTextInput && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId) &&
        !SDL_TextInputActive(window_))
        SDL_StartTextInput(window_);
    visionKeys();
    if (!vision_) return;
    auto& v            = *vision_;
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float side   = std::clamp(avail.x * 0.42f, px(340), px(620));
    const float under  = ImGui::GetTextLineHeightWithSpacing() * 4;
    const float size   = std::floor(
        std::max(px(200), std::min(avail.x - side - ImGui::GetStyle().ItemSpacing.x - px(20), avail.y - under)));
    ImGui::BeginGroup();
    {
        const auto start = ImGui::GetCursorPosX();
        std::string what, where;
        switch (v.kind) {
            case VKind::Coordinates:
                what  = v.mode == CoordMode::Find ? TR("Coordinates · find the square")
                                                  : TR("Coordinates · name the square");
                where = v.flipped ? TR("seen from Black") : TR("seen from White");
                break;
            case VKind::Blindfold:
                what  = v.strength.empty() ? trf("Blindfold · {}", v.engine)
                                           : trf("Blindfold · {} · {}", v.engine, v.strength);
                where = v.state.current.moves.empty() ? std::string()
                                                      : trf("move {}", (v.state.current.moves.size() + 2) / 2);
                break;
            case VKind::Line:
                if (v.line < v.lines.size()) what = trf("A line read in your head · {}", TR(v.lines[v.line].opening));
                where = trf("{} of {}", std::min(v.line + 1, v.lines.size()), v.lines.size());
                break;
        }
        ImGui::TextDisabled("%s", what.c_str());
        if (!where.empty()) {
            ImGui::SameLine(
                std::max(ImGui::GetCursorPosX() + px(10), start + size - ImGui::CalcTextSize(where.c_str()).x));
            ImGui::TextDisabled("%s", where.c_str());
        }
    }
    drawVisionBoard(size);
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + size);
    switch (v.kind) {
        case VKind::Coordinates:
            ImGui::TextDisabled("%s", v.mode == CoordMode::Find
                                          ? TR("Click the square asked; a fault shows it in green, yours in orange.")
                                          : TR("Type the square marked: its letter, then its figure."));
            break;
        case VKind::Blindfold:
            ImGui::TextDisabled("%s", v.phase == VPhase::End ? TR("The game's pieces, shown.")
                                      : v.peeking ? TR("The pieces, for a moment.")
                                                  : TR("The pieces are hidden; only the last move is marked."));
            break;
        case VKind::Line:
            ImGui::TextDisabled("%s", v.phase == VPhase::Answered
                                          ? TR("The position after the moves read.")
                                          : TR("The position before the moves: read them without moving the pieces."));
            break;
    }
    ImGui::PopTextWrapPos();
    ImGui::EndGroup();
    ImGui::SameLine(0, px(18));
    ImGui::BeginChild("##vision_side", ImVec2(0, 0));
    drawVisionColumn();
    ImGui::EndChild();
}

void App::drawVisionBoard(float size) {
    auto& v = *vision_;
    auto& b = board_;
    BoardDecor decor;
    decor.flipped     = b.flip;
    decor.eval_white  = -1.0f;
    const auto green  = markColour('G', true);
    const auto orange = IM_COL32(236, 140, 52, 150);
    const auto blue   = markColour('B', true);
    std::array<chess::Piece, 64> pieces;
    pieces.fill(chess::Piece::NONE);
    bool moves = false;
    chess::Board board;
    switch (v.kind) {
        case VKind::Coordinates:
            decor.coordinates = false;
            if (v.phase == VPhase::Asking && v.mode == CoordMode::Name && v.asked != chess::Square::NO_SQ)
                decor.tints.push_back({v.asked, blue});
            if (v.missed && since(v.answered_at) < FAULT_SECONDS) {
                decor.tints.push_back({*v.missed, green});
                if (v.wrong) decor.tints.push_back({*v.wrong, orange});
            }
            break;
        case VKind::Blindfold: {
            board  = boardAt(b.game, false, b.ply);
            pieces = shownPieces(board, v.hidden, v.peeking || v.phase == VPhase::End);
            if (b.ply > 0) decor.last_move = b.game.moves[static_cast<std::size_t>(b.ply - 1)].move;
            if (v.where && v.where_said) {
                decor.tints.push_back({v.where->square, green});
                if (*v.where_said != v.where->square) decor.tints.push_back({*v.where_said, orange});
            }
            moves = !v.where && v.phase != VPhase::End && v.state.running && v.state.you &&
                    v.state.thinking == v.state.you && !liveGameOn();
            break;
        }
        case VKind::Line:
            if (v.line < v.lines.size()) {
                const auto& l = v.lines[v.line];
                board         = chess::Board(l.fen);
                if (v.phase == VPhase::Answered)
                    for (const auto m : l.moves) board.makeMove(m);
                pieces           = piecesOfBoard(board);
                const bool right = v.said && *v.said == l.answer;
                decor.tints.push_back({l.square, v.phase == VPhase::Answered ? (right ? green : orange) : blue});
            }
            break;
    }
    decor.selected = moves ? b.selected : std::nullopt;
    if (decor.selected)
        for (const auto& m : movesFrom(board, *decor.selected)) decor.targets.push_back(shownTarget(m));
    const ImVec2 top = ImGui::GetCursorScreenPos();
    // The flat board always: the 3D one has no hidden pieces nor a board without its letters.
    drawBoard(atlas_, pieces, size, decor);
    const auto hover = boardSquareAt(ImGui::GetMousePos(), top, size, decor);
    const bool left  = ImGui::IsItemClicked(ImGuiMouseButton_Left);
    if (moves) {
        boardPointer(board, left ? hover : std::nullopt, hover, ImGui::IsItemClicked(ImGuiMouseButton_Right));
        drawPromotionPopup(board);
    } else if (left && hover) {
        visionSquare(*hover);
    }
}

void App::drawVisionColumn() {
    auto& v = *vision_;
    const ui::TopicRegion topic(guide::topics::Vision);
    ImGui::PushTextWrapPos();
    switch (v.kind) {
        case VKind::Coordinates: {
            beginTinted("##coord_card", ui::CARD_BD);
            const double left = v.phase == VPhase::Asking  ? std::max(0.0, COORD_SECONDS - since(v.began))
                                : v.phase == VPhase::Ready ? COORD_SECONDS
                                                           : 0.0;
            ImGui::TextDisabled("%s", (v.mode == CoordMode::Find ? TR("Coordinates · find the square")
                                                                 : TR("Coordinates · name the square")));
            ImGui::SameLine();
            ImGui::TextDisabled("· %d:%02d", static_cast<int>(left) / 60, static_cast<int>(std::ceil(left)) % 60);
            if (v.phase == VPhase::Ready) {
                ImGui::PushFont(ui::titleFont(), ImGui::GetStyle().FontSizeBase * 1.3f);
                ImGui::TextUnformatted(TR("Ready?"));
                ImGui::PopFont();
                wrapped(v.mode == CoordMode::Find ? TR("A square is named: click it on the board, which has no letters "
                                                       "nor figures while it lasts.")
                                                  : TR("A square is marked in blue: type its name, a letter and a "
                                                       "figure."),
                        true);
                if (primaryButton((std::string(TR("Start")) + " · " + TR("Enter")).c_str())) {
                    v.phase = VPhase::Asking;
                    v.began = Clock::now();
                    visionNextSquare();
                }
            } else if (v.phase == VPhase::Asking) {
                ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 3.0f);
                ImGui::TextUnformatted(v.mode == CoordMode::Find ? squareName(v.asked).c_str()
                                                                 : (v.typed.empty() ? "…" : v.typed.c_str()));
                ImGui::PopFont();
                std::string line = trn("{} right · {} fault", "{} right · {} faults", v.faults, v.right, v.faults);
                if (v.missed) line += " · " + trf("the last, {}, in green", squareName(*v.missed));
                ImGui::TextUnformatted(line.c_str());
            } else {
                ImGui::PushFont(ui::titleFont(), ImGui::GetStyle().FontSizeBase * 1.3f);
                ImGui::TextUnformatted(trn("{} square in 30 s", "{} squares in 30 s", v.right, v.right).c_str());
                ImGui::PopFont();
                ImGui::TextUnformatted(trn("{} fault", "{} faults", v.faults, v.faults).c_str());
                if (v.right > v.record_before)
                    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::GOOD), "%s",
                                       v.record_before > 0 ? trf("A new record: {} before.", v.record_before).c_str()
                                                           : TR("Your first record."));
                else
                    ImGui::TextDisabled("%s", trf("Your record: {}.", v.record_before).c_str());
            }
            endTinted();
            break;
        }
        case VKind::Blindfold: {
            beginTinted("##blind_card", ui::CARD_BD);
            const bool white = v.you == chess::Color::WHITE;
            ImGui::PushFont(ui::titleFont(), ImGui::GetStyle().FontSizeBase * 1.1f);
            ImGui::TextUnformatted((white ? trf("Blindfold against {} · you: White", v.engine)
                                          : trf("Blindfold against {} · you: Black", v.engine))
                                       .c_str());
            ImGui::PopFont();
            if (!v.state.error.empty())
                ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s", v.state.error.c_str());
            const auto& g = v.state.current;
            if (!g.moves.empty()) {
                const auto start = boardAt(ReplayGame{}, false, 0);
                std::vector<chess::Move> ms;
                for (const auto& m : g.moves) ms.push_back(m.move);
                wrapped(movesText(start, ms));
            } else {
                wrapped(TR("The game starts: the moves come here as they are played."), true);
            }
            if (v.phase != VPhase::End) {
                if (v.state.running && v.state.you && v.state.thinking == v.state.you && !v.where) {
                    const auto board = boardAt(board_.game, false, board_.ply);
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextUnformatted(fmt::format("{}{}", board.fullMoveNumber(),
                                                       board.sideToMove() == chess::Color::WHITE ? "." : "…")
                                               .c_str());
                    ImGui::SameLine();
                    if (!board_.move_typed) ImGui::TextDisabled("%s", TR("type your move, Enter"));
                    drawMoveField();
                } else if (v.state.running && !v.where) {
                    ImGui::TextDisabled("%s", trf("{} thinks…", v.engine).c_str());
                }
            }
            endTinted();
            ImGui::Dummy(ImVec2(0, px(4)));
            ui::beginCard("##blind_aids");
            ImGui::TextUnformatted(trn("Shift held shows the pieces 2 s · {} more time",
                                       "Shift held shows the pieces 2 s · {} more times", v.peeks, v.peeks)
                                       .c_str());
            if (v.where) {
                ImGui::TextUnformatted(trf("Where is {}?", TR(v.where->text)).c_str());
                if (v.where_said) {
                    const bool right = *v.where_said == v.where->square;
                    ImGui::TextColored(
                        ImGui::ColorConvertU32ToFloat4(right ? ui::GOOD : ui::WARN), "%s",
                        (right ? trf("You: {}, right.", squareName(*v.where_said))
                               : trf("You: {}; it is on {}.", squareName(*v.where_said), squareName(v.where->square)))
                            .c_str());
                    ImGui::TextDisabled("%s", TR("Enter: back to the game"));
                } else {
                    ImGui::TextDisabled("%s", TR("Click its square, or type it."));
                }
            } else {
                ImGui::TextUnformatted(TR("Where is…? F2"));
                ImGui::SameLine();
                ImGui::TextDisabled("%s", TR("· a piece named, its square asked"));
            }
            const char* hidden = v.hidden == Hidden::All        ? TR("Hidden: every piece")
                                 : v.hidden == Hidden::AfterTen ? TR("Hidden: every piece after the 10th move")
                                                                : TR("Hidden: all but the pawns");
            ImGui::TextDisabled("%s", hidden);
            ui::endCard();
            if (v.phase == VPhase::End) {
                ImGui::Dummy(ImVec2(0, px(4)));
                const auto& finished = v.state.finished;
                const auto result    = finished.empty() ? std::string() : finished.back().header("Result");
                ImGui::TextUnformatted(result.empty() || result == "*"
                                           ? TR("The game is stopped; its pieces are shown, its moves kept in "
                                                "games/blindfold.pgn.")
                                           : trf("The game is over: {}. Its pieces are shown, its moves kept in "
                                                 "games/blindfold.pgn.",
                                                 result)
                                                 .c_str());
            }
            break;
        }
        case VKind::Line: {
            if (v.line >= v.lines.size()) break;
            const auto& l = v.lines[v.line];
            beginTinted("##line_card", ui::CARD_BD);
            const chess::Board board(l.fen);
            ImGui::TextDisabled("%s",
                                (board.sideToMove() == chess::Color::WHITE ? trf("{} · White to play", TR(l.opening))
                                                                           : trf("{} · Black to play", TR(l.opening)))
                                    .c_str());
            ImGui::PushFont(ui::titleFont(), ImGui::GetStyle().FontSizeBase * 1.2f);
            wrapped(movesText(board, l.moves));
            ImGui::PopFont();
            ImGui::TextUnformatted(trf("Which piece is on {} now?", squareName(l.square)).c_str());
            if (v.phase == VPhase::Asking) {
                const auto answer = [&](chess::Piece p) {
                    v.said = p;
                    if (p == l.answer) ++v.lines_right;
                    v.phase = VPhase::Answered;
                };
                for (const auto colour : {chess::Color::WHITE, chess::Color::BLACK}) {
                    bool first = true;
                    for (const auto type :
                         {chess::PieceType::KING, chess::PieceType::QUEEN, chess::PieceType::ROOK,
                          chess::PieceType::BISHOP, chess::PieceType::KNIGHT, chess::PieceType::PAWN}) {
                        const chess::Piece p(type, colour);
                        const char* label = pieceWords(p);
                        if (!first) ui::sameLineOrWrap(label);
                        first = false;
                        if (ImGui::Button(fmt::format("{}##answer{}", label, static_cast<int>(p.internal())).c_str()))
                            answer(p);
                    }
                }
                if (ImGui::Button(TR("an empty square"))) answer(chess::Piece::NONE);
                ImGui::TextDisabled("%s", TR("Or type the piece as FEN writes it: N a white knight, n a black one, - "
                                             "for none."));
            } else {
                const bool right = v.said && *v.said == l.answer;
                ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(right ? ui::GOOD : ui::WARN), "%s",
                                   (right ? trf("Right: {}.", pieceWords(l.answer))
                                          : trf("It is {}; you said {}.", pieceWords(l.answer),
                                                pieceWords(v.said.value_or(chess::Piece::NONE))))
                                       .c_str());
                if (primaryButton((std::string(TR("Next")) + " · " + TR("Enter")).c_str())) {
                    ++v.line;
                    v.said.reset();
                    if (v.line >= v.lines.size()) {
                        finishVision(false);
                    } else {
                        v.phase     = VPhase::Asking;
                        board_.flip = chess::Board(v.lines[v.line].fen).sideToMove() == chess::Color::BLACK;
                    }
                }
            }
            ImGui::TextDisabled("%s", trf("{} right so far", v.lines_right).c_str());
            endTinted();
            break;
        }
    }
    ImGui::Dummy(ImVec2(0, px(6)));
    if (v.phase == VPhase::End) {
        if (v.kind == VKind::Line)
            ImGui::TextUnformatted(trf("{} right of {}.", v.lines_right, v.lines.size()).c_str());
        if (primaryButton((std::string(TR("Again")) + " · " + TR("Enter")).c_str())) {
            const auto kind = v.kind;
            closeVision(false);
            if (kind == VKind::Coordinates)
                startCoordinates();
            else if (kind == VKind::Blindfold)
                startBlindfold();
            else
                startHeadLines();
            ImGui::PopTextWrapPos();
            return;
        }
        ImGui::SameLine();
        if (ImGui::Button((std::string(TR("Back to the Training page")) + " · " + TR("Esc")).c_str())) {
            closeVision(true);
            ImGui::PopTextWrapPos();
            return;
        }
    } else if (ImGui::Button(TR("Stop · Esc"))) {
        finishVision(true);
    }
    ImGui::PopTextWrapPos();
}

}  // namespace fcg
