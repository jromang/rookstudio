// Theory studies (#77, mockup M11, board 2; theory_shelf.hpp): the Openings page's Theory part beside the
// repertoires (the studies, each with its numbers, shown in the tree or not, opened on the Board, taken out of the
// theory); in the Board page's Tree tab, a Theory column marking the moves the studies comment and "What your studies
// say" under the moves, each note with its source, where it is, its text and its line to open on the board; in the
// Notation tab, "Theory · N" above the moves, which leads to the Tree tab (Alt+3).

#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>

#include <fmt/include/fmt/format.h>
#include <imgui.h>

#include <study/import.hpp>
#include <study/theory_notes.hpp>

#include "app.hpp"
#include "board_text.hpp"
#include "guide.hpp"
#include "i18n.hpp"
#include "keyboard.hpp"
#include "lichess_studies.hpp"
#include "notation_view.hpp"
#include "ui.hpp"

namespace fcg {

namespace {

namespace fs = std::filesystem;

float px(float v) { return v * ImGui::GetFontSize() / 16.0f; }

int64_t nowSeconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

// A move as the notation writes it, in the interface's letters: "7…Qb6", "7.f4".
std::string moveLabel(int number, bool black, const std::string& san) {
    return fmt::format("{}{}{}", number, black ? "…" : ".", localSan(san));
}

// Moves from the one at `ply` (0 White's first move of move 1), their numbers as the notation writes them: "7…Qb6
// 8.Qd2 Qxb2".
std::string movesText(const std::vector<std::string>& sans, int ply) {
    std::string out;
    for (std::size_t i = 0; i < sans.size(); ++i, ++ply) {
        if (!out.empty()) out += ' ';
        const bool black = ply % 2 == 1;
        if (!black)
            out += fmt::format("{}.", ply / 2 + 1);
        else if (i == 0)
            out += fmt::format("{}…", ply / 2 + 1);
        out += localSan(sans[i]);
    }
    return out;
}

// The ply of a note's move, counted as movesText() counts them.
int plyOf(const study::TheoryNote& n) { return (n.number - 1) * 2 + (n.black ? 1 : 0); }

// "+0.12", "#3", "#-3": White's point of view.
std::string evalText(const fastchess::Score& score) {
    if (score.type == fastchess::ScoreType::MATE) return fmt::format("#{}", score.value);
    return fmt::format("{:+.2f}", static_cast<double>(score.value) / 100.0);
}

// The last moves of a note's line to the position it is found at, three at most: "6.f4 e6 7.Bg5".
std::string lineTail(const study::TheoryNote& n, bool on_move, std::size_t count = 3) {
    const std::size_t end = n.path.size() - (on_move && !n.path.empty() ? 1 : 0);
    if (end == 0) return {};
    const std::size_t from = end > count ? end - count : 0;
    // The note's own move is the path's last, at plyOf(n).
    const int first = plyOf(n) - static_cast<int>(n.path.size() - 1 - from);
    return movesText(std::vector<std::string>(n.path.begin() + static_cast<std::ptrdiff_t>(from),
                                              n.path.begin() + static_cast<std::ptrdiff_t>(end)),
                     first);
}

// Where a note is: its kind of source, its place in its line, a transposition, an evaluation.
std::string whereText(const TheoryFound& f) {
    const auto& n = *f.note;
    std::vector<std::string> parts;
    if (f.kind == TheorySource::Kind::Lichess) parts.emplace_back(TR("Lichess study"));
    if (f.kind == TheorySource::Kind::Repertoire) parts.emplace_back(TR("repertoire"));
    if (n.san.empty()) {
        parts.emplace_back(TR("before the first move"));
    } else if (f.on_move) {
        if (n.path.size() >= 2) {
            const auto before = n.path[n.path.size() - 2];
            const int ply     = plyOf(n) - 1;
            parts.push_back(trf("after {}, on {}", moveLabel(ply / 2 + 1, ply % 2 == 1, before),
                                moveLabel(n.number, n.black, n.san)));
        } else {
            parts.push_back(trf("on {}", moveLabel(n.number, n.black, n.san)));
        }
    } else {
        parts.push_back(trf("after {}", moveLabel(n.number, n.black, n.san)));
    }
    if (f.transposed) parts.push_back(trf("found by transposition ({})", lineTail(n, f.on_move)));
    if (n.eval && n.before.empty() && n.after.empty()) parts.emplace_back(TR("an evaluation"));
    std::string out;
    for (const auto& p : parts) out += (out.empty() ? "" : " · ") + p;
    return out;
}

// What a note says: the move and its symbols first (on a move, or about the position when the move has a symbol);
// its evaluation, then its comments.
std::string noteText(const TheoryFound& f) {
    const auto& n = *f.note;
    std::string out;
    const bool judged = std::any_of(n.nags.begin(), n.nags.end(), [](uint8_t nag) { return nag < 10; });
    if (!n.san.empty() && (f.on_move || judged)) out = moveLabel(n.number, n.black, n.san);
    for (const auto nag : n.nags) out += (out.empty() || nag >= 10 ? " " : "") + nagText(nag);
    if (n.eval) {
        const auto value = evalText(*n.eval);
        out += (out.empty() ? "" : " ") +
               (n.depth > 0 ? trf("({}, depth {})", value, n.depth) : fmt::format("({})", value));
    }
    for (const auto* comment : {&n.before, &n.after})
        if (!comment->empty()) out += (out.empty() ? "" : " ") + *comment;
    // Only marks: say how many.
    if (!n.hasText() && n.nags.empty()) {
        const auto arrows = static_cast<int>(n.arrows.size()), squares = static_cast<int>(n.squares.size());
        if (arrows > 0) out += (out.empty() ? "" : " ") + trn("{} arrow", "{} arrows", arrows, arrows);
        if (squares > 0) out += (out.empty() ? "" : ", ") + trn("{} square", "{} squares", squares, squares);
    }
    while (!out.empty() && out.front() == ' ') out.erase(out.begin());
    return out;
}

// The line a note opens: its game's name when it has one, then its move and the next ones, or the moves to the
// position it is about.
std::string lineLabel(const TheoryFound& f) {
    const auto& n = *f.note;
    std::string moves;
    if (n.san.empty()) {
        moves = TR("its start");
    } else if (f.on_move) {
        std::vector<std::string> sans{n.san};
        sans.insert(sans.end(), n.next.begin(), n.next.end());
        moves = movesText(sans, plyOf(n));
    } else {
        moves = lineTail(n, false);
    }
    auto game = f.game;
    if (game.empty() && f.games > 1) game = trf("game {}", n.game + 1);
    return game.empty() ? moves : fmt::format("{}: {}", game, moves);
}

// "PGN · read in place", "Lichess study · read on 18 November"…, for the list and the chip of a study.
std::string stateText(const TheorySource& s, const lichess::StudyRecord* record) {
    const char* kind = s.kind == TheorySource::Kind::Lichess ? TR("Lichess study") : "PGN";
    if (s.reading()) return trf("{} · reading, {} %", kind, static_cast<int>(100 * s.progress()));
    if (s.missing)
        return s.kind == TheorySource::Kind::Lichess ? trf("{} · not read on this machine", kind)
                                                     : trf("{} · not found", kind);
    if (!s.error.empty()) return trf("{} · cannot be read", kind);
    if (s.kind == TheorySource::Kind::Lichess && record && record->read_at > 0)
        return trf("{} · read {}", kind, ui::dayText(static_cast<std::time_t>(record->read_at)));
    if (s.readings > 1) {
        const auto ago  = std::max<int64_t>(0, nowSeconds() - s.read_at);
        const auto when = ago < 60     ? std::string(TR("just now"))
                          : ago < 3600 ? trn("{} min ago", "{} min ago", static_cast<int>(ago / 60), ago / 60)
                                       : trn("{} h ago", "{} h ago", static_cast<int>(ago / 3600), ago / 3600);
        return trf("{} · changed on the disk, read again {}", kind, when);
    }
    return trf("{} · read in place", kind);
}

// The last folders of a path and its name: "Documents › Chess › Najdorf.pgn".
std::string shortPath(const fs::path& file) {
    std::vector<std::string> parts;
    for (auto p = file; !p.empty() && parts.size() < 3; p = p.parent_path()) {
        if (p.filename().empty()) break;
        parts.insert(parts.begin(), p.filename().string());
        if (p == p.parent_path()) break;
    }
    std::string out;
    for (const auto& part : parts) out += (out.empty() ? "" : " › ") + part;
    return out;
}

// `text` cut with "…" to fit `width`.
std::string fitted(std::string text, float width) {
    if (ImGui::CalcTextSize(text.c_str()).x <= width) return text;
    while (!text.empty() && ImGui::CalcTextSize((text + "…").c_str()).x > width) {
        text.pop_back();
        while (!text.empty() && (static_cast<unsigned char>(text.back()) & 0xC0) == 0x80) text.pop_back();
        if (!text.empty() && static_cast<unsigned char>(text.back()) >= 0xC0) text.pop_back();
    }
    return text + "…";
}

// A pill of muted text on a light ground, in the line: "PGN · read in place".
void pill(const std::string& text) {
    const auto at   = ImGui::GetCursorScreenPos();
    const auto size = ImGui::CalcTextSize(text.c_str());
    const ImVec2 pad(px(8), px(2));
    ImGui::Dummy(ImVec2(size.x + pad.x * 2, size.y + pad.y * 2));
    auto* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(at, ImVec2(at.x + size.x + pad.x * 2, at.y + size.y + pad.y * 2),
                      ImGui::GetColorU32(ImGuiCol_FrameBg), size.y);
    dl->AddText(ImVec2(at.x + pad.x, at.y + pad.y), ImGui::GetColorU32(ImGuiCol_TextDisabled), text.c_str());
}

// Text that links: in the accent's colour, underlined under the mouse; true when clicked.
bool linkText(const char* id, const std::string& text) {
    ImGui::PushID(id);
    const auto at      = ImGui::GetCursorScreenPos();
    const auto size    = ImGui::CalcTextSize(text.c_str());
    const bool clicked = ImGui::InvisibleButton("##link", ImVec2(std::max(size.x, 1.0f), size.y));
    const bool hovered = ImGui::IsItemHovered();
    auto* dl           = ImGui::GetWindowDrawList();
    dl->AddText(at, ui::ACCENT, text.c_str());
    if (hovered) {
        dl->AddLine(ImVec2(at.x, at.y + size.y), ImVec2(at.x + size.x, at.y + size.y), ui::ACCENT);
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }
    ImGui::PopID();
    return clicked;
}

// A part of the Openings page in its title's line: the one shown in the text's colour, underlined.
bool partLink(const char* label, bool shown) {
    const auto at   = ImGui::GetCursorScreenPos();
    const auto size = ImGui::CalcTextSize(label);
    ImGui::PushID(label);
    const bool clicked = ImGui::InvisibleButton("##part", ImVec2(size.x, ImGui::GetFrameHeight()));
    const bool hovered = ImGui::IsItemHovered();
    ImGui::PopID();
    auto* dl      = ImGui::GetWindowDrawList();
    const float y = at.y + (ImGui::GetFrameHeight() - size.y) * 0.5f;
    dl->AddText(ImVec2(at.x, y), ImGui::GetColorU32(shown || hovered ? ImGuiCol_Text : ImGuiCol_TextDisabled), label);
    if (shown)
        dl->AddLine(ImVec2(at.x, y + size.y + px(2)), ImVec2(at.x + size.x, y + size.y + px(2)), ui::ACCENT, px(2));
    if (hovered && !shown) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    return clicked && !shown;
}

}  // namespace

// ---------------------------------------------------------------------------
// The shelf, the board's position

TheoryShelf& App::theory() {
    if (!theory_) theory_ = std::make_unique<TheoryShelf>(storage_.root(), !options_.scripted);
    return *theory_;
}

void App::pollTheory() {
    if (!theory_) return;  // made when first needed
    const auto now = std::chrono::steady_clock::now();
    // The repertoires' comments: their files followed, once a second at most.
    if (now - theory_synced_ >= std::chrono::seconds(1)) {
        theory_synced_ = now;
        std::vector<std::pair<std::string, fs::path>> files;
        for (const auto& e : repertoires().entries()) files.emplace_back(e.name, e.file);
        theory_->syncRepertoires(files);
    }
    theory_->poll(now);
}

const TheoryAt& App::boardTheory() {
    auto& b        = board_;
    auto& t        = theory();
    const auto fen = boardFen();
    const auto of =
        fmt::format("{}|{}|{}|{}|{}|{}", fen, t.generation(), board_game_serial_, b.line, b.ply, b.changes.count);
    if (of == theory_here_.of) return theory_here_.at;
    theory_here_.of = of;
    theory_here_.moves.clear();
    const auto board = boardAt(b.game, b.chess960, b.ply);
    // The board's moves from the standard start, which tell a transposition.
    std::optional<std::vector<std::string>> line;
    if (!b.chess960 && (b.game.start_fen.empty() || b.game.start_fen == chess::constants::STARTPOS)) {
        line.emplace();
        chess::Board walk;
        for (int i = 0; i < b.ply && i < static_cast<int>(b.game.moves.size()); ++i) {
            const auto m = b.game.moves[static_cast<std::size_t>(i)].move;
            if (m == chess::Move(chess::Move::NULL_MOVE) || m == chess::Move(chess::Move::NO_MOVE)) {
                line.reset();
                break;
            }
            line->push_back(chess::uci::moveToSan(walk, m));
            walk.makeMove<true>(m);
        }
    }
    theory_here_.at = t.at(study::theoryKey(board), line ? &*line : nullptr);
    for (const auto& f : theory_here_.at.found) {
        if (!f.on_move || theory_here_.moves.count(f.note->san)) continue;
        try {
            const auto move = chess::uci::parseSan(board, f.note->san);
            if (move != chess::Move(chess::Move::NO_MOVE)) theory_here_.moves[f.note->san] = move;
        } catch (...) {
        }
    }
    return theory_here_.at;
}

// ---------------------------------------------------------------------------
// Adding, opening

void App::addTheoryFile(const fs::path& file) {
    bool added       = false;
    const auto index = theory().addFile(file, &added);
    showTheoryPart(index);
    const auto& name = theory().studies()[index].name;
    theory_said_     = added ? trf("“{}” added: its notes show in the Tree tab once it is read.", name)
                             : trf("“{}” is in the theory already.", name);
    theory_said_at_  = ImGui::GetTime();
}

void App::makeTheoryStudy(const std::string& id) {
    loadStudiesState();
    const auto* r   = studies_.find(id);
    const auto name = r ? r->name : id;
    std::error_code ec;
    if (!fs::exists(lichess::studyCache(storage_.root(), id), ec)) {
        // Never read on this machine: its PGN asked of Lichess first (the Lichess page's reading), then made one.
        readStudy(id, {});
        if (study_job_ && study_job_->id == id) study_job_->theory_after = true;
        return;
    }
    bool added       = false;
    const auto index = theory().addLichess(id, name, &added);
    theory_chosen_   = static_cast<int>(index);
    theory_said_     = added ? trf("“{}” is a theory study: its notes show in the Tree tab.", name)
                             : trf("“{}” is a theory study already.", name);
    theory_said_at_  = ImGui::GetTime();
    study_said_of_   = id;  // said on the Lichess page too
    study_said_bad_  = false;
    study_said_      = theory_said_;
}

void App::showTheoryPart(std::optional<std::size_t> chosen) {
    page_            = Page::Openings;
    openings_theory_ = true;
    if (chosen) theory_chosen_ = static_cast<int>(*chosen);
}

void App::openTheoryDialog() {
    dialogs_.openFile([this](const std::string& path) { addTheoryFile(path); }, {{"PGN", "pgn"}},
                      storage_.root().string());
}

void App::openTheoryCopy(const TheorySource& s) {
    // Its games on the Board, a copy: the theory never writes into the file, which stays as its author writes it.
    std::ifstream in(s.file, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (text.empty()) {
        theory_said_    = trf("“{}” cannot be read now: {}", s.name, s.file.string());
        theory_said_at_ = ImGui::GetTime();
        return;
    }
    toStudyBoard();
    const auto serial = board_game_serial_;
    readOnBoard(text, s.name);
    if (board_game_serial_ == serial) return;  // it could not be read: the board says why
    board_.message = trf("A copy of “{}”: Ctrl+S saves it elsewhere, the file stays as its author writes it.", s.name);
    board_.message_error = false;
}

void App::openTheoryNote(const TheoryFound& f) {
    const auto& n     = *f.note;
    const auto serial = board_game_serial_;
    if (f.kind == TheorySource::Kind::Lichess) {
        // The chapter as the Lichess page opens it, its link to the study kept (#79).
        openStudyChapter(f.lichess, n.game);
    } else {
        // Its game as the file has it now, a copy on the board: the theory never writes into the file.
        std::ifstream in(f.file, std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (text.empty()) {
            boardToast(trf("“{}” cannot be read now: {}", f.source, f.file.string()));
            return;
        }
        const auto games = study::splitGames(text);
        if (n.game >= games.size()) {
            boardToast(trf("“{}” changed since it was read: it is read again, its notes follow.", f.source));
            return;
        }
        const auto title = f.game.empty() ? f.source : fmt::format("{} · {}", f.game, f.source);
        readOnBoard(std::string(games[n.game]), title);
        if (board_game_serial_ != serial) {
            board_.title = title;
            board_.message =
                trf("A copy of “{}”: Ctrl+S saves it elsewhere, the file stays as its author "
                    "writes it.",
                    f.source);
            board_.message_error = false;
        }
    }
    if (board_game_serial_ == serial) return;  // it could not be read: the board says why
    toStudyBoard();
    showBoardLine(n.line, n.ply);
    // The file changed since it was read: the note's position is no longer there.
    if (study::theoryKey(boardAt(board_.game, board_.chess960, board_.ply)) != n.to)
        boardToast(trf("“{}” changed since it was read: it is read again, its notes follow.", f.source));
}

// ---------------------------------------------------------------------------
// The Openings page's Theory part

void App::drawOpeningsParts() {
    // In the title's line: "· Repertoires · Theory", the part shown underlined.
    ImGui::SameLine(0, px(14));
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("·");
    ImGui::SameLine(0, px(8));
    if (partLink(TR("Repertoires"), !openings_theory_)) openings_theory_ = false;
    ImGui::SameLine(0, px(8));
    ImGui::TextDisabled("·");
    ImGui::SameLine(0, px(8));
    if (partLink(TR("Theory"), openings_theory_)) openings_theory_ = true;
    if (ImGui::IsItemHovered() && !openings_theory_)
        ImGui::SetTooltip("%s", TR("Annotated PGN files and Lichess studies, their notes in the Tree tab"));
}

void App::drawTheoryPart() {
    auto& t = theory();
    loadStudiesState();
    // At the right of the title: Add a PGN…, From a Lichess study…
    const char* add_label     = TR("Add a PGN…");
    const char* lichess_label = TR("From a Lichess study…");
    const auto& style         = ImGui::GetStyle();
    const auto button_w       = [&](const char* label) {
        return style.FramePadding.x * 2 + ImGui::GetFrameHeight() * 0.42f + style.ItemInnerSpacing.x +
               ImGui::CalcTextSize(label).x;
    };
    const float right_w = button_w(add_label) + style.ItemSpacing.x + button_w(lichess_label);
    ImGui::SameLine(std::max(ImGui::GetCursorPosX() + px(20), ImGui::GetContentRegionMax().x - right_w));
    if (ui::iconButton("theory_add", ui::Icon::Plus, add_label)) openTheoryDialog();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", TR("An annotated PGN, read where it is and read again when it changes; RookStudio "
                                   "never writes into it. Nothing leaves the machine."));
    ImGui::SameLine();
    if (ui::iconButton("theory_lichess", ui::Icon::Globe, lichess_label)) ImGui::OpenPopup("##theory_lichess");
    if (ImGui::BeginPopup("##theory_lichess")) {
        ImGui::TextDisabled("%s", TR("Your Lichess studies read on this machine"));
        bool any = false;
        for (const auto& r : studies_.studies) {
            std::error_code ec;
            if (!fs::exists(lichess::studyCache(storage_.root(), r.id), ec)) continue;  // never read here
            any            = true;
            const bool has = t.findLichess(r.id).has_value();
            if (ImGui::MenuItem(fmt::format("{}##theory_study_{}", r.name, r.id).c_str(),
                                has ? TR("in the theory") : nullptr, false, !has))
                makeTheoryStudy(r.id);
        }
        if (!any) ImGui::TextDisabled("%s", TR("None yet: a study is read on the Lichess page."));
        ImGui::Separator();
        if (ImGui::MenuItem(TR("Your Lichess studies…"))) {
            page_          = Page::Lichess;
            studies_.shown = studies_.opened = true;
            saveStudiesState();
        }
        ImGui::EndPopup();
    }
    ImGui::TextDisabled("%s", TR("Drop an annotated PGN here: its notes will show in the Tree tab, at each position it "
                                 "comments."));
    ImGui::Dummy(ImVec2(0, px(6)));

    const auto& studies = t.studies();
    const float now     = static_cast<float>(ImGui::GetTime());
    const bool keys     = keyboard::plain();
    if (studies.empty()) {
        ui::beginCard("theory_empty");
        ImGui::TextUnformatted(TR("No theory study yet."));
        ImGui::PushTextWrapPos(0);
        ImGui::TextDisabled("%s",
                            TR("Add an annotated PGN (your analyses, a book in PGN), drop one here, or start from "
                               "one of your Lichess studies: their comments, symbols, evaluations, arrows and "
                               "squares show in the Board page's Tree tab, at each position they comment, "
                               "whatever the move order that leads there."));
        ImGui::PopTextWrapPos();
        bool shown = t.repertoiresShown();
        if (ImGui::Checkbox(TR("Your repertoires' comments in the tree too"), &shown)) t.showRepertoires(shown);
        ui::endCard();
        drawTheoryUndo();
        return;
    }
    theory_chosen_ = std::clamp(theory_chosen_, 0, static_cast<int>(studies.size()) - 1);
    // The keys: Up and Down choose a study, Enter opens it on the Board, Delete takes it out (undone by its toast).
    std::optional<std::size_t> open, take_out;
    if (keys) {
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))
            theory_chosen_ = std::min(theory_chosen_ + 1, static_cast<int>(studies.size()) - 1);
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) theory_chosen_ = std::max(theory_chosen_ - 1, 0);
        if (keyboard::enterPressed()) open = static_cast<std::size_t>(theory_chosen_);
        if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) take_out = static_cast<std::size_t>(theory_chosen_);
    }

    // The studies, the one chosen shown at the right.
    const float list_w = std::min(px(300), ImGui::GetContentRegionAvail().x * 0.35f);
    ImGui::BeginChild("##theory_list", ImVec2(list_w, 0), ImGuiChildFlags_Borders);
    for (std::size_t i = 0; i < studies.size(); ++i) {
        const auto& s = studies[i];
        ImGui::PushID(static_cast<int>(i));
        const auto start = ImGui::GetCursorPos();
        if (ImGui::Selectable("##theory_row", theory_chosen_ == static_cast<int>(i), 0,
                              ImVec2(0, ImGui::GetTextLineHeightWithSpacing() * 2.2f)))
            theory_chosen_ = static_cast<int>(i);
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) open = i;
        // Its name, its annotated positions at the right; under them, its kind and state.
        const auto count = s.notes ? ui::exactCount(s.notes->counts().positions) : std::string("–");
        ImGui::SetCursorPos(ImVec2(start.x + px(6), start.y + px(3)));
        const float count_w = ImGui::CalcTextSize(count.c_str()).x;
        ImGui::PushClipRect(ImGui::GetCursorScreenPos(),
                            ImVec2(ImGui::GetWindowPos().x + ImGui::GetContentRegionMax().x - count_w - px(14),
                                   ImGui::GetCursorScreenPos().y + ImGui::GetTextLineHeight()),
                            true);
        ImGui::TextUnformatted(s.name.c_str());
        ImGui::PopClipRect();
        ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetContentRegionMax().x - count_w - px(6)));
        ImGui::TextDisabled("%s", count.c_str());
        ImGui::SetCursorPosX(start.x + px(6));
        auto sub = stateText(s, s.kind == TheorySource::Kind::Lichess ? studies_.find(s.lichess) : nullptr);
        if (!s.shown) sub += std::string(" · ") + TR("hidden from the tree");
        ImGui::TextDisabled("%s", fitted(sub, ImGui::GetContentRegionAvail().x - px(6)).c_str());
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", sub.c_str());
        ImGui::PopID();
    }
    ImGui::Dummy(ImVec2(0, px(4)));
    ImGui::Separator();
    bool shown = t.repertoiresShown();
    if (ImGui::Checkbox(TR("Your repertoires' comments too"), &shown)) t.showRepertoires(shown);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", TR("The comments of your repertoires show in the Tree tab the same way, under their "
                                   "names."));
    ImGui::EndChild();
    ImGui::SameLine(0, px(14));
    ImGui::BeginChild("##theory_chosen", ImVec2(0, 0));
    const auto index = static_cast<std::size_t>(theory_chosen_);
    drawTheoryStudy(index, open, take_out);
    if (now - theory_said_at_ < 8 && !theory_said_.empty())
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::ACCENT), "%s", theory_said_.c_str());
    drawTheoryUndo();
    ImGui::EndChild();
    if (open && *open < t.studies().size()) openTheoryCopy(t.studies()[*open]);
    if (take_out && *take_out < t.studies().size()) {
        const auto name = t.studies()[*take_out].name;
        if (auto removed = t.remove(*take_out))
            theory_undo_ = TheoryUndo{*removed, trf("“{}” taken out of the theory; the file stays where it is.", name),
                                      ImGui::GetTime()};
    }
}

