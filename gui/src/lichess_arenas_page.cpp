// Lichess's arenas and Swiss tournaments (#95, mockup M13, board 8), their module out of App (#107; lichess_arenas.hpp
// is their part of the network): the Lichess page's card "Lichess tournaments" (the arenas in progress and to come,
// read without an account, Rapid / Classical / All; each line Follow, and Join or why not; the user's teams' Swiss
// tournaments, team:read asked once), a tournament followed in the mosaic (a fourth source: its standing every 15 s
// with its players' sheets, the games of its first eight, three moves late, eight at most, never the user's own), the
// Play page's "On Lichess › Tournaments" (tournament:write asked once; the conditions as Lichess judges them, with the
// user's figures; the rules; joined again each minute while the user waits; Pause, Alt+P), the arena's line and the
// game before on the Board, the lock held between two games while the user is in the arena (point 18), a Swiss's rounds
// and their notice at the window's corner, a finished Swiss kept in a database with its standing, the Home page's line
// (R3), and closing RookStudio during an arena. What leaves the machine (R16): following, the tournament asked and its
// games, without an account; joining, with the token, the join, the pause and the user's moves; one request at a time,
// a minute after a 429.

#include "lichess_arenas_page.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <functional>
#include <memory>
#include <set>

#include <SDL3/SDL.h>
#include <fmt/include/fmt/format.h>
#include <imgui.h>

#include <study/crosstable.hpp>

#include "crosstables.hpp"
#include "databases.hpp"
#include "guide.hpp"
#include "home.hpp"
#include "i18n.hpp"
#include "jobs.hpp"
#include "keyboard.hpp"
#include "lichess_account.hpp"
#include "lichess_broadcasts.hpp"
#include "lichess_settings.hpp"
#include "ui.hpp"
#include "widgets.hpp"

namespace fcg {

namespace {

using Clock = std::chrono::steady_clock;

float px(float v) { return v * ImGui::GetFontSize() / 16.0f; }

template <typename T>
bool ready(const std::future<T>& f) {
    return f.valid() && f.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}

std::string lower(std::string_view text) {
    std::string out(text);
    for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

int64_t nowMillis() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

void tip(const std::string& text) {
    if (!ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled)) return;
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
    ImGui::TextUnformatted(text.c_str());
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

using ui::endTinted;
using ui::mix;
using ui::primaryButton;
using ui::wrapped;

// The arenas' tinted boxes are a little tighter than the training's.
constexpr ui::Tint TINT{.padding = ImVec2(14, 10)};
void beginTinted(const char* id, ImU32 tone) { ui::beginTinted(id, tone, TINT); }

void heading(const std::string& text) {
    ImGui::PushFont(ui::titleFont(), ImGui::GetStyle().FontSizeBase);
    ImGui::TextUnformatted(text.c_str());
    ImGui::PopFont();
}

void coloured(const std::string& text, ImU32 tone) {
    ImGui::PushStyleColor(ImGuiCol_Text, tone);
    ImGui::TextWrapped("%s", text.c_str());
    ImGui::PopStyleColor();
}

// A chip: a speed of the card; filled when chosen.
bool chip(const char* label, bool chosen) {
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, px(12));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(chosen ? ui::SELECT : ui::CARD_BG));
    ImGui::PushStyleColor(ImGuiCol_Border, ImGui::ColorConvertU32ToFloat4(chosen ? ui::ACCENT : ui::CARD_BD));
    const bool clicked = ImGui::SmallButton(label);
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);
    return clicked && !chosen;
}

const char* speedName(std::string_view key) {
    if (key == "ultraBullet") return TR("UltraBullet");
    if (key == "bullet") return TR("Bullet");
    if (key == "blitz") return TR("Blitz");
    if (key == "rapid") return TR("Rapid");
    if (key == "classical") return TR("Classical");
    return TR("Correspondence");
}

// A length of time as the lines say it: "38 min", "2 h 10", "3 d 4 h".
std::string lengthText(int64_t seconds) {
    seconds = std::max<int64_t>(0, seconds);
    if (seconds < 3600) return trf("{} min", std::max<int64_t>(1, (seconds + 59) / 60));
    if (seconds < 86400) return trf("{} h {:02}", seconds / 3600, seconds % 3600 / 60);
    return trf("{} d {} h", seconds / 86400, seconds % 86400 / 3600);
}

// When an arena is: "in progress · 38 min" (what is left), "in 40 min".
std::string whenText(const lichess::ArenaInfo& a) {
    const auto now = nowMillis();
    if (a.state == lichess::TourState::Started)
        return a.finishes_at > 0 ? trf("{} left", lengthText((a.finishes_at - now) / 1000))
                                 : std::string(TR("in progress"));
    if (a.state == lichess::TourState::Finished) return TR("finished");
    return a.starts_at > 0 ? trf("in {}", lengthText((a.starts_at - now) / 1000)) : std::string(TR("to come"));
}

// A day and an hour, local: "Thursday 20:00", the date when it is more than a week away.
std::string startText(int64_t ms) {
    if (ms <= 0) return {};
    const auto t = static_cast<std::time_t>(ms / 1000);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    static const char* const DAYS[] = {N_("Sunday"),   N_("Monday"), N_("Tuesday"), N_("Wednesday"),
                                       N_("Thursday"), N_("Friday"), N_("Saturday")};
    if (ms - nowMillis() > int64_t{7} * 86400000)
        return fmt::format("{:04}-{:02}-{:02}", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
    return trf("{} {:02}:{:02}", i18n::tr(DAYS[tm.tm_wday % 7]), tm.tm_hour, tm.tm_min);
}

std::string clockOf(int64_t limit_s, int64_t increment_s) { return lichess::tvClockText(limit_s, increment_s); }

// A name cut with "…" to fit `room`, its whole text on hover.
void cutText(const std::string& text, float room, bool strong = false) {
    std::string shown = text;
    const bool cut    = ImGui::CalcTextSize(shown.c_str()).x > room;
    if (cut) {
        while (!shown.empty() && ImGui::CalcTextSize((shown + "…").c_str()).x > room) {
            shown.pop_back();
            while (!shown.empty() && (static_cast<unsigned char>(shown.back()) & 0xC0) == 0x80) shown.pop_back();
            if (!shown.empty() && static_cast<unsigned char>(shown.back()) >= 0xC0) shown.pop_back();
        }
        shown += "…";
    }
    if (strong) ImGui::PushFont(ui::titleFont(), ImGui::GetStyle().FontSizeBase);
    ImGui::TextUnformatted(shown.c_str());
    if (strong) ImGui::PopFont();
    if (cut) ImGui::SetItemTooltip("%s", text.c_str());
}

std::string arenaUrl(const std::string& id) { return fmt::format("https://lichess.org/tournament/{}", id); }
std::string swissUrl(const std::string& id) { return fmt::format("https://lichess.org/swiss/{}", id); }

// A condition in the reader's words, from what the tournament states; Lichess's own words when RookStudio does not
// read it.
std::string conditionText(const lichess::TourCondition& c, std::string_view perf) {
    using Kind        = lichess::TourCondition::Kind;
    const auto* speed = speedName(c.perf.empty() ? perf : std::string_view(c.perf));
    switch (c.kind) {
        case Kind::RatedGames:
            return trf("At least {} rated games in {}", c.value, speed);
        case Kind::MaxRating:
            return trf("{} rating at most {}", speed, c.value);
        case Kind::MinRating:
            return trf("{} rating at least {}", speed, c.value);
        case Kind::Titled:
            return TR("Titled players only");
        case Kind::Team:
            return trf("Members of the team {} only", c.team_name);
        case Kind::AccountAge:
            return trn("An account at least {} day old", "An account at least {} days old", c.value, c.value);
        case Kind::AllowList:
            return TR("Players the organiser chose only");
        case Kind::Other:
            break;
    }
    return c.text;
}

// The user's figure for a condition, from their profile: "(you: 312)".
std::string figureOf(const lichess::TourCondition& c, std::string_view perf, const lichess::Profile* profile) {
    using Kind = lichess::TourCondition::Kind;
    if (!profile || (c.kind != Kind::RatedGames && c.kind != Kind::MaxRating && c.kind != Kind::MinRating)) return {};
    for (const auto& s : profile->speeds) {
        if (s.key != (c.perf.empty() ? perf : std::string_view(c.perf))) continue;
        if (c.kind == Kind::RatedGames) return trf("(you: {})", s.games);
        if (s.games == 0) return TR("(you: not rated yet)");
        return trf("(you: {})", std::to_string(s.rating) + (s.provisional ? "?" : ""));
    }
    return {};
}

}  // namespace

// ---------------------------------------------------------------------------
// The state of things

LichessArenasPage::LichessArenasPage(ArenasContext& app) : app_(app) {}

LichessArenasPage::~LichessArenasPage() = default;

bool LichessArenasPage::holdsAids() const {
    // In the arena from here, once it has started: between two games too (point 18), until Pause; the account
    // unlinked, RookStudio is in no arena any more.
    return app_.lichessAccount() && arena_play_ && arena_play_->seat.locks() && arena_play_->state.value &&
           arena_play_->state.value->info.state == lichess::TourState::Started;
}

const lichess::PlayingGame& LichessArenasPage::waiting() const { return arena_play_->waiting; }

bool LichessArenasPage::arenaGameOn() const {
    if (!arena_play_) return false;
    const auto online_game = app_.onlineGame();
    if (online_game && !online_game->ended && !online_game->state.over() &&
        online_game->state.tournament == arena_play_->id)
        return true;
    const auto& playing = app_.playingGames();
    return std::any_of(playing.begin(), playing.end(), [&](const auto& g) { return g.tournament == arena_play_->id; });
}

std::string LichessArenasPage::arenaTimeLeft() const {
    if (!arena_play_ || !arena_play_->state.value) return {};
    const auto& a = *arena_play_->state.value;
    if (a.info.state != lichess::TourState::Started || a.info.finishes_at <= 0) return {};
    return lengthText((a.info.finishes_at - nowMillis()) / 1000);
}

bool LichessArenasPage::followedShown() const { return tour_follow_ && app_.mosaicShown() == tour_follow_->key; }

const std::string& LichessArenasPage::followedKey() const {
    static const std::string none;
    return tour_follow_ ? tour_follow_->key : none;
}

const std::vector<std::string>& LichessArenasPage::slots() const {
    static const std::vector<std::string> none;
    return tour_follow_ ? tour_follow_->slots : none;
}

std::size_t LichessArenasPage::streams() const {
    return tour_follow_ && tour_follow_->follower ? tour_follow_->follower->open() : 0;
}

bool LichessArenasPage::isArenaGame(const std::string& tournament) const {
    return arena_play_ && !tournament.empty() && tournament == arena_play_->id;
}

bool LichessArenasPage::isSwissGame(const std::string& swiss) const {
    return swiss_play_ && !swiss.empty() && swiss == swiss_play_->id;
}

bool LichessArenasPage::inArena() const { return arena_play_ && arena_play_->seat.in(); }

bool LichessArenasPage::arenaOver() const { return arena_play_ && arena_play_->seat.isOver(); }

const std::string& LichessArenasPage::arenaName() const { return arena_play_->name; }

const std::string& LichessArenasPage::lastGame() const { return arena_play_->last_game; }

bool LichessArenasPage::berserkable() const {
    return arena_play_ && arena_play_->state.value && arena_play_->state.value->info.berserkable;
}

// A tournament's requests under way parked until they end, out of sight: a future of std::async waits for its task when
// destroyed.
void LichessArenasPage::dropTour() {
    if (!tour_follow_) return;
    app_.jobs().park(std::move(tour_follow_->arena_job));
    app_.jobs().park(std::move(tour_follow_->swiss_job));
    app_.jobs().park(std::move(tour_follow_->status_job));
    tour_follow_.reset();  // its streams closed
}

void LichessArenasPage::dropArena() {
    if (!arena_play_) return;
    app_.jobs().park(std::move(arena_play_->job));
    app_.jobs().park(std::move(arena_play_->join_job));
    app_.jobs().park(std::move(arena_play_->pause_job));
    arena_play_.reset();
}

void LichessArenasPage::dropSwiss() {
    if (!swiss_play_) return;
    app_.jobs().park(std::move(swiss_play_->job));
    app_.jobs().park(std::move(swiss_play_->join_job));
    app_.jobs().park(std::move(swiss_play_->pause_job));
    app_.jobs().park(std::move(swiss_play_->keep_job));
    swiss_play_.reset();
}

// ---------------------------------------------------------------------------
// Each frame

