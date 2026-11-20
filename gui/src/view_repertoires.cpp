// The Openings page of the Chess space (mockup M3, board 2): the repertoires, the active one's coverage of the
// databases' games and its gaps, the most often met first.

#include <algorithm>
#include <chrono>
#include <string>

#include <fmt/include/fmt/format.h>
#include <imgui.h>

#include "app.hpp"
#include "i18n.hpp"
#include "ui.hpp"

namespace fcg {

namespace {

float px(float v) { return v * ImGui::GetFontSize() / 16.0f; }

// "1.e4 c5 2.Nf3", the moves from the start.
std::string movetext(const std::vector<std::string>& line) {
    std::string out;
    for (std::size_t i = 0; i < line.size(); ++i)
        out += (i % 2 == 0 ? std::to_string(i / 2 + 1) + "." : std::string()) + line[i] + " ";
    if (!out.empty()) out.pop_back();
    return out;
}

}  // namespace

RepertoireShelf& App::repertoires() {
    if (!repertoires_) repertoires_ = std::make_unique<RepertoireShelf>(storage_.root());
    return *repertoires_;
}

std::optional<std::vector<chess::Move>> App::boardPath() const {
    const auto& b = board_;
    if (!b.game.start_fen.empty() && b.game.start_fen != chess::constants::STARTPOS) return std::nullopt;
    if (b.chess960) return std::nullopt;
    std::vector<chess::Move> path;
    for (int i = 0; i < b.ply && i < static_cast<int>(b.game.moves.size()); ++i)
        path.push_back(b.game.moves[static_cast<std::size_t>(i)].move);
    return path;
}

void App::showLineOnBoard(const std::vector<std::string>& line, const std::string& title) {
    readOnBoard("[Event \"" + title + "\"]\n\n" + movetext(line) + " *\n", title);
    board_.ply      = static_cast<int>(board_.game.moves.size());
    board_side_tab_ = 2;  // the tree beside it, its replies there
    page_           = Page::Board;
}

void App::drawOpeningsPage() {
    auto& r = repertoires();
    ui::pageTitle(TR("Openings"), guideSection());
    // At the right, as wide as its label in the language of the interface.
    const char* new_label = TR("New repertoire");
    const auto& style     = ImGui::GetStyle();
    const float new_w     = style.FramePadding.x * 2 + ImGui::GetFrameHeight() * 0.42f + style.ItemInnerSpacing.x +
                            ImGui::CalcTextSize(new_label).x;
    ImGui::SameLine(std::max(ImGui::GetCursorPosX() + px(20), ImGui::GetContentRegionMax().x - new_w));
    if (ui::iconButton("rep_new", ui::Icon::Plus, new_label)) {
        rep_naming_ = true;
        rep_name_.fill(0);
    }
    ImGui::TextDisabled("%s", TR("Your lines, each repertoire a PGN the Board page edits like a game. The star of the "
                                 "Board page's Tree tab adds a move to the active one."));
    if (rep_naming_) {
        ImGui::SetNextItemWidth(px(260));
        if (!ImGui::IsAnyItemActive()) ImGui::SetKeyboardFocusHere();
        const bool done = ImGui::InputTextWithHint("##rep_name", TR("Its name"), rep_name_.data(), rep_name_.size(),
                                                   ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(px(110));
        const char* sides[] = {TR("White"), TR("Black")};
        ImGui::Combo("##rep_side", &rep_side_, sides, 2);
        ImGui::SameLine();
        if (done || ImGui::Button(TR("Make"))) {
            r.create(rep_name_.data(), rep_side_ == 1 ? chess::Color::BLACK : chess::Color::WHITE);
            rep_naming_ = false;
            coverage_of_.clear();
        }
        ImGui::SameLine();
        if (ImGui::Button(TR("Cancel"))) rep_naming_ = false;
    }
    ImGui::Dummy(ImVec2(0, px(6)));
    if (r.entries().empty()) {
        ui::beginCard("rep_empty");
        ImGui::TextUnformatted(TR("No repertoire yet."));
        ImGui::TextDisabled("%s", TR("A star in the Board page's Tree tab starts one; or make one here."));
        ui::endCard();
        return;
    }

    // The repertoires, the active one chosen.
    const float list_w = std::min(px(300), ImGui::GetContentRegionAvail().x * 0.35f);
    ImGui::BeginChild("##rep_list", ImVec2(list_w, 0), ImGuiChildFlags_Borders);
    for (std::size_t i = 0; i < r.entries().size(); ++i) {
        const auto& e = r.entries()[i];
        ImGui::PushID(static_cast<int>(i));
        const auto start = ImGui::GetCursorPos();
        if (ImGui::Selectable("##rep", r.active() == static_cast<int>(i), 0,
                              ImVec2(0, ImGui::GetTextLineHeightWithSpacing() * 2.2f))) {
            r.setActive(static_cast<int>(i));
            coverage_of_.clear();
        }
        ImGui::SetCursorPos(ImVec2(start.x + px(6), start.y + px(3)));
        ImGui::TextUnformatted(e.name.c_str());
        // Its side at the right; under it, what its training has for today (#76) and its size.
        const char* side_name = e.side == chess::Color::WHITE ? TR("White") : TR("Black");
        ImGui::SameLine(std::max(ImGui::GetCursorPosX(),
                                 ImGui::GetContentRegionMax().x - ImGui::CalcTextSize(side_name).x - px(6)));
        ImGui::TextDisabled("%s", side_name);
        ImGui::SetCursorPosX(start.x + px(6));
        ImGui::TextDisabled("%s", trf("{} · {}", trainingListLine(i),
                                      trn("{} position", "{} positions", static_cast<int>(r.map(i).size()),
                                          ui::exactCount(r.map(i).size())))
                                      .c_str());
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::SameLine(0, px(14));

    ImGui::BeginChild("##rep_chosen", ImVec2(0, 0));
    const auto index = static_cast<std::size_t>(std::max(0, r.active()));
    const auto& e    = r.entries()[index];
    ui::pageTitle(e.name.c_str());
    if (ui::iconButton("rep_open", ui::Icon::Board, TR("Open its file on the board"))) openBoardPgn(e.file);
    ImGui::SameLine(0, px(12));
    ImGui::TextDisabled(
        "%s", trf("{} · active · {}", e.side == chess::Color::WHITE ? TR("White") : TR("Black"),
                  trn("{} position where it is your move", "{} positions where it is your move",
                      trainingSummary(index).counts.cards, ui::exactCount(trainingSummary(index).counts.cards)))
                  .c_str());
    // Its training (#76, mockup M11, board 1): what is due, the new positions, Start.
    ImGui::Dummy(ImVec2(0, px(4)));
    drawTrainingCard(index);
    ImGui::Dummy(ImVec2(0, px(8)));

    // Its coverage of the databases' games, measured in the background.
    auto& shelf_ = shelf();
    // Measured on (M3, board 2): the databases whose tree is built, each a chip that leaves it out or takes it back.
    std::vector<study::TreeBase> bases;
    std::vector<std::pair<std::string, std::filesystem::path>> chips;
    for (std::size_t i = 0; i < shelf_.entries().size(); ++i) {
        if (shelf_.entries()[i].source) continue;
        if (const auto source = shelf_.treeSource(i); source && source->tree) {
            chips.push_back({source->name, shelf_.entries()[i].folder});
            if (!coverage_left_.count(shelf_.entries()[i].folder)) bases.push_back({source->snapshot, source->tree});
        }
    }
    if (chips.empty()) {
        ImGui::TextDisabled("%s", TR("Its coverage is measured on the databases whose opening tree is built: import "
                                     "games on the Databases page."));
        ImGui::EndChild();
        return;
    }
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", TR("Measured on"));
    for (const auto& [name, folder] : chips) {
        ImGui::SameLine();
        const bool on = !coverage_left_.count(folder);
        if (on) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(ui::ACCENT));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
        }
        if (ImGui::SmallButton(fmt::format("{}##cover_{}", name, folder.string()).c_str())) {
            if (on)
                coverage_left_.insert(folder);
            else
                coverage_left_.erase(folder);
        }
        if (on) ImGui::PopStyleColor(2);
    }
    if (bases.empty()) {
        ImGui::TextDisabled("%s", TR("No database chosen: a click on one measures the coverage on it."));
        ImGui::EndChild();
        return;
    }
    const char* depths[] = {TR("End of my lines"), TR("Move 10"), TR("Move 15")};
    for (int i = 0; i < 3; ++i) {
        if (i > 0) ImGui::SameLine();
        if (ImGui::RadioButton(depths[i], coverage_depth_ == i)) coverage_depth_ = i;
    }
    std::string of = fmt::format("{}|{}|{}", e.file.string(), coverage_depth_, r.map(index).size());
    for (const auto& base : bases) of += fmt::format("|{}", base.snapshot->generation());
    for (const auto& chip : chips) of += coverage_left_.count(chip.second) ? "-" : "+";  // which ones, not how many
    if (of != coverage_of_ && (!coverage_job_ || coverage_job_->of != of)) {
        CoverageJob job;
        job.of = of;
        study::CoverageOptions options;
        options.until_ply = coverage_depth_ == 1 ? 20 : coverage_depth_ == 2 ? 30 : 0;
        job.done          = std::async(std::launch::async, [map = r.map(index), bases, options] {
            return study::coverage(map, study::treeMoves(bases), options);
        });
        coverage_job_     = std::move(job);
    }
    if (coverage_job_ && coverage_job_->done.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        coverage_    = coverage_job_->done.get();
        coverage_of_ = coverage_job_->of;
        coverage_job_.reset();
    }
    if (!coverage_ || coverage_of_ != of) {
        ImGui::TextDisabled("%s", TR("Measuring…"));
        ImGui::EndChild();
        return;
    }
    const auto& c = *coverage_;
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 2.2f);
    ImGui::Text("%d %%", static_cast<int>(std::lround(100 * c.covered)));
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::TextDisabled("%s", TR("of the games along the repertoire find an answer to the end of its lines"));
    ImGui::ProgressBar(static_cast<float>(c.covered), ImVec2(px(320), px(8)), "");
    ImGui::TextDisabled(
        "%s", trf("{} · {} in the databases · {}",
                  trn("{} position", "{} positions", static_cast<int>(c.positions), ui::exactCount(c.positions)),
                  ui::exactCount(c.known), trn("{} gap", "{} gaps", static_cast<int>(c.gaps.size()), c.gaps.size()))
                  .c_str());
    ImGui::Separator();
    ImGui::TextUnformatted(TR("Gaps, the most often met first"));
    ImGui::SameLine();
    ImGui::TextDisabled("%s", TR("a click shows the position, the tree beside it"));
    for (std::size_t i = 0; i < c.gaps.size(); ++i) {
        const auto& g = c.gaps[i];
        ImGui::PushID(static_cast<int>(i));
        const auto missing = g.move.empty() ? std::string(TR("no move of yours here")) : "→ " + g.move;
        const auto label   = fmt::format("{:5.1f} %   {} {}", 100 * g.reach, movetext(g.line), missing);
        if (ImGui::Selectable(label.c_str())) showLineOnBoard(g.line, e.name);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s",
                              trn("{} game", "{} games", static_cast<int>(std::min<uint64_t>(g.games, 1000000000)),
                                  ui::exactCount(g.games))
                                  .c_str());
        ImGui::PopID();
    }
    if (c.gaps.empty()) ImGui::TextDisabled("%s", TR("None: every move played often enough has its answer."));
    ImGui::EndChild();
}

}  // namespace fcg
