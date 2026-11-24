// The Chess space's Questions tab (mockup M5, boards 1 and 2): questions about the position, answered by the local
// assistant from what the engine, the databases and the tables establish; and, the first time, the card that offers its
// model. Nothing leaves the machine but the model's download, once.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>
#include <variant>
#include <vector>

#include <fmt/include/fmt/format.h>
#include <imgui.h>

#include <commentary/position.hpp>
#include <commentary/question.hpp>
#include <study/game_tree.hpp>
#include <study/import.hpp>

#include "app.hpp"
#include "assistant_notes.hpp"
#include "assistant_service.hpp"
#include "board_text.hpp"
#include "engine_oracle.hpp"
#include "i18n.hpp"
#include "machines.hpp"
#include "ui.hpp"

namespace fcg {

namespace {

float px(float v) { return v * ImGui::GetFontSize() / 16.0f; }

bool french() { return i18n::language() == i18n::Language::French; }

// A score from White's side: "+0.62", "#3", "-#2".
std::string whiteScore(const commentary::Score& s) {
    if (s.kind == commentary::Score::Kind::Mate)
        return s.value > 0 ? fmt::format("#{}", s.value) : fmt::format("-#{}", -s.value);
    return fmt::format("{:+.2f}", s.value / 100.0);
}

// The table under two moves compared (M5, board 2): for each, the engine's score after it, White's; its games in the
// databases and how they scored for the side that played it; and the line that follows.
void drawComparison(const commentary::Position& from, const commentary::Answered& ans,
                    const std::optional<commentary::References>& refs, const std::string& engine,
                    const std::vector<commentary::ReferenceBase>& bases, commentary::PieceLetters letters) {
    const std::string base = bases.size() == 1 ? bases.front().name : std::string(TR("Your databases"));
    ImGui::PushTextWrapPos(-1.0f);  // the card wraps its text: not the table's cells, sized by what they hold
    if (!ImGui::BeginTable("##compare", 4, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersInnerH)) {
        ImGui::PopTextWrapPos();
        return;
    }
    ImGui::TableSetupColumn(TR("Move"));
    ImGui::TableSetupColumn(engine.empty() ? TR("Engine") : engine.c_str());
    ImGui::TableSetupColumn(base.c_str());
    ImGui::TableSetupColumn(TR("The line"), ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableHeadersRow();
    for (std::size_t i = 0; i < ans.moves.size() && i < ans.lines.size(); ++i) {
        const auto& mv = ans.moves[i];
        auto after     = from;
        after.playUnchecked(mv);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(commentary::moveText(from, mv, letters).c_str());
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(whiteScore(ans.lines[i].score).c_str());
        ImGui::TableNextColumn();
        std::string games = "–";
        if (refs) {
            games = TR("no game");
            for (const auto& p : refs->moves) {
                if (!(p.move == mv) || p.stats.games == 0) continue;
                const auto& st     = p.stats;
                const auto decided = st.white + st.draws + st.black;
                const auto won     = from.turn() == commentary::Color::White ? st.white : st.black;
                const auto counted = ui::exactCount(st.games);
                games = decided == 0
                            ? trn("{} game", "{} games", st.games, counted)
                            : trn("{} game, {} %", "{} games, {} %", st.games, counted,
                                  static_cast<int>(std::lround(
                                      100.0 * (static_cast<double>(won) + 0.5 * static_cast<double>(st.draws)) /
                                      static_cast<double>(decided))));
            }
        }
        ImGui::TextUnformatted(games.c_str());
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(commentary::lineText(after, ans.lines[i].moves, 4, letters).c_str());
    }
    ImGui::EndTable();
    ImGui::PopTextWrapPos();
}

// A move with its number, as the notation writes it: "14.Nd5", "14…Bxd5"; in the interface's letters, or as `san` is.
std::string numberedSan(const chess::Board& before, const std::string& san, bool local = true) {
    const auto n     = before.fullMoveNumber();
    const auto shown = local ? localSan(san) : san;
    return before.sideToMove() == chess::Color::WHITE ? fmt::format("{}.{}", n, shown) : fmt::format("{}…{}", n, shown);
}

}  // namespace

void App::drawAssistantFirstUse() {
    auto& a = *assistant_;
    ui::beginCard("assistant_first_use");
    ImGui::PushTextWrapPos();
    const auto size = formatBytes(a.recommended().size);
    if (a.downloading()) {
        // The download (M5, board 1): how far, how fast, how long still; Cancel. The program stays the user's.
        const auto total = a.recommended().size;
        const auto done  = a.downloaded();
        ImGui::TextUnformatted(TR("Downloading the model"));
        std::string line   = trf("{} of {}", formatBytes(done), size);
        const double speed = a.downloadSpeed();
        if (speed > 1) {
            line += fmt::format(" · {}/s", formatBytes(static_cast<uint64_t>(speed)));
            if (total > done) {
                const auto left = static_cast<int>(static_cast<double>(total - done) / speed);
                line += " · " + (left >= 60 ? trf("{} min {} s left", left / 60, left % 60) : trf("{} s left", left));
            }
        }
        ImGui::TextDisabled("%s", line.c_str());
        ImGui::ProgressBar(total ? static_cast<float>(done) / static_cast<float>(total) : 0.0f, ImVec2(-1, 0), "");
        if (ImGui::Button(TR("Cancel"))) a.cancelDownload();
        ImGui::TextDisabled("%s", TR("The program stays yours meanwhile; closed, it will resume the download where it "
                                     "was."));
    } else if (a.settings().not_now) {
        // « Not now » said: offered again here, quietly.
        ImGui::TextUnformatted(TR("The assistant needs its model, downloaded once, to answer questions."));
        if (ui::iconButton("assistant_download", ui::Icon::Download, trf("Download the model ({})", size).c_str()))
            a.download();
        ImGui::SameLine();
        if (ImGui::Button(TR("A GGUF file on the disk…")))
            dialogs_.openFile(Dialogs::Purpose::AssistantModel, {{"GGUF", "gguf"}});
    } else {
        ImGui::TextUnformatted(
            TR("An assistant that comments on your games and answers your questions, on your machine"));
        ImGui::TextDisabled("%s", TR("It relies on what your engine, your databases and the endgame tables establish, "
                                     "and says it in words; when nothing establishes it, it says so too. In English or "
                                     "in French."));
        if (ImGui::BeginTable("##assistant_facts", 2)) {
            // The labels as wide as the widest, the text wrapped in what is left.
            float label_w = 0;
            for (const char* l : {TR("The model"), TR("On this machine"), TR("Disk and memory")})
                label_w = std::max(label_w, ImGui::CalcTextSize(l).x);
            ImGui::TableSetupColumn("##label", ImGuiTableColumnFlags_WidthFixed, label_w + px(4));
            ImGui::TableSetupColumn("##text", ImGuiTableColumnFlags_WidthStretch);
            const auto row = [](const char* label, const std::string& text) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", label);
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(text.c_str());
            };
            row(TR("The model"), trf("RookStudio commentary v11 · {}, downloaded once", size));
            row(TR("On this machine"), TR("on the GPU when there is one to use, else on the processor: a comment in "
                                          "about a quarter of a second on a GPU, 3 seconds on a processor"));
            row(TR("Disk and memory"), trf("{} on the disk · about 3 GB of memory while it serves", size));
            ImGui::EndTable();
        }
        ImGui::TextUnformatted(TR("What stays on the machine"));
        ImGui::TextDisabled("%s", TR("Everything: your games, your questions, its answers. Only the model's download "
                                     "goes through the Internet, once, from huggingface.co, checked when it arrives; "
                                     "it resumes where it was after a break."));
        if (ui::iconButton("assistant_download", ui::Icon::Download, trf("Download the model ({})", size).c_str()))
            a.download();
        ImGui::SameLine();
        if (ImGui::Button(TR("A GGUF file on the disk…")))
            dialogs_.openFile(Dialogs::Purpose::AssistantModel, {{"GGUF", "gguf"}});
        ImGui::SameLine();
        if (ImGui::Button(TR("Not now"))) {
            a.settings().not_now = true;
            a.saveSettings();
        }
        ImGui::TextDisabled("%s", TR("Without it, everything else works: the analysis, the review's figures, the "
                                     "template comments, the tree."));
    }
    if (!a.downloadError().empty())
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s",
                           trf("The download stopped: {}. It resumes from there.", a.downloadError()).c_str());
    ImGui::PopTextWrapPos();
    ui::endCard();
}