void LichessArenasPage::poll() {
    using namespace std::chrono_literals;
    const auto now      = Clock::now();
    const auto frame    = app_.frame();
    const auto* account = app_.lichessAccount();
    const auto token    = account ? account->token : std::string();
    const bool linked   = account != nullptr;
    // After a 429: until the host's pause is over.
    const auto pause_end = [&] { return now + lichess::apiPause(); };

    // The permission to read the user's teams granted: its question goes (#45's card).
    if (app_.scopeAsked() == lichess::SCOPE_TEAM_READ && linked && !app_.lichessLinking() &&
        account->has(lichess::SCOPE_TEAM_READ))
        app_.dropScopeAsk();
    // The card's list: asked when it shows (or the Play page's part), again after a minute; the list of before stays
    // when a refresh fails, the failure said.
    arena_card_.list.poll();
    const bool card_shown = drawnLately(arena_card_.drawn, frame);
    if (card_shown && arena_card_.list.due())
        arena_card_.list.ask([](const TaskState<>& state) { return lichess::arenaList(&state.cancel); });
    // The user's teams' Swiss tournaments, with team:read: their teams, then each one's, every five minutes.
    if (ready(arena_card_.swiss_job)) {
        auto [swisses, teams] = arena_card_.swiss_job.get();
        arena_card_.swisses   = std::move(swisses);
        arena_card_.teams     = teams;
    }
    if (lichess::teamSwissesDue({.shown     = card_shown,
                                 .readable  = linked && account->has(lichess::SCOPE_TEAM_READ),
                                 .new_token = arena_card_.swiss_token != token,
                                 .asking    = arena_card_.swiss_job.valid(),
                                 .asked     = arena_card_.swiss_asked},
                                now)) {
        arena_card_.swiss_asked = now;
        arena_card_.swiss_token = token;
        arena_card_.swiss_job   = std::async(std::launch::async, [token, name = account->username] {
            auto teams = lichess::teamsOf(name, token);
            auto out   = lichess::failedWith<std::vector<lichess::SwissInfo>>(teams);
            if (!teams.value) return std::make_pair(std::move(out), std::size_t{0});
            std::vector<lichess::SwissInfo> all;
            const auto week = nowMillis() - int64_t{7} * 86400000;
            for (std::size_t i = 0; i < teams.value->size() && i < 8; ++i) {
                const auto& team = (*teams.value)[i];
                auto list        = lichess::teamSwisses(team.id, token, 5);
                if (!list.value) {  // the others are listed all the same, this failure said
                    static_cast<lichess::Failure&>(out) = list;
                    continue;
                }
                for (auto& s : *list.value) {
                    if (s.state == lichess::TourState::Finished && s.starts_at < week) continue;
                    s.team_name = team.name;
                    all.push_back(std::move(s));
                }
            }
            // To come and in progress first, the soonest first; then those just finished.
            std::stable_sort(all.begin(), all.end(), [](const auto& x, const auto& y) {
                const bool xf = x.state == lichess::TourState::Finished, yf = y.state == lichess::TourState::Finished;
                if (xf != yf) return !xf;
                return xf ? x.starts_at > y.starts_at : x.starts_at < y.starts_at;
            });
            out.value = std::move(all);
            return std::make_pair(std::move(out), teams.value->size());
        });
    }

    // The tournament followed in the mosaic.
    if (tour_follow_) {
        auto& f          = *tour_follow_;
        const bool shown = followedShown();
        const bool drawn = shown && drawnLately(app_.mosaicDrawn(), frame);
        if (ready(f.arena_job)) {
            auto a = f.arena_job.get();
            if (a.net == lichess::NetError::RateLimited) f.waits_until = pause_end();
            if (a.value) {
                f.arena = std::move(a);
                if (f.name.empty()) f.name = f.arena.value->info.name;
            } else {
                static_cast<lichess::Failure&>(f.arena) = a;
            }
        }
        if (ready(f.swiss_job)) {
            auto [info, rows] = f.swiss_job.get();
            if (info.net == lichess::NetError::RateLimited) f.waits_until = pause_end();
            if (rows.net == lichess::NetError::RateLimited) f.waits_until = pause_end();
            if (info.value) {
                if (rows.value) {
                    f.swiss_rows = std::move(rows);
                    f.rows_round = info.value->round;
                }
                f.swiss_info = std::move(info);
                if (f.name.empty()) f.name = f.swiss_info.value->name;
            } else {
                static_cast<lichess::Failure&>(f.swiss_info) = info;
            }
        }
        // Its standing every 15 s while the mosaic shows; a Swiss's every 30 s (its standing after each round).
        if (lichess::standingDue({.drawn       = drawn,
                                  .swiss       = f.swiss,
                                  .asking      = f.arena_job.valid() || f.swiss_job.valid(),
                                  .asked       = f.asked,
                                  .waits_until = f.waits_until},
                                 now)) {
            f.asked = now;
            if (f.swiss) {
                const bool rows_due = lichess::swissRowsDue(
                    f.swiss_rows.value.has_value(), f.swiss_info.value ? &*f.swiss_info.value : nullptr, f.rows_round);
                f.swiss_job = std::async(std::launch::async, [id = f.id, token, rows_due] {
                    auto info = lichess::swissOf(id, token);
                    lichess::Answer<std::vector<lichess::SwissRow>> rows;
                    if (info.value && rows_due) rows = lichess::swissResults(id);
                    return std::make_pair(std::move(info), std::move(rows));
                });
            } else {
                f.arena_job =
                    std::async(std::launch::async, [id = f.id, token] { return lichess::arenaOf(id, 1, token); });
            }
        }
        // The first eight's names, as the standing gives them; their games found by player.
        std::vector<std::string> top;
        if (f.swiss) {
            if (f.swiss_rows.value)
                for (const auto& r : *f.swiss_rows.value)
                    if (top.size() < lichess::ARENA_GAMES && !r.absent) top.push_back(r.name);
        } else if (f.arena.value) {
            for (const auto& p : f.arena.value->standing)
                if (top.size() < lichess::ARENA_GAMES && !p.paused) top.push_back(p.name);
        }
        const bool playing = f.swiss ? f.swiss_info.value && f.swiss_info.value->ongoing > 0
                                     : f.arena.value && f.arena.value->info.state == lichess::TourState::Started;
        if (ready(f.status_job)) {
            auto a = f.status_job.get();
            if (a.net == lichess::NetError::RateLimited) f.waits_until = pause_end();
            if (a.value) {
                f.status_error = {};
                // Their games, in the standing's order: never one of the user's own.
                std::vector<std::string> mine;
                for (const auto& g : app_.playingGames()) mine.push_back(g.id);
                if (const auto online_game = app_.onlineGame()) mine.push_back(online_game->id);
                f.listed.clear();
                for (const auto& name : f.top)
                    for (const auto& s : *a.value) {
                        if (lower(s.name) != lower(name) || !s.playing || s.game.empty()) continue;
                        if (std::find(mine.begin(), mine.end(), s.game) != mine.end()) continue;
                        if (std::any_of(f.listed.begin(), f.listed.end(),
                                        [&](const auto& g) { return g.id == s.game; }))
                            continue;
                        lichess::TvGame g;
                        g.id         = s.game;
                        g.white.name = s.name;
                        g.white.id   = s.id;
                        g.status     = "started";
                        f.listed.push_back(std::move(g));
                    }
            } else {
                f.status_error = a;
            }
        }
        const bool some_over =
            std::any_of(f.places.begin(), f.places.end(), [&](const auto& id) { return f.over.count(id) > 0; });
        if (lichess::statusesDue({.shown       = shown,
                                  .playing     = playing,
                                  .top         = top.size(),
                                  .top_changed = top != f.top,
                                  .some_over   = some_over,
                                  .places      = f.places.size(),
                                  .asking      = f.status_job.valid(),
                                  .asked       = f.status_asked,
                                  .waits_until = f.waits_until},
                                 now)) {
            f.top          = top;
            f.status_asked = now;
            f.status_job   = std::async(std::launch::async, [top] { return lichess::playerStatuses(top); });
        }
        // The places: a finished game keeps its tile 5 s, its result shown, then gives it to the next one.
        const auto over = f.follower ? f.follower->over() : std::set<std::string>{};
        f.over.insert(over.begin(), over.end());
        for (const auto& id : f.over) f.over_since.emplace(id, now);
        std::set<std::string> resting;
        for (const auto& [id, at] : f.over_since)
            if (now - at < 5s) resting.insert(id);
        const auto streams = app_.gameStreams();
        const std::size_t total =
            streams.rounds >= lichess::MOST_GAME_STREAMS ? 0 : lichess::MOST_GAME_STREAMS - streams.rounds;
        const std::size_t others = streams.tv + streams.players;
        f.quota                  = shown ? (total > others ? total - others : 0) : 0;
        f.places = lichess::tvPlaces(f.places, f.over, f.listed, {}, app_.ownLichessIds(), f.quota, resting);
        std::erase_if(f.over, [&](const std::string& id) {
            const bool listed =
                std::any_of(f.listed.begin(), f.listed.end(), [&](const auto& g) { return g.id == id; });
            return !listed && std::find(f.places.begin(), f.places.end(), id) == f.places.end() && !over.count(id);
        });
        std::erase_if(f.over_since, [&](const auto& e) { return !f.over.count(e.first); });
        if (f.places.empty() && !shown) {
            f.follower.reset();
        } else {
            if (!f.follower) f.follower = std::make_unique<lichess::PlayersFollower>();
            f.follower->watch(f.places, f.quota);
        }
    }

    // The arena the user is in from here.
    if (arena_play_) {
        auto& a = *arena_play_;
        if (ready(a.job)) {
            auto [state, me] = a.job.get();
            if (state.net == lichess::NetError::RateLimited) a.waits_until = pause_end();
            if (state.value) {
                const bool fire_before = a.me && a.me->fire;
                // It starts: joined again at once, Lichess pairing then the players present.
                const bool started = a.state.value && a.state.value->info.state == lichess::TourState::Created &&
                                     state.value->info.state == lichess::TourState::Started;
                a.state            = std::move(state);
                if (started && a.seat.in() && !a.join_job.valid()) joinArenaHere(true);
                if (me) {
                    a.me = me;
                    // The game before's points, once the sheet has it.
                    const auto sheet = lichess::readSheet(me->sheet, a.state.value->streaks);
                    if (a.sheet_before >= 0 && static_cast<int>(sheet.size()) > a.sheet_before && a.last_points < 0 &&
                        !a.last_game.empty()) {
                        a.last_points   = sheet.back().points;
                        a.last_doubled  = sheet.back().doubled;
                        a.streak_starts = me->fire && !fire_before && !sheet.back().doubled;
                    }
                }
                if (a.state.value->info.state == lichess::TourState::Finished) a.seat.over();
                if (a.state.value->me && a.state.value->me->paused && a.seat.in() && !a.pause_job.valid() &&
                    !arenaGameOn() && !a.join_job.valid() && Clock::now() - a.join_asked > 20s)
                    a.seat.pause();  // paused by Lichess (a first move missed, a pause on lichess.org)
            } else {
                static_cast<lichess::Failure&>(a.state) = state;
            }
        }
        // Its page every 15 s while the user is in it or looks at it; their own page when they are not in the first
        // ten.
        const bool looked = app_.playTournamentsShown() || a.seat.in() || app_.homeShown();
        if (lichess::arenaPageDue({.looked      = looked,
                                   .over        = a.seat.isOver(),
                                   .asking      = a.job.valid(),
                                   .asked       = a.asked,
                                   .waits_until = a.waits_until},
                                  now)) {
            a.asked = now;
            a.job =
                std::async(std::launch::async, [id = a.id, token, name = linked ? account->username : std::string()] {
                    auto state = lichess::arenaOf(id, 1, token);
                    std::optional<lichess::ArenaPlayer> me;
                    if (state.value && !name.empty()) {
                        if (const auto* row = lichess::standingOf(*state.value, name)) {
                            me = *row;
                        } else if (state.value->me && state.value->me->rank > 10) {
                            const auto page = lichess::arenaOf(id, lichess::standingPage(state.value->me->rank), token);
                            if (page.value)
                                if (const auto* mine = lichess::standingOf(*page.value, name)) me = *mine;
                        }
                    }
                    return std::make_pair(std::move(state), std::move(me));
                });
        }
        if (ready(a.join_job)) {
            const auto r = a.join_job.get();
            if (r.ok) {
                a.seat.joined(Clock::now());
                a.error = {};
                a.asked = {};  // its page asked again at once: the user's rank
            } else {
                a.error = r;
                if (r.net == lichess::NetError::RateLimited) a.waits_until = pause_end();
                if (!a.quiet_join) a.seat.pause();
            }
        }
        if (ready(a.pause_job)) {
            const auto r = a.pause_job.get();
            if (!r.ok) a.error = r;
            if (a.quit_after) {
                a.quit_after = false;
                app_.closeAnyway();
            }
        }
        if (a.quit_after && app_.seconds() - a.quit_asked > 5.0) {  // Lichess too slow: closed all the same
            a.quit_after = false;
            app_.closeAnyway();
        }
        // A game of it being played: no join; its end, the next join at once.
        const bool on          = arenaGameOn();
        const auto online_game = app_.onlineGame();
        a.seat.playing(on);
        if (on && online_game && online_game->state.tournament == a.id && a.last_game != online_game->id &&
            std::find(a.games.begin(), a.games.end(), online_game->id) == a.games.end()) {
            a.games.push_back(online_game->id);
            a.sheet_before = a.me ? static_cast<int>(lichess::readSheet(a.me->sheet).size()) : -1;
        }
        // Its end: the game before, as the card says it until the next one.
        if (online_game && online_game->ended && online_game->state.tournament == a.id &&
            a.last_game != online_game->id) {
            const auto& og = *online_game;
            const auto& s  = og.state;
            a.last_game    = og.id;
            a.last_moves   = static_cast<int>((s.moves.size() + 1) / 2);
            a.last_points  = -1;
            const bool won = !s.winner.empty() && (s.winner == "white") == og.white;
            a.last_result  = s.winner.empty() ? "draw" : won ? "won" : "lost";
            if (lichess::lostAtStart(s, og.white)) a.last_result = "nostart";
            a.asked = {};  // the sheet asked again at once
        }
        if (lichess::rejoinDue(a.seat, linked, a.join_job.valid(), a.waits_until, now)) joinArenaHere(true);
        if (a.join_pending && linked && !app_.lichessLinking() && account->has(lichess::SCOPE_TOURNAMENT_WRITE)) {
            a.join_pending = false;
            joinArenaHere();
        }
        // What the lock holds between two games: the arena itself.
        a.waiting.tournament = a.id;
        a.waiting.speed      = a.state.value ? a.state.value->info.speed : std::string("rapid");
        // Alt+P: the pause, and back in, wherever the user is.
        if ((a.seat.in() || a.seat.paused()) && app_.shortcutPressed(ImGuiMod_Alt | ImGuiKey_P)) toggleArenaPause();
    }

    // The Swiss the Play page shows.
    if (swiss_play_) {
        auto& s = *swiss_play_;
        if (ready(s.job)) {
            auto [info, rows, trf_read] = s.job.get();
            if (info.value) {
                if (rows.value) {
                    s.rows       = std::move(rows);
                    s.rows_round = info.value->round;
                }
                if (trf_read) s.trf = std::move(trf_read);
                s.info = std::move(info);
                if (s.name.empty()) s.name = s.info.value->name;
            } else {
                static_cast<lichess::Failure&>(s.info) = info;
            }
        }
        const bool looked = app_.playTournamentsShown() || s.joined;
        if (lichess::swissPageDue({.looked   = looked,
                                   .joined   = s.joined,
                                   .started  = s.info.value && s.info.value->state == lichess::TourState::Started,
                                   .complete = s.info.value && s.info.value->state == lichess::TourState::Finished &&
                                               s.rows.value && s.trf,
                                   .asking   = s.job.valid(),
                                   .asked    = s.asked},
                                  now)) {
            s.asked             = now;
            const bool rows_due =
                lichess::swissRowsDue(s.rows.value.has_value(), s.info.value ? &*s.info.value : nullptr, s.rows_round);
            s.job               = std::async(std::launch::async, [id = s.id, token, rows_due] {
                auto info = lichess::swissOf(id, token);
                lichess::Answer<std::vector<lichess::SwissRow>> rows;
                std::optional<lichess::Trf> trf_read;
                if (info.value && rows_due) {
                    rows = lichess::swissResults(id);
                    if (info.value->round > 0)
                        if (const auto t = lichess::swissTrf(id); t.value) trf_read = lichess::readTrf(*t.value);
                }
                return std::make_tuple(std::move(info), std::move(rows), std::move(trf_read));
            });
        }
        if (ready(s.join_job)) {
            const auto r = s.join_job.get();
            if (r.ok) {
                s.joined = true;
                s.error  = {};
                s.asked  = {};
            } else {
                s.error = r;
            }
        }
        if (ready(s.pause_job)) {
            const auto r = s.pause_job.get();
            if (r.ok)
                s.joined = false;
            else
                s.error = r;
        }
        if (s.join_pending && linked && !app_.lichessLinking() && account->has(lichess::SCOPE_TOURNAMENT_WRITE)) {
            s.join_pending = false;
            joinSwissHere();
        }
        // A game of it started where the user is: they are in it, its round read again at once.
        if (const auto online_game = app_.onlineGame();
            online_game && online_game->state.swiss == s.id && s.game != online_game->id) {
            s.game   = online_game->id;
            s.joined = true;
            s.asked  = {};
        }
        if (ready(s.keep_job)) {
            auto kept    = s.keep_job.get();
            s.kept_in    = kept.base;
            s.keep_error = kept.failure;
            s.kept_games = kept.games;
            // The rounds without a game, set by hand for the tournament's crosstable (#92).
            if (!kept.failure.failed() && !kept.key.empty() && !kept.hand.empty()) {
                const auto place   = crosstablePlace(s.kept_folder, kept.key);
                auto settings      = app_.crosstables().get(place);
                settings.hand      = kept.hand;
                settings.hand_from = "lichess-trf";  // the crosstable says where they come from
                app_.crosstables().set(place, settings);
            }
            if (!kept.failure.failed() && kept.index < app_.shelf().entries().size())
                app_.shelf().rebuildTree(kept.index);
        }
    }
}

