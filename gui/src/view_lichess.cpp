// The Chess space's Lichess page (mockup M4, board 1): the account linked or not, a player's games kept in a database,
// the official broadcasts, and the online services the analysis may ask, each decided here. Nothing on it is needed:
// the program works without an account and without the Internet (R15); what leaves the machine is said where it is
// decided (R16).

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <string>

#include <SDL3/SDL.h>
#include <fmt/include/fmt/format.h>
#include <imgui.h>
#include <tour/util.hpp>

#include "app.hpp"
#include "console.hpp"
#include "guide.hpp"
#include "https.hpp"
#include "i18n.hpp"
#include "lichess_account.hpp"
#include "lichess_arenas.hpp"
#include "lichess_arenas_page.hpp"
#include "lichess_bot_page.hpp"
#include "lichess_broadcasts.hpp"
#include "lichess_games.hpp"
#include "lichess_tv_page.hpp"
#include "storage.hpp"
#include "ui.hpp"
#include "widgets.hpp"

namespace fcg {

namespace {

float px(float v) { return v * ImGui::GetFontSize() / 16.0f; }

std::string lower(std::string text) {
    for (auto& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

// A day as the page says it: "18 November 2026".
std::string dateText(int64_t seconds) {
    const auto t = static_cast<std::time_t>(seconds);
    const auto tm = localTime(t);
    char text[32];
    std::strftime(text, sizeof(text), "%Y-%m-%d", &tm);
    return text;
}

}  // namespace

void App::loadLichess() {
    lichess_account_ = lichess::loadAccount(storage_.root());
    if (const auto json = readJson<nlohmann::json>(storage_.root() / "lichess.json")) {
        try {
            lichess_ = json->get<LichessSettings>();
        } catch (const std::exception&) {
            lichess_ = LichessSettings{};
        }
    }
}

void App::saveLichessSettings() { (void)writeJson(storage_.root() / "lichess.json", lichess_); }

std::vector<std::string> App::ownLichessIds() {
    std::vector<std::string> out;
    // The user's bot (#85) is theirs too: its module reads its account the first time it is asked.
    const lichess::Account* const accounts[] = {lichess_account_ ? &*lichess_account_ : nullptr, bot_->account()};
    for (const auto* a : accounts) {
        if (!a) continue;
        const auto id = lower(a->username);
        if (!id.empty() && std::find(out.begin(), out.end(), id) == out.end()) out.push_back(id);
    }
    return out;
}

void App::revokeToken(std::string token) {
    jobs_.park(std::async(std::launch::async, [token = std::move(token)] { return lichess::revoke(token); }),
               [](const lichess::Failure& failure) {
                   if (failure.failed())
                       Console::instance().add(
                           trf("Lichess: the token replaced could not be revoked: {}", lichess::describe(failure)),
                           true);
               });
}

void App::pollLichess() {
    // The link: the browser said yes or no, or the wait ended.
    if (lichess_link_ && isReady(lichess_link_->result)) {
        auto answer         = lichess_link_->result.get();
        const auto previous = std::move(lichess_link_->previous_token);
        const auto follow   = std::move(lichess_link_->then_follow);
        lichess_link_.reset();
        if (answer.value) {
            try {
                lichess::saveAccount(storage_.root(), *answer.value);
                lichess_account_ = *answer.value;
                lichess_error_   = {};
                if (lichess_.player.empty()) lichess_.player = answer.value->username;
                saveLichessSettings();
                if (window_) SDL_RaiseWindow(window_);  // back in front of the browser
                // A link made again: the token it replaces revoked (#45).
                if (!previous.empty() && previous != answer.value->token) revokeToken(previous);
                // Allowed to read broadcasts now: the rounds followed without it followed again with it (#44).
                if (lichess_account_->has(lichess::SCOPE_STUDY_READ)) {
                    for (auto& f : followed_rounds_) {
                        if (f.with_token) continue;
                        f.follower->stop();
                        f.follower = std::make_unique<lichess::RoundFollower>(
                            f.whole ? f.entry.tour.id : f.entry.round.id, lichess_account_->token, f.whole);
                        f.with_token = true;
                    }
                    lichess_scope_ask_.reset();
                }
                if (follow) followBroadcast(*follow);
            } catch (const std::exception& e) {
                lichess_error_ = lichess::Failure::own(e.what());
            }
        } else if (answer.net != lichess::NetError::Cancelled) {
            if (lichess_scope_ask_)
                lichess_scope_ask_->error = answer;
            else
                lichess_error_ = answer;
        }
    }
    if (lichess_unlink_.valid() && isReady(lichess_unlink_)) {
        const auto failure = lichess_unlink_.get();
        if (failure.failed())
            Console::instance().add(trf("Lichess: the token could not be revoked: {}", lichess::describe(failure)),
                                    true);
    }
    // A player's games: the sync ends.
    if (lichess_sync_ && isReady(lichess_sync_->done)) {
        auto report          = lichess_sync_->done.get();
        const auto index     = lichess_sync_->index;
        lichess_sync_report_ = report;
        lichess_sync_.reset();
        if (report.added > 0 && index < shelf().entries().size()) shelf().rebuildTree(index);
    }
    // The broadcasts: asked once a page shows them, again after ten minutes.
    if (lichess_broadcasts_job_.valid() && isReady(lichess_broadcasts_job_)) {
        lichess_broadcasts_      = lichess_broadcasts_job_.get();
        lichess_broadcasts_time_ = std::chrono::steady_clock::now();
    }
    tv_->poll();            // Lichess TV: its card, a channel's games, the Board's game, those kept (#86)
    arenas_->poll();        // Lichess's tournaments: the card, one followed, one played, a Swiss (#95)
    pollBroadcastBoards();  // the mosaic's games, their engines, the game the board follows
}

std::optional<std::size_t> App::lichessBase() {
    if (lichess_.player.empty()) return std::nullopt;
    const auto name     = "Lichess · " + lichess_.player;
    const auto& entries = shelf().entries();
    for (std::size_t i = 0; i < entries.size(); ++i)
        if (!entries[i].source && entries[i].name == name) return i;
    return std::nullopt;
}

lichess::SyncFilters App::lichessFilters() const {
    lichess::SyncFilters f;
    f.perf_types = lichess::perfTypes(lichess_.sync_speeds);
    if (lichess_.sync_since == 1) {
        const auto now =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
                .count();
        f.since = now - int64_t{365} * 24 * 3600 * 1000;
    } else if (lichess_.sync_since == 2 && lichess_.sync_date.size() == 10) {
        auto date = lichess_.sync_date;  // 2024-01-31, as Lichess's tags write it: 2024.01.31
        std::replace(date.begin(), date.end(), '-', '.');
        if (const auto at = lichess::startOf(date, "00:00:00")) f.since = *at;
    }
    return f;
}

void App::startLichessSync() {
    if (lichess_sync_ || lichess_.player.empty()) return;
    auto& s           = shelf();
    const auto name   = "Lichess · " + lichess_.player;
    std::size_t index = s.entries().size();
    try {
        const auto base = lichessBase();
        index           = base ? *base : s.create(name);
    } catch (const std::exception& e) {
        lichess_sync_report_        = lichess::SyncReport{};
        lichess_sync_report_->error = e.what();
        return;
    }
    auto job      = std::make_unique<LichessSync>();
    job->index    = index;
    job->name     = name;
    job->cancel   = std::make_shared<std::atomic<bool>>(false);
    job->progress = std::make_shared<lichess::SyncProgress>();
    job->db       = s.open(index);
    if (!job->db) return;
    const std::string token  = lichess_account_ ? lichess_account_->token : std::string();
    const std::string player = lichess_.player;
    job->done                = std::async(std::launch::async, [db = job->db, player, token, progress = job->progress,
                                                               cancel = job->cancel, filters = lichessFilters()] {
        return lichess::syncGames(*db, player, token, *progress, *cancel, 500, filters);
    });
    lichess_new_games_.reset();  // counted again after it
    lichess_sync_report_.reset();
    lichess_sync_ = std::move(job);
}

void App::drawLichessHomeCard() {
    // The home's Lichess card (M0, M4): the account, a player's games and their sync, the broadcasts live. Lichess is
    // asked from here only once the user has turned to it (an account linked, a player's games imported).
    const ui::Card card("chess_lichess");
    const ui::TextWrap wrap;
    ImGui::TextUnformatted("Lichess");
    ImGui::SameLine();
    ImGui::TextDisabled("%s",
                        lichess_account_ ? trf("· {}", lichess_account_->username).c_str() : TR("· no account linked"));
    ImGui::SameLine();
    if (ImGui::SmallButton(TR("The Lichess page ›"))) page_ = Page::Lichess;
    drawLichessAccountPanel();  // the account's ratings, figures and activity (#46)
    drawPlayingGames();         // the user's games in progress (#48)
    drawFinishedBanner(true);   // one that ended (#49)
    if (const auto base = lichessBase()) {
        const auto facts = shelf().facts(*base);
        auto line        = trf("Lichess · {}: {}", lichess_.player, ui::exactCount(facts.games));
        if (facts.changed) line += " · " + trf("synced {}", ui::dayText(facts.changed));
        // The new games, counted when the Home page opens, at most once an hour, if the user chose it (M4, board 3).
        if (lichess_.count_new && !lichess_sync_) {
            const auto now = std::chrono::steady_clock::now();
            if (!lichess_count_job_.valid() && dueAfter(lichess_counted_, std::chrono::hours(1), now)) {
                lichess_counted_ = now;
                lichess_count_job_ =
                    std::async(std::launch::async, [folder = shelf().entries()[*base].folder, player = lichess_.player,
                                                    token = lichess_account_ ? lichess_account_->token : std::string(),
                                                    filters = lichessFilters(), cancel = lichess_count_cancel_] {
                        return lichess::countNewGames(folder, player, token, filters, cancel.get());
                    });
            }
            if (isReady(lichess_count_job_)) {
                const auto answer  = lichess_count_job_.get();
                lichess_new_games_ = answer.value;
            }
        }
        ImGui::TextDisabled("%s", line.c_str());
        if (lichess_new_games_ && *lichess_new_games_ > 0) {
            ImGui::SameLine();
            ImGui::TextColored(
                ImGui::ColorConvertU32ToFloat4(ui::ACCENT), "%s",
                trn("+{} new game", "+{} new games", *lichess_new_games_,
                    *lichess_new_games_ >= 1000 ? std::string("1000") : std::to_string(*lichess_new_games_))
                    .c_str());
        }
        ImGui::SameLine();
        if (lichess_sync_) {
            ImGui::TextUnformatted(
                trf("{} games received", ui::exactCount(lichess_sync_->progress->games.load())).c_str());
        } else if (ImGui::SmallButton(TR("Sync"))) {
            startLichessSync();
        }
    }
    if (lichess_account_ || !lichess_.player.empty()) {
        askBroadcasts();
        if (lichess_broadcasts_.value && !lichess_broadcasts_.value->active.empty()) {
            auto active = lichess_broadcasts_.value->active;
            std::stable_sort(active.begin(), active.end(),
                             [](const auto& x, const auto& y) { return x.tour.tier > y.tour.tier; });
            for (std::size_t i = 0; i < active.size() && i < 3; ++i) {
                const ui::Id id(static_cast<int>(i));
                ImGui::TextUnformatted(trf("Live: {} · {}", active[i].tour.name, active[i].round.name).c_str());
                ImGui::SameLine();
                if (ImGui::SmallButton(TR("Follow"))) followBroadcast(active[i]);
            }
        }
    }
}

void App::askBroadcasts() {
    if (lichess_broadcasts_job_.valid()) return;
    const auto now = std::chrono::steady_clock::now();
    if (lichess_broadcasts_.value && now - lichess_broadcasts_time_ < std::chrono::minutes(10)) return;
    if (!lichess_broadcasts_.value && lichess_broadcasts_.failed() &&
        now - lichess_broadcasts_time_ < std::chrono::minutes(1))
        return;
    lichess_broadcasts_time_ = now;
    lichess_broadcasts_job_  = std::async(std::launch::async, [] { return lichess::topBroadcasts(); });
}

void App::startLichessLink(std::vector<std::string> scopes, std::string username,
                           std::optional<lichess::BroadcastEntry> then_follow) {
    lichess_error_ = {};
    if (lichess_scope_ask_) lichess_scope_ask_->error = {};
    auto link         = std::make_unique<LichessLink>();
    link->cancel      = std::make_shared<std::atomic<bool>>(false);
    auto url          = std::make_shared<std::string>();
    auto opened       = std::make_shared<std::atomic<bool>>(true);
    link->url         = url;
    link->opened      = opened;
    link->then_follow = std::move(then_follow);
    if (lichess_account_) link->previous_token = lichess_account_->token;
    link->result  = std::async(std::launch::async, [cancel = link->cancel, url, opened, scopes = std::move(scopes),
                                                    username = std::move(username)] {
        return lichess::link(
            [url, opened](const std::string& page) {
                *url    = page;
                *opened = SDL_OpenURL(page.c_str());
                return opened->load();
            },
            scopes, std::chrono::minutes(5), cancel.get(), username);
    });
    lichess_link_ = std::move(link);
}

void App::drawLichessScopeAsk() {
    if (!lichess_scope_ask_ || !lichess_account_) return;
    auto& ask = *lichess_scope_ask_;
    const ui::Card card("lichess_scope");
    const ui::TextWrap wrap;
    ImGui::TextUnformatted(TR("Lichess asks for one more permission"));
    if (ask.follow)
        ImGui::TextDisabled("%s", trf("to follow “{} · {}”", ask.follow->tour.name, ask.follow->round.name).c_str());
    if (ask.search) {  // a search among the broadcasts (#55)
        ImGui::TextUnformatted(
            TR("To search the broadcasts, Lichess asks for an account allowed to read studies and "
               "broadcasts."));
        ImGui::TextDisabled("%s", TR("The permission lets RookStudio read your studies and the broadcasts; it writes "
                                     "nothing. Lichess's page opens for it; your account stays linked meanwhile, and "
                                     "the token it replaces is revoked."));
    } else if (ask.scope == lichess::SCOPE_TEAM_READ) {  // the user's teams' Swiss tournaments (#95)
        ImGui::TextUnformatted(
            TR("To list your teams' Swiss tournaments, RookStudio must be allowed to read your "
               "teams."));
        ImGui::TextDisabled("%s", TR("The permission lets RookStudio read the teams you are a member of; it writes "
                                     "nothing. Lichess's page opens for it; your account stays linked meanwhile, and "
                                     "the token it replaces is revoked."));
    } else if (ask.scope == lichess::SCOPE_FOLLOW_READ) {  // the followed players' mosaic (#50)
        ImGui::TextUnformatted(
            TR("To show the games of the players your account follows, RookStudio must be allowed "
               "to read whom it follows."));
        ImGui::TextDisabled("%s", TR("The permission lets RookStudio read the players you follow on Lichess; it writes "
                                     "nothing. Lichess's page opens for it; your account stays linked meanwhile, and "
                                     "the token it replaces is revoked."));
    } else {
        ImGui::TextUnformatted(
            TR("Since August 2026, Lichess lets broadcasts be followed with an account allowed to read "
               "studies and broadcasts; without it, it slows them down heavily and may stop them."));
        ImGui::TextDisabled("%s", TR("The permission lets RookStudio read your studies and the broadcasts; it writes "
                                     "nothing. Lichess's page opens for it; your account stays linked meanwhile, and "
                                     "the token it replaces is revoked."));
    }
    if (lichess_link_) {
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::ACCENT), "%s",
                           TR("Waiting for Lichess: allow RookStudio in the browser's page, then come back here."));
    } else {
        if (ask.error.failed())
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s",
                               lichess::linkRefused(ask.error)
                                   ? TR("You refused it on Lichess: nothing changed.")
                                   : trf("Lichess gave nothing ({}).", lichess::describe(ask.error)).c_str());
        if (ui::iconButton("lichess_scope_allow", ui::Icon::External, TR("Allow on Lichess…"))) {
            auto scopes = lichess_account_->scopes;
            if (!lichess_account_->has(ask.scope)) scopes.push_back(ask.scope);
            lichess_players_then_mosaic_ = ask.players;
            startLichessLink(scopes, lichess_account_->username, ask.follow);
        }
        if (ask.players) {  // the players added by name, without reading whom the account follows
            ImGui::SameLine();
            if (ImGui::SmallButton(TR("Without it"))) {
                lichess_.follow_declined = true;
                saveLichessSettings();
                lichess_scope_ask_.reset();
                openPlayersMosaic(true);
                return;
            }
        }
        if (ask.follow) {
            ImGui::SameLine();
            if (ImGui::SmallButton(TR("Follow without it"))) {
                const auto entry = *ask.follow;
                lichess_scope_ask_.reset();
                followBroadcast(entry, true);
                return;
            }
        }
        ImGui::SameLine();
        if (ImGui::SmallButton(TR("Not now"))) {
            broadcast_search_after_link_ = false;
            if (ask.scope == lichess::SCOPE_TEAM_READ) {
                lichess_.team_declined = true;
                saveLichessSettings();
            }
            lichess_scope_ask_.reset();
            return;
        }
    }
}

