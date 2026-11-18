// The Chess space's column beside the board (mockups M2 and M3): its tabs, the opening tree of the databases and the
// Polyglot books of the library, and the reference games of the position.

#include <algorithm>
#include <cmath>
#include <string>

#include <fmt/include/fmt/format.h>
#include <imgui.h>

#include <commentary/position.hpp>
#include <study/scan_board.hpp>

#include "app.hpp"
#include "i18n.hpp"
#include "ui.hpp"

namespace fcg {

namespace {

float px(float v) { return v * ImGui::GetFontSize() / 16.0f; }

// FIDE's rating difference for a score, from 0.50 to 1.00 by hundredths (FIDE handbook B.02, table 8.1a); a score
// under one half is the difference of its complement, negated.
int ratingDifference(double score) {
    static constexpr int DP[51] = {0,   7,   14,  21,  29,  36,  43,  50,  57,  65,  72,  80,  87,  95,  102, 110, 117,
                                   125, 133, 141, 149, 158, 166, 175, 184, 193, 202, 211, 220, 230, 240, 251, 262, 273,
                                   284, 296, 309, 322, 336, 351, 366, 383, 401, 422, 444, 470, 501, 538, 589, 677, 800};
    const bool below            = score < 0.5;
    const int at                = static_cast<int>(std::lround(std::abs((below ? 1 - score : score) - 0.5) * 100));
    const int dp                = DP[std::clamp(at, 0, 50)];
    return below ? -dp : dp;
}

// The legal move of `board` that a code of the same position names.
std::optional<chess::Move> moveOfCode(const chess::Board& board, const study::ScanBoard& scan, int code) {
    const auto wanted = scan.decode(code);
    chess::Movelist list;
    chess::movegen::legalmoves(list, board);
    for (const auto& m : list) {
        const int promotion = m.typeOf() == chess::Move::PROMOTION ? static_cast<int>(m.promotionType())
                                                                   : static_cast<int>(study::NO_KIND);
        const bool castle   = m.typeOf() == chess::Move::CASTLING;
        if (m.from().index() == wanted.from && m.to().index() == wanted.to && promotion == wanted.promotion &&
            castle == ((wanted.flags & (study::MOVE_SHORT_CASTLE | study::MOVE_LONG_CASTLE)) != 0))
            return m;
    }
    return std::nullopt;
}

}  // namespace

void App::drawChessColumn() {
    // The tabs, in the order of the mockup; Alt+1 to Alt+4.
    const char* names[] = {TR("Notation"), TR("Analysis"), TR("Tree"), TR("References")};
    for (int i = 0; i < 4; ++i)
        if (ImGui::IsKeyChordPressed(ImGuiMod_Alt | static_cast<ImGuiKey>(ImGuiKey_1 + i))) board_side_tab_ = i;
    if (ImGui::BeginTabBar("##chess_column")) {
        for (int i = 0; i < 4; ++i) {
            const auto label = fmt::format("{}###chess_tab{}", names[i], i);
            if (ImGui::BeginTabItem(label.c_str(), nullptr,
                                    board_side_tab_ == i ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None)) {
                ImGui::EndTabItem();
            }
            if (ImGui::IsItemClicked()) board_side_tab_ = i;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Alt+%d", i + 1);
        }
        ImGui::EndTabBar();
    }
    switch (board_side_tab_) {
        case 0:
            drawBoardMoves(std::max(px(160), ImGui::GetContentRegionAvail().y));
            break;
        case 1: {
            drawBoardAnalysis();
            const float graph = boardGraphHeight();
            drawBoardGraph(std::max(graph, ImGui::GetContentRegionAvail().y));
            break;
        }
        case 2:
            drawBoardTree();
            break;
        default:
            drawBoardReferences();
            break;
    }
}

void App::drawBoardTree() {
    auto& b = board_;
    auto& s = shelf();
    // The sources: the databases whose tree covers their games, then the library's Polyglot books.
    std::vector<std::pair<std::string, std::size_t>> databases;
    for (std::size_t i = 0; i < s.entries().size(); ++i) {
        if (s.entries()[i].source) continue;
        if (const auto source = s.treeSource(i); source && source->tree) databases.push_back({source->name, i});
    }
    std::vector<const BookEntry*> books;
    for (const auto& book : books_) {
        auto ext = std::filesystem::path(book.path).extension().string();
        for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (ext == ".bin") books.push_back(&book);
    }
    const int count = static_cast<int>(databases.size() + books.size());
    if (count == 0) {
        ImGui::PushTextWrapPos();
        ImGui::TextDisabled("%s", TR("No database with its opening tree, nor Polyglot book: import games on the "
                                     "Databases page (the tree is built after the import), or add a book on the Books "
                                     "page of the Engines space."));
        ImGui::PopTextWrapPos();
        return;
    }
    tree_source_ = std::clamp(tree_source_, 0, count - 1);
    for (int i = 0; i < count; ++i) {
        const bool book  = i >= static_cast<int>(databases.size());
        const auto label = book ? books[static_cast<std::size_t>(i) - databases.size()]->name
                                : databases[static_cast<std::size_t>(i)].first;
        if (i > 0) ImGui::SameLine();
        const bool on = tree_source_ == i;
        if (on) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(ui::ACCENT));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
        }
        if (ImGui::Button(fmt::format("{}##tree_source{}", label, i).c_str())) tree_source_ = i;
        if (on) ImGui::PopStyleColor(2);
    }

    // The rows of the position shown, made again when it or the source changes.
    const auto fen  = boardFen();
    const bool book = tree_source_ >= static_cast<int>(databases.size());
    const auto of   = fmt::format(
        "{}|{}|{}", fen, tree_source_,
        book ? std::string() : std::to_string(s.facts(databases[static_cast<std::size_t>(tree_source_)].second).games));
    if (of != tree_rows_of_) {
        tree_rows_of_ = of;
        tree_rows_.clear();
        tree_summary_.clear();
        tree_book_       = book;
        const auto board = boardAt(b.game, b.chess960, b.ply);
        const auto mover = board.sideToMove();
        study::ScanBoard scan;
        if (!book && scan.setFen(fen, b.chess960)) {
            const auto source = s.treeSource(databases[static_cast<std::size_t>(tree_source_)].second);
            const auto answer = source && source->tree ? source->tree->at(*source->snapshot, scan.key()) : std::nullopt;
            if (answer) {
                const auto& t      = answer->total;
                const auto decided = t.white + t.draws + t.black;
                tree_summary_ = trn("{} game", "{} games", static_cast<int>(std::min<uint64_t>(t.games, 1000000000)),
                                    ui::exactCount(t.games));
                if (decided > 0)
                    tree_summary_ +=
                        trf(" · White {} % · draws {} % · Black {} %", std::lround(100.0 * t.white / decided),
                            std::lround(100.0 * t.draws / decided), std::lround(100.0 * t.black / decided));
                for (const auto& m : answer->moves) {
                    if (m.code == study::TreeMove::END) continue;
                    const auto move = moveOfCode(board, scan, m.code);
                    if (!move) continue;
                    TreeRow row;
                    row.move              = *move;
                    row.san               = chess::uci::moveToSan(board, *move);
                    row.games             = m.stats.games;
                    const auto& st        = m.stats;
                    const auto done       = st.white + st.draws + st.black;
                    const bool white_side = mover == chess::Color::WHITE;
                    if (done > 0) {
                        row.white = static_cast<double>(st.white) / done;
                        row.draws = static_cast<double>(st.draws) / done;
                        row.black = static_cast<double>(st.black) / done;
                        const double score =
                            (static_cast<double>(white_side ? st.white : st.black) + st.draws / 2.0) / done;
                        row.score            = static_cast<int>(std::lround(100 * score));
                        const auto own_rated = white_side ? st.white_rated : st.black_rated;
                        const auto own_sum   = white_side ? st.white_elo : st.black_elo;
                        const auto opp_rated = white_side ? st.black_rated : st.white_rated;
                        const auto opp_sum   = white_side ? st.black_elo : st.white_elo;
                        if (own_rated) row.elo = static_cast<int>(own_sum / own_rated);
                        if (opp_rated) row.perf = static_cast<int>(opp_sum / opp_rated) + ratingDifference(score);
                    }
                    row.last = static_cast<int>(st.last >> 9);
                    tree_rows_.push_back(std::move(row));
                }
            } else {
                tree_summary_ = TR("No two games of this database reach this position.");
            }
        } else if (book) {
            const auto* entry = books[static_cast<std::size_t>(tree_source_) - databases.size()];
            auto& opened      = tree_books_[entry->path];
            try {
                if (!opened) opened = std::make_shared<tour::PolyglotBook>(std::filesystem::path(entry->path));
                auto copy          = board;
                const auto choices = opened->moves(copy);
                uint64_t total     = 0;
                for (const auto& c : choices) total += c.weight;
                for (const auto& c : choices) {
                    TreeRow row;
                    row.move  = c.move;
                    row.san   = chess::uci::moveToSan(board, c.move);
                    row.games = total ? static_cast<uint64_t>(std::lround(1000.0 * c.weight / total)) : 0;
                    tree_rows_.push_back(std::move(row));
                }
                tree_summary_ = choices.empty() ? std::string(TR("The book has no move here."))
                                                : trn("{} move in the book", "{} moves in the book",
                                                      static_cast<int>(choices.size()), choices.size());
            } catch (const std::exception& e) {
                tree_summary_ = e.what();
            }
        }
    }
    ImGui::TextUnformatted(tree_summary_.c_str());

    // The star of the active repertoire: a move of it, added or taken out by a click (any side to move).
    const auto path  = boardPath();
    auto& reps       = repertoires();
    const int active = reps.active();
    const auto mover = boardAt(b.game, b.chess960, b.ply).sideToMove();
    const auto now   = ImGui::GetTime();
    std::optional<chess::Move> star;

    // The moves: a click plays one.
    std::optional<chess::Move> play;
    constexpr auto flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit;
    const int columns    = tree_book_ ? 3 : 9;
    const float toast_h  = star_undo_ && now - star_undo_->at < 8 ? ImGui::GetFrameHeightWithSpacing() : 0.0f;
    if (!tree_rows_.empty() && ImGui::BeginTable("##tree_rows", columns, flags, ImVec2(0, -toast_h))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight());
        ImGui::TableSetupColumn(TR("Move"));
        if (tree_book_) {
            ImGui::TableSetupColumn(TR("Weight"), ImGuiTableColumnFlags_WidthStretch);
        } else {
            ImGui::TableSetupColumn(TR("Games"));
            ImGui::TableSetupColumn(TR("White · draws · Black"), ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn(TR("Score"));
            ImGui::TableSetupColumn(TR("Avg. Elo"));
            ImGui::TableSetupColumn(TR("Perf."));
            ImGui::TableSetupColumn(TR("Last"));
            ImGui::TableSetupColumn("");
        }
        ImGui::TableHeadersRow();
        for (std::size_t r = 0; r < tree_rows_.size(); ++r) {
            const auto& row = tree_rows_[r];
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::PushID(static_cast<int>(r));
            if (path) {
                const bool in = active >= 0 && reps.has(static_cast<std::size_t>(active), *path, row.move);
                const auto at = ImGui::GetCursorScreenPos();
                const float h = ImGui::GetTextLineHeight();
                if (ImGui::InvisibleButton("##repertoire_star", ImVec2(h, h))) star = row.move;
                ui::drawIcon(ImGui::GetWindowDrawList(), ui::Icon::Star, ImVec2(at.x + h * 0.5f, at.y + h * 0.5f), h,
                             in ? ui::WARN : (ImGui::IsItemHovered() ? ImGui::GetColorU32(ImGuiCol_Text) : ui::MUTED));
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", in ? TR("In the active repertoire: a click takes it out")
                                               : TR("A click adds it to the active repertoire"));
            }
            ImGui::TableNextColumn();
            if (ImGui::Selectable(row.san.c_str(), false)) play = row.move;
            ImGui::PopID();
            ImGui::TableNextColumn();
            if (tree_book_) {
                ImGui::ProgressBar(static_cast<float>(row.games) / 1000.0f, ImVec2(-1, 0),
                                   fmt::format("{} %", std::lround(row.games / 10.0)).c_str());
                continue;
            }
            ImGui::TextUnformatted(ui::exactCount(row.games).c_str());
            ImGui::TableNextColumn();
            // White's share light, the draws grey, Black's dark: told apart by their lightness.
            {
                const auto pos = ImGui::GetCursorScreenPos();
                const float w  = std::max(px(60), ImGui::GetContentRegionAvail().x);
                const float h  = ImGui::GetTextLineHeight() * 0.8f;
                auto* dl       = ImGui::GetWindowDrawList();
                const float y  = pos.y + ImGui::GetTextLineHeight() * 0.1f;
                const float x1 = pos.x + w * static_cast<float>(row.white);
                const float x2 = x1 + w * static_cast<float>(row.draws);
                dl->AddRectFilled(ImVec2(pos.x, y), ImVec2(x1, y + h), IM_COL32(246, 247, 249, 255));
                dl->AddRectFilled(ImVec2(x1, y), ImVec2(x2, y + h), IM_COL32(154, 163, 176, 255));
                dl->AddRectFilled(ImVec2(x2, y), ImVec2(pos.x + w, y + h), IM_COL32(43, 48, 56, 255));
                dl->AddRect(ImVec2(pos.x, y), ImVec2(pos.x + w, y + h), IM_COL32(201, 206, 214, 255));
                ImGui::Dummy(ImVec2(w, ImGui::GetTextLineHeight()));
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", trf("White {} %, draws {} %, Black {} %", std::lround(100 * row.white),
                                                std::lround(100 * row.draws), std::lround(100 * row.black))
                                                .c_str());
            }
            ImGui::TableNextColumn();
            if (row.score >= 0) ImGui::Text("%d %%", row.score);
            ImGui::TableNextColumn();
            if (row.elo > 0) ImGui::Text("%d", row.elo);
            ImGui::TableNextColumn();
            if (row.perf > 0) ImGui::Text("%d", row.perf);
            ImGui::TableNextColumn();
            if (row.last > 0) ImGui::TextDisabled("%d", row.last);
            ImGui::TableNextColumn();
        }
        ImGui::EndTable();
    }
    if (star && path) {
        const auto index = active >= 0 ? static_cast<std::size_t>(active) : reps.activeOrNew(mover);
        auto before      = reps.entries()[index].lines;
        const auto board = boardAt(b.game, b.chess960, b.ply);
        const auto san   = chess::uci::moveToSan(board, *star);
        const bool added = reps.toggle(index, *path, *star);
        star_undo_       = StarUndo{
            index, std::move(before),
            trf(added ? TR("{} added to « {} »") : TR("{} taken out of « {} »"), san, reps.entries()[index].name), now};
    }
    if (star_undo_ && now - star_undo_->at < 8) {
        ImGui::TextUnformatted(star_undo_->said.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton(TR("Undo"))) {
            repertoires().restore(star_undo_->index, std::move(star_undo_->lines));
            star_undo_.reset();
        }
    }
    if (play) boardPlay(*play);
}

