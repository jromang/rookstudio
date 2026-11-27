// "Check the endgames against the tables" (#94 with #42, mockup M13, board 7, point 20): a finished tournament's games
// that reached seven pieces or fewer set against the tablebases, asked from the tournament's menu (the right button on
// its header or on its row of the Tournaments page) or Ctrl+K. A few positions a game (endgame_check.hpp), the disk's
// tables first, the answers the profile kept, Lichess's with the user's consent and one request at a time, each answer
// kept for good. Its card under the tournament's header: nothing of the Tests view changes before the click.

#include <algorithm>
#include <chrono>
#include <string>
#include <vector>

#include <fmt/include/fmt/format.h>
#include <imgui.h>

#include <tour/pgn_file.hpp>
#include <tour/state.hpp>

#include "app.hpp"
#include "board_tables.hpp"
#include "endgame_check.hpp"
#include "endgames.hpp"
#include "guide.hpp"
#include "https.hpp"
#include "i18n.hpp"
#include "lichess_api.hpp"
#include "online_cache.hpp"
#include "pgn_store.hpp"
#include "tablebases.hpp"
#include "ui.hpp"

namespace fcg {

namespace fs = std::filesystem;

namespace {

float px(float v) { return v * ImGui::GetFontSize() / 16.0f; }

// A table cell's text: wider than the cell, it ends in "…", whole under the mouse.
void cellText(const std::string& text, bool muted = false) {
    const float room  = ImGui::GetContentRegionAvail().x;
    std::string shown = text;
    if (ImGui::CalcTextSize(text.c_str()).x > room) {
        while (!shown.empty() && ImGui::CalcTextSize((shown + "…").c_str()).x > room) {
            while (!shown.empty() && (static_cast<unsigned char>(shown.back()) & 0xC0) == 0x80) shown.pop_back();
            if (!shown.empty()) shown.pop_back();
        }
        shown += "…";
    }
    if (muted)
        ImGui::TextDisabled("%s", shown.c_str());
    else
        ImGui::TextUnformatted(shown.c_str());
    if (shown != text && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenOverlappedByItem))
        ImGui::SetTooltip("%s", text.c_str());
}

// A tournament's games as the check takes them, those that count, in their numbers' order, with their checks.
EndgameCheckGames readCheckedGames(const fs::path& dir, const std::atomic<bool>& cancel) {
    EndgameCheckGames out;
    try {
        const auto state = tour::TournamentState::open(dir, true);
        const auto& spec = state->spec();
        const bool frc   = spec.conditions.variant == fastchess::VariantType::FRC;
        auto results     = state->results();
        std::sort(results.begin(), results.end(),
                  [](const tour::GameResult& a, const tour::GameResult& b) { return a.number() < b.number(); });
        const auto name = [&](int id) {
            const auto* p = spec.participant(id);
            return p ? p->engine.name : std::string("?");
        };
        for (const auto& r : results) {
            if (cancel) return out;
            if (r.voided) continue;
            auto text = tour::readSpan(state->pgnPath(), r.pgn);
            if (!text) continue;
            if (r.set) *text = tour::countedPgn(std::move(*text), r);
            const auto replay = parseReplay(*text);
            if (!replay) continue;
            CheckedGame g;
            g.number    = r.number() + 1;
            g.white     = name(r.white);
            g.black     = name(r.black);
            g.start_fen = replay->start_fen;
            g.chess960  = frc;
            for (const auto& m : replay->moves) g.moves.push_back(m.move);
            g.result = r.result == "1-0" ? 1 : r.result == "0-1" ? -1 : 0;
            g.end    = gameEndOf(static_cast<int>(r.termination), r.reason, r.set.has_value());
            g.reason = r.reason;
            out.games.push_back(std::move(g));
        }
        // Each check points to its game: made once the games are all there, the vector moved whole afterwards.
        for (const auto& g : out.games) out.checks.push_back(std::make_unique<EndgameGameCheck>(g));
    } catch (const std::exception& e) {
        out.error = e.what();
    }
    return out;
}

int sideSign(const std::string& fen) { return fen.find(" b ") == std::string::npos ? 1 : -1; }

}  // namespace

