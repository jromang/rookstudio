// A Lichess link pasted (#47, mockup M8, board "Lien"): Ctrl+V on the Board page or on the Databases page. The link is
// recognised without asking Lichess anything; a card then says what it is, where its games go (the destination offered
// first, the others beside it), what is asked of Lichess and the right the token needs; Enter does it, Escape leaves
// it. One game or one chapter opens on the board; a study, a broadcast, an arena, a swiss or the bookmarks come into a
// database, as many games as they have, in the background; a player's link goes to the sync of their games; a round
// under way can be followed live instead.

#include <algorithm>
#include <string>

#include <SDL3/SDL.h>
#include <fmt/include/fmt/format.h>
#include <imgui.h>

#include <study/store.hpp>

#include "app.hpp"
#include "board_text.hpp"
#include "console.hpp"
#include "guide.hpp"
#include "i18n.hpp"
#include "ui.hpp"

namespace fcg {

namespace {

float px(float v) { return v * ImGui::GetFontSize() / 16.0f; }

using lichess::LinkKind;

bool onBoard(LinkKind kind) { return kind == LinkKind::Game || kind == LinkKind::Chapter; }

const char* kindTitle(LinkKind kind) {
    switch (kind) {
        case LinkKind::Game:
            return TR("A game");
        case LinkKind::Study:
            return TR("A study");
        case LinkKind::Chapter:
            return TR("A chapter of a study");
        case LinkKind::BroadcastRound:
            return TR("A broadcast round");
        case LinkKind::BroadcastTour:
            return TR("A broadcast tournament");
        case LinkKind::Arena:
            return TR("An arena");
        case LinkKind::Swiss:
            return TR("A Swiss tournament");
        case LinkKind::Player:
            return TR("A player's games");
        case LinkKind::Bookmarks:
            return TR("Your bookmarked games");
    }
    return "";
}

const char* kindWhat(LinkKind kind) {
    switch (kind) {
        case LinkKind::Study:
            return TR("Its chapters as games, with their comments, arrows and circles.");
        case LinkKind::BroadcastRound:
            return TR("Its games as they are now; a round under way can be followed live instead.");
        case LinkKind::BroadcastTour:
            return TR("The games of its rounds.");
        case LinkKind::Arena:
        case LinkKind::Swiss:
            return TR("Its games, as many as it has.");
        case LinkKind::Player:
            return TR("Their public games, with the filters of the sync on the Lichess page.");
        case LinkKind::Bookmarks:
            return TR("The games you bookmarked on Lichess.");
        case LinkKind::Game:
        case LinkKind::Chapter:
            break;
    }
    return "";
}

const char* kindSent(LinkKind kind) {
    switch (kind) {
        case LinkKind::Game:
            return TR("Asked: the game by its id, without an account.");
        case LinkKind::Study:
        case LinkKind::Chapter:
            return TR("Asked: the study as PGN; a private one with your token.");
        case LinkKind::BroadcastRound:
            return TR("Asked: the round as PGN, with your token.");
        case LinkKind::BroadcastTour:
            return TR("Asked: the tournament's rounds as PGN, with your token.");
        case LinkKind::Arena:
        case LinkKind::Swiss:
            return TR("Asked: the tournament's games, streamed, without an account.");
        case LinkKind::Player:
            return TR("Asked: their name and the filters of the sync.");
        case LinkKind::Bookmarks:
            return TR("Asked: your bookmarks, with your token.");
    }
    return "";
}

// The database a link's games go into unless another is chosen.
std::string defaultBase(LinkKind kind) {
    switch (kind) {
        case LinkKind::BroadcastRound:
        case LinkKind::BroadcastTour:
            return TR("Broadcasts");
        case LinkKind::Study:
            return TR("Lichess studies");
        case LinkKind::Arena:
        case LinkKind::Swiss:
            return TR("Lichess tournaments");
        case LinkKind::Bookmarks:
            return TR("Lichess bookmarks");
        default:
            return TR("Lichess games");
    }
}

// A line of a game's tags for the card: its players, result, event and length.
std::string gameLine(const std::string& pgn) {
    const auto tag = [&](const char* name) {
        const auto key = fmt::format("[{} \"", name);
        const auto at  = pgn.find(key);
        if (at == std::string::npos) return std::string();
        const auto from = at + key.size();
        const auto to   = pgn.find('"', from);
        return to == std::string::npos ? std::string() : pgn.substr(from, to - from);
    };
    std::string line  = fmt::format("{} – {}", tag("White"), tag("Black"));
    const auto result = tag("Result");
    if (!result.empty() && result != "*") line += " · " + result;
    const auto event = tag("Event");
    if (!event.empty()) line += " · " + event;
    return line;
}

// A slug of a broadcast's address as a name: "open-2026" → "open 2026".
std::string slugName(std::string slug) {
    std::replace(slug.begin(), slug.end(), '-', ' ');
    return slug;
}

}  // namespace

bool App::offerLichessLink(const std::string& text, bool databases) {
    const auto link = lichess::parseLink(text);
    if (!link) {
        if (!lichess::lichessAddress(text)) return false;
        const char* said = TR("RookStudio does not know this Lichess address: nothing was asked of Lichess.");
        boardToast(said);
        db_message_ = said;
        Console::instance().add(fmt::format("{} {}", said, text), false);
        return true;
    }
    LinkCard card;
    card.link = *link;
    card.url  = text;
    while (!card.url.empty() && std::isspace(static_cast<unsigned char>(card.url.back()))) card.url.pop_back();
    while (!card.url.empty() && std::isspace(static_cast<unsigned char>(card.url.front()))) card.url.erase(0, 1);
    card.to_base = !onBoard(link->kind) || databases;
    if (databases && db_selected_ >= 0 && db_selected_ < static_cast<int>(shelf().entries().size()) &&
        !shelf().entries()[static_cast<std::size_t>(db_selected_)].source)
        card.base = db_selected_;
    // One game or one chapter: its PGN at once, for its line on the card and to open it.
    if (onBoard(link->kind)) {
        const auto token = lichess_account_ ? lichess_account_->token : std::string();
        card.pgn         = std::async(std::launch::async, [l = *link, token, cancel = card.cancel] {
            return lichess::linkPgn(l, token, cancel.get());
        });
    }
    closeLinkCard();  // the card of a link pasted before
    link_card_ = std::move(card);
    return true;
}

void App::closeLinkCard() {
    if (!link_card_) return;
    // Its game may still be asked, behind another request to Lichess or its pause after a 429 (#98).
    *link_card_->cancel = true;
    jobs_.park(std::move(link_card_->pgn));
    link_card_.reset();
}

void App::pollLinkJob() {
    if (link_card_ && link_card_->pgn.valid() &&
        link_card_->pgn.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        link_card_->got = link_card_->pgn.get();
    if (!link_job_ || !link_job_->result.valid() ||
        link_job_->result.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        return;
    const auto report = link_job_->result.get();
    auto& job         = *link_job_;
    if (!report.error.empty()) {
        job.said = trf("Stopped: {}. The games received are in “{}”.", report.error, job.name);
    } else if (report.cancelled) {
        job.said = trf("Stopped at your request: {} games kept in “{}”.", ui::exactCount(report.added), job.name);
    } else {
        job.said = trn("Done: {} new game in “{}”", "Done: {} new games in “{}”", report.added,
                       ui::exactCount(report.added), job.name);
        if (report.duplicates > 0) job.said += trf(", {} already there left out", ui::exactCount(report.duplicates));
        job.said += ".";
    }
    job.done = true;
    if (report.added > 0) shelf().rebuildTree(job.index);  // its opening tree takes the new games in
}

void App::actOnLink() {
    if (!link_card_) return;
    auto& card       = *link_card_;
    const auto& link = card.link;
    const auto token = lichess_account_ ? lichess_account_->token : std::string();
    // On the board: the game or chapter asked for at once.
    if (onBoard(link.kind) && !card.to_base) {
        if (!card.got || !card.got->value) return;
        readOnBoard(*card.got->value, gameLine(*card.got->value));
        auto& b = board_;
        if (link.white) b.flip = !*link.white;
        if (link.ply >= 0) b.ply = std::clamp(link.ply, 0, static_cast<int>(b.game.moves.size()));
        page_ = Page::Board;
        closeLinkCard();
        return;
    }
    if (link.kind == LinkKind::Player) {
        lichess_.player = link.id;
        saveLichessSettings();
        startLichessSync();
        page_ = Page::Lichess;
        closeLinkCard();
        return;
    }
    if (link.kind == LinkKind::BroadcastRound && card.follow) {
        // Followed live, as the Lichess page's Follow does: its names from its address until Lichess sends them.
        lichess::BroadcastEntry entry;
        entry.round.id   = link.id;
        entry.round.url  = card.url;  // its tournament asked of Lichess from it (#55)
        const auto parts = [&] {
            std::vector<std::string> out;
            auto u = card.url.substr(
                card.url.find("/broadcast/") == std::string::npos ? 0 : card.url.find("/broadcast/") + 11);
            for (std::size_t at = 0; at < u.size();) {
                const auto next = u.find('/', at);
                out.push_back(u.substr(at, next == std::string::npos ? std::string::npos : next - at));
                if (next == std::string::npos) break;
                at = next + 1;
            }
            return out;
        }();
        if (parts.size() >= 2) {
            entry.tour.name  = slugName(parts[0]);
            entry.round.name = slugName(parts[1]);
        }
        followBroadcast(entry);
        page_ = Page::Lichess;
        closeLinkCard();
        return;
    }
    if (link_job_ && !link_job_->done) return;  // one at a time
    // Into a database: the one chosen, else the one by default, made when it is missing.
    auto& s           = shelf();
    std::size_t index = s.entries().size();
    std::string name  = defaultBase(link.kind);
    if (card.base >= 0 && card.base < static_cast<int>(s.entries().size())) {
        index = static_cast<std::size_t>(card.base);
        name  = s.entries()[index].name;
    } else {
        for (std::size_t i = 0; i < s.entries().size(); ++i)
            if (!s.entries()[i].source && s.entries()[i].name == name) index = i;
    }
    std::shared_ptr<study::Database> db;
    try {
        if (index == s.entries().size()) index = s.create(name);
        db = s.open(index);
    } catch (const std::exception& e) {
        db_message_ = e.what();
        return;
    }
    if (!db || db->readOnly()) {
        db_message_ =
            trf("“{}” is read-only while another program writes to it: import into it once it has finished.", name);
        return;
    }
    LinkJob job;
    job.link   = link;
    job.index  = index;
    job.name   = name;
    job.result = std::async(std::launch::async, [db, l = link, token, games = job.games, cancel = job.cancel] {
        return lichess::importLink(*db, l, token, *games, *cancel);
    });
    link_job_  = std::move(job);
    closeLinkCard();  // a game's link imported: its game may still be asked for the card
}

void App::drawLinkCard() {
    // The import under way, or its outcome, until it is put away.
    if (link_job_) {
        auto& job = *link_job_;
        ui::beginCard("lichess_link_job");
        ImGui::PushTextWrapPos();
        if (!job.done) {
            ImGui::TextUnformatted(
                trf("Lichess · {}: {} games received", job.name, ui::exactCount(job.games->load())).c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton(TR("Cancel"))) *job.cancel = true;
        } else {
            ImGui::TextUnformatted(job.said.c_str());
            if (ImGui::SmallButton(TR("Open the database"))) {
                page_        = Page::Databases;
                db_selected_ = static_cast<int>(job.index);
                db_list_     = true;
                link_job_.reset();
            }
            if (link_job_) {
                ImGui::SameLine();
                if (ImGui::SmallButton(TR("Close"))) link_job_.reset();
            }
        }
        ImGui::PopTextWrapPos();
        ui::endCard();
    }
    if (!link_card_) return;
    auto& card       = *link_card_;
    const auto& link = card.link;
    ui::TopicRegion topic(guide::topics::LichessLinks);
    ui::beginCard("lichess_link");
    ImGui::PushTextWrapPos();
    ImGui::TextDisabled("%s", card.url.c_str());
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.15f);
    if (link.kind == LinkKind::Player)
        ImGui::TextUnformatted(trf("{}'s games", link.id).c_str());
    else
        ImGui::TextUnformatted(kindTitle(link.kind));
    ImGui::PopFont();
    // What it is: a game's players and result once Lichess sent it.
    if (onBoard(link.kind)) {
        if (!card.got)
            ImGui::TextDisabled("%s", TR("Asking Lichess…"));
        else if (card.got->value)
            ImGui::TextDisabled("%s", gameLine(*card.got->value).c_str());
        else
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s", card.got->error.c_str());
    } else {
        ImGui::TextDisabled("%s", kindWhat(link.kind));
    }
    // Where it goes: the destinations of its kind, the first offered.
    std::vector<std::pair<const char*, int>> places;  // label, what it means: 0 the board, 1 a database, 2 live
    if (onBoard(link.kind)) places.push_back({TR("On the board"), 0});
    if (link.kind != LinkKind::Player) places.push_back({TR("Into a database"), 1});
    if (link.kind == LinkKind::Player) places.push_back({TR("Into their database, kept up to date"), 1});
    if (link.kind == LinkKind::BroadcastRound) places.push_back({TR("Follow it live"), 2});
    for (std::size_t i = 0; i < places.size(); ++i) {
        const int what = places[i].second;
        const bool on  = what == 0 ? !card.to_base : what == 2 ? card.follow : card.to_base && !card.follow;
        if (i > 0) ImGui::SameLine();
        if (ImGui::RadioButton(places[i].first, on)) {
            card.to_base = what != 0;
            card.follow  = what == 2;
        }
    }
    // The database, when its games go into one: the one by default (made when missing) or another of yours.
    if (card.to_base && !card.follow && link.kind != LinkKind::Player) {
        auto& s            = shelf();
        const auto fresh   = defaultBase(link.kind);
        const bool chosen  = card.base >= 0 && card.base < static_cast<int>(s.entries().size());
        const auto current = chosen ? s.entries()[static_cast<std::size_t>(card.base)].name : fresh;
        ImGui::SetNextItemWidth(px(260));
        if (ImGui::BeginCombo("##link_base", current.c_str())) {
            if (ImGui::Selectable(fresh.c_str(), !chosen)) card.base = -1;
            for (std::size_t i = 0; i < s.entries().size(); ++i) {
                const auto& e = s.entries()[i];
                if (e.source || e.name == fresh) continue;  // tournaments and files read without a copy
                if (ImGui::Selectable(e.name.c_str(), card.base == static_cast<int>(i)))
                    card.base = static_cast<int>(i);
            }
            ImGui::EndCombo();
        }
    }
    // What it needs of the account.
    const bool broadcast = link.kind == LinkKind::BroadcastRound || link.kind == LinkKind::BroadcastTour;
    bool blocked         = false;
    if (link.kind == LinkKind::Bookmarks && !lichess_account_) {
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s",
                           TR("Your bookmarks need a linked Lichess account."));
        if (ImGui::SmallButton(TR("Link the account…"))) startLichessLink();
        blocked = true;
    } else if (broadcast && lichess_account_ && !lichess_account_->has(lichess::SCOPE_STUDY_READ)) {
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s",
                           TR("Lichess asks for an account allowed to read studies and broadcasts: asked once, as to "
                              "follow a round."));
        if (ui::iconButton("link_scope_allow", ui::Icon::External, TR("Allow on Lichess…"))) {
            auto scopes = lichess_account_->scopes;
            scopes.push_back(lichess::SCOPE_STUDY_READ);
            startLichessLink(scopes, lichess_account_->username);
        }
    } else if (broadcast && !lichess_account_) {
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s",
                           TR("Without a linked account, Lichess slows broadcasts down heavily, and may refuse them."));
    }
    // The action, Enter; what is asked of Lichess.
    const char* action = card.follow ? TR("Follow") : card.to_base ? TR("Import") : TR("Open");
    const bool waiting = onBoard(link.kind) && !card.to_base && (!card.got || !card.got->value);
    const bool busy    = card.to_base && !card.follow && link.kind != LinkKind::Player && link_job_ && !link_job_->done;
    ImGui::BeginDisabled(blocked || waiting || busy);
    const bool enter = ImGui::IsKeyPressed(ImGuiKey_Enter, false) && !ImGui::GetIO().WantTextInput;
    const bool act   = ImGui::Button(action) || enter;
    if (enter) link_key_frame_ = ImGui::GetFrameCount();
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", busy ? TR("An import runs already: one at a time.") : TR("Enter"));
    ImGui::SameLine();
    if (ImGui::SmallButton(TR("Not now")) ||
        (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !ImGui::GetIO().WantTextInput)) {
        link_key_frame_ = ImGui::GetFrameCount();
        closeLinkCard();
        ImGui::PopTextWrapPos();
        ui::endCard();
        return;
    }
    ImGui::TextDisabled("%s", kindSent(link.kind));
    ImGui::PopTextWrapPos();
    ui::endCard();
    if (act && !blocked && !waiting && !busy) actOnLink();
}

}  // namespace fcg
