// Endgames against the tablebases and the mates (#82, mockup M12, board 3; endgames.hpp): the Training page's Endgames
// part (the tables there, the catalogue, the endgames of the user's games, the mates of Lichess's puzzles), the tables
// missing (download those of 3 to 5 pieces, a folder already on the disk, Lichess's online), and an endgame played on
// the Board page, whose column becomes the endgame's: the tables answer each move, a fault is said with the moves that
// kept the result, the position before it comes back in the day's review.
//
// The keys: on the page, ↑ ↓ choose an endgame once one was clicked, Enter plays it; during an endgame, a move on the
// board or typed, Ctrl+Z takes a move back, Alt+H shows the right move, Escape stops; after a fault, Enter takes it
// back.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <future>
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
#include "machines.hpp"
#include "online_cache.hpp"
#include "ui.hpp"

namespace fcg {

namespace fs = std::filesystem;

namespace {

float px(float v) { return v * ImGui::GetFontSize() / 16.0f; }

using Clock   = std::chrono::steady_clock;
using EgPhase = EndgameRun::Phase;

double since(Clock::time_point t) { return std::chrono::duration<double>(Clock::now() - t).count(); }

// The tables' move comes after this: time to see one's own.
constexpr double REPLY_SECONDS = 0.5;
// A question to Lichess that gave nothing is asked again after this.
constexpr int RETRY_SECONDS = 10;

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
    ImGui::PushStyleColor(ImGuiCol_Button,
                          ImGui::ColorConvertU32ToFloat4(chosen ? mix(ui::CARD_BG, ui::ACCENT, 0.30f) : ui::CARD_BG));
    ImGui::PushStyleColor(ImGuiCol_Border, ImGui::ColorConvertU32ToFloat4(chosen ? ui::ACCENT : ui::CARD_BD));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    const bool clicked = ImGui::Button(fmt::format("{}##{}", label, id).c_str());
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);
    return clicked;
}

bool enterPressed() {
    return !ImGui::GetIO().WantTextInput && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId) &&
           (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false));
}

// A move with its number as the interface writes it: "24.Nc7+", "23…Qxd4".
std::string numbered(const chess::Board& before, chess::Move move) {
    return fmt::format("{}{}{}", before.fullMoveNumber(), before.sideToMove() == chess::Color::WHITE ? "." : "…",
                       localSan(chess::uci::moveToSan(before, move)));
}

// "win", "mate", "hold the draw": what a task asks.
const char* goalText(const EndgameTask& t) {
    if (t.mate) return TR("mate");
    return t.goal == EndgameGoal::Win ? TR("win") : TR("hold the draw");
}

// The question: "Your move, White: win".
const char* questionText(chess::Color side, const EndgameTask& t) {
    const bool white = side == chess::Color::WHITE;
    if (t.mate) return white ? TR("Your move, White: mate") : TR("Your move, Black: mate");
    if (t.goal == EndgameGoal::Win) return white ? TR("Your move, White: win") : TR("Your move, Black: win");
    return white ? TR("Your move, White: hold the draw") : TR("Your move, Black: hold the draw");
}

// The verdict for the user as the Board's online tables say it: "a win", "a draw", "a loss" (the goal's "win" is a
// verb).
const char* verdictWord(int result) { return result > 0 ? TR("a win") : result < 0 ? TR("a loss") : TR("a draw"); }

// The verdict of the tables, by the side it favours: "The tables: White wins".
const char* tablesVerdict(int user_result, chess::Color user) {
    if (user_result == 0) return TR("The tables: a draw");
    const bool white_wins = (user_result > 0) == (user == chess::Color::WHITE);
    return white_wins ? TR("The tables: White wins") : TR("The tables: Black wins");
}

// The verdict's colour, by the goal: green when it holds the goal, orange for a draw that does not, red for a loss.
bool meetsGoal(int user_result, EndgameGoal goal) {
    return goal == EndgameGoal::Win ? user_result > 0 : user_result >= 0;
}
ImU32 verdictTone(int user_result, EndgameGoal goal) {
    return meetsGoal(user_result, goal) ? ui::GOOD : user_result < 0 ? ui::BAD : ui::WARN;
}

const char* separator() {
#ifdef _WIN32
    return ";";
#else
    return ":";
#endif
}

// The pieces of the board after `moves` from `fen`, its history played (repetitions).
chess::Board playedBoard(const std::string& fen, const std::vector<chess::Move>& moves) {
    chess::Board board(fen);
    for (const auto m : moves) board.makeMove(m);
    return board;
}

// The position the tables are asked about: the moves played, but a move kept (played before their answer came).
chess::Board askedBoard(const EndgameRun& r) {
    auto moves = r.moves;
    if (r.kept && !moves.empty()) moves.pop_back();
    return playedBoard(r.start_fen, moves);
}

}  // namespace

// ---------------------------------------------------------------------------
// The record, the tables

EndgameRecord& App::endgameRecord() {
    if (!endgame_record_) endgame_record_ = readEndgameRecord(endgamesFolder());
    return *endgame_record_;
}

void App::saveEndgameRecord() {
    if (options_.scripted || !endgame_record_) return;
    if (!writeEndgameRecord(endgamesFolder(), *endgame_record_))
        Console::instance().add(trf("The endgames' results could not be kept in {}", endgamesFolder().string()), true);
}

const SyzygyDownload::State& App::syzygyState() {
    // Read again now and then (a folder's files, their marks), not while the download writes them: its progress says.
    const bool busy = syzygy_download_ && syzygy_download_->busy();
    if (!syzygy_state_ || (!busy && since(syzygy_state_at_) > 10.0)) {
        syzygy_state_    = syzygy_download_ ? syzygy_download_->state() : SyzygyDownload::State{};
        syzygy_state_at_ = Clock::now();
    }
    return *syzygy_state_;
}

std::string App::endgameTablesPaths() {
    std::string paths = setup_.tb_paths;
    if (syzygy_download_ && (syzygy_download_->busy() || syzygyState().files > 0)) {
        const auto own  = syzygy_download_->folder().string();
        const auto dirs = splitTablebasePath(paths);
        if (std::find(dirs.begin(), dirs.end(), own) == dirs.end()) paths += (paths.empty() ? "" : separator()) + own;
    }
    return paths;
}

int App::diskTablesPieces() {
    const auto paths = endgameTablesPaths();
    if (paths.empty()) return 0;
    // The most pieces whose WDL tables are all there, those of fewer pieces too (a capture leads to them): a set half
    // downloaded covers what it covers whole.
    const auto covered = [](const TablebaseCheck& c) {
        int n = 0;
        for (int k = 3; k <= c.largest; ++k) {
            if (const auto it = c.missing.find(k); it != c.missing.end() && !it->second.empty()) break;
            n = k;
        }
        return n;
    };
    // Checked again off the interface's thread every few seconds; the first time at once (names and sizes only), so
    // that an endgame chosen at once finds its tables.
    if (const auto* check = tb_checks_.get(paths)) return covered(*check);
    if (disk_tables_of_ != paths) {
        disk_tables_of_     = paths;
        disk_tables_pieces_ = covered(checkTablebases(paths));
    }
    return disk_tables_pieces_;
}

bool App::diskTablesLocked() const {
    if (run_->running() && !run_->remote()) return true;
    for (const auto& r : runs_)
        if (r.run->running() && !r.run->remote()) return true;
    return false;
}

std::string App::endgameNeed(const std::string& fen) {
    const int pieces = piecesOf(fen);
    if (!diskTablesLocked() && diskTablesPieces() >= pieces) return {};
    if (pieces <= 7 && lichess_.tablebase == 1) return {};
    if (pieces > 7) return trf("{} pieces: more than any table knows", pieces);
    if (diskTablesLocked() && diskTablesPieces() >= pieces)
        return TR("Lichess's tables online, the tournament holding those of your disk while it plays");
    return pieces <= 5 ? trf("the tables of {} pieces, on the disk or Lichess's online", pieces)
                       : trf("Lichess's tables online ({} pieces)", pieces);
}

std::string App::endgameLock() const { return trainingLock(); }

void App::startSyzygyDownload() {
    if (!syzygy_download_ || syzygy_download_->busy()) return;
    syzygy_error_.clear();
    syzygy_since_ = Clock::now();
    syzygy_from_  = 0;
    syzygy_download_->start();
}