void App::drawTournamentMenu(const fs::path& dir) {
    if (ImGui::MenuItem(TR("Check the endgames against the tables"))) startEndgameCheck(dir);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", TR("The games that reached seven pieces or fewer, each result set against what the "
                                   "tablebases say: the wins let slip, the lost positions held, the adjudications "
                                   "they contradict."));
}

void App::startEndgameCheck(const fs::path& dir) {
    // Shown on the tournament's page, under its header.
    const auto* t = run_->tournament();
    if (!t || fs::absolute(t->dir()).lexically_normal() != fs::absolute(dir).lexically_normal()) {
        openTournament(dir);
        if (!start_error_.empty()) return;
    }
    page_ = Page::Tournament;
    closeEndgameCheck();
    auto c            = std::make_unique<EndgameCheckRun>();
    c->dir            = dir;
    const auto* shown = run_->tournament();
    c->name           = shown ? shown->spec().name : dir.filename().string();
    c->reading = std::async(std::launch::async, [dir, cancel = c->cancel] { return readCheckedGames(dir, *cancel); });
    endgame_check_ = std::move(c);
}

void App::closeEndgameCheck() {
    if (!endgame_check_) return;
    auto& c   = *endgame_check_;
    *c.cancel = true;
    // What runs still ends on its own: parked until then, the window not waiting for it.
    jobs_.park(std::move(c.asking));
    jobs_.park(std::move(c.reading));
    endgame_check_.reset();
}

void App::pollEndgameCheck() {
    if (!endgame_check_) return;
    auto& c = *endgame_check_;
    if (c.reading.valid()) {
        if (!isReady(c.reading)) return;
        auto read = c.reading.get();
        c.error   = std::move(read.error);
        c.games   = std::move(read.games);
        c.checks  = std::move(read.checks);
    }
    if (c.summary || !c.error.empty()) return;

    // Lichess's answer for the position a check waits for.
    if (isReady(c.asking)) {
        auto a          = c.asking.get();
        auto& check     = *c.checks[c.asking_game];
        const auto& fen = check.fen(c.asking_ply);
        if (!a.value && !a.error.empty()) {
            c.error = a.error;  // "Try again" asks it again
            return;
        }
        ++c.from_lichess;
        TablesSays says;
        if (a.value && a.value->outcome != lichess::Outcome::Unknown)
            if (const auto r = outcomeResult(a.value->outcome)) says = *r * sideSign(fen);
        c.told[fen] = says;
        check.give(c.asking_ply, says);
    }
    if (c.asking.valid() || c.consent) return;

    // The disk's tables and the answers the profile kept at once, a few milliseconds a frame; Lichess's one at a time.
    const int disk    = diskTablesPieces();
    const bool locked = diskTablesLocked();
    const auto paths  = endgameTablesPaths();
    const auto until  = std::chrono::steady_clock::now() + std::chrono::milliseconds(4);
    while (c.current < c.checks.size() && std::chrono::steady_clock::now() < until) {
        auto& check    = *c.checks[c.current];
        const auto ply = check.next();
        if (!ply) {
            ++c.current;
            continue;
        }
        const auto& fen  = check.fen(*ply);
        const int pieces = check.pieces(*ply);
        if (const auto it = c.told.find(fen); it != c.told.end()) {  // another game's position, told already
            check.give(*ply, it->second);
            continue;
        }
        auto source = checkSource(pieces, disk, locked, false, lichess_.tablebase, c.allowed);
        if (source == CheckSource::Disk) {
            const chess::Board board(fen, c.games[c.current].chess960);
            std::optional<TableVerdict> v;
            {
                const auto lock = lockSyzygy();
                v               = probeTables(board, paths, false);
            }
            if (v) {
                ++c.from_disk;
                const int says = v->rule50 ? 0 : v->result * sideSign(fen);
                c.told[fen]    = says;
                check.give(*ply, says);
                continue;
            }
        }
        // Not on the disk (or a table missing there): the profile's answer, else Lichess's.
        const auto kept  = cachedTablebase(storage_.root(), fen);
        const bool known = kept && kept->outcome != lichess::Outcome::Unknown;
        source           = checkSource(pieces, 0, true, known, lichess_.tablebase, c.allowed);
        switch (source) {
            case CheckSource::Kept: {
                ++c.from_profile;
                const auto r    = outcomeResult(kept->outcome);
                const auto says = r ? TablesSays(*r * sideSign(fen)) : std::nullopt;
                c.told[fen]     = says;
                check.give(*ply, says);
                continue;
            }
            case CheckSource::Lichess:
                c.asking_game = c.current;
                c.asking_ply  = *ply;
                c.asking      = std::async(std::launch::async, [root = storage_.root(), fen, cancel = c.cancel] {
                    bool asked = false;
                    return keptOrAskedTables(root, fen, asked, cancel.get());
                });
                return;
            case CheckSource::Consent:
                if (!c.not_now) {
                    c.consent = true;  // the card asks
                    return;
                }
                check.give(*ply, std::nullopt);
                continue;
            case CheckSource::Disk:
            case CheckSource::None:
                check.give(*ply, std::nullopt);
                continue;
        }
    }
    if (c.current < c.checks.size()) return;
    EndgameCheckSummary s;
    s.games = c.games.size();
    for (std::size_t i = 0; i < c.checks.size(); ++i) {
        const auto& check = *c.checks[i];
        if (!check.reached()) continue;
        ++s.reached;
        s.asked += check.asked();
        if (!check.judged()) continue;
        ++s.judged;
        for (auto& f : check.findings(i)) s.findings.push_back(std::move(f));
    }
    c.summary = std::move(s);
}

