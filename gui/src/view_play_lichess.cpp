// Playing on Lichess (#84, mockup M12, board 5): the Play page's "On Lichess" part (a seek in the lobby, a challenge
// sent, the challenges received, the games in progress), the game played on the Board page through Lichess's Board API
// (lichess_board.hpp), the challenges at the window's corner and on the Home page, and closing RookStudio during a live
// game. The lock of #48 keeps the aids away during the game as during any game of the user's: every aid in a live
// game, the engine, the cloud, the tables and the assistant on a correspondence game's positions, the tree of the
// user's databases and books staying open (Lichess allows a book there).

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string_view>

#include <SDL3/SDL.h>
#include <fmt/include/fmt/format.h>
#include <imgui.h>

#include "app.hpp"
#include "board_text.hpp"
#include "console.hpp"
#include "guide.hpp"
#include "i18n.hpp"
#include "sound.hpp"
#include "ui.hpp"

namespace fcg {

using lichess::ChallengeClock;

namespace {

using SteadyClock = std::chrono::steady_clock;

float px(float v) { return v * ImGui::GetFontSize() / 16.0f; }

constexpr double RESIGN_SECONDS = 4.0;  // "Resign…" waits so long for Enter or a second click
// A real-time seek closed by Lichess: a game's start this soon after is the one it found; later, it expired.
constexpr auto SEEK_TAKEN = std::chrono::seconds(15);

void tip(const std::string& text) {
    if (!ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled)) return;
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
    ImGui::TextUnformatted(text.c_str());
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

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

void coloured(const std::string& text, ImU32 tone) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(tone));
    ImGui::TextWrapped("%s", text.c_str());
    ImGui::PopStyleColor();
}

// A chip: a clock, a colour; filled when chosen.
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

// A short label on a coloured ground, on the line of the text around it.
void tag(const char* text, ImU32 tone) {
    const ImVec2 size = ImGui::CalcTextSize(text);
    const ImVec2 pad(px(7), px(2));
    const ImVec2 at = ImGui::GetCursorScreenPos();
    auto* dl        = ImGui::GetWindowDrawList();
    dl->AddRectFilled(ImVec2(at.x, at.y - pad.y), ImVec2(at.x + size.x + pad.x * 2, at.y + size.y + pad.y),
                      tone ? tone : ImGui::GetColorU32(ImGuiCol_FrameBg), px(5));
    dl->AddText(ImVec2(at.x + pad.x, at.y), tone ? IM_COL32_WHITE : ImGui::GetColorU32(ImGuiCol_TextDisabled), text);
    ImGui::Dummy(ImVec2(size.x + pad.x * 2, size.y));
}

void heading(const char* text) {
    ImGui::PushFont(ui::titleFont(), ImGui::GetStyle().FontSizeBase);
    ImGui::TextUnformatted(text);
    ImGui::PopFont();
}