void App::pollEndgames() {
    // The download: its end, and the tables loaded again as each piece count's files are all there.
    if (syzygy_download_) {
        if (const auto why = syzygy_download_->finished()) {
            syzygy_state_.reset();
            if (why->empty()) {
                // The set is whole: offered to the tournaments, the setup's folders taking it in.
                const auto own  = syzygy_download_->folder().string();
                const auto dirs = splitTablebasePath(setup_.tb_paths);
                if (std::find(dirs.begin(), dirs.end(), own) == dirs.end()) {
                    setup_.tb_paths += (setup_.tb_paths.empty() ? "" : separator()) + own;
                    saveSetupSoon();
                }
                Console::instance().add(trf("The Syzygy tables of 3 to 5 pieces are on the disk: {}", own));
            } else if (*why != "cancelled") {
                syzygy_error_ = *why;
                Console::instance().add(trf("The Syzygy tables' download stopped: {}", *why), true);
            }
        }
        if (syzygy_download_->generation() != syzygy_generation_ && !diskTablesLocked()) {
            syzygy_generation_ = syzygy_download_->generation();
            syzygy_state_.reset();
            const auto lock = lockSyzygy();
            forgetSyzygy();
        }
    }
    if (vision_) pollVision();
    const auto ready = [](auto& f) {
        return f.valid() && f.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
    };
    // Fair play (#48): the tables are an aid; the endgame waits for a live game's end, where it is.
    if (!endgame_ || liveGameOn()) return;
    auto& r = *endgame_;
    // Lichess's answer.
    if (ready(r.asking)) {
        auto answer = r.asking.get();
        if (answer.value && answer.value->outcome != lichess::Outcome::Unknown) {
            keepTablebase(storage_.root(), r.asking_fen, *answer.value);
            r.answers[r.asking_fen] = *answer.value;
            r.waiting.clear();
            r.waiting_status = 0;
            if (r.asking_fen == askedBoard(r).getFen()) {
                r.answer     = std::move(*answer.value);
                r.answer_fen = r.asking_fen;
                r.online     = true;
                endgameAnswered();
            }
        } else {
            r.waiting        = answer.value ? std::string(TR("Lichess's tables do not know this position."))
                                            : (answer.error.empty() ? std::string(TR("no answer")) : answer.error);
            r.waiting_status = answer.status;
            r.retry_at       = Clock::now() + std::chrono::seconds(answer.status == 429 ? 61 : RETRY_SECONDS);
        }
        if (!endgame_) return;
        // An answer for a position left meanwhile (a move taken back): the board's own asked now.
        if (!r.answer && r.waiting.empty() && r.missing.empty() && r.phase != EgPhase::Done && r.phase != EgPhase::End)
            askEndgameTables();
    }
    if (ready(r.win_line_job)) {
        const auto line = r.win_line_job.get();
        if (line.value) r.win_line = *line.value;
    }
    // A question that gave nothing, asked again.
    if (!r.waiting.empty() && !r.asking.valid() && Clock::now() >= r.retry_at && r.phase != EgPhase::Done &&
        r.phase != EgPhase::End)
        askEndgameTables();
    // The tables' move, half a second after the user's.
    if (r.phase == EgPhase::Reply && r.answer && r.answer_fen == playedBoard(r.start_fen, r.moves).getFen() &&
        since(r.reply_at) >= REPLY_SECONDS)
        endgameTablesMove();
}

// ---------------------------------------------------------------------------
// The Training page's part

std::vector<EndgameTask> App::gameEndgames() {
    std::vector<EndgameTask> out;
    for (const auto* e : mistakeExercises()) {
        const auto board = e->board();
        if (piecesOf(board) > 7 || !board.castlingRights().isEmpty()) continue;
        EndgameTask t;
        t.fen = board.getFen();
        // "You – Player_D, 41.Kg2?": the game and the move that let the result go.
        std::string move = e->played;
        if (const auto m = chess::uci::uciToMove(board, e->played); m != chess::Move::NO_MOVE)
            move = numbered(board, m) + (e->judgement == Judgement::Blunder ? "??" : "?");
        t.title = fmt::format("{} – {}, {}", e->white, e->black, move);
        t.goal  = EndgameGoal::Win;  // the tables say which at its start
        out.push_back(std::move(t));
    }
    return out;
}

std::string App::endgameState(const EndgameEntry& entry) {
    std::string out;
    const auto add  = [&](const std::string& part) { out += (out.empty() ? "" : " · ") + part; };
    const int n     = piecesOf(entry.fen);
    const auto need = endgameNeed(entry.fen);
    if (!need.empty())
        add(trf("{} pieces", n));
    else if (diskTablesLocked() || diskTablesPieces() < n)
        add(trf("{} pieces · Lichess", n));
    const auto& results = endgameRecord().results;
    const auto it       = results.find(entry.id);
    if (it == results.end() || it->second.tries == 0) {
        if (need.empty()) add(TR("to do"));
    } else if (it->second.missed_last) {
        add(TR("to do again"));
    } else if (it->second.done > 0) {
        if (entry.mate && it->second.best > 0 && it->second.tables > 0)
            add(trf("done in {} (the tables: {})", it->second.best, it->second.tables));
        else
            add(TR("done"));
    } else {
        add(TR("to do"));
    }
    return out;
}

void App::drawTrainingEndgames() {
    const ui::TopicRegion topic(guide::topics::Endgames);
    bool enter = enterPressed();
    if (enter) {
        const auto owner = ImGui::GetID("##training_enter");
        ImGui::SetKeyOwner(ImGuiKey_Enter, owner);
        ImGui::SetKeyOwner(ImGuiKey_KeypadEnter, owner);
    }
    // What answers: the tables of the disk, Lichess's, neither.
    const int disk     = diskTablesPieces();
    const bool locked  = diskTablesLocked();
    const bool lichess = lichess_.tablebase == 1;
    std::string tables;
    if (disk > 0 && locked)
        tables =
            TR("The tables of your disk are the tournament's while it plays: Lichess's online meanwhile, after "
               "your consent.");
    else if (disk > 0)
        tables = lichess
                     ? trf("The tables: those of your disk (Syzygy, up to {} pieces, the same as your tournaments'); "
                           "beyond, Lichess's online, up to 7 pieces.",
                           disk)
                     : trf("The tables: those of your disk (Syzygy, up to {} pieces, the same as your tournaments'); "
                           "beyond, Lichess's online, up to 7 pieces, after your consent.",
                           disk);
    else if (lichess)
        tables =
            TR("The tables: Lichess's online, up to 7 pieces, each position played sent as its FEN; those of a "
               "disk answer without a network.");
    else
        tables = TR("No tables yet: those of your disk (Syzygy) or Lichess's online, after your consent.");
    const auto& all  = endgameCatalogue();
    std::size_t done = 0;
    for (const auto& e : all)
        if (const auto it = endgameRecord().results.find(e.id); it != endgameRecord().results.end() && it->second.done)
            ++done;
    wrapped(TR("You play, the tables defend: each of your moves is judged at once.") + std::string(" ") + tables + " " +
                trf("{} endgames · {} done.", all.size(), done),
            true);
    ImGui::Dummy(ImVec2(0, px(2)));
    if (!endgame_error_.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(ui::WARN));
        ImGui::TextWrapped("%s", endgame_error_.c_str());
        ImGui::PopStyleColor();
    }
    // The tables missing for the endgame chosen, or for every one; the download under way or stopped. The tables there
    // now (a folder chosen, the download's files), the card goes.
    if (const auto* e = endgame_need_.empty() ? nullptr : endgameEntry(endgame_need_); e && endgameNeed(e->fen).empty())
        endgame_need_.clear();
    const auto& state = syzygyState();
    if ((syzygy_download_ && syzygy_download_->busy()) || !syzygy_error_.empty() ||
        (state.files > 0 && !state.complete)) {
        drawSyzygyDownloadCard();
        ImGui::Dummy(ImVec2(0, px(4)));
    }
    if (!endgame_need_.empty() || (disk == 0 && !lichess)) {
        bool card_enter = enter && !endgame_keys_;
        drawEndgameTablesCard(card_enter);
        if (!card_enter) enter = false;
        ImGui::Dummy(ImVec2(0, px(4)));
    }
    drawEndgameCatalogue(enter);
    ImGui::Dummy(ImVec2(0, px(4)));
    drawEndgamesFromGames();
    ImGui::Dummy(ImVec2(0, px(4)));
    if (endgame_scroll_mates_) {
        ImGui::SetScrollHereY(0.0f);
        endgame_scroll_mates_ = false;
    }
    drawMatesCard();
}

void App::drawEndgameCatalogue(bool& enter) {
    const auto& all   = endgameCatalogue();
    const auto lock   = endgameLock();
    endgame_selected_ = std::clamp(endgame_selected_, 0, static_cast<int>(all.size()) - 1);
    // The arrows move over the endgames once one was clicked; Enter plays the one chosen.
    if (endgame_keys_ && !ImGui::GetIO().WantTextInput && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) {
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))
            endgame_selected_ = std::min(endgame_selected_ + 1, static_cast<int>(all.size()) - 1);
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) endgame_selected_ = std::max(endgame_selected_ - 1, 0);
    }
    std::optional<std::size_t> start;
    if (enter && endgame_keys_) {
        start = static_cast<std::size_t>(endgame_selected_);
        enter = false;
    }
    const float gap   = px(10);
    const float width = (ImGui::GetContentRegionAvail().x - gap) / 2.0f;
    const auto group  = [&](EndgameGroup g, const char* id) {
        ui::beginCard(id, ImVec2(width, 0));
        ImGui::TextDisabled("%s", TR(endgameGroupName(g)));
        for (std::size_t i = 0; i < all.size(); ++i) {
            const auto& e = all[i];
            if (e.group != g) continue;
            const bool chosen = endgame_keys_ && static_cast<int>(i) == endgame_selected_;
            const auto need   = endgameNeed(e.fen);
            const auto state  = endgameState(e);
            ImGui::PushID(static_cast<int>(i));
            const auto at   = ImGui::GetCursorScreenPos();
            const float row = ImGui::GetTextLineHeight() + px(10);
            const float w   = ImGui::GetContentRegionAvail().x;
            if (ImGui::InvisibleButton("##endgame", ImVec2(std::max(1.0f, w), row))) {
                endgame_selected_ = static_cast<int>(i);
                endgame_keys_     = true;
                start             = i;
            }
            const bool hovered = ImGui::IsItemHovered();
            if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            auto* dl = ImGui::GetWindowDrawList();
            if (chosen || hovered)
                dl->AddRectFilled(at, ImVec2(at.x + w, at.y + row),
                                  chosen ? mix(ui::CARD_BG, ui::ACCENT, 0.18f) : mix(ui::CARD_BG, ui::CARD_BD, 0.5f),
                                  px(6));
            const ImU32 text = ImGui::GetColorU32(need.empty() ? ImGuiCol_Text : ImGuiCol_TextDisabled);
            const ImU32 grey = ImGui::GetColorU32(ImGuiCol_TextDisabled);
            const float y    = at.y + px(5);
            ImGui::PushFont(ui::titleFont(), ImGui::GetStyle().FontSizeBase);
            const char* name = TR(e.name);
            dl->AddText(ImVec2(at.x + px(8), y), text, name);
            const float name_w = ImGui::CalcTextSize(name).x;
            ImGui::PopFont();
            const auto goal = std::string(" · ") + goalText(taskOf(e));
            dl->AddText(ImVec2(at.x + px(8) + name_w, y), grey, goal.c_str());
            const float state_w = ImGui::CalcTextSize(state.c_str()).x;
            dl->AddText(ImVec2(at.x + w - px(8) - state_w, y), grey, state.c_str());
            if (hovered) tip(need.empty() ? std::string(TR(e.idea)) : trf("This endgame needs {}.", need));
            ImGui::PopID();
        }
        ui::endCard();
    };
    ImGui::BeginGroup();
    group(EndgameGroup::Mates, "##eg_mates");
    group(EndgameGroup::Rooks, "##eg_rooks");
    ImGui::EndGroup();
    ImGui::SameLine(0, gap);
    ImGui::BeginGroup();
    group(EndgameGroup::Pawns, "##eg_pawns");
    group(EndgameGroup::Pieces, "##eg_pieces");
    ImGui::EndGroup();
    ImGui::TextDisabled("%s", lock.empty() ? TR("A click plays an endgame; then ↑ ↓ choose another and Enter plays it.")
                                           : lock.c_str());
    if (start && lock.empty()) {
        const auto& e = all[*start];
        if (!endgameNeed(e.fen).empty()) {
            endgame_need_ = e.id;  // the card says what it needs
        } else {
            endgame_need_.clear();
            (void)startEndgame(e);
        }
    }
}