void App::askQuestion(const std::string& text, const std::string& asked_text) {
    const auto& b = board_;
    if (text.empty() || !assistant_ || gameLocked()) return;  // fair play (#48)
    // The engine that answers: the analysis's, kept while it is the same.
    const auto* engine = analysisEngine();
    if (!engine)
        oracle_.reset();
    else if (!oracle_ || oracle_->engine() != engine->name)
        oracle_ = std::make_shared<EngineOracle>(*engine);
    QuestionAsked q;
    q.question = text;
    q.fen      = boardAt(b.game, b.chess960, b.ply).getFen();
    q.line     = b.line;
    q.ply      = b.ply;
    q.engine   = oracle_ ? oracle_->engine() : std::string();
    // What the answer is made from, owned by the thread that writes it: the position, the one before and the moves
    // that led to them, the moves from the usual start (the opening's name), the databases.
    std::vector<std::string> line;
    if (!b.chess960 && b.game.start_fen == std::string(chess::constants::STARTPOS))
        for (int i = 0; i < b.ply && i < static_cast<int>(b.game.moves.size()); ++i)
            line.push_back(chess::uci::moveToUci(b.game.moves[static_cast<std::size_t>(i)].move, false));
    std::string earlier_fen, previous, earlier_previous;
    if (b.ply >= 1) {
        earlier_fen = boardAt(b.game, b.chess960, b.ply - 1).getFen();
        previous    = chess::uci::moveToUci(b.game.moves[static_cast<std::size_t>(b.ply - 1)].move, b.chess960);
        if (b.ply >= 2)
            earlier_previous =
                chess::uci::moveToUci(b.game.moves[static_cast<std::size_t>(b.ply - 2)].move, b.chess960);
    }
    auto& s = shelf();
    for (std::size_t i = 0; i < s.entries().size(); ++i)
        if (!s.entries()[i].source)
            if (auto source = s.treeSource(i)) q.bases.push_back({source->name, source->snapshot, source->tree});
    const auto lang = french() ? commentary::Lang::Fr : commentary::Lang::En;
    q.answer =
        std::async(std::launch::async,
                   [question = asked_text.empty() ? text : asked_text, fen = q.fen, earlier_fen, previous,
                    earlier_previous, line = std::move(line), bases = q.bases, lang, model = assistant_.get(),
                    oracle = oracle_]() -> QuestionResult {
                       QuestionResult r;
                       const auto pos = commentary::Position::fromFen(fen);
                       if (!pos) {
                           r.error = "the position cannot be read";
                           return r;
                       }
                       std::optional<commentary::Position> earlier;
                       if (!earlier_fen.empty()) earlier = commentary::Position::fromFen(earlier_fen);
                       commentary::Asked asked;
                       asked.question = question;
                       asked.before   = &*pos;
                       if (earlier) {
                           asked.earlier  = &*earlier;
                           asked.previous = earlier->parseUci(previous);
                       }
                       asked.lang    = lang;
                       asked.letters = lang == commentary::Lang::Fr ? commentary::PieceLetters::French
                                                                    : commentary::PieceLetters::English;
                       asked.bases   = bases;
                       std::vector<commentary::Move> moves;
                       auto at = commentary::Position::initial();
                       for (const auto& u : line) {
                           const auto m = at.parseUci(u);
                           if (!m) {
                               moves.clear();
                               break;
                           }
                           moves.push_back(*m);
                           at.playUnchecked(*m);
                       }
                       // The move that led to the position before, read in the position two moves back, which `line`
                       // reaches when the game starts from the usual position.
                       if (!earlier_previous.empty() && moves.size() >= 2) {
                           auto two_back = commentary::Position::initial();
                           for (std::size_t i = 0; i + 2 < moves.size(); ++i) two_back.playUnchecked(moves[i]);
                           asked.earlier_previous = two_back.parseUci(earlier_previous);
                       }
                       asked.line = moves;
                       r.answer   = commentary::answer(asked, *model, oracle.get(), nullptr);
                       // Two moves compared: the position they are played from, and what the databases hold of it,
                       // for the table under the answer.
                       if (const auto* a = std::get_if<commentary::Answered>(&r.answer);
                           a && a->kind == commentary::Action::Kind::Compare) {
                           r.from = a->earlier && earlier ? *earlier : *pos;
                           if (!bases.empty()) r.refs = commentary::references(*r.from, bases, 0);
                       }
                       if (const auto* p = std::get_if<commentary::Problem>(&r.answer);
                           p && *p == commentary::Problem::NoEngine && oracle && !oracle->error().empty())
                           r.error = oracle->error();
                       return r;
                   });
    questions_.push_back(std::move(q));
}

