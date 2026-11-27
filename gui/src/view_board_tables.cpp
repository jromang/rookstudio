// The study Board's Endgames tab (#42, mockup M13, board 7, point 20): once seven pieces or fewer are left (eight in
// the positions of Lichess's op1 tables), the tables' verdict and where it comes from, the moves grouped by the outcome
// they leave, their distances in players' words, and each move that spoils the outcome with why; a move hovered shows
// its main line on the board, a click plays it. The disk's tables first, Lichess's with the user's consent (mockup M4),
// each answer kept in the profile. Then the assistant's sentence on the game's move from here, taught by the tables.

#include <algorithm>
#include <chrono>
#include <string>
#include <vector>

#include <fmt/include/fmt/format.h>
#include <imgui.h>

#include <commentary/tables_said.hpp>

#include "app.hpp"
#include "board_tables.hpp"
#include "board_text.hpp"
#include "endgames.hpp"
#include "i18n.hpp"
#include "lichess_api.hpp"
#include "online_cache.hpp"
#include "tablebases.hpp"
#include "ui.hpp"
#include "widgets.hpp"

namespace fcg {

namespace {

float px(float v) { return v * ImGui::GetFontSize() / 16.0f; }

using ui::numbered;

// The moves a group shows at most, and the spoiling moves whose main line is asked of Lichess by itself (the others
// when hovered).
constexpr std::size_t SHOWN_SPOILERS = 12;
constexpr std::size_t ASKED_SPOILERS = 8;

}  // namespace

bool App::boardTablesReach() const {
    return tablesReach(boardAt(board_.game, board_.chess960, board_.ply), board_.chess960) == TablesReach::Yes;
}

const std::vector<std::string>* App::boardTablesLine(const lichess::TablebaseMove& move, bool ask) {
    auto& t = board_tables_;
    if (!t.answer) return nullptr;
    chess::Board board;
    board.setFen(t.fen);
    const auto m = chess::uci::uciToMove(board, move.uci);
    if (m == chess::Move::NO_MOVE) return nullptr;
    board.makeMove(m);
    const auto fen = board.getFen();
    if (const auto it = t.lines.find(fen); it != t.lines.end()) return &it->second;
    if (t.lines_failed.count(fen) > 0) return nullptr;
    if (move.checkmate || move.stalemate) return &(t.lines[fen] = {});
    if (!t.online) {
        // The disk's: their best moves followed, at once.
        const auto paths = endgameTablesPaths();
        std::vector<std::string> line;
        {
            const auto lock = lockSyzygy();
            line            = tablesLine(board, [&](const chess::Board& b) { return probeDiskTables(b, paths); }, 40);
        }
        return &(t.lines[fen] = std::move(line));
    }
    // Lichess's: kept in the profile, else asked, one at a time.
    if (ask && !t.line_job.valid()) {
        t.line_job_fen = fen;
        t.line_job     = std::async(std::launch::async, [root = storage_.root(), fen, stop = jobs_.cancelFlag()] {
            bool asked = false;
            return keptOrAskedLine(root, fen, asked, stop.get());
        });
    }
    return nullptr;
}

void App::drawBoardEndgames() {
    auto& b          = board_;
    auto& t          = board_tables_;
    const auto board = boardAt(b.game, b.chess960, b.ply);
    const auto fen   = board.getFen();
    const double now = ImGui::GetTime();
    const int pieces = piecesOf(board);
    ImGui::PushTextWrapPos();
    switch (tablesReach(board, b.chess960)) {
        case TablesReach::TooMany:
            ImGui::TextUnformatted(trf("{} pieces: the tables speak of seven pieces or fewer.", pieces).c_str());
            ImGui::TextDisabled("%s",
                                TR("From seven pieces down, this tab gives the tables' verdict, each move by the "
                                   "outcome it leaves, how far each is from mate or from a capture, and the moves "
                                   "that spoil the result, with why. Eight pieces with a pawn of each side on one "
                                   "file are in Lichess's op1 tables too."));
            ImGui::PopTextWrapPos();
            return;
        case TablesReach::Castling:
            ImGui::TextUnformatted(
                TR("Castling is still possible here: no table holds a position with castling "
                   "rights."));
            ImGui::PopTextWrapPos();
            return;
        case TablesReach::Chess960:
            ImGui::TextUnformatted(TR("A Chess960 game: the tables are those of standard chess."));
            ImGui::PopTextWrapPos();
            return;
        case TablesReach::Yes:
            break;
    }
    // Another position: its own answer, the main lines of the last one kept in the profile.
    if (t.fen != fen) {
        t.fen   = fen;
        t.since = now;
        t.answer.reset();
        t.online = false;
        t.nothing.reset();
        t.lines.clear();
        t.lines_failed.clear();
        t.hovered.clear();
    }
    const bool settled = now - t.since > 0.4;  // stepping through a game asks nothing on the way
    if (isReady(t.asking)) {
        auto a = t.asking.get();
        if (t.asking_fen == fen) {
            if (a.value && a.value->outcome != lichess::Outcome::Unknown) {
                sortTableMoves(*a.value);
                t.answer = std::move(*a.value);
                t.online = true;
            } else {
                t.nothing = a;  // why: a failure (an outage, a 429, a refusal), or none (the position unknown)
            }
        }
    }
    // A 429's pause over: the tables asked again by themselves, as the tab said.
    const auto pause = tablesPause();
    if (t.nothing && tablesAskAgain(*t.nothing, pause)) t.nothing.reset();
    if (isReady(t.line_job)) {
        auto a = t.line_job.get();
        if (a.value)
            t.lines[t.line_job_fen] = std::move(*a.value);
        else
            t.lines_failed.insert(t.line_job_fen);
    }

    // Where the answer comes from: the disk's tables, else Lichess's once allowed.
    const bool op1  = pieces == 8;
    const int disk  = diskTablesPieces();
    const bool once = online_once_.count(positionKey(fen)) > 0;
    auto way        = tablesWay(pieces, op1, disk, diskTablesLocked(), lichess_.tablebase, once);
    if (!t.answer && way == TablesWay::Disk) {
        const auto paths = endgameTablesPaths();
        std::optional<lichess::Tablebase> answer;
        {
            const auto lock = lockSyzygy();
            answer          = probeDiskTables(board, paths);
        }
        if (answer) {
            sortTableMoves(*answer);
            t.answer = std::move(*answer);
            t.online = false;
        } else {
            way = tablesWay(pieces, op1, 0, true, lichess_.tablebase, once);  // a table missing: as without the disk
        }
    }
    if (!t.answer && way == TablesWay::Lichess && !t.nothing && !t.asking.valid()) {
        if (auto kept = cachedTablebase(storage_.root(), fen); kept && kept->outcome != lichess::Outcome::Unknown) {
            sortTableMoves(*kept);
            t.answer = std::move(*kept);
            t.online = true;
        } else if (settled) {
            t.asking_fen = fen;
            t.asking     = std::async(std::launch::async, [root = storage_.root(), fen, stop = jobs_.cancelFlag()] {
                bool asked = false;
                return keptOrAskedTables(root, fen, asked, stop.get());
            });
        }
    }
    if (!t.answer) {
        if (way == TablesWay::Consent && !online_tb_not_now_) {
            // The first use (mockup M4, board 5): what would go, and three answers.
            ui::beginCard("tables_tab_consent");
            ImGui::PushTextWrapPos();
            ImGui::TextUnformatted(
                pieces == 8
                    ? TR("Lichess's tables may know this ending: eight pieces with opposing pawns, their verdict "
                         "and the moves to the next capture.")
                    : trf("Lichess's tables know this ending ({} pieces); those of your disk do not.", pieces).c_str());
            ImGui::TextDisabled(
                "%s", TR("To ask, its FEN would go to tablebase.lichess.ovh, without an account. Nothing else."));
            if (ImGui::SmallButton(TR("Ask for this position"))) online_once_.insert(positionKey(fen));
            ImGui::SameLine();
            if (ImGui::SmallButton(TR("Always"))) {
                lichess_.tablebase = 1;
                saveLichessSettings();
            }
            ImGui::SameLine();
            if (ImGui::SmallButton(TR("Not now"))) online_tb_not_now_ = true;
            ImGui::TextDisabled("%s", TR("“Always” can be changed on the Lichess page, under Online for the analysis, "
                                         "which also says what is sent."));
            ImGui::PopTextWrapPos();
            ui::endCard();
        } else if (way == TablesWay::Consent || way == TablesWay::Refused) {
            ImGui::TextUnformatted(
                disk > 0 ? trf("Your tables on the disk go up to {} pieces, and Lichess's are not asked.", disk).c_str()
                         : TR("No tables on your disk, and Lichess's are not asked."));
            ImGui::TextDisabled("%s", way == TablesWay::Refused
                                          ? TR("The Lichess page, under Online for the analysis, says what is sent "
                                               "and lets you allow them.")
                                          : TR("“Not now” was chosen for Lichess's tables."));
            if (ImGui::SmallButton(TR("Ask for this position"))) online_once_.insert(positionKey(fen));
        } else if (t.asking.valid()) {
            ImGui::TextDisabled("%s", TR("Asking Lichess's tables…"));
        } else if (t.nothing && t.nothing->failed()) {
            // The pause counted down on Try again, which waits for it; asked again by itself once it is over (#141).
            drawRepairNotice(lichessNotice(*t.nothing, tablesNothing(*t.nothing, pause), lichessAccess(),
                                           retryState(Retry::BoardTables, urlHost(lichess::servers().tablebase))),
                             "tables_nothing");
        } else if (t.nothing) {
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s",
                               tablesNothing(*t.nothing, pause).c_str());
            if (ImGui::SmallButton(TR("Try again"))) t.nothing.reset();
        }
        ImGui::PopTextWrapPos();
        return;
    }