void App::drawEndgameTablesCard(bool& enter) {
    const ui::TopicRegion topic(guide::topics::EndgameTables);
    const auto* entry = endgame_need_.empty() ? nullptr : endgameEntry(endgame_need_);
    const int pieces  = entry ? piecesOf(entry->fen) : 5;
    beginTinted("##endgame_tables", ui::WARN);
    ImGui::PushTextWrapPos();
    ui::inlineIcon(ui::Icon::Alert, ui::WARN);
    ImGui::SameLine();
    ImGui::PushFont(ui::titleFont(), ImGui::GetStyle().FontSizeBase * 1.05f);
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s",
                       entry ? trf("No tables for “{}”", TR(entry->name)).c_str() : TR("No tables yet"));
    ImGui::PopFont();
    if (entry) {
        if (diskTablesLocked() && diskTablesPieces() >= pieces)
            wrapped(
                TR("The tables of your disk are the tournament's while it plays: Lichess's online answer meanwhile."));
        else
            wrapped(trf("It has {} pieces: it needs the tables of {} pieces on the disk, or Lichess's online.", pieces,
                        pieces));
    } else {
        wrapped(TR("The endgames are played against the tables: those of your disk, or Lichess's online."));
    }
    ImGui::Spacing();
    const auto option = [&](int value, const std::string& title, const std::string& text) {
        ImGui::PushID(value);
        if (ImGui::RadioButton("##tables_choice", endgame_tables_choice_ == value)) endgame_tables_choice_ = value;
        ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::TextUnformatted(title.c_str());
        ImGui::TextDisabled("%s", text.c_str());
        ImGui::EndGroup();
        if (ImGui::IsItemClicked()) endgame_tables_choice_ = value;
        ImGui::PopID();
    };
    const auto total = syzygy345Bytes();
    if (pieces <= 5)
        option(0, trf("Download the tables of 3 to 5 pieces · {}", formatBytes(total)),
               TR("Syzygy, from Lichess's mirror; a download cut short goes on, each file checked; in the profile, and "
                  "offered to the tournaments."));
    if (pieces <= 5)
        option(1, TR("A folder of tables already on the disk…"),
               setup_.tb_paths.empty()
                   ? std::string(TR("The tournaments' setting (Settings › Adjudication): no folder yet."))
                   : trf("The tournaments' setting (Settings › Adjudication): {}.", setup_.tb_paths));
    option(2, TR("Lichess's, online"),
           lichess_.tablebase == 2
               ? std::string(TR("Up to 7 pieces, a request a position played; you said never on the Lichess page "
                                "(Online for the analysis): choosing it here allows them."))
               : std::string(TR("Up to 7 pieces, a request a position played; the online tables' consent, once (the "
                                "Lichess page, Online for the analysis).")));
    if (pieces > 5) endgame_tables_choice_ = 2;
    ImGui::Spacing();
    const char* label          = endgame_tables_choice_ == 0   ? nullptr
                                 : endgame_tables_choice_ == 1 ? TR("Choose the folder…")
                                                               : TR("Use Lichess's tables");
    const std::string download = trf("Download · {}", formatBytes(total));
    const bool busy            = syzygy_download_ && syzygy_download_->busy();
    if (primaryButton(label ? label : download.c_str(), !busy || endgame_tables_choice_ != 0) || (enter && !busy)) {
        enter = false;
        switch (endgame_tables_choice_) {
            case 0:
                startSyzygyDownload();
                break;
            case 1:
                dialogs_.openFolder(Dialogs::Purpose::Tablebases);
                break;
            default:
                lichess_.tablebase = 1;
                saveLichessSettings();
                break;
        }
        if (endgame_tables_choice_ == 2 && entry) {
            const auto id = endgame_need_;
            endgame_need_.clear();
            if (const auto* e = endgameEntry(id)) (void)startEndgame(*e);
        }
    }
    ImGui::SameLine(0, px(8));
    ImGui::TextDisabled("%s",
                        endgame_tables_choice_ == 0 ? TR("Enter · the time said once the pace is known") : TR("Enter"));
    if (entry) {
        ImGui::SameLine(0, px(14));
        if (ImGui::SmallButton(TR("Not now##endgame_tables"))) endgame_need_.clear();
    }
    wrapped(TR("Without tables, the endgames that need them are greyed, each with what it needs; the other exercises "
               "work. Online, a cut makes the endgame wait: your moves are kept, the judging goes on when the network "
               "comes back."),
            true);
    wrapped(
        TR("What leaves the machine: with the tables of the disk, nothing; with Lichess's, each position played, as "
           "its FEN; the download, on your click, its size said before."),
        true);
    ImGui::PopTextWrapPos();
    endTinted();
}

void App::drawSyzygyDownloadCard() {
    if (!syzygy_download_) return;
    const ui::TopicRegion topic(guide::topics::EndgameTables);
    auto& d          = *syzygy_download_;
    const auto total = syzygy345Bytes();
    ui::beginCard("##syzygy_download");
    ImGui::PushTextWrapPos();
    ui::inlineIcon(ui::Icon::Download, ImGui::GetColorU32(ImGuiCol_Text));
    ImGui::SameLine();
    ImGui::PushFont(ui::titleFont(), ImGui::GetStyle().FontSizeBase);
    ImGui::TextUnformatted(TR("Syzygy tables of 3 to 5 pieces"));
    ImGui::PopFont();
    if (d.busy()) {
        const auto& p    = d.progress();
        const auto bytes = p.bytes();
        if (syzygy_from_ == 0 && bytes > 0) syzygy_from_ = bytes;
        const double seconds = since(syzygy_since_);
        const double rate =
            seconds > 3 && bytes > syzygy_from_ ? static_cast<double>(bytes - syzygy_from_) / seconds : 0.0;
        ImGui::TextUnformatted(trf("{} of {} · {} files of {}", formatBytes(bytes), formatBytes(total), p.files.load(),
                                   syzygy345Files().size())
                                   .c_str());
        if (rate > 0) {
            ImGui::SameLine();
            ImGui::TextDisabled(
                "%s",
                trf("about {} left", ui::formatDuration(static_cast<double>(total - std::min(total, bytes)) / rate))
                    .c_str());
        }
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImGui::ColorConvertU32ToFloat4(ui::ACCENT));
        ImGui::ProgressBar(static_cast<float>(static_cast<double>(bytes) / static_cast<double>(total)),
                           ImVec2(-1, px(8)), "");
        ImGui::PopStyleColor();
        const int ready = p.ready.load();
        ImGui::TextDisabled("%s", ready >= 3 ? trf("The endgames of {} pieces or fewer are ready; the others come as "
                                                   "their files arrive.",
                                                   ready)
                                                   .c_str()
                                             : TR("The smallest tables first: the endgames of 3 and 4 pieces are "
                                                  "ready within seconds."));
        if (ImGui::Button(TR("Cancel##syzygy_download"))) d.cancel();
    } else {
        const auto& state = syzygyState();
        if (state.complete) {
            ui::inlineIcon(ui::Icon::Check, ui::GOOD);
            ImGui::SameLine();
            ImGui::TextUnformatted(
                trf("On your disk ({}), offered to the tournaments: {}", formatBytes(state.bytes), d.folder().string())
                    .c_str());
        } else {
            ImGui::TextUnformatted(trf("The download stopped at {} of {}: {} files of {}.", formatBytes(state.bytes),
                                       formatBytes(total), state.files, syzygy345Files().size())
                                       .c_str());
            if (!syzygy_error_.empty())
                ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s", syzygy_error_.c_str());
            if (ImGui::Button(TR("Go on##syzygy_download"))) startSyzygyDownload();
            tip(TR("The files missing, one after the other; a file cut short goes on from where it stopped."));
        }
    }
    ImGui::TextDisabled("%s", TR("What leaves the machine: the request for each file to tablebase.lichess.ovh."));
    ImGui::PopTextWrapPos();
    ui::endCard();
}