std::string lower(std::string text) {
    for (auto& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

const char* speedName(std::string_view key) {
    if (key == "ultraBullet" || key == "bullet") return TR("Bullet");
    if (key == "blitz") return TR("Blitz");
    if (key == "rapid") return TR("Rapid");
    if (key == "classical") return TR("Classical");
    return TR("Correspondence");
}

// "Rated rapid 10+5", "Casual correspondence · 3 days a move"
std::string kindText(bool rated, std::string_view speed, const ChallengeClock& clock, bool unlimited = false) {
    const auto clock_text = unlimited ? std::string(TR("no clock")) : lichess::clockText(clock);
    return rated ? trf("Rated · {} {}", speedName(speed), clock_text)
                 : trf("Casual · {} {}", speedName(speed), clock_text);
}

// A clock of a game played, as Lichess shows it: minutes and seconds, tenths under 20 s; hours when there are.
std::string clockText(int64_t ms) {
    ms = std::max<int64_t>(0, ms);
    if (ms >= 3'600'000) return fmt::format("{}:{:02}:{:02}", ms / 3'600'000, ms / 60'000 % 60, ms / 1000 % 60);
    if (ms >= 20'000) return fmt::format("{}:{:02}", ms / 60'000, ms / 1000 % 60);
    return fmt::format("{}:{:02}.{}", ms / 60'000, ms / 1000 % 60, ms / 100 % 10);
}

// The time left to move by correspondence: "2 d 4 h left".
std::string daysLeft(int64_t ms) {
    const auto s = std::max<int64_t>(0, ms / 1000);
    if (s >= 86400) return trf("{} d {} h left", s / 86400, s % 86400 / 3600);
    if (s >= 3600) return trf("{} h {} min left", s / 3600, s % 3600 / 60);
    return trf("{} min left", s / 60);
}

// The seek's clocks (M12, board 5): rapid, classical, correspondence.
struct ClockRow {
    const char* speed;
    std::vector<ChallengeClock> clocks;
};
const std::vector<ClockRow>& seekRows() {
    static const std::vector<ClockRow> rows = {
        {"rapid", {{10, 0, 0}, {10, 5, 0}, {15, 10, 0}}},
        {"classical", {{30, 0, 0}, {30, 20, 0}}},
        {"correspondence", {{0, 0, 1}, {0, 0, 2}, {0, 0, 3}, {0, 0, 5}, {0, 0, 7}, {0, 0, 10}, {0, 0, 14}}}};
    return rows;
}

// A challenge's clocks: blitz too.
const std::vector<ChallengeClock>& challengeClocks() {
    static const std::vector<ChallengeClock> clocks = {{3, 2, 0}, {5, 3, 0}, {10, 5, 0}, {15, 10, 0}, {0, 0, 3}};
    return clocks;
}

// Lichess's reasons to decline, in the order the menu offers them.
struct Reason {
    const char* key;
    const char* label;
};
constexpr Reason REASONS[] = {{"generic", N_("Not now")},         {"later", N_("Later")},
                              {"tooFast", N_("Too fast for me")}, {"tooSlow", N_("Too slow for me")},
                              {"casual", N_("Casual only")},      {"rated", N_("Rated only")}};

// What a challenge declined says, from Lichess's key, in the interface's language.
std::string declinedText(const lichess::Challenge& c) {
    const auto& k = c.decline_key;
    if (k == "later") return TR("not now, later");
    if (k == "toofast") return TR("too fast for them");
    if (k == "tooslow") return TR("too slow for them");
    if (k == "timecontrol") return TR("not with this clock");
    if (k == "rated") return TR("they want a rated game");
    if (k == "casual") return TR("they want a casual game");
    if (k == "variant" || k == "standard") return TR("not this variant");
    if (!c.decline_reason.empty()) return c.decline_reason;
    return TR("no reason given");
}

// The clocks to offer once a challenge was declined: slower when it was too fast, faster when too slow.
std::vector<ChallengeClock> otherClocks(const lichess::Challenge& c, const ChallengeClock& was) {
    std::vector<ChallengeClock> out;
    const auto length = [](const ChallengeClock& k) {
        return k.days > 0 ? 1000000 + k.days : k.minutes * 60 + 40 * k.increment;
    };
    for (const auto& k : challengeClocks()) {
        if (k == was) continue;
        if (c.decline_key == "toofast" && length(k) <= length(was)) continue;
        if (c.decline_key == "tooslow" && length(k) >= length(was)) continue;
        out.push_back(k);
    }
    return out;
}

// The user's side to move in a game's state: the side of the first move, then each move.
bool whiteToMove(const lichess::BoardState& s) {
    const auto& fen    = s.initial_fen;
    const auto space   = fen.find(' ');
    const bool first_w = space == std::string::npos || space + 1 >= fen.size() || fen[space + 1] != 'b';
    return first_w == (s.moves.size() % 2 == 0);
}

}  // namespace

// ---------------------------------------------------------------------------
// What the profile keeps, the permission

lichess::OnlinePlaySettings& App::onlineSettings() {
    if (!online_settings_) {
        online_settings_ = lichess::readOnlinePlay(playFolder());
        // The player challenged last, in the field once.
        std::snprintf(online_user_.data(), online_user_.size(), "%s", online_settings_->opponent.c_str());
    }
    return *online_settings_;
}

void App::saveOnlineSettings() {
    if (online_settings_ && !options_.scripted) (void)lichess::writeOnlinePlay(playFolder(), *online_settings_);
}

bool App::onlineScope() const { return lichess_account_ && lichess_account_->has(lichess::SCOPE_BOARD_PLAY); }

void App::askOnlineScope() {
    if (lichess_link_ || !lichess_account_) return;
    auto scopes = lichess_account_->scopes;
    if (std::find(scopes.begin(), scopes.end(), lichess::SCOPE_BOARD_PLAY) == scopes.end())
        scopes.emplace_back(lichess::SCOPE_BOARD_PLAY);
    startLichessLink(scopes, lichess_account_->username);
}

// ---------------------------------------------------------------------------
// The events, the requests

void App::onlineCall(std::string what, std::function<lichess::Reply(const std::string& token)> call) {
    if (!lichess_account_) return;
    play_online_error_.clear();
    online_calls_.push_back(
        {std::move(what), std::async(std::launch::async, [token = lichess_account_->token, call = std::move(call)] {
             return call(token);
         })});
}

void App::onlineEvent(const lichess::GameEvent& e) {
    using Kind = lichess::GameEvent::Kind;
    switch (e.kind) {
        case Kind::Opened:  // the challenges waiting come again
            online_incoming_.clear();
            break;
        case Kind::Challenge: {
            if (!e.challenge.incoming) break;
            const auto same = [&](const lichess::Challenge& c) { return c.id == e.challenge.id; };
            if (std::none_of(online_incoming_.begin(), online_incoming_.end(), same)) {
                online_incoming_.push_back(e.challenge);
                // A new one rings, once, unless a live game is being played (it waits for its end).
                if (std::find(online_noticed_.begin(), online_noticed_.end(), e.challenge.id) ==
                        online_noticed_.end() &&
                    !liveGameOn())
                    sound::play(sound::Kind::Notify);
            }
            break;
        }
        case Kind::ChallengeCanceled:
            std::erase_if(online_incoming_, [&](const auto& c) { return c.id == e.challenge.id; });
            break;
        case Kind::ChallengeDeclined: {
            // The challenge sent from here: still waiting, or just told declined by its own request.
            if (e.challenge.incoming) break;
            const bool waiting =
                online_challenge_ && (online_challenge_->id() == e.challenge.id || online_challenge_->id().empty());
            if (!waiting && !(online_declined_ && online_declined_->id == e.challenge.id)) break;
            online_declined_ = e.challenge;
            if (waiting) online_challenge_->settle("declined");
            break;
        }
        case Kind::Started: {
            const auto& g = e.game;
            std::erase_if(online_incoming_, [&](const auto& c) { return c.id == g.id; });
            const bool accepted =
                std::find(online_accepted_.begin(), online_accepted_.end(), g.id) != online_accepted_.end();
            const bool challenged = online_challenge_ && online_challenge_->id() == g.id;
            if (challenged) online_challenge_->settle("accepted");
            // A real-time seek's game: from the lobby, while the seek is open or just after Lichess closed it.
            bool sought = false;
            if (online_seek_ && g.live() && (g.source.empty() || g.source == "lobby" || g.source == "pool")) {
                const auto st = online_seek_->state();
                sought =
                    st == lichess::Seek::State::Open || st == lichess::Seek::State::Opening ||
                    (st == lichess::Seek::State::Closed && SteadyClock::now() - online_seek_->closedAt() < SEEK_TAKEN);
                if (sought) online_seek_.reset();
            }
            // The live game RookStudio closed during, still being played.
            const bool resumed =
                !online_resume_checked_ && !onlineSettings().playing.empty() && onlineSettings().playing == g.id;
            // A game of the arena or the Swiss the user is in from here (#95): it opens by itself.
            const bool tournament = (arena_play_ && !g.tournament.empty() && g.tournament == arena_play_->id) ||
                                    (swiss_play_ && !g.swiss.empty() && g.swiss == swiss_play_->id);
            // It takes the Board, unless a live game is being played there (Lichess pairs one at a time); a
            // correspondence game shown there gives way, still among the games in progress.
            if ((accepted || challenged || sought || resumed || tournament) && lichess::playableHere(g)) {
                const auto& o = online_game_;
                if (!o || o->ended || o->id == g.id || o->state.over() || !o->state.live())
                    openOnlineGame(g.id, g.white);
            }
            if (resumed) online_resume_checked_ = true;
            break;
        }
        case Kind::Finished:
            if (online_game_ && online_game_->id == e.game.id) {
                online_game_->rating_diff  = e.game.rating_diff;
                online_game_->rating_known = e.game.rated;
            }
            break;
    }
}

void App::pollOnlinePlay() {
    const auto ready = [](auto& f) {
        return f.valid() && f.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
    };
    // The requests under way: what failed is said where the user plays.
    for (auto& c : online_calls_) {
        if (!ready(c.reply)) continue;
        const auto r = c.reply.get();
        if (r.ok) continue;
        play_online_error_ = trf("{}: {}", c.what, lichess::describe(r));
        if (page_ == Page::Board) boardToast(play_online_error_);
    }
    std::erase_if(online_calls_, [](const auto& c) { return !c.reply.valid(); });
    if (!lichess_account_) {
        online_seek_.reset();
        online_challenge_.reset();
        online_incoming_.clear();
        return;
    }
    // The game kept from the last session: looked for in the games in progress the event stream sends on open; gone,
    // it ended meanwhile.
    if (!online_resume_checked_ && lichess_events_ && lichess_events_->connected() &&
        SteadyClock::now() - lichess_events_opened_ > std::chrono::seconds(5)) {
        online_resume_checked_ = true;
        if (!onlineSettings().playing.empty() && !(online_game_ && online_game_->id == onlineSettings().playing)) {
            onlineSettings().playing.clear();
            saveOnlineSettings();
        }
    }
    // The seek: Lichess refused it, closed it (taken, or expired), or keeps it in its lobby (by correspondence).
    if (online_seek_) {
        const auto st  = online_seek_->state();
        const auto ask = online_seek_->ask();
        if (st == lichess::Seek::State::Failed) {
            const auto why    = online_seek_->failure();
            online_seek_said_ = why.net == lichess::NetError::Offline
                                    ? trf("Your seek stopped: {}.", lichess::describe(why))
                                    : trf("Lichess refused the seek: {}", lichess::describe(why));
            online_seek_.reset();
        } else if (st == lichess::Seek::State::Closed) {
            if (ask.clock.days > 0) {
                online_seek_said_ =
                    trf("Your seek, {}, waits in Lichess's lobby until someone takes it; the game will "
                        "be among your games in progress. The API cannot call it back: lichess.org's "
                        "lobby can.",
                        kindText(ask.rated, "correspondence", ask.clock));
                online_seek_.reset();
            } else if (SteadyClock::now() - online_seek_->closedAt() > SEEK_TAKEN) {
                online_seek_said_ = TR("Nobody took your seek before Lichess closed it: seek again?");
                online_seek_.reset();
            }
        }
    }
    // The challenge sent: refused, declined, called back; accepted, its game opens as it starts.
    if (online_challenge_) {
        const auto st  = online_challenge_->state();
        const auto who = online_challenge_->ask().user;
        if (st == lichess::ChallengeOut::State::Failed) {
            const auto why         = online_challenge_->failure();
            online_challenge_said_ = why.net == lichess::NetError::Offline
                                         ? trf("Your challenge to {} stopped: {}.", who, lichess::describe(why))
                                         : trf("Lichess refused the challenge to {}: {}", who, lichess::describe(why));
            online_challenge_.reset();
        } else if (st == lichess::ChallengeOut::State::Done) {
            const auto done = online_challenge_->done();
            if (done == "declined") {
                // Lichess's reason comes on the event stream, a moment before or after.
                online_challenge_said_ = who;
                if (!online_declined_ || online_declined_->id != online_challenge_->id()) {
                    lichess::Challenge c;
                    c.id             = online_challenge_->id();
                    c.dest           = who;
                    online_declined_ = c;
                }
                online_declined_->clock = online_challenge_->ask().clock;
            } else if (done == "canceled") {
                online_challenge_said_ = trf("Your challenge to {} is called back.", who);
            } else {
                online_challenge_said_.clear();
            }
            online_challenge_.reset();
        }
    }
    if (!online_game_) return;
    auto& og = *online_game_;
    // The token replaced (a link made again): the game's stream opened again with the new one.
    if (og.stream && !og.state.over() && og.stream->token() != lichess_account_->token) {
        og.stream  = std::make_unique<lichess::GameStream>(lichess_account_->token, og.id);
        og.version = 0;
    }
    if (og.stream && og.stream->state(og.state, og.version)) {
        og.board_dirty = true;
        // The user's side, by their name.
        const auto me = lower(lichess_account_->username);
        if (og.state.full) {
            if (lower(og.state.white.name) == me || lower(og.state.white.id) == me) og.white = true;
            if (lower(og.state.black.name) == me || lower(og.state.black.id) == me) og.white = false;
        }
        // The move sent: in its stream now.
        if (og.sent && og.state.moves.size() > og.sent_at) og.sent.reset();
        // A live game is kept for the next launch: closed during it, RookStudio brings it back.
        if (og.state.full && og.state.live() && !og.state.over() && onlineSettings().playing != og.id) {
            onlineSettings().playing = og.id;
            saveOnlineSettings();
        }
        // As the lock (#48) holds it: its position now, whose turn, its clock.
        if (!og.state.over() && og.state.full) {
            auto& p             = og.playing;
            const auto game     = lichess::replayOf(og.state);
            const bool chess960 = og.state.variant == "chess960";
            p.id                = og.id;
            if (p.full_id.empty()) p.full_id = og.id;
            p.board          = true;
            p.white          = og.white;
            p.fen            = boardAt(game, chess960, static_cast<int>(game.moves.size())).getFen();
            p.speed          = og.state.speed.empty() ? (og.state.live() ? "rapid" : "correspondence") : og.state.speed;
            p.variant        = og.state.variant;
            p.rated          = og.state.rated;
            p.my_turn        = whiteToMove(og.state) == og.white;
            p.tournament      = og.state.tournament;
            p.swiss           = og.state.swiss;
            const auto& them = og.white ? og.state.black : og.state.white;
            p.opponent       = them.name;
            p.opponent_rating = them.rating;
            p.seconds_left    = og.state.clockNow(og.white, SteadyClock::now()) / 1000;
        }
    }
    // The lock follows the game among the games in progress while it lasts, whatever the event stream did (opened
    // again, it lists them anew).
    if (!og.ended && !og.state.over() && og.state.full) {
        const auto it = std::find_if(lichess_playing_.begin(), lichess_playing_.end(),
                                     [&](const auto& g) { return g.id == og.id; });
        if (it == lichess_playing_.end()) {
            lichess_playing_.push_back(og.playing);
        } else {
            const auto full_id = it->full_id;
            *it                = og.playing;
            if (!full_id.empty()) it->full_id = full_id;
        }
    }
    // The move's answer: refused, or cut, it is taken back.
    if (ready(og.move_job)) {
        const auto r = og.move_job.get();
        if (!r.ok) {
            og.sent.reset();
            og.board_dirty = true;
            boardToast(r.net == lichess::NetError::Offline
                           ? std::string(TR("The connection to Lichess is cut: your move did not go. Play it again "
                                            "when it comes back."))
                           : trf("Lichess refused your move: {}", lichess::describe(r)));
        }
    }
    // A move made during the opponent's turn: played at the user's, if it is legal then.
    if (og.premove && !og.state.over() && !og.sent && !og.move_job.valid() && whiteToMove(og.state) == og.white &&
        og.state.connected) {
        const auto game     = lichess::replayOf(og.state);
        const bool chess960 = og.state.variant == "chess960";
        const auto board    = boardAt(game, chess960, static_cast<int>(game.moves.size()));
        const auto wanted   = *og.premove;
        og.premove.reset();
        chess::Movelist legal;
        chess::movegen::legalmoves(legal, board);
        std::optional<chess::Move> found;
        for (const auto& m : legal)
            if (m.from() == wanted.from() && m.to() == wanted.to() && m.typeOf() == wanted.typeOf() &&
                (m.typeOf() != chess::Move::PROMOTION || m.promotionType() == wanted.promotionType()))
                found = m;
        if (found)
            sendOnlineMove(*found);
        else
            boardToast(TR("Your move made in advance is not legal now: it is dropped"));
        og.board_dirty = true;
    }
    if (og.board_dirty && og.shown) showOnlineGame();
    if (og.state.over() && !og.ended) finishOnlineGame();
    if (og.state.ended && !og.state.over() && !og.ended) {
        // Lichess does not know it any more, or refused the token: nothing more can be done here.
        og.ended = true;
        if (onlineSettings().playing == og.id) {
            onlineSettings().playing.clear();
            saveOnlineSettings();
        }
    }
}

// ---------------------------------------------------------------------------
// A seek, a challenge, the answers

void App::startSeek() {
    if (!onlineScope() || online_seek_) return;
    auto& s = onlineSettings();
    lichess::SeekAsk ask;
    ask.clock = s.seekClock();
    if (!lichess::seekAllowed(ask.clock)) return;
    ask.rated = s.seek_rated;
    // The opponents around the user's rating in that speed, when it is known and sure.
    if (s.seek_range > 0 && lichess_profile_) {
        const auto speed = lichess::speedOf(ask.clock);
        for (const auto& r : lichess_profile_->speeds)
            if (r.key == speed && r.games > 0 && !r.provisional) {
                ask.rating_min = std::max(600, r.rating - s.seek_range);
                ask.rating_max = r.rating + s.seek_range;
            }
    }
    online_seek_said_.clear();
    online_seek_ = std::make_unique<lichess::Seek>(lichess_account_->token, ask);
}

void App::cancelSeek() {
    if (!online_seek_) return;
    if (online_seek_->ask().clock.days <= 0) online_seek_said_ = TR("Seek called back.");
    online_seek_.reset();  // its request closed: Lichess takes it out of the lobby
}

void App::sendChallenge(const lichess::ChallengeAsk& ask) {
    if (!onlineScope() || online_challenge_ || ask.user.empty()) return;
    online_challenge_said_.clear();
    online_declined_.reset();
    online_challenge_ = std::make_unique<lichess::ChallengeOut>(lichess_account_->token, ask);
}

void App::cancelChallenge() {
    if (online_challenge_) online_challenge_->cancel();
}

void App::answerChallenge(const std::string& id, bool accept, const std::string& reason) {
    const auto it =
        std::find_if(online_incoming_.begin(), online_incoming_.end(), [&](const auto& c) { return c.id == id; });
    if (it == online_incoming_.end()) return;
    const auto who = it->challenger;
    if (accept) {
        online_accepted_.push_back(id);
        onlineCall(trf("The challenge of {}", who),
                   [id](const std::string& token) { return lichess::acceptChallenge(token, id); });
    } else {
        onlineCall(trf("The challenge of {}", who),
                   [id, reason](const std::string& token) { return lichess::declineChallenge(token, id, reason); });
    }
    online_noticed_.push_back(id);
    online_incoming_.erase(it);
}

// ---------------------------------------------------------------------------
// The game on the Board

void App::openOnlineGame(const std::string& id, bool white) {
    if (!lichess_account_) return;
    if (online_game_ && online_game_->id == id && !online_game_->ended) {
        backToOnlineGame();
        return;
    }
    auto& b = board_;
    // The game shown gives way, among the recent games with its changes.
    rememberBoardGame();
    if (engine_game_ && engine_game_->over) closeEngineGame();
    board_origin_.reset();
    b.undo.clear();
    b.redo.clear();
    b.start_notes = {};
    b.comment_edit.reset();
    b.changes  = {};
    b.watching = false;
    b.selected.reset();
    b.move_typed.reset();
    b.preview.reset();
    b.pgn.reset();
    if (b.analysing) setBoardAnalysis(false);
    b.threat_on          = false;
    online_game_         = std::make_unique<OnlineGame>();
    online_game_->id     = id;
    online_game_->white  = white;
    online_game_->stream = std::make_unique<lichess::GameStream>(lichess_account_->token, id);
    // Until its stream speaks: the start, the user's side below.
    b.game           = ReplayGame{};
    b.game.start_fen = std::string(chess::constants::STARTPOS);
    b.tree           = GameTree{b.game, {}};
    b.line           = -1;
    b.ply            = 0;
    b.chess960       = false;
    b.flip           = !white;
    b.title          = trf("Lichess · {}", id);
    b.message.clear();
    board_side_tab_ = 0;  // the notation: the other tabs are locked during a live game
    toStudyBoard();
    board_back_.reset();
    ++board_game_serial_;
}

bool App::onlineShown() const {
    return online_game_ && online_game_->shown && !online_game_->ended && !online_game_->state.over();
}

bool App::onlineMyTurn() const {
    if (!online_game_) return false;
    const auto& og = *online_game_;
    return og.state.full && !og.state.over() && !og.sent && whiteToMove(og.state) == og.white;
}

void App::showOnlineGame() {
    auto& og            = *online_game_;
    auto& b             = board_;
    auto game           = lichess::replayOf(og.state);
    const bool chess960 = og.state.variant == "chess960";
    // The move sent, shown played until its stream has it.
    if (og.sent) {
        auto board = boardAt(game, chess960, static_cast<int>(game.moves.size()));
        chess::Movelist legal;
        chess::movegen::legalmoves(legal, board);
        if (std::find(legal.begin(), legal.end(), *og.sent) != legal.end()) {
            ReplayMove m;
            m.san   = chess::uci::moveToSan(board, *og.sent);
            m.move  = *og.sent;
            m.mover = board.sideToMove();
            game.moves.push_back(std::move(m));
        }
    }
    // The board follows the game, unless the user went back in it.
    const bool at_end = b.line >= 0 || b.ply >= static_cast<int>(b.game.moves.size());
    b.game            = std::move(game);
    b.tree            = GameTree{b.game, {}};
    b.line            = -1;
    b.chess960        = chess960;
    if (at_end) b.ply = static_cast<int>(b.game.moves.size());
    b.ply = std::clamp(b.ply, 0, static_cast<int>(b.game.moves.size()));
    if (og.state.full) {
        b.title = fmt::format("{} – {}", og.state.white.name, og.state.black.name);
        if (!og.flipped) {
            b.flip     = !og.white;
            og.flipped = true;
        }
    }
    og.board_dirty = false;
}

void App::backToOnlineGame() {
    if (!online_game_) return;
    auto& og = *online_game_;
    auto& b  = board_;
    if (!og.shown) {
        rememberBoardGame();
        board_origin_.reset();
        b.undo.clear();
        b.redo.clear();
        b.start_notes = {};
        b.comment_edit.reset();
        b.changes  = {};
        b.watching = false;
        b.pgn.reset();
        b.ply      = INT32_MAX;  // at the move being played
        og.shown   = true;
        og.flipped = false;
        ++board_game_serial_;
    }
    og.board_dirty = true;
    showOnlineGame();
    b.selected.reset();
    toStudyBoard();
    board_back_.reset();
}

chess::Board App::boardForPicking(const chess::Board& shown) const {
    if (!onlineShown()) return shown;
    const auto& og = *online_game_;
    const auto& b  = board_;
    if (!og.state.live() || og.held || b.line >= 0 || b.ply != static_cast<int>(b.game.moves.size())) return shown;
    if ((shown.sideToMove() == chess::Color::WHITE) == og.white) return shown;
    auto passed = shown;  // the opponent's turn: the user's men as if it were theirs
    passed.makeNullMove();
    return passed;
}

void App::onlineMoveAsked(chess::Move move) {
    auto& og = *online_game_;
    auto& b  = board_;
    b.selected.reset();
    if (og.state.over() || !og.state.full) return;
    if (!og.state.connected) {
        boardToast(TR("The connection to Lichess is cut: your move does not go. Play it again when it comes back."));
        return;
    }
    const bool chess960 = og.state.variant == "chess960";
    if (!onlineMyTurn()) {
        if (!og.state.live()) {
            boardToast(TR("It is your opponent's move"));
            return;
        }
        // Live: it waits for the user's turn, played then if it is legal.
        og.premove  = move;
        auto passed = boardAt(b.game, chess960, static_cast<int>(b.game.moves.size()));
        passed.makeNullMove();
        boardToast(trf("{} waits for your turn · Esc takes it back", boardSan(chess::uci::moveToSan(passed, move))));
        return;
    }
    // By correspondence, a move waits for Enter (M12, board 9, point 9); live, it goes at once.
    if (!og.state.live()) {
        og.held = move;
        return;
    }
    sendOnlineMove(move);
}

void App::sendOnlineMove(chess::Move move) {
    auto& og = *online_game_;
    if (!lichess_account_ || og.move_job.valid()) return;
    const bool chess960 = og.state.variant == "chess960";
    og.sent             = move;
    og.sent_at          = og.state.moves.size();
    og.board_dirty      = true;
    const bool draw     = std::exchange(og.offer_draw, false);
    og.move_job = std::async(std::launch::async,
                             [token = lichess_account_->token, id = og.id, uci = chess::uci::moveToUci(move, chess960),
                              draw] { return lichess::boardMove(token, id, uci, draw); });
    showOnlineGame();
}

void App::onlineAction(const std::string& action) {
    if (!online_game_) return;
    const auto id   = online_game_->id;
    const auto what = action == "resign"             ? std::string(TR("Resigning"))
                      : action == "abort"            ? std::string(TR("Aborting the game"))
                      : action == "claim-victory"    ? std::string(TR("Claiming the victory"))
                      : action == "claim-draw"       ? std::string(TR("Claiming a draw"))
                      : action.rfind("draw", 0) == 0 ? std::string(TR("The draw"))
                                                     : std::string(TR("The takeback"));
    onlineCall(what, [id, action](const std::string& token) { return lichess::boardAction(token, id, action); });
}

void App::onlineResign() {
    if (!onlineShown()) return;
    auto& og         = *online_game_;
    const double now = ImGui::GetTime();
    if (now - og.resign_at <= RESIGN_SECONDS) {
        og.resign_at = -100;
        onlineAction("resign");
        return;
    }
    og.resign_at = now;
}

bool App::onlineGameKeys() {
    if (!onlineShown()) return false;
    auto& og       = *online_game_;
    const auto& io = ImGui::GetIO();
    if (io.WantTextInput) return false;
    const bool enter = !io.KeyCtrl && !io.KeyAlt && !io.KeyShift &&
                       (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false));
    const bool escape = ImGui::IsKeyPressed(ImGuiKey_Escape, false);
    if (og.held) {
        if (enter) {
            const auto m = *og.held;
            og.held.reset();
            sendOnlineMove(m);
        } else if (escape) {
            og.held.reset();
        }
    } else if (og.premove && escape) {
        og.premove.reset();
        boardToast(TR("The move made in advance is taken back"));
    } else if (enter && ImGui::GetTime() - og.resign_at <= RESIGN_SECONDS) {
        onlineResign();
    }
    boardMoveKeys();
    return true;
}

void App::finishOnlineGame() {
    auto& og = *online_game_;
    og.ended = true;
    og.sent.reset();
    og.premove.reset();
    og.held.reset();
    // The lock let go at once (the event stream may be cut meanwhile), the game brought in (#49): once, however its
    // end is heard.
    lichess::PlayingGame g;
    const auto it =
        std::find_if(lichess_playing_.begin(), lichess_playing_.end(), [&](const auto& p) { return p.id == og.id; });
    if (it != lichess_playing_.end()) {
        g = *it;
        lichess_playing_.erase(it);
    } else {
        const auto& them  = og.white ? og.state.black : og.state.white;
        g.id              = og.id;
        g.full_id         = og.id;
        g.white           = og.white;
        g.speed           = og.state.speed;
        g.rated           = og.state.rated;
        g.opponent        = them.name;
        g.opponent_rating = them.rating;
    }
    finishLichessGame(g);
    if (onlineSettings().playing == og.id) {
        onlineSettings().playing.clear();
        saveOnlineSettings();
    }
    og.board_dirty = true;
    if (og.shown) showOnlineGame();
}

void App::closeOnlineGame() {
    if (!online_game_) return;
    if (!online_game_->ended && !online_game_->state.over()) return;  // a game being played is not let go
    online_game_.reset();
}

std::optional<std::string> App::onlineStripClock(chess::Color side) const {
    if (!online_game_ || !online_game_->shown || !online_game_->state.full) return std::nullopt;
    const auto& s    = online_game_->state;
    const bool white = side == chess::Color::WHITE;
    const auto ms    = s.clockNow(white, SteadyClock::now());
    if (s.live()) return s.clock_ms >= 0 ? std::optional<std::string>(clockText(ms)) : std::nullopt;
    // By correspondence, the time left to the side to move only.
    if (s.over() || whiteToMove(s) != white) return std::string();
    return daysLeft(ms);
}

std::string App::onlineOpponent() const {
    if (!online_game_) return {};
    const auto& them = online_game_->white ? online_game_->state.black : online_game_->state.white;
    return them.rating > 0 ? fmt::format("{} ({})", them.name, them.rating) : them.name;
}

// ---------------------------------------------------------------------------
// The Play page's part

void App::drawPlayLichess() {
    const ui::TopicRegion topic(guide::topics::PlayLichess);
    if (!lichess_account_) {
        beginTinted("##online_no_account", ui::ACCENT);
        ImGui::PushTextWrapPos();
        heading(TR("Playing on Lichess needs your account linked"));
        wrapped(TR("RookStudio plays your games through Lichess's Board API, with your account. Everything else in "
                   "RookStudio works without it."),
                true);
        if (lichess_link_) {
            coloured(TR("Waiting for Lichess: allow RookStudio in the browser's page, then come back here."),
                     ui::ACCENT);
        } else {
            if (primaryButton(TR("Link the Lichess account…"))) startLichessLink();
            ImGui::SameLine();
            if (ImGui::Button(TR("The Lichess page ›"))) page_ = Page::Lichess;
        }
        ImGui::PopTextWrapPos();
        endTinted();
        return;
    }
    if (!onlineScope()) return drawOnlineScopeCard();
    pollLichessProfile();
    // The account and its ratings in the speeds played here, asked when they are ten minutes old.
    {
        const auto now   = SteadyClock::now();
        const bool stale = !lichess_profile_ || std::time(nullptr) - lichess_profile_->read_at > 600;
        if (stale && !lichess_profile_job_.valid() && !options_.scripted &&
            (lichess_profile_asked_.time_since_epoch().count() == 0 ||
             now - lichess_profile_asked_ > std::chrono::minutes(10))) {
            lichess_profile_asked_ = now;
            lichess_profile_job_   = std::async(
                std::launch::async, [token = lichess_account_->token] { return lichess::accountProfile(token); });
        }
        std::string line = lichess_account_->username;
        if (lichess_profile_)
            for (const auto& r : lichess_profile_->speeds)
                if ((r.key == "rapid" || r.key == "classical" || r.key == "correspondence") && r.games > 0)
                    line +=
                        " · " + trf("{} {}", speedName(r.key), std::to_string(r.rating) + (r.provisional ? "?" : ""));
        ImGui::TextDisabled("%s", line.c_str());
    }
    // Its parts (#95): the games (a seek, a challenge, those in progress), the tournaments; the one shown last kept.
    if (online_part_ < 0) online_part_ = std::clamp(onlineSettings().part, 0, 1);
    {
        const auto part = [&](const char* label, int which) {
            const auto size    = ImGui::CalcTextSize(label);
            const auto at      = ImGui::GetCursorScreenPos();
            const bool shown   = online_part_ == which;
            const bool clicked = ImGui::InvisibleButton(fmt::format("##online_part{}", which).c_str(),
                                                        ImVec2(size.x, ImGui::GetFrameHeight()));
            const bool hovered = ImGui::IsItemHovered();
            if (hovered && !shown) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            auto* dl      = ImGui::GetWindowDrawList();
            const float y = at.y + (ImGui::GetFrameHeight() - size.y) * 0.5f;
            dl->AddText(ImVec2(at.x, y), ImGui::GetColorU32(shown || hovered ? ImGuiCol_Text : ImGuiCol_TextDisabled),
                        label);
            if (shown)
                dl->AddLine(ImVec2(at.x, y + size.y + px(2)), ImVec2(at.x + size.x, y + size.y + px(2)), ui::ACCENT,
                            px(2));
            if (clicked && !shown) {
                online_part_          = which;
                onlineSettings().part = which;
                saveOnlineSettings();
            }
        };
        part(TR("Games"), 0);
        ImGui::SameLine(0, px(8));
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("·");
        ImGui::SameLine(0, px(8));
        part(TR("Tournaments"), 1);
        if (arena_play_ && arena_play_->seat.in() && online_part_ == 0) {
            ImGui::SameLine(0, px(10));
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::ACCENT), "%s", TR("· in an arena"));
        }
    }
    if (online_part_ == 1) {
        drawIncomingChallenges(false);
        drawPlayTournaments();
        return;
    }
    // Enter: a challenge received taken first, else the seek.
    const auto& io = ImGui::GetIO();
    bool enter     = !io.WantTextInput && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId) && !io.KeyCtrl &&
                     !io.KeyAlt &&
                     (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false));
    if (!io.WantTextInput && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId) &&
        ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        if (online_seek_)
            cancelSeek();
        else if (online_challenge_)
            cancelChallenge();
    }
    drawIncomingChallenges(false);
    if (enter && !online_incoming_.empty() && !liveGameOn()) {
        for (const auto& c : online_incoming_)
            if (c.board && (c.variant == "standard" || c.variant == "chess960" || c.variant == "fromPosition")) {
                answerChallenge(c.id, true);
                break;
            }
        enter = false;
    }
    // The game being played: back to it.
    if (online_game_ && !online_game_->ended && !online_game_->state.over()) {
        beginTinted("##online_going", ui::ACCENT);
        ImGui::PushTextWrapPos();
        wrapped(online_game_->shown
                    ? trf("Your game against {} is being played on the Board.", onlineOpponent())
                    : trf("Your game against {} goes on: it waits for you on the Board.", onlineOpponent()));
        if (primaryButton(TR("Back to it##online"))) backToOnlineGame();
        ImGui::PopTextWrapPos();
        endTinted();
        ImGui::Dummy(ImVec2(0, px(6)));
    }
    if (ImGui::BeginTable("##online_columns", 2, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("##seek", ImGuiTableColumnFlags_WidthStretch, 1.25f);
        ImGui::TableSetupColumn("##challenge", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        drawSeekCard(enter);
        ImGui::TableNextColumn();
        drawChallengeCard();
        ImGui::Dummy(ImVec2(0, px(4)));
        drawOnlineGamesList();
        ImGui::EndTable();
    }
    ImGui::PushTextWrapPos();
    if (!play_online_error_.empty()) coloured(play_online_error_, ui::BAD);
    wrapped(TR("What leaves the machine: your seeks and challenges, your moves, your chat messages, draws and "
               "resignations, with your account's token. Nothing else."),
            true);
    ImGui::PopTextWrapPos();
}

void App::drawOnlineScopeCard() {
    beginTinted("##online_scope", ui::ACCENT);
    ImGui::PushTextWrapPos();
    heading(TR("To play from RookStudio, Lichess asks for one more permission"));
    wrapped(
        TR("Playing your games (board:play): seeking and accepting games, playing your moves in them, offering a "
           "draw, resigning, writing in the chat. RookStudio never plays a move you did not play."));
    wrapped(TR("Lichess's page opens; your account stays linked meanwhile, and the token it replaces is revoked. The "
               "same permission serves the lock and the challenges received."),
            true);
    if (lichess_link_) {
        coloured(TR("Waiting for Lichess: allow RookStudio in the browser's page, then come back here."), ui::ACCENT);
    } else {
        if (lichess_error_.failed())
            coloured(trf("Lichess gave nothing ({}).", lichess::describe(lichess_error_)), ui::WARN);
        const auto& io = ImGui::GetIO();
        const bool enter =
            !io.WantTextInput && !io.KeyCtrl && !io.KeyAlt &&
            (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false));
        if (primaryButton(TR("Allow on Lichess…##board_play")) || enter) askOnlineScope();
        ImGui::SameLine();
        ImGui::TextDisabled("%s", TR("Enter"));
        ImGui::SameLine();
        if (ImGui::Button(TR("Not now##board_play"))) {
            onlineSettings().scope_declined = true;
            saveOnlineSettings();
            playSettings().part = 0;
            play_part_          = 0;
            savePlaySettings();
        }
    }
    ImGui::PopTextWrapPos();
    endTinted();
    ImGui::Dummy(ImVec2(0, px(6)));
    ui::beginCard("##online_rules");
    ImGui::PushTextWrapPos();
    heading(TR("What Lichess asks in return"));
    wrapped(TR("No help during a game. Live, RookStudio locks all its aids on every position; by correspondence, its "
               "engine, the cloud, the tables and the assistant on that game, the tree of your databases and your "
               "books staying open (Lichess allows a book there)."),
            true);
    wrapped(TR("Lichess's Board API is made for people: rapid, classical and correspondence games are sought from "
               "here, blitz is played by a direct challenge, bullet not at all."),
            true);
    ImGui::PopTextWrapPos();
    ui::endCard();
}