void App::drawTheoryUndo() {
    if (!theory_undo_ || ImGui::GetTime() - theory_undo_->at >= 8) return;
    ImGui::TextUnformatted(theory_undo_->said.c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton(TR("Undo"))) {
        theory_chosen_ = static_cast<int>(theory().restore(theory_undo_->removed));
        theory_undo_.reset();
    }
}

void App::drawTheoryStudy(std::size_t index, std::optional<std::size_t>& open, std::optional<std::size_t>& take_out) {
    auto& t = theory();
    if (index >= t.studies().size()) return;
    const auto& s      = t.studies()[index];
    const auto* record = s.kind == TheorySource::Kind::Lichess ? studies_.find(s.lichess) : nullptr;
    const auto* notes  = s.notes.get();
    const auto& counts = notes ? notes->counts() : study::TheoryCounts{};
    // Its name and its kind.
    {
        const float h = ImGui::GetFontSize() * 1.25f;
        const auto at = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(h, h));
        ui::drawIcon(ImGui::GetWindowDrawList(), ui::Icon::Comment, ImVec2(at.x + h * 0.5f, at.y + h * 0.5f), h,
                     ImGui::GetColorU32(ImGuiCol_Text));
        ImGui::SameLine(0, px(8));
        ImGui::PushFont(ui::titleFont(), ImGui::GetFontSize() * 1.15f);
        ImGui::TextUnformatted(s.name.c_str());
        ImGui::PopFont();
        ImGui::SameLine(0, px(10));
        pill(stateText(s, record));
    }
    // Where it is, and what it holds.
    std::string where =
        s.kind == TheorySource::Kind::Lichess ? std::string(TR("Its copy in your profile")) : shortPath(s.file);
    if (notes && counts.games > 0) {
        const auto n = static_cast<int>(std::min<std::size_t>(counts.games, 1000000000));
        const auto c = ui::exactCount(counts.games);
        where += " · " + (s.kind == TheorySource::Kind::Lichess
                              ? (counts.variations > 0
                                     ? trn("{} chapter and its variations", "{} chapters and their variations", n, c)
                                     : trn("{} chapter", "{} chapters", n, c))
                              : (counts.variations > 0
                                     ? trn("{} game and its variations", "{} games and their variations", n, c)
                                     : trn("{} game", "{} games", n, c)));
    }
    ImGui::TextDisabled("%s", where.c_str());
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", s.file.string().c_str());

    if (s.reading()) {
        ImGui::ProgressBar(static_cast<float>(s.progress()), ImVec2(px(320), px(8)), "");
        ImGui::TextDisabled("%s", TR("Reading its games, in the background…"));
    }
    if (s.missing) {
        ImGui::PushTextWrapPos(0);
        if (s.kind == TheorySource::Kind::Lichess) {
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s",
                               TR("Its PGN is not on this machine: read the study on the Lichess page, and its notes "
                                  "come here."));
        } else {
            ImGui::TextColored(
                ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s",
                trf("Not found where it was: {}. Put it back there, or find it where it went.", s.file.string())
                    .c_str());
        }
        ImGui::PopTextWrapPos();
        if (s.kind == TheorySource::Kind::File && ImGui::SmallButton(TR("Find it…"))) {
            dialogs_.openFile(
                [this](const std::string& path) {
                    if (theory_chosen_ >= 0 && static_cast<std::size_t>(theory_chosen_) < theory().studies().size())
                        theory().relocate(static_cast<std::size_t>(theory_chosen_), path);
                },
                {{"PGN", "pgn"}}, s.file.parent_path().string());
        }
        if (s.kind == TheorySource::Kind::Lichess && ImGui::SmallButton(TR("Your Lichess studies…"))) {
            page_          = Page::Lichess;
            studies_.shown = studies_.opened = true;
            study_shown_                     = s.lichess;
        }
    } else if (!s.error.empty()) {
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s",
                           trf("It cannot be read: {}.", s.error).c_str());
    }

    // Its numbers (M11, board 2).
    if (notes) {
        ImGui::Dummy(ImVec2(0, px(2)));
        const auto number = [&](std::size_t value, const std::string& label, bool first) {
            if (!first) ImGui::SameLine(0, px(18));
            ImGui::PushFont(ui::titleFont(), ImGui::GetFontSize());
            ImGui::TextUnformatted(ui::exactCount(value).c_str());
            ImGui::PopFont();
            ImGui::SameLine(0, px(4));
            ImGui::TextDisabled("%s", label.c_str());
        };
        const auto n = [](std::size_t value) { return std::min<std::size_t>(value, 1000000000); };
        number(counts.positions, trn("position annotated", "positions annotated", n(counts.positions)), true);
        number(counts.comments, trn("comment", "comments", n(counts.comments)), false);
        number(counts.symbols, trn("symbol", "symbols", n(counts.symbols)), false);
        number(counts.evaluations, trn("evaluation", "evaluations", n(counts.evaluations)), false);
        ImGui::PushTextWrapPos(0);
        if (counts.notes == 0 && !s.reading())
            ImGui::TextDisabled("%s", TR("No notes in it: its games have no comment, symbol, written evaluation, arrow "
                                         "or square."));
        if (counts.left_out > 0)
            ImGui::TextDisabled("%s", trn("{} comment of the assistant left out: it is about one game's move, and the "
                                          "assistant comments any position on request.",
                                          "{} comments of the assistant left out: they are about one game's moves, and "
                                          "the assistant comments any position on request.",
                                          static_cast<int>(counts.left_out), counts.left_out)
                                          .c_str());
        if (counts.unreadable > 0 || !notes->error().empty())
            ImGui::TextDisabled(
                "%s", trf("{} · the rest is read.", notes->error().empty()
                                                        ? trn("{} game cannot be read", "{} games cannot be read",
                                                              static_cast<int>(counts.unreadable), counts.unreadable)
                                                        : notes->error())
                          .c_str());
        ImGui::PopTextWrapPos();
    }
    ImGui::Dummy(ImVec2(0, px(4)));
    ImGui::PushTextWrapPos(0);
    ImGui::TextDisabled("%s",
                        TR("Its notes are kept by position, as the tree is: they show whatever the move order that "
                           "leads there, in a game, a database or your repertoire."));
    ImGui::PopTextWrapPos();
    bool shown = s.shown;
    if (ImGui::Checkbox(TR("Show its notes in the tree"), &shown)) t.setShown(index, shown);
    ImGui::Dummy(ImVec2(0, px(2)));
    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(ui::ACCENT));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
    if (ui::iconButton("theory_open", ui::Icon::Board, TR("Open it on the Board"))) open = index;
    ImGui::PopStyleColor(2);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", TR("Its games on the Board, a copy: Ctrl+S saves them elsewhere · Enter"));
    ImGui::SameLine();
    if (ImGui::Button(TR("Remove it from the theory"))) take_out = index;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TR("The file stays where it is · Del"));
    ImGui::PushTextWrapPos(0);
    ImGui::TextDisabled("%s",
                        s.kind == TheorySource::Kind::Lichess
                            ? TR("Removed, its copy stays in your profile. Read again on the Lichess page, its "
                                 "notes follow.")
                            : TR("Removed, the file stays where it is: RookStudio never writes into it. Changed on "
                                 "the disk, it is read again by itself."));
    ImGui::PopTextWrapPos();
}