void App::pollQuestions() {
    if (!assistant_) return;
    assistant_->setGpuTaken(gpuTournament());
    assistant_->poll();
    pollBoardComments();
    pollStory();
    for (auto& q : questions_)
        if (q.answer.valid() && q.answer.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
            q.done = q.answer.get();
    std::erase_if(questions_left_,
                  [](auto& f) { return f.wait_for(std::chrono::seconds(0)) == std::future_status::ready; });
}

void App::addAssistantComment(const std::string& text, int line, int ply, const std::string& fen,
                              const std::vector<std::string>& basis) {
    auto& b = board_;
    if (ply <= 0 || text.empty()) return;
    // The move it was asked after, if the game still has it there.
    if (line >= static_cast<int>(b.tree.variations.size())) return;
    const auto game = lineOf(b.tree, line);
    if (ply > static_cast<int>(game.moves.size()) || boardAt(game, b.chess960, ply).getFen() != fen) {
        boardToast(TR("The game has changed since the question: the answer has no move to go on."));
        return;
    }
    auto before      = boardSnapshot();
    MoveNotes* notes = notesAt(b.tree, line, ply);
    if (!notes) return;
    if (notes->after.find(text) != std::string::npos || notes->assistant.find(text) != std::string::npos) {
        boardToast(TR("The answer is in the notation already"));
        return;
    }
    // The assistant's comment, beside the one the move may have, which stays as it is (#60); after its earlier one.
    if (!notes->assistant.empty()) notes->assistant += ' ';
    notes->assistant += text;
    if (!basis.empty()) assistant_basis_[notes->assistant] = basis;
    boardChanged(std::move(before));
    b.game = lineOf(b.tree, b.line);
    boardToast(TR("Answer added to the notation · Ctrl+Z takes it back"));
}

void App::drawBoardQuestions() {
    // The questions are answered with an engine: without one, the card that offers one (M9, board 2).
    if (engines_.empty()) {
        drawFirstEngineCard(TR("The assistant answers with an engine's analysis: the questions need one."));
        return;
    }
    auto& a = *assistant_;
    // The first use, where it is asked, never at start (M5, board 1).
    if (!a.installed() || a.downloading()) {
        drawAssistantFirstUse();
        return;
    }
    auto& b         = board_;
    const bool busy = std::any_of(questions_.begin(), questions_.end(), [](const auto& q) { return !q.done; });

    // The question, and those the program suggests (read without the model when they name no move).
    const float ask_w = ImGui::CalcTextSize(TR("Ask")).x + ImGui::GetStyle().FramePadding.x * 2 + px(8);
    ImGui::SetNextItemWidth(std::max(px(160), ImGui::GetContentRegionAvail().x - ask_w));
    if (question_focus_) {
        ImGui::SetKeyboardFocusHere();
        question_focus_ = false;
    }
    const bool enter =
        ImGui::InputTextWithHint("##question", TR("Your question about the position or the game"),
                                 question_text_.data(), question_text_.size(), ImGuiInputTextFlags_EnterReturnsTrue);
    if (enter) ImGui::SetKeyboardFocusHere(-1);  // the field stays there for the next question
    ImGui::SameLine();
    ImGui::BeginDisabled(busy || !question_text_[0]);
    const bool ask = ImGui::Button(TR("Ask"));
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", TR("Enter"));
    if ((ask || enter) && question_text_[0] && !busy) {
        askQuestion(question_text_.data());
        question_text_.fill(0);
    }
    // Each suggestion as shown, and as asked: in French for a French interface, else in English.
    std::vector<std::pair<std::string, std::string>> chips;
    const bool asked_french = french();
    if (b.ply >= 1 && b.ply <= static_cast<int>(b.game.moves.size())) {
        // Shown in the interface's letters (#66), asked in the assistant's: French ones, else English.
        const auto before = boardAt(b.game, b.chess960, b.ply - 1);
        const auto& san   = b.game.moves[static_cast<std::size_t>(b.ply - 1)].san;
        chips.emplace_back(trf("Why {}?", numberedSan(before, san)),
                           asked_french ? std::string() : fmt::format("Why {}?", numberedSan(before, san, false)));
    }
    for (const char* c : {N_("What is the threat?"), N_("What is the best move?"), N_("Who played this position?"),
                          N_("What is this opening?")})
        chips.emplace_back(TR(c), asked_french ? std::string() : std::string(c));
    ImGui::BeginDisabled(busy);
    for (std::size_t i = 0; i < chips.size(); ++i) {
        const float w = ImGui::CalcTextSize(chips[i].first.c_str()).x + ImGui::GetStyle().FramePadding.x * 2;
        if (i > 0) ui::sameLineOrWrap(w, px(4));
        if (ImGui::SmallButton(fmt::format("{}##chip{}", chips[i].first, i).c_str()))
            askQuestion(chips[i].first, chips[i].second);
    }
    ImGui::EndDisabled();
    ImGui::Dummy(ImVec2(0, px(4)));

    // The questions and their answers, the latest first, for this game.
    std::optional<std::size_t> remove;
    for (std::size_t n = questions_.size(); n-- > 0;) {
        auto& q = questions_[n];
        ImGui::PushID(static_cast<int>(n));
        ui::beginCard("question");
        ImGui::PushTextWrapPos();
        ImGui::TextDisabled("%s", trf("You: {}", q.question).c_str());
        const bool here = q.fen == boardFen();
        if (!q.done) {
            ImGui::TextUnformatted(a.state() == AssistantService::State::Loading ? TR("The model is loading…")
                                                                                 : TR("The assistant is thinking…"));
        } else if (!q.done->error.empty()) {
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s", q.done->error.c_str());
        } else if (const auto* ans = std::get_if<commentary::Answered>(&q.done->answer)) {
            ImGui::TextUnformatted(ans->text.c_str());
            if (ans->kind == commentary::Action::Kind::Compare && q.done->from)
                drawComparison(*q.done->from, *ans, q.done->refs, q.engine, q.bases,
                               french() ? commentary::PieceLetters::French : commentary::PieceLetters::English);
            // What it rests on (M5, board 2).
            using K = commentary::Action::Kind;
            std::vector<std::string> sources;
            switch (ans->kind) {
                case K::Games:
                case K::Played: {
                    std::string names;
                    for (const auto& base : q.bases) names += (names.empty() ? "" : ", ") + base.name;
                    sources.push_back(trf("{}: the search of the position, the tree for the scores", names));
                    break;
                }
                case K::Opening:
                    sources.emplace_back(TR("Lichess's opening names (public domain), the line played"));
                    break;
                case K::Tablebase:
                    sources.emplace_back(TR("the endgame tables"));
                    break;
                case K::Threat:
                    sources.push_back(trf("the threat computed by {}, the side to move passing its turn", q.engine));
                    break;
                default:
                    sources.push_back(trf("{}, 300,000 nodes per position", q.engine));
                    sources.emplace_back(TR("the facts of the positions its lines lead to"));
                    break;
            }
            ImGui::TextDisabled("%s", TR("What the answer rests on"));
            for (const auto& s : sources) ImGui::TextDisabled("· %s", s.c_str());
            // What can be done with it, on one line.
            bool any          = false;
            const auto action = [&](const std::string& label) {
                if (any) ImGui::SameLine();
                any = true;
                return ImGui::SmallButton(label.c_str());
            };
            if (ans->best && here) {
                const auto board = boardAt(b.game, b.chess960, b.ply);
                const auto m     = chess::uci::uciToMove(board, commentary::uci(*ans->best));
                if (m != chess::Move::NO_MOVE && action(trf("Play {}", localSan(chess::uci::moveToSan(board, m)))))
                    boardPlayAsked(m);
            }
            if (ans->kind == K::Try && !ans->moves.empty() && here && action(TR("Show the line in preview"))) {
                BoardPreview preview;
                preview.ply = ans->earlier ? std::max(0, b.ply - 1) : b.ply;
                preview.at  = b.ply;
                for (const auto& m : ans->moves) preview.moves.push_back(commentary::uci(m));
                preview.label  = TR("The move tried");
                board_.preview = std::move(preview);
            }
            if (ans->kind == K::Games && !ans->games.empty()) {
                const auto& g = ans->games.front();
                if (action(TR("Open the strongest")) && g.base < q.bases.size())
                    readOnBoard(study::importedPgn(q.bases[g.base].snapshot->game(g.id)),
                                fmt::format("{} – {}", g.white, g.black));
                if (action(TR("The reference games"))) board_side_tab_ = 3;
            }
            if (q.ply > 0 && ans->kind != K::Opening && action(TR("Add to the notation")))
                addAssistantComment(ans->text, q.line, q.ply, q.fen, sources);
            if (action(TR("Remove"))) remove = n;
        } else if (const auto* problem = std::get_if<commentary::Problem>(&q.done->answer)) {
            using P         = commentary::Problem;
            const char* why = *problem == P::UnknownMove  ? TR("The question names no move of this position.")
                              : *problem == P::Unanalyzed ? TR("This needs the game's review first.")
                              : *problem == P::InCheck ? TR("The side to move is in check: no threat beyond the check.")
                              : *problem == P::NoEngine
                                  ? (q.engine.empty() ? TR("No engine to answer: choose one in the Analysis tab.")
                                                      : TR("The engine gave no answer, or the game is over."))
                              : *problem == P::NoDatabase ? TR("No database to look in: import games first.")
                                                          : TR("No endgame table knows this position.");
            ImGui::TextUnformatted(TR("I cannot tell."));
            ImGui::TextDisabled("%s", why);
        }
        if (q.done && !std::holds_alternative<commentary::Answered>(q.done->answer) && ImGui::SmallButton(TR("Remove")))
            remove = n;
        ImGui::PopTextWrapPos();
        ui::endCard();
        ImGui::PopID();
    }
    if (remove) questions_.erase(questions_.begin() + static_cast<std::ptrdiff_t>(*remove));
    if (questions_.empty()) {
        ImGui::PushTextWrapPos();
        ImGui::TextDisabled("%s", TR("What it can do: explain a move, the best move, the threat, try a move, compare "
                                     "two; the reference games and the moves played in a database; the opening's "
                                     "name; what the endgame tables say. A question beyond them gets an “I cannot "
                                     "tell” and what it can show instead."));
        ImGui::TextDisabled("%s", TR("Why believe it: it speaks only of what your engine, your databases and the "
                                     "tables establish, shown under the answer, and names only legal moves."));
        ImGui::PopTextWrapPos();
    }
}

}  // namespace fcg