void App::drawSeekCard(bool& enter) {
    auto& s      = onlineSettings();
    bool changed = false;
    ui::beginCard("##online_seek");
    ImGui::PushTextWrapPos();
    heading(TR("Seek a game"));
    const auto chosen = s.seekClock();
    const float label = ImGui::GetCursorPosX() +
                        std::max({ImGui::CalcTextSize(TR("Rapid")).x, ImGui::CalcTextSize(TR("Classical")).x,
                                  ImGui::CalcTextSize(TR("Correspondence")).x}) +
                        px(14);
    ImGui::BeginDisabled(online_seek_ != nullptr);
    for (const auto& row : seekRows()) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", speedName(row.speed));
        ImGui::SameLine(label);
        bool first = true;
        for (const auto& clock : row.clocks) {
            // "1 day · 2 · 3 · 5 · 7 · 10 · 14 days", as the mockup writes them.
            const auto label_text = clock.days == 1 || clock.days == 14
                                        ? trn("{} day", "{} days", clock.days, clock.days)
                                    : clock.days > 0 ? std::to_string(clock.days)
                                                     : lichess::clockText(clock);
            if (!first) ui::sameLineOrWrap(ImGui::CalcTextSize(label_text.c_str()).x + px(24), px(4));
            first = false;
            if (chip(label_text.c_str(), clock == chosen,
                     fmt::format("seek{}_{}_{}", clock.minutes, clock.increment, clock.days).c_str())) {
                s.seek_minutes   = clock.minutes;
                s.seek_increment = clock.increment;
                s.seek_days      = clock.days;
                changed          = true;
            }
            if (clock.days > 0) tip(lichess::clockText(clock));
        }
    }
    ImGui::EndDisabled();
    wrapped(
        TR("Blitz and bullet are not sought from a program: Lichess keeps them for its site. Blitz stays possible as "
           "a direct challenge, beside."),
        true);
    ImGui::BeginDisabled(online_seek_ != nullptr);
    if (ImGui::Checkbox(TR("Rated##seek"), &s.seek_rated)) changed = true;
    // The opponents around the user's rating in that speed.
    ImGui::SameLine(0, px(16));
    std::optional<int> rating;
    if (lichess_profile_)
        for (const auto& r : lichess_profile_->speeds)
            if (r.key == lichess::speedOf(chosen) && r.games > 0 && !r.provisional) rating = r.rating;
    if (rating) {
        int range = s.seek_range;
        ImGui::SetNextItemWidth(px(120));
        const auto text = range > 0 ? trf("± {}", range) : std::string(TR("anyone"));
        if (ImGui::SliderInt("##seek_range", &range, 0, 500, text.c_str(), ImGuiSliderFlags_AlwaysClamp)) {
            s.seek_range = range / 50 * 50;
            changed      = true;
        }
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        if (s.seek_range > 0)
            ImGui::TextUnformatted(trf("Opponent rated from {} to {} (your rating ± {})",
                                       std::max(600, *rating - s.seek_range), *rating + s.seek_range, s.seek_range)
                                       .c_str());
        else
            ImGui::TextDisabled("%s", TR("Opponents of any rating: paired fastest"));
    } else {
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", TR("Opponents of any rating, your rating in this speed not being sure yet"));
    }
    ImGui::EndDisabled();
    // Seek: the event stream first, as Lichess asks (not to miss the game's start).
    const bool stream = lichess_events_ && lichess_events_->connected();
    const bool lock   = liveGameOn() != nullptr;
    const bool can    = !online_seek_ && stream && lichess::seekAllowed(chosen) && !lock &&
                        !(online_game_ && !online_game_->ended && online_game_->state.live());
    if (!online_seek_) {
        if (primaryButton(TR("Seek an opponent"), can) || (enter && can)) {
            enter = false;
            startSeek();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%s", TR("Enter"));
        if (!stream)
            wrapped(lichess_events_ && !lichess_events_->error().empty()
                        ? trf("Waiting for Lichess's event stream ({}): a seek goes once it is open.",
                              lichess_events_->error())
                        : std::string(TR("Waiting for Lichess's event stream: a seek goes once it is open.")),
                    true);
        else if (lock)
            wrapped(TR("One of your games is being played live: no seek meanwhile."), true);
    } else {
        // Seeking: since when, called back by Escape.
        beginTinted("##seeking", ui::ACCENT);
        const auto& ask  = online_seek_->ask();
        std::string what = kindText(ask.rated, lichess::speedOf(ask.clock), ask.clock);
        if (ask.rating_min > 0) what += " · " + fmt::format("{}–{}", ask.rating_min, ask.rating_max);
        const auto s_held =
            std::chrono::duration_cast<std::chrono::seconds>(SteadyClock::now() - online_seek_->since()).count();
        ImGui::TextUnformatted(TR("Seeking"));
        ImGui::SameLine();
        ImGui::TextDisabled("%s", fmt::format("· {} · {}:{:02}", what, s_held / 60, s_held % 60).c_str());
        if (ImGui::SmallButton(TR("Cancel · Esc##seek"))) cancelSeek();
        wrapped(TR("The seek lives while RookStudio holds it open; it stops if RookStudio closes. The game found opens "
                   "on the Board."),
                true);
        endTinted();
    }
    if (!online_seek_said_.empty()) wrapped(online_seek_said_, true);
    ImGui::PopTextWrapPos();
    ui::endCard();
    if (changed) saveOnlineSettings();
}