void App::drawEndgamesFromGames() {
    const auto tasks = gameEndgames();
    beginTinted("##endgames_games", ui::ACCENT);
    ImGui::PushTextWrapPos();
    ImGui::PushFont(ui::titleFont(), ImGui::GetStyle().FontSizeBase);
    ImGui::TextUnformatted(TR("From your games"));
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::TextDisabled("%s",
                        trn("· {} endgame of your reviewed games, of 7 pieces or fewer",
                            "· {} endgames of your reviewed games, of 7 pieces or fewer", tasks.size(), tasks.size())
                            .c_str());
    if (tasks.empty()) {
        wrapped(TR("None yet: the positions of 7 pieces or fewer where the reviews of your games saw the win (or the "
                   "draw) slip away come here (see My mistakes)."),
                true);
    } else {
        wrapped(trf("The position where the review saw the win (or the draw) slip away, to play again against the "
                    "tables. For instance: {}.",
                    tasks.front().title),
                true);
        const auto lock = endgameLock();
        if (primaryButton(trn("Start · {} endgame", "Start · {} endgames", tasks.size(), tasks.size()).c_str(),
                          lock.empty()))
            (void)startGameEndgames();
        if (!lock.empty()) tip(lock);
    }
    ImGui::PopTextWrapPos();
    endTinted();
}

void App::drawMatesCard() {
    const ui::TopicRegion topic(guide::topics::Mates);
    ui::beginCard("##mates_card");
    ImGui::PushTextWrapPos();
    ui::inlineIcon(ui::Icon::Zap, ui::ACCENT);
    ImGui::SameLine();
    ImGui::PushFont(ui::titleFont(), ImGui::GetStyle().FontSizeBase);
    ImGui::TextUnformatted(TR("Mates"));
    ImGui::PopFont();
    wrapped(TR("Lichess's mate puzzles, in 1 to 5 moves, and by pattern: back rank, smothered, Anastasia's, Arabian, "
               "Boden's… And the elementary mates above, counted against the tables."),
            true);
    const auto rate = [&](std::string_view key) -> std::string {
        const auto it = puzzleRecord().themes.find(std::string(key));
        if (it == puzzleRecord().themes.end() || it->second.tried < 5) return {};
        return fmt::format(" {} %", 100 * it->second.won / it->second.tried);
    };
    const auto store = offline_puzzles_ ? offline_puzzles_->store() : nullptr;
    const auto lock  = puzzleLock();
    const auto play  = [&](std::string_view key) {
        puzzles_theme_ = std::string(key);
        if (store) {
            puzzles_online_ = false;
            startPuzzles();
        } else if (puzzleScopes()) {
            puzzles_online_ = true;
            startPuzzles();
        } else {
            // Neither the database nor the account: the Puzzles part says how to have them.
            puzzles_online_ = false;
            training_tab_   = 1;
        }
    };
    ImGui::BeginDisabled(!lock.empty());
    static constexpr std::string_view MATES[]    = {"mateIn1", "mateIn2", "mateIn3", "mateIn4", "mateIn5"};
    static constexpr std::string_view PATTERNS[] = {"backRankMate", "smotheredMate",    "anastasiaMate", "arabianMate",
                                                    "bodenMate",    "doubleBishopMate", "hookMate"};
    bool first                                   = true;
    for (const auto key : MATES) {
        const auto name = puzzleThemeName(key) + rate(key);
        if (!first) ui::sameLineOrWrap(name.c_str());
        first = false;
        if (chip(name.c_str(), false, std::string(key).c_str())) play(key);
    }
    first = true;
    for (const auto key : PATTERNS) {
        const auto name = puzzleThemeName(key) + rate(key);
        if (!first) ui::sameLineOrWrap(name.c_str());
        first = false;
        if (chip(name.c_str(), false, std::string(key).c_str())) play(key);
    }
    ImGui::EndDisabled();
    if (!lock.empty()) ImGui::TextDisabled("%s", lock.c_str());
    ImGui::TextDisabled(
        "%s", store            ? TR("A click starts 20 puzzles of the database on your disk; Ctrl+K “puzzles mate”.")
              : puzzleScopes() ? TR("A click starts a batch of your Lichess puzzles; the database offline is on the "
                                    "Puzzles part.")
                               : TR("Lichess's puzzle database first, on the Puzzles part (a click leads there), or "
                                    "your account online."));
    ImGui::PopTextWrapPos();
    ui::endCard();
}

// ---------------------------------------------------------------------------
// A session

bool App::startEndgame(const EndgameEntry& entry) {
    if (!endgameLock().empty()) return false;
    auto run    = std::make_unique<EndgameRun>();
    run->source = EndgameRun::Source::Catalogue;
    run->title  = TR(entry.name);
    run->tasks.push_back(taskOf(entry));
    startEndgameRun(std::move(run));
    return true;
}

bool App::startEndgameReview() {
    if (!endgameLock().empty()) return false;
    const auto day = trainingDay(epochNow());
    std::vector<const EndgameCard*> due;
    for (const auto& [key, c] : endgameRecord().cards)
        if (c.card.due <= day.end && endgameNeed(c.fen).empty()) due.push_back(&c);
    if (due.empty()) return false;
    std::sort(due.begin(), due.end(), [](const auto* a, const auto* b) { return a->card.due < b->card.due; });
    auto run    = std::make_unique<EndgameRun>();
    run->source = EndgameRun::Source::Review;
    run->title  = TR("Endgames to review");
    for (const auto* c : due) {
        EndgameTask t;
        t.fen    = c->fen;
        t.entry  = c->entry;
        t.title  = c->title;
        t.goal   = c->goal;
        t.mate   = c->mate;
        t.review = true;
        if (const auto* e = endgameEntry(c->entry)) {
            t.idea  = e->idea;
            t.title = e->name;
        }
        run->tasks.push_back(std::move(t));
    }
    startEndgameRun(std::move(run));
    return true;
}

bool App::startGameEndgames() {
    if (!endgameLock().empty()) return false;
    auto tasks = gameEndgames();
    std::erase_if(tasks, [&](const auto& t) { return !endgameNeed(t.fen).empty(); });
    if (tasks.empty()) {
        endgame_error_ = TR("No tables answer these positions now: see what they need above.");
        return false;
    }
    auto run    = std::make_unique<EndgameRun>();
    run->source = EndgameRun::Source::Games;
    run->title  = TR("Endgames of your games");
    run->tasks  = std::move(tasks);
    startEndgameRun(std::move(run));
    return true;
}

void App::startEndgameRun(std::unique_ptr<EndgameRun> run) {
    if (training_) closeTraining(false);
    const bool chained = review_running_;
    if (puzzle_) closePuzzles(false);
    if (mistake_) closeMistakes(false);
    if (guess_) closeGuess(false);
    if (endgame_) closeEndgame(false);
    if (vision_) closeVision(false);
    review_running_ = chained;
    endgame_error_.clear();
    run->flip     = board_.flip;
    run->tab      = board_side_tab_;
    const auto& b = board_;
    run->aside    = !b.game.moves.empty() || !b.tree.variations.empty() || !b.title.empty();
    toStudyBoard();
    openOnBoard(ReplayGame{}, 0, false, trf("Endgames · {}", run->title), false);
    board_back_.reset();
    run->started = Clock::now();
    endgame_     = std::move(run);
    beginEndgameTask();
}

void App::beginEndgameTask() {
    auto& r = *endgame_;
    if (r.current >= r.tasks.size()) return finishEndgames(false);
    const auto& t = r.tasks[r.current];
    r.start_fen   = t.fen;
    r.user        = chess::Board(t.fen).sideToMove();
    r.moves.clear();
    r.answer.reset();
    r.answer_fen.clear();
    r.kept.reset();
    r.judged.reset();
    r.changes.clear();
    r.fault_pending = r.fault = r.helped = r.hint = false;
    r.held                                        = 0;
    r.tables_mate.reset();
    r.win_line.clear();
    r.outcome = EndgameOutcome::Going;
    r.missing.clear();
    r.waiting.clear();
    r.phase        = EgPhase::Asking;
    r.task_started = Clock::now();
    board_.flip    = r.user == chess::Color::BLACK;
    showEndgamePosition();
    askEndgameTables();
}

void App::showEndgamePosition() {
    auto& r = *endgame_;
    auto& b = board_;
    ReplayGame game;
    game.start_fen = r.start_fen;
    chess::Board board(r.start_fen);
    for (const auto m : r.moves) {
        ReplayMove rm;
        rm.san   = chess::uci::moveToSan(board, m);
        rm.move  = m;
        rm.mover = board.sideToMove();
        game.moves.push_back(std::move(rm));
        board.makeMove(m);
    }
    b.game     = std::move(game);
    b.tree     = GameTree{b.game, {}};
    b.line     = -1;
    b.ply      = static_cast<int>(b.game.moves.size());
    b.chess960 = false;
    b.selected.reset();
    b.promotion.reset();
    b.preview.reset();
    b.drag.reset();
    b.drag_over.reset();
    b.move_typed.reset();
    b.move_error.clear();
    b.comment_edit.reset();
    b.undo.clear();
    b.redo.clear();
    b.changes     = {};
    b.start_notes = {};
    b.new_game    = true;
}