    // The verdict, where it comes from, how far.
    const auto& a      = *t.answer;
    const auto stm     = board.sideToMove();
    const auto group   = positionGroup(a);
    std::string header = a.checkmate   ? std::string(TR("Checkmate"))
                         : a.stalemate ? std::string(TR("Stalemate"))
                                       : outcomeVerdict(a.outcome, stm);
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.25f);
    ImGui::TextUnformatted(header.c_str());
    ImGui::PopFont();
    std::string about   = tablesSource(t.online, pieces, disk);
    const auto distance = tablesDistance(a.dtm, a.dtz, a.dtc);
    const char* metric  = tablesMetric(a.dtm, a.dtz, a.dtc);
    if (!distance.empty()) about += " · " + distance;
    about += " · " + std::string(stm == chess::Color::WHITE ? TR("White to move") : TR("Black to move"));
    ImGui::TextDisabled("%s", about.c_str());
    if (ImGui::IsItemHovered()) {
        auto tip = std::string(t.online ? TR("Asked once, kept in your profile: a verdict never changes.")
                                        : TR("Nothing leaves the machine."));
        if (metric[0]) tip += fmt::format(" ({})", metric);
        ImGui::SetTooltip("%s", tip.c_str());
    }
    ImGui::Spacing();

    // The moves by the outcome they leave, the best first; those that spoil it said with why.
    std::optional<std::string> play;
    const auto shownMove = [&](const lichess::TablebaseMove& m) { return localSan(m.san); };
    const auto hoverMove = [&](const lichess::TablebaseMove& m) {
        if (t.hovered != m.uci) {
            t.hovered       = m.uci;
            t.hovered_since = now;
        }
        BoardPreview preview;
        preview.ply = preview.at = b.ply;
        preview.moves.push_back(m.uci);
        if (const auto* line = boardTablesLine(m, now - t.hovered_since > 0.4))
            preview.moves.insert(preview.moves.end(), line->begin(), line->end());
        preview.label     = TR("The tables' line");
        preview.hover     = true;
        b.preview         = std::move(preview);
        b.preview_hovered = true;
        const char* name  = tablesMetric(m.dtm, m.dtz, m.dtc);
        ImGui::SetTooltip("%s", (trf("{}: {}", shownMove(m), afterMoveWords(m, stm)) +
                                 (name[0] ? fmt::format(" ({})", name) : std::string()) + "\n" +
                                 TR("Its main line on the board; a click plays it."))
                                    .c_str());
    };
    std::size_t asked_spoilers = 0;
    for (const auto& g : groupTablesMoves(a)) {
        ImGui::PushID(static_cast<int>(g.group));
        const bool spoil = !g.moves.empty() && spoilsOutcome(a, a.moves[g.moves.front()]);
        const auto title = tablesGroupTitle(g.group, group);
        if (spoil)
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s", title.c_str());
        else
            ImGui::TextUnformatted(title.c_str());
        ImGui::SameLine(0, px(6));
        ImGui::TextDisabled("· %d", static_cast<int>(g.moves.size()));
        if (spoil) {
            // Each with its refutation, the first move of the tables' line after it, and what the tables then say.
            for (std::size_t k = 0; k < g.moves.size() && k < SHOWN_SPOILERS; ++k) {
                const auto& m = a.moves[g.moves[k]];
                ImGui::PushID(static_cast<int>(k));
                if (ImGui::SmallButton(shownMove(m).c_str())) play = m.uci;
                if (ImGui::IsItemHovered()) hoverMove(m);
                ImGui::SameLine(0, px(8));
                std::string why;
                const bool ask = settled && asked_spoilers++ < ASKED_SPOILERS;
                if (const auto* line = boardTablesLine(m, ask); line && !line->empty()) {
                    auto after       = board;
                    const auto moved = chess::uci::uciToMove(after, m.uci);
                    after.makeMove(moved);
                    const auto reply = chess::uci::uciToMove(after, line->front());
                    if (reply != chess::Move::NO_MOVE)
                        why = "→ " + numbered(after, localSan(chess::uci::moveToSan(after, reply))) + " · ";
                }
                why += afterMoveWords(m, stm);
                ImGui::TextDisabled("%s", why.c_str());
                ImGui::PopID();
            }
            if (g.moves.size() > SHOWN_SPOILERS)
                ImGui::TextDisabled("%s", trf("and {} more", g.moves.size() - SHOWN_SPOILERS).c_str());
        } else {
            for (std::size_t k = 0; k < g.moves.size(); ++k) {
                const auto& m     = a.moves[g.moves[k]];
                const auto label  = shownMove(m);
                const float width = ImGui::CalcTextSize(label.c_str()).x + ImGui::GetStyle().FramePadding.x * 2;
                if (k > 0) ui::sameLineOrWrap(width, px(4));
                ImGui::PushID(static_cast<int>(k));
                if (ImGui::SmallButton(label.c_str())) play = m.uci;
                if (ImGui::IsItemHovered()) hoverMove(m);
                ImGui::PopID();
            }
        }
        ImGui::PopID();
        ImGui::Spacing();
    }
    ImGui::TextDisabled("%s", TR("A move hovered shows its main line on the board; a click plays it. The distance to "
                                 "mate up to six pieces; at seven, the moves to a capture or a pawn move; at eight "
                                 "with opposing pawns (op1), the moves to the next capture, promotion or mate. A win "
                                 "drawn by the fifty-move rule takes more than fifty moves without a capture or a pawn "
                                 "move."));

    // The assistant, taught by the tables: the game's move from here, in its own words and letters.
    if (b.ply < static_cast<int>(b.game.moves.size())) {
        const auto& gm  = b.game.moves[static_cast<std::size_t>(b.ply)];
        const auto uci  = chess::uci::moveToUci(gm.move, b.chess960);
        const auto it   = std::find_if(a.moves.begin(), a.moves.end(), [&](const auto& m) { return m.uci == uci; });
        const auto now_ = outcomeResult(a.outcome);
        const auto then = it == a.moves.end() ? std::nullopt : moveResult(*it);
        if (it != a.moves.end() && now_ && then) {
            const bool fr      = i18n::language() == i18n::Language::French;
            const auto letters = [&](const std::string& san) { return fr ? frenchSan(san) : san; };
            const auto verdict = [](int r) {
                return r > 0 ? commentary::Verdict::Win : r < 0 ? commentary::Verdict::Loss : commentary::Verdict::Draw;
            };
            commentary::TablesSaid said;
            said.move   = numbered(board, letters(chess::uci::moveToSan(board, gm.move)));
            said.mover  = stm == chess::Color::WHITE ? commentary::Color::White : commentary::Color::Black;
            said.before = verdict(*now_);
            said.after  = verdict(*then);
            said.men    = pieces;
            if (it->dtm && *then != 0) said.mate = movesOfPlies(*it->dtm);
            if (*then < *now_) {
                if (const auto* line = boardTablesLine(*it, settled); line && !line->empty()) {
                    auto after = board;
                    after.makeMove(gm.move);
                    if (const auto reply = chess::uci::uciToMove(after, line->front()); reply != chess::Move::NO_MOVE)
                        said.refutation = numbered(after, letters(chess::uci::moveToSan(after, reply)));
                }
                if (const auto best = moveResult(a.moves.front());
                    best && *best == *now_ && a.moves.front().uci != uci) {
                    const auto m = chess::uci::uciToMove(board, a.moves.front().uci);
                    if (m != chess::Move::NO_MOVE)
                        said.kept = numbered(board, letters(chess::uci::moveToSan(board, m)));
                }
            }
            ImGui::Spacing();
            const auto tint = ImGui::ColorConvertU32ToFloat4(ui::assistantColour());
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(tint.x, tint.y, tint.z, 0.08f));
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(tint.x, tint.y, tint.z, 0.40f));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(12), px(10)));
            ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, px(8));
            ImGui::BeginChild(
                "##tables_assistant", ImVec2(0, 0),
                ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_AutoResizeY);
            ImGui::PushTextWrapPos();
            ImGui::TextColored(tint, "%s", TR("The assistant, taught by the tables"));
            ImGui::PushFont(ui::italicFont(), 0.0f);
            ImGui::TextColored(
                ImGui::ColorConvertU32ToFloat4(ui::assistantColour()), "%s",
                commentary::tablesSentences(said, fr ? commentary::Lang::Fr : commentary::Lang::En).c_str());
            ImGui::PopFont();
            ImGui::TextDisabled("%s", TR("Said only when the tables say so: written from their verdicts and "
                                         "distances, without the model."));
            ImGui::PopTextWrapPos();
            ImGui::EndChild();
            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor(2);
        }
    }
    ImGui::PopTextWrapPos();
    if (play) {
        const auto move = chess::uci::uciToMove(board, *play);
        if (move != chess::Move::NO_MOVE) boardPlayAsked(move);
    }
}

}  // namespace fcg
