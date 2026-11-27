// Lichess's evaluations kept offline (#58, mockup M11, board 6): their card on the Lichess page, among the services
// (what is kept, the disk's free space, Download and prepare, A file already downloaded…; the preparation, its
// progress and Cancel; once ready, Update and Remove them), and where they answer: the Analysis tab's line above the
// engine, the review's known positions, the Tree tab's Eval. column. Nothing leaves the machine but the request for
// the file, its size said before.

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <future>

#include <fmt/include/fmt/format.h>
#include <imgui.h>

#include "app.hpp"
#include "board_text.hpp"
#include "console.hpp"
#include "guide.hpp"
#include "i18n.hpp"
#include "keyboard.hpp"
#include "lichess_months.hpp"
#include "machines.hpp"
#include "ui.hpp"
#include "widgets.hpp"

namespace fcg {

namespace fs = std::filesystem;

namespace {

float px(float v) { return v * ImGui::GetFontSize() / 16.0f; }

// A tooltip for the item just drawn, also when it is disabled, its lines wrapped.
void tip(const char* text) {
    if (!ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled)) return;
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

// White's score as the interface writes it: "+0.31", "#5" (White mates), "#-3" (Black mates).
std::string whiteScore(const study::OfflineEval& e) {
    return e.mate ? fmt::format("#{}", e.mate) : fmt::format("{:+.2f}", static_cast<double>(e.cp) / 100.0);
}

// The best move with its number: "6…e6", "7.Nf3"; empty when there is none, or it cannot be played.
std::string numberedMove(const chess::Board& board, const std::string& uci) {
    if (uci.empty()) return {};
    try {
        const auto move = chess::uci::uciToMove(board, uci);
        if (move == chess::Move::NO_MOVE) return {};
        const bool white = board.sideToMove() == chess::Color::WHITE;
        return fmt::format("{}{}{}", board.fullMoveNumber(), white ? "." : "…",
                           localSan(chess::uci::moveToSan(board, move)));
    } catch (const std::exception&) {
        return {};
    }
}

// The space the profile's disk has left (the folder is made at the first preparation); 0 when it cannot be said.
uint64_t freeSpace(const fs::path& folder) {
    std::error_code ec;
    const auto info = fs::space(fs::exists(folder, ec) ? folder : folder.parent_path(), ec);
    return ec ? 0 : info.available;
}

}  // namespace

std::optional<study::OfflineEval> App::offlineEval(const chess::Board& board) {
    if (!offline_evals_ || board.chess960() || aidsLocked() || liveGameOn() || engineGameHidesAids())
        return std::nullopt;
    // Kept by the position itself, its en passant square too (a best move's legality may depend on it): not by a
    // hash two positions can share.
    const auto position = study::evalsPosition(board);
    auto key            = std::string(reinterpret_cast<const char*>(position.bytes), position.size);
    key += static_cast<char>(board.enpassantSq().index());
    if (const auto it = offline_evals_seen_.find(key); it != offline_evals_seen_.end()) return it->second;
    // A few milliseconds of lookups a frame: from the disk (the index's first reads, or the runs of a preparation,
    // a page each), the Tree tab's column fills over the next frames rather than holding this one.
    if (ImGui::GetFrameCount() != offline_evals_frame_) {
        offline_evals_frame_ = ImGui::GetFrameCount();
        offline_evals_spent_ = 0;
    }
    if (offline_evals_spent_ > 0.006) return std::nullopt;
    if (offline_evals_seen_.size() > 4096) offline_evals_seen_.clear();
    const auto start = std::chrono::steady_clock::now();
    auto found       = offline_evals_->find(board);
    offline_evals_spent_ += std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    offline_evals_seen_.emplace(std::move(key), found);
    return found;
}

std::function<std::optional<PositionEval>(const chess::Board&)> App::reviewKnown() const {
    if (!offline_evals_) return {};
    auto lookup = offline_evals_->lookup();
    if (!lookup) return {};
    // The lookup held by the review: a preparation that ends meanwhile, or the program closed, does not take its
    // files away from under it.
    return [lookup = std::move(lookup)](const chess::Board& board) { return reviewKnownEval(*lookup, board); };
}

void App::startOfflineEvals(std::string source, int min_men) {
    if (!offline_evals_ || offline_evals_->busy()) return;
    offline_evals_error_.clear();
    offline_evals_failure_ = {};
    offline_evals_note_.clear();
    offline_evals_since_ = std::chrono::steady_clock::now();
    offline_evals_from_  = 0;
    offline_evals_->start(std::move(source), min_men);
}

void App::pollOfflineEvals() {
    if (!offline_evals_) return;
    if (page_ != Page::Lichess) offline_evals_keys_ = false;  // Enter is the card's only while it is shown
    offline_evals_->poll();
    if (isReady(offline_evals_update_)) {
        const auto size  = offline_evals_update_.get();
        const auto state = offline_evals_->state();
        if (size.failed()) {
            offline_evals_note_ = trf("The evaluations kept stay: {}.", lichess::describe(size));
        } else if (!size.date.empty() && size.date == state.date) {
            offline_evals_note_ = trf("Already the latest file, of {}.", size.date);
        } else {
            startOfflineEvals(lichess::evalsUrl(), state.min_men);
        }
    }
    const auto end = offline_evals_->finished();
    if (offline_evals_->generation() != offline_evals_generation_) {  // what answers changed
        offline_evals_generation_ = offline_evals_->generation();
        offline_evals_seen_.clear();
    }
    if (!end) return;
    const auto state = offline_evals_->state();
    if (end->done()) {
        Console::instance().add(trf("Lichess's evaluations kept offline: {} positions, {}",
                                    ui::exactCount(state.positions), formatBytes(state.bytes)));
    } else if (end->failed()) {
        offline_evals_error_   = end->error;
        offline_evals_failure_ = end->net;
        Console::instance().add(trf("Lichess's evaluations offline: the preparation stopped: {}", end->error) +
                                    (end->net.detail.empty() ? std::string() : " (" + end->net.detail + ")"),
                                true);
    }
}

void App::drawOfflineEvalLine(const chess::Board& board) {
    const auto e = offlineEval(board);
    if (!e) return;
    const auto move  = numberedMove(board, e->best);
    const auto text  = move.empty() ? trf("Lichess offline · {} · depth {}", whiteScore(*e), e->depth)
                                    : trf("Lichess offline · {} · depth {} · {}", whiteScore(*e), e->depth, move);
    const auto state = offline_evals_->state();
    if (ImGui::Selectable(fmt::format("{}##offline_eval", text).c_str()) && !e->best.empty()) {
        BoardPreview preview;
        preview.ply    = board_.ply;
        preview.at     = board_.ply;
        preview.moves  = {e->best};
        preview.label  = TR("Lichess offline");
        board_.preview = std::move(preview);
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "%s", (state.date.empty() ? std::string(TR("Lichess's evaluation of this position, kept on this disk: "
                                                       "nothing is asked of the network. Scores are White's; a click "
                                                       "shows the move on the board."))
                                      : trf("Lichess's evaluation of this position, kept on this disk (the file of "
                                            "{}): nothing is asked of the network. Scores are White's; a click shows "
                                            "the move on the board.",
                                            state.date))
                      .c_str());
    ui::topic(guide::topics::OfflineEvals);
}

void App::drawLichessOfflineEvals() {
    if (!offline_evals_) return;
    auto& evals      = *offline_evals_;
    const auto state = evals.state();
    if (offline_evals_focus_) {  // brought into view by the page, which scrolls (drawLichessPage)
        offline_evals_y_     = ImGui::GetCursorScreenPos().y;
        offline_evals_focus_ = false;
        offline_evals_keys_  = true;
    }
    const ui::Card card("lichess_offline_evals");
    // Enter: when the card is under the mouse, or was brought into view by Ctrl+K.
    const bool enter = ImGui::IsKeyPressed(ImGuiKey_Enter, false) && keyboard::available() &&
                       (offline_evals_keys_ || ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows));
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows))
        offline_evals_keys_ = false;  // a click elsewhere: Enter is another card's
    const ui::TextWrap wrap;
    ui::inlineIcon(ui::Icon::Download, ImGui::GetColorU32(ImGuiCol_Text));
    ImGui::SameLine();
    ImGui::PushFont(ui::titleFont(), ImGui::GetFontSize() * 1.1f);
    ImGui::TextUnformatted(TR("Lichess's evaluations, offline"));
    ImGui::PopFont();
    ImGui::SameLine();
    ui::guideLink("offline_evals_help", guide::topics::OfflineEvals);
    ImGui::TextDisabled(
        "%s", trf("{} million positions evaluated by Stockfish on Lichess (database.lichess.org, CC0). Kept on "
                  "your disk with their evaluation, depth and best move, they give an evaluation at once where "
                  "Lichess has one, without asking the network.",
                  (EVALS_POSITIONS + 500000) / 1000000)
                  .c_str());
    ImGui::Spacing();
    const auto openings = [] {
        return trf("The openings only: the positions with {} men or more, about the first 20 moves",
                   study::EVALS_OPENING_MEN);
    };

    // During: the progress, Cancel.
    if (evals.busy()) {
        const auto& p    = evals.progress();
        const auto size  = p.size.load();
        const auto bytes = p.bytes.load();
        if (p.merging) {
            const auto total = p.total.load(), merged = p.merged.load();
            ImGui::TextUnformatted(
                trf("Writing the index: {} positions of {}", ui::exactCount(merged), ui::exactCount(total)).c_str());
            ImGui::ProgressBar(
                total ? static_cast<float>(static_cast<double>(merged) / static_cast<double>(total)) : 0.0f,
                ImVec2(-1, px(8)), "");
        } else {
            // The rate since this preparation began, from where it began (a reading gone on starts further).
            if (offline_evals_from_ == 0 && bytes > 0) offline_evals_from_ = bytes;
            const double seconds =
                std::chrono::duration<double>(std::chrono::steady_clock::now() - offline_evals_since_).count();
            const double rate = seconds > 5 && bytes > offline_evals_from_
                                    ? static_cast<double>(bytes - offline_evals_from_) / seconds
                                    : 0.0;
            ImGui::TextUnformatted((size ? trf("{} read of {} · {} positions kept", formatBytes(bytes),
                                               formatBytes(size), ui::exactCount(p.kept))
                                    : bytes
                                        ? trf("{} read · {} positions kept", formatBytes(bytes), ui::exactCount(p.kept))
                                        : std::string(TR("Connecting…")))
                                       .c_str());
            if (size) {
                const int percent = static_cast<int>(100.0 * static_cast<double>(bytes) / static_cast<double>(size));
                ImGui::SameLine();
                ImGui::TextDisabled(
                    "%s", rate > 0 ? trf("{} % · about {} left", percent,
                                         ui::formatDuration(static_cast<double>(size - std::min(size, bytes)) / rate))
                                         .c_str()
                                   : fmt::format("{} %", percent).c_str());
            }
            ImGui::ProgressBar(size ? static_cast<float>(static_cast<double>(bytes) / static_cast<double>(size)) : 0.0f,
                               ImVec2(-1, px(8)), "");
        }
        ImGui::TextDisabled("%s", TR("The positions read answer a few million at a time, without waiting for the end. "
                                     "Cut short (Cancel, the network, the program closed), the download goes on from "
                                     "where it stopped."));
        if (ImGui::Button(TR("Cancel##offline_evals"))) evals.cancel();
        return;
    }

    // Ready: what is kept, Update, Remove them.
    if (state.ready) {
        ImGui::BeginGroup();
        ui::inlineIcon(ui::Icon::Check, ui::GOOD);
        ImGui::SameLine();
        std::string what = trf("{} positions · {}", ui::exactCount(state.positions), formatBytes(state.bytes));
        if (!state.date.empty()) what += " · " + trf("the file of {}", state.date);
        if (state.min_men > 0) what += " · " + trf("{} men or more", state.min_men);
        ImGui::TextUnformatted(what.c_str());
        ImGui::EndGroup();
        ImGui::TextDisabled("%s", TR("Lichess publishes the file again each month: Update reads the new one, the "
                                     "evaluations kept answering meanwhile."));
        if (state.stopped) {
            ImGui::TextUnformatted(
                trf("An update stopped at {} of {}.", formatBytes(state.stopped_at), formatBytes(state.size)).c_str());
            if (!offline_evals_error_.empty())  // the file out of the sentence, its folder a click away (#141)
                drawRepairNotice(netNotice(offline_evals_failure_, offline_evals_error_, {}, Notice::Level::Warning),
                                 "offline_evals_update");
            if (ImGui::Button(TR("Go on##offline_evals_update")))
                startOfflineEvals(state.source, state.stopped_min_men);
            ImGui::SameLine();
            if (ImGui::Button(TR("Forget it"))) {
                evals.forgetStopped();
                offline_evals_error_.clear();
                offline_evals_failure_ = {};
            }
        } else if (offline_evals_remove_) {
            ImGui::TextUnformatted(trf("Remove Lichess's evaluations kept offline? It frees {}; having them again "
                                       "means reading the whole file again.",
                                       formatBytes(state.bytes))
                                       .c_str());
            if (ImGui::Button(TR("Remove them##offline_evals_yes"))) {
                evals.remove();
                offline_evals_seen_.clear();
                offline_evals_remove_ = false;
                offline_evals_note_.clear();
            }
            ImGui::SameLine();
            if (ImGui::Button(TR("Keep them")) || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
                offline_evals_remove_ = false;
        } else {
            ImGui::BeginDisabled(offline_evals_update_.valid());
            if (ImGui::Button(TR("Update"))) {
                offline_evals_note_.clear();
                offline_evals_update_ = std::async(std::launch::async, [stop = jobs_.cancelFlag()] {
                    return evalsSize(lichess::evalsUrl(), stop.get());
                });
            }
            ImGui::EndDisabled();
            tip(
                TR("Asks database.lichess.org for its file's date; a newer file is read while it downloads, the "
                   "evaluations kept answering until it is ready."));
            ImGui::SameLine();
            if (ImGui::Button(trf("Remove them · frees {}", formatBytes(state.bytes)).c_str()))
                offline_evals_remove_ = true;
            if (offline_evals_update_.valid()) ImGui::TextDisabled("%s", TR("Asking database.lichess.org…"));
            if (!offline_evals_note_.empty()) ImGui::TextDisabled("%s", offline_evals_note_.c_str());
        }
        ImGui::Spacing();
        ImGui::TextUnformatted(TR("Where they serve"));
        const auto bullet = [](const std::string& text) {
            ImGui::Bullet();
            ImGui::SameLine();
            ImGui::TextUnformatted(text.c_str());  // wrapped, unlike BulletText
        };
        bullet(
            TR("Analysis: a line above the engine (“Lichess offline · +0.31 · depth 46 · 6…e6”), there as soon as "
               "the position shows."));
        bullet(
            trf("Review: the positions known at depth {} or more are taken as they are; the engine searches the "
                "others.",
                REVIEW_KNOWN_DEPTH));
        bullet(TR("Tree: an Eval. column beside each move."));
        ImGui::TextDisabled("%s", TR("What leaves the machine: the request for the file to database.lichess.org, "
                                     "its size said before. Then nothing."));
        return;
    }

    // Before: what is kept, the disk, the file; or a preparation cut short.
    const auto free = freeSpace(evals.folder());
    if (state.stopped) {
        ImGui::TextUnformatted(
            (state.size
                 ? trf("The preparation stopped at {} of {}: {} positions kept so far, {} of them answering "
                       "already.",
                       formatBytes(state.stopped_at), formatBytes(state.size), ui::exactCount(state.kept),
                       ui::exactCount(state.answering))
                 : trf("The preparation stopped at {}: {} positions kept so far, {} of them answering already.",
                       formatBytes(state.stopped_at), ui::exactCount(state.kept), ui::exactCount(state.answering)))
                .c_str());
        ImGui::TextDisabled("%s", (state.stopped_min_men > 0 ? openings() : std::string(TR("Every position"))).c_str());
        if (!offline_evals_error_.empty())  // the file out of the sentence, its folder a click away (#141)
            drawRepairNotice(netNotice(offline_evals_failure_, offline_evals_error_, {}, Notice::Level::Warning),
                             "offline_evals_stopped");
        if (ImGui::Button(TR("Go on##offline_evals")) || enter) {
            offline_evals_keys_ = false;
            startOfflineEvals(state.source, state.stopped_min_men);
        }
        tip(TR("Reads the rest of the file, from where it stopped."));
        ImGui::SameLine();
        if (ImGui::Button(TR("Start again##offline_evals"))) {
            evals.forgetStopped();
            offline_evals_error_.clear();
            offline_evals_failure_ = {};
        }
        return;
    }
    ImGui::TextUnformatted(TR("What is kept"));
    ImGui::RadioButton(TR("Every position: the evaluation, the depth and the best move of each"),
                       &offline_evals_choice_, 0);
    ImGui::SameLine();
    ImGui::TextDisabled("%s", trf("· about {}", formatBytes(EVALS_ALL_BYTES)).c_str());
    ImGui::RadioButton(openings().c_str(), &offline_evals_choice_, 1);
    tip(trf("Lichess's file gives no move number: a position counts among the openings by the men still on the board. "
            "In Lichess's games, {} men or more is 89 % of the positions of the first 20 moves, and 8 % of the "
            "later ones.",
            study::EVALS_OPENING_MEN)
            .c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("%s", trf("· about {}", formatBytes(EVALS_OPENINGS_BYTES)).c_str());
    const auto kept   = offline_evals_choice_ == 1 ? EVALS_OPENINGS_BYTES : EVALS_ALL_BYTES;
    const auto needed = kept + kept * 4 / 3;  // its runs (12 bytes a position) beside the index (9) while it prepares
    std::string disk =
        trf("Lichess's file: about {}, read while it downloads, never kept", formatBytes(EVALS_FILE_BYTES));
    if (free > 0) disk += " · " + trf("on this disk: {} free", formatBytes(free));
    disk += " · " + trf("about {} used while it prepares", formatBytes(needed));
    ImGui::TextDisabled("%s", disk.c_str());
    if (free > 0 && free < needed)
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s",
                           trf("This disk lacks room: free at least {} in {}.", formatBytes(needed - free),
                               evals.folder().parent_path().string())
                               .c_str());
    if (!offline_evals_error_.empty())  // the file out of the sentence, its folder a click away (#141)
        drawRepairNotice(netNotice(offline_evals_failure_, offline_evals_error_, {}, Notice::Level::Warning),
                         "offline_evals_error");
    const int min_men = offline_evals_choice_ == 1 ? study::EVALS_OPENING_MEN : 0;
    if (ImGui::Button(TR("Download and prepare")) || enter) {
        offline_evals_keys_ = false;
        startOfflineEvals(lichess::evalsUrl(), min_men);
    }
    tip(
        TR("Downloads Lichess's evaluations database and keeps what is chosen, while it arrives; a download cut short "
           "goes on from where it stopped."));
    ImGui::SameLine();
    ImGui::TextDisabled("%s", TR("Enter"));
    ImGui::SameLine();
    if (ImGui::Button(TR("A file already downloaded…"))) {
        dialogs_.openFile(
            [this](const std::string& path) {
                startOfflineEvals(path, offline_evals_choice_ == 1 ? study::EVALS_OPENING_MEN : 0);
            },
            {{"zstd", "zst"}, {TR("All files"), "*"}});
    }
    tip(TR("lichess_db_eval.jsonl.zst, from database.lichess.org: read from the disk, nothing is downloaded."));
    ImGui::TextDisabled("%s", TR("What leaves the machine: the request for the file to database.lichess.org, its size "
                                 "said before. Then nothing."));
}

}  // namespace fcg