void App::askEndgameTables() {
    auto& r          = *endgame_;
    const auto board = askedBoard(r);
    const auto fen   = board.getFen();
    r.missing.clear();
    if (r.answer && r.answer_fen == fen) return endgameAnswered();
    if (const auto it = r.answers.find(fen); it != r.answers.end()) {
        r.answer     = it->second;
        r.answer_fen = fen;
        return endgameAnswered();
    }
    r.answer.reset();
    r.answer_fen.clear();
    const int pieces = piecesOf(board);
    // The disk's tables, at once.
    if (!diskTablesLocked() && diskTablesPieces() >= pieces) {
        const auto paths = endgameTablesPaths();
        std::optional<lichess::Tablebase> answer;
        {
            const auto lock = lockSyzygy();
            answer          = probeDiskTables(board, paths);
        }
        if (answer) {
            r.answers[fen] = *answer;
            r.answer       = std::move(*answer);
            r.answer_fen   = fen;
            r.online       = false;
            r.waiting.clear();
            return endgameAnswered();
        }
    }
    // Lichess's, with the user's consent: kept in the profile, else asked in the background.
    if (pieces <= 7 && lichess_.tablebase == 1) {
        if (auto kept = cachedTablebase(storage_.root(), fen); kept && kept->outcome != lichess::Outcome::Unknown) {
            r.answers[fen] = *kept;
            r.answer       = std::move(*kept);
            r.answer_fen   = fen;
            r.online       = true;
            r.waiting.clear();
            return endgameAnswered();
        }
        if (!r.asking.valid()) {
            r.asking_fen = fen;
            r.online     = true;
            r.asking     = std::async(std::launch::async,
                                      [fen, cancel = r.cancel] { return lichess::tablebase(fen, cancel.get()); });
        }
        return;
    }
    r.missing = endgameNeed(fen);
    if (r.missing.empty()) r.missing = trf("the tables of {} pieces", pieces);
}

void App::endgameAnswered() {
    auto& r = *endgame_;
    if (!r.answer) return;
    // The start's verdict: the goal it gives, and the mate the tables give (a move kept, played before it came, aside).
    if (r.moves.size() == (r.kept ? 1u : 0u) && r.changes.empty()) {
        const int result = outcomeResult(r.answer->outcome).value_or(0);
        r.verdict = r.start_verdict = result;
        r.changes.push_back({std::string(), result});
        auto& t = r.tasks[r.current];
        if (result < 0) {
            // Lost anyway (a position of a game): nothing to train; the next one.
            r.kept.reset();
            r.moves.clear();
            endgameEnded(EndgameOutcome::Lost);
            return;
        }
        if (t.entry.empty() && !t.review) t.goal = result > 0 ? EndgameGoal::Win : EndgameGoal::Draw;
        if (t.mate) r.tables_mate = tablesMateMoves(*r.answer);
    }
    if (r.phase == EgPhase::Asking && r.kept) {
        const auto move = *r.kept;
        r.kept.reset();
        // The board shows the move already: judged on the position before it.
        r.moves.pop_back();
        endgameJudge(move);
    }
}

void App::endgameAnswer(chess::Move move) {
    if (!endgame_ || liveGameOn()) return;
    auto& r = *endgame_;
    if (r.phase != EgPhase::Asking || r.kept) return;
    const auto board = playedBoard(r.start_fen, r.moves);
    if (board.sideToMove() != r.user) return;
    if (board_.ply != static_cast<int>(r.moves.size())) return;  // the position asked only
    r.hint = false;
    r.win_line.clear();
    if (!r.answer || r.answer_fen != board.getFen()) {
        // The tables' answer is not there yet (the network): the move is kept, judged when it comes.
        r.kept = move;
        r.moves.push_back(move);
        showEndgamePosition();
        return;
    }
    endgameJudge(move);
}

void App::endgameJudge(chess::Move move) {
    auto& r          = *endgame_;
    const auto board = playedBoard(r.start_fen, r.moves);
    const auto& t    = r.tasks[r.current];
    r.judged         = judgeEndgameMove(*r.answer, chess::uci::moveToUci(move));
    r.judged_move    = move;
    const auto san   = numbered(board, move);
    r.moves.push_back(move);
    showEndgamePosition();
    const auto after = playedBoard(r.start_fen, r.moves);
    std::optional<int> result;
    if (r.judged->known) {
        result = r.judged->after;
        if (r.judged->fault()) {
            r.fault         = true;
            r.fault_pending = true;
            r.changes.push_back({san + "?", r.judged->after});
            // The position before the fault: a card of the day's review, back tomorrow (unless it is the card played).
            const auto now = epochNow();
            const auto key = positionKey(board.getFen());
            auto& cards    = endgameRecord().cards;
            if (!(t.review && key == positionKey(t.fen))) {
                auto card = missedEndgameCard(t, board.getFen(), now, trainingDayEnd(now), trainingScheduler());
                if (const auto it = cards.find(key); it != cards.end()) card.since = it->second.since;
                cards[key] = std::move(card);
                saveEndgameRecord();
            }
        }
        r.verdict = r.judged->after;
    }
    if (t.goal == EndgameGoal::Draw && result.value_or(0) >= 0) ++r.held;
    const auto outcome = endgameOutcome(after, move, board.isCapture(move), r.user, t, result, r.held);
    if (outcome != EndgameOutcome::Going) return endgameEnded(outcome);
    // The tables' move: their answer for the new position first.
    r.phase    = EgPhase::Reply;
    r.reply_at = Clock::now();
    askEndgameTables();
}

void App::endgameTablesMove() {
    auto& r          = *endgame_;
    const auto board = playedBoard(r.start_fen, r.moves);
    const auto move  = tablesMove(*r.answer);
    if (!move) return;  // the game is over there: said by endgameOutcome() after the user's move
    const auto m = chess::uci::uciToMove(board, *move);
    if (m == chess::Move::NO_MOVE) return;
    // What the tables say for the user after their move: the opposite of their outcome now.
    const auto theirs = outcomeResult(r.answer->outcome);
    r.moves.push_back(m);
    showEndgamePosition();
    const auto after   = playedBoard(r.start_fen, r.moves);
    const auto& t      = r.tasks[r.current];
    const auto outcome = endgameOutcome(after, m, board.isCapture(m), r.user, t,
                                        theirs ? std::optional<int>(-*theirs) : std::nullopt, r.held);
    if (outcome != EndgameOutcome::Going) return endgameEnded(outcome);
    if (r.fault_pending) {
        r.fault_pending = false;
        // The fault said with the tables' reply: "1.Kd5? Ke7".
        if (!r.changes.empty()) r.changes.back().moves += " " + localSan(chess::uci::moveToSan(board, m));
        r.phase = EgPhase::Fault;
        return;
    }
    r.phase = EgPhase::Asking;
    askEndgameTables();
}

void App::endgameTakeBack() {
    if (!endgame_) return;
    auto& r = *endgame_;
    if (r.phase != EgPhase::Asking && r.phase != EgPhase::Fault) return;
    if (r.kept) {  // the move kept, not judged yet
        r.kept.reset();
        r.moves.pop_back();
        showEndgamePosition();
        return;
    }
    // Back to the user's last turn: the tables' reply and the user's move before it.
    const bool fault = r.phase == EgPhase::Fault;
    std::size_t back = 0;
    {
        auto board = chess::Board(r.start_fen);
        std::vector<chess::Color> movers;
        for (const auto m : r.moves) {
            movers.push_back(board.sideToMove());
            board.makeMove(m);
        }
        while (back < movers.size()) {
            ++back;
            if (movers[movers.size() - back] == r.user) break;
        }
        if (back == 0 || movers[movers.size() - back] != r.user) return;
    }
    r.moves.resize(r.moves.size() - back);
    if (fault && r.changes.size() > 1) r.changes.pop_back();
    r.verdict = r.changes.empty() ? r.start_verdict : r.changes.back().verdict;
    if (r.tasks[r.current].goal == EndgameGoal::Draw && r.held > 0) --r.held;
    r.helped = true;
    r.hint   = false;
    r.win_line.clear();
    r.phase = EgPhase::Asking;
    showEndgamePosition();
    askEndgameTables();
}

void App::endgameRestart() {
    if (!endgame_ || endgame_->phase == EgPhase::Done || endgame_->phase == EgPhase::End) return;
    auto& r           = *endgame_;
    const bool review = r.tasks[r.current].review;
    const bool fault  = r.fault;
    beginEndgameTask();
    // A card played again keeps what happened in it: its grade comes from the first try.
    if (review) {
        r.fault  = fault;
        r.helped = true;
    }
}

void App::endgameContinue() {
    if (!endgame_ || endgame_->phase != EgPhase::Fault) return;
    endgame_->phase = EgPhase::Asking;
    askEndgameTables();
}

void App::endgameSeeWin() {
    if (!endgame_ || endgame_->phase != EgPhase::Fault) return;
    endgameTakeBack();
    auto& r          = *endgame_;
    const auto board = playedBoard(r.start_fen, r.moves);
    r.helped         = true;
    // The tables' line from there: the disk's at once, else Lichess's in one request.
    if (!diskTablesLocked() && diskTablesPieces() >= piecesOf(board)) {
        const auto paths = endgameTablesPaths();
        const auto lock  = lockSyzygy();
        r.win_line       = tablesLine(board, [&](const chess::Board& b) { return probeDiskTables(b, paths); }, 16);
    } else if (lichess_.tablebase == 1 && !r.win_line_job.valid()) {
        r.win_line_job = std::async(std::launch::async, [fen = board.getFen(), cancel = r.cancel] {
            return lichess::tablebaseMainline(fen, cancel.get());
        });
    }
}