void App::drawLichessAccount() {
    ui::beginCard("lichess_account");
    ImGui::PushTextWrapPos();
    ImGui::TextUnformatted(TR("Your Lichess account"));
    if (lichess_link_) {
        // Waiting for the browser (M4, board 2): the rest of the program goes on.
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::ACCENT), "%s",
                           TR("Waiting for Lichess: allow RookStudio in the browser's page, then come back here."));
        const auto url = *lichess_link_->url;
        if (!lichess_link_->opened->load() && !url.empty()) {
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s",
                               TR("No browser opened: paste this address in a browser of this machine."));
            ImGui::TextDisabled("%s", url.c_str());
            if (ImGui::SmallButton(TR("Copy"))) ImGui::SetClipboardText(url.c_str());
            ImGui::SameLine();
        }
        ImGui::BeginDisabled(url.empty());
        if (ImGui::SmallButton(TR("Open the page again"))) (void)SDL_OpenURL(url.c_str());
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::SmallButton(TR("Cancel")) || ImGui::IsKeyPressed(ImGuiKey_Escape)) *lichess_link_->cancel = true;
        ImGui::SameLine();
        ImGui::TextDisabled("%s", TR("Esc"));
        ImGui::TextDisabled("%s",
                            TR("Lichess sends the browser back to 127.0.0.1, on a port only RookStudio listens on "
                               "while it links. Without an answer in 5 minutes, the link stops by itself."));
    } else if (lichess_account_) {
        const auto& a = *lichess_account_;
        ImGui::TextUnformatted(trf("Linked as {}", a.username).c_str());
        if (a.expires > 0) {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", trf("· the token lasts until {}", dateText(a.expires)).c_str());
        }
        // What the token may do, asked for when a feature needed it (#45).
        ImGui::TextDisabled("%s", a.has(lichess::SCOPE_STUDY_READ)
                                      ? TR("RookStudio may read your studies and the broadcasts.")
                                      : TR("RookStudio reads your name and public data only; it asks for more when a "
                                           "feature needs it."));
        // Lichess's tokens last a year and cannot be refreshed: a new link a month before the end.
        const auto now =
            std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
                .count();
        if (a.expires > 0 && a.expires - now < 30 * 24 * 3600) {
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s",
                               a.expires <= now ? TR("The token has ended: link the account again.")
                                                : TR("The token ends within a month: renew it to keep the link."));
            if (ImGui::SmallButton(TR("Renew…"))) startLichessLink(a.scopes, a.username);
            ImGui::SameLine();
        }
        if (ImGui::SmallButton(TR("Unlink"))) {
            lichess_unlink_ =
                std::async(std::launch::async, [root = storage_.root()] { return lichess::forgetAccount(root); });
            lichess_account_.reset();
        }
        ImGui::TextDisabled("%s",
                            TR("The token is kept in your profile, readable by you alone. Unlinking revokes it at "
                               "Lichess and deletes it here; your databases stay."));
        // A game of the user's that ends (#49): its review started by itself.
        if (ImGui::Checkbox(TR("Review my finished games at once"), &lichess_.review_finished)) saveLichessSettings();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s",
                              TR("When one of your Lichess games ends, it opens on the Board and its review starts, "
                                 "unless the Board holds a game with unsaved changes or work under way; a banner "
                                 "offers it then."));
    } else {
        ImGui::TextDisabled("%s", TR("No account linked. Linked, it brings:"));
        // Wrapped, unlike BulletText: a long line in another language stays inside the card.
        for (const char* line : {TR("Lichess's opening explorer in the Tree tab: masters, all players, one player;"),
                                 TR("your games synced faster, your private ones too;"),
                                 TR("your studies, Lichess's puzzles online, your games on Lichess from the Play "
                                    "page.")}) {
            ImGui::Bullet();
            ImGui::SameLine();
            ImGui::TextWrapped("%s", line);
        }
        // What went wrong last, and the gesture that mends it (M4, board 2).
        if (lichess_error_.failed()) {
            const bool refused = lichess::linkRefused(lichess_error_);
            const bool late    = lichess_error_.net == lichess::NetError::TimedOut;
            const bool offline = lichess_error_.net == lichess::NetError::Offline;
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s",
                               refused ? TR("You refused the access on Lichess: nothing is linked.")
                               : late ? TR("No answer from Lichess in 5 minutes: the link stopped, the port is closed.")
                               : offline ? TR("Lichess cannot be reached from here: the link waits for a connection. "
                                              "Everything else works without it.")
                                         : trf("Lichess cannot be reached from here ({}): the link waits for a "
                                               "connection. Everything else works without it.",
                                               lichess::describe(lichess_error_))
                                               .c_str());
        }
        if (ui::iconButton("lichess_link", ui::Icon::External,
                           !lichess_error_.failed() ? TR("Link the account…") : TR("Try again")))
            startLichessLink();
        ImGui::TextDisabled("%s", TR("The browser opens on lichess.org: you allow RookStudio there, without giving it "
                                     "a password."));
    }
    ImGui::PopTextWrapPos();
    ui::endCard();
}