void App::drawChallengeCard() {
    auto& s      = onlineSettings();
    bool changed = false;
    ui::beginCard("##online_challenge");
    ImGui::PushTextWrapPos();
    heading(TR("Challenge a player"));
    const bool sending = online_challenge_ != nullptr;
    ImGui::BeginDisabled(sending);
    ImGui::TextDisabled("%s", TR("Their name on Lichess"));
    if (std::exchange(online_user_focus_, false)) ImGui::SetKeyboardFocusHere();
    ImGui::SetNextItemWidth(std::min(px(260), ImGui::GetContentRegionAvail().x));
    const bool typed_enter = ImGui::InputTextWithHint("##online_user", TR("a Lichess username"), online_user_.data(),
                                                      online_user_.size(), ImGuiInputTextFlags_EnterReturnsTrue);
    // The clock: blitz too, never bullet.
    const auto clock = s.challengeClock();
    bool first       = true;
    bool known       = false;
    for (const auto& k : challengeClocks()) {
        const auto text = k.days > 0 ? trn("{} day", "{} days", k.days, k.days) : lichess::clockText(k);
        if (!first) ui::sameLineOrWrap(ImGui::CalcTextSize(text.c_str()).x + px(24), px(4));
        first = false;
        known |= k == clock;
        if (chip(text.c_str(), k == clock, fmt::format("chal{}_{}_{}", k.minutes, k.increment, k.days).c_str())) {
            s.challenge_minutes   = k.minutes;
            s.challenge_increment = k.increment;
            s.challenge_days      = k.days;
            changed               = true;
        }
    }
    ui::sameLineOrWrap(ImGui::CalcTextSize(TR("other…")).x + px(24), px(4));
    if (chip(TR("other…"), !known, "chal_other")) ImGui::OpenPopup("##challenge_other");
    if (ImGui::BeginPopup("##challenge_other")) {
        ImGui::TextDisabled("%s", TR("Minutes and seconds added a move, or days a move"));
        int minutes = s.challenge_minutes, increment = s.challenge_increment, days = s.challenge_days;
        ImGui::SetNextItemWidth(px(160));
        if (ImGui::SliderInt(TR("minutes##chal"), &minutes, 0, 180)) changed = true;
        ImGui::SetNextItemWidth(px(160));
        if (ImGui::SliderInt(TR("seconds a move##chal"), &increment, 0, 60)) changed = true;
        for (const int d : {0, 1, 2, 3, 5, 7, 10, 14}) {
            if (d > 0) ImGui::SameLine();
            if (ImGui::RadioButton(d == 0 ? TR("a clock") : fmt::format("{}##days", d).c_str(), days == d)) {
                days    = d;
                changed = true;
            }
        }
        if (changed) {
            s.challenge_minutes   = minutes;
            s.challenge_increment = increment;
            s.challenge_days      = days;
        }
        ImGui::EndPopup();
    }
    const auto now_clock = s.challengeClock();
    if (ImGui::Checkbox(TR("Rated##challenge"), &s.challenge_rated)) changed = true;
    ImGui::SameLine(0, px(14));
    for (int c = 0; c < 3; ++c) {
        const char* name = c == 0 ? TR("White##chal_colour") : c == 1 ? TR("Black##chal_colour") : TR("At random");
        if (c > 0) ImGui::SameLine(0, px(4));
        if (chip(name, s.challenge_colour == c, fmt::format("chal_colour{}", c).c_str())) {
            s.challenge_colour = c;
            changed            = true;
        }
    }
    ImGui::EndDisabled();
    const std::string user = online_user_.data();
    const bool allowed     = lichess::challengeAllowed(now_clock);
    const bool can         = !sending && !user.empty() && allowed && lower(user) != lower(lichess_account_->username);
    if (!allowed)
        coloured(lichess::validClock(now_clock, false)
                     ? std::string(TR("Bullet is not played through the Board API: three minutes at least, the "
                                      "increment counted forty times."))
                     : std::string(TR("Lichess does not take this clock.")),
                 ui::WARN);
    if (!sending) {
        if (ImGui::Button(TR("Send the challenge")) || (typed_enter && can)) {
            if (can) {
                lichess::ChallengeAsk ask;
                ask.user   = user;
                ask.clock  = now_clock;
                ask.rated  = s.challenge_rated;
                ask.colour = s.challenge_colour == 0 ? "white" : s.challenge_colour == 1 ? "black" : "random";
                s.opponent = user;
                changed    = true;
                sendChallenge(ask);
            }
        }
        if (!can && !user.empty() && allowed && lower(user) == lower(lichess_account_->username))
            ImGui::TextDisabled("%s", TR("That is you."));
    } else {
        // Sent: it waits for the player's answer while RookStudio holds it; Escape calls it back.
        const auto& ask = online_challenge_->ask();
        beginTinted("##challenge_sent", ui::ACCENT);
        ImGui::PushTextWrapPos();
        const char* colour = ask.colour == "white"   ? TR("you: White")
                             : ask.colour == "black" ? TR("you: Black")
                                                     : TR("colour at random");
        ImGui::TextUnformatted(trf("Challenge sent to {}", ask.user).c_str());
        wrapped(fmt::format("{} · {}", kindText(ask.rated, lichess::speedOf(ask.clock), ask.clock), colour), true);
        wrapped(ask.clock.days > 0 ? std::string(TR("It waits for their answer on Lichess; the game opens on the Board "
                                                    "when they accept."))
                                   : std::string(TR("It waits for their answer while you stay; Esc calls it back.")),
                true);
        if (ImGui::SmallButton(TR("Call it back"))) cancelChallenge();
        ImGui::PopTextWrapPos();
        endTinted();
    }
    // Declined: Lichess's reason, and another clock a click away.
    if (!sending && online_declined_ && !online_challenge_said_.empty()) {
        const auto& d   = *online_declined_;
        const auto& who = d.dest.empty() ? online_challenge_said_ : d.dest;
        coloured(trf("{} declines: {}", who, declinedText(d)), ui::WARN);
        const auto others = otherClocks(d, d.clock);
        if (!others.empty() && d.decline_key != "rated" && d.decline_key != "casual") {
            ImGui::TextDisabled("%s", TR("Another clock, sent at once:"));
            for (const auto& k : others) {
                const auto text = k.days > 0 ? trn("{} day", "{} days", k.days, k.days) : lichess::clockText(k);
                ImGui::SameLine(0, px(4));
                if (chip(text.c_str(), false, fmt::format("again{}_{}_{}", k.minutes, k.increment, k.days).c_str())) {
                    lichess::ChallengeAsk ask;
                    ask.user   = who;
                    ask.clock  = k;
                    ask.rated  = s.challenge_rated;
                    ask.colour = s.challenge_colour == 0 ? "white" : s.challenge_colour == 1 ? "black" : "random";
                    s.challenge_minutes   = k.minutes;
                    s.challenge_increment = k.increment;
                    s.challenge_days      = k.days;
                    changed               = true;
                    sendChallenge(ask);
                }
            }
        } else if (d.decline_key == "rated" || d.decline_key == "casual") {
            if (ImGui::SmallButton(d.decline_key == "rated" ? TR("Send it rated") : TR("Send it casual"))) {
                lichess::ChallengeAsk ask;
                ask.user          = who;
                ask.clock         = d.clock;
                ask.rated         = d.decline_key == "rated";
                ask.colour        = s.challenge_colour == 0 ? "white" : s.challenge_colour == 1 ? "black" : "random";
                s.challenge_rated = ask.rated;
                changed           = true;
                sendChallenge(ask);
            }
        }
    } else if (!sending && !online_challenge_said_.empty() && !online_declined_) {
        wrapped(online_challenge_said_, true);
    }
    ImGui::PopTextWrapPos();
    ui::endCard();
    if (changed) saveOnlineSettings();
}