void App::endgameEnded(EndgameOutcome outcome) {
    auto& r        = *endgame_;
    auto& t        = r.tasks[r.current];
    r.outcome      = outcome;
    r.phase        = EgPhase::Done;
    const bool ok  = outcome == EndgameOutcome::Won || outcome == EndgameOutcome::Held;
    int user_moves = 0;
    {
        chess::Board board(r.start_fen);
        for (const auto m : r.moves) {
            if (board.sideToMove() == r.user) ++user_moves;
            board.makeMove(m);
        }
    }
    const auto now         = epochNow();
    auto& record           = endgameRecord();
    const bool lost_anyway = r.moves.empty() && outcome == EndgameOutcome::Lost;
    // Stopped: a try only when it had a fault; a card played again is not graded, still due.
    const bool stopped = outcome == EndgameOutcome::Going;
    if (!lost_anyway && !(stopped && !r.fault)) {
        if (!t.entry.empty() && !t.review) {
            auto& e = record.results[t.entry];
            ++e.tries;
            e.last        = now;
            e.missed_last = r.fault || !ok;
            if (ok) {
                ++e.done;
                e.clean = e.clean || (!r.fault && !r.helped);
                if (t.mate && r.tables_mate) {
                    e.tables = *r.tables_mate;
                    if (!r.helped && !r.fault && (e.best == 0 || user_moves < e.best)) e.best = user_moves;
                }
            }
        }
        if (t.review && !stopped) {
            const auto key = positionKey(t.fen);
            if (auto it = record.cards.find(key); it != record.cards.end()) {
                it->second.card =
                    trainingScheduler().review(it->second.card, endgameRating(ok, r.fault, r.helped), now);
                r.next_review = it->second.card.due;
            }
        }
        const auto ms    = static_cast<int64_t>(std::min(since(r.task_started), 1800.0) * 1000);
        record.answer_ms = record.answer_ms > 0 ? (record.answer_ms * 3 + ms) / 4 : ms;
        saveEndgameRecord();
    }
    r.results.push_back({r.current, lost_anyway ? EndgameOutcome::Going : outcome, r.fault, r.helped, user_moves});
    board_.selected.reset();
    board_.move_typed.reset();
    if (lost_anyway) endgameNext();  // a game's position lost anyway: nothing to play
}

void App::endgameNext() {
    auto& r = *endgame_;
    ++r.current;
    if (r.current >= r.tasks.size()) return finishEndgames(false);
    beginEndgameTask();
}

void App::finishEndgames(bool stopped) {
    auto& r   = *endgame_;
    r.phase   = EgPhase::End;
    r.stopped = stopped;
    r.took    = since(r.started);
    board_.selected.reset();
    board_.move_typed.reset();
}

void App::closeEndgame(bool to_training) {
    if (!endgame_) return;
    if (to_training) {
        // Back where it was started: the Endgames part, or Today for the day's review.
        if (!review_running_ && endgame_->source != EndgameRun::Source::Review) training_tab_ = 4;
        review_running_ = false;
        review_chain_.clear();
        training_enter_puzzles_ = false;
    }
    const bool aside = endgame_->aside, flip = endgame_->flip;
    board_side_tab_ = endgame_->tab;
    leaveEndgameQuestions();
    endgame_.reset();
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

void App::leaveEndgameQuestions() {
    if (!endgame_) return;
    *endgame_->cancel = true;
    // Ending out of sight (a future of std::async waits for its task when it goes).
    jobs_.park(std::move(endgame_->asking));
    jobs_.park(std::move(endgame_->win_line_job));
}

void App::dropEndgame() {
    board_.flip     = endgame_->flip;
    board_side_tab_ = endgame_->tab;
    leaveEndgameQuestions();
    endgame_.reset();
    review_running_ = false;
    review_chain_.clear();
    forget_board_game_ = true;
}

// ---------------------------------------------------------------------------
// The Board page during an endgame

void App::endgameKeys() {
    auto& r        = *endgame_;
    const auto& io = ImGui::GetIO();
    if (io.WantTextInput || ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) return;
    const auto owner = ImGui::GetID("##endgame_keys");
    for (const auto key : {ImGuiKey_Enter, ImGuiKey_KeypadEnter, ImGuiKey_Escape, ImGuiKey_LeftArrow,
                           ImGuiKey_RightArrow, ImGuiKey_UpArrow, ImGuiKey_DownArrow, ImGuiKey_Home, ImGuiKey_End})
        ImGui::SetKeyOwner(key, owner);
    const bool plain = !io.KeyCtrl && !io.KeyAlt && !io.KeySuper;
    const bool enter =
        plain && (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false));
    const bool escape = ImGui::IsKeyPressed(ImGuiKey_Escape, false);
    const bool undo   = ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Z);
    switch (r.phase) {
        case EgPhase::Asking:
            if (escape) {
                endgameEnded(EndgameOutcome::Going);
                r.stopped = true;
            } else if (!liveGameOn()) {
                if (undo)
                    endgameTakeBack();
                else if (ImGui::IsKeyChordPressed(ImGuiMod_Alt | ImGuiKey_H) && r.answer && !r.answer->moves.empty())
                    r.hint = r.helped = true;
                else
                    boardMoveKeys();
            }
            break;
        case EgPhase::Reply:
            if (escape) {
                endgameEnded(EndgameOutcome::Going);
                r.stopped = true;
            }
            break;
        case EgPhase::Fault:
            if (escape) {
                endgameEnded(EndgameOutcome::Going);
                r.stopped = true;
            } else if (enter || undo) {
                endgameTakeBack();
            }
            break;
        case EgPhase::Done: {
            const bool last = r.current + 1 >= r.tasks.size();
            if (escape) {
                if (r.source == EndgameRun::Source::Catalogue && !review_running_)
                    closeEndgame(true);
                else
                    finishEndgames(true);
            } else if (enter) {
                if (r.source == EndgameRun::Source::Catalogue) {
                    if (review_running_) {
                        if (!nextReview()) closeEndgame(true);
                    } else {
                        r.results.clear();
                        r.current = 0;
                        beginEndgameTask();  // again
                    }
                } else if (last) {
                    finishEndgames(false);
                } else {
                    endgameNext();
                }
            }
            break;
        }
        case EgPhase::End:
            if (review_running_) {
                if (escape)
                    closeEndgame(true);
                else if (enter && !nextReview())
                    closeEndgame(true);
            } else if (escape || enter) {
                closeEndgame(true);
            }
            break;
    }
}

void App::drawEndgamePage() {
    ui::pageTitle(TR("Board"), guideSection());
    if (!ImGui::GetIO().WantTextInput && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId) &&
        !SDL_TextInputActive(window_))
        SDL_StartTextInput(window_);
    endgameKeys();
    if (!endgame_) return;  // left by a key
    auto& r            = *endgame_;
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float side   = std::clamp(avail.x * 0.42f, px(340), px(620));
    const float under  = ImGui::GetTextLineHeightWithSpacing() * 4;
    const float size   = std::floor(
        std::max(px(200), std::min(avail.x - side - ImGui::GetStyle().ItemSpacing.x - px(20), avail.y - under)));
    ImGui::BeginGroup();
    if (r.phase != EgPhase::End && r.current < r.tasks.size()) {
        const auto& t    = r.tasks[r.current];
        const auto start = ImGui::GetCursorPosX();
        std::string what = t.entry.empty() ? t.title : std::string(TR(t.title.c_str()));
        if (t.review) what += std::string(" · ") + TR("to review");
        ImGui::TextDisabled("%s", what.c_str());
        const auto board  = playedBoard(r.start_fen, r.moves);
        std::string where = board.sideToMove() == chess::Color::WHITE ? TR("White to play") : TR("Black to play");
        if (r.tasks.size() > 1) where = trf("{} of {}", r.current + 1, r.tasks.size()) + " · " + where;
        ImGui::SameLine(std::max(ImGui::GetCursorPosX() + px(10), start + size - ImGui::CalcTextSize(where.c_str()).x));
        ImGui::TextDisabled("%s", where.c_str());
    }
    drawEndgameBoard(size);
    if (r.phase != EgPhase::End && r.current < r.tasks.size()) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + size);
        if (r.moves.empty())
            ImGui::TextDisabled("%s", TR("The start: you play, the tables answer."));
        else {
            chess::Board before(r.start_fen);
            for (std::size_t i = 0; i + 1 < r.moves.size(); ++i) before.makeMove(r.moves[i]);
            ImGui::TextDisabled("%s", trf("Last move: {} (in pale green).", numbered(before, r.moves.back())).c_str());
        }
        ImGui::PopTextWrapPos();
    }
    ImGui::EndGroup();
    ImGui::SameLine(0, px(18));
    ImGui::BeginChild("##endgame_side", ImVec2(0, 0));
    drawEndgameColumn();
    ImGui::EndChild();
}