// ---------------------------------------------------------------------------
// The Lichess page's card

void LichessArenasPage::drawCard() {
    arena_card_.drawn = app_.frame();
    app_.askLichessProfile();
    const ui::TopicRegion topic(guide::topics::LichessTournaments);
    ui::beginCard("lichess_arenas");
    if (arena_card_y_ == -2) arena_card_y_ = ImGui::GetCursorScreenPos().y;  // asked for: the page scrolls to it
    ImGui::PushTextWrapPos();
    ui::inlineIcon(ui::Icon::Trophy, ui::ACCENT);
    ImGui::SameLine();
    ImGui::TextUnformatted(TR("Lichess tournaments"));
    ImGui::SameLine();
    ImGui::TextDisabled("%s", TR("· in progress and to come, read without an account"));
    // Its speeds, as chips at the right.
    {
        const char* labels[] = {TR("Rapid"), TR("Classical"), TR("All")};
        float w              = 0;
        for (const char* l : labels) w += ImGui::CalcTextSize(l).x + ImGui::GetStyle().FramePadding.x * 2 + px(8);
        ui::sameLineOrWrap(w);
        for (int i = 0; i < 3; ++i) {
            if (i > 0) ImGui::SameLine(0, px(4));
            if (chip(fmt::format("{}##arena_speed{}", labels[i], i).c_str(),
                     app_.lichessSettings().arena_speeds == i)) {
                app_.lichessSettings().arena_speeds = i;
                app_.saveLichessSettings();
            }
        }
    }
    const auto& list  = arena_card_.list;
    const auto speeds = app_.lichessSettings().arena_speeds == 0   ? lichess::ArenaSpeeds::Rapid
                        : app_.lichessSettings().arena_speeds == 1 ? lichess::ArenaSpeeds::Classical
                                                                   : lichess::ArenaSpeeds::All;
    if (!list.value) {
        ImGui::TextDisabled("%s", list.failed() ? lichess::gaveNothing(list).c_str() : TR("Asking Lichess…"));
    } else {
        std::vector<const lichess::ArenaInfo*> shown;
        for (const auto& a : *list.value)
            if (lichess::arenaShown(a, speeds)) shown.push_back(&a);
        if (shown.empty()) ImGui::TextDisabled("%s", TR("No arena of these speeds now."));
        const auto* profile = app_.lichessProfile();
        // The last column as wide as its widest line: Follow, then Join or why not.
        const auto& style = ImGui::GetStyle();
        float why_w       = ImGui::CalcTextSize(TR("Join")).x + style.FramePadding.x * 2;
        for (const auto* a : shown) {
            const auto offer = lichess::joinOffer(*a, profile);
            if (offer == lichess::JoinOffer::WebOnly)
                why_w = std::max(why_w, ImGui::CalcTextSize(trf("{}: on lichess.org", speedName(a->speed)).c_str()).x);
            else if (offer == lichess::JoinOffer::ConditionNotMet)
                why_w = std::max(why_w, ImGui::CalcTextSize(TR("condition not met")).x);
        }
        const float join_w =
            ImGui::CalcTextSize(TR("Follow")).x + style.FramePadding.x * 2 + style.ItemSpacing.x + why_w + px(2);
        ImGui::PushTextWrapPos(-1.0f);  // the cells cut their text instead
        if (!shown.empty() &&
            ImGui::BeginTable("##arenas", 5, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerH)) {
            ImGui::TableSetupColumn(TR("Arena"), ImGuiTableColumnFlags_WidthStretch, 2.4f);
            ImGui::TableSetupColumn(TR("Time control"), ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableSetupColumn(TR("When"), ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableSetupColumn(TR("Players"), ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableSetupColumn("##join", ImGuiTableColumnFlags_WidthFixed, join_w);
            ImGui::TableHeadersRow();
            for (const auto* a : shown) {
                ImGui::PushID(a->id.c_str());
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                cutText(a->name, ImGui::GetContentRegionAvail().x);
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(clockOf(a->limit_s, a->increment_s).c_str());
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                ImGui::TextDisabled("%s", whenText(*a).c_str());  // the column as wide as its widest
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(a->state == lichess::TourState::Created
                                           ? trn("{} joined", "{} joined", a->players, ui::exactCount(a->players)).c_str()
                                           : ui::exactCount(a->players).c_str());
                ImGui::TableNextColumn();
                if (ImGui::SmallButton(TR("Follow"))) followTournament(a->id, a->name, false);
                tip(TR("Its standing and the games of its first eight in the mosaic, without an account."));
                ImGui::SameLine();
                switch (lichess::joinOffer(*a, profile)) {
                    case lichess::JoinOffer::Join:
                        if (primaryButton(TR("Join##arena"))) chooseArena(a->id, a->name);
                        tip(TR("On the Play page, its conditions and its rules; your games on the Board."));
                        break;
                    case lichess::JoinOffer::WebOnly: {
                        const auto text = trf("{}: on lichess.org", speedName(a->speed));
                        ImGui::AlignTextToFramePadding();
                        ImGui::TextDisabled("%s", text.c_str());
                        if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                        if (ImGui::IsItemClicked()) SDL_OpenURL(arenaUrl(a->id).c_str());
                        tip(a->speed == "blitz"
                                ? TR("Lichess's Board API plays no blitz in a tournament: follow it here, play it on "
                                     "lichess.org (a click opens it).")
                                : TR("Lichess's Board API plays no bullet in a tournament: follow it here, play it on "
                                     "lichess.org (a click opens it)."));
                        break;
                    }
                    case lichess::JoinOffer::ConditionNotMet: {
                        ImGui::AlignTextToFramePadding();
                        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s", TR("condition not met"));
                        std::string why;
                        for (const auto& c : a->conditions) {
                            const auto figure = figureOf(c, a->speed, profile);
                            why += (why.empty() ? "" : "\n") + conditionText(c, a->speed) +
                                   (figure.empty() ? "" : " " + figure);
                        }
                        if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                        tip(why);
                        if (ImGui::IsItemClicked()) chooseArena(a->id, a->name);
                        break;
                    }
                    case lichess::JoinOffer::Finished:
                        ImGui::TextDisabled("%s", TR("finished"));
                        break;
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        ImGui::PopTextWrapPos();
        if (list.failed())
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s",
                               trf("The list shown was read earlier: {}.", lichess::describe(list)).c_str());
    }
    ImGui::Dummy(ImVec2(0, px(4)));
    drawSwissList();
    ImGui::Dummy(ImVec2(0, px(4)));
    beginTinted("##arenas_board_api", ui::ACCENT);
    ui::inlineIcon(ui::Icon::Info, ui::ACCENT);
    ImGui::SameLine();
    wrapped(
        TR("Lichess lets a program play rapid, classical and correspondence only (its Board API); blitz by a "
           "direct challenge only, never in a tournament. Blitz and bullet arenas are followed here and played on "
           "lichess.org: their line says so, with the link."));
    endTinted();
    ImGui::TextDisabled("%s", TR("The variants' arenas are not shown. Asked of Lichess without an account: the list, "
                                 "and the tournament you follow. Joining asks your account."));
    ImGui::PopTextWrapPos();
    ui::endCard();
}

void LichessArenasPage::drawSwissList() {
    ImGui::TextUnformatted(TR("Your teams' Swiss tournaments"));
    ImGui::SameLine();
    ImGui::TextDisabled("%s", TR("· team:read, asked once to read your teams"));
    const auto* account = app_.lichessAccount();
    if (!account) {
        ImGui::TextDisabled("%s",
                            TR("With your Lichess account linked: a Swiss tournament is for its team's members."));
        return;
    }
    if (!account->has(lichess::SCOPE_TEAM_READ)) {
        if (app_.lichessSettings().team_declined) {
            ImGui::TextDisabled("%s", TR("Your teams are not read."));
            ImGui::SameLine();
        }
        if (ImGui::SmallButton(TR("Read my teams…"))) app_.askScope(lichess::SCOPE_TEAM_READ);
        tip(TR("Lichess asks for the permission to read your teams (team:read); RookStudio writes nothing to them."));
        return;
    }
    const auto& sw = arena_card_.swisses;
    if (!sw.value) {
        ImGui::TextDisabled("%s", sw.failed() ? lichess::gaveNothing(sw).c_str() : TR("Asking Lichess…"));
        return;
    }
    if (sw.value->empty()) {
        ImGui::TextDisabled("%s", arena_card_.teams == 0 ? TR("You are in no team on Lichess.")
                                                         : TR("No Swiss tournament of your teams now."));
        return;
    }
    for (const auto& s : *sw.value) {
        ImGui::PushID(s.id.c_str());
        const auto name = s.team_name.empty() ? s.name : trf("{} — {}", s.team_name, s.name);
        ImGui::AlignTextToFramePadding();
        ImGui::PushFont(ui::titleFont(), ImGui::GetStyle().FontSizeBase);
        ImGui::TextUnformatted(name.c_str());
        ImGui::PopFont();
        std::string more = trn("{} round", "{} rounds", s.rounds, s.rounds) + " · " + clockOf(s.limit_s, s.increment_s);
        if (s.state == lichess::TourState::Created)
            more += " · " + startText(s.starts_at) + " · " + trn("{} joined", "{} joined", s.players, ui::exactCount(s.players));
        else if (s.state == lichess::TourState::Started)
            more += " · " + trf("round {} of {}", s.round, s.rounds) + " · " +
                    trn("{} player", "{} players", s.players, ui::exactCount(s.players));
        else
            more += " · " + std::string(TR("finished"));
        ui::sameLineOrWrap(ImGui::CalcTextSize(more.c_str()).x);
        ImGui::TextDisabled("%s", more.c_str());
        ImGui::SameLine();
        if (s.state == lichess::TourState::Finished) {
            if (ImGui::SmallButton(TR("Keep it in a database…"))) {
                chooseSwiss(s);
                swiss_play_->keep_menu = true;
            }
        } else if (!lichess::playableHere(s)) {
            ImGui::TextDisabled("%s", trf("{}: on lichess.org", speedName(s.speed())).c_str());
            if (ImGui::IsItemClicked()) SDL_OpenURL(swissUrl(s.id).c_str());
        } else if (primaryButton(TR("Join##swiss"))) {
            chooseSwiss(s);
        }
        if (s.state == lichess::TourState::Started) {
            ImGui::SameLine();
            if (ImGui::SmallButton(TR("Follow##swiss"))) followTournament(s.id, s.name, true);
        }
        ImGui::PopID();
    }
}

void LichessArenasPage::showCard() {
    app_.showLichessPage();
    arena_card_y_ = -2;
}

void LichessArenasPage::scrollToCard() {
    if (arena_card_y_ >= 0) {
        ImGui::SetScrollFromPosY(arena_card_y_ - ImGui::GetWindowPos().y, 0.0f);
        arena_card_y_ = -1;
    }
}

// ---------------------------------------------------------------------------
// The mosaic

void LichessArenasPage::followTournament(const std::string& id, const std::string& name, bool swiss) {
    const auto key = std::string(swiss ? lichess::SWISS_MOSAIC : lichess::ARENA_MOSAIC) + id;
    if (!tour_follow_ || tour_follow_->key != key) {
        TourFollow f;
        f.key   = key;
        f.id    = id;
        f.name  = name;
        f.swiss = swiss;
        dropTour();  // the one before: its streams closed
        tour_follow_ = std::move(f);
    }
    app_.openMosaic(key);
}

void LichessArenasPage::stopFollowing() {
    dropTour();
    if (isTourMosaic(app_.mosaicShown())) app_.closeMosaic();
}

std::vector<lichess::BroadcastGame> LichessArenasPage::mosaicGames() {
    // The first eight's games in the order of their places; a game that ended keeps its tile until the one taking its
    // place has come, so that the tiles do not move (as Lichess TV's).
    std::vector<lichess::BroadcastGame> games;
    if (!tour_follow_) return games;
    auto& f = *tour_follow_;
    if (f.follower) {
        const auto all  = f.follower->games();
        const auto find = [&](const std::string& url) {
            return std::find_if(all.begin(), all.end(), [&](const auto& g) { return g.url == url; });
        };
        std::vector<std::string> slots;
        for (std::size_t i = 0; i < f.places.size(); ++i) {
            const auto url     = fmt::format("https://lichess.org/{}", f.places[i]);
            const bool kept_on = find(url) == all.end() && i < f.slots.size() && find(f.slots[i]) != all.end();
            slots.push_back(kept_on ? f.slots[i] : url);
        }
        f.slots = std::move(slots);
        for (const auto& url : f.slots)
            if (const auto it = find(url); it != all.end()) games.push_back(*it);
    }
    return games;
}

void LichessArenasPage::drawMosaicHead() {
    if (!tour_follow_) return;
    auto& f = *tour_follow_;
    ImGui::PushFont(ui::titleFont(), ImGui::GetStyle().FontSizeBase * 1.1f);
    ImGui::TextUnformatted(f.name.empty() ? (f.swiss ? TR("A Swiss tournament") : TR("An arena")) : f.name.c_str());
    ImGui::PopFont();
    std::vector<std::string> parts;
    if (!f.swiss && f.arena.value) {
        const auto& a = *f.arena.value;
        parts.push_back(clockOf(a.info.limit_s, a.info.increment_s));
        if (a.info.state == lichess::TourState::Started && a.seconds_to_finish >= 0)
            parts.push_back(trf("{} left", lengthText(a.seconds_to_finish)));
        else if (a.info.state == lichess::TourState::Created && a.seconds_to_start >= 0)
            parts.push_back(trf("starts in {}", lengthText(a.seconds_to_start)));
        else if (a.info.state == lichess::TourState::Finished)
            parts.push_back(TR("finished"));
        parts.push_back(trn("{} player", "{} players", a.info.players, ui::exactCount(a.info.players)));
    } else if (f.swiss && f.swiss_info.value) {
        const auto& s = *f.swiss_info.value;
        parts.push_back(trf("round {} of {}", s.round, s.rounds));
        parts.push_back(clockOf(s.limit_s, s.increment_s));
        parts.push_back(trn("{} player", "{} players", s.players, ui::exactCount(s.players)));
    }
    parts.push_back(TR("followed without an account"));
    if (const auto& engine = app_.lichessSettings().broadcast_engine; !engine.empty())
        parts.push_back(trf("analysed by {}", engine));
    std::string line;
    for (const auto& p : parts) line += (line.empty() ? "· " : " · ") + p;
    ImGui::SameLine();
    ui::sameLineOrWrap(ImGui::CalcTextSize(line.c_str()).x);
    ImGui::TextDisabled("%s", line.c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton(TR("On lichess.org##tour")))
        SDL_OpenURL((f.swiss ? swissUrl(f.id) : arenaUrl(f.id)).c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton(TR("Stop following##tour"))) {
        stopFollowing();
        return;
    }
    // Lichess refused too many requests: the standing waits its minute, the games go on.
    const auto now = Clock::now();
    if (now < f.waits_until) {
        const auto left = std::chrono::duration_cast<std::chrono::seconds>(f.waits_until - now).count() + 1;
        ImGui::TextColored(
            ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s",
            trf("Too many requests (429): the standing waits a minute ({} s left); the games go on, their "
                "streams apart.",
                left)
                .c_str());
    } else if (const auto& error = f.swiss ? static_cast<const lichess::Failure&>(f.swiss_info) : f.arena;
               error.failed()) {
        ImGui::TextDisabled("· %s", lichess::describeAlone(error).c_str());
    }
}

void LichessArenasPage::drawStanding() {
    if (!tour_follow_) return;
    auto& f = *tour_follow_;
    const ui::TopicRegion topic(guide::topics::LichessTournaments);
    ui::beginCard("tour_standing");
    ImGui::TextUnformatted(TR("The standing"));
    ImGui::SameLine();
    if (f.swiss) {
        if (f.rows_round > 0)
            ImGui::TextDisabled("%s", trf("· after round {}", f.rows_round).c_str());
        else
            ImGui::TextDisabled("%s", TR("· before the first round"));
    } else {
        ImGui::TextDisabled("%s", TR("· every 15 s"));
    }
    ImGui::PushTextWrapPos(-1.0f);
    if (!f.swiss && f.arena.value) {
        const auto& a = *f.arena.value;
        if (ImGui::BeginTable("##arena_standing", 4, ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("##rank", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableSetupColumn("##player", ImGuiTableColumnFlags_WidthStretch, 1.6f);
            ImGui::TableSetupColumn("##sheet", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableSetupColumn("##score", ImGuiTableColumnFlags_WidthFixed);
            for (const auto& p : a.standing) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%d", p.rank);
                ImGui::TableNextColumn();
                const auto name = p.title.empty() ? p.name : fmt::format("{} {}", p.title, p.name);
                const auto who  = p.rating > 0 ? fmt::format("{} {}", name, p.rating) : name;
                cutText(who, ImGui::GetContentRegionAvail().x);
                if (p.paused && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TR("Paused"));
                ImGui::TableNextColumn();
                // The sheet as lichess.org shows it, the oldest game first, a streak's doubled points in orange; cut
                // on its left, its newest games kept.
                const auto sheet  = lichess::readSheet(p.sheet, a.streaks);
                const float room  = ImGui::GetContentRegionAvail().x;
                const float dots  = ImGui::CalcTextSize("…").x;  // the monospaced font has no "…"
                const float small = ImGui::GetStyle().FontSizeBase * 0.92f;
                ImGui::PushFont(ui::monoFont(), small);
                const float digit_w = ImGui::CalcTextSize("0").x;
                ImGui::PopFont();
                std::size_t from = 0;
                if (static_cast<float>(sheet.size()) * digit_w > room) {
                    const auto fit = static_cast<std::size_t>(std::max(0.0f, (room - dots) / digit_w));
                    from           = sheet.size() - std::min(fit, sheet.size());
                }
                const bool cut = from > 0;
                ImGui::BeginGroup();
                if (cut) {
                    ImGui::TextDisabled("…");
                    ImGui::SameLine(0, 0);
                }
                ImGui::PushFont(ui::monoFont(), small);
                for (std::size_t k = from; k < sheet.size(); ++k) {
                    const auto digit = std::to_string(sheet[k].points);
                    if (k > from) ImGui::SameLine(0, 0);
                    if (sheet[k].doubled)
                        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s", digit.c_str());
                    else
                        ImGui::TextUnformatted(digit.c_str());
                }
                ImGui::PopFont();
                if (sheet.empty()) ImGui::TextDisabled("–");
                ImGui::EndGroup();
                // The whole sheet when it is cut; the streak.
                if ((cut || p.fire) && ImGui::IsItemHovered()) {
                    std::string text;
                    if (cut)
                        for (const auto& s : sheet) text += std::to_string(s.points);
                    if (p.fire)
                        text +=
                            (text.empty() ? "" : "\n") + std::string(TR("On a streak: the next win counts double."));
                    ImGui::SetTooltip("%s", text.c_str());
                }
                ImGui::TableNextColumn();
                ImGui::PushFont(ui::titleFont(), ImGui::GetStyle().FontSizeBase);
                ImGui::Text("%d", p.score);
                ImGui::PopFont();
            }
            ImGui::EndTable();
        }
    } else if (f.swiss && f.swiss_rows.value) {
        if (ImGui::BeginTable("##swiss_standing", 5, ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn(TR("Pl."), ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableSetupColumn(TR("Player"), ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn(TR("Pts"), ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableSetupColumn(TR("Tie-break"), ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableSetupColumn(TR("Perf."), ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableHeadersRow();
            std::size_t shown = 0;
            for (const auto& r : *f.swiss_rows.value) {
                if (++shown > 12) break;
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%d", r.rank);
                ImGui::TableNextColumn();
                cutText(r.rating > 0 ? fmt::format("{} {}", r.name, r.rating) : r.name,
                        ImGui::GetContentRegionAvail().x);
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(home::halfPoints(r.points).c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(fmt::format("{:.2f}", r.tie_break).c_str());
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", r.performance > 0 ? std::to_string(r.performance).c_str() : "–");
            }
            ImGui::EndTable();
        }
    } else {
        ImGui::TextDisabled("%s", TR("Asking Lichess…"));
    }
    ImGui::PopTextWrapPos();
    ui::endCard();
}

void LichessArenasPage::drawMosaicEmpty() {
    if (!tour_follow_) return;
    const auto& f       = *tour_follow_;
    const bool started  = f.swiss ? f.swiss_info.value && f.swiss_info.value->state != lichess::TourState::Created
                                  : f.arena.value && f.arena.value->info.state != lichess::TourState::Created;
    const bool finished = f.swiss ? f.swiss_info.value && f.swiss_info.value->state == lichess::TourState::Finished
                                  : f.arena.value && f.arena.value->info.state == lichess::TourState::Finished;
    ImGui::PushTextWrapPos();
    if (f.quota == 0)
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s",
                           trf("Lichess lets an address follow {} games at once: the rounds and the players "
                               "followed take them all now.",
                               lichess::MOST_GAME_STREAMS)
                               .c_str());
    else
        ImGui::TextDisabled("%s", finished   ? TR("It is over: its standing stays.")
                                  : !started ? TR("It has not started: the games of its first eight come here when "
                                                  "it does.")
                                  : f.swiss  ? TR("No game of its first eight now: the next round's games come here "
                                                  "as it starts, three moves late.")
                                             : TR("None of its first eight plays now: their games come here as they "
                                                  "start, three moves late."));
    ImGui::PopTextWrapPos();
    ImGui::Dummy(ImVec2(0, px(6)));
    drawStanding();
}

// ---------------------------------------------------------------------------
// The Play page: On Lichess › Tournaments

void LichessArenasPage::chooseArena(const std::string& id, const std::string& name) {
    if (!arena_play_ || arena_play_->id != id) {
        if (arena_play_ && (arena_play_->seat.in() || arena_play_->seat.paused()))
            pauseArena();  // in another one: paused there first
        ArenaPlay a;
        a.id   = id;
        a.name = name;
        dropArena();
        arena_play_ = std::move(a);
    }
    dropSwiss();
    tour_part_swiss_ = false;
    app_.showPlayTournaments(true);
}

void LichessArenasPage::chooseSwiss(const lichess::SwissInfo& swiss) {
    if (!swiss_play_ || swiss_play_->id != swiss.id) {
        SwissPlay s;
        s.id         = swiss.id;
        s.name       = swiss.name;
        s.team_id    = swiss.team_id;
        s.team_name  = swiss.team_name;
        s.info.value = swiss;
        dropSwiss();
        swiss_play_ = std::move(s);
    }
    if (arena_play_ && !arena_play_->seat.in() && !arena_play_->seat.paused()) dropArena();
    tour_part_swiss_ = true;
    app_.showPlayTournaments(true);
}

void LichessArenasPage::joinArenaHere(bool quiet) {
    const auto* account = app_.lichessAccount();
    if (!arena_play_ || !account) return;
    auto& a = *arena_play_;
    if (a.join_job.valid()) return;
    if (!account->has(lichess::SCOPE_TOURNAMENT_WRITE)) {
        a.join_pending = true;
        if (!app_.lichessLinking())
            app_.startLichessLink(lichess::scopesAdding(account->scopes, lichess::SCOPE_TOURNAMENT_WRITE),
                                  account->username);
        return;
    }
    a.quiet_join = quiet;
    a.join_asked = Clock::now();
    if (!quiet) a.error = {};
    a.join_job = std::async(std::launch::async,
                            [token = account->token, id = a.id] { return lichess::joinArena(token, id, true); });
}

void LichessArenasPage::pauseArena() {
    const auto* account = app_.lichessAccount();
    if (!arena_play_ || !account) return;
    auto& a = *arena_play_;
    a.seat.pause();  // the aids back at once, the joins stopped; a game being played goes on to its end
    if (a.pause_job.valid()) return;
    a.pause_job = std::async(std::launch::async,
                             [token = account->token, id = a.id] { return lichess::withdrawArena(token, id); });
}

void LichessArenasPage::pauseAndClose() {
    if (!arena_play_) return;
    pauseArena();  // RookStudio closes once Lichess took the pause, or after a few seconds
    arena_play_->quit_after = true;
    arena_play_->quit_asked = app_.seconds();
}

void LichessArenasPage::toggleArenaPause() {
    if (!arena_play_) return;
    if (arena_play_->seat.in())
        pauseArena();
    else if (arena_play_->seat.paused())
        joinArenaHere();
}

void LichessArenasPage::joinSwissHere() {
    const auto* account = app_.lichessAccount();
    if (!swiss_play_ || !account) return;
    auto& s = *swiss_play_;
    if (s.join_job.valid()) return;
    if (!account->has(lichess::SCOPE_TOURNAMENT_WRITE)) {
        s.join_pending = true;
        if (!app_.lichessLinking())
            app_.startLichessLink(lichess::scopesAdding(account->scopes, lichess::SCOPE_TOURNAMENT_WRITE),
                                  account->username);
        return;
    }
    s.error = {};
    s.join_job =
        std::async(std::launch::async, [token = account->token, id = s.id] { return lichess::joinSwiss(token, id); });
}

void LichessArenasPage::pauseSwiss() {
    const auto* account = app_.lichessAccount();
    if (!swiss_play_ || !account || swiss_play_->pause_job.valid()) return;
    swiss_play_->pause_job = std::async(std::launch::async, [token = account->token, id = swiss_play_->id] {
        return lichess::withdrawSwiss(token, id);
    });
}

bool LichessArenasPage::backToTournament(const std::string& swiss) {
    if (swiss_play_ && swiss == swiss_play_->id && swiss_play_->info.value)
        chooseSwiss(*swiss_play_->info.value);
    else if (arena_play_)
        chooseArena(arena_play_->id, arena_play_->name);
    else
        return false;
    return true;
}

void LichessArenasPage::drawGameRound(const std::string& swiss) {
    if (!swiss.empty() && swiss_play_ && swiss == swiss_play_->id && swiss_play_->info.value)
        ImGui::TextDisabled("%s", trf("{} · round {} of {}", swiss_play_->name, swiss_play_->info.value->round,
                                      swiss_play_->info.value->rounds)
                                      .c_str());
}

void LichessArenasPage::drawPlayTournaments() {
    const ui::TopicRegion topic(guide::topics::PlayTournaments);
    // The one chosen last, the arena or the Swiss.
    if (swiss_play_ && (tour_part_swiss_ || !arena_play_)) return drawSwissPart();
    if (arena_play_) return drawArenaPart();
    // None chosen yet: those the Board API plays, from the Lichess page's card.
    ui::beginCard("##play_tournaments");
    ImGui::PushTextWrapPos();
    heading(TR("Arenas you can play from RookStudio"));
    arena_card_.drawn   = app_.frame();  // the list asked as the card asks it
    const auto& list    = arena_card_.list;
    const auto* profile = app_.lichessProfile();
    if (!list.value) {
        ImGui::TextDisabled("%s", list.failed() ? lichess::gaveNothing(list).c_str() : TR("Asking Lichess…"));
    } else {
        int shown = 0;
        for (const auto& a : *list.value) {
            if (!lichess::playableHere(a)) continue;
            ++shown;
            ImGui::PushID(a.id.c_str());
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(a.name.c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("%s", fmt::format("· {} · {}", clockOf(a.limit_s, a.increment_s), whenText(a)).c_str());
            ImGui::SameLine();
            // Join, or the condition missing as the card says it: which one under the mouse, its page a click away.
            if (lichess::joinOffer(a, profile) == lichess::JoinOffer::ConditionNotMet) {
                ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s", TR("condition not met"));
                std::string why;
                for (const auto& c : a.conditions) {
                    const auto figure = figureOf(c, a.speed, profile);
                    why += (why.empty() ? "" : "\n") + conditionText(c, a.speed) + (figure.empty() ? "" : " " + figure);
                }
                if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                tip(why);
                if (ImGui::IsItemClicked()) chooseArena(a.id, a.name);
            } else if (primaryButton(TR("Join##arena_list"))) {
                chooseArena(a.id, a.name);
            }
            ImGui::PopID();
        }
        if (shown == 0)
            ImGui::TextDisabled("%s", TR("No rapid or classical arena now: the Lichess page lists the others."));
    }
    // The user's teams' Swiss tournaments, to come or in progress, read with team:read.
    if (app_.lichessAccount()->has(lichess::SCOPE_TEAM_READ) && arena_card_.swisses.value) {
        bool heading_said = false;
        for (const auto& sw : *arena_card_.swisses.value) {
            if (sw.state == lichess::TourState::Finished) continue;
            if (!std::exchange(heading_said, true)) {
                ImGui::Dummy(ImVec2(0, px(4)));
                heading(TR("Your teams' Swiss tournaments"));
            }
            ImGui::PushID(sw.id.c_str());
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted((sw.team_name.empty() ? sw.name : trf("{} — {}", sw.team_name, sw.name)).c_str());
            ImGui::SameLine();
            ImGui::TextDisabled(
                "%s", fmt::format("· {} · {}", clockOf(sw.limit_s, sw.increment_s),
                                  sw.state == lichess::TourState::Started ? trf("round {} of {}", sw.round, sw.rounds)
                                                                          : startText(sw.starts_at))
                          .c_str());
            ImGui::SameLine();
            if (lichess::playableHere(sw)) {
                if (primaryButton(TR("Join##swiss_list"))) chooseSwiss(sw);
            } else {
                ImGui::TextDisabled("%s", trf("{}: on lichess.org", speedName(sw.speed())).c_str());
            }
            ImGui::PopID();
        }
    }
    if (ImGui::SmallButton(TR("Lichess tournaments on the Lichess page ›"))) showCard();
    wrapped(
        TR("Blitz and bullet arenas are followed on the Lichess page and played on lichess.org: Lichess's Board API "
           "plays rapid, classical and correspondence only."),
        true);
    ImGui::PopTextWrapPos();
    ui::endCard();
}

void LichessArenasPage::drawArenaPart() {
    app_.askLichessProfile();
    auto& a             = *arena_play_;
    const auto* state   = a.state.value ? &*a.state.value : nullptr;
    const auto* profile = app_.lichessProfile();
    const auto* account = app_.lichessAccount();
    const bool enter    = keyboard::plain() && keyboard::enterPressed();
    // What it is.
    ui::inlineIcon(ui::Icon::Trophy, ui::ACCENT);
    ImGui::SameLine();
    ImGui::PushFont(ui::titleFont(), ImGui::GetStyle().FontSizeBase * 1.1f);
    ImGui::TextUnformatted((state ? state->info.name : a.name).c_str());
    ImGui::PopFont();
    if (state) {
        const auto& i = state->info;
        std::vector<std::string> parts{clockOf(i.limit_s, i.increment_s), i.rated ? TR("rated") : TR("casual")};
        if (i.berserkable) parts.push_back(TR("berserk allowed"));
        if (i.state == lichess::TourState::Started && state->seconds_to_finish >= 0)
            parts.push_back(trf("{} left", lengthText(state->seconds_to_finish)));
        else if (i.state == lichess::TourState::Created && state->seconds_to_start >= 0)
            parts.push_back(trf("starts in {}", lengthText(state->seconds_to_start)));
        parts.push_back(trn("{} player", "{} players", i.players, ui::exactCount(i.players)));
        std::string line;
        for (const auto& p : parts) line += (line.empty() ? "· " : " · ") + p;
        ImGui::SameLine();
        ui::sameLineOrWrap(ImGui::CalcTextSize(line.c_str()).x);
        ImGui::TextDisabled("%s", line.c_str());
    }
    ImGui::SameLine();
    if (ImGui::SmallButton(TR("Other tournaments"))) {
        if (a.seat.in()) pauseArena();
        dropArena();
        return;
    }
    tip(TR("The list of the arenas you can play; leaving this one pauses you in it."));
    ImGui::Dummy(ImVec2(0, px(4)));
    if (!state) {
        ImGui::TextDisabled("%s", a.state.failed() ? lichess::gaveNothing(a.state).c_str() : TR("Asking Lichess…"));
        return;
    }
    // Over: its end, its games, their review.
    if (state->info.state == lichess::TourState::Finished) return drawArenaOver(false);
    // Blitz, bullet: followed here, played on lichess.org.
    if (!lichess::playableHere(state->info)) {
        beginTinted("##arena_web", ui::ACCENT);
        ImGui::PushTextWrapPos();
        ui::inlineIcon(ui::Icon::Info, ui::ACCENT);
        ImGui::SameLine();
        coloured(trf("{} arena: Lichess's Board API does not play it", speedName(state->info.speed)), ui::ACCENT);
        wrapped(TR("A program plays no blitz nor bullet in a tournament: only the site does."), true);
        if (ImGui::SmallButton(TR("Follow it here"))) followTournament(a.id, state->info.name, false);
        ImGui::SameLine();
        if (ImGui::SmallButton(TR("Play it on lichess.org"))) SDL_OpenURL(arenaUrl(a.id).c_str());
        ImGui::PopTextWrapPos();
        endTinted();
        return;
    }
    // The first time: tournament:write.
    if (!account->has(lichess::SCOPE_TOURNAMENT_WRITE) && !drawTournamentScope(enter)) {
        dropArena();
        return;
    }
    // In it: the line, the waiting, the games; else the conditions and the rules.
    if (a.seat.in() || a.seat.paused()) {
        drawArenaLine(false);
        ImGui::PushTextWrapPos();
        if (a.seat.paused()) {
            wrapped(
                TR("Paused: Lichess pairs you no more, your points stay. The aids are back. Back in the arena: Alt+P, "
                   "or the button."),
                true);
        } else if (arenaGameOn()) {
            wrapped(TR("Your game is being played on the Board."), true);
            if (primaryButton(TR("Back to the game##arena"))) app_.backToOnlineGame();
        } else {
            const auto since =
                std::chrono::duration_cast<std::chrono::seconds>(Clock::now() - a.seat.lastJoin()).count();
            coloured(trf("Waiting for a game… joined again {} s ago", since), ui::ACCENT);
            wrapped(TR("The next game opens on the Board by itself."), true);
        }
        if (a.error.failed()) coloured(lichess::describeAlone(a.error), ui::WARN);
        // The games played from here.
        if (!a.games.empty()) {
            ImGui::TextDisabled("%s", trn("{} game played from here in this arena",
                                          "{} games played from here in this arena", a.games.size(), a.games.size())
                                          .c_str());
        }
        ImGui::PopTextWrapPos();
        if (!a.last_game.empty() && !arenaGameOn()) drawArenaGameBefore();
    } else {
        const bool team = std::any_of(state->info.conditions.begin(), state->info.conditions.end(),
                                      [](const auto& c) { return c.kind == lichess::TourCondition::Kind::Team; });
        if (ImGui::BeginTable("##arena_join", 2, ImGuiTableFlags_SizingStretchSame)) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ui::beginCard("##arena_conditions");
            ImGui::PushTextWrapPos();
            heading(TR("The arena's conditions"));
            ImGui::SameLine();
            ImGui::TextDisabled("%s", state->info.judged ? TR("· as Lichess judges them") : TR("· your figures"));
            if (state->info.conditions.empty()) wrapped(TR("None: anyone may join."), true);
            for (const auto& c : state->info.conditions) {
                const bool ok      = c.ok ? *c.ok : lichess::conditionsMet({c}, state->info.speed, profile) != false;
                const bool unknown = !c.ok && lichess::conditionsMet({c}, state->info.speed, profile) == std::nullopt;
                ui::inlineIcon(unknown ? ui::Icon::Info
                               : ok    ? ui::Icon::Check
                                       : ui::Icon::Alert,
                               unknown ? ui::MUTED
                               : ok    ? ui::GOOD
                                       : ui::WARN);
                ImGui::SameLine();
                const auto figure = figureOf(c, state->info.speed, profile);
                auto text         = conditionText(c, state->info.speed);
                if (!figure.empty()) text += " " + figure;
                ImGui::TextWrapped("%s", text.c_str());
                if (!c.refusal.empty()) {
                    ImGui::Indent(px(22));
                    coloured(c.refusal, ui::WARN);
                    ImGui::Unindent(px(22));
                }
            }
            const auto met  = lichess::conditionsMet(state->info.conditions, state->info.speed, profile);
            const bool can  = met != false && (!state->info.judged || state->info.accepted);
            const bool keys = account->has(lichess::SCOPE_TOURNAMENT_WRITE);  // else Enter asks for it
            if (!can) coloured(TR("A condition is missing: Lichess would refuse the join."), ui::WARN);
            ImGui::Dummy(ImVec2(0, px(2)));
            if (primaryButton(TR("Join##arena_play"), can && !a.join_job.valid()) ||
                (enter && keys && can && !a.join_job.valid()))
                joinArenaHere();
            // Enter asks for the permission first, while it is missing (its card says so).
            if (a.join_job.valid() || keys) {
                ImGui::SameLine();
                ImGui::TextDisabled("%s", a.join_job.valid() ? TR("Joining…") : TR("Enter"));
            }
            if (a.error.failed()) {
                coloured(lichess::describeAlone(a.error), ui::WARN);
                if (team) {
                    for (const auto& c : state->info.conditions)
                        if (c.kind == lichess::TourCondition::Kind::Team) {
                            wrapped(TR("Joining a team is done on lichess.org: RookStudio does not manage teams."),
                                    true);
                            if (ImGui::SmallButton(TR("Open the team on lichess.org")))
                                SDL_OpenURL(fmt::format("https://lichess.org/team/{}", c.team_id).c_str());
                        }
                }
            }
            ImGui::PopTextWrapPos();
            ui::endCard();
            ImGui::TableNextColumn();
            ui::beginCard("##arena_rules");
            ImGui::PushTextWrapPos();
            heading(TR("The arena's rules, in one line each"));
            wrapped(TR("A win is worth 2 points, a draw 1. After two wins in a row, the next ones count double."));
            if (state->info.berserkable)
                wrapped(TR("Berserk: half your clock, a point more if you win; before your first move."));
            wrapped(TR("A game over, the next one comes within seconds; Pause takes you out, your points stay."));
            if (ImGui::SmallButton(TR("The full rules on lichess.org")))
                SDL_OpenURL("https://lichess.org/tournament/help");
            ImGui::PopTextWrapPos();
            ui::endCard();
            ImGui::EndTable();
        }
    }
    ImGui::Dummy(ImVec2(0, px(4)));
    ImGui::PushTextWrapPos();
    wrapped(
        TR("Lichess pairs a player away from the arena's page for a minute only: RookStudio joins you again each "
           "minute while you wait for a game. Closed, it no longer does: you are paired no more, your points stay."),
        true);
    wrapped(TR("What leaves the machine: the join, the pause and your moves, with your account's token; the arena's "
               "page is asked every 15 s. Nothing else."),
            true);
    ImGui::PopTextWrapPos();
}

bool LichessArenasPage::drawTournamentScope(bool enter) {
    beginTinted("##tournament_scope", ui::ACCENT);
    ImGui::PushTextWrapPos();
    heading(TR("The first time: Lichess asks for one more permission"));
    wrapped(TR("tournament:write, to join, pause and leave a tournament. board:play, to play, is given already. "
               "Lichess's page opens; your account stays linked, the token it replaces is revoked."),
            true);
    bool keep = true;
    if (app_.lichessLinking()) {
        coloured(TR("Waiting for Lichess: allow RookStudio in the browser's page, then come back here."), ui::ACCENT);
    } else {
        if (app_.lichessLinkError().failed())
            coloured(trf("Lichess gave nothing ({}).", lichess::describe(app_.lichessLinkError())), ui::WARN);
        if (primaryButton(TR("Allow on Lichess…##tournament")) || enter) {
            const auto* account = app_.lichessAccount();
            app_.startLichessLink(lichess::scopesAdding(account->scopes, lichess::SCOPE_TOURNAMENT_WRITE),
                                  account->username);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%s", TR("Enter"));
        ImGui::SameLine();
        if (ImGui::Button(TR("Not now##tournament"))) {
            app_.onlineSettings().tournament_declined = true;
            app_.saveOnlineSettings();
            keep = false;
        }
    }
    ImGui::PopTextWrapPos();
    endTinted();
    ImGui::Dummy(ImVec2(0, px(6)));
    return keep;
}

void LichessArenasPage::drawArenaLine(bool board) {
    if (!arena_play_) return;
    auto& a           = *arena_play_;
    const auto* state = a.state.value ? &*a.state.value : nullptr;
    const ui::TopicRegion topic(guide::topics::PlayTournaments);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ColorConvertU32ToFloat4(mix(ui::CARD_BG, IM_COL32_WHITE, 0.03f)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(12), px(8)));
    ImGui::BeginChild(board ? "##arena_line_board" : "##arena_line", ImVec2(0, 0),
                      ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_AutoResizeY);
    ui::inlineIcon(ui::Icon::Trophy, ImGui::GetColorU32(ImGuiCol_Text));
    ImGui::SameLine();
    ImGui::PushFont(ui::titleFont(), ImGui::GetStyle().FontSizeBase);
    ImGui::TextUnformatted((state ? state->info.name : a.name).c_str());
    ImGui::PopFont();
    std::vector<std::string> parts;
    if (a.me && state)
        parts.push_back(trf("rank {} of {}", ui::exactCount(a.me->rank), ui::exactCount(state->info.players)));
    else if (state && state->me)
        parts.push_back(trf("rank {} of {}", ui::exactCount(state->me->rank), ui::exactCount(state->info.players)));
    if (a.me) parts.push_back(trn("{} point", "{} points", a.me->score, a.me->score));
    std::string line;
    for (const auto& p : parts) line += " · " + p;
    if (!line.empty()) {
        ImGui::SameLine(0, 0);
        ImGui::TextUnformatted(line.c_str());
    }
    if (a.me && a.me->fire && (!state || state->streaks)) {
        ImGui::SameLine(0, 0);
        ImGui::TextUnformatted(" · ");
        ImGui::SameLine(0, 0);
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s", TR("streak: the next win counts double"));
    }
    if (const auto left = arenaTimeLeft(); !left.empty()) {
        ImGui::SameLine(0, 0);
        ImGui::TextUnformatted((" · " + left).c_str());
    }
    if (a.seat.isOver()) {
        ImGui::SameLine(0, 0);
        ImGui::TextUnformatted((" · " + std::string(TR("over"))).c_str());
    } else {
        const char* label = a.seat.paused() ? TR("Back in the arena · Alt+P") : TR("Pause · Alt+P");
        ui::sameLineOrWrap(label);
        if (ImGui::SmallButton(fmt::format("{}###arena_pause{}", label, board ? 1 : 0).c_str())) toggleArenaPause();
        tip(a.seat.paused() ? TR("Joins the arena again: Lichess pairs you, the aids are locked again.")
                            : TR("Takes you out of the arena after the game being played: your points stay, the aids "
                                 "come back."));
    }
    if (Clock::now() < a.waits_until) {
        const auto left = std::chrono::duration_cast<std::chrono::seconds>(a.waits_until - Clock::now()).count() + 1;
        ImGui::PushTextWrapPos();
        coloured(trf("Too many requests (429): the standing waits a minute ({} s left); your games go on.", left),
                 ui::WARN);
        ImGui::PopTextWrapPos();
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

void LichessArenasPage::drawArenaGameBefore() {
    if (!arena_play_ || arena_play_->last_game.empty()) return;
    auto& a = *arena_play_;
    beginTinted("##arena_before", a.last_result == "won" ? ui::GOOD : a.last_result == "draw" ? ui::ACCENT : ui::WARN);
    ImGui::PushTextWrapPos();
    ImGui::TextDisabled("%s", TR("The game before, at its end:"));
    std::string title;
    if (a.last_result == "won")
        title = trn("Won in {} move", "Won in {} moves", a.last_moves, a.last_moves);
    else if (a.last_result == "lost")
        title = trn("Lost in {} move", "Lost in {} moves", a.last_moves, a.last_moves);
    else if (a.last_result == "nostart")
        title = TR("Lost: your first move did not come in time");
    else
        title = trn("Drawn in {} move", "Drawn in {} moves", a.last_moves, a.last_moves);
    if (a.last_points >= 0) {
        title += " · " + trn("+{} point", "+{} points", a.last_points, a.last_points);
        if (a.streak_starts)
            title += " · " + std::string(TR("the streak starts"));
        else if (a.last_doubled)
            title += " · " + std::string(TR("doubled by the streak"));
    } else if (!a.seat.isOver()) {  // the arena over, its standing is not read again
        title += " · " + std::string(TR("its points: asking Lichess…"));
    }
    coloured(title, a.last_result == "won" ? ui::GOOD : a.last_result == "draw" ? ui::ACCENT : ui::WARN);
    const auto* account = app_.lichessAccount();
    const auto base     = account ? "Lichess · " + account->username : std::string("Lichess");
    wrapped(a.seat.in() ? trf("The next game in a few seconds. It is kept in “{}”, its review offered at the arena's "
                              "end.",
                              base)
                        : trf("It is kept in “{}”, its review offered at the arena's end.", base),
            true);
    ImGui::PopTextWrapPos();
    endTinted();
}

void LichessArenasPage::drawArenaOver(bool home) {
    if (!arena_play_ || !arena_play_->state.value) return;
    auto& a             = *arena_play_;
    const auto& s       = *a.state.value;
    const auto* account = app_.lichessAccount();
    const auto base     = account ? "Lichess · " + account->username : std::string("Lichess");
    int rank = a.me ? a.me->rank : s.me ? s.me->rank : 0, points = a.me ? a.me->score : 0;
    const auto held = app_.heldGames(a.id);  // the games of the arena kept, their review held until its end
    const auto title =
        rank > 0 ? trn("The arena is over: rank {} of {}, {} point", "The arena is over: rank {} of {}, {} points",
                       points, ui::exactCount(rank), ui::exactCount(s.info.players), points)
                 : std::string(TR("The arena is over"));
    if (home) {
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::GOOD), "%s", title.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton(TR("See it##arena_over"))) chooseArena(a.id, a.name);
        return;
    }
    const ui::Tinted tinted("##arena_over", ui::GOOD, TINT);
    const ui::TextWrap wrap;
    ui::inlineIcon(ui::Icon::Trophy, ui::GOOD);
    ImGui::SameLine();
    coloured(title, ui::GOOD);
    const auto games = std::max(a.games.size(), held);
    wrapped(games > 0 ? trn("Your {} game is in “{}”.", "Your {} games are in “{}”.", games, games, base)
                      : std::string(TR("You played no game of it from here.")),
            true);
    if (held > 0 && app_.lastHeldArrived(a.id)) {
        if (primaryButton(TR("Review the last game"))) app_.reviewLastHeld(a.id);
        ImGui::SameLine();
    }
    if (games > 0 && ImGui::Button(TR("Its games in the list"))) {
        auto& sh = app_.shelf();
        for (std::size_t i = 0; i < sh.entries().size(); ++i)
            if (!sh.entries()[i].source && lower(sh.entries()[i].name) == lower(base))
                app_.showDatabaseGames(i, s.info.name);
        app_.letGoHeld(a.id);
    }
    ImGui::SameLine();
    if (ImGui::Button(TR("Close##arena_over"))) {
        app_.letGoHeld(a.id);
        dropArena();
        return;
    }
}

void LichessArenasPage::drawSwissPart() {
    auto& s             = *swiss_play_;
    const auto* info    = s.info.value ? &*s.info.value : nullptr;
    const auto* account = app_.lichessAccount();
    const bool enter    = keyboard::plain() && keyboard::enterPressed();
    ui::inlineIcon(ui::Icon::Users, ui::ACCENT);
    ImGui::SameLine();
    ImGui::PushFont(ui::titleFont(), ImGui::GetStyle().FontSizeBase * 1.1f);
    ImGui::TextUnformatted((s.team_name.empty() ? s.name : trf("{} — {}", s.team_name, s.name)).c_str());
    ImGui::PopFont();
    if (info) {
        std::vector<std::string> parts;
        if (info->state == lichess::TourState::Created)
            parts.push_back(trn("{} round", "{} rounds", info->rounds, info->rounds));
        else
            parts.push_back(trf("round {} of {}", info->round, info->rounds));
        parts.push_back(clockOf(info->limit_s, info->increment_s));
        parts.push_back(trn("{} player", "{} players", info->players, ui::exactCount(info->players)));
        parts.push_back(info->rated ? TR("rated") : TR("casual"));
        parts.push_back(TR("reserved for the team"));
        std::string line;
        for (const auto& p : parts) line += (line.empty() ? "· " : " · ") + p;
        ImGui::SameLine();
        ui::sameLineOrWrap(ImGui::CalcTextSize(line.c_str()).x);
        ImGui::TextDisabled("%s", line.c_str());
    }
    ImGui::SameLine();
    if (ImGui::SmallButton(TR("Other tournaments##swiss"))) {
        dropSwiss();
        return;
    }
    if (arena_play_ && (arena_play_->seat.in() || arena_play_->seat.paused())) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", (arena_play_->seat.in() ? trf("You are in the arena “{}”.", arena_play_->name)
                                                          : trf("You are paused in the arena “{}”.", arena_play_->name))
                                      .c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton(TR("Back to the arena##swiss"))) {
            tour_part_swiss_ = false;
            return;
        }
    }
    ImGui::Dummy(ImVec2(0, px(4)));
    if (!info) {
        ImGui::TextDisabled("%s", s.info.failed() ? lichess::gaveNothing(s.info).c_str() : TR("Asking Lichess…"));
        return;
    }
    const bool over = info->state == lichess::TourState::Finished;
    if (!over && !s.joined && !account->has(lichess::SCOPE_TOURNAMENT_WRITE) && !drawTournamentScope(enter)) {
        dropSwiss();
        return;
    }
    const bool scoped = account->has(lichess::SCOPE_TOURNAMENT_WRITE);
    if (!lichess::playableHere(*info) && !over) {
        beginTinted("##swiss_web", ui::ACCENT);
        ImGui::PushTextWrapPos();
        coloured(trf("{}: Lichess's Board API does not play it in a tournament", speedName(info->speed())), ui::ACCENT);
        if (ImGui::SmallButton(TR("Play it on lichess.org##swiss"))) SDL_OpenURL(swissUrl(s.id).c_str());
        ImGui::PopTextWrapPos();
        endTinted();
        return;
    }
    if (ImGui::BeginTable("##swiss_cols", 2, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        // The standing after the last round.
        ui::beginCard("##swiss_standing_play");
        ImGui::TextUnformatted(TR("The standing"));
        ImGui::SameLine();
        ImGui::TextDisabled(
            "%s", s.rows_round > 0 ? trf("· after round {}", s.rows_round).c_str() : TR("· before the first round"));
        ImGui::PushTextWrapPos(-1.0f);
        if (s.rows.value && !s.rows.value->empty() &&
            ImGui::BeginTable("##swiss_rows", 5, ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn(TR("Pl."), ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableSetupColumn(TR("Player"), ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn(TR("Pts"), ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableSetupColumn(TR("Tie-break"), ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableSetupColumn(TR("Perf."), ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableHeadersRow();
            const auto me = account ? lower(account->username) : std::string();
            std::size_t n = 0;
            for (const auto& r : *s.rows.value) {
                const bool mine = lower(r.name) == me;
                if (++n > 8 && !mine) continue;
                ImGui::TableNextRow();
                if (mine)
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,
                                           ImGui::ColorConvertFloat4ToU32(
                                               ImGui::ColorConvertU32ToFloat4(mix(ui::CARD_BG, ui::ACCENT, 0.12f))));
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%d", r.rank);
                ImGui::TableNextColumn();
                cutText(r.rating > 0 ? fmt::format("{} {}", mine ? std::string(TR("You")) : r.name, r.rating)
                                     : (mine ? std::string(TR("You")) : r.name),
                        ImGui::GetContentRegionAvail().x);
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(home::halfPoints(r.points).c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(fmt::format("{:.2f}", r.tie_break).c_str());
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", r.performance > 0 ? std::to_string(r.performance).c_str() : "–");
            }
            ImGui::EndTable();
        } else {
            ImGui::TextDisabled("%s",
                                s.rows.failed() ? lichess::gaveNothing(s.rows).c_str() : TR("No game played yet."));
        }
        ImGui::PopTextWrapPos();
        ui::endCard();
        ImGui::TableNextColumn();
        // The user's games by round.
        ui::beginCard("##swiss_mine");
        ImGui::PushTextWrapPos();
        heading(TR("Your games"));
        const auto me    = account ? account->username : std::string();
        const auto* mine = s.trf ? s.trf->player(me) : nullptr;
        bool any         = false;
        if (mine)
            for (std::size_t r = 0; r < mine->rounds.size(); ++r) {
                const auto& round = mine->rounds[r];
                if (round.code == 'Z') continue;
                const auto* them = round.opponent > 0 ? s.trf->number(round.opponent) : nullptr;
                // The opponent as the standing writes the name (the TRF has the account's id, lowercase).
                std::string name = them ? them->name : std::string();
                if (s.rows.value)
                    for (const auto& row : *s.rows.value)
                        if (lower(row.name) == lower(name)) name = row.name;
                const int n = static_cast<int>(r) + 1;
                std::string text;
                if (them && round.code == '1')
                    text = trf("R{} · against {} · won", n, name);
                else if (them && round.code == '=')
                    text = trf("R{} · against {} · drawn", n, name);
                else if (them && round.code == '0')
                    text = trf("R{} · against {} · lost", n, name);
                else if (them && round.code == '+')
                    text = trf("R{} · against {} · won by forfeit", n, name);
                else if (them)
                    text = trf("R{} · against {} · lost by forfeit", n, name);
                else if (round.code == 'U')
                    text = trf("R{} · a bye, 1 point", n);
                else if (round.code == 'H')
                    text = trf("R{} · joined late, ½ point", n);
                else
                    text = trf("R{} · absent", n);
                ImGui::TextUnformatted(text.c_str());
                any = true;
            }
        if (const auto online_game = app_.onlineGame();
            online_game && online_game->state.swiss == s.id && !online_game->ended) {
            const auto& og = *online_game;
            ImGui::TextColored(
                ImGui::ColorConvertU32ToFloat4(ui::ACCENT), "%s",
                trf("R{} · against {} · being played, on the Board", info->round, app_.onlineOpponent()).c_str());
            if (!og.shown) {
                ImGui::SameLine();
                if (ImGui::SmallButton(TR("Back to the game##swiss"))) app_.backToOnlineGame();
            }
            any = true;
        }
        if (!any) ImGui::TextDisabled("%s", s.joined ? TR("None yet.") : TR("You have not joined it."));
        ImGui::PopTextWrapPos();
        ui::endCard();
        ImGui::Dummy(ImVec2(0, px(6)));
        // The next round, the joining, the end.
        if (over) {
            beginTinted("##swiss_over", ui::GOOD);
            ImGui::PushTextWrapPos();
            coloured(TR("The Swiss is over"), ui::GOOD);
            if (!s.kept_in.empty()) {
                wrapped(trf("Kept in “{}”: its games and its standing, its crosstable a click away.", s.kept_in), true);
                if (ImGui::SmallButton(TR("Open its crosstable"))) {
                    auto& sh = app_.shelf();
                    for (std::size_t i = 0; i < sh.entries().size(); ++i)
                        if (!sh.entries()[i].source && sh.entries()[i].name == s.kept_in)
                            app_.openCrosstable(i, s.name, s.kept_in);
                }
            } else if (s.keep_job.valid()) {
                wrapped(TR("Its games and its standing are coming from Lichess…"), true);
            } else {
                if (s.keep_error.failed())
                    coloured(trf("It could not be kept: {}", lichess::describe(s.keep_error)), ui::WARN);
                if (primaryButton(TR("Keep it in a database…"))) s.keep_menu = true;
                tip(TR(
                    "Its games, each with its round, and its standing (its TRF file): the database's crosstable then "
                    "reads it, byes and late joins included."));
                drawSwissKeepMenu();
            }
            ImGui::PopTextWrapPos();
            endTinted();
        } else {
            beginTinted("##swiss_next", ui::ACCENT);
            ImGui::PushTextWrapPos();
            if (info->next_round_at > 0) {
                const auto t = static_cast<std::time_t>(info->next_round_at / 1000);
                std::tm tm{};
#ifdef _WIN32
                localtime_s(&tm, &t);
#else
                localtime_r(&t, &tm);
#endif
                ImGui::PushFont(ui::titleFont(), ImGui::GetStyle().FontSizeBase);
                ImGui::TextUnformatted(trf("Round {} at {:02}:{:02}", info->round + 1, tm.tm_hour, tm.tm_min).c_str());
                ImGui::PopFont();
            }
            wrapped(
                TR("RookStudio tells you two minutes before, by a notice at the window's corner. A game whose first "
                   "move you do not play in time is lost."),
                true);
            if (!s.joined) {
                if (primaryButton(TR("Join##swiss_play"), !s.join_job.valid()) ||
                    (enter && scoped && !s.join_job.valid()))
                    joinSwissHere();
                if (s.join_job.valid() || scoped) {  // else Enter asks for the permission first
                    ImGui::SameLine();
                    ImGui::TextDisabled("%s", s.join_job.valid() ? TR("Joining…") : TR("Enter"));
                }
            } else {
                ImGui::TextUnformatted(TR("You are in it."));
                ImGui::SameLine();
                if (ImGui::SmallButton(TR("Leave it"))) pauseSwiss();
                tip(TR("Lichess pairs you no more from the next round; your points stay."));
            }
            if (s.error.failed()) {
                coloured(lichess::describeAlone(s.error), ui::WARN);
                if (!s.team_id.empty()) {
                    wrapped(TR("Joining a team is done on lichess.org: RookStudio does not manage teams."), true);
                    if (ImGui::SmallButton(TR("Open the team on lichess.org##swiss")))
                        SDL_OpenURL(fmt::format("https://lichess.org/team/{}", s.team_id).c_str());
                }
            }
            ImGui::PopTextWrapPos();
            endTinted();
        }
        ImGui::EndTable();
    }
    ImGui::PushTextWrapPos();
    wrapped(
        TR("The round's games in the mosaic: those of its first eight, found by player each round (a Swiss does not "
           "list its games in progress)."),
        true);
    if (info->state == lichess::TourState::Started && ImGui::SmallButton(TR("Its games in the mosaic")))
        followTournament(s.id, s.name, true);
    ImGui::PopTextWrapPos();
}

void LichessArenasPage::keepSwiss(const std::string& folder, const std::string& name) {
    if (!swiss_play_ || swiss_play_->keep_job.valid()) return;
    auto& s           = *swiss_play_;
    auto& sh          = app_.shelf();
    std::size_t index = sh.entries().size();
    try {
        for (std::size_t i = 0; i < sh.entries().size(); ++i)
            if (!sh.entries()[i].source && (folder.empty() ? sh.entries()[i].name == name
                                                           : sh.entries()[i].folder == std::filesystem::path(folder)))
                index = i;
        if (index == sh.entries().size()) index = sh.create(name);
    } catch (const std::exception& e) {
        s.keep_error = lichess::Failure::own(e.what());
        return;
    }
    const auto db = sh.open(index);
    if (!db || db->readOnly()) {
        s.keep_error = lichess::Failure::own(trf("“{}” cannot be written", sh.entries()[index].name));
        return;
    }
    app_.lichessSettings().swiss_keep_base = sh.entries()[index].folder.string();
    app_.saveLichessSettings();
    s.kept_folder        = sh.entries()[index].folder.string();
    const auto base      = sh.entries()[index].name;
    const auto* account  = app_.lichessAccount();
    const auto token     = account ? account->token : std::string();
    const int64_t starts = s.info.value ? s.info.value->starts_at : 0;
    // Its games and its TRF asked, read and kept; the rounds without a game go to its crosstable's settings once back.
    s.keep_job   = std::async(std::launch::async, [db, base, index, id = s.id, name = s.name, token, starts] {
        SwissKept out;
        out.index        = index;
        const auto games = lichess::swissGames(id, token);
        if (!games.value) {
            out.failure = games;
            return out;
        }
        const auto trf_text = lichess::swissTrf(id);
        if (!trf_text.value) {
            out.failure = trf_text;
            return out;
        }
        auto kept = lichess::keepSwiss(lichess::readTrf(*trf_text.value), *games.value, name, id,
                                       starts > 0 ? lichess::swissStartText(starts) : std::string());
        try {
            out.games = kept.games.size();
            (void)db->append(std::move(kept.games), false);
        } catch (const std::exception& e) {
            out.failure = lichess::Failure::own(e.what());
            return out;
        }
        out.base = base;
        out.key  = kept.key;
        out.hand = std::move(kept.hand);
        return out;
    });
    s.kept_games = 0;
}

void LichessArenasPage::drawSwissKeepMenu() {
    if (!swiss_play_) return;
    constexpr const char* kId = "##swiss_keep";
    if (swiss_play_->keep_menu) {
        swiss_play_->keep_menu = false;
        ImGui::OpenPopup(kId);
    }
    if (!ImGui::BeginPopup(kId)) return;
    auto& sh               = app_.shelf();
    const std::string name = TR("Lichess tournaments");  // as the Broadcasts database is named
    ImGui::TextDisabled("%s", TR("Keep the Swiss in"));
    std::vector<std::size_t> order;
    for (std::size_t i = 0; i < sh.entries().size(); ++i)
        if (!sh.entries()[i].source) order.push_back(i);
    std::stable_sort(order.begin(), order.end(), [&](std::size_t x, std::size_t y) {
        const auto rank = [&](std::size_t i) {
            return sh.entries()[i].folder == std::filesystem::path(app_.lichessSettings().swiss_keep_base) ? 0
                   : sh.entries()[i].name == name                                                          ? 1
                                                                                                           : 2;
        };
        return rank(x) < rank(y);
    });
    std::optional<std::pair<std::string, std::string>> chosen;
    for (const auto i : order) {
        const auto& e = sh.entries()[i];
        ImGui::PushID(static_cast<int>(i));
        if (ImGui::Selectable(e.name.c_str(), false, 0, ImVec2(px(300), 0))) chosen = {e.folder.string(), e.name};
        ImGui::PopID();
    }
    if (std::none_of(order.begin(), order.end(), [&](std::size_t i) { return sh.entries()[i].name == name; }))
        if (ImGui::Selectable(trf("“{}”, a new database", name).c_str(), false, 0, ImVec2(px(300), 0)))
            chosen = {std::string(), name};
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) ImGui::CloseCurrentPopup();
    if (chosen) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
    if (chosen) keepSwiss(chosen->first, chosen->second);
}

// ---------------------------------------------------------------------------
// Elsewhere: the Home page, the window's corner

void LichessArenasPage::drawHome() {
    if (arena_play_ && (arena_play_->seat.in() || arena_play_->seat.paused())) {
        auto& a = *arena_play_;
        ui::beginCard("chess_arena");
        ImGui::PushTextWrapPos();
        ui::inlineIcon(a.seat.in() ? ui::Icon::Lock : ui::Icon::Trophy, ui::ACCENT);
        ImGui::SameLine();
        std::string line = a.seat.in() ? trf("In the arena: {}", a.name) : trf("Paused in the arena: {}", a.name);
        if (a.me && a.state.value)
            line +=
                " · " + trf("rank {} of {}", ui::exactCount(a.me->rank), ui::exactCount(a.state.value->info.players));
        if (a.me) line += " · " + trn("{} point", "{} points", a.me->score, a.me->score);
        if (const auto left = arenaTimeLeft(); !left.empty()) line += " · " + trf("{} left", left);
        ImGui::TextUnformatted(line.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton(arenaGameOn() ? TR("Back to the game##home_arena") : TR("Back to the arena##home_arena"))) {
            if (arenaGameOn())
                app_.backToOnlineGame();
            else
                chooseArena(a.id, a.name);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton(a.seat.paused() ? TR("Back in the arena · Alt+P##home") : TR("Pause · Alt+P##home")))
            toggleArenaPause();
        if (a.seat.in())
            ImGui::TextDisabled("%s", TR("The aids stay locked while you are in the arena, between two games too."));
        ImGui::PopTextWrapPos();
        ui::endCard();
        ImGui::Dummy(ImVec2(0, px(6)));
    } else if (arena_play_ && arena_play_->seat.isOver() && !arena_play_->over_seen) {
        ui::beginCard("chess_arena_over");
        drawArenaOver(true);
        ui::endCard();
        ImGui::Dummy(ImVec2(0, px(6)));
    }
    if (swiss_play_ && swiss_play_->joined && swiss_play_->info.value &&
        swiss_play_->info.value->state != lichess::TourState::Finished) {
        const auto& s = *swiss_play_;
        ui::beginCard("chess_swiss");
        ImGui::PushTextWrapPos();
        ui::inlineIcon(ui::Icon::Users, ui::ACCENT);
        ImGui::SameLine();
        const auto& info = *s.info.value;
        if (info.state == lichess::TourState::Created && info.next_round_at > 0) {
            const auto t = static_cast<std::time_t>(info.next_round_at / 1000);
            std::tm tm{};
#ifdef _WIN32
            localtime_s(&tm, &t);
#else
            localtime_r(&t, &tm);
#endif
            ImGui::TextUnformatted(
                trf("In the Swiss: {} · its first round at {:02}:{:02}", s.name, tm.tm_hour, tm.tm_min).c_str());
        } else {
            ImGui::TextUnformatted(trf("In the Swiss: {} · round {} of {}", s.name, info.round, info.rounds).c_str());
        }
        ImGui::SameLine();
        if (ImGui::SmallButton(TR("See it##home_swiss"))) chooseSwiss(*s.info.value);
        ImGui::PopTextWrapPos();
        ui::endCard();
        ImGui::Dummy(ImVec2(0, px(6)));
    }
}

void LichessArenasPage::drawSwissNotice() {
    if (!swiss_play_ || !swiss_play_->joined || !swiss_play_->info.value) return;
    auto& s          = *swiss_play_;
    const auto& info = *s.info.value;
    const int next   = info.round + 1;
    if (info.state == lichess::TourState::Finished || s.noticed_round >= next) return;
    if (!lichess::roundNoticeDue(info.next_round_at, nowMillis())) return;
    if (app_.liveGameOn() && !holdsAids()) return;  // during a live game, nothing at the corner
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
    if (ImGui::Begin("##swiss_notice", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                         ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDocking)) {
        ImGui::PushTextWrapPos(px(400));
        ui::inlineIcon(ui::Icon::Users, ui::ACCENT);
        ImGui::SameLine();
        const auto left = std::max<int64_t>(0, (info.next_round_at - nowMillis()) / 1000);
        heading(trf("Round {} of “{}” in {}:{:02}", next, s.name, left / 60, left % 60));
        ImGui::TextDisabled("%s",
                            TR("Your game opens on the Board by itself. A first move not played in time loses it."));
        if (ImGui::Button(TR("See it##swiss_notice"))) chooseSwiss(info);
        ImGui::SameLine();
        if (ImGui::SmallButton(TR("Later##swiss_notice"))) s.noticed_round = next;
        ImGui::PopTextWrapPos();
    }
    ImGui::End();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);
}

// ---------------------------------------------------------------------------
// Ctrl+K and --focus

void LichessArenasPage::addCommands(const PaletteAdd& add, const std::string& lichess_view,
                                    const std::string& play_view) {
    // The card; an arena of its list followed or joined; the Play page's part; in an arena, the pause and berserk; the
    // user's teams' Swiss tournaments; a finished Swiss kept.
    const auto* account = app_.lichessAccount();
    add(TR("Lichess tournaments: arenas and Swiss tournaments"), lichess_view, [this] {
        showCard();  // the page itself, where its card is
    });
    add(TR("Play an arena or a Swiss tournament on Lichess"), play_view, [this] { app_.showPlayTournaments(true); });
    if (arena_card_.list.value)
        for (const auto& a : *arena_card_.list.value) {
            add(trf("Follow the arena: {}", a.name), "Lichess",
                [this, id = a.id, name = a.name] { followTournament(id, name, false); });
            if (lichess::playableHere(a) && account)
                add(trf("Join the arena: {}", a.name), TR("Play"),
                    [this, id = a.id, name = a.name] { chooseArena(id, name); });
        }
    if (tour_follow_)
        add(trf("The tournament followed in the mosaic: {}", tour_follow_->name), "Lichess",
            [this] { app_.openMosaic(tour_follow_->key); });
    if (arena_play_ && (arena_play_->seat.in() || arena_play_->seat.paused())) {
        add(arena_play_->seat.in() ? TR("Pause in the arena") : TR("Back in the arena"), "Alt+P",
            [this] { toggleArenaPause(); });
        add(TR("Back to the arena"), TR("Play"), [this] { chooseArena(arena_play_->id, arena_play_->name); });
    }
    const auto online_game = app_.onlineGame();
    if (online_game && arena_play_ && online_game->state.tournament == arena_play_->id && arena_play_->state.value &&
        arena_play_->state.value->info.berserkable &&
        lichess::berserkAllowed(true, online_game->state, online_game->white))
        add(TR("Berserk"), TR("Play"), [this] {
            app_.onlineCall(TR("Berserk"), [id = app_.onlineGame()->id](const std::string& token) {
                return lichess::berserk(token, id);
            });
        });
    if (account && !account->has(lichess::SCOPE_TEAM_READ))
        add(TR("Read my teams' Swiss tournaments…"), "Lichess", [this] {
            app_.showLichessPage();
            app_.askScope(lichess::SCOPE_TEAM_READ);
        });
    if (swiss_play_ && swiss_play_->info.value && swiss_play_->info.value->state == lichess::TourState::Finished &&
        swiss_play_->kept_in.empty() && !swiss_play_->keep_job.valid())
        add(TR("Keep the Swiss in a database…"), TR("Play"), [this] {
            chooseSwiss(*swiss_play_->info.value);
            swiss_play_->keep_menu = true;
        });
}

void LichessArenasPage::addFocusWords(FocusWords& words, const std::string& w) {
    words.on({"lichess-tournaments"}, [&] {  // the Lichess page scrolled to its card of tournaments (#95)
        showCard();
    });
    words.on({"arena-mosaic=*", "swiss-mosaic=*", "play-arena=*", "play-swiss=*"}, [&] {
        // arena-mosaic=ID[@NAME], swiss-mosaic=ID[@NAME]: a tournament followed in the mosaic;
        // play-arena=ID[@NAME], play-swiss=ID[@NAME]: the Play page's Tournaments part on it (#95), with
        // lichess-api= before them.
        const auto eq   = w.find('=');
        const auto kind = w.substr(0, eq);
        const auto spec = w.substr(eq + 1);
        const auto at   = spec.find('@');
        const auto id   = spec.substr(0, at);
        const auto name = at == std::string::npos ? std::string() : spec.substr(at + 1);
        if (kind == "arena-mosaic" || kind == "swiss-mosaic") {
            followTournament(id, name, kind == "swiss-mosaic");
        } else if (kind == "play-arena") {
            chooseArena(id, name);
        } else {
            lichess::SwissInfo s;
            s.id   = id;
            s.name = name;
            chooseSwiss(s);
            swiss_play_->info = {};  // asked of Lichess
        }
    });
    words.on({"play-tournaments"}, [&] {  // the Play page's "On Lichess › Tournaments" part (#95)
        app_.showPlayTournaments(false);
    });
}

}  // namespace fcg