void App::drawEndgameCheck() {
    if (!endgame_check_) return;
    auto& c = *endgame_check_;
    const ui::TopicRegion topic(guide::topics::EndgameCheck);
    ui::beginCard("endgame_check");
    ImGui::PushTextWrapPos();
    ImGui::PushFont(ui::titleFont(), ImGui::GetFontSize() * 1.1f);
    ImGui::TextUnformatted(TR("The endgames against the tables"));
    ImGui::PopFont();
    const auto sources = [&] {
        std::vector<std::string> parts;
        if (c.from_disk > 0)
            parts.push_back(trn("{} position from your tables on the disk", "{} positions from your tables on the disk",
                                c.from_disk, c.from_disk));
        if (c.from_profile > 0)
            parts.push_back(trn("{} answer kept in your profile", "{} answers kept in your profile", c.from_profile,
                                c.from_profile));
        if (c.from_lichess > 0)
            parts.push_back(trn("{} position looked up in Lichess's tables",
                                "{} positions looked up in Lichess's tables", c.from_lichess, c.from_lichess));
        std::string out;
        for (const auto& p : parts) out += (out.empty() ? "" : " · ") + p;
        return out;
    };
    bool close = false;
    if (c.reading.valid()) {
        ImGui::TextDisabled("%s", TR("Reading the tournament's games…"));
    } else if (!c.error.empty() && c.games.empty()) {
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s",
                           trf("The games cannot be read: {}", c.error).c_str());
        if (ImGui::SmallButton(TR("Close"))) close = true;
    } else if (!c.summary) {
        std::size_t reached = 0, done = 0;
        for (const auto& check : c.checks)
            if (check->reached()) {
                ++reached;
                if (check->done()) ++done;
            }
        ImGui::TextUnformatted(
            trf("Checking the games that reached seven pieces or fewer: {} of {}…", done, reached).c_str());
        if (const auto s = sources(); !s.empty()) ImGui::TextDisabled("%s", s.c_str());
        if (c.consent) {
            // Lichess's tables for what the disk's do not cover (mockup M4): what would go, three answers.
            const int disk = diskTablesPieces();
            ImGui::TextUnformatted(disk > 0
                                       ? trf("Your tables on the disk go up to {} pieces; Lichess's 7-piece tables "
                                             "would tell the rest: a few positions a game, one request at a time, "
                                             "each answer kept in your profile.",
                                             disk)
                                             .c_str()
                                       : TR("No tables on your disk; Lichess's 7-piece tables would tell the "
                                            "positions: a few a game, one request at a time, each answer kept in "
                                            "your profile."));
            ImGui::TextDisabled("%s", TR("To ask, each position's FEN goes to tablebase.lichess.ovh, without an "
                                         "account. Nothing else."));
            if (ImGui::SmallButton(TR("For this check"))) {
                c.allowed = true;
                c.consent = false;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton(TR("Always"))) {
                lichess_.tablebase = 1;
                saveLichessSettings();
                c.consent = false;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton(TR("Not now"))) {
                c.not_now = true;
                c.consent = false;
            }
        } else if (!c.error.empty()) {
            const auto pause = HostGate::instance().pausedFor("tablebase.lichess.ovh");
            if (pause.count() > 0) {
                const auto s = std::chrono::duration_cast<std::chrono::seconds>(pause).count();
                ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s",
                                   trf("Too many requests in a short time: Lichess asked for a pause. Trying again in "
                                       "{}:{:02}.",
                                       s / 60, s % 60)
                                       .c_str());
            } else {
                ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s",
                                   trf("Lichess's tables gave nothing: {}", c.error).c_str());
            }
            if (ImGui::SmallButton(TR("Try again"))) c.error.clear();
            ImGui::SameLine();
        } else if (c.asking.valid()) {
            ImGui::TextDisabled("%s", TR("Asking Lichess's tables…"));
        }
        if (ImGui::SmallButton(TR("Stop"))) close = true;
    } else {
        const auto& s = *c.summary;
        ImGui::TextUnformatted(endgameCheckSentence(s, c.games).c_str());
        if (const auto text = sources(); !text.empty()) ImGui::TextDisabled("%s", text.c_str());
        if (s.judged < s.reached && (lichess_.tablebase == 2 || c.not_now))
            ImGui::TextDisabled("%s", TR("Lichess's tables would tell the positions your disk's do not cover: the "
                                         "Lichess page, under Online for the analysis, lets you allow them."));
        if (!s.findings.empty()) {
            // A row each, the game opened at the position it is about.
            // As tall as its rows, up to eight; more scroll.
            constexpr std::size_t SHOWN = 8;
            const bool scroll           = s.findings.size() > SHOWN;
            const auto flags            = ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg |
                                          ImGuiTableFlags_SizingStretchProp | (scroll ? ImGuiTableFlags_ScrollY : 0);
            const float row             = ImGui::GetFrameHeight() + ImGui::GetStyle().CellPadding.y * 2;
            const float height          = scroll ? row * static_cast<float>(SHOWN) + ImGui::GetTextLineHeight() +
                                                       ImGui::GetStyle().CellPadding.y * 2
                                                 : 0.0f;
            if (ImGui::BeginTable("##endgame_findings", 6, flags, ImVec2(0, height))) {
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn(TR("Game"), ImGuiTableColumnFlags_WidthFixed, px(56));
                ImGui::TableSetupColumn(TR("White"), ImGuiTableColumnFlags_WidthStretch, 1.0f);
                ImGui::TableSetupColumn(TR("Black"), ImGuiTableColumnFlags_WidthStretch, 1.0f);
                ImGui::TableSetupColumn(TR("Result"), ImGuiTableColumnFlags_WidthFixed, px(64));
                ImGui::TableSetupColumn(TR("What the tables say"), ImGuiTableColumnFlags_WidthStretch, 4.0f);
                ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed,
                                        ImGui::CalcTextSize(TR("Open")).x + ImGui::GetStyle().FramePadding.x * 2);
                ImGui::TableHeadersRow();
                for (std::size_t i = 0; i < s.findings.size(); ++i) {
                    const auto& f = s.findings[i];
                    const auto& g = c.games[f.game];
                    ImGui::PushID(static_cast<int>(i));
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextUnformatted(fmt::format("{}", g.number).c_str());
                    ImGui::TableNextColumn();
                    cellText(g.white);
                    ImGui::TableNextColumn();
                    cellText(g.black);
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(g.result > 0 ? "1-0" : g.result < 0 ? "0-1" : "½-½");
                    ImGui::TableNextColumn();
                    cellText(endgameFindingWords(f, g));
                    ImGui::TableNextColumn();
                    if (ImGui::SmallButton(TR("Open"))) {
                        // In the Games tab, at the move it is about.
                        openTournament(c.dir);
                        page_            = Page::Tournament;
                        tab_request_     = Tab::Games;
                        select_game_id_  = g.number;
                        select_game_ply_ = static_cast<int>(f.ply);
                    }
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("%s", TR("The game in the Games tab, at the position it is about."));
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
        }
    }
    if (c.summary && ImGui::SmallButton(TR("Close"))) close = true;
    ImGui::PopTextWrapPos();
    ui::endCard();
    if (close) closeEndgameCheck();
}

}  // namespace fcg