void App::drawLichessGames() {
    ui::beginCard("lichess_games");
    ImGui::PushTextWrapPos();
    ImGui::TextUnformatted(TR("A player's games"));
    ImGui::TextDisabled("%s", TR("The public games of a Lichess player, yours or another's, in a database: without an "
                                 "account too."));
    if (lichess_.player.empty() && lichess_account_) lichess_.player = lichess_account_->username;
    // The player, and the database that holds their games: "Lichess · name".
    std::snprintf(lichess_player_.data(), lichess_player_.size(), "%s",
                  lichess_player_active_ ? lichess_player_.data() : lichess_.player.c_str());
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(TR("Player"));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(px(220));
    ImGui::InputText("##lichess_player", lichess_player_.data(), lichess_player_.size(),
                     ImGuiInputTextFlags_CharsNoBlank);
    lichess_player_active_ = ImGui::IsItemActive();
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        lichess_.player = lichess_player_.data();
        saveLichessSettings();
    }
    const std::string player = lichess_player_.data();
    const auto name          = "Lichess · " + player;
    auto& s                  = shelf();
    std::optional<std::size_t> base;
    for (std::size_t i = 0; i < s.entries().size(); ++i)
        if (!s.entries()[i].source && s.entries()[i].name == name) base = i;
    ImGui::SameLine();
    if (lichess_sync_) {
        // The games coming (M4, board 3): by batches, the oldest first; a cancel keeps what came.
        ImGui::TextUnformatted(
            trf("{}: {} games received", lichess_sync_->name, ui::exactCount(lichess_sync_->progress->games.load()))
                .c_str());
        if (ImGui::SmallButton(TR("Cancel"))) *lichess_sync_->cancel = true;
        ImGui::TextDisabled("%s", TR("The games come by batches, the oldest first; what was received stays if you "
                                     "cancel."));
    } else {
        ImGui::BeginDisabled(player.empty());
        const auto* label = base ? TR("Sync") : TR("Import");
        if (ui::iconButton("lichess_sync", ui::Icon::Download, label)) {
            lichess_.player = player;
            saveLichessSettings();
            startLichessSync();
        }
        ImGui::EndDisabled();
        if (base) {
            ImGui::SameLine();
            if (ui::iconButton("lichess_open_base", ui::Icon::Database, TR("Open the database"))) {
                db_selected_ = static_cast<int>(*base);
                db_list_     = true;
                page_        = Page::Databases;
            }
            const auto facts = s.facts(*base);
            auto line        = trn("{} game", "{} games", static_cast<int>(std::min<uint64_t>(facts.games, 1000000000)),
                                   ui::exactCount(facts.games));
            if (facts.changed) line += " · " + trf("synced {}", ui::dayText(facts.changed));
            ImGui::TextDisabled("%s · %s", name.c_str(), line.c_str());
        }
        if (lichess_sync_report_) {
            const auto& r = *lichess_sync_report_;
            if (r.failed())
                ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s",
                                   trf("Stopped: {}. The games received are in the database; Sync goes on from the "
                                       "next one.",
                                       lichess::describe(r))
                                       .c_str());
            else
                ImGui::TextUnformatted(trf("Done: {} new games{}.", ui::exactCount(r.added),
                                           r.cancelled ? std::string(TR(", stopped at your request")) : std::string())
                                           .c_str());
        }
    }
    // What is asked, what goes out (M4, board 3).
    if (ImGui::TreeNode(TR("What is asked"))) {
        bool changed = false;
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", TR("Speeds"));
        ImGui::SameLine();
        changed |= ImGui::RadioButton(TR("All"), &lichess_.sync_speeds, 0);
        ImGui::SameLine();
        changed |= ImGui::RadioButton(TR("Blitz, rapid, classical"), &lichess_.sync_speeds, 1);
        ImGui::SameLine();
        changed |= ImGui::RadioButton(TR("Classical and correspondence"), &lichess_.sync_speeds, 2);
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", TR("Since"));
        ImGui::SameLine();
        changed |= ImGui::RadioButton(TR("The first game"), &lichess_.sync_since, 0);
        ImGui::SameLine();
        changed |= ImGui::RadioButton(TR("A year ago"), &lichess_.sync_since, 1);
        ImGui::SameLine();
        changed |= ImGui::RadioButton(TR("A date"), &lichess_.sync_since, 2);
        if (lichess_.sync_since == 2) {
            ImGui::SameLine();
            if (!ImGui::IsAnyItemActive() || lichess_date_[0] == 0)
                std::snprintf(lichess_date_.data(), lichess_date_.size(), "%s", lichess_.sync_date.c_str());
            ImGui::SetNextItemWidth(px(110));
            ImGui::InputTextWithHint("##sync_date", "2024-01-31", lichess_date_.data(), lichess_date_.size(),
                                     ImGuiInputTextFlags_CharsDecimal | ImGuiInputTextFlags_CharsNoBlank);
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                lichess_.sync_date = lichess_date_.data();
                changed            = true;
            }
        }
        ImGui::TextDisabled("%s", TR("Variants: standard chess and Chess960."));
        ImGui::TextDisabled("%s", TR("They apply to the games to come: a sync asks for what follows the last game "
                                     "received."));
        if (ImGui::Checkbox(TR("Count the new games when the Home page opens"), &lichess_.count_new)) {
            changed          = true;
            lichess_counted_ = {};
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%s", TR("one request, at most once an hour: the player's name"));
        if (changed) saveLichessSettings();
        ImGui::TreePop();
    }
    ImGui::TextDisabled("%s", TR("Sent to Lichess: the player's name, these filters and the date of the last game "
                                 "received; with a linked account, your token too. The games keep their link to "
                                 "lichess.org, their clocks and Lichess's evaluations."));
    ImGui::PopTextWrapPos();
    ui::endCard();
}