// ---------------------------------------------------------------------------
// The Tree tab and the Notation tab

void App::drawTreeTheoryCell(const std::string& san) {
    const auto& at = theory_here_.at;
    const auto it  = at.moves.find(san);
    if (it == at.moves.end()) return;
    std::string text;
    for (const auto nag : it->second.symbols) text += (text.empty() ? "" : " ") + nagText(nag);
    if (it->second.notes > 0)
        text += (text.empty() ? "" : " · ") + trn("{} note", "{} notes", it->second.notes, it->second.notes);
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::ACCENT), "%s", text.c_str());
    if (ImGui::IsItemHovered()) {
        std::string tip;
        for (const auto& f : at.found)
            if (f.on_move && f.note->san == san) {
                auto said = noteText(f);
                if (said.size() > 90) {
                    said.resize(87);
                    while (!said.empty() && (static_cast<unsigned char>(said.back()) & 0xC0) == 0x80) said.pop_back();
                    if (!said.empty() && static_cast<unsigned char>(said.back()) >= 0xC0) said.pop_back();
                    said += "…";
                }
                tip += (tip.empty() ? "" : "\n") + fmt::format("{}: {}", f.source, said);
            }
        ImGui::SetTooltip("%s", tip.c_str());
    }
}

std::optional<chess::Move> App::drawTreeTheory(float height) {
    std::optional<chess::Move> hovered;
    const auto& at = boardTheory();
    if (at.found.empty() || height < ImGui::GetTextLineHeightWithSpacing() * 2) return hovered;
    const ui::TopicRegion topic(guide::topics::TheoryStudies);
    ImGui::Dummy(ImVec2(0, px(2)));
    ImGui::PushFont(ui::titleFont(), ImGui::GetFontSize());
    ImGui::TextUnformatted(TR("What your studies say"));
    ImGui::PopFont();
    ImGui::SameLine(0, px(6));
    const auto n = static_cast<int>(at.found.size());
    ImGui::TextDisabled("%s", trn("· {} note at this position", "· {} notes at this position", n, n).c_str());
    ImGui::BeginChild("##tree_theory",
                      ImVec2(0, std::max(px(40), height - ImGui::GetTextLineHeightWithSpacing() - px(4))));
    constexpr std::size_t SHOWN = 40;  // a position of the first moves can have hundreds
    const auto board_hash       = boardAt(board_.game, board_.chess960, board_.ply).hash();
    std::optional<std::size_t> open;
    for (std::size_t i = 0; i < at.found.size() && i < SHOWN; ++i) {
        const auto& f    = at.found[i];
        const auto& note = *f.note;
        ImGui::PushID(static_cast<int>(i));
        ui::beginCard("##theory_note");
        // Its source, where it is.
        ImGui::PushFont(ui::titleFont(), ImGui::GetFontSize());
        ImGui::TextUnformatted(f.source.c_str());
        ImGui::PopFont();
        ImGui::SameLine(0, px(8));
        ImGui::PushTextWrapPos(0);
        ImGui::TextDisabled("%s", whereText(f).c_str());
        // Its text.
        ImGui::TextWrapped("%s", noteText(f).c_str());
        ImGui::PopTextWrapPos();
        // Its line, opened on the board; its marks.
        if (linkText("line", lineLabel(f))) open = i;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", trf("“{}” on the Board at this move; the game shown goes to the recent games "
                                        "(Ctrl+Tab).",
                                        f.source)
                                        .c_str());
        const auto arrows = static_cast<int>(note.arrows.size()), squares = static_cast<int>(note.squares.size());
        std::string extra = i == 0 ? std::string(TR("open the line on the board")) : std::string();
        if (arrows + squares > 0) {
            auto marks = arrows > 0 ? trn("{} arrow", "{} arrows", arrows, arrows) : std::string();
            if (squares > 0) marks += (marks.empty() ? "" : ", ") + trn("{} square", "{} squares", squares, squares);
            marks = f.on_move ? trf("{} after {}", marks, moveLabel(note.number, note.black, note.san))
                              : trf("{}, shown on the board under the mouse", marks);
            extra += (extra.empty() ? "" : " · ") + marks;
        }
        if (!extra.empty()) {
            ui::sameLineOrWrap(ImGui::CalcTextSize(extra.c_str()).x, px(12));
            ImGui::PushTextWrapPos(0);
            ImGui::TextDisabled("%s", extra.c_str());
            ImGui::PopTextWrapPos();
        }
        ui::endCard();
        // Under the mouse: a note on a move shows the move as the tree's arrow; one about the position its marks.
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)) {
            if (f.on_move) {
                if (const auto m = theory_here_.moves.find(note.san); m != theory_here_.moves.end())
                    hovered = m->second;
            } else if (arrows + squares > 0) {
                theory_marks_ = TheoryMarks{board_hash, note.arrows, note.squares, ImGui::GetFrameCount()};
            }
        }
        ImGui::PopID();
    }
    if (at.found.size() > SHOWN)
        ImGui::TextDisabled("%s",
                            trn("and {} more note", "and {} more notes", static_cast<int>(at.found.size() - SHOWN),
                                ui::exactCount(at.found.size() - SHOWN))
                                .c_str());
    ImGui::EndChild();
    if (open) {
        // The board changes: after the notes are drawn, with a copy of the note's place (the notes it points into
        // held with it).
        const auto found = at.found[*open];
        const auto held  = at.held;
        openTheoryNote(found);
    }
    return hovered;
}

