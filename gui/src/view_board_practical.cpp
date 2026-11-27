// The practical moves of the study Board's Analysis tab (#94, mockup M13, board 7, point 17): "Against a human of 1500,
// rapid". Among the moves the engine puts within 0.3 pawn of its best, the one that scores best against a player of
// that level in that level's games (Lichess's explorer with a linked account, else the databases filtered by rating),
// from 200 games; outside known games, the one that leaves the opponent the fewest good answers by the engine. The
// table of the candidates, the "best" and "practical" badges, a sentence held to the figures, the arrows on the board.

#include <algorithm>
#include <chrono>
#include <string>
#include <vector>

#include <fmt/include/fmt/format.h>
#include <imgui.h>

#include "app.hpp"
#include "board_text.hpp"
#include "endgames.hpp"
#include "guide.hpp"
#include "i18n.hpp"
#include "practical.hpp"
#include "ui.hpp"

namespace fcg {

namespace {

float px(float v) { return v * ImGui::GetFontSize() / 16.0f; }

// A move numbered from the position before it, in the interface's letters: "4.Ng5", "4…d5".
std::string numberedMove(const chess::Board& before, const std::string& uci) {
    const auto m = chess::uci::uciToMove(before, uci);
    if (m == chess::Move::NO_MOVE) return uci;
    return fmt::format("{}{}{}", before.fullMoveNumber(), before.sideToMove() == chess::Color::WHITE ? "." : "…",
                       localSan(chess::uci::moveToSan(before, m)));
}

chess::Board after(chess::Board board, const std::string& uci) {
    const auto m = chess::uci::uciToMove(board, uci);
    if (m != chess::Move::NO_MOVE) board.makeMove(m);
    return board;
}

// A text in a table's cell: wider than the cell, it ends in "…", whole under the mouse.
void cellText(const std::string& text, bool muted = false) {
    const float room  = ImGui::GetContentRegionAvail().x;
    std::string shown = text;
    if (room > 0 && ImGui::CalcTextSize(text.c_str()).x > room) {
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

// A small pill after a move: "best" in grey, "practical" in the accent.
void badge(const char* text, bool accent) {
    ImGui::SameLine(0, px(6));
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const float fs  = ImGui::GetFontSize() * 0.82f;
    const ImVec2 sz = ImGui::GetFont()->CalcTextSizeA(fs, FLT_MAX, 0.0f, text);
    const ImVec2 pad(px(5), px(1));
    const float y    = at.y + (ImGui::GetTextLineHeight() - sz.y) / 2 - pad.y;
    auto* dl         = ImGui::GetWindowDrawList();
    const ImU32 fill = accent ? ui::ACCENT : ImGui::GetColorU32(ImGuiCol_FrameBg);
    dl->AddRectFilled(ImVec2(at.x, y), ImVec2(at.x + sz.x + pad.x * 2, y + sz.y + pad.y * 2), fill, px(5));
    dl->AddText(ImGui::GetFont(), fs, ImVec2(at.x + pad.x, y + pad.y),
                accent ? IM_COL32_WHITE : ImGui::GetColorU32(ImGuiCol_TextDisabled), text);
    ImGui::Dummy(ImVec2(sz.x + pad.x * 2, ImGui::GetTextLineHeight()));
}

const char* speedName(const std::string& speed) {
    if (speed == "blitz") return TR("blitz");
    if (speed == "classical") return TR("classical");
    if (speed == "correspondence") return TR("correspondence");
    return TR("rapid");
}

// The engine's searches of the practical moves: the opponent's replies after a candidate, all of them (MultiPV), and
// the answer to the level's most played reply.
constexpr uint64_t PRACTICAL_NODES = 1000000;
// The depth of the analysis's lines from which the level's replies along the practical move's line are asked.
constexpr int64_t PRACTICAL_SETTLED_DEPTH = 16;

}  // namespace

const std::vector<LevelMove>* App::practicalLevelAt(const chess::Board& board, bool ask) {
    auto& p           = practical_;
    const auto& s     = board_.settings;
    const bool online = lichess_account_ && !lichess_account_->token.empty();
    const auto key    = fmt::format("{}|{}|{}|{}", positionKey(board.getFen()), s.practical_level,
                                    online ? s.practical_speed : std::string(), online ? "lichess" : "databases");
    if (const auto it = p.level.find(key); it != p.level.end()) return &it->second;
    if (!ask || p.level_failed.count(key) > 0 || p.explorer_job.valid() || p.db_job.valid()) return nullptr;
    p.job_key = key;
    p.error.clear();
    if (online) {
        // What leaves the machine: the position and the level, to Lichess's explorer, with the account's token.
        lichess::ExplorerQuery q;
        q.source       = lichess::ExplorerSource::Lichess;
        q.fen          = board.getFen();
        q.speeds       = {s.practical_speed};
        q.ratings      = {explorerGroup(s.practical_level)};
        q.moves        = 12;
        q.top_games    = 0;
        q.token        = lichess_account_->token;
        p.explorer_job = std::async(std::launch::async, [q] { return lichess::explorer(q); });
    } else {
        std::vector<LevelSource> sources;
        auto& sh = shelf();
        for (std::size_t i = 0; i < sh.entries().size(); ++i)
            if (!sh.entries()[i].source)
                if (auto source = sh.treeSource(i); source && source->tree)
                    sources.push_back({source->tree, source->snapshot});
        p.db_job = std::async(std::launch::async, [sources, board, level = s.practical_level] {
            return levelMoves(sources, board, level);
        });
    }
    return nullptr;
}

void App::drawPractical() {
    auto& b = board_;
    auto& p = practical_;
    auto& s = b.settings;
    // The answers that came, whatever the position shown now.
    if (isReady(p.explorer_job)) {
        auto a = p.explorer_job.get();
        if (a.value) {
            p.level[p.job_key] = levelMoves(*a.value);
        } else {
            p.level_failed.insert(p.job_key);
            p.error = lichess::describe(a);
        }
    }
    if (isReady(p.db_job)) {
        try {
            p.level[p.job_key] = p.db_job.get();
        } catch (const std::exception& e) {
            // A database's tree that cannot be read: no moves for that position, not asked again; why in the log.
            p.level_failed.insert(p.job_key);
            (void)loggedStoreError(trf("The practical moves could not be read: {}.", storeErrorText(e)), e);
        }
    }

    // The line: on or off, the level, the speed, where the games come from.
    const bool online      = lichess_account_ && !lichess_account_->token.empty();
    const auto [low, high] = levelBand(s.practical_level);
    const ui::TopicRegion topic(guide::topics::PracticalMoves);
    ui::beginCard("practical");
    ImGui::PushTextWrapPos();
    bool on = s.practical;
    if (ImGui::Checkbox(TR("Against a human of"), &on)) {
        s.practical = on;
        saveBoardSettings();
        if (!on && p.engine) {
            p.engine->stop();
            p.engine.reset();
            p.task.clear();
        }
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(px(74));
    if (ImGui::BeginCombo("##practical_level", std::to_string(s.practical_level).c_str())) {
        for (const int level : practicalLevels())
            if (ImGui::Selectable(std::to_string(level).c_str(), level == s.practical_level)) {
                s.practical_level = level;
                saveBoardSettings();
            }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!online);
    ImGui::SetNextItemWidth(px(120));
    if (ImGui::BeginCombo("##practical_speed", speedName(s.practical_speed))) {
        for (const auto& speed : practicalSpeeds())
            if (ImGui::Selectable(speedName(speed), speed == s.practical_speed)) {
                s.practical_speed = speed;
                saveBoardSettings();
            }
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();
    if (!online && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", TR("The speed chooses Lichess's games, with a linked account; your databases are taken "
                                   "by rating alone."));
    ImGui::SameLine();
    const auto source = online ? trf("Lichess {}–{}, {} (linked account)", low, high == 4000 ? 3000 : high,
                                     speedName(s.practical_speed))
                               : trf("your databases, players rated {}–{} on average", low, high == 4000 ? 3000 : high);
    ImGui::TextDisabled("· %s", source.c_str());
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", online ? TR("Sent: the position and the level, to Lichess's explorer, with your "
                                            "account's token.")
                                       : TR("Nothing leaves the machine: Lichess's games need a linked account."));
    if (!s.practical) {
        ImGui::PopTextWrapPos();
        ui::endCard();
        return;
    }

    // The engine's lines of the position: the candidates.
    const auto board    = boardAt(b.game, b.chess960, b.ply);
    const auto fen      = board.getFen();
    const auto stm      = board.sideToMove();
    const auto position = analysisPosition(b.game, b.chess960, b.ply);
    const auto& lines   = b.analyser->lines();
    const double now    = ImGui::GetTime();
    if (p.seen != fen) {
        p.seen  = fen;
        p.since = now;
    }
    const bool settled = now - p.since > 0.4;
    if (!b.analyser->active() || !(b.analyser->position() == position) || lines.empty()) {
        ImGui::TextDisabled("%s", TR("The practical move needs the engine's lines here: start the analysis (Alt+A)."));
        ImGui::PopTextWrapPos();
        ui::endCard();
        return;
    }
    const lichess::Tablebase* tables =
        board_tables_.answer && board_tables_.fen == fen ? &*board_tables_.answer : nullptr;
    auto candidates = practicalCandidates(lines, tables);
    if (candidates.empty()) {
        ImGui::PopTextWrapPos();
        ui::endCard();
        return;
    }
    const auto* level = practicalLevelAt(board, settled);
    static const std::vector<LevelMove> none;
    for (auto& c : candidates)
        if (const auto it = p.holding.find(positionKey(after(board, c.uci).getFen())); it != p.holding.end()) {
            c.holding = it->second.first;
            c.replies = it->second.second;
        }
    auto choice = choosePractical(candidates, level ? *level : none, stm);

    // The engine of the practical moves: one thread, what it searches given once the last search ended.
    const auto* entry = analysisEngine();
    if (entry && (!p.engine || p.engine_name != entry->name)) {
        if (p.engine) p.engine->stop();
        p.engine = std::make_unique<Analyser>();
        AnalysisSettings settings;
        settings.engine  = entry->name;
        settings.threads = 1;
        settings.hash    = 64;
        settings.multipv = 1;
        p.engine->start(*entry, settings);
        p.engine_name = entry->name;
        p.task.clear();
    }
    if (p.engine) {
        p.engine->poll();
        if (!p.task.empty() && p.engine->finished()) {
            const auto searched = p.engine->position();
            const auto key      = positionKey(searched.fen);
            if (p.task.rfind("replies|", 0) == 0) {
                chess::Board there;
                there.setFen(searched.fen);
                chess::Movelist legal;
                chess::movegen::legalmoves(legal, there);
                p.holding[key] = {holdingReplies(p.engine->lines()), static_cast<int>(legal.size())};
            } else if (!p.engine->lines().empty() && p.engine->lines().front().score &&
                       !p.engine->lines().front().pv.empty()) {
                p.judged[key] = {p.engine->lines().front().pv.front(), scoreCp(*p.engine->lines().front().score)};
            }
            p.task.clear();
        }
    }
    const auto search = [&](const chess::Board& at, const char* kind, int multipv) {
        if (!p.engine || !p.task.empty() || p.engine->state() == Analyser::State::Failed) return;
        auto settings    = p.engine->settings();
        settings.multipv = multipv;
        p.engine->update(settings);
        AnalysisPosition pos;
        pos.fen = at.getFen();
        p.engine->analyse(pos, SearchLimit{0, 0, PRACTICAL_NODES}, true);
        p.task = fmt::format("{}|{}", kind, pos.fen);
    };
    // Outside known games: the opponent's replies after each candidate, one candidate at a time.
    const bool outside = level && outsideKnownGames(candidates) && candidates.size() > 1;
    if (outside && settled && lines.front().depth >= PRACTICAL_SETTLED_DEPTH) {
        for (const auto& c : candidates) {
            if (c.holding) continue;
            const auto there = after(board, c.uci);
            chess::Movelist legal;
            chess::movegen::legalmoves(legal, there);
            if (legal.empty()) continue;
            search(there, "replies", std::min(64, static_cast<int>(legal.size())));
            break;
        }
    }

    // The level's most played reply along the practical move's line, and what punishes it (when the figures say so):
    // at the first turn of the opponent where it is not the engine's.
    struct Said {
        std::string prefix;  // the line's moves before it, numbered
        std::string reply;   // numbered, with its mark
        double pct       = 0;
        const char* side = "";  // whose reply
        std::string answer;     // numbered
    };
    std::optional<Said> said;
    const auto& chosen = candidates[choice.index];
    // Asked once the engine has gone deep enough for its choice to hold: a choice of the first plies costs requests.
    if (choice.way == PracticalWay::Games && settled && lines.front().depth >= PRACTICAL_SETTLED_DEPTH) {
        auto at = after(board, chosen.uci);
        std::string prefix;
        for (std::size_t turn = 0; turn < 2; ++turn) {
            const auto* replies = practicalLevelAt(at, true);
            if (!replies) break;
            const auto top = std::find_if(replies->begin(), replies->end(),
                                          [](const LevelMove& m) { return m.games >= PRACTICAL_GAMES; });
            if (top == replies->end()) break;
            const std::size_t k = 1 + 2 * turn;
            const auto expected = k < chosen.pv.size() ? chosen.pv[k] : std::string();
            if (top->uci != expected) {
                const auto pct = moverScore(*top, at.sideToMove());
                const auto bad = after(at, top->uci);
                const auto it  = p.judged.find(positionKey(bad.getFen()));
                if (it == p.judged.end()) {
                    search(bad, "judge", 1);
                } else if (pct && *pct <= 50.0) {
                    // What the reply costs the side that plays it: the engine's score for the practical move's side
                    // after it, against the line's.
                    const int cost = it->second.score_cp - scoreCp(chosen.score);
                    if (cost >= 50) {
                        Said s;
                        s.prefix = prefix;
                        s.reply  = numberedMove(at, top->uci) + replyMark(cost);
                        s.pct    = *pct;
                        s.side   = at.sideToMove() == chess::Color::WHITE ? TR("White") : TR("Black");
                        s.answer = numberedMove(bad, it->second.best);
                        said     = std::move(s);
                    }
                }
                break;
            }
            // The level plays the engine's reply: the line goes on, our next move the engine's.
            if (k + 1 >= chosen.pv.size()) break;
            prefix += (prefix.empty() ? "" : " ") + numberedMove(at, top->uci);
            at = after(at, top->uci);
            prefix += " " + numberedMove(at, chosen.pv[k + 1]);
            at = after(at, chosen.pv[k + 1]);
        }
    }

    // The table: each candidate, the engine's score, the score against the level, its games there.
    const int depth = static_cast<int>(lines.front().depth);
    if (ImGui::BeginTable("##practical", 4, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerH)) {
        ImGui::TableSetupColumn(TR("Move"), ImGuiTableColumnFlags_WidthStretch, 1.6f);
        ImGui::TableSetupColumn(trf("{} · depth {}", b.analyser->engine(), depth).c_str(),
                                ImGuiTableColumnFlags_WidthStretch, 1.3f);
        ImGui::TableSetupColumn(trf("Against {}", s.practical_level).c_str(), ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn(outside ? TR("Replies that hold") : TR("Games at this level"),
                                ImGuiTableColumnFlags_WidthStretch, 1.2f);
        ImGui::TableHeadersRow();
        for (std::size_t i = 0; i < candidates.size(); ++i) {
            const auto& c = candidates[i];
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(numberedMove(board, c.uci).c_str());
            if (i == 0) badge(TR("best"), false);
            if (i == choice.index) badge(TR("practical"), true);
            ImGui::TableNextColumn();
            cellText(ui::formatScore(whiteScore(c.score, stm)));
            ImGui::TableNextColumn();
            if (c.expected)
                cellText(fmt::format("{:.0f} %", *c.expected));
            else
                cellText("–", true);
            if (!c.expected && ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", trf("Fewer than {} games at this level", PRACTICAL_GAMES).c_str());
            ImGui::TableNextColumn();
            if (outside)
                cellText(c.holding ? trf("{} of {}", *c.holding, c.replies) : std::string("…"), !c.holding);
            else
                cellText(level ? ui::exactCount(c.games) : std::string("…"), c.games < PRACTICAL_GAMES);
        }
        ImGui::EndTable();
    }

    // The sentence, held to the figures.
    std::string sentence;
    const auto move = numberedMove(board, chosen.uci);
    switch (choice.way) {
        case PracticalWay::Games:
            sentence = choice.index == 0
                           ? trf("The practical move is the engine's best too: {}, {:.0f} % expected at this level.",
                                 move, *chosen.expected)
                           : trf("The practical move: {} — {:.2f} from the engine's best, and {:.0f} % expected at "
                                 "this level.",
                                 move, chosen.loss_cp / 100.0, *chosen.expected);
            if (said)
                sentence += " " + (said->prefix.empty()
                                       ? trf("At {}, the most played reply is {} ({:.0f} % for {}), which {} punishes.",
                                             s.practical_level, said->reply, said->pct, said->side, said->answer)
                                       : trf("After {}, the most played reply at {} is {} ({:.0f} % for {}), which {} "
                                             "punishes.",
                                             said->prefix, s.practical_level, said->reply, said->pct, said->side,
                                             said->answer));
            break;
        case PracticalWay::Replies:
            sentence =
                trn("The practical move: {} — {} reply of {} holds, by the engine; no games known at this level.",
                    "The practical move: {} — {} replies of {} hold, by the engine; no games known at this "
                    "level.",
                    static_cast<uint64_t>(*chosen.holding), move, *chosen.holding, chosen.replies);
            break;
        case PracticalWay::Engine:
            if (candidates.size() == 1)
                sentence = trf("The only move within 0.3 pawn of the engine's best: {}.", move);
            else if (!level)
                sentence = online ? TR("Asking Lichess's explorer for the games of this level…")
                                  : TR("Counting the games of this level in your databases…");
            else
                sentence =
                    TR("No games known at this level: the engine counts the opponent's good replies after each "
                       "move…");
            break;
    }
    if (!p.error.empty()) sentence += " " + trf("(Lichess's explorer gave nothing: {})", p.error);
    ImGui::Spacing();
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::ACCENT), "%s", sentence.c_str());
    ImGui::TextDisabled("%s", TR("Among the moves the engine puts within 0.3 pawn of its best (or that keep the "
                                 "tables' verdict), the one that scores best against a player of this level, in the "
                                 "games of this level, from 200 games; outside known games, the one that leaves the "
                                 "opponent the fewest good replies, by the engine. Never a losing move."));
    ImGui::PopTextWrapPos();
    ui::endCard();
    // The arrows: the practical move in the accent's blue, the engine's best in grey (drawBoardColumn()).
    practical_arrows_ = PracticalArrows{board.hash(), ImGui::GetFrameCount(), candidates.front().uci, chosen.uci};
}

}  // namespace fcg