void App::startBroadcastSearch() {
    const std::string words(broadcast_search_.data());
    if (words.empty() || broadcast_search_job_.valid() || !lichess_account_) return;
    // Lichess searches for an account allowed to read broadcasts only (since 2026-10): asked once, here.
    if (!lichess_account_->has(lichess::SCOPE_STUDY_READ)) {
        LichessScopeAsk ask;
        ask.scope                    = lichess::SCOPE_STUDY_READ;
        ask.search                   = true;
        lichess_scope_ask_           = ask;
        broadcast_search_after_link_ = true;
        return;
    }
    broadcast_searched_   = words;
    broadcast_search_job_ = std::async(std::launch::async, [words, token = lichess_account_->token] {
        return lichess::searchBroadcasts(words, token);
    });
}

void App::drawLichessBroadcasts() {
    askBroadcasts();
    const ui::Card card("lichess_broadcasts");
    const ui::TextWrap wrap;
    ImGui::TextUnformatted(TR("Official broadcasts"));
    // A search among the recent ones (#55): the words, and an account allowed to read broadcasts.
    ImGui::BeginDisabled(!lichess_account_);
    ImGui::SetNextItemWidth(std::min(ImGui::GetContentRegionAvail().x * 0.6f, px(320)));
    if (ImGui::InputTextWithHint("##broadcast_search", TR("Search the broadcasts"), broadcast_search_.data(),
                                 broadcast_search_.size(), ImGuiInputTextFlags_EnterReturnsTrue))
        startBroadcastSearch();
    ImGui::SameLine();
    if (ImGui::SmallButton(TR("Search##broadcasts"))) startBroadcastSearch();
    ImGui::EndDisabled();
    if (!lichess_account_ && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", TR("With your Lichess account linked, allowed to read broadcasts."));
    if (broadcast_found_ || broadcast_search_job_.valid()) {
        ImGui::SameLine();
        if (ImGui::SmallButton(TR("Back to the list"))) {
            broadcast_found_.reset();
            broadcast_search_.fill('\0');
        }
    }
    const auto& b = lichess_broadcasts_;
    if (!b.value && !broadcast_found_ && !broadcast_search_job_.valid()) {
        ImGui::TextDisabled("%s", b.failed() ? lichess::gaveNothing(b).c_str() : TR("Asking Lichess…"));
    } else {
        const auto row = [&](const lichess::BroadcastEntry& e, bool live) {
            ImGui::PushID(e.round.id.c_str());
            ImGui::GetWindowDrawList()->AddCircleFilled(
                ImVec2(ImGui::GetCursorScreenPos().x + px(5), ImGui::GetCursorScreenPos().y + px(9)), px(4),
                live ? IM_COL32(209, 74, 59, 255) : IM_COL32(201, 206, 214, 255));
            ImGui::Dummy(ImVec2(px(12), 0));
            ImGui::SameLine();
            // A long event's name is cut with "…", so that its round and Follow stay on the row.
            const auto tag    = live ? trf("live · {}", e.round.name) : e.round.name;
            const auto& style = ImGui::GetStyle();
            const float room = ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(tag.c_str()).x -
                               ImGui::CalcTextSize(TR("Follow")).x - style.FramePadding.x * 2 - style.ItemSpacing.x * 2;
            std::string name = e.tour.name;
            if (ImGui::CalcTextSize(name.c_str()).x > room) {
                while (!name.empty() && ImGui::CalcTextSize((name + "…").c_str()).x > room) {
                    name.pop_back();
                    while (!name.empty() && (static_cast<unsigned char>(name.back()) & 0xC0) == 0x80) name.pop_back();
                    if (!name.empty() && static_cast<unsigned char>(name.back()) >= 0xC0) name.pop_back();
                }
                name += "…";
                ImGui::TextUnformatted(name.c_str());
                ImGui::SetItemTooltip("%s", e.tour.name.c_str());
            } else {
                ImGui::TextUnformatted(name.c_str());
            }
            ImGui::SameLine();
            ImGui::TextDisabled("%s", tag.c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton(TR("Follow"))) followBroadcast(e);
            ImGui::PopID();
        };
        if (broadcast_search_job_.valid() || broadcast_found_) {
            if (broadcast_search_job_.valid() || !broadcast_found_) {
                ImGui::TextDisabled("%s", TR("Searching…"));
                return;
            }
            const auto& found = *broadcast_found_;
            if (!found.value)
                ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s",
                                   trf("Lichess did not search: {}", lichess::describe(found)).c_str());
            else if (found.value->empty())
                ImGui::TextDisabled("%s", trf("No broadcast found for “{}”.", broadcast_searched_).c_str());
            else
                ImGui::TextDisabled("%s", trf("Found for “{}”:", broadcast_searched_).c_str());
            if (found.value)
                for (const auto& e : *found.value) row(e, e.round.ongoing);
            return;
        }
        // The best known events first (Lichess's tier), six of them unless all are asked for; the next ones after.
        auto active = b.value->active;
        std::stable_sort(active.begin(), active.end(),
                         [](const auto& x, const auto& y) { return x.tour.tier > y.tour.tier; });
        const std::size_t shown = lichess_all_broadcasts_ ? active.size() : std::min<std::size_t>(active.size(), 6);
        for (std::size_t i = 0; i < shown; ++i) row(active[i], true);
        if (active.size() > 6) {
            if (ImGui::SmallButton(lichess_all_broadcasts_
                                       ? TR("Fewer")
                                       : trf("All the broadcasts live ({})", ui::exactCount(active.size())).c_str()))
                lichess_all_broadcasts_ = !lichess_all_broadcasts_;
        }
        for (std::size_t i = 0; i < b.value->upcoming.size() && i < 3; ++i) row(b.value->upcoming[i], false);
        if (b.value->active.empty() && b.value->upcoming.empty())
            ImGui::TextDisabled("%s", TR("No official broadcast now."));
    }
    ImGui::TextDisabled(
        "%s", TR("Follow opens the round in a mosaic on this page, each game analysed by your engine, on your "
                 "machine. Lichess limits the rounds followed at once: 4 without an account, 8 with "
                 "one."));
}

