// From the Board to lichess.org (#54, mockup M8, board "Lien"): the position opened on Lichess's analysis board, the
// game shared, the position played out, a game of Lichess bookmarked. Each says what it sends; none touches the line of
// a game the user is playing on Lichess (R17), the rest staying possible meanwhile.

#include <cfloat>
#include <chrono>
#include <future>

#include <SDL3/SDL.h>
#include <imgui.h>

#include <fmt/include/fmt/format.h>

#include "app.hpp"
#include "board_text.hpp"
#include "console.hpp"
#include "i18n.hpp"
#include "lichess_links.hpp"
#include "ui.hpp"

namespace fcg {

namespace {

// The time controls offered for a position played out.
const lichess::ChallengeClock CLOCKS[] = {{3, 2, 0},   {5, 3, 0}, {10, 5, 0}, {15, 10, 0},
                                          {30, 20, 0}, {0, 0, 1}, {0, 0, 3}};

// An action of the menu: its name, what it does and what it sends, greyed with why when it cannot be done.
bool action(const char* id, const char* label, const char* what, const std::string& sent, const char* why_not) {
    ImGui::PushID(id);
    ImGui::BeginDisabled(why_not != nullptr);
    const bool clicked = ImGui::Selectable(label, false, ImGuiSelectableFlags_None);
    ImGui::EndDisabled();
    ImGui::Indent();
    ImGui::TextDisabled("%s", why_not ? why_not : what);
    ImGui::TextDisabled("%s", trf("Sent: {}", sent).c_str());
    ImGui::Unindent();
    ImGui::PopID();
    return clicked;
}

}  // namespace

std::string App::boardLichessGameId() const {
    for (const char* tag : {"Site", "GameId"}) {
        const auto value = board_.tree.main.header(tag);
        if (value.empty()) continue;
        if (const auto link = lichess::parseLink(value); link && link->kind == lichess::LinkKind::Game) return link->id;
    }
    return {};
}

void App::openPositionOnLichess() {
    if (toLichessLock(false)) return;
    const auto& b    = board_;
    const auto board = boardAt(b.game, b.chess960, b.ply);
    SDL_OpenURL(lichess::analysisUrl(board.getFen(), b.chess960, b.flip).c_str());
}

void App::shareGameOnLichess() {
    if (toLichessLock(true) || lichess_share_job_.valid()) return;
    to_lichess_        = ToLichess{};
    to_lichess_->kind  = ToLichess::Kind::Share;
    to_lichess_->busy  = true;
    lichess_share_job_ = std::async(std::launch::async,
                                    [pgn = gamePgn(board_.tree, board_.chess960)] { return lichess::importGame(pgn); });
}

void App::playPositionOnLichess(const lichess::ChallengeClock& clock) {
    if (toLichessLock(false) || lichess_challenge_job_.valid()) return;
    const auto& b      = board_;
    to_lichess_        = ToLichess{};
    to_lichess_->kind  = ToLichess::Kind::Challenge;
    to_lichess_->busy  = true;
    to_lichess_->clock = clock;
    lichess_challenge_job_ =
        std::async(std::launch::async, [fen = boardAt(b.game, b.chess960, b.ply).getFen(), chess960 = b.chess960,
                                        clock] { return lichess::openChallenge(fen, chess960, clock); });
}

void App::bookmarkGameOnLichess() {
    const auto id = boardLichessGameId();
    if (id.empty() || !lichess_account_ || lichess_bookmark_job_.valid()) return;
    to_lichess_       = ToLichess{};
    to_lichess_->kind = ToLichess::Kind::Bookmark;
    if (!lichess_account_->has(lichess::SCOPE_PREFERENCE_WRITE)) {
        lichess_bookmark_pending_ = id;  // asked first, under the board
        return;
    }
    to_lichess_->busy     = true;
    lichess_bookmark_job_ = std::async(
        std::launch::async, [id, token = lichess_account_->token] { return lichess::bookmarkGame(id, token); });
}

void App::pollToLichess() {
    using namespace std::chrono_literals;
    if (lichess_share_job_.valid() && lichess_share_job_.wait_for(0s) == std::future_status::ready) {
        const auto answer = lichess_share_job_.get();
        if (to_lichess_ && to_lichess_->kind == ToLichess::Kind::Share) {
            to_lichess_->busy = false;
            if (answer.value) {
                to_lichess_->url = *answer.value;
                ImGui::SetClipboardText(answer.value->c_str());
                Console::instance().add(trf("The game is on Lichess, its address copied: {}", *answer.value));
            } else {
                to_lichess_->error = answer.error;
            }
        }
    }
    if (lichess_challenge_job_.valid() && lichess_challenge_job_.wait_for(0s) == std::future_status::ready) {
        auto answer = lichess_challenge_job_.get();
        if (to_lichess_ && to_lichess_->kind == ToLichess::Kind::Challenge) {
            to_lichess_->busy = false;
            if (answer.value)
                to_lichess_->challenge = std::move(answer.value);
            else
                to_lichess_->error = answer.error;
        }
    }
    if (lichess_bookmark_job_.valid() && lichess_bookmark_job_.wait_for(0s) == std::future_status::ready) {
        const auto answer = lichess_bookmark_job_.get();
        if (to_lichess_ && to_lichess_->kind == ToLichess::Kind::Bookmark) {
            to_lichess_->busy = false;
            if (!answer.value) to_lichess_->error = answer.error;
        }
        if (answer.value) Console::instance().add(TR("The game is in your bookmarks on Lichess"));
    }
    // The permission granted on Lichess's page: the game asked for bookmarked.
    if (!lichess_bookmark_pending_.empty() && !lichess_link_ && lichess_account_ &&
        lichess_account_->has(lichess::SCOPE_PREFERENCE_WRITE)) {
        const auto id         = std::exchange(lichess_bookmark_pending_, {});
        to_lichess_           = ToLichess{};
        to_lichess_->kind     = ToLichess::Kind::Bookmark;
        to_lichess_->busy     = true;
        lichess_bookmark_job_ = std::async(
            std::launch::async, [id, token = lichess_account_->token] { return lichess::bookmarkGame(id, token); });
    }
}

void App::drawToLichessMenu() {
    if (ui::iconButton("to_lichess", ui::Icon::External, TR("lichess.org…")) || std::exchange(open_to_lichess_, false))
        ImGui::OpenPopup("##to_lichess");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", TR("Open the position on lichess.org, share the game, have the position played out, "
                                   "bookmark a game of Lichess."));
    ImGui::SetNextWindowSizeConstraints(ImVec2(ImGui::GetFontSize() * 24, 0),
                                        ImVec2(ImGui::GetFontSize() * 30, FLT_MAX));
    if (!ImGui::BeginPopup("##to_lichess")) return;
    drawToLichessActions();
    ImGui::EndPopup();
}

