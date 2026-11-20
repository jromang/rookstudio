// Preparing a game against an opponent (#52, mockup M8, board "Prep"): the Lichess page's card. Their games of the last
// year (blitz, rapid, classical) come into "Lichess · opponent" through their sync, their level from their public
// profile; then, from that database's tree (prep.hpp), their games and years, the user's games against them, what they
// play, and the lines they play that the user's repertoires do not answer, each opened on the Board with a click.
// Locked while a game of the user's against them is played (fair play).

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>

#include <fmt/include/fmt/format.h>
#include <imgui.h>

#include "app.hpp"
#include "guide.hpp"
#include "i18n.hpp"
#include "ui.hpp"

namespace fcg {

namespace {

float px(float v) { return v * ImGui::GetFontSize() / 16.0f; }

std::string lower(std::string text) {
    for (auto& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

bool lichessName(std::string_view name) {
    if (name.size() < 2 || name.size() > 30) return false;
    return std::all_of(name.begin(), name.end(),
                       [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-'; });
}

// "1.e4 c5 2.Nf3": a line from the start, the move after it at the end when there is one.
std::string lineText(const std::vector<std::string>& line, const std::string& move) {
    std::vector<std::string> all = line;
    if (!move.empty()) all.push_back(move);
    std::string out;
    for (std::size_t i = 0; i < all.size(); ++i) {
        if (!out.empty()) out += ' ';
        out += i % 2 == 0 ? fmt::format("{}.{}", i / 2 + 1, all[i]) : all[i];
    }
    return out;
}

// "1.e4 67 %, 1.d4 33 %"; `black` writes "1...c5".
std::string movesText(const std::vector<PrepMove>& moves, bool black) {
    std::string out;
    for (const auto& m : moves) {
        if (!out.empty()) out += ", ";
        out += fmt::format("1{}{} {:.0f} %", black ? "..." : ".", m.san, m.share * 100);
    }
    return out;
}

std::string dateText(uint32_t packed) {
    if (packed == 0) return "?";
    const auto year = packed >> 9, month = packed >> 5 & 15, day = packed & 31;
    if (month == 0) return fmt::format("{}", year);
    if (day == 0) return fmt::format("{}-{:02}", year, month);
    return fmt::format("{}-{:02}-{:02}", year, month, day);
}

// A figure of the card: what it is, its value large, what it means.
void figure(const char* label, const std::string& value, const std::string& detail) {
    ImGui::TextDisabled("%s", label);
    ImGui::PushFont(ui::titleFont(), ImGui::GetFontSize() * 1.35f);
    ImGui::TextUnformatted(value.c_str());
    ImGui::PopFont();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", detail.c_str());
    ImGui::PopTextWrapPos();
}

}  // namespace

void App::startPrep(const std::string& opponent) {
    if (prep_) {
        if (prep_->cancel) *prep_->cancel = true;
        prep_.reset();  // its jobs end first
    }
    lichess_.prep_player = opponent;
    saveLichessSettings();
    Prep p;
    p.opponent = opponent;
    p.cancel   = std::make_shared<std::atomic<bool>>(false);
    p.progress = std::make_shared<lichess::SyncProgress>();
    // Their database: "Lichess · opponent", made when missing; a sync already writing in it brings the games itself.
    const auto name = "Lichess · " + opponent;
    auto& s         = shelf();
    for (std::size_t i = 0; i < s.entries().size(); ++i)
        if (!s.entries()[i].source && lower(s.entries()[i].name) == lower(name)) p.index = i;
    std::shared_ptr<study::Database> db;
    try {
        if (p.index == SIZE_MAX) p.index = s.create(name);
        if (!(lichess_sync_ && lichess_sync_->index == p.index)) db = s.open(p.index);
    } catch (const std::exception& e) {
        p.error = e.what();
    }
    // A year of their blitz, rapid and classical games (what Lichess's export is asked: their name and these filters).
    if (db) {
        lichess::SyncFilters filters;
        filters.perf_types = lichess::perfTypes(1);
        filters.since =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
                .count() -
            int64_t{365} * 24 * 3600 * 1000;
        const std::string token = lichess_account_ ? lichess_account_->token : std::string();
        p.sync =
            std::async(std::launch::async, [db, opponent, token, progress = p.progress, cancel = p.cancel, filters] {
                return lichess::syncGames(*db, opponent, token, *progress, *cancel, 500, filters);
            });
    }
    p.profile_job = std::async(std::launch::async, [opponent] { return lichess::publicProfile(opponent); });
    prep_         = std::move(p);
}

void App::pollPrep() {
    if (!prep_) return;
    auto& p          = *prep_;
    const auto ready = [](auto& job) {
        return job.valid() && job.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
    };
    if (ready(p.profile_job)) {
        auto answer = p.profile_job.get();
        if (answer.value)
            p.profile = std::move(*answer.value);
        else
            p.profile_error = answer.error;
    }
    if (ready(p.sync)) {
        const auto report = p.sync.get();
        if (!report.error.empty() && !report.cancelled) p.error = report.error;
        if (report.added > 0 && p.index < shelf().entries().size()) shelf().rebuildTree(p.index);
    }
    if (ready(p.report_job)) p.report = p.report_job.get();
    // The report once the database's tree is there, again when the tree or the games changed.
    if (p.sync.valid() || p.report_job.valid() || p.index >= shelf().entries().size()) return;
    const auto source = shelf().treeSource(p.index);
    if (!source || !source->tree || !source->snapshot || source->tree->stale(*source->snapshot)) return;
    const auto key = source->snapshot->generation() * 1000003 + source->tree->games();
    if (key == p.reported) return;
    p.reported = key;
    std::vector<std::pair<std::string, study::Repertoire>> repertoires;
    auto& shelf_of = this->repertoires();  // read from the profile the first time
    for (std::size_t i = 0; i < shelf_of.entries().size(); ++i)
        repertoires.emplace_back(shelf_of.entries()[i].name, shelf_of.map(i));
    const std::string me = lichess_account_ ? lichess_account_->username : std::string();
    p.report_job = std::async(std::launch::async, [base     = study::TreeBase{source->snapshot, source->tree},
                                                   opponent = p.opponent, me, repertoires = std::move(repertoires)] {
        return prepReport(base, opponent, me, repertoires);
    });
}

void App::openPrepLine(const std::vector<std::string>& line, const std::string& move) {
    if (!prep_) return;
    page_ = Page::Board;
    readOnBoard(lineText(line, move), trf("Against {}", prep_->opponent));
    board_.ply = static_cast<int>(board_.game.moves.size());  // the position the line comes to
    // The Tree tab, on their games: its sources are the databases with a tree (together first when there are two or
    // more), then the books and Lichess.
    board_side_tab_ = 2;
    auto& s         = shelf();
    std::vector<std::size_t> bases;
    for (std::size_t i = 0; i < s.entries().size(); ++i) {
        if (s.entries()[i].source) continue;
        if (const auto source = s.treeSource(i); source && source->tree) bases.push_back(i);
    }
    const auto it = std::find(bases.begin(), bases.end(), prep_->index);
    if (it != bases.end()) tree_source_ = (bases.size() >= 2 ? 1 : 0) + static_cast<int>(it - bases.begin());
}

void App::drawPrepCard() {
    const ui::TopicRegion topic(guide::topics::Prep);
    ui::beginCard("lichess_prep");
    ImGui::PushTextWrapPos();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(TR("Prepare a game against"));
    ImGui::SameLine();
    if (!prep_prefilled_) {
        prep_prefilled_   = true;
        const auto length = std::min(lichess_.prep_player.size(), prep_name_.size() - 1);
        std::copy_n(lichess_.prep_player.data(), length, prep_name_.data());
    }
    if (prep_focus_) {
        ImGui::SetKeyboardFocusHere();
        prep_focus_ = false;
    }
    ImGui::SetNextItemWidth(px(200));
    const bool enter = ImGui::InputTextWithHint("##prep_name", TR("A Lichess name"), prep_name_.data(),
                                                prep_name_.size(), ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    const std::string typed = prep_name_.data();
    const bool busy         = prep_ && lower(prep_->opponent) == lower(typed) && prep_->sync.valid();
    ImGui::BeginDisabled(!lichessName(typed) || busy);
    if (ImGui::SmallButton(TR("Prepare")) || (enter && lichessName(typed) && !busy)) startPrep(typed);
    ImGui::EndDisabled();
    if (!prep_) {
        ImGui::TextDisabled("%s", TR("Their games of the last year come into a database of their own; then what they "
                                     "play, your games against them, and the lines they play that your repertoire "
                                     "does not answer."));
        ImGui::PopTextWrapPos();
        ui::endCard();
        return;
    }
    auto& p = *prep_;
    // Fair play: nothing of it while a game of the user's against them is played.
    if (std::any_of(lichess_playing_.begin(), lichess_playing_.end(),
                    [&](const auto& g) { return lower(g.opponent) == lower(p.opponent); })) {
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s",
                           trf("Locked while your game against {} is played on Lichess.", p.opponent).c_str());
        ImGui::PopTextWrapPos();
        ui::endCard();
        return;
    }
    const auto* r = p.report ? &*p.report : nullptr;
    if (ImGui::BeginTable("##prep_figures", 3, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextColumn();
        if (p.sync.valid()) {
            figure(TR("The opponent's games"), ui::exactCount(p.progress->games.load()), TR("coming from Lichess…"));
        } else if (r) {
            const auto years = r->first == 0                       ? std::string()
                               : (r->first >> 9) == (r->last >> 9) ? fmt::format("{}", r->first >> 9)
                                                                   : fmt::format("{}–{}", r->first >> 9, r->last >> 9);
            figure(TR("The opponent's games"), ui::exactCount(r->games),
                   r->games == 0 ? std::string(TR("none in a year of blitz, rapid and classical"))
                                 : trf("blitz, rapid and classical, {}, in their database", years));
        } else {
            figure(TR("The opponent's games"), "…", TR("their database's tree is being built…"));
        }
        ImGui::TableNextColumn();
        if (!lichess_account_) {
            figure(TR("Between you two"), "–", TR("your linked account tells them"));
        } else if (r) {
            const int n = r->won + r->drawn + r->lost;
            figure(TR("Between you two"), fmt::format("+{} ={} −{}", r->won, r->drawn, r->lost),
                   n == 0 ? std::string(TR("no game between you in their database"))
                          : trn("{} game, the last on {}", "{} games, the last on {}", static_cast<uint64_t>(n), n,
                                dateText(r->last_between)));
        } else {
            figure(TR("Between you two"), "…", "");
        }
        ImGui::TableNextColumn();
        if (p.profile) {
            // The speed they play most, out of the four over the board.
            const lichess::SpeedRating* most = nullptr;
            for (const auto& sp : p.profile->speeds)
                if (sp.key != "correspondence" && (!most || sp.games > most->games)) most = &sp;
            if (most && most->games > 0)
                figure(TR("Their level"), fmt::format("{}{}", most->rating, most->provisional ? "?" : ""),
                       trf("{} · {} games", most->key, ui::exactCount(static_cast<uint64_t>(most->games))));
            else
                figure(TR("Their level"), "–", TR("no rated game"));
        } else {
            figure(TR("Their level"), p.profile_error.empty() ? "…" : "–", p.profile_error);
        }
        ImGui::EndTable();
    }
    if (r && r->games > 0) {
        ImGui::Spacing();
        ImGui::TextUnformatted(TR("What they play"));
        if (!r->with_white.empty())
            ImGui::TextDisabled("%s", trf("With White: {}", movesText(r->with_white, false)).c_str());
        for (const auto& against : r->with_black)
            if (!against.replies.empty())
                ImGui::TextDisabled("%s",
                                    trf("Against 1.{}: {}", against.san, movesText(against.replies, true)).c_str());
        ImGui::Spacing();
        ImGui::TextUnformatted(TR("Your repertoire against them"));
        if (repertoires().entries().empty()) {
            ImGui::TextDisabled("%s", TR("No repertoire yet: the Tree tab's stars make one, measured on the Openings "
                                         "page."));
        } else if (r->gaps.empty()) {
            ImGui::TextDisabled("%s", TR("Your repertoires answer the lines they play often."));
        } else {
            ImGui::TextDisabled("%s", trn("{} line they play often that your repertoire does not answer, the most "
                                          "frequent first: a click shows it, with the tree of their games.",
                                          "{} lines they play often that your repertoire does not answer, the most "
                                          "frequent first: a click shows them, with the tree of their games.",
                                          r->gaps.size(), r->gaps.size())
                                          .c_str());
            for (std::size_t i = 0; i < r->gaps.size(); ++i) {
                const auto& g = r->gaps[i];
                if (ImGui::SmallButton(fmt::format("{}##prep_gap{}", lineText(g.gap.line, g.gap.move), i).c_str())) {
                    openPrepLine(g.gap.line, g.gap.move);
                    ImGui::PopTextWrapPos();
                    ui::endCard();
                    return;
                }
                ImGui::SameLine();
                ImGui::TextDisabled(
                    "%s", trn("{} · {} game", "{} · {} games", g.gap.games, g.repertoire, ui::exactCount(g.gap.games))
                              .c_str());
            }
        }
    }
    ImGui::Spacing();
    ImGui::TextDisabled("%s", TR("Asked of Lichess: their name with a year of blitz, rapid and classical games, and "
                                 "their public profile. Nothing of yours."));
    if (!p.error.empty())
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s", trf("Lichess: {}", p.error).c_str());
    ImGui::PopTextWrapPos();
    ui::endCard();
}

}  // namespace fcg