void App::followBroadcast(const lichess::BroadcastEntry& entry, bool without_account) {
    for (auto& f : followed_rounds_) {
        if (f.key() != entry.round.id) continue;
        if (f.entry.tour.id.empty() && !entry.tour.id.empty()) f.entry = entry;  // its names, now known
        openBroadcastMosaic(f.key());
        return;
    }
    // Lichess asks for a token allowed to read broadcasts (#44): asked once, on the Lichess page, before following.
    const bool allowed = lichess_account_ && lichess_account_->has(lichess::SCOPE_STUDY_READ);
    if (lichess_account_ && !allowed && !without_account) {
        lichess_scope_ask_ = LichessScopeAsk{lichess::SCOPE_STUDY_READ, entry, {}};
        broadcast_round_.clear();  // the page itself, where the question is
        page_ = Page::Lichess;
        return;
    }
    // Lichess's limit: four rounds at once without an account, eight with one.
    const std::size_t most = allowed ? 8 : 4;
    if (followed_rounds_.size() >= most) {
        Console::instance().add(trf("Lichess lets {} rounds be followed at once: stop one first.", most), true);
        return;
    }
    FollowedRound f;
    f.entry      = entry;
    f.with_token = allowed;
    f.follower =
        std::make_unique<lichess::RoundFollower>(entry.round.id, allowed ? lichess_account_->token : std::string());
    followed_rounds_.push_back(std::move(f));
    openBroadcastMosaic(entry.round.id);  // Follow opens the round as a mosaic (M4)
}