namespace fcg {

std::string App::gpuTournament() const {
    for (const auto& r : runs_) {
        const auto* t = r.run->tournament();
        if (!r.run->running() || !t) continue;
        const bool gpu = r.run->remote() ? r.run->inFlight().gpus > 0
                                         : std::any_of(t->spec().participants.begin(), t->spec().participants.end(),
                                                       [](const auto& p) { return p.gpu; });
        if (gpu) return t->spec().name;
    }
    return {};
}

std::string App::assistantStatus() const {
    if (!assistant_) return {};
    const auto& a = *assistant_;
    using S       = AssistantService::State;
    switch (a.state()) {
        case S::Downloading: {
            const auto total = std::max<uint64_t>(1, a.recommended().size);
            return trf("Downloading the assistant's model: {} %", a.downloaded() * 100 / total);
        }
        case S::Loading:
            return TR("The local assistant: loading its model");
        case S::Ready:
            return a.onGpu() ? TR("The local assistant: ready (GPU)") : TR("The local assistant: ready (processor)");
        default:
            return {};
    }
}

void App::drawAssistantSettings(float label) {
    if (!assistant_) return;
    auto& a          = *assistant_;
    bool changed     = false;
    using S          = AssistantService::State;
    const auto state = a.state();

    ui::label(TR("Assistant"), label);
    ImGui::BeginGroup();
    ImGui::PushTextWrapPos();
    // Where it stands, and the gesture that goes with it (M5, board 5).
    switch (state) {
        case S::NoModel:
            ImGui::TextUnformatted(TR("No model on this machine: the Questions tab of the Board page offers it."));
            break;
        case S::Downloading:
            ImGui::TextUnformatted(assistantStatus().c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton(TR("Cancel"))) a.cancelDownload();
            break;
        case S::Unloaded:
            ImGui::TextUnformatted(TR("Not loaded: the next question or comment loads it, in a few seconds."));
            break;
        case S::Loading:
            ImGui::TextUnformatted(TR("Loading its model…"));
            break;
        case S::Ready:
            ImGui::TextUnformatted(trf("Ready · {}", a.devices()).c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton(TR("Unload"))) a.unload();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", TR("Its process ends and gives its memory back; the next question loads it "
                                           "again."));
            break;
        case S::Failed:
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s",
                               trf("The model could not be loaded: {}", a.failure()).c_str());
            break;
    }
    if (a.gpuFailed()) {
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s",
                           TR("The graphics driver failed the model: it runs on the processor since. The details are "
                              "in the log."));
        if (ImGui::SmallButton(TR("Try the GPU again"))) a.retryGpu();
    }
    if (!a.gpuTaken().empty() && a.settings().gpu) {
        ImGui::TextDisabled("%s", trf("The tournament “{}” uses the GPU: the assistant answers on the processor "
                                      "meanwhile, in about 3 seconds.",
                                      a.gpuTaken())
                                      .c_str());
        if (ImGui::SmallButton(TR("Take the GPU anyway"))) a.takeGpuAnyway();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", TR("An engine that computes on the GPU keeps the priority: sharing it would skew "
                                       "its results."));
    }
    ImGui::PopTextWrapPos();
    ImGui::EndGroup();

    // Its model: the catalogue's, or a GGUF file of the disk.
    ui::label(TR("Its model"), label);
    ImGui::BeginGroup();
    ImGui::PushTextWrapPos();
    if (a.settings().model_file.empty()) {
        ImGui::TextUnformatted(trf("RookStudio commentary v11 · {}{}", formatBytes(a.recommended().size),
                                   a.installed() ? std::string() : std::string(" · ") + TR("not downloaded"))
                                   .c_str());
        if (!a.installed() && !a.downloading()) {
            ImGui::SameLine();
            if (ImGui::SmallButton(a.partlyDownloaded() ? TR("Resume the download") : TR("Download"))) a.download();
        }
    } else {
        ImGui::TextUnformatted(a.settings().model_file.c_str());
        if (!a.knowsComments())
            ImGui::TextDisabled("%s", TR("It answers the questions, but it has not learnt RookStudio's comments: "
                                         "those stay the templates'."));
        if (ImGui::SmallButton(TR("Back to RookStudio's model"))) {
            a.unload();
            a.settings().model_file.clear();
            changed = true;
        }
        ImGui::SameLine();
    }
    if (ImGui::SmallButton(TR("A GGUF file on the disk…")))
        dialogs_.openFile(Dialogs::Purpose::AssistantModel, {{"GGUF", "gguf"}});
    ImGui::PopTextWrapPos();
    ImGui::EndGroup();

    // Where it runs, and for how long it stays.
    ui::label(TR("It runs on"), label);
    int where = a.settings().gpu ? 0 : 1;
    if (ImGui::RadioButton(TR("The GPU, when there is one to use"), &where, 0)) changed = true;
    ImGui::SameLine();
    if (ImGui::RadioButton(TR("The processor"), &where, 1)) changed = true;
    if (changed && a.settings().gpu != (where == 0)) {
        a.settings().gpu = where == 0;
        a.unload();  // loaded again where it now runs
    }
    ui::label(TR("Unloaded after"), label);
    int minutes = a.settings().unload_minutes;
    ImGui::SetNextItemWidth(px(110));
    if (ImGui::InputInt("##unload_minutes", &minutes, 5, 15)) {
        a.settings().unload_minutes = std::clamp(minutes, 0, 24 * 60);
        changed                     = true;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s", TR("minutes without serving; 0: kept until Unload"));
    if (changed) a.saveSettings();
}

}  // namespace fcg