void App::drawToLichessActions() {
    const ui::TopicRegion topic(guide::topics::ToLichess);  // its group closed before the popup
    ImGui::PushTextWrapPos(0);
    ImGui::TextUnformatted(TR("To lichess.org"));
    ImGui::Separator();
    const auto* on_position = toLichessLock(false);
    const auto* on_game     = toLichessLock(true);
    if (on_position || on_game) {
        ui::inlineIcon(ui::Icon::Lock, ui::WARN);
        ImGui::SameLine();
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s",
                           TR("A game you are playing on Lichess is on this line: nothing of it leaves the machine "
                              "while it is played."));
        ImGui::Spacing();
    }
    const char* locked = TR("Not on the line of a game you are playing.");
    if (action("open", TR("Open the position on lichess.org"), TR("Its analysis board, in the browser."),
               TR("the position, in the address; nothing else."), on_position ? locked : nullptr)) {
        openPositionOnLichess();
        ImGui::CloseCurrentPopup();
    }
    ImGui::Spacing();
    const bool empty = board_.tree.main.moves.empty();
    if (action("share", TR("Share the game"), TR("Lichess keeps a copy; its address is copied."),
               TR("the game, without an account. Whoever has its address can see it, its players' names too."),
               on_game                      ? locked
               : empty                      ? TR("A game with moves.")
               : lichess_share_job_.valid() ? TR("Being shared.")
                                            : nullptr)) {
        shareGameOnLichess();
        ImGui::CloseCurrentPopup();
    }
    ImGui::Spacing();
    // Played out: the time control chosen in the line under it.
    ImGui::PushID("play");
    ImGui::BeginDisabled(on_position != nullptr || lichess_challenge_job_.valid());
    ImGui::TextUnformatted(TR("Have the position played out"));
    ImGui::Indent();
    for (const auto& clock : CLOCKS) {
        ImGui::PushID(&clock);
        if (&clock != &CLOCKS[0]) ui::sameLineOrWrap(lichess::clockText(clock).c_str());
        if (ImGui::SmallButton(lichess::clockText(clock).c_str())) {
            playPositionOnLichess(clock);
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopID();
    }
    ImGui::EndDisabled();
    ImGui::TextDisabled("%s", on_position ? locked
                                          : TR("An open challenge from this position: a link for each side, to send to "
                                               "whoever you want. Not rated."));
    ImGui::TextDisabled("%s",
                        trf("Sent: {}", TR("the position and the time control chosen, without an account.")).c_str());
    ImGui::Unindent();
    ImGui::PopID();
    ImGui::Spacing();
    const bool from_lichess = !boardLichessGameId().empty();
    if (action("bookmark", TR("Bookmark the game on Lichess"), TR("Among your bookmarks on lichess.org."),
               TR("its id, with your token (one more permission, asked the first time)."),
               !from_lichess       ? TR("For a game from Lichess.")
               : !lichess_account_ ? TR("Your Lichess account linked, on the Lichess page.")
                                   : nullptr)) {
        bookmarkGameOnLichess();
        ImGui::CloseCurrentPopup();
    }
    ImGui::PopTextWrapPos();
}