void App::drawLichessFollowed() {
    if (followed_rounds_.empty()) return;
    ui::beginCard("lichess_followed");
    ImGui::PushTextWrapPos();
    ImGui::TextUnformatted(TR("Rounds followed"));
    std::optional<std::size_t> stop;
    for (std::size_t r = 0; r < followed_rounds_.size(); ++r) {
        auto& f = followed_rounds_[r];
        ImGui::PushID(static_cast<int>(r));
        ImGui::Spacing();
        ImGui::TextUnformatted(
            fmt::format("{} · {}", f.entry.tour.name, f.whole ? TR("the whole tournament") : f.entry.round.name)
                .c_str());
        ImGui::SameLine();
        const auto broke = f.follower->failure();
        if (f.follower->streaming())
            ImGui::TextColored(ImVec4(0.82f, 0.29f, 0.23f, 1.0f), "%s", TR("live"));
        else
            ImGui::TextDisabled(
                "%s", broke.failed()
                          ? trf("the stream broke ({}): it opens again by itself", lichess::describe(broke)).c_str()
                          : TR("connecting…"));
        ImGui::SameLine();
        if (ImGui::SmallButton(TR("The mosaic"))) openBroadcastMosaic(f.key());
        ImGui::SameLine();
        if (ImGui::SmallButton(TR("Stop following"))) stop = r;
        // Lichess refuses it, or slows it down, without an account allowed to read broadcasts (#44).
        if (!f.with_token) {
            const bool refused = f.follower->refused();
            ImGui::TextColored(refused ? ImGui::ColorConvertU32ToFloat4(ui::WARN) : ui::MUTED4, "%s",
                               refused            ? TR("Lichess refuses this round without an account allowed to read "
                                                       "broadcasts.")
                               : lichess_account_ ? TR("Followed without the permission to read broadcasts: Lichess "
                                                       "slows that down heavily, and may stop it.")
                                                  : TR("Followed without an account: Lichess slows that down heavily, "
                                                       "and may stop it."));
            ImGui::SameLine();
            if (lichess_account_) {
                if (!lichess_link_ && ImGui::SmallButton(TR("Allow on Lichess…"))) {
                    lichess_scope_ask_ = LichessScopeAsk{lichess::SCOPE_STUDY_READ, std::nullopt, {}};
                    auto scopes        = lichess_account_->scopes;
                    scopes.emplace_back(lichess::SCOPE_STUDY_READ);
                    startLichessLink(scopes, lichess_account_->username);
                }
            } else if (!lichess_link_ && ImGui::SmallButton(TR("Link the account…"))) {
                startLichessLink({lichess::SCOPE_STUDY_READ});
            }
        }
        const auto games = f.follower->games();
        for (std::size_t g = 0; g < games.size(); ++g) {
            const auto& game = games[g];
            ImGui::PushID(static_cast<int>(g));
            ImGui::BulletText("%s", fmt::format("{}{} – {}{}  {}", game.white,
                                                game.white_elo ? fmt::format(" {}", game.white_elo) : "", game.black,
                                                game.black_elo ? fmt::format(" {}", game.black_elo) : "",
                                                game.result.empty() || game.result == "*" ? TR("playing") : game.result)
                                        .c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton(TR("On the board"))) {
                readOnBoard(game.pgn, fmt::format("{} – {}", game.white, game.black));
                page_ = Page::Board;
            }
            ImGui::PopID();
        }
        ImGui::PopID();
    }
    if (stop) {
        followed_rounds_[*stop].follower->stop();
        followed_rounds_.erase(followed_rounds_.begin() + static_cast<std::ptrdiff_t>(*stop));
    }
    ImGui::TextDisabled("%s", TR("The moves come from Lichess's stream, opened again after a break."));
    ImGui::PopTextWrapPos();
    ui::endCard();
}

void App::drawLichessOnline() {
    ui::beginCard("lichess_online");
    ImGui::PushTextWrapPos();
    ImGui::TextUnformatted(TR("Online for the analysis"));
    ImGui::SameLine();
    ImGui::TextDisabled("%s", TR("each decided here, or at its first use"));
    const auto service = [&](const char* id, const char* name, int& choice, const char* sent) {
        bool on = choice == 1;
        if (ImGui::Checkbox((std::string(name) + "##" + id).c_str(), &on)) {
            choice = on ? 1 : 2;
            saveLichessSettings();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%s", choice == 0 ? TR("· asked at its first use") : "");
        ImGui::Indent(px(28));
        ImGui::TextDisabled("%s", sent);
        ImGui::Unindent(px(28));
    };
    service("cloud", TR("Lichess's cloud evaluation, in the Analysis tab"), lichess_.cloud,
            TR("Sent for each position shown: its FEN, without an account. Lichess answers with the lines its servers "
               "have already computed, when it has them; your engine always answers."));
    service("tablebase", TR("Endgames online, up to 7 pieces"), lichess_.tablebase,
            TR("Sent for a position of 7 pieces or fewer that the tables on your disk do not know: its FEN, "
               "without an account."));
    bool explorer = lichess_.explorer;
    ImGui::BeginDisabled(!lichess_account_);
    if (ImGui::Checkbox(TR("Lichess's explorer, in the Tree tab"), &explorer)) {
        lichess_.explorer = explorer;
        saveLichessSettings();
    }
    ImGui::EndDisabled();
    ImGui::Indent(px(28));
    ImGui::TextDisabled("%s", lichess_account_
                                  ? TR("Sent for each position: its FEN and the filter chosen (masters, speeds, a "
                                       "player), with your token. Nothing of your databases.")
                                  : TR("Needs a linked account: Lichess requires it now, even for the masters."));
    ImGui::Unindent(px(28));
    ImGui::PopTextWrapPos();
    ui::endCard();
}

void App::drawLichessPage() {
    // A followed round, as a mosaic (M4, board 4).
    if (!broadcast_round_.empty()) {
        drawBroadcastMosaic();
        return;
    }
    // The page opens: the followed studies asked whether they changed (#79, M11 point 10), a HEAD each, at most every
    // five minutes.
    const int frame = ImGui::GetFrameCount();
    if (!drawnLately(lichess_page_frame_, frame, 2) &&
        dueAfter(study_checked_, std::chrono::minutes(5), std::chrono::steady_clock::now()))
        checkFollowedStudies();
    lichess_page_frame_ = frame;
    {
        const ui::TextWrap wrap;
        ImGui::TextDisabled("%s", TR("Everything here is optional: RookStudio works without an account and without the "
                                     "Internet. What goes to Lichess is said where you decide it."));
    }
    ImGui::Dummy(ImVec2(0, px(6)));
    if (lichess_scope_ask_ && lichess_account_) {
        drawLichessScopeAsk();
        ImGui::Dummy(ImVec2(0, px(8)));
    }
    const float avail = ImGui::GetContentRegionAvail().x;
    const bool two    = avail > px(900);
    const float col   = two ? (avail - px(14)) / 2 : avail;
    {
        const ui::Child left("##lichess_left", ImVec2(col, 0), ImGuiChildFlags_AutoResizeY);
        {
            ui::TopicRegion topic(guide::topics::LichessLink);
            drawLichessAccount();
        }
        drawListenOffer();  // the right to hear the user's games start and end (#48)
        ImGui::Dummy(ImVec2(0, px(8)));
        {
            ui::TopicRegion topic(guide::topics::LichessGames);
            drawLichessGames();
        }
        ImGui::Dummy(ImVec2(0, px(8)));
        {
            ui::TopicRegion topic(guide::topics::LichessStudies);
            drawLichessStudies();  // the user's studies (#79)
        }
        ImGui::Dummy(ImVec2(0, px(8)));
        drawPrepCard();  // a game prepared against an opponent (#52)
    }
    if (two) ImGui::SameLine(0, px(14));
    {
        const ui::Child right("##lichess_right", ImVec2(col, 0), ImGuiChildFlags_AutoResizeY);
        {
            ui::TopicRegion topic(guide::topics::LichessBroadcasts);
            drawLichessBroadcasts();
            ImGui::Dummy(ImVec2(0, px(8)));
            if (!followed_rounds_.empty()) {
                drawLichessFollowed();
                ImGui::Dummy(ImVec2(0, px(8)));
            }
        }
        arenas_->drawCard();  // Lichess's arenas and the user's teams' Swiss tournaments (#95)
        ImGui::Dummy(ImVec2(0, px(8)));
        tv_->drawCard();  // Lichess TV's channels (#86)
        ImGui::Dummy(ImVec2(0, px(8)));
        drawFollowedPlayers();  // the players followed live (#50)
        ImGui::Dummy(ImVec2(0, px(8)));
        {
            ui::TopicRegion topic(guide::topics::OfflineEvals);
            drawLichessOfflineEvals();  // Lichess's evaluations kept offline (#58)
        }
        ImGui::Dummy(ImVec2(0, px(8)));
        if (std::any_of(offers().begin(), offers().end(),
                        [](const lichess::OfferedEngine& o) { return !o.id.empty(); })) {
            drawLichessEnginesCard();  // the engines offered to lichess.org's analysis board (#96)
            ImGui::Dummy(ImVec2(0, px(8)));
        }
        {
            ui::TopicRegion topic(guide::topics::LichessOnline);
            drawLichessOnline();
        }
    }
    // The evaluations' card asked for (Ctrl+K): the page scrolled to it.
    if (offline_evals_y_ >= 0) {
        ImGui::SetScrollFromPosY(offline_evals_y_ - ImGui::GetWindowPos().y, 0.0f);
        offline_evals_y_ = -1;
    }
    // So the tournaments' card (#95).
    arenas_->scrollToCard();
}

}  // namespace fcg