void App::drawIncomingChallenges(bool compact) {
    const bool live = liveGameOn() != nullptr;
    for (std::size_t i = 0; i < online_incoming_.size(); ++i) {
        const auto c = online_incoming_[i];  // a copy: an answer takes it out of the list
        ImGui::PushID(c.id.c_str());
        const bool playable =
            c.board && (c.variant == "standard" || c.variant == "chess960" || c.variant == "fromPosition");
        const auto name  = c.challenger_rating > 0
                               ? fmt::format("{}{} ({}{})", c.challenger_title.empty() ? "" : c.challenger_title + " ",
                                             c.challenger, c.challenger_rating, c.challenger_provisional ? "?" : "")
                               : c.challenger;
        const auto white = c.userWhite();
        const char* side = !white ? TR("colour at random") : *white ? TR("you: White") : TR("you: Black");
        const auto what  = fmt::format("{} · {}", kindText(c.rated, c.speed, c.clock, c.unlimited), side);
        if (compact) {
            ui::inlineIcon(ui::Icon::Swords, ui::ACCENT);
            ImGui::SameLine();
            ImGui::TextUnformatted(trf("{} challenges you", name).c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("%s", what.c_str());
            ImGui::SameLine();
        } else {
            beginTinted("##incoming", ui::ACCENT);
            ui::inlineIcon(ui::Icon::Swords, ui::ACCENT);
            ImGui::SameLine();
            heading(trf("{} challenges you", name).c_str());
            ImGui::TextDisabled("%s", what.c_str());
        }
        if (!playable) {
            if (!compact)
                wrapped(c.variant != "standard" && c.variant != "chess960" && c.variant != "fromPosition"
                            ? std::string(TR("RookStudio plays standard chess and Chess960 only: this one is played on "
                                             "lichess.org."))
                            : std::string(TR("Lichess's Board API cannot play it (bullet): it is played on "
                                             "lichess.org.")),
                        true);
            if (ImGui::SmallButton(TR("Open on lichess.org")))
                SDL_OpenURL(fmt::format("https://lichess.org/{}", c.id).c_str());
            ImGui::SameLine();
        } else {
            ImGui::BeginDisabled(live);
            const bool accept = compact ? ImGui::SmallButton(TR("Accept")) : primaryButton(TR("Accept · Enter"));
            ImGui::EndDisabled();
            if (live) tip(TR("Once your live game is over."));
            if (accept) answerChallenge(c.id, true);
            ImGui::SameLine();
        }
        if (compact ? ImGui::SmallButton(TR("Decline ▾")) : ImGui::Button(TR("Decline ▾")))
            ImGui::OpenPopup("##decline");
        if (ImGui::BeginPopup("##decline")) {
            for (const auto& r : REASONS)
                if (ImGui::MenuItem(TR(r.label))) answerChallenge(c.id, false, r.key);
            ImGui::EndPopup();
        }
        if (compact) {
            ImGui::NewLine();
        } else {
            endTinted();
            ImGui::Dummy(ImVec2(0, px(4)));
        }
        ImGui::PopID();
    }
}

void App::drawOnlineGamesList() {
    ui::beginCard("##online_games");
    ImGui::PushTextWrapPos();
    heading(TR("Your games in progress"));
    if (lichess_playing_.empty()) {
        wrapped(TR("None now. A correspondence game waits here for your move; a click opens it on the Board."), true);
    } else {
        // Your turn first, then theirs, then the live games.
        const auto rank = [](const lichess::PlayingGame& g) { return g.live() ? 2 : g.my_turn ? 0 : 1; };
        std::vector<const lichess::PlayingGame*> games;
        for (const auto& g : lichess_playing_) games.push_back(&g);
        std::stable_sort(games.begin(), games.end(), [&](const auto* a, const auto* b) { return rank(*a) < rank(*b); });
        for (const auto* p : games) {
            const auto g = *p;  // a copy: opening it changes the list
            ImGui::PushID(g.id.c_str());
            tag(g.live()    ? TR("Live game")
                : g.my_turn ? TR("Your turn")
                            : TR("Their turn"),
                g.live()    ? ui::ACCENT
                : g.my_turn ? ui::WARN
                            : 0);
            ImGui::SameLine();
            const auto who = g.opponent_rating > 0 ? fmt::format("{} ({})", g.opponent, g.opponent_rating) : g.opponent;
            const bool here = lichess::playableHere(g);
            if (ImGui::Selectable(fmt::format("{} · {}##game", who, speedName(g.speed)).c_str(), false,
                                  ImGuiSelectableFlags_None,
                                  ImVec2(std::max(px(80), ImGui::GetContentRegionAvail().x - px(110)), 0))) {
                if (here)
                    openOnlineGame(g.id, g.white);
                else
                    SDL_OpenURL(fmt::format("https://lichess.org/{}", g.full_id).c_str());
            }
            tip(here ? std::string(TR("On the Board: your moves are played there."))
                     : std::string(TR("Lichess's Board API cannot play it: lichess.org opens.")));
            if (g.seconds_left >= 0 && (g.live() || g.my_turn)) {
                ImGui::SameLine();
                ImGui::TextDisabled("%s", g.live() ? clockText(g.seconds_left * 1000).c_str()
                                                   : daysLeft(g.seconds_left * 1000).c_str());
            }
            ImGui::PopID();
        }
        wrapped(TR("A click opens it on the Board; your move is played there."), true);
    }
    ImGui::PopTextWrapPos();
    ui::endCard();
}

// ---------------------------------------------------------------------------
// The game's card, on the Board page

void App::drawOnlineGameCard() {
    auto& og = *online_game_;
    if (og.ended || og.state.over()) return drawOnlineGameEnd();
    const ui::TopicRegion topic(guide::topics::LichessGame);
    const auto& s = og.state;
    beginTinted("##online_game", ui::ACCENT);
    ImGui::PushTextWrapPos();
    if (!og.shown) {
        wrapped(trf("Your game against {} continues on Lichess.", onlineOpponent()));
        ImGui::PushStyleColor(ImGuiCol_Text, ui::WARN);
        wrapped(s.live() ? std::string(TR("Your clock runs when it is your move."))
                         : std::string(TR("By correspondence: it waits for your move.")));
        ImGui::PopStyleColor();
        if (primaryButton(TR("Back to the game##online_game"))) backToOnlineGame();
        ImGui::PopTextWrapPos();
        endTinted();
        return;
    }
    ImGui::PushFont(ui::titleFont(), ImGui::GetStyle().FontSizeBase * 1.1f);
    if (s.full) {
        ChallengeClock clock;
        if (s.clock_ms >= 0) clock = {static_cast<int>(s.clock_ms / 60000), static_cast<int>(s.increment_ms / 1000), 0};
        if (s.days > 0) clock = {0, 0, s.days};
        wrapped(
            trf("{} against {}", kindText(s.rated, s.speed, clock, s.clock_ms < 0 && s.days == 0), onlineOpponent()));
    } else {
        wrapped(TR("Your game on Lichess"));
    }
    ImGui::PopFont();
    // A tournament's game (#95): the arena's line (rank, points, the streak, the time left, Pause); a Swiss's round.
    const bool arena = arena_play_ && !s.tournament.empty() && s.tournament == arena_play_->id;
    if (arena) drawArenaLine(true);
    if (!s.swiss.empty() && swiss_play_ && s.swiss == swiss_play_->id && swiss_play_->info.value)
        ImGui::TextDisabled("%s", trf("{} · round {} of {}", swiss_play_->name, swiss_play_->info.value->round,
                                      swiss_play_->info.value->rounds)
                                      .c_str());
    // The connection cut: the clock runs on Lichess meanwhile; tried again by itself.
    if (!s.connected) {
        ImGui::PopTextWrapPos();
        endTinted();
        beginTinted("##online_cut", ui::WARN);
        ImGui::PushTextWrapPos();
        ui::inlineIcon(ui::Icon::Alert, ui::WARN);
        ImGui::SameLine();
        ImGui::BeginGroup();
        if (!s.full && s.tries == 0) {
            coloured(TR("Connecting to Lichess…"), ui::ACCENT);
        } else {
            coloured(s.live() ? std::string(TR("The connection to Lichess is cut: your clock runs on Lichess"))
                              : std::string(TR("The connection to Lichess is cut")),
                     ui::WARN);
            const auto left = std::max<int64_t>(
                0, std::chrono::duration_cast<std::chrono::seconds>(s.retry_at - SteadyClock::now()).count() + 1);
            wrapped(trf("Trying again in {} s (try {}). Your moves made meanwhile do not go: play them again when it "
                        "comes back.",
                        left, s.tries + 1),
                    true);
            // Why, when Lichess said something: no answer is what the line above says.
            if (s.failure.failed() && s.failure.net != lichess::NetError::Offline)
                ImGui::TextDisabled("%s", lichess::describe(s.failure).c_str());
        }
        if (ImGui::SmallButton(TR("Open the game on lichess.org")))
            SDL_OpenURL(fmt::format("https://lichess.org/{}", og.id).c_str());
        ImGui::EndGroup();
        ImGui::PopTextWrapPos();
        endTinted();
        beginTinted("##online_game_rest", ui::ACCENT);
        ImGui::PushTextWrapPos();
    }
    const bool mine      = onlineMyTurn();
    const auto& them     = og.white ? s.black : s.white;
    const bool they_draw = og.white ? s.black_draw : s.white_draw, i_draw = og.white ? s.white_draw : s.black_draw;
    const bool they_back = og.white ? s.black_takeback : s.white_takeback,
               i_back    = og.white ? s.white_takeback : s.black_takeback;
    if (og.sent)
        ImGui::TextDisabled("%s", TR("Your move goes to Lichess…"));
    else if (mine)
        coloured(TR("Your move"), ui::ACCENT);
    else if (s.full)
        ImGui::TextDisabled("%s", trf("{} to move", them.name).c_str());
    // The first moves, before Lichess aborts the game.
    if (s.first_move_ms >= 0 && s.moves.size() < 2 && mine) {
        const auto left = std::max<int64_t>(
            0, (s.first_move_ms -
                std::chrono::duration_cast<std::chrono::milliseconds>(SteadyClock::now() - s.told).count()) /
                   1000);
        // A tournament's game is lost, not aborted (#95).
        coloured(lichess::tournamentGame(s)
                     ? trf("Play your first move within {} s, or the game is lost.", left)
                     : trf("Play your first move within {} s, or Lichess aborts the game.", left),
                 ui::WARN);
    }
    // What the opponent offers, what the user offered.
    if (they_draw) {
        coloured(trf("{} offers a draw", them.name), ui::ACCENT);
        ImGui::SameLine();
        if (ImGui::SmallButton(TR("Accept##draw"))) onlineAction("draw/yes");
        ImGui::SameLine();
        if (ImGui::SmallButton(TR("Decline##draw"))) onlineAction("draw/no");
    } else if (i_draw || og.offer_draw) {
        ImGui::TextDisabled("%s", og.offer_draw && !i_draw ? TR("Your draw offer goes with your next move.")
                                                           : TR("Your draw offer waits for their answer."));
    }
    if (they_back) {
        coloured(trf("{} asks to take back a move", them.name), ui::ACCENT);
        ImGui::SameLine();
        if (ImGui::SmallButton(TR("Accept##takeback"))) onlineAction("takeback/yes");
        ImGui::SameLine();
        if (ImGui::SmallButton(TR("Decline##takeback"))) onlineAction("takeback/no");
    } else if (i_back) {
        ImGui::TextDisabled("%s", TR("Your takeback request waits for their answer."));
    }
    // The opponent gone: a win, or a draw, claimed when Lichess allows it.
    if (s.opponent_gone) {
        const auto waited = std::chrono::duration_cast<std::chrono::seconds>(SteadyClock::now() - s.gone_told).count();
        const auto left   = s.claim_in_s < 0 ? int64_t{-1} : std::max<int64_t>(0, s.claim_in_s - waited);
        if (left > 0) {
            coloured(trf("{} left the game: you may claim the victory in {} s.", them.name, left), ui::WARN);
        } else {
            coloured(trf("{} left the game.", them.name), ui::WARN);
            if (primaryButton(TR("Claim the victory"))) onlineAction("claim-victory");
            ImGui::SameLine();
            if (ImGui::Button(TR("Claim a draw"))) onlineAction("claim-draw");
        }
    }
    // By correspondence, the move waits for Enter.
    if (og.held) {
        auto board     = boardAt(board_.game, board_.chess960, static_cast<int>(board_.game.moves.size()));
        const auto san = chess::uci::moveToSan(board, *og.held);
        beginTinted("##online_held", ui::ACCENT);
        const int number = board.fullMoveNumber();
        ImGui::TextUnformatted(
            trf("Your move: {}",
                fmt::format("{}{}{}", number, board.sideToMove() == chess::Color::WHITE ? "." : "…", boardSan(san)))
                .c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("%s", TR("· not sent yet"));
        if (primaryButton(TR("Send · Enter"))) {
            const auto m = *og.held;
            og.held.reset();
            sendOnlineMove(m);
        }
        ImGui::SameLine();
        if (ImGui::Button(TR("Take it back · Esc"))) og.held.reset();
        endTinted();
    }
    if (og.premove) {
        ImGui::TextDisabled("%s", TR("A move made in advance waits for your turn (the dashed arrow): played then if it "
                                     "is legal. Esc takes it back."));
    }
    // Berserk in an arena that allows it (#95): before the user's first move only.
    if ((og.white ? s.black_berserk : s.white_berserk) && arena)
        ImGui::TextDisabled("%s", trf("{} went berserk: half their clock.", them.name).c_str());
    if (arena && arena_play_->state.value && arena_play_->state.value->info.berserkable) {
        const bool done   = og.white ? s.white_berserk : s.black_berserk;
        const bool can    = lichess::berserkAllowed(true, s, og.white);
        const char* label = done  ? TR("Berserk: half your clock, a point more if you win")
                            : can ? TR("Berserk")
                                  : TR("Berserk · no longer possible after your first move");
        ImGui::BeginDisabled(!can);
        if (ImGui::Button(fmt::format("{}###online_berserk", label).c_str()))
            onlineCall(TR("Berserk"), [id = og.id](const std::string& token) { return lichess::berserk(token, id); });
        ImGui::EndDisabled();
        tip(TR("Half your clock, a point more if you win: before your first move only."));
        ImGui::SameLine();
    }
    // What can be done.
    ImGui::BeginDisabled(i_draw || og.offer_draw || !s.full);
    if (ImGui::Button(TR("Offer a draw##online"))) {
        if (mine && s.live())
            og.offer_draw = true;  // with the next move, as Lichess allows
        else
            onlineAction("draw/yes");
    }
    ImGui::EndDisabled();
    tip(TR("Lichess tells your opponent, who takes it or not."));
    const bool armed   = ImGui::GetTime() - og.resign_at <= RESIGN_SECONDS;
    const char* resign = armed ? TR("Resign? Enter, or click again") : TR("Resign…");
    ui::sameLineOrWrap(resign);
    if (armed) ImGui::PushStyleColor(ImGuiCol_Text, ui::WARN);
    if (ImGui::Button(fmt::format("{}###online_resign", resign).c_str())) onlineResign();
    if (armed) ImGui::PopStyleColor();
    // A tournament's game takes no takeback and cannot be aborted: Lichess's rule (#95).
    const bool tournament = lichess::tournamentGame(s);
    if (!tournament) {
        ui::sameLineOrWrap(TR("Ask for a takeback"));
        ImGui::BeginDisabled(i_back || s.moves.empty());
        if (ImGui::Button(TR("Ask for a takeback"))) onlineAction("takeback/yes");
        ImGui::EndDisabled();
        tip(TR("Your opponent takes it or not, as their Lichess preferences allow."));
    }
    if (s.moves.size() < 2 && !tournament) {
        ui::sameLineOrWrap(TR("Abort"));
        if (ImGui::Button(TR("Abort"))) onlineAction("abort");
        tip(TR("Before both sides have moved, the game ends without a result."));
    }
    const auto chat_label =
        s.chat.empty() ? std::string(TR("Chat")) : fmt::format("{} · {}", TR("Chat"), s.chat.size());
    ui::sameLineOrWrap(chat_label.c_str());
    if (ImGui::Button(fmt::format("{}###online_chat", chat_label).c_str())) og.chat_open = !og.chat_open;
    if (og.chat_open) drawOnlineChat();
    wrapped(s.live() ? std::string(TR("Your moves go as soon as they are played, on the board or typed (Nf3, Enter); a "
                                      "move made during your opponent's turn waits for yours. If your opponent "
                                      "leaves, “Claim the victory” comes when Lichess allows it."))
                     : std::string(TR("By correspondence, a move waits for your Enter; live, it goes at once.")),
            true);
    // By correspondence, the user's other games, a click away.
    if (!s.live()) {
        std::vector<lichess::PlayingGame> others;
        for (const auto& g : lichess_playing_)
            if (!g.live() && g.id != og.id) others.push_back(g);
        std::stable_sort(others.begin(), others.end(),
                         [](const auto& a, const auto& b) { return a.my_turn > b.my_turn; });
        if (!others.empty()) {
            ImGui::TextDisabled("%s", TR("Your other games:"));
            for (const auto& g : others) {
                const auto label = g.my_turn && g.seconds_left >= 0
                                       ? trf("{} (your turn, {})", g.opponent, daysLeft(g.seconds_left * 1000))
                                   : g.my_turn ? trf("{} (your turn)", g.opponent)
                                               : trf("{} (their turn)", g.opponent);
                ui::sameLineOrWrap(label.c_str());
                if (ImGui::SmallButton(fmt::format("{}##other_{}", label, g.id).c_str())) {
                    if (lichess::playableHere(g)) openOnlineGame(g.id, g.white);
                    ImGui::PopTextWrapPos();
                    endTinted();
                    return;
                }
            }
        }
    }
    if (ImGui::SmallButton(TR("Open on lichess.org##online")))
        SDL_OpenURL(fmt::format("https://lichess.org/{}", og.id).c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("%s", TR("Closing RookStudio does not end the game: it comes back at the next launch."));
    ImGui::PopTextWrapPos();
    endTinted();
    // The arena's game before, at its end (#95).
    if (arena && !arena_play_->last_game.empty() && arena_play_->last_game != og.id) drawArenaGameBefore();
}

void App::drawOnlineChat() {
    auto& og          = *online_game_;
    const auto& lines = og.state.chat;
    ImGui::BeginChild("##online_chat_lines", ImVec2(0, px(110)), ImGuiChildFlags_Borders);
    for (const auto& l : lines) {
        if (l.room != "player") continue;
        ImGui::TextDisabled("%s", l.user.c_str());
        ImGui::SameLine();
        ImGui::TextWrapped("%s", l.text.c_str());
    }
    if (og.chat_seen != lines.size()) {
        ImGui::SetScrollHereY(1.0f);
        og.chat_seen = lines.size();
    }
    ImGui::EndChild();
    ImGui::SetNextItemWidth(std::max(px(120), ImGui::GetContentRegionAvail().x - px(70)));
    const bool enter =
        ImGui::InputTextWithHint("##online_chat_text", TR("A message to your opponent"), og.chat_text.data(),
                                 og.chat_text.size(), ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if ((ImGui::Button(TR("Send##chat")) || enter) && og.chat_text[0] != '\0') {
        const std::string text = og.chat_text.data();
        og.chat_text[0]        = '\0';
        onlineCall(TR("The chat"),
                   [id = og.id, text](const std::string& token) { return lichess::boardChat(token, id, text); });
    }
}

void App::drawOnlineGameEnd() {
    auto& og      = *online_game_;
    const auto& s = og.state;
    const ui::TopicRegion topic(guide::topics::LichessGame);
    // Its stream stopped before its end: Lichess does not know the game any more, or refused the token.
    if (!s.over()) {
        beginTinted("##online_lost", ui::WARN);
        ImGui::PushTextWrapPos();
        coloured(trf("Lichess no longer answers for your game against {}: {}.", onlineOpponent(),
                     lichess::describe(s.failure)),
                 ui::WARN);
        if (ImGui::SmallButton(TR("Open the game on lichess.org##lost")))
            SDL_OpenURL(fmt::format("https://lichess.org/{}", og.id).c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton(TR("Close##online_lost"))) {
            online_game_.reset();
            ImGui::PopTextWrapPos();
            endTinted();
            return;
        }
        ImGui::PopTextWrapPos();
        endTinted();
        return;
    }
    const auto& them = og.white ? s.black : s.white;
    const bool won   = !s.winner.empty() && (s.winner == "white") == og.white;
    const bool lost  = !s.winner.empty() && !won;
    // An arena's game (#95): its line, the game before at its end; the next one comes by itself.
    if (arena_play_ && !s.tournament.empty() && s.tournament == arena_play_->id) {
        drawArenaLine(true);
        drawArenaGameBefore();
        if (arena_play_ && arena_play_->seat.isOver())
            drawArenaOver(false);  // its end: rank, points, the games and their review
        else if (arena_play_ && !arena_play_->seat.in() && ImGui::SmallButton(TR("Close##arena_game")))
            closeOnlineGame();
        return;
    }
    std::string title;
    const auto& st = s.status;
    if (st == "aborted")
        title = TR("Game aborted");
    else if (st == "noStart" && won)  // a tournament's game, lost by the one who did not start it (#95)
        title = trf("You won: {} did not play their first move in time", them.name);
    else if (st == "noStart" && lost)
        title = TR("You lost: your first move did not come in time");
    else if (st == "noStart")
        title = TR("Game aborted: a first move did not come in time");
    else if (st == "mate")
        title = won ? TR("You won by checkmate") : TR("Checkmate: you lost");
    else if (st == "resign")
        title = won ? trf("You won: {} resigned", them.name) : std::string(TR("You resigned"));
    else if (st == "outoftime")
        title = won    ? TR("You won on time")
                : lost ? TR("You lost on time")
                       : TR("Draw: time out against insufficient material");
    else if (st == "timeout")
        title = won    ? trf("You won: {} left the game", them.name)
                : lost ? TR("You lost: you left the game")
                       : TR("Draw: a player left the game");
    else if (st == "stalemate")
        title = TR("Draw by stalemate");
    else if (st == "insufficientMaterialClaim")
        title = TR("Draw: insufficient material");
    else if (st == "draw")
        title = TR("Draw");
    else
        title = won ? TR("You won") : lost ? TR("You lost") : TR("Game over");
    beginTinted("##online_end", ui::ACCENT);
    ImGui::PushTextWrapPos();
    ImGui::PushFont(ui::titleFont(), ImGui::GetStyle().FontSizeBase * 1.25f);
    wrapped(title);
    ImGui::PopFont();
    std::string sub = trf("Against {}", onlineOpponent());
    if (og.rating_known && s.rated && st != "aborted" && st != "noStart")
        sub += " · " + trf("your rating {}{}", og.rating_diff >= 0 ? "+" : "", og.rating_diff);
    wrapped(sub, true);
    // A tournament's game (#95): back to it; else another game, the same seek.
    const auto clock = onlineSettings().seekClock();
    if (lichess::tournamentGame(s)) {
        if (primaryButton(TR("Back to the tournament##online_end"))) {
            if (swiss_play_ && s.swiss == swiss_play_->id && swiss_play_->info.value)
                chooseSwiss(*swiss_play_->info.value);
            else if (arena_play_)
                chooseArena(arena_play_->id, arena_play_->name);
            else
                page_ = Page::Play;
            ImGui::PopTextWrapPos();
            endTinted();
            return;
        }
        ImGui::SameLine();
    } else if (lichess::seekAllowed(clock) && clock.days == 0) {
        if (primaryButton(trf("Another game · {}", lichess::clockText(clock)).c_str(),
                          !online_seek_ && !liveGameOn())) {
            closeOnlineGame();
            playSettings().part = 1;
            play_part_          = 1;
            page_               = Page::Play;
            startSeek();
            ImGui::PopTextWrapPos();
            endTinted();
            return;
        }
        ImGui::SameLine();
    }
    if (ImGui::Button(TR("The Play page"))) {
        play_part_ = 1;
        page_      = Page::Play;
    }
    ImGui::SameLine();
    if (ImGui::Button(TR("Close##online_end"))) {
        closeOnlineGame();
        ImGui::PopTextWrapPos();
        endTinted();
        return;
    }
    tip(TR("The game stays on the board as any game; it comes into your Lichess database as your finished games do."));
    ImGui::PopTextWrapPos();
    endTinted();
}

// ---------------------------------------------------------------------------
// Elsewhere: the Home page, the window's corner, closing

void App::drawOnlineHome() {
    // The game being played here, first (R3).
    if (online_game_ && !online_game_->ended && !online_game_->state.over()) {
        ui::inlineIcon(ui::Icon::Lock, ui::ACCENT);
        ImGui::SameLine();
        ImGui::TextUnformatted(trf("Your game against {} on Lichess", onlineOpponent()).c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton(TR("Back to the game##home"))) backToOnlineGame();
    }
    if (!online_incoming_.empty()) drawIncomingChallenges(true);
}

void App::drawOnlineNotice() {
    if (online_incoming_.empty() || liveGameOn()) return;  // during a live game, it waits for its end
    if (page_ == Page::ChessHome || (page_ == Page::Play && play_part_ == 1)) {
        for (const auto& c : online_incoming_)  // seen there: not again at the corner
            if (std::find(online_noticed_.begin(), online_noticed_.end(), c.id) == online_noticed_.end())
                online_noticed_.push_back(c.id);
        return;
    }
    const lichess::Challenge* shown = nullptr;
    for (const auto& c : online_incoming_)
        if (std::find(online_noticed_.begin(), online_noticed_.end(), c.id) == online_noticed_.end()) shown = &c;
    if (!shown) return;
    const auto c         = *shown;
    const auto* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - px(18),
                                   viewport->WorkPos.y + viewport->WorkSize.y - px(18)),
                            ImGuiCond_Always, ImVec2(1.0f, 1.0f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(px(300), 0), ImVec2(px(420), FLT_MAX));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::ColorConvertU32ToFloat4(mix(ui::CARD_BG, ui::ACCENT, 0.10f)));
    ImGui::PushStyleColor(ImGuiCol_Border, ImGui::ColorConvertU32ToFloat4(ui::ACCENT));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(14), px(12)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, px(10));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    if (ImGui::Begin("##challenge_notice", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                         ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDocking)) {
        const ui::TopicRegion topic(guide::topics::PlayLichess);
        ImGui::PushTextWrapPos(px(400));
        ui::inlineIcon(ui::Icon::Swords, ui::ACCENT);
        ImGui::SameLine();
        const auto name =
            c.challenger_rating > 0 ? fmt::format("{} ({})", c.challenger, c.challenger_rating) : c.challenger;
        heading(trf("{} challenges you on Lichess", name).c_str());
        const auto white = c.userWhite();
        ImGui::TextDisabled("%s", fmt::format("{} · {}", kindText(c.rated, c.speed, c.clock, c.unlimited),
                                              !white   ? TR("colour at random")
                                              : *white ? TR("you: White")
                                                       : TR("you: Black"))
                                      .c_str());
        const bool playable =
            c.board && (c.variant == "standard" || c.variant == "chess960" || c.variant == "fromPosition");
        if (playable && primaryButton(TR("Accept##notice"))) answerChallenge(c.id, true);
        if (playable) ImGui::SameLine();
        if (ImGui::Button(TR("Decline##notice"))) answerChallenge(c.id, false, "generic");
        ImGui::SameLine();
        if (ImGui::Button(TR("See it"))) {
            play_part_          = 1;
            playSettings().part = 1;
            page_               = Page::Play;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton(TR("Later"))) online_noticed_.push_back(c.id);
        tip(TR("It stays on the Play page and the Home page until it is answered or called back."));
        ImGui::PopTextWrapPos();
    }
    ImGui::End();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);
}