void App::drawBoardReferences() {
    auto& s = shelf();
    // The databases, their trees when they cover them: the strongest games through the position.
    std::vector<DatabaseShelf::TreeSource> sources;
    for (std::size_t i = 0; i < s.entries().size(); ++i)
        if (!s.entries()[i].source)
            if (auto source = s.treeSource(i)) sources.push_back(std::move(*source));
    if (sources.empty()) {
        ImGui::TextDisabled("%s", TR("No database yet: the reference games come from the Databases page's."));
        return;
    }
    const auto fen = boardFen();
    std::string of = fen;
    for (const auto& src : sources) of += fmt::format("|{}", src.snapshot->generation());
    if (of != references_of_ && (!references_job_ || references_job_->of != of)) {
        ReferencesJob job;
        job.of      = of;
        job.sources = sources;
        std::vector<commentary::ReferenceBase> bases;
        for (const auto& src : sources) bases.push_back({src.name, src.snapshot, src.tree});
        job.done        = std::async(std::launch::async, [fen, bases = std::move(bases)] {
            const auto position = commentary::Position::fromFen(fen);
            return position ? commentary::references(*position, bases, 12) : commentary::References{};
        });
        references_job_ = std::move(job);
    }
    if (references_job_ && references_job_->done.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        references_         = references_job_->done.get();
        references_sources_ = std::move(references_job_->sources);
        references_of_      = references_job_->of;
        references_job_.reset();
    }
    if (references_job_ && references_of_ != of) {
        ImGui::TextDisabled("%s", TR("Looking…"));
        return;
    }
    const auto& t = references_.total;
    ImGui::TextUnformatted(trn("{} game reaches this position", "{} games reach this position",
                               static_cast<int>(std::min<uint64_t>(t.games, 1000000000)), ui::exactCount(t.games))
                               .c_str());
    ImGui::TextDisabled("%s",
                        TR("The strongest first (the players' mean rating), then the newest. A click opens one."));
    for (std::size_t i = 0; i < references_.games.size(); ++i) {
        const auto& g = references_.games[i];
        ImGui::PushID(static_cast<int>(i));
        const auto result = g.result == '1' ? "1-0" : g.result == '0' ? "0-1" : g.result == '=' ? "½-½" : "*";
        auto line         = fmt::format("{} – {}", g.white, g.black);
        if (g.white_elo || g.black_elo) line += fmt::format("  ({} · {})", g.white_elo, g.black_elo);
        if (ImGui::Selectable(fmt::format("{}##ref", line).c_str())) {
            if (g.base < references_sources_.size()) {
                const auto& snap = references_sources_[g.base].snapshot;
                readOnBoard(study::importedPgn(snap->game(g.id)), fmt::format("{} – {}", g.white, g.black));
            }
        }
        ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetContentRegionAvail().x * 0.55f));
        ImGui::TextDisabled("%s", fmt::format("{}{}{} · {}", g.event, g.year ? " " : "",
                                              g.year ? std::to_string(g.year) : std::string(), result)
                                      .c_str());
        ImGui::PopID();
    }
    if (!references_.listed)
        ImGui::TextDisabled("%s",
                            TR("Too many games reach it to look for the strongest: early in the opening, the Tree "
                               "tab tells more."));
}

}  // namespace fcg
