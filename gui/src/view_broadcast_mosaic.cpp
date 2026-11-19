// The broadcast mosaic (mockup M4, board 4): a round followed on Lichess, its games side by side as in the Live
// mosaic, each analysed by an engine of the library, a thread or a few a game, while the mosaic shows. The moves come
// from Lichess's stream, opened again after a break; a game can be followed on the Board page, which moves on with it;
// once the whole round has ended, it goes into the « Broadcasts » database.

#include <algorithm>
#include <chrono>
#include <string>
#include <vector>

#include <fmt/include/fmt/format.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <study/game_tree.hpp>
#include <study/import.hpp>

#include "app.hpp"
#include "board_text.hpp"
#include "board_view.hpp"
#include "console.hpp"
#include "i18n.hpp"
#include "live.hpp"
#include "online_cache.hpp"
#include "pgn_store.hpp"
#include "ui.hpp"

namespace fcg {

namespace {

float px(float v) { return v * ImGui::GetFontSize() / 16.0f; }

bool playing(const std::string& result) { return result.empty() || result == "*"; }

// The clock a side had after its last move, from the game's [%clk]; empty when the broadcast sends none.
std::string clockOf(const ReplayGame& game, chess::Color side) {
    for (auto it = game.moves.rbegin(); it != game.moves.rend(); ++it)
        if (it->mover == side && it->clock_ms) return ui::formatClock(*it->clock_ms);
    return {};
}

// The engine's score for White, in pawns, and its line in the reader's notation.
struct Seen {
    std::optional<fastchess::Score> white;  // White's point of view
    std::string score, line;
    int64_t depth = 0;
};

Seen seenBy(const Analyser* analyser, const chess::Board& board) {
    Seen s;
    if (!analyser || analyser->lines().empty()) return s;
    const auto& l = analyser->lines().front();
    if (!l.score) return s;
    auto w = *l.score;
    if (board.sideToMove() == chess::Color::BLACK) w.value = -w.value;
    s.white = w;
    s.score = w.type == fastchess::ScoreType::MATE ? fmt::format("#{}", w.value)
                                                   : fmt::format("{:+.2f}", static_cast<double>(w.value) / 100.0);
    s.line  = localLine(pvToSan(board, l.pv, 4));
    s.depth = l.depth;
    return s;
}

// What the tables say of a position, for White: none when they do not know it.
const char* tablesSay(const std::optional<lichess::Outcome>& outcome, bool white_to_move) {
    if (!outcome || *outcome == lichess::Outcome::Unknown) return nullptr;
    const auto o   = *outcome;
    const bool win = o == lichess::Outcome::Win, loss = o == lichess::Outcome::Loss;
    if (o == lichess::Outcome::Draw || o == lichess::Outcome::CursedWin || o == lichess::Outcome::BlessedLoss)
        return TR("draw");
    if ((win && white_to_move) || (loss && !white_to_move)) return TR("White wins");
    if (win || loss) return TR("Black wins");
    return nullptr;  // a win or a loss the fifty moves may change: not said
}

}  // namespace

void App::openBroadcastMosaic(const std::string& round_id) {
    if (broadcast_round_ != round_id) {
        broadcast_boards_.clear();  // their engines end
        broadcast_selected_.clear();
    }
    broadcast_round_ = round_id;
    page_            = Page::Lichess;  // its view, or the view shown
}

void App::pollBroadcastBoards() {
    const FollowedRound* round = nullptr;
    for (const auto& f : followed_rounds_)
        if (f.entry.round.id == broadcast_round_) round = &f;
    const bool players = broadcast_round_ == lichess::PLAYERS_MOSAIC;  // the followed players' games (#50)
    if (!round && !players) {
        broadcast_boards_.clear();
        if (!broadcast_round_.empty()) broadcast_round_.clear();  // no longer followed: the page itself again
    }
    // The games, read again when Lichess sent them again.
    if (round || players) {
        const auto games = round               ? round->follower->games()
                           : players_follower_ ? players_follower_->games()
                                               : std::vector<lichess::BroadcastGame>{};
        if (players)  // a player's game no longer followed leaves the mosaic
            std::erase_if(broadcast_boards_, [&](const auto& b) {
                return std::none_of(games.begin(), games.end(), [&](const auto& g) { return g.url == b.url; });
            });
        for (const auto& g : games) {
            auto it = std::find_if(broadcast_boards_.begin(), broadcast_boards_.end(),
                                   [&](const auto& b) { return b.url == g.url; });
            if (it == broadcast_boards_.end()) {
                broadcast_boards_.emplace_back();
                it      = broadcast_boards_.end() - 1;
                it->url = g.url;
            }
            if (it->version == g.version) continue;
            it->version   = g.version;
            it->white     = g.white;
            it->black     = g.black;
            it->white_elo = g.white_elo;
            it->black_elo = g.black_elo;
            it->result    = g.result;
            it->pgn       = g.pgn;
            if (auto game = parseReplay(g.pgn)) {
                it->game  = std::move(*game);
                it->board = boardAt(it->game, false, static_cast<int>(it->game.moves.size()));
            }
        }
        if (broadcast_selected_.empty() && !broadcast_boards_.empty()) broadcast_selected_ = broadcast_boards_[0].url;
    }
    // Their engines: one each for the games still played, while the mosaic shows (its engine and threads a game).
    const bool shown        = ImGui::GetFrameCount() - broadcast_drawn_ < 3;
    const EngineEntry* with = nullptr;
    for (const auto& e : engines_)
        if (e.name == lichess_.broadcast_engine) with = &e;
    // As many games as half the cores the tournaments leave allow, the game chosen first: an open's round has a
    // hundred boards.
    const int threads = std::clamp(lichess_.broadcast_threads, 1, 8);
    int room          = std::max(1, (topology_.logical - tournamentThreads()) / 2 / threads);
    std::vector<BroadcastBoard*> order;
    for (auto& b : broadcast_boards_)
        if (b.url == broadcast_selected_) order.push_back(&b);
    for (auto& b : broadcast_boards_)
        if (b.url != broadcast_selected_) order.push_back(&b);
    for (auto* p : order) {
        auto& b = *p;
        if (!shown || !with || !playing(b.result) || room <= 0 || liveGameOn()) {
            b.analyser.reset();
            continue;
        }
        --room;
        if (!b.analyser) {
            b.analyser = std::make_unique<Analyser>();
            AnalysisSettings settings;
            settings.engine  = with->name;
            settings.threads = threads;
            settings.multipv = 1;
            b.analyser->start(*with, settings);
        }
        AnalysisPosition position;
        position.fen = b.game.start_fen.empty() ? std::string(chess::constants::STARTPOS) : b.game.start_fen;
        for (const auto& m : b.game.moves) position.moves.push_back(chess::uci::moveToUci(m.move, false));
        b.analyser->analyse(position);  // the same position: nothing happens
        b.analyser->poll();
    }
    // The tables' verdict for the games of 7 pieces or fewer, when the user said « always » to Lichess's tables (M4,
    // board 4): from the profile's answers, else asked, one position at a time, while the mosaic shows.
    if (broadcast_tb_job_.valid() && broadcast_tb_job_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        const auto answer = broadcast_tb_job_.get();
        if (answer.value) keepTablebase(storage_.root(), broadcast_tb_fen_, *answer.value);
    }
    for (auto& b : broadcast_boards_) {
        const auto pieces = b.board.occ().count();
        if (pieces > 7 || pieces < 3 || lichess_.tablebase != 1) {
            b.tb.reset();
            continue;
        }
        const auto fen = b.board.getFen();
        if (fen == b.tb_fen && b.tb) continue;
        if (const auto known = cachedTablebase(storage_.root(), fen)) {
            b.tb_fen = fen;
            b.tb     = known->outcome;
        } else if (shown && !broadcast_tb_job_.valid()) {
            broadcast_tb_fen_ = fen;
            broadcast_tb_job_ = std::async(std::launch::async, [fen] { return lichess::tablebase(fen); });
        }
    }

    // A followed round whose games have all ended goes into the « Broadcasts » database, once, shown or not.
    for (const auto& f : followed_rounds_) {
        const auto changes = f.follower->changes();
        auto& seen         = broadcast_checked_[f.entry.round.id];
        if (seen == changes || broadcast_saved_.count(f.entry.round.id)) continue;
        seen             = changes;
        const auto games = f.follower->games();
        if (!games.empty() && std::none_of(games.begin(), games.end(), [](const auto& g) { return playing(g.result); }))
            saveBroadcastRound(f, games);
    }

    // The game the Board page follows: it moves on, unless the user changed it.
    if (board_broadcast_) {
        const BroadcastBoard* game = nullptr;
        for (const auto& b : broadcast_boards_)
            if (b.url == board_broadcast_->url) game = &b;
        for (const auto& f : followed_rounds_) {
            if (game || f.entry.round.id != board_broadcast_->round) continue;
            // Another round on the mosaic: read from its follower.
            for (const auto& g : f.follower->games()) {
                if (g.url != board_broadcast_->url || g.version == board_broadcast_->version) continue;
                if (auto replay = parseReplay(g.pgn)) {
                    BroadcastBoard b;
                    b.url     = g.url;
                    b.version = g.version;
                    b.game    = std::move(*replay);
                    auto& bb  = board_;
                    if (bb.tree.variations.empty() && bb.undo.empty()) {
                        const bool last = bb.ply >= static_cast<int>(bb.game.moves.size());
                        bb.tree         = GameTree{b.game, {}};
                        bb.line         = -1;
                        bb.game         = b.game;
                        if (last) bb.ply = static_cast<int>(bb.game.moves.size());
                        board_broadcast_->version = g.version;
                    }
                }
            }
        }
        auto& bb = board_;
        if (game && game->version != board_broadcast_->version) {
            // The user's own changes end the following: the game is theirs then.
            if (!bb.tree.variations.empty() || !bb.undo.empty()) {
                board_broadcast_.reset();
            } else {
                const bool last = bb.ply >= static_cast<int>(bb.game.moves.size());
                bb.game         = game->game;
                bb.tree         = GameTree{bb.game, {}};
                bb.line         = -1;
                if (last) bb.ply = static_cast<int>(bb.game.moves.size());
                bb.ply                    = std::min(bb.ply, static_cast<int>(bb.game.moves.size()));
                board_broadcast_->version = game->version;
            }
        }
    }
}

void App::saveBroadcastRound(const FollowedRound& round, const std::vector<lichess::BroadcastGame>& games_sent) {
    broadcast_saved_.insert(round.entry.round.id);
    auto& s          = shelf();
    const auto name  = std::string(TR("Broadcasts"));
    std::size_t base = s.entries().size();
    for (std::size_t i = 0; i < s.entries().size(); ++i)
        if (!s.entries()[i].source && s.entries()[i].name == name) base = i;
    try {
        if (base == s.entries().size()) base = s.create(name);
        const auto db = s.open(base);
        if (!db || db->readOnly()) return;
        std::vector<study::ImportedGame> games;
        for (const auto& g : games_sent) {
            study::ImportedGame game;
            if (study::readImportedGame(g.pgn, game)) games.push_back(std::move(game));
        }
        const auto count = games.size();
        (void)db->append(std::move(games), false);
        s.rebuildTree(base);
        Console::instance().add(trf("{} · {}: the round's {} games saved in “{}”.", round.entry.tour.name,
                                    round.entry.round.name, count, name),
                                false);
    } catch (const std::exception& e) {
        Console::instance().add(trf("The round could not be saved in “{}”: {}", name, e.what()), true);
    }
}

void App::followBroadcastOnBoard(const BroadcastBoard& game) {
    openOnBoard(game.game, static_cast<int>(game.game.moves.size()), false,
                fmt::format("{} – {}", game.white, game.black), false);
    BoardBroadcast follow;
    follow.round     = broadcast_round_;
    follow.url       = game.url;
    follow.version   = game.version;
    board_broadcast_ = std::move(follow);
}

void App::drawBoardBroadcastLine() {
    if (!board_broadcast_) return;
    auto& b = board_;
    ImGui::TextColored(ImVec4(0.82f, 0.29f, 0.23f, 1.0f), "%s", TR("Live from Lichess"));
    if (b.ply < static_cast<int>(b.game.moves.size()) || b.line >= 0) {
        ImGui::SameLine();
        if (ImGui::SmallButton(TR("Back to live"))) {
            b.line = -1;
            b.game = lineOf(b.tree, -1);
            b.ply  = static_cast<int>(b.game.moves.size());
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", TR("The game went on meanwhile: the board goes to its last move."));
    }
}

void App::drawBroadcastMosaic() {
    broadcast_drawn_           = ImGui::GetFrameCount();
    const FollowedRound* round = nullptr;
    for (const auto& f : followed_rounds_)
        if (f.entry.round.id == broadcast_round_) round = &f;
    if (ImGui::SmallButton(TR("‹ Lichess"))) {
        broadcast_round_.clear();
        return;
    }
    const bool players = broadcast_round_ == lichess::PLAYERS_MOSAIC;  // the followed players' games (#50)
    if (!round && !players) return;
    ImGui::SameLine();
    if (players) {
        ImGui::TextUnformatted(TR("Followed players"));
        ImGui::SameLine();
        const auto live = std::count_if(broadcast_boards_.begin(), broadcast_boards_.end(),
                                        [](const auto& b) { return playing(b.result); });
        if (live > 0)
            ImGui::TextColored(ImVec4(0.82f, 0.29f, 0.23f, 1.0f), "%s",
                               trn("{} game live", "{} games live", static_cast<uint64_t>(live), live).c_str());
        const auto error = players_follower_ ? players_follower_->error() : std::string();
        if (!error.empty()) {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", trf("· Lichess: {}", error).c_str());
        }
    } else {
        ImGui::TextUnformatted(fmt::format("{} · {}", round->entry.tour.name, round->entry.round.name).c_str());
        ImGui::SameLine();
        const auto error = round->follower->error();
        if (round->follower->streaming())
            ImGui::TextColored(ImVec4(0.82f, 0.29f, 0.23f, 1.0f), "%s", TR("live"));
        else
            ImGui::TextDisabled("%s", error.empty()
                                          ? TR("connecting…")
                                          : trf("the stream broke ({}): it opens again by itself", error).c_str());
    }

    // Analysed by: an engine of the library, its threads for each game; or none.
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", TR("Analysed by"));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(px(200));
    const char* none = TR("no engine");
    if (ImGui::BeginCombo("##broadcast_engine",
                          lichess_.broadcast_engine.empty() ? none : lichess_.broadcast_engine.c_str())) {
        if (ImGui::Selectable(none, lichess_.broadcast_engine.empty())) {
            lichess_.broadcast_engine.clear();
            saveLichessSettings();
        }
        for (const auto& e : engines_) {
            if (ImGui::Selectable(e.name.c_str(), e.name == lichess_.broadcast_engine)) {
                lichess_.broadcast_engine = e.name;
                for (auto& b : broadcast_boards_) b.analyser.reset();  // started again with it
                saveLichessSettings();
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(px(100));
    int threads = lichess_.broadcast_threads;
    if (ImGui::InputInt("##broadcast_threads", &threads, 1, 1)) {
        lichess_.broadcast_threads = std::clamp(threads, 1, 8);
        for (auto& b : broadcast_boards_)
            if (b.analyser) {
                auto settings    = b.analyser->settings();
                settings.threads = lichess_.broadcast_threads;
                b.analyser->update(settings);
            }
        saveLichessSettings();
    }
    ImGui::SameLine();
    ImGui::TextDisabled(
        "%s", trn("thread a game", "threads a game", static_cast<uint64_t>(lichess_.broadcast_threads)).c_str());
    ImGui::SameLine();
    if (!players && ImGui::SmallButton(TR("Stop following"))) {
        for (std::size_t r = 0; r < followed_rounds_.size(); ++r) {
            if (followed_rounds_[r].entry.round.id != broadcast_round_) continue;
            followed_rounds_[r].follower->stop();
            followed_rounds_.erase(followed_rounds_.begin() + static_cast<std::ptrdiff_t>(r));
            break;
        }
        broadcast_boards_.clear();
        broadcast_round_.clear();
        return;
    }
    ImGui::Spacing();

    if (broadcast_boards_.empty()) {
        ImGui::TextDisabled("%s", players
                                      ? TR("None of them plays now: their games come here as they start, three "
                                           "moves behind, as Lichess shows them.")
                                      : TR("No game of the round has come yet: they appear as Lichess sends them."));
        return;
    }
    // The mosaic on the left, the game chosen and the round on the right.
    const float avail  = ImGui::GetContentRegionAvail().x;
    const float side_w = std::min(px(360), avail * 0.34f);
    const float grid_w = avail - side_w - px(12);
    const float room   = grid_w - ImGui::GetStyle().ScrollbarSize - px(2);  // the cards beside the scrollbar
    const int cols     = std::max(1, static_cast<int>(room / px(230)));
    const float card   = (room - px(8) * static_cast<float>(cols - 1)) / static_cast<float>(cols);
    ImGui::BeginChild("##broadcast_grid", ImVec2(grid_w, 0));
    const float line = ImGui::GetTextLineHeight();
    for (std::size_t k = 0; k < broadcast_boards_.size(); ++k) {
        auto& b = broadcast_boards_[k];
        if (k % static_cast<std::size_t>(cols) != 0) ImGui::SameLine(0, px(8));
        const bool selected = b.url == broadcast_selected_;
        ImGui::PushStyleColor(ImGuiCol_Border, ImGui::ColorConvertU32ToFloat4(selected ? ui::ACCENT : ui::CARD_BD));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ColorConvertU32ToFloat4(ui::CARD_BG));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(6), px(5)));
        const float inner = card - px(12);
        // Two names, the board, the bar, a line of text; their spacing and the card's padding.
        const float h = inner + line * 3 + px(4) + ImGui::GetStyle().ItemSpacing.y * 4 + px(5) * 2 + px(4);
        ImGui::BeginChild(fmt::format("##bcard{}", k).c_str(), ImVec2(card, h),
                          ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        const auto name_row = [&](chess::Color color) {
            const auto& name = color == chess::Color::WHITE ? b.white : b.black;
            const auto clock = clockOf(b.game, color);
            const bool moves = playing(b.result) && b.board.sideToMove() == color;
            ImGui::PushStyleColor(ImGuiCol_Text, moves ? ui::ACCENT : ImGui::GetColorU32(ImGuiCol_Text));
            const ImVec2 pos = ImGui::GetCursorScreenPos();
            const float end  = pos.x + inner - ImGui::CalcTextSize(clock.c_str()).x - px(6);
            ImGui::RenderTextEllipsis(ImGui::GetWindowDrawList(), pos, ImVec2(end, pos.y + line), end, name.c_str(),
                                      nullptr, nullptr);
            ImGui::Dummy(ImVec2(end - pos.x, line));
            ImGui::PopStyleColor();
            if (!clock.empty()) {
                ImGui::SameLine(inner - ImGui::CalcTextSize(clock.c_str()).x + ImGui::GetStyle().WindowPadding.x);
                ImGui::TextDisabled("%s", clock.c_str());
            }
        };
        name_row(chess::Color::BLACK);
        BoardDecor decor;
        decor.eval_white = -1.0f;
        if (!b.game.moves.empty()) decor.last_move = b.game.moves.back().move;
        drawBoard(atlas_, b.board, inner, decor);
        name_row(chess::Color::WHITE);
        // The engine's view: a thin bar, its score and line; the result once the game ended.
        const auto seen = seenBy(b.analyser.get(), b.board);
        {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const auto pos = ImGui::GetCursorScreenPos();
            const float f =
                seen.white ? std::clamp(0.5f + ui::whitePawns(*seen.white, chess::Color::WHITE) / 16.0f, 0.0f, 1.0f)
                           : 0.5f;
            const float bar = px(4);
            dl->AddRectFilled(pos, ImVec2(pos.x + inner, pos.y + bar), IM_COL32(25, 25, 25, 255), bar);
            dl->AddRectFilled(pos, ImVec2(pos.x + inner * f, pos.y + bar), IM_COL32(236, 236, 236, 255), bar);
            dl->AddRect(pos, ImVec2(pos.x + inner, pos.y + bar), IM_COL32(130, 130, 130, 255), bar);
            ImGui::Dummy(ImVec2(inner, bar));
        }
        if (!playing(b.result)) {
            ImGui::TextUnformatted(b.result.c_str());
        } else if (const char* says = b.tb_fen == b.board.getFen()
                                          ? tablesSay(b.tb, b.board.sideToMove() == chess::Color::WHITE)
                                          : nullptr) {
            // The tables' verdict, for White: exact, before any engine's score.
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::ACCENT), "%s",
                               trf("Tables (Lichess): {}", says).c_str());
        } else if (!seen.score.empty()) {
            ImGui::TextDisabled("%s", fmt::format("{}  {}", seen.score, seen.line).c_str());
        } else if (lichess_.broadcast_engine.empty()) {
            ImGui::TextDisabled(" ");
        } else {
            ImGui::TextDisabled("%s", b.analyser ? TR("analysing…") : TR("not analysed: the cores are taken"));
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(2);
        if (ImGui::IsItemClicked()) broadcast_selected_ = b.url;
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) followBroadcastOnBoard(b);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", TR("A click chooses the game; a double click follows it on the board."));
    }
    ImGui::EndChild();
    ImGui::SameLine(0, px(12));

    // The game chosen, and the round.
    ImGui::BeginChild("##broadcast_side", ImVec2(side_w, 0));
    ImGui::PushTextWrapPos();
    const BroadcastBoard* chosen = nullptr;
    for (const auto& b : broadcast_boards_)
        if (b.url == broadcast_selected_) chosen = &b;
    if (chosen) {
        ui::beginCard("broadcast_chosen");
        ImGui::PushTextWrapPos();
        ImGui::TextUnformatted(fmt::format("{} – {}", chosen->white, chosen->black).c_str());
        const auto moves = chosen->game.moves.size();
        const auto move  = moves / 2 + 1;
        ImGui::TextDisabled("%s", playing(chosen->result) ? (chosen->board.sideToMove() == chess::Color::WHITE
                                                                 ? trf("playing · move {}, White to move", move)
                                                                 : trf("playing · move {}, Black to move", move))
                                                                .c_str()
                                                          : trf("ended · {}", chosen->result).c_str());
        // Its last moves, numbered.
        std::string last;
        const std::size_t from = moves > 12 ? moves - 12 : 0;
        auto at                = boardAt(chosen->game, false, static_cast<int>(from));
        for (std::size_t i = from; i < moves; ++i) {
            const auto& m = chosen->game.moves[i];
            if (at.sideToMove() == chess::Color::WHITE)
                last += fmt::format("{}.", at.fullMoveNumber());
            else if (i == from)
                last += fmt::format("{}…", at.fullMoveNumber());
            last += localSan(m.san.empty() ? chess::uci::moveToSan(at, m.move) : m.san) + " ";
            at.makeMove<true>(m.move);
        }
        if (from > 0) last = "… " + last;
        ImGui::TextUnformatted(last.c_str());
        if (ImGui::Button(TR("Follow on the board"))) followBroadcastOnBoard(*chosen);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", TR("On the board, the game moves on with each move received; going back to "
                                       "analyse it lets it move on behind, “Back to live” brings it back."));
        ImGui::SameLine();
        if (ImGui::Button(TR("Copy the PGN"))) ImGui::SetClipboardText(chosen->pgn.c_str());
        ImGui::PopTextWrapPos();
        ui::endCard();
        ImGui::Dummy(ImVec2(0, px(6)));
    }
    ui::beginCard("broadcast_round");
    ImGui::TextUnformatted(players ? TR("Their games") : TR("The round"));
    if (ImGui::BeginTable("##round_games", 2, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("##game", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("##state", ImGuiTableColumnFlags_WidthFixed);  // as wide as its widest
        for (const auto& b : broadcast_boards_) {
            ImGui::PushID(b.url.c_str());
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            auto state = playing(b.result) ? trf("move {}", b.game.moves.size() / 2 + 1) : b.result;
            if (const char* says = playing(b.result) && b.tb_fen == b.board.getFen()
                                       ? tablesSay(b.tb, b.board.sideToMove() == chess::Color::WHITE)
                                       : nullptr)
                state += " · " + trf("tables: {}", says);
            if (ImGui::Selectable(fmt::format("{} – {}##round", b.white, b.black).c_str(), b.url == broadcast_selected_,
                                  ImGuiSelectableFlags_SpanAllColumns))
                broadcast_selected_ = b.url;
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) followBroadcastOnBoard(b);
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", state.c_str());
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ui::endCard();
    ImGui::TextDisabled("%s", players ? TR("The moves come from Lichess, three moves behind, as it shows them to "
                                           "anyone; the analysis is made on your machine, with your engines. A game "
                                           "that ends stays here until another takes its place.")
                                      : TR("The moves come from Lichess's stream, opened again by itself after a "
                                           "break; the analysis is made on your machine, with your engines. A "
                                           "finished game stays here until the round ends, then the whole round "
                                           "goes into the “Broadcasts” database."));
    ImGui::PopTextWrapPos();
    ImGui::EndChild();
}

}  // namespace fcg