void App::drawToLichessResult(float width) {
    // One more permission, for a bookmark.
    if (!lichess_bookmark_pending_.empty() && lichess_account_) {
        ui::beginCard("to_lichess_scope", ImVec2(width, 0));
        ImGui::PushTextWrapPos(0);
        ImGui::TextUnformatted(TR("Lichess asks for one more permission"));
        ImGui::TextDisabled("%s",
                            TR("To bookmark a game, RookStudio must be allowed to change your preferences on "
                               "Lichess; it changes nothing else. Lichess's page opens for it; your account stays "
                               "linked meanwhile, and the token it replaces is revoked."));
        if (lichess_link_) {
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::ACCENT), "%s",
                               TR("Waiting for Lichess: allow RookStudio in the browser's page, then come back here."));
        } else {
            if (!lichess_error_.empty())
                ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s",
                                   lichess_error_.find("refused") != std::string::npos
                                       ? TR("You refused it on Lichess: nothing changed.")
                                       : trf("Lichess gave nothing ({}).", lichess_error_).c_str());
            if (ui::iconButton("to_lichess_allow", ui::Icon::External, TR("Allow on Lichess…"))) {
                auto scopes = lichess_account_->scopes;
                scopes.push_back(lichess::SCOPE_PREFERENCE_WRITE);
                startLichessLink(scopes, lichess_account_->username);
            }
            ImGui::SameLine();
            if (ImGui::SmallButton(TR("Not now"))) {
                lichess_bookmark_pending_.clear();
                to_lichess_.reset();
            }
        }
        ImGui::PopTextWrapPos();
        ui::endCard();
        return;
    }
    if (!to_lichess_ || (to_lichess_->kind == ToLichess::Kind::Bookmark && !lichess_bookmark_pending_.empty())) return;
    auto& t    = *to_lichess_;
    bool close = false;
    {
        const ui::TopicRegion topic(guide::topics::ToLichess);  // around the card, closed after it
        ui::beginCard("to_lichess_result", ImVec2(width, 0));
        ImGui::PushTextWrapPos(0);
        if (t.busy) {
            ImGui::TextDisabled("%s", TR("Asking Lichess…"));
        } else if (!t.error.empty()) {
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s",
                               trf("Lichess did not do it: {}", t.error).c_str());
        } else if (t.kind == ToLichess::Kind::Share) {
            ImGui::TextUnformatted(TR("The game is on Lichess; its address is copied."));
            ImGui::TextDisabled("%s", t.url.c_str());
            if (ui::iconButton("to_lichess_open_game", ui::Icon::External, TR("Open"))) SDL_OpenURL(t.url.c_str());
            ImGui::SameLine();
        } else if (t.kind == ToLichess::Kind::Challenge && t.challenge) {
            ImGui::TextUnformatted(trf("An open challenge on Lichess, {}: send each side's link to its player.",
                                       lichess::clockText(t.clock))
                                       .c_str());
            const auto side = [&](const char* id, const char* name, const std::string& url) {
                ImGui::PushID(id);
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(name);
                ImGui::SameLine();
                ImGui::TextDisabled("%s", url.c_str());
                ImGui::SameLine();
                if (ImGui::SmallButton(TR("Copy"))) ImGui::SetClipboardText(url.c_str());
                ImGui::SameLine();
                if (ImGui::SmallButton(TR("Open"))) SDL_OpenURL(url.c_str());
                ImGui::PopID();
            };
            side("white", TR("White"), t.challenge->white);
            side("black", TR("Black"), t.challenge->black);
            ImGui::TextDisabled("%s", TR("Lichess keeps it waiting for its players a while; the game is not rated."));
        } else if (t.kind == ToLichess::Kind::Bookmark) {
            ImGui::TextUnformatted(TR("The game is in your bookmarks on Lichess."));
        }
        if (!t.busy && ImGui::SmallButton(TR("Close"))) close = true;
        ImGui::PopTextWrapPos();
        ui::endCard();
    }
    if (close) to_lichess_.reset();
}

}  // namespace fcg
