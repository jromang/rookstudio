// The Chess space's Lichess page (mockup M4, board 1): the account linked or not, a player's games kept in a database,
// the official broadcasts, and the online services the analysis may ask, each decided here. Nothing on it is needed:
// the program works without an account and without the Internet (R15); what leaves the machine is said where it is
// decided (R16).

#include <algorithm>
#include <chrono>
#include <ctime>
#include <string>

#include <SDL3/SDL.h>
#include <fmt/include/fmt/format.h>
#include <imgui.h>

#include "app.hpp"
#include "console.hpp"
#include "https.hpp"
#include "i18n.hpp"
#include "lichess_account.hpp"
#include "lichess_broadcasts.hpp"
#include "lichess_games.hpp"
#include "storage.hpp"
#include "ui.hpp"

namespace fcg {

namespace {

float px(float v) { return v * ImGui::GetFontSize() / 16.0f; }

template <typename T>
bool ready(const std::future<T>& f) {
    return f.valid() && f.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}

// A day as the page says it: "18 November 2026".
std::string dateText(int64_t seconds) {
    const auto t = static_cast<std::time_t>(seconds);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
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

void App::pollLichess() {
    // The link: the browser said yes or no, or the wait ended.
    if (lichess_link_ && ready(lichess_link_->result)) {
        auto answer = lichess_link_->result.get();
        lichess_link_.reset();
        if (answer.value) {
            try {
                lichess::saveAccount(storage_.root(), *answer.value);
                lichess_account_ = *answer.value;
                lichess_error_.clear();
                if (lichess_.player.empty()) lichess_.player = answer.value->username;
                saveLichessSettings();
                if (window_) SDL_RaiseWindow(window_);  // back in front of the browser
            } catch (const std::exception& e) {
                lichess_error_ = e.what();
            }
        } else if (answer.error != "cancelled") {
            lichess_error_ = answer.error;
        }
    }
    if (lichess_unlink_.valid() && ready(lichess_unlink_)) {
        const auto error = lichess_unlink_.get();
        if (!error.empty()) Console::instance().add(trf("Lichess: the token could not be revoked: {}", error), true);
    }
    // A player's games: the sync ends.
    if (lichess_sync_ && ready(lichess_sync_->done)) {
        auto report          = lichess_sync_->done.get();
        const auto index     = lichess_sync_->index;
        lichess_sync_report_ = report;
        lichess_sync_.reset();
        if (report.added > 0 && index < shelf().entries().size()) shelf().rebuildTree(index);
    }
    // The broadcasts: asked once a page shows them, again after ten minutes.
    if (lichess_broadcasts_job_.valid() && ready(lichess_broadcasts_job_)) {
        lichess_broadcasts_      = lichess_broadcasts_job_.get();
        lichess_broadcasts_time_ = std::chrono::steady_clock::now();
    }
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
    ui::beginCard("chess_lichess");
    ImGui::PushTextWrapPos();
    ImGui::TextUnformatted("Lichess");
    ImGui::SameLine();
    ImGui::TextDisabled("%s",
                        lichess_account_ ? trf("· {}", lichess_account_->username).c_str() : TR("· no account linked"));
    ImGui::SameLine();
    if (ImGui::SmallButton(TR("The Lichess page ›"))) page_ = Page::Lichess;
    if (const auto base = lichessBase()) {
        const auto facts = shelf().facts(*base);
        auto line        = trf("Lichess · {}: {}", lichess_.player, ui::exactCount(facts.games));
        if (facts.changed) line += " · " + trf("synced {}", ui::dayText(facts.changed));
        // The new games, counted when the home opens, at most once an hour, if the user chose it (M4, board 3).
        if (lichess_.count_new && !lichess_sync_) {
            const auto now = std::chrono::steady_clock::now();
            if (!lichess_count_job_.valid() &&
                (lichess_counted_.time_since_epoch().count() == 0 || now - lichess_counted_ > std::chrono::hours(1))) {
                lichess_counted_ = now;
                lichess_count_job_ =
                    std::async(std::launch::async, [folder = shelf().entries()[*base].folder, player = lichess_.player,
                                                    token = lichess_account_ ? lichess_account_->token : std::string(),
                                                    filters = lichessFilters(), cancel = lichess_count_cancel_] {
                        return lichess::countNewGames(folder, player, token, filters, cancel.get());
                    });
            }
            if (lichess_count_job_.valid() &&
                lichess_count_job_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
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
                ImGui::PushID(static_cast<int>(i));
                ImGui::TextUnformatted(trf("Live: {} · {}", active[i].tour.name, active[i].round.name).c_str());
                ImGui::SameLine();
                if (ImGui::SmallButton(TR("Follow"))) followBroadcast(active[i]);
                ImGui::PopID();
            }
        }
    }
    ImGui::PopTextWrapPos();
    ui::endCard();
}

void App::askBroadcasts() {
    if (lichess_broadcasts_job_.valid()) return;
    const auto now = std::chrono::steady_clock::now();
    if (lichess_broadcasts_.value && now - lichess_broadcasts_time_ < std::chrono::minutes(10)) return;
    if (!lichess_broadcasts_.value && !lichess_broadcasts_.error.empty() &&
        now - lichess_broadcasts_time_ < std::chrono::minutes(1))
        return;
    lichess_broadcasts_time_ = now;
    lichess_broadcasts_job_  = std::async(std::launch::async, [] { return lichess::topBroadcasts(); });
}

void App::startLichessLink() {
    lichess_error_.clear();
    auto link     = std::make_unique<LichessLink>();
    link->cancel  = std::make_shared<std::atomic<bool>>(false);
    auto url      = std::make_shared<std::string>();
    auto opened   = std::make_shared<std::atomic<bool>>(true);
    link->url     = url;
    link->opened  = opened;
    link->result  = std::async(std::launch::async, [cancel = link->cancel, url, opened] {
        return lichess::link(
            [url, opened](const std::string& page) {
                *url    = page;
                *opened = SDL_OpenURL(page.c_str());
                return opened->load();
            },
            {}, std::chrono::minutes(5), cancel.get());
    });
    lichess_link_ = std::move(link);
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
        ImGui::TextDisabled("%s", TR("Lichess sends the browser back to 127.0.0.1, a port only RookStudio listens on "
                                     "while it links. Without an answer in 5 minutes, the link stops by itself."));
    } else if (lichess_account_) {
        const auto& a = *lichess_account_;
        ImGui::TextUnformatted(trf("{} linked", a.username).c_str());
        if (a.expires > 0) {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", trf("· the token lasts until {}", dateText(a.expires)).c_str());
        }
        if (ImGui::SmallButton(TR("Unlink"))) {
            lichess_unlink_ =
                std::async(std::launch::async, [root = storage_.root()] { return lichess::forgetAccount(root); });
            lichess_account_.reset();
        }
        ImGui::TextDisabled("%s",
                            TR("The token is kept in your profile, readable by you alone. Unlinking revokes it at "
                               "Lichess and deletes it here; your databases stay."));
    } else {
        ImGui::TextDisabled("%s", TR("No account linked. Linked, it brings:"));
        ImGui::BulletText("%s", TR("Lichess's opening explorer in the Tree tab: masters, all players, one player;"));
        ImGui::BulletText("%s", TR("your games synced faster, your private ones too;"));
        ImGui::BulletText("%s", TR("later: your studies, playing online (the next previews)."));
        // What went wrong last, and the gesture that mends it (M4, board 2).
        if (!lichess_error_.empty()) {
            const bool refused = lichess_error_.find("refused") != std::string::npos;
            const bool late    = lichess_error_.find("in time") != std::string::npos;
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s",
                               refused ? TR("You refused the access on Lichess: nothing is linked.")
                               : late ? TR("No answer from Lichess in 5 minutes: the link stopped, the port is closed.")
                                      : trf("Lichess cannot be reached from here ({}): the link waits for a "
                                            "connection. Everything else works without it.",
                                            lichess_error_)
                                            .c_str());
        }
        if (ui::iconButton("lichess_link", ui::Icon::External,
                           lichess_error_.empty() ? TR("Link the account…") : TR("Try again")))
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
                                     "cancel (R18)."));
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
            if (!r.error.empty())
                ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s",
                                   trf("Stopped: {}. The games received are in the database; Sync goes on from the "
                                       "next one.",
                                       r.error)
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
        changed |= ImGui::RadioButton(TR("A year"), &lichess_.sync_since, 1);
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
        if (ImGui::Checkbox(TR("Count the new games when the home opens"), &lichess_.count_new)) {
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

void App::drawLichessBroadcasts() {
    askBroadcasts();
    ui::beginCard("lichess_broadcasts");
    ImGui::PushTextWrapPos();
    ImGui::TextUnformatted(TR("Official broadcasts"));
    const auto& b = lichess_broadcasts_;
    if (!b.value) {
        ImGui::TextDisabled("%s", b.error.empty() ? TR("Asking Lichess…")
                                                  : trf("Lichess cannot be reached from here: {}.", b.error).c_str());
    } else {
        const auto row = [&](const lichess::BroadcastEntry& e, bool live) {
            ImGui::PushID(e.round.id.c_str());
            ImGui::GetWindowDrawList()->AddCircleFilled(
                ImVec2(ImGui::GetCursorScreenPos().x + px(5), ImGui::GetCursorScreenPos().y + px(9)), px(4),
                live ? IM_COL32(209, 74, 59, 255) : IM_COL32(201, 206, 214, 255));
            ImGui::Dummy(ImVec2(px(12), 0));
            ImGui::SameLine();
            ImGui::TextUnformatted(e.tour.name.c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("%s", (live ? trf("live · {}", e.round.name) : e.round.name).c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton(TR("Follow"))) followBroadcast(e);
            ImGui::PopID();
        };
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
    ImGui::TextDisabled("%s",
                        TR("Follow opens the round in the Live mosaic, each game analysed by your engine, on your "
                           "machine. Lichess limits the rounds followed at once: 4 without an account, 8 with "
                           "one."));
    ImGui::PopTextWrapPos();
    ui::endCard();
}

void App::followBroadcast(const lichess::BroadcastEntry& entry) {
    for (const auto& f : followed_rounds_)
        if (f.entry.round.id == entry.round.id) return;
    // Lichess's limit: four rounds at once without an account, eight with one.
    const std::size_t most = lichess_account_ ? 8 : 4;
    if (followed_rounds_.size() >= most) {
        Console::instance().add(trf("Lichess lets {} rounds be followed at once: stop one first.", most), true);
        return;
    }
    FollowedRound f;
    f.entry    = entry;
    f.follower = std::make_unique<lichess::RoundFollower>(entry.round.id,
                                                          lichess_account_ ? lichess_account_->token : std::string());
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
        ImGui::TextUnformatted(fmt::format("{} · {}", f.entry.tour.name, f.entry.round.name).c_str());
        ImGui::SameLine();
        const auto error = f.follower->error();
        if (f.follower->streaming())
            ImGui::TextColored(ImVec4(0.82f, 0.29f, 0.23f, 1.0f), "%s", TR("live"));
        else
            ImGui::TextDisabled("%s", error.empty()
                                          ? TR("connecting…")
                                          : trf("the stream broke ({}): it opens again by itself", error).c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton(TR("The mosaic"))) openBroadcastMosaic(f.entry.round.id);
        ImGui::SameLine();
        if (ImGui::SmallButton(TR("Stop following"))) stop = r;
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
    ImGui::TextDisabled("%s", TR("The moves come from Lichess's stream, opened again after a break (R6)."));
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
            TR("Sent for a position of 7 pieces or fewer: its FEN, without an account. Beyond the tables on your "
               "disk."));
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
    ImGui::PushTextWrapPos();
    ImGui::TextDisabled("%s", TR("Everything here is optional: RookStudio works without an account and without the "
                                 "Internet (R15). What goes to Lichess is said where you decide it (R16)."));
    ImGui::PopTextWrapPos();
    ImGui::Dummy(ImVec2(0, px(6)));
    const float avail = ImGui::GetContentRegionAvail().x;
    const bool two    = avail > px(900);
    const float col   = two ? (avail - px(14)) / 2 : avail;
    ImGui::BeginChild("##lichess_left", ImVec2(col, 0), ImGuiChildFlags_AutoResizeY);
    drawLichessAccount();
    ImGui::Dummy(ImVec2(0, px(8)));
    drawLichessGames();
    ImGui::EndChild();
    if (two) ImGui::SameLine(0, px(14));
    ImGui::BeginChild("##lichess_right", ImVec2(col, 0), ImGuiChildFlags_AutoResizeY);
    drawLichessBroadcasts();
    ImGui::Dummy(ImVec2(0, px(8)));
    if (!followed_rounds_.empty()) {
        drawLichessFollowed();
        ImGui::Dummy(ImVec2(0, px(8)));
    }
    drawLichessOnline();
    ImGui::EndChild();
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
    if (!lichess_account_ || !lichess_.explorer) {
        drawLichessTreeCard(lichess_account_.has_value());
        return false;
    }
    // The answers that came.
    if (explorer_job_.valid() && explorer_job_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        if (explorer_answers_.size() > 2000) explorer_answers_.clear();
        explorer_answers_[explorer_asked_] = explorer_job_.get();
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
    }
    if (source == 2) {
        q.player       = lichess_.player;
        q.player_white = board.sideToMove() == chess::Color::WHITE;
    }
    std::string key = fmt::format("{}|{}", source, fen);
    for (const auto& s : q.speeds) key += "|" + s;
    for (const auto r : q.ratings) key += fmt::format("|{}", r);
    key += "|" + q.player;
    const auto found = explorer_answers_.find(key);
    if (found == explorer_answers_.end()) {
        // Asked once the position has stayed a moment: stepping through a game asks nothing on the way.
        const double now = ImGui::GetTime();
        if (fen != explorer_seen_) {
            explorer_seen_  = fen;
            explorer_since_ = now;
        }
        if (now - explorer_since_ > 0.4 && !explorer_job_.valid()) {
            explorer_asked_ = key;
            explorer_job_   = std::async(std::launch::async, [q] { return lichess::explorer(q); });
        }
        tree_rows_.clear();
        tree_rows_of_.clear();
        tree_summary_ = TR("Asking Lichess…");
        return true;
    }
    if (tree_rows_of_ == key) return true;
    tree_rows_of_ = key;
    tree_rows_.clear();
    tree_row_     = -1;
    const auto& a = found->second;
    if (!a.value) {
        const auto pause = HostGate::instance().pausedFor("explorer.lichess.ovh");
        tree_summary_ =
            pause.count() > 0
                ? trf("Too many requests in a short time: Lichess asked for a pause. Trying again in {} s.",
                      std::chrono::duration_cast<std::chrono::seconds>(pause).count())
                : trf("Lichess cannot be reached from here ({}): trying again at the next position. The local "
                      "databases answer as always.",
                      a.error);
        explorer_answers_.erase(found);  // asked again at the next look
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
        tree_rows_.push_back(std::move(row));
    }
    if (tree_rows_.empty() && all == 0) tree_summary_ = TR("No game of this source reaches this position.");
    return true;
}

}  // namespace fcg