namespace fcg {

void App::drawLichessTreeCard(bool linked) {
    ui::beginCard("lichess_tree_card");
    ImGui::PushTextWrapPos();
    if (!linked) {
        // M3, board 2: nothing is required, the local databases and the books stay.
        ImGui::TextUnformatted(
            TR("Lichess's explorer (the masters, all the players, one player) now asks for a linked "
               "account. Nothing is required: the local databases and the books stay."));
        if (ui::iconButton("lichess_tree_link", ui::Icon::External, TR("Link the account…"))) {
            page_ = Page::Lichess;
            startLichessLink();
        }
    } else {
        ImGui::TextUnformatted(TR("Lichess's explorer is turned off on the Lichess page."));
        if (ImGui::SmallButton(TR("The Lichess page ›"))) page_ = Page::Lichess;
    }
    ImGui::TextDisabled("%s", TR("Sent to Lichess for each position: its FEN and the filter chosen, with your token. "
                                 "Nothing of your databases."));
    ImGui::PopTextWrapPos();
    ui::endCard();
}

bool App::lichessTreeRows(int source, const std::string& fen) {
    tree_db_   = -1;
    tree_book_ = false;
    tree_ctg_  = false;
    if (aidsLocked()) {  // fair play (#48): the user's databases and books only
        ImGui::TextDisabled("%s", TR("Lichess's explorer is paused on the positions of your correspondence game."));
        return false;
    }
    if (!lichess_account_ || !lichess_.explorer) {
        drawLichessTreeCard(lichess_account_.has_value());
        return false;
    }
    // The answers that came.
    if (isReady(explorer_job_)) {
        if (explorer_answers_.size() > 2000) explorer_answers_.clear();
        auto answer = explorer_job_.get();
        if (!answer.value) explorer_failed_[explorer_asked_] = ImGui::GetTime();
        explorer_answers_[explorer_asked_] = std::move(answer);
    }
    lichess::ExplorerQuery q;
    q.source         = source == 0   ? lichess::ExplorerSource::Masters
                       : source == 1 ? lichess::ExplorerSource::Lichess
                                     : lichess::ExplorerSource::Player;
    q.fen            = fen;
    q.token          = lichess_account_->token;
    const auto board = boardAt(board_.game, board_.chess960, board_.ply);
    if (source == 1) {
        q.speeds  = lichess_.explorer_speeds;
        q.ratings = lichess_.explorer_ratings;
        q.history = lichess_.explorer_months;  // the position's months too (#56)
    }
    if (source == 2) {
        q.player       = lichess_.player;
        q.player_white = board.sideToMove() == chess::Color::WHITE;
    }
    std::string key = fmt::format("{}|{}", source, fen);
    for (const auto& s : q.speeds) key += "|" + s;
    for (const auto r : q.ratings) key += fmt::format("|{}", r);
    key += "|" + q.player;
    if (q.history) key += "|months";
    // The filters of a move's months: those of the rows.
    std::string filters;
    for (const auto& s : q.speeds) filters += "|" + s;
    for (const auto r : q.ratings) filters += fmt::format("|{}", r);
    const auto found = explorer_answers_.find(key);
    if (found == explorer_answers_.end()) {
        // Asked once the position has stayed a moment: stepping through a game asks nothing on the way.
        const double now = ImGui::GetTime();
        if (fen != explorer_seen_) {
            explorer_seen_  = fen;
            explorer_since_ = now;
        }
        if (now - explorer_since_ > 0.4 && !explorer_job_.valid()) {
            explorer_asked_     = key;
            explorer_job_since_ = now;
            explorer_job_       = std::async(std::launch::async, [q] { return lichess::explorer(q); });
        }
        tree_rows_.clear();
        tree_rows_of_.clear();
        // A player's games are gathered by Lichess the first time they are asked for: it can take a minute.
        tree_summary_ = source == 2 && explorer_job_.valid() && explorer_asked_ == key && now - explorer_job_since_ > 5
                            ? trf("Lichess is gathering {}'s games: the first time can take a minute…", q.player)
                            : std::string(TR("Asking Lichess…"));
        return true;
    }
    if (tree_rows_of_ == key) return true;
    tree_rows_of_ = key;
    tree_rows_.clear();
    tree_games_.clear();
    tree_row_     = -1;
    const auto& a = found->second;
    if (!a.value) {
        const auto pause = HostGate::instance().pausedFor("explorer.lichess.ovh");
        tree_summary_ =
            pause.count() > 0
                ? trf("Too many requests in a short time: Lichess asked for a pause. Trying again in {} s.",
                      std::chrono::duration_cast<std::chrono::seconds>(pause).count())
            : a.net == lichess::NetError::Offline
                ? std::string(TR("Lichess did not answer: asked again in a minute. The local databases answer as "
                                 "always."))
                : trf("Lichess did not answer ({}): asked again in a minute. The local databases answer as always.",
                      lichess::describe(a));
        // Asked again after a minute, or at once in another position: never in a loop.
        if (ImGui::GetTime() - explorer_failed_[key] > 60) {
            explorer_answers_.erase(found);
            explorer_failed_.erase(key);
        }
        tree_rows_of_.clear();
        return true;
    }
    const auto& e   = *a.value;
    const auto all  = e.white + e.draws + e.black;
    const bool side = board.sideToMove() == chess::Color::WHITE;
    tree_summary_ =
        trn("{} game", "{} games", static_cast<int>(std::min<uint64_t>(all, 1000000000)), ui::exactCount(all));
    if (all > 0)
        tree_summary_ += trf(" · White {} % · draws {} % · Black {} %", std::lround(100.0 * e.white / all),
                             std::lround(100.0 * e.draws / all), std::lround(100.0 * e.black / all));
    if (!e.opening.empty()) tree_summary_ += fmt::format(" · {} {}", e.eco, e.opening);
    // Where its games go, month by month (#56).
    switch (lichess::trendOf(e.history)) {
        case lichess::Trend::Rising:
            tree_summary_ += std::string(" · ") + TR("more played lately");
            break;
        case lichess::Trend::Falling:
            tree_summary_ += std::string(" · ") + TR("less played lately");
            break;
        case lichess::Trend::Steady:
            tree_summary_ += std::string(" · ") + TR("played as much lately");
            break;
        case lichess::Trend::Unknown:
            break;
    }
    tree_games_        = e.top_games;  // the games the explorer names, a click away (#56)
    tree_games_source_ = source;
    for (const auto& m : e.moves) {
        chess::Move move = chess::Move::NO_MOVE;
        try {
            move = chess::uci::uciToMove(board, m.uci);
        } catch (...) {
        }
        if (move == chess::Move::NO_MOVE) continue;
        TreeRow row;
        row.move        = move;
        row.san         = chess::uci::moveToSan(board, move);
        const auto done = m.white + m.draws + m.black;
        row.games       = done;
        if (done > 0) {
            row.white = static_cast<double>(m.white) / done;
            row.draws = static_cast<double>(m.draws) / done;
            row.black = static_cast<double>(m.black) / done;
            row.score = static_cast<int>(
                std::lround(100.0 * (static_cast<double>(side ? m.white : m.black) + m.draws / 2.0) / done));
        }
        row.elo = m.average_rating;
        if (q.history) {  // its position's months, asked on their own
            auto after = board;
            after.makeMove(move);
            row.months = fmt::format("months|{}{}", after.getFen(), filters);
        }
        tree_rows_.push_back(std::move(row));
    }
    if (tree_rows_.empty() && all == 0) tree_summary_ = TR("No game of this source reaches this position.");
    return true;
}

void App::pollExplorerMonths() {
    using namespace std::chrono_literals;
    if (isReady(explorer_month_job_)) {
        auto answer = explorer_month_job_.get();
        if (!answer.value) explorer_failed_[explorer_month_asked_] = ImGui::GetTime();
        explorer_answers_[explorer_month_asked_] = std::move(answer);
    }
    if (explorer_month_job_.valid() || !lichess_account_ || !lichess_.explorer_months || aidsLocked()) return;
    if (ImGui::GetTime() - explorer_since_ < 0.4) return;  // the position stayed a moment
    // A move's months at a time, those Lichess did not answer asked again after a minute.
    for (const auto& row : tree_rows_) {
        if (row.months.empty()) continue;
        if (const auto known = explorer_answers_.find(row.months); known != explorer_answers_.end()) {
            if (known->second.value || ImGui::GetTime() - explorer_failed_[row.months] < 60) continue;
            explorer_answers_.erase(known);
        }
        const auto from = row.months.find('|') + 1;
        lichess::ExplorerQuery q;
        q.source              = lichess::ExplorerSource::Lichess;
        q.fen                 = row.months.substr(from, row.months.find('|', from) - from);
        q.speeds              = lichess_.explorer_speeds;
        q.ratings             = lichess_.explorer_ratings;
        q.token               = lichess_account_->token;
        q.history             = true;
        q.moves               = 1;
        q.top_games           = 0;
        explorer_month_asked_ = row.months;
        explorer_month_job_   = std::async(std::launch::async, [q] { return lichess::explorer(q); });
        return;
    }
}

void App::drawTreeGames() {
    using namespace std::chrono_literals;
    if (isReady(explorer_game_job_)) {
        const auto answer = explorer_game_job_.get();
        if (answer.value) {
            readOnBoard(*answer.value, explorer_game_title_);
            // At the position the tree showed, which the game went through.
            const auto fields = [](const std::string& fen) {
                std::size_t at = 0;
                for (int i = 0; i < 3 && at != std::string::npos; ++i) at = fen.find(' ', at + 1);
                return fen.substr(0, at);
            };
            auto& b      = board_;
            auto board   = boardAt(b.game, b.chess960, 0);
            const auto n = static_cast<int>(b.game.moves.size());
            for (int ply = 0; ply <= n; ++ply) {
                if (fields(board.getFen()) == fields(explorer_game_fen_)) {
                    b.ply = ply;
                    break;
                }
                if (ply < n) board.makeMove(b.game.moves[static_cast<std::size_t>(ply)].move);
            }
        } else {
            explorer_game_error_ = lichess::describe(answer);
        }
    }
    if (tree_games_.empty() || !lichess_account_) return;
    const ui::TopicRegion topic(guide::topics::ExplorerMore);
    ImGui::TextDisabled("%s", tree_games_source_ == 0 ? TR("Masters' games") : TR("Strong games on Lichess"));
    for (const auto& g : tree_games_) {
        const char* result = g.winner == 'w' ? "1-0" : g.winner == 'b' ? "0-1" : "½-½";
        const auto title   = fmt::format("{} – {} · {} · {}", g.white, g.black, result, g.year);
        ImGui::BeginDisabled(explorer_game_job_.valid());
        if (ImGui::Selectable(fmt::format("{}##tree_game_{}", title, g.id).c_str(), false)) {
            explorer_game_title_ = title;
            explorer_game_fen_   = boardFen();
            explorer_game_error_.clear();
            explorer_game_job_ = std::async(
                std::launch::async, [id = g.id, token = lichess_account_->token, masters = tree_games_source_ == 0] {
                    return masters ? lichess::mastersGame(id, token) : lichess::gamePgn(id, token);
                });
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", trf("{} – {} ({} – {}): a click opens it on the board, asked of Lichess.", g.white,
                                        g.black, g.white_rating, g.black_rating)
                                        .c_str());
    }
    if (explorer_game_job_.valid())
        ImGui::TextDisabled("%s", TR("Asking Lichess for the game…"));
    else if (!explorer_game_error_.empty())
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s",
                           trf("Lichess did not give the game: {}", explorer_game_error_).c_str());
}

}  // namespace fcg