void App::drawQuitOnline() {
    if (quit_online_open_) {
        ImGui::OpenPopup(TR("Close RookStudio?###quit_online"));
        quit_online_open_ = false;
    }
    const auto* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal(TR("Close RookStudio?###quit_online"), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return;
    {
        const ui::TopicRegion topic(guide::topics::LichessGame);  // ended before the popup is
        ImGui::PushTextWrapPos(px(420));
        if (online_game_ && !online_game_->ended && !online_game_->state.over()) {
            const auto& s   = online_game_->state;
            const auto mine = s.clockNow(online_game_->white, SteadyClock::now());
            ImGui::TextUnformatted(trf("Your game against {} continues on Lichess: your clock runs ({}).",
                                       onlineOpponent(), clockText(mine))
                                       .c_str());
            ImGui::TextDisabled("%s", TR("At the next launch, a game still being played comes back on the Board."));
        }
        // In an arena from here (#95): closed, RookStudio joins no more.
        if (arenaHoldsAids())
            ImGui::TextUnformatted(trf("You are in the arena “{}”: closed, RookStudio no longer joins you again and "
                                       "Lichess pairs you no more; your points stay.",
                                       arena_play_->name)
                                       .c_str());
        ImGui::PopTextWrapPos();
    }
    ImGui::Spacing();
    if (primaryButton(TR("Stay · Esc")) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
    ImGui::SameLine();
    if (arenaHoldsAids()) {
        if (ImGui::Button(TR("Pause and close"))) {
            pauseArena();  // RookStudio closes once Lichess took the pause, or after a few seconds
            arena_play_->quit_after = true;
            arena_play_->quit_asked = ImGui::GetTime();
            ImGui::CloseCurrentPopup();
        }
    } else if (ImGui::Button(TR("Close anyway"))) {
        quit_online_said_ = true;
        ImGui::CloseCurrentPopup();
        requestQuit();
    }
    ImGui::EndPopup();
}

}  // namespace fcg