void App::drawEndgameBoard(float size) {
    auto& b          = board_;
    auto& r          = *endgame_;
    const auto board = boardAt(b.game, b.chess960, b.ply);
    BoardDecor decor;
    decor.flipped    = b.flip;
    decor.eval_white = -1.0f;
    if (b.ply > 0) decor.last_move = b.game.moves[static_cast<std::size_t>(b.ply - 1)].move;
    const auto green  = markColour('G', true);
    const auto orange = IM_COL32(236, 140, 52, 150);
    if (r.phase == EgPhase::Fault && r.judged && r.moves.size() >= 2) {
        // The fault in orange (the move before the tables' reply), the moves that kept the result in green.
        const auto fault = r.moves[r.moves.size() - 2];
        decor.tints.push_back({fault.from(), orange});
        decor.tints.push_back({shownTarget(fault), orange});
        chess::Board before(r.start_fen);
        for (std::size_t i = 0; i + 2 < r.moves.size(); ++i) before.makeMove(r.moves[i]);
        for (const auto& u : r.judged->kept)
            if (const auto m = chess::uci::uciToMove(before, u); m != chess::Move::NO_MOVE)
                decor.tints.push_back({shownTarget(m), green});
    }
    if (r.phase == EgPhase::Asking && r.answer && r.answer_fen == board.getFen()) {
        if (r.hint && !r.answer->moves.empty())
            if (const auto m = chess::uci::uciToMove(board, r.answer->moves.front().uci); m != chess::Move::NO_MOVE)
                decor.arrows.push_back({m, markColour('G', false)});
        if (!r.win_line.empty())
            if (const auto m = chess::uci::uciToMove(board, r.win_line.front()); m != chess::Move::NO_MOVE)
                decor.arrows.push_back({m, markColour('G', false)});
    }
    const bool moves = r.phase == EgPhase::Asking && !r.kept && board.sideToMove() == r.user &&
                       b.ply == static_cast<int>(r.moves.size()) && !liveGameOn();
    decor.selected   = moves ? b.selected : std::nullopt;
    if (decor.selected)
        for (const auto& m : movesFrom(board, *decor.selected)) decor.targets.push_back(shownTarget(m));
    const bool dragging = moves && b.drag && b.drag->moved && b.selected == b.drag->from;
    if (dragging) {
        decor.lifted = b.drag->from;
        if (b.drag_over && std::find(decor.targets.begin(), decor.targets.end(), *b.drag_over) != decor.targets.end())
            decor.drop = b.drag_over;
    }
    const ImVec2 top = ImGui::GetCursorScreenPos();
    if (appearance::current().board3d.on && board3d_.available()) {
        board3d_.setMarks(Board3d::Marks::Off);
        const auto input = board3d_.draw(board, size, decor);
        if (moves) boardPointer(board, input.clicked, input.hovered, input.right_click);
    } else {
        if (!appearance::current().board3d.on) board3d_.release();
        drawBoard(atlas_, board, size, decor);
        const auto hover = boardSquareAt(ImGui::GetMousePos(), top, size, decor);
        if (moves) {
            const bool left  = ImGui::IsItemClicked(ImGuiMouseButton_Left),
                       right = ImGui::IsItemClicked(ImGuiMouseButton_Right);
            boardPointer(board, left ? hover : std::nullopt, hover, right);
            if (dragging && b.drag) drawDraggedPiece(atlas_, board.at(b.drag->from), size);
        }
    }
    drawPromotionPopup(board);
}