void App::drawTheoryChip() {
    // Fair play (#48): no aid while one of the user's games is played live.
    if (liveGameOn()) return;
    const auto& at = boardTheory();
    if (at.found.empty()) return;
    const auto n      = static_cast<int>(at.found.size());
    const auto label  = trf("Theory · {}", n);
    const float width = ImGui::CalcTextSize(label.c_str()).x + ImGui::GetStyle().FramePadding.x * 2;
    ImGui::SameLine(0, px(12));
    if (ImGui::GetContentRegionAvail().x < width)
        ImGui::NewLine();
    else
        ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), ImGui::GetContentRegionMax().x - width));
    const auto accent = ImGui::ColorConvertU32ToFloat4(ui::ACCENT);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, ImGui::GetFrameHeight() * 0.5f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(accent.x, accent.y, accent.z, 0.18f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(accent.x, accent.y, accent.z, 0.32f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(accent.x, accent.y, accent.z, 0.45f));
    if (ImGui::Button(fmt::format("{}###theory_chip", label).c_str())) board_side_tab_ = 2;
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s",
                          trn("What your studies say about this position: {} note, in the Tree tab · Alt+3",
                              "What your studies say about this position: {} notes, in the Tree tab · Alt+3", n, n)
                              .c_str());
}

}  // namespace fcg