void App::drawEndgameColumn() {
    auto& r = *endgame_;
    const ui::TopicRegion topic(guide::topics::Endgames);
    if (liveGameOn()) {  // fair play (#48): the tables are an aid; the endgame waits for the live game's end
        drawFairPlayBanner();
        if (ImGui::Button(TR("Stop · Esc"))) finishEndgames(true);
        return;
    }
    if (r.phase == EgPhase::End) return drawEndgameEnd();
    if (r.current >= r.tasks.size()) return;
    const auto& t    = r.tasks[r.current];
    const auto board = playedBoard(r.start_fen, r.moves);
    // The tables' verdict now.
    if (!r.changes.empty()) {
        const ImU32 tone = verdictTone(r.verdict, t.goal);
        beginTinted("##endgame_verdict", tone);
        ui::inlineIcon(meetsGoal(r.verdict, t.goal) ? ui::Icon::Check : ui::Icon::Alert, tone);
        ImGui::SameLine();
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(tone), "%s", tablesVerdict(r.verdict, r.user));
        if (r.answer && r.answer_fen == board.getFen()) {
            ImGui::SameLine(0, px(12));
            // To mate when Lichess says it, or in the elementary mates (no capture nor pawn move before the mate);
            // else to the next capture or pawn move.
            const auto mate = r.answer->dtm ? r.answer->dtm : t.mate ? r.answer->dtz : std::nullopt;
            if (mate && *mate != 0) {
                const auto n = static_cast<uint64_t>((std::abs(*mate) + 1) / 2);
                ImGui::TextDisabled("%s", trn("mate in {} move at best", "mate in {} moves at best", n, n).c_str());
            } else if (r.answer->dtz && *r.answer->dtz != 0) {
                const auto n = static_cast<uint64_t>((std::abs(*r.answer->dtz) + 1) / 2);
                ImGui::TextDisabled("%s", trn("{} move at most before a capture or a pawn move",
                                              "{} moves at most before a capture or a pawn move", n, n)
                                              .c_str());
            }
        }
        endTinted();
        ImGui::Dummy(ImVec2(0, px(4)));
    }
    switch (r.phase) {
        case EgPhase::Asking:
        case EgPhase::Reply: {
            beginTinted("##endgame_question", ui::CARD_BD);
            ImGui::PushTextWrapPos();
            ImGui::PushFont(ui::titleFont(), ImGui::GetStyle().FontSizeBase * 1.2f);
            ImGui::TextUnformatted(questionText(r.user, t));
            ImGui::PopFont();
            wrapped(t.goal == EndgameGoal::Win
                        ? TR("The defender plays the move that resists the longest. Each move is judged at once: the "
                             "verdict stays, or it changes, and you know before going on.")
                        : TR("The tables attack with the move that leaves you the fewest ways to hold. Each move is "
                             "judged at once: the verdict stays, or it changes, and you know before going on."),
                    true);
            if (r.phase == EgPhase::Asking) drawMoveField();
            if (ImGui::Button(TR("Take back · Ctrl+Z"))) endgameTakeBack();
            ImGui::SameLine();
            ImGui::BeginDisabled(!r.answer || r.answer->moves.empty() || r.phase != EgPhase::Asking);
            if (ImGui::Button(TR("The right move · Alt+H"))) r.hint = r.helped = true;
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("%s", TR("H alone begins a move of the h-pawn: the right move is under Alt."));
            ImGui::SameLine();
            if (ImGui::Button(TR("Start again"))) endgameRestart();
            ImGui::SameLine();
            if (ImGui::Button(TR("Stop · Esc"))) {
                endgameEnded(EndgameOutcome::Going);
                r.stopped = true;
            }
            if (r.hint && r.answer && !r.answer->moves.empty())
                wrapped(trf("The tables play {} (the green arrow); this endgame no longer counts as done without help.",
                            localSan(r.answer->moves.front().san)),
                        true);
            if (!r.win_line.empty()) {
                std::string line;
                auto b = board;
                for (const auto& u : r.win_line) {
                    const auto m = chess::uci::uciToMove(b, u);
                    if (m == chess::Move::NO_MOVE) break;
                    line += (line.empty() ? "" : " ") + (line.empty() || b.sideToMove() == chess::Color::WHITE
                                                             ? numbered(b, m)
                                                             : localSan(chess::uci::moveToSan(b, m)));
                    b.makeMove(m);
                }
                wrapped(trf("The tables' line: {}.", line));
            }
            if (r.phase == EgPhase::Reply) ImGui::TextDisabled("%s", TR("The tables answer…"));
            ImGui::PopTextWrapPos();
            endTinted();
            break;
        }
        case EgPhase::Fault: {
            beginTinted("##endgame_fault", ui::WARN);
            ImGui::PushTextWrapPos();
            const auto& last = r.changes.back();
            ImGui::PushFont(ui::titleFont(), ImGui::GetStyle().FontSizeBase * 1.2f);
            ImGui::TextUnformatted(
                (last.verdict == 0 ? trf("{}: a draw, now", last.moves) : trf("{}: a loss, now", last.moves)).c_str());
            ImGui::PopFont();
            if (r.judged && !r.judged->kept.empty()) {
                chess::Board before(r.start_fen);
                for (std::size_t i = 0; i + 2 < r.moves.size(); ++i) before.makeMove(r.moves[i]);
                std::string kept;
                for (std::size_t i = 0; i < r.judged->kept.size() && i < 4; ++i)
                    if (const auto m = chess::uci::uciToMove(before, r.judged->kept[i]); m != chess::Move::NO_MOVE)
                        kept += (kept.empty() ? "" : ", ") + numbered(before, m) + "!";
                if (r.judged->kept.size() > 4) kept += ", …";
                wrapped(r.judged->before > 0 ? trf("Kept the win: {} (the green squares).", kept)
                                             : trf("Kept the draw: {} (the green squares).", kept));
            }
            if (!t.idea.empty()) wrapped(TR(t.idea.c_str()));
            if (primaryButton(TR("Take back · Ctrl+Z"))) endgameTakeBack();
            ImGui::SameLine();
            if (ImGui::Button(r.judged && r.judged->before > 0 ? TR("See the win") : TR("See the draw")))
                endgameSeeWin();
            ImGui::SameLine();
            if (ImGui::Button(TR("Continue anyway"))) endgameContinue();
            ImGui::PopTextWrapPos();
            endTinted();
            ImGui::Dummy(ImVec2(0, px(4)));
            {
                const std::vector<chess::Move> before(r.moves.begin(),
                                                      r.moves.end() - std::min<std::ptrdiff_t>(2, std::ssize(r.moves)));
                if (!(t.review && positionKey(t.fen) == positionKey(playedBoard(r.start_fen, before).getFen())))
                    wrapped(TR("The position before the fault comes back in the day's review (FSRS): tomorrow."), true);
            }
            break;
        }
        case EgPhase::Done: {
            const bool ok = r.outcome == EndgameOutcome::Won || r.outcome == EndgameOutcome::Held;
            beginTinted("##endgame_done", ok ? ui::GOOD : r.stopped ? ui::CARD_BD : ui::WARN);
            ImGui::PushTextWrapPos();
            if (ok) {
                ui::inlineIcon(ui::Icon::Check, ui::GOOD);
                ImGui::SameLine();
            }
            const auto& res = r.results.back();
            std::string title;
            const auto last = r.moves.empty() ? chess::Move::NO_MOVE : r.moves.back();
            const auto end  = playedBoard(r.start_fen, r.moves);
            switch (r.outcome) {
                case EndgameOutcome::Won:
                    if (end.isGameOver().first == chess::GameResultReason::CHECKMATE)
                        title = t.mate && r.tables_mate
                                    ? trn("Mate in {} move (the tables: {})", "Mate in {} moves (the tables: {})",
                                          res.moves, res.moves, *r.tables_mate)
                                    : std::string(TR("Won: mate"));
                    else if (last != chess::Move::NO_MOVE && last.typeOf() == chess::Move::PROMOTION)
                        title = TR("Won: the pawn became a queen");
                    else
                        title = TR("Won: the other king stands alone");
                    break;
                case EndgameOutcome::Held:
                    title = r.held >= HOLD_MOVES
                                ? trn("Held: {} move, the draw kept", "Held: {} moves, the draw kept", r.held, r.held)
                                : std::string(TR("Held: a draw by the rules"));
                    break;
                case EndgameOutcome::Drawn:
                    title = TR("A draw: the win slipped away");
                    break;
                case EndgameOutcome::Lost:
                    title = TR("Lost");
                    break;
                case EndgameOutcome::Going:
                    title = TR("Stopped");
                    break;
            }
            ImGui::PushFont(ui::titleFont(), ImGui::GetStyle().FontSizeBase * 1.2f);
            ImGui::TextUnformatted(title.c_str());
            ImGui::PopFont();
            if (ok && (res.fault || res.helped))
                wrapped(res.fault ? TR("With a fault taken back: it comes back in the day's review.")
                                  : TR("With help: the right move shown, or a move taken back."),
                        true);
            if (t.review && r.next_review > 0) {
                const auto days =
                    std::max<int64_t>(1, (r.next_review - epochNow() + study::fsrs::DAY / 2) / study::fsrs::DAY);
                wrapped(days == 1 ? std::string(TR("It comes back tomorrow."))
                                  : trn("It comes back in {} day.", "It comes back in {} days.",
                                        static_cast<uint64_t>(days), days),
                        true);
            }
            const bool last_task = r.current + 1 >= r.tasks.size();
            if (r.source == EndgameRun::Source::Catalogue) {
                if (review_running_) {
                    ImGui::PopTextWrapPos();
                    endTinted();
                    drawReviewNext(false);
                    break;
                }
                if (primaryButton((std::string(TR("Again")) + " · " + TR("Enter")).c_str())) {
                    r.results.clear();
                    r.current = 0;
                    beginEndgameTask();
                }
                ImGui::SameLine();
                // The next endgame of the catalogue that is not done yet.
                const auto& all          = endgameCatalogue();
                const EndgameEntry* next = nullptr;
                const auto here = std::find_if(all.begin(), all.end(), [&](const auto& e) { return t.entry == e.id; });
                for (auto it = here == all.end() ? all.begin() : here + 1; it != all.end(); ++it) {
                    const auto res_it = endgameRecord().results.find(it->id);
                    if ((res_it == endgameRecord().results.end() || res_it->second.done == 0) &&
                        endgameNeed(it->fen).empty()) {
                        next = &*it;
                        break;
                    }
                }
                if (next && ImGui::Button(trf("Next: {}", TR(next->name)).c_str())) {
                    const auto* e = next;
                    closeEndgame(false);
                    (void)startEndgame(*e);
                    ImGui::PopTextWrapPos();
                    endTinted();
                    return;
                }
                if (next) ImGui::SameLine();
                if (ImGui::Button(TR("Back to the Training page"))) {
                    ImGui::PopTextWrapPos();
                    endTinted();
                    closeEndgame(true);
                    return;
                }
            } else {
                if (primaryButton(
                        (std::string(last_task ? TR("The end") : TR("Next")) + " · " + TR("Enter")).c_str())) {
                    if (last_task)
                        finishEndgames(false);
                    else
                        endgameNext();
                }
            }
            ImGui::PopTextWrapPos();
            endTinted();
            break;
        }
        case EgPhase::End:
            break;
    }
    if (!endgame_) return;
    // The verdict at the start and each move that changed it.
    if (r.changes.size() > 1 || r.phase == EgPhase::Done) {
        ImGui::Dummy(ImVec2(0, px(4)));
        ui::beginCard("##endgame_changes");
        if (ImGui::BeginTable("##changes", 2, ImGuiTableFlags_SizingStretchProp)) {
            for (const auto& c : r.changes) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(c.moves.empty() ? TR("At the start") : c.moves.c_str());
                ImGui::TableNextColumn();
                ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(verdictTone(c.verdict, t.goal)), "%s",
                                   verdictWord(c.verdict));
            }
            ImGui::EndTable();
        }
        ui::endCard();
    }
    ImGui::Dummy(ImVec2(0, px(4)));
    ImGui::PushTextWrapPos();
    // What answers, what the endgame counts.
    if (!r.missing.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(ui::WARN));
        ImGui::TextWrapped("%s", trf("No tables answer this position: it needs {}.", r.missing).c_str());
        ImGui::PopStyleColor();
        if (ImGui::Button(TR("Back to the Training page##endgame_missing"))) {
            ImGui::PopTextWrapPos();
            closeEndgame(true);
            return;
        }
    } else if (!r.waiting.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(ui::WARN));
        ImGui::TextWrapped("%s",
                           (r.waiting_status == 429
                                ? trf("Lichess asks to wait a minute ({}): your moves are kept, the judging goes on "
                                      "by itself.",
                                      r.waiting)
                                : trf("Lichess's tables do not answer ({}): your moves are kept, the judging goes "
                                      "on when the network comes back.",
                                      r.waiting))
                               .c_str());
        ImGui::PopStyleColor();
        if (ImGui::SmallButton(TR("Try again now##endgame_retry"))) {
            r.retry_at = Clock::now();
            r.waiting.clear();
            askEndgameTables();
        }
    } else if (r.asking.valid()) {
        ImGui::TextDisabled("%s", TR("Asking Lichess's tables…"));
    }
    if (r.kept) {
        chess::Board before(r.start_fen);
        for (std::size_t i = 0; i + 1 < r.moves.size(); ++i) before.makeMove(r.moves[i]);
        ImGui::TextDisabled("%s", trf("{}: kept, judged when the tables answer.", numbered(before, *r.kept)).c_str());
    }
    wrapped(r.online
                ? std::string(TR("The tables: Lichess's online; each position played goes to tablebase.lichess.ovh "
                                 "as its FEN."))
                : trf("The tables: those of the disk (Syzygy, up to {} pieces); nothing leaves the machine.",
                      diskTablesPieces()),
            true);
    wrapped(t.mate ? TR("The mate counts, set against the tables' own from the start. Syzygy gives the distance to the "
                        "next capture or pawn move (DTZ), Lichess the distance to mate up to 6 pieces.")
            : t.goal == EndgameGoal::Win
                ? TR("The win counts when a pawn becomes a queen, the other king stands alone, or at the mate. Syzygy "
                     "gives the distance to the next capture or pawn move (DTZ), Lichess the distance to mate up to 6 "
                     "pieces.")
                : trf("The draw counts after {} moves held, or when a rule draws the game (stalemate, too little "
                      "material, the repetition, the fifty moves).",
                      HOLD_MOVES),
            true);
    ImGui::PopTextWrapPos();
}

void App::drawEndgameEnd() {
    auto& r        = *endgame_;
    std::size_t ok = 0, played = 0;
    for (const auto& res : r.results) {
        if (res.outcome == EndgameOutcome::Going && !r.stopped) continue;
        ++played;
        if ((res.outcome == EndgameOutcome::Won || res.outcome == EndgameOutcome::Held) && !res.fault) ++ok;
    }
    ImGui::PushFont(ui::titleFont(), ImGui::GetStyle().FontSizeBase * 1.3f);
    ImGui::TextUnformatted(
        trn("{} endgame in {}", "{} endgames in {}", played, played, ui::formatDuration(r.took)).c_str());
    ImGui::PopFont();
    ImGui::TextUnformatted(trf("{} won or held without a fault · {} to review", ok, played - ok).c_str());
    ImGui::Dummy(ImVec2(0, px(4)));
    for (const auto& res : r.results) {
        if (res.task >= r.tasks.size()) continue;
        const auto& t   = r.tasks[res.task];
        const char* how = res.outcome == EndgameOutcome::Won     ? TR("won")
                          : res.outcome == EndgameOutcome::Held  ? TR("held")
                          : res.outcome == EndgameOutcome::Drawn ? TR("drawn")
                          : res.outcome == EndgameOutcome::Lost  ? TR("lost")
                                                                 : TR("stopped");
        ImGui::TextUnformatted(fmt::format("{} · {}{}", t.entry.empty() ? t.title : std::string(TR(t.title.c_str())),
                                           how, res.fault ? std::string(" · ") + TR("a fault") : std::string())
                                   .c_str());
    }
    ImGui::Dummy(ImVec2(0, px(4)));
    if (review_running_) {
        drawReviewNext(false);
        return;
    }
    if (primaryButton((std::string(TR("Back to the Training page")) + " · " + TR("Enter")).c_str())) closeEndgame(true);
}

}  // namespace fcg
