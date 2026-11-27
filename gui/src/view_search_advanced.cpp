// The advanced searches of the Databases page's search panel (#89, mockup M13, board 2): the Pattern tab grown into a
// builder of conditions, a line of moves and a text in our own language with CQL's words (study/patterns.hpp), the
// Manoeuvre tab (a man's route) and the Structure tab (the pawns, exactly or near), each search in the background with
// its progress, Cancel and resumption, the first games found already in the list, each game at the position found.

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <functional>
#include <initializer_list>
#include <string>
#include <vector>

#include <fmt/format.h>
#include <imgui.h>

#include <commentary/formation.hpp>
#include <commentary/openings.hpp>
#include <study/patterns.hpp>

#include "app.hpp"
#include "board_text.hpp"
#include "board_view.hpp"
#include "guide.hpp"
#include "i18n.hpp"
#include "ui.hpp"
#include "widgets.hpp"

namespace fcg {

namespace {

float px(float v) { return v * ImGui::GetFontSize() / 16.0f; }
uint64_t bit(int square) { return uint64_t{1} << square; }
std::string squareName(int s) { return {static_cast<char>('a' + s % 8), static_cast<char>('1' + s / 8)}; }

constexpr chess::PieceType kKinds[] = {chess::PieceType::PAWN, chess::PieceType::KNIGHT, chess::PieceType::BISHOP,
                                       chess::PieceType::ROOK, chess::PieceType::QUEEN,  chess::PieceType::KING};

chess::Piece pieceOf(int side, int kind) {
    return chess::Piece(kKinds[kind], side == 0 ? chess::Color::WHITE : chess::Color::BLACK);
}

// A man as a row of the builder names it.
const char* manPhrase(int side, int kind) {
    static const char* white[] = {N_("a white pawn"), N_("a white knight"), N_("a white bishop"),
                                  N_("a white rook"), N_("a white queen"),  N_("the white king")};
    static const char* black[] = {N_("a black pawn"), N_("a black knight"), N_("a black bishop"),
                                  N_("a black rook"), N_("a black queen"),  N_("the black king")};
    return i18n::tr(side == 0 ? white[kind] : black[kind]);
}
const char* manDefinite(int side, int kind) {
    static const char* white[] = {N_("the white pawn"), N_("the white knight"), N_("the white bishop"),
                                  N_("the white rook"), N_("the white queen"),  N_("the white king")};
    static const char* black[] = {N_("the black pawn"), N_("the black knight"), N_("the black bishop"),
                                  N_("the black rook"), N_("the black queen"),  N_("the black king")};
    return i18n::tr(side == 0 ? white[kind] : black[kind]);
}
const char* sameMan(int kind) {
    static const char* same[] = {N_("the same pawn"), N_("the same knight"), N_("the same bishop"),
                                 N_("the same rook"), N_("the same queen"),  N_("the same king")};
    return kind >= 0 && kind < 6 ? i18n::tr(same[kind]) : TR("the same man");
}

// Squares as a reader reads them: "f3", "a-h8", "f3, g3".
std::string squaresShown(uint64_t squares) {
    auto t = study::squaresText(squares);
    if (!t.empty() && t.front() == '[') t = t.substr(1, t.size() - 2);
    for (std::size_t at = 0; (at = t.find(',', at)) != std::string::npos; at += 2) t.replace(at, 1, ", ");
    return t;
}

std::string menPhrase(const study::Men& m) {
    if (m.pieces == 0) {
        return std::popcount(m.squares) == 1 ? trf("the square {}", squaresShown(m.squares))
                                             : trf("the squares {}", squaresShown(m.squares));
    }
    std::vector<std::string> parts;
    uint16_t left = m.pieces;
    if ((left & study::Men::WHITE) == study::Men::WHITE && (left & study::Men::BLACK) == study::Men::BLACK) {
        parts.push_back(TR("any man"));
        left = static_cast<uint16_t>(left & ~(study::Men::WHITE | study::Men::BLACK));
    }
    if ((left & study::Men::WHITE) == study::Men::WHITE) {
        parts.push_back(TR("any white man"));
        left = static_cast<uint16_t>(left & ~study::Men::WHITE);
    }
    if ((left & study::Men::BLACK) == study::Men::BLACK) {
        parts.push_back(TR("any black man"));
        left = static_cast<uint16_t>(left & ~study::Men::BLACK);
    }
    for (int side = 0; side < 2; ++side)
        for (const int kind : {5, 4, 3, 2, 1, 0})
            if (left & study::Men::man(side, kind)) parts.push_back(manPhrase(side, kind));
    if (left & study::Men::EMPTY) parts.push_back(TR("an empty square"));
    std::string out = parts.empty() ? std::string() : parts[0];
    for (std::size_t i = 1; i < parts.size(); ++i) out = trf("{} or {}", out, parts[i]);
    if (!m.anywhere()) out = trf("{} on {}", out, squaresShown(m.squares));
    return out;
}

// The kind of men a set names when it names one ("the same knight"), -1 otherwise.
int kindOf(const study::Men& m) {
    int kind = -1;
    for (int b = 0; b < 12; ++b)
        if (m.pieces & (1u << b)) {
            if (kind >= 0 && kind != b % 6) return -1;
            kind = b % 6;
        }
    return (m.pieces & study::Men::EMPTY) ? -1 : kind;
}

// A word of a row: a man, a verb, or the words between; a click on it opens what it can become.
enum class Word { Man, Verb, Glue };
bool word(const std::string& text, const char* id, Word kind) {
    const auto label = text + "##" + id;
    const float w    = ImGui::CalcTextSize(text.c_str()).x + ImGui::GetStyle().FramePadding.x * 2;
    ui::sameLineOrWrap(w, px(4));
    int colours = 0;
    if (kind == Word::Verb) {
        ImGui::PushStyleColor(ImGuiCol_Button, (ui::ACCENT & 0x00ffffff) | 0x40000000);
        ImGui::PushStyleColor(ImGuiCol_Text, ui::ACCENT);
        colours = 2;
    } else if (kind == Word::Glue) {
        ImGui::PushStyleColor(ImGuiCol_Button, 0);
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(ImGuiCol_TextDisabled));
        colours = 2;
    }
    const bool clicked = ImGui::SmallButton(label.c_str());
    ImGui::PopStyleColor(colours);
    return clicked;
}

// A whole phrase whose placeholders are drawn by `parts` (words of a row, a number field) and whose other words are
// drawn as text between them, in the order the translation gives: "{} pins {} on {}", "In {} moves at most"; "{1}"
// names a part by its place, for a language that puts the words in another order.
void phrase(const char* translated, std::initializer_list<std::function<void()>> parts) {
    const std::vector<std::function<void()>> drawn(parts);
    std::size_t next = 0, at = 0;
    const std::string_view f(translated);
    const auto glue = [&](std::string_view text) {
        while (!text.empty() && text.front() == ' ') text.remove_prefix(1);
        while (!text.empty() && text.back() == ' ') text.remove_suffix(1);
        if (text.empty()) return;
        const std::string t(text);
        ui::sameLineOrWrap(ImGui::CalcTextSize(t.c_str()).x, px(4));
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", t.c_str());
    };
    while (at < f.size()) {
        const auto open = f.find('{', at);
        if (open == std::string_view::npos) break;
        const auto close = f.find('}', open);
        if (close == std::string_view::npos) break;
        glue(f.substr(at, open - at));
        std::size_t index = next;
        if (close > open + 1) {
            index = 0;
            for (std::size_t i = open + 1; i < close; ++i) index = index * 10 + static_cast<std::size_t>(f[i] - '0');
        }
        ++next;
        if (index < drawn.size() && drawn[index]) drawn[index]();
        at = close + 1;
    }
    glue(f.substr(std::min(at, f.size())));
}

// A line of its own made of a whole phrase around a field: "In {} moves at most".
void fieldPhrase(const char* translated, const std::function<void()>& field) {
    const std::string f(translated);
    const auto at      = f.find("{}");
    const auto trimmed = [](std::string t) {
        while (!t.empty() && t.front() == ' ') t.erase(t.begin());
        while (!t.empty() && t.back() == ' ') t.pop_back();
        return t;
    };
    const auto before = trimmed(at == std::string::npos ? f : f.substr(0, at));
    const auto after  = trimmed(at == std::string::npos ? std::string() : f.substr(at + 2));
    ImGui::AlignTextToFramePadding();
    if (!before.empty()) {
        ImGui::TextUnformatted(before.c_str());
        ImGui::SameLine();
    }
    field();
    if (!after.empty()) {
        ImGui::SameLine();
        ImGui::TextUnformatted(after.c_str());
    }
}

// Why a text does not read, as the panel says it.
std::string errorText(const study::PatternError& e) {
    using K = study::PatternErrorKind;
    switch (e.kind) {
        case K::UnknownWord:
            return e.suggestion.empty()
                       ? trf("Line {}: “{}” is not a known word.", e.line, e.word)
                       : trf("Line {}: “{}” is not a known word — “{}”?", e.line, e.word, e.suggestion);
        case K::NotUnderstood:
            return trf("Line {}: “{}” is CQL that RookStudio does not read.", e.line, e.word);
        case K::BadSquares:
            return trf("Line {}: the squares of “{}” do not read (a1 to h8, a-h2, [a1,h8]).", e.line, e.word);
        case K::NeedMen:
            return trf("Line {}: after “{}”, men or a name ($c) are wanted.", e.line, e.word);
        case K::NeedCondition:
            return e.word.empty() ? trf("Line {}: a condition is wanted.", e.line)
                                  : trf("Line {}: “{}” is not a condition here.", e.line, e.word);
        case K::NeedKind:
            return trf("Line {}: after “promote”, Q, R, B or N.", e.line);
        case K::NeedPawns:
            return trf("Line {}: after “only”, P or p.", e.line);
        case K::NeedName:
            return trf("Line {}: “piece” wants a name, “in” and men: piece $c in N.", e.line);
        case K::NotNamed:
            return trf("Line {}: “{}” is not named by a line “piece {} in …”.", e.line, e.word, e.word);
        case K::NamedTwice:
            return trf("Line {}: “{}” is named twice.", e.line, e.word);
        case K::TooManyNames:
            return trf("Line {}: four names at most.", e.line);
        case K::SecondLine:
            return trf("Line {}: a pattern has one line of moves.", e.line);
        case K::StepWithoutLine:
            return trf("Line {}: “-->” follows a line “line --> …”.", e.line);
        case K::EmptyLine:
            return trf("Line {}: a step is wanted after “{}”.", e.line, e.word);
        case K::TooManySteps:
            return trf("Line {}: eight steps at most.", e.line);
        case K::GapAtEnd:
            return trf("Line {}: a line of moves cannot end with plies of anything: a step is wanted after them.", e.line);
        case K::BadGap:
            return trf("Line {}: “{}” does not read: {}, 20 plies at most.", e.line, e.word, ".{0,2}");
        case K::Unexpected:
            return trf("Line {}: “{}” is too much here.", e.line, e.word);
    }
    return {};
}

// A text of several lines, of any length.
bool editLines(const char* id, std::string& value, ImVec2 size) {
    const auto resize = [](ImGuiInputTextCallbackData* data) {
        if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
            auto* text = static_cast<std::string*>(data->UserData);
            text->resize(static_cast<std::size_t>(data->BufTextLen));
            data->Buf = text->data();
        }
        return 0;
    };
    return ImGui::InputTextMultiline(id, value.data(), value.capacity() + 1, size, ImGuiInputTextFlags_CallbackResize,
                                     resize, &value);
}

// A box of the panel: a warning, or a note; its title, its text, and the gestures that fix it.
void box(bool warning, const std::string& title, const std::string& text) {
    ImGui::PushStyleColor(ImGuiCol_Text, warning ? ui::WARN : ImGui::GetColorU32(ImGuiCol_Text));
    ImGui::PushTextWrapPos();
    ImGui::TextUnformatted(title.c_str());
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
    if (!text.empty()) {
        ImGui::PushTextWrapPos();
        ImGui::TextDisabled("%s", text.c_str());
        ImGui::PopTextWrapPos();
    }
}

// A value wider than its cell ends in "…", whole under the mouse.
void fittedText(const std::string& text) {
    const float room = ImGui::GetContentRegionAvail().x;
    if (ImGui::CalcTextSize(text.c_str()).x <= room) {
        ImGui::TextUnformatted(text.c_str());
        return;
    }
    std::string cut = text;
    while (!cut.empty() && ImGui::CalcTextSize((cut + "…").c_str()).x > room) {
        while (!cut.empty() && (static_cast<unsigned char>(cut.back()) & 0xC0) == 0x80) cut.pop_back();
        if (!cut.empty()) cut.pop_back();
    }
    ImGui::TextUnformatted((cut + "…").c_str());
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenOverlappedByItem)) ImGui::SetTooltip("%s", text.c_str());
}

// The board of a game after `ply` plies, and the move that led there.
struct GameAt {
    chess::Board board;
    std::optional<chess::Move> last;
    std::string move;  // "24.Nc7+", in the reader's letters
};

std::optional<GameAt> gameAt(const study::Snapshot& snap, study::GameId id, uint32_t ply) {
    const auto g = snap.gameMoves(id);
    GameAt at;
    at.board.set960((g.flags & study::GAME_CHESS960) != 0);
    if (g.flags & study::GAME_FEN) {
        const auto fen = snap.startFen(id);  // empty for a damaged record
        if (fen.empty() || !at.board.setFen(fen)) return std::nullopt;
    } else {
        at.board.setFen(chess::constants::STARTPOS);
    }
    study::ScanBoard scan;
    if (!scan.set(at.board)) return std::nullopt;
    std::size_t p = 0;
    for (uint32_t i = 0; i < ply && i < g.plies && p < g.codes.size(); ++i) {
        const int count = scan.codeCount();
        int code        = g.codes[p++];
        if (count > 256) {
            if (p >= g.codes.size()) return std::nullopt;
            code = (code << 8) | g.codes[p++];
        }
        const auto move = study::moveOfCode(at.board, scan, code);
        if (!move) return std::nullopt;
        if (i + 1 == ply) {
            const auto san   = localSan(chess::uci::moveToSan(at.board, *move));
            const int number = static_cast<int>(at.board.fullMoveNumber());
            at.move          = at.board.sideToMove() == chess::Color::WHITE ? fmt::format("{}.{}", number, san)
                                                                            : fmt::format("{}…{}", number, san);
            at.last          = *move;
        }
        scan.play(scan.decode(code));
        at.board.makeMove(*move);
    }
    return at;
}

std::string newVariableName(const study::Pattern& p, const study::Men& men) {
    const int kind  = kindOf(men);
    const char base = kind >= 0 ? "pnbrqk"[kind] : 'x';
    for (int n = 1;; ++n) {
        const auto name = n == 1 ? std::string(1, base) : std::string(1, base) + std::to_string(n);
        if (std::none_of(p.variables.begin(), p.variables.end(), [&](const auto& v) { return v.name == name; }))
            return name;
    }
}

// The names no condition uses any more, taken out, the others' indexes kept in step.
void pruneVariables(study::Pattern& p) {
    std::vector<bool> used(p.variables.size(), false);
    const auto mark = [&](const study::Subject& s) {
        if (s.variable >= 0 && static_cast<std::size_t>(s.variable) < used.size())
            used[static_cast<std::size_t>(s.variable)] = true;
    };
    const auto walk = [&](auto&& self, const study::Condition& c) -> void {
        mark(c.a);
        mark(c.b);
        mark(c.c);
        for (const auto& alt : c.alternatives) self(self, alt);
    };
    for (const auto& c : p.conditions) walk(walk, c);
    for (const auto& s : p.line) {
        for (const auto& c : s.conditions) walk(walk, c);
        if (s.move) {
            if (s.move->from) mark(*s.move->from);
            if (s.move->to) mark(*s.move->to);
            if (s.move->captured) mark(*s.move->captured);
        }
    }
    std::vector<int> remap(p.variables.size(), -1);
    std::vector<study::Variable> kept;
    for (std::size_t v = 0; v < p.variables.size(); ++v)
        if (used[v]) {
            remap[v] = static_cast<int>(kept.size());
            kept.push_back(p.variables[v]);
        }
    if (kept.size() == p.variables.size()) return;
    const auto fix = [&](study::Subject& s) {
        if (s.variable >= 0) s.variable = remap[static_cast<std::size_t>(s.variable)];
    };
    const auto fixAll = [&](auto&& self, study::Condition& c) -> void {
        fix(c.a);
        fix(c.b);
        fix(c.c);
        for (auto& alt : c.alternatives) self(self, alt);
    };
    for (auto& c : p.conditions) fixAll(fixAll, c);
    for (auto& s : p.line) {
        for (auto& c : s.conditions) fixAll(fixAll, c);
        if (s.move) {
            if (s.move->from) fix(*s.move->from);
            if (s.move->to) fix(*s.move->to);
            if (s.move->captured) fix(*s.move->captured);
        }
    }
    p.variables = std::move(kept);
}

// The builder's rows: each condition's words, a click on a word opening what it can become.
class Builder {
   public:
    Builder(study::Pattern& p, const std::array<int8_t, 64>& men) : p_(p), men_(men) {}
    bool changed = false;
    std::optional<std::size_t> remove;

    // The men of a word, chosen in its popup; `squares_alone`: squares may stand for men ("attacks f7").
    void subject(study::Subject& s, const char* id, bool squares_alone, bool empty_square) {
        std::string text;
        if (s.variable >= 0 && static_cast<std::size_t>(s.variable) < p_.variables.size()) {
            const auto& v = p_.variables[static_cast<std::size_t>(s.variable)];
            text = introduced_[static_cast<std::size_t>(s.variable)] ? sameMan(kindOf(v.men)) : menPhrase(v.men);
            introduced_[static_cast<std::size_t>(s.variable)] = true;
        } else {
            text = menPhrase(s.men);
        }
        if (word(text, id, Word::Man)) ImGui::OpenPopup(id);
        if (!ImGui::BeginPopup(id)) return;
        study::Men* men = s.variable >= 0 && static_cast<std::size_t>(s.variable) < p_.variables.size()
                              ? &p_.variables[static_cast<std::size_t>(s.variable)].men
                              : &s.men;
        ImGui::PushItemFlag(ImGuiItemFlags_AutoClosePopups, false);
        for (int side = 0; side < 2; ++side) {
            ImGui::TextDisabled("%s", side == 0 ? TR("White") : TR("Black"));
            for (const int kind : {5, 4, 3, 2, 1, 0}) {
                const auto b      = study::Men::man(side, kind);
                const bool chosen = (men->pieces & b) != 0;
                if (ImGui::MenuItem(manPhrase(side, kind), nullptr, chosen)) {
                    const auto next =
                        static_cast<uint16_t>(chosen ? men->pieces & ~b : (men->pieces & ~study::Men::EMPTY) | b);
                    // No men left: the squares alone, when the word takes squares and some are given.
                    if (next != 0 || (squares_alone && !men->anywhere())) men->pieces = next;
                    changed = true;
                }
            }
            const auto all    = side == 0 ? study::Men::WHITE : study::Men::BLACK;
            const bool chosen = (men->pieces & all) == all;
            if (ImGui::MenuItem(side == 0 ? TR("any white man") : TR("any black man"), nullptr, chosen)) {
                men->pieces =
                    static_cast<uint16_t>(chosen ? men->pieces & ~all : (men->pieces & ~study::Men::EMPTY) | all);
                if (men->pieces == 0 && (!squares_alone || men->anywhere())) men->pieces = all;
                changed = true;
            }
        }
        if (empty_square && ImGui::MenuItem(TR("an empty square"), nullptr, men->pieces == study::Men::EMPTY)) {
            men->pieces = study::Men::EMPTY;
            changed     = true;
        }
        ImGui::PopItemFlag();
        // Its squares, typed: "f3", "a-h8", "f3, g3"; nothing for anywhere.
        ImGui::Separator();
        ImGui::TextDisabled("%s", TR("On squares (empty: anywhere)"));
        static std::array<char, 64> typed{};
        if (ImGui::IsWindowAppearing())
            std::snprintf(typed.data(), typed.size(), "%s", men->anywhere() ? "" : squaresShown(men->squares).c_str());
        ImGui::SetNextItemWidth(px(180));
        if (ImGui::InputTextWithHint("##squares", "f3, a-h8", typed.data(), typed.size())) {
            if (typed[0] == 0) {
                men->squares = ~uint64_t{0};
                if (men->pieces == 0) men->pieces = study::Men::WHITE;  // anything anywhere says nothing
                changed = true;
            } else if (const auto squares = study::readSquares(typed.data())) {
                men->squares = *squares;
                changed      = true;
            }
        }
        if (typed[0] != 0 && !study::readSquares(typed.data()))
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::BAD), "%s", TR("Squares as a1, a-h2, f3, g3"));
        // The same man as another row's: a name the language follows (piece $n in N).
        ImGui::Separator();
        if (s.variable >= 0) {
            if (ImGui::MenuItem(TR("Not the same man any more"))) {
                s.men      = p_.variables[static_cast<std::size_t>(s.variable)].men;
                s.variable = -1;
                changed    = true;
            }
        } else {
            for (std::size_t r = 0; r < p_.conditions.size(); ++r) {
                auto& other = p_.conditions[r].a;
                if (&other == &s) continue;
                const bool single = other.variable >= 0 ||
                                    (std::popcount(other.men.pieces) == 1 && !(other.men.pieces & study::Men::EMPTY));
                if (!single) continue;
                if (ImGui::MenuItem(trf("The same man as in row {}", r + 1).c_str())) {
                    if (other.variable < 0) {
                        study::Variable v;
                        v.name = newVariableName(p_, other.men);
                        v.men  = other.men;
                        p_.variables.push_back(v);
                        other.variable = static_cast<int>(p_.variables.size()) - 1;
                    }
                    s.variable = other.variable;
                    changed    = true;
                    ImGui::CloseCurrentPopup();
                }
            }
        }
        ImGui::EndPopup();
    }

    // One condition's words.
    void condition(study::Condition& c, const std::string& id) {
        using R              = study::Relation;
        const auto verbPopup = [&](const std::string& text) {
            const auto vid = id + "v";
            if (word(text, vid.c_str(), Word::Verb)) ImGui::OpenPopup(vid.c_str());
            if (!ImGui::BeginPopup(vid.c_str())) return;
            const auto relation = [&](const char* label, R r) {
                if (!ImGui::MenuItem(label, nullptr, c.relation == r)) return;
                const int side =
                    c.a.variable < 0 && (c.a.men.pieces & study::Men::BLACK) && !(c.a.men.pieces & study::Men::WHITE)
                        ? 1
                        : 0;
                if (r == R::Attacks && c.relation != R::Attacks)
                    c.b = study::Subject{{study::Men::man(side ^ 1, study::KING)}, -1};
                if (r == R::Defends && c.relation != R::Defends)
                    c.b =
                        study::Subject{{static_cast<uint16_t>(side == 0 ? study::Men::WHITE : study::Men::BLACK)}, -1};
                if (r == R::Pins && c.relation != R::Pins) {
                    c.b = study::Subject{{static_cast<uint16_t>((side == 0 ? study::Men::BLACK : study::Men::WHITE) &
                                                                ~study::Men::man(side ^ 1, study::KING))},
                                         -1};
                    c.c = study::Subject{{study::Men::man(side ^ 1, study::KING)}, -1};
                }
                c.relation = r;
                changed    = true;
            };
            relation(TR("is on"), R::On);
            relation(TR("attacks"), R::Attacks);
            relation(TR("defends"), R::Defends);
            relation(TR("pins … on …"), R::Pins);
            relation(TR("is en prise"), R::EnPrise);
            relation(TR("is passed"), R::Passed);
            ImGui::Separator();
            if (ImGui::MenuItem(TR("Not (the contrary)"), nullptr, c.negated)) {
                c.negated = !c.negated;
                changed   = true;
            }
            ImGui::EndPopup();
        };
        // The conditions of a position alone: a click turns them round.
        const auto toggle = [&](const std::string& text, auto&& change) {
            if (word(text, (id + "t").c_str(), Word::Verb)) {
                change();
                changed = true;
            }
        };
        switch (c.relation) {
            case R::On:
                subject(c.a, (id + "a").c_str(), false, true);
                verbPopup(c.a.variable < 0 && c.a.men.anywhere()
                              ? (c.negated ? TR("is not on the board") : TR("is on the board"))
                              : (c.negated ? TR("is not there") : TR("is there")));
                break;
            case R::Attacks:
            case R::Defends:
                subject(c.a, (id + "a").c_str(), false, false);
                verbPopup(c.relation == R::Attacks ? (c.negated ? TR("does not attack") : TR("attacks"))
                                                   : (c.negated ? TR("does not defend") : TR("defends")));
                subject(c.b, (id + "b").c_str(), true, false);
                break;
            case R::Pins:
                // The verb is a word of its own; "on" is the phrase's.
                phrase(TR("{} {} {} on {}"), {[&] { subject(c.a, (id + "a").c_str(), false, false); },
                                              [&] { verbPopup(c.negated ? TR("does not pin") : TR("pins")); },
                                              [&] { subject(c.b, (id + "b").c_str(), false, false); },
                                              [&] { subject(c.c, (id + "c").c_str(), false, false); }});
                break;
            case R::EnPrise:
                subject(c.a, (id + "a").c_str(), false, false);
                verbPopup(c.negated ? TR("is not en prise") : TR("is en prise"));
                break;
            case R::Passed:
                subject(c.a, (id + "a").c_str(), false, false);
                verbPopup(c.negated ? TR("is not passed") : TR("is passed"));
                break;
            case R::Check:
                toggle(c.negated ? TR("The side to move is not in check") : TR("The side to move is in check"),
                       [&] { c.negated = !c.negated; });
                break;
            case R::Mate:
            case R::Stalemate:
                toggle(c.relation == R::Mate ? (c.negated ? TR("Not checkmate") : TR("Checkmate"))
                                             : (c.negated ? TR("Not stalemate") : TR("Stalemate")),
                       [&] { c.relation = c.relation == R::Mate ? R::Stalemate : R::Mate; });
                break;
            case R::WhiteToMove:
            case R::BlackToMove:
                toggle((c.relation == R::WhiteToMove) != c.negated ? TR("White to move") : TR("Black to move"), [&] {
                    c.relation = c.relation == R::WhiteToMove ? R::BlackToMove : R::WhiteToMove;
                    c.negated  = false;
                });
                break;
            case R::OnlyPawns:
                word(c.side == 0 ? TR("White has only these pawns") : TR("Black has only these pawns"),
                     (id + "o").c_str(), Word::Verb);
                break;
        }
        // Its alternatives, each "{} or {}" with what comes before.
        for (std::size_t i = 0; i < c.alternatives.size(); ++i)
            phrase(TR("{} or {}"), {nullptr, [&] { condition(c.alternatives[i], id + "_" + std::to_string(i)); }});
    }

    // A row: its words, then × to take it away.
    void row(std::size_t index, study::Condition& c) {
        ImGui::PushID(static_cast<int>(index));
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%zu", index + 1);
        ImGui::SameLine();
        ImGui::BeginGroup();
        condition(c, "w");
        ImGui::SameLine(0, px(8));
        if (ImGui::SmallButton("×")) remove = index;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TR("Take this condition away"));
        ImGui::EndGroup();
        ImGui::PopID();
    }

    // A step of the line: its move ("the white bishop takes on h7 with check (Bxh7+)") or its position.
    bool step(std::size_t index, study::Step& s, int gap_shown) {
        bool drop = false;
        ImGui::PushID(static_cast<int>(1000 + index));
        ImGui::AlignTextToFramePadding();
        if (s.max_gap > 0 && static_cast<int>(s.max_gap + 1) != gap_shown)
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::ACCENT), "%s",
                               trf("within {} plies", s.max_gap + 1).c_str());
        else
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::ACCENT), "%s", TR("then"));
        ImGui::SameLine(px(72));
        ImGui::BeginGroup();
        if (s.move) {
            auto& m = *s.move;
            // A move of any man until men are chosen: drawn, the pattern is left as it is.
            study::Subject any{{static_cast<uint16_t>(study::Men::WHITE | study::Men::BLACK)}, -1};
            const bool before = changed;
            changed           = false;
            subject(m.from ? *m.from : any, "from", false, false);
            if (changed && !m.from) m.from = any;
            changed = changed || before;
            if (word(m.captures ? TR("takes on") : TR("goes to"), "verb", Word::Verb)) {
                m.captures = !m.captures;
                if (!m.captures) m.captured.reset();
                changed = true;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", TR("A click switches between “takes on” and “goes to”."));
            // Its target squares.
            const auto target = m.to ? squaresShown(m.to->men.squares) : std::string(TR("anywhere"));
            if (word(target, "to", Word::Man)) ImGui::OpenPopup("to");
            if (ImGui::BeginPopup("to")) {
                static std::array<char, 64> typed{};
                if (ImGui::IsWindowAppearing())
                    std::snprintf(typed.data(), typed.size(), "%s",
                                  m.to ? squaresShown(m.to->men.squares).c_str() : "");
                ImGui::TextDisabled("%s", TR("On squares (empty: anywhere)"));
                ImGui::SetNextItemWidth(px(180));
                if (ImGui::InputTextWithHint("##to", "h7, g5", typed.data(), typed.size())) {
                    if (typed[0] == 0) {
                        m.to.reset();
                        changed = true;
                    } else if (const auto squares = study::readSquares(typed.data())) {
                        study::Subject t;
                        t.men.squares = *squares;
                        m.to          = t;
                        changed       = true;
                    }
                }
                ImGui::EndPopup();
            }
            // With check, mating, or neither: a condition of the position after it.
            auto check         = std::find_if(s.conditions.begin(), s.conditions.end(), [](const study::Condition& c) {
                return (c.relation == study::Relation::Check || c.relation == study::Relation::Mate) && !c.negated &&
                       c.alternatives.empty();
            });
            const char* suffix = check == s.conditions.end()                 ? TR("(no check)")
                                 : check->relation == study::Relation::Check ? TR("with check")
                                                                             : TR("and mates");
            if (word(suffix, "check", Word::Verb)) {
                if (check == s.conditions.end()) {
                    study::Condition c;
                    c.relation = study::Relation::Check;
                    s.conditions.push_back(c);
                } else if (check->relation == study::Relation::Check) {
                    check->relation = study::Relation::Mate;
                } else {
                    s.conditions.erase(check);
                }
                changed = true;
            }
            // As a player writes it, when it says one kind and one square.
            const int kind =
                !m.from ? -1
                        : kindOf(m.from->variable >= 0 ? p_.variables[static_cast<std::size_t>(m.from->variable)].men
                                                       : m.from->men);
            if (kind >= 0 && m.to && std::popcount(m.to->men.squares) == 1) {
                check           = std::find_if(s.conditions.begin(), s.conditions.end(), [](const study::Condition& c) {
                    return (c.relation == study::Relation::Check || c.relation == study::Relation::Mate) && !c.negated;
                });
                std::string san = kind == 0 ? std::string() : std::string(1, "PNBRQK"[kind]);
                if (m.captures) san += "x";
                san += squareName(std::countr_zero(m.to->men.squares));
                if (check != s.conditions.end()) san += check->relation == study::Relation::Check ? "+" : "#";
                ui::sameLineOrWrap(ImGui::CalcTextSize(san.c_str()).x + px(12), px(4));
                ImGui::TextDisabled("(%s)", localSan(san).c_str());
            }
        }
        // The other conditions of the step's position.
        for (std::size_t i = 0; i < s.conditions.size(); ++i) {
            auto& c = s.conditions[i];
            if (s.move && (c.relation == study::Relation::Check || c.relation == study::Relation::Mate) && !c.negated &&
                c.alternatives.empty())
                continue;
            if (s.move || i > 0)
                phrase(TR("{} and {}"), {nullptr, [&] { condition(c, "s" + std::to_string(i)); }});
            else
                condition(c, "s" + std::to_string(i));
        }
        ImGui::SameLine(0, px(8));
        if (ImGui::SmallButton("×")) drop = true;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TR("Take this step away"));
        ImGui::EndGroup();
        ImGui::PopID();
        return drop;
    }

   private:
    study::Pattern& p_;
    const std::array<int8_t, 64>& men_;
    std::array<bool, study::Pattern::MAX_VARIABLES> introduced_{};
};

}  // namespace

// ---------------------------------------------------------------------------------------------------------------
// The pattern as the fields hold it

study::Pattern fullPattern(const SearchFields& f) {
    study::Pattern p = f.language;
    std::vector<study::Condition> men;
    for (int sq = 0; sq < 64; ++sq) {
        const int v = f.pattern[static_cast<std::size_t>(sq)];
        if (v < 0) continue;
        study::Condition c;
        c.relation      = study::Relation::On;
        c.a.men.pieces  = v == kEmptySquare ? study::Men::EMPTY : study::Men::man(v / 6, v % 6);
        c.a.men.squares = bit(sq);
        men.push_back(c);
    }
    p.conditions.insert(p.conditions.begin(), men.begin(), men.end());
    for (int side = 0; side < 2; ++side) {
        if (!f.only_pawns[static_cast<std::size_t>(side)]) continue;
        study::Condition c;
        c.relation = study::Relation::OnlyPawns;
        c.side     = side;
        p.conditions.push_back(c);
    }
    study::canonicalize(p);
    return p;
}

void setPattern(SearchFields& f, const study::Pattern& p) {
    f.pattern.fill(-1);
    f.only_pawns = {};
    f.language   = p;
    f.language.conditions.clear();
    for (const auto& c : p.conditions) {
        if (study::manOnSquare(c)) {
            const int sq = std::countr_zero(c.a.men.squares);
            const int b  = std::countr_zero(c.a.men.pieces);
            if (f.pattern[static_cast<std::size_t>(sq)] < 0 && b <= 12) {
                f.pattern[static_cast<std::size_t>(sq)] = static_cast<int8_t>(b == 12 ? kEmptySquare : b);
                continue;
            }
        }
        if (c.relation == study::Relation::OnlyPawns && !c.negated && c.alternatives.empty()) {
            f.only_pawns[static_cast<std::size_t>(c.side)] = true;
            continue;
        }
        f.language.conditions.push_back(c);
    }
}

std::optional<study::SearchQuery> App::languageQuery() const {
    const auto& l = db_search_.language;
    if (l.variables.empty() && l.conditions.empty() && l.line.empty()) return std::nullopt;
    auto full = fullPattern(db_search_);
    if (study::contradiction(full)) return std::nullopt;
    return study::SearchQuery{study::RelationQuery{std::make_shared<const study::PatternMatcher>(std::move(full))}};
}

std::optional<study::ManoeuvreQuery> App::routeQuery() {
    const auto& r = db_search_.route;
    if (r.squares.size() < 2) return std::nullopt;
    study::ManoeuvreQuery q;
    q.side           = r.side;
    q.kind           = r.kind;
    q.route          = r.squares;
    q.others_between = r.others_between;
    q.same_man       = r.same_man;
    q.within         = static_cast<uint32_t>(std::max(1, r.within));
    if (r.from_board) {
        study::ScanBoard board;
        if (board.setFen(r.board_fen.empty() ? boardFen() : r.board_fen, board_.chess960)) q.from = board;
    }
    return q;
}

std::optional<study::StructureQuery> App::structureQuery() const {
    const auto& s = db_search_.structure;
    if (!s.taken) return std::nullopt;
    study::StructureQuery q;
    q.pawns      = s.pawns;
    q.distance   = std::clamp(s.distance, 0, 2);
    q.both_sides = s.both_sides;
    q.side       = s.side;
    q.reversed   = s.reversed;
    q.plies      = static_cast<uint32_t>(std::max(1, s.plies));
    return q;
}

void App::patternChanged() {
    pruneVariables(db_search_.language);
    setPattern(db_search_, fullPattern(db_search_));  // a man put on one square goes to the small board
    pattern_text_ = study::writePattern(fullPattern(db_search_));
    pattern_error_.reset();
}

const char* App::presetName(std::string_view key) {
    if (key == "knight-fork") return TR("Knight fork");
    if (key == "pin-on-king") return TR("Pin on the king");
    if (key == "back-rank-mate") return TR("Back-rank mate");
    if (key == "queen-double-attack") return TR("Queen's double attack");
    if (key == "h7-sacrifice") return TR("Sacrifice on h7");
    if (key == "protected-passed-pawn") return TR("Protected passed pawn");
    return "";
}

void App::applyPatternPreset(std::size_t index, bool open_panel) {
    const auto& presets = study::patternPresets();
    if (index >= presets.size()) return;
    const auto read = study::readPattern(presets[index].text);
    if (!read.pattern) return;
    auto pattern  = *read.pattern;
    pattern.title = {presetName(presets[index].key)};
    setPattern(db_search_, pattern);
    pattern_text_ = study::writePattern(fullPattern(db_search_));
    pattern_error_.reset();
    db_pattern_job_.broad_kept = false;
    if (open_panel) openSearchOn(TabPattern);
}

void App::openSearchOn(int tab) {
    page_               = Page::Databases;
    const auto& entries = shelf().entries();
    // The database the board's game comes from, else the one chosen, else the first of the user's.
    if (board_origin_)
        for (std::size_t e = 0; e < entries.size(); ++e)
            if (entries[e].folder == board_origin_->folder && (tab == TabRoute || tab == TabStructure))
                db_selected_ = static_cast<int>(e);
    if (db_selected_ < 0 || static_cast<std::size_t>(db_selected_) >= entries.size()) {
        for (std::size_t e = 0; e < entries.size(); ++e)
            if (!entries[e].source) {
                db_selected_ = static_cast<int>(e);
                break;
            }
    }
    if (db_selected_ < 0) return;
    db_list_        = true;
    db_search_open_ = true;
    search_select_  = tab;
    search_tab_     = tab;
    db_row_         = 0;
}

void App::searchThisManoeuvre() {
    // The man under the mouse when Ctrl+K was pressed, else the one that moved last.
    const int shown = board_.ply;
    auto board      = boardAt(board_.game, board_.chess960, shown);
    std::optional<int> square;
    if (palette_square_ && board.at(*palette_square_) != chess::Piece::NONE) square = palette_square_->index();
    if (!square && shown > 0)
        square = displayedTarget(board_.game.moves[static_cast<std::size_t>(shown - 1)].move).index();
    if (!square || board.at(chess::Square(*square)) == chess::Piece::NONE) {
        boardToast(TR("Point at a man on the board, then Ctrl+K “This manoeuvre”."));
        return;
    }
    const auto piece = board.at(chess::Square(*square));
    auto& r          = db_search_.route;
    r                = SearchFields::Route{};
    r.side           = piece.color() == chess::Color::WHITE ? 0 : 1;
    r.kind           = static_cast<int>(piece.type());
    // Its moves in the game after the position shown; none: those that brought it there.
    std::vector<uint8_t> after{static_cast<uint8_t>(*square)};
    {
        auto b = board;
        int sq = *square;
        for (std::size_t i = static_cast<std::size_t>(shown); i < board_.game.moves.size() && after.size() < 6; ++i) {
            const auto& m     = board_.game.moves[i].move;
            const bool castle = m.typeOf() == chess::Move::CASTLING;
            if (m.from().index() == sq || (castle && m.to().index() == sq)) {
                sq = castle && m.to().index() == sq ? (m.to() > m.from() ? m.from().index() + 1 : m.from().index() - 1)
                                                    : displayedTarget(m).index();
                after.push_back(static_cast<uint8_t>(sq));
            } else if (m.to().index() == sq && !castle) {
                break;  // taken
            }
            b.makeMove(m);
        }
    }
    if (after.size() >= 2) {
        r.squares    = after;
        r.from_board = true;
        r.board_fen  = board.getFen();
    } else {
        // Backwards: where it came from.
        std::vector<uint8_t> before{static_cast<uint8_t>(*square)};
        int sq = *square;
        for (int i = shown - 1; i >= 0 && before.size() < 6; --i) {
            const auto& m = board_.game.moves[static_cast<std::size_t>(i)].move;
            if (displayedTarget(m).index() == sq && m.typeOf() != chess::Move::CASTLING) {
                sq = m.from().index();
                before.insert(before.begin(), static_cast<uint8_t>(sq));
            }
        }
        r.squares = before;
    }
    db_route_job_.broad_kept = false;
    openSearchOn(TabRoute);
}

void App::searchThisStructure() {
    const auto board = boardAt(board_.game, board_.chess960, board_.ply);
    auto& s          = db_search_.structure;
    s.pawns          = {board.pieces(chess::PieceType::PAWN, chess::Color::WHITE).getBits(),
                        board.pieces(chess::PieceType::PAWN, chess::Color::BLACK).getBits()};
    s.taken          = true;
    s.set            = true;
    s.said           = board_.ply > 0 ? trf("after {}", boardMoveLabel(board_.ply)) : std::string(TR("at the start"));
    db_structure_job_.broad_kept = false;
    openSearchOn(TabStructure);
}

const App::SearchJob* App::foundJob() const {
    if (!db_pattern_job_.of.empty()) return &db_pattern_job_;
    if (!db_route_job_.of.empty()) return &db_route_job_;
    if (!db_structure_job_.of.empty() && (db_search_.structure.set || (db_search_open_ && search_tab_ == TabStructure)))
        return &db_structure_job_;
    if (!db_position_job_.of.empty() && (db_search_.position || (db_search_open_ && search_tab_ == TabPosition)))
        return &db_position_job_;
    return nullptr;
}

std::string App::foundMove(const std::shared_ptr<const study::Snapshot>& snapshot, study::GameId id, uint32_t ply) {
    const auto* job = foundJob();
    const auto of   = job ? job->of : std::string();
    if (of != found_moves_of_) {
        found_moves_.clear();
        found_moves_of_ = of;
    }
    if (const auto it = found_moves_.find(id); it != found_moves_.end()) return it->second;
    std::string text = ply == 0 ? std::string(TR("the start")) : std::string();
    if (ply > 0)
        if (const auto at = gameAt(*snapshot, id, ply)) text = at->move;
    if (found_moves_.size() > 4096) found_moves_.clear();
    return found_moves_[id] = text;
}

// ---------------------------------------------------------------------------------------------------------------
// The state of a search

void App::drawSearchState(SearchJob& job, const char* nothing, const char* how) {
    const ui::TextWrap wrap;
    if (job.of.empty()) {
        ImGui::TextDisabled("%s", nothing);
        return;
    }
    const auto count = [](std::size_t n) {
        return trn("{} game", "{} games", static_cast<int>(std::min<std::size_t>(n, 1000000000)), ui::exactCount(n));
    };
    const auto* search   = job.search ? job.search.get() : job.cancelled.get();
    const uint64_t total = search ? std::max<uint64_t>(1, search->total()) : 1;
    // The database imported into meanwhile: what the search covers.
    auto& s = shelf();
    if (s.import() && db_selected_ >= 0 && s.import()->index == static_cast<std::size_t>(db_selected_) && search) {
        const auto& entries = s.entries();
        box(false, trf("“{}” is being imported", entries[static_cast<std::size_t>(db_selected_)].name),
            trf("The search covers what is already committed ({}) and is made again by itself at the end.",
                count(search->total())));
    }
    if (job.search && !job.ready) {
        const uint64_t looked = job.search->searched();
        ImGui::ProgressBar(static_cast<float>(looked) / static_cast<float>(total), ImVec2(-px(90), 0),
                           trf("{} found so far", count(job.ids.size())).c_str());
        ImGui::SameLine();
        if (ImGui::Button(TR("Cancel"))) {
            job.search->cancel();
            job.search->wait();
            job.search->hitsFrom(job.next_part, *job.hits);
            job.ids.clear();
            for (const auto& h : *job.hits) job.ids.push_back(h.id);
            job.stats     = job.search->stats();
            job.searched  = job.search->searched();
            job.cancelled = std::move(job.search);
        }
        // Very broad: the first games are in the list already, the search goes on.
        if (!job.broad_kept && looked > total / 20 && job.ids.size() * 2 > looked) {
            box(false,
                trf("Very broad: {} ({} %)", count(job.ids.size()),
                    static_cast<int>(
                        std::lround(100.0 * static_cast<double>(job.ids.size()) / static_cast<double>(looked)))),
                TR("The first ones are already in the list; the search goes on."));
            if (&job == &db_pattern_job_) {
                if (ImGui::SmallButton(TR("Add a condition"))) ImGui::OpenPopup("##add_condition");
                ImGui::SameLine();
            }
            if (ImGui::SmallButton(TR("Keep it as it is"))) job.broad_kept = true;
        }
    } else if (job.cancelled && !job.search) {
        box(false, trf("Cancelled after looking at {}", count(job.searched)),
            trf("{} found until then, kept in the list.", count(job.ids.size())));
        if (ImGui::Button(TR("Resume (the rest only)"))) {
            job.search = startSearch(job.cancelled->snapshot(), job.cancelled->query(), job.cancelled.get());
            job.cancelled.reset();
        }
    } else if (job.ready) {
        if (job.ids.empty()) {
            box(false, trf("No game in {}", ui::exactCount(total)), how);
        } else {
            ImGui::TextUnformatted(count(job.ids.size()).c_str());
            ImGui::SameLine(0, px(4));
            ImGui::TextDisabled("· %s", job.ms < 1000
                                            ? fmt::format("{} ms", static_cast<int>(std::lround(job.ms))).c_str()
                                            : fmt::format("{:.1f} s", job.ms / 1000).c_str());
            const auto& r = job.stats.results;
            if (const uint64_t scored = r[0] + r[1] + r[2]; scored > 0) {
                ImGui::SameLine(0, px(4));
                ImGui::TextDisabled(
                    "· %s",
                    trf("White scores {} %", static_cast<int>(std::lround(
                                                 100.0 * (static_cast<double>(r[0]) + 0.5 * static_cast<double>(r[1])) /
                                                 static_cast<double>(scored))))
                        .c_str());
            }
            if (!job.broad_kept && job.ids.size() * 2 > total) {
                box(false,
                    trf("Very broad: {} ({} %)", count(job.ids.size()),
                        static_cast<int>(
                            std::lround(100.0 * static_cast<double>(job.ids.size()) / static_cast<double>(total)))),
                    TR("Another condition narrows it."));
                if (&job == &db_pattern_job_) {
                    if (ImGui::SmallButton(TR("Add a condition"))) ImGui::OpenPopup("##add_condition");
                    ImGui::SameLine();
                }
                if (ImGui::SmallButton(TR("Keep it as it is"))) job.broad_kept = true;
            }
        }
    }
}

void App::drawFoundGame(const SearchJob& job, float size) {
    if (job.hits->empty() || !db_view_ || !db_view_->snapshot) return;
    const auto& hits = *job.hits;
    // The game chosen in the list when it was found, else the first found.
    const study::SearchHit* hit = &hits.front();
    if (!db_view_->ids.empty() && db_row_ < db_view_->ids.size()) {
        const auto id = db_view_->ids[db_row_];
        const auto it = std::lower_bound(hits.begin(), hits.end(), id,
                                         [](const study::SearchHit& h, study::GameId g) { return h.id < g; });
        if (it != hits.end() && it->id == id) hit = &*it;
    }
    const auto snap = db_view_->snapshot;
    if (hit->id >= snap->games()) return;
    const auto at = gameAt(*snap, hit->id, hit->ply);
    if (!at) return;
    ImGui::TextDisabled("%s", trf("One of the {}, at the position found", ui::exactCount(job.ids.size())).c_str());
    BoardDecor decor;
    decor.eval_white = -1.0f;
    decor.last_move  = at->last;
    drawBoard(atlas_, at->board, size, decor);
    const auto row   = snap->row(hit->id);
    const auto names = snap->rowIds(hit->id);
    std::string line = fmt::format("{} – {}", snap->shownName(names.white), snap->shownName(names.black));
    if (row.date >> 9) line += fmt::format(", {}", row.date >> 9);
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + std::max(size, px(220)));
    ImGui::TextUnformatted(line.c_str());
    if (!at->move.empty()) ImGui::TextUnformatted(at->move.c_str());
    ImGui::TextDisabled("%s", TR("Enter or a double click in the list opens the game at this move."));
    ImGui::PopTextWrapPos();
}

// ---------------------------------------------------------------------------------------------------------------
// The Pattern tab

void App::drawPatternTab() {
    auto& p      = db_search_;
    bool changed = false;
    ImGui::PushTextWrapPos();
    ImGui::TextDisabled("%s",
                        TR("Conditions, one per line, all true in the same position. A click on a word changes it."));
    ImGui::PopTextWrapPos();

    // The men on squares (preview 1): men put on a board, empty squares, the rest anything; the first condition.
    const float size = std::min(px(220), ImGui::GetContentRegionAvail().x * 0.5f);
    std::array<chess::Piece, 64> pieces;
    pieces.fill(chess::Piece(chess::Piece::NONE));
    BoardDecor decor;
    decor.eval_white = -1.0f;
    for (int sq = 0; sq < 64; ++sq) {
        const int v = p.pattern[static_cast<std::size_t>(sq)];
        if (v >= 0 && v < kEmptySquare) pieces[static_cast<std::size_t>(sq)] = pieceOf(v / 6, v % 6);
        if (v == kEmptySquare) decor.tints.push_back({chess::Square(sq), IM_COL32(236, 140, 80, 150)});
    }
    if (step_by_board_ && step_from_ >= 0) decor.selected = chess::Square(step_from_);
    const ImVec2 top = ImGui::GetCursorScreenPos();
    drawBoard(atlas_, pieces, size, decor);
    if (ImGui::IsItemHovered()) {
        const bool left  = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
        const bool right = ImGui::IsMouseClicked(ImGuiMouseButton_Right);
        if (left || right) {
            if (const auto square = boardSquareAt(ImGui::GetMousePos(), top, size, decor)) {
                const int sq = square->index();
                if (step_by_board_ && left) {
                    // A move played on the board: the man's kind from where it stands, to the square clicked.
                    const int v = p.pattern[static_cast<std::size_t>(sq)];
                    if (step_from_ < 0) {
                        if (v >= 0 && v < kEmptySquare) step_from_ = sq;
                    } else if (sq != step_from_) {
                        const int man = p.pattern[static_cast<std::size_t>(step_from_)];
                        study::Step step;
                        study::PatternMove m;
                        m.from = study::Subject{{study::Men::man(man / 6, man % 6)}, -1};
                        study::Subject to;
                        to.men.squares = bit(sq);
                        m.to           = to;
                        m.captures     = v >= 0 && v < kEmptySquare && v / 6 != man / 6;
                        step.move      = m;
                        if (p.language.line.size() < study::Pattern::MAX_STEPS) p.language.line.push_back(step);
                        step_from_     = -1;
                        step_by_board_ = false;
                        changed        = true;
                    }
                } else {
                    auto& at          = p.pattern[static_cast<std::size_t>(sq)];
                    const int8_t want = ImGui::GetIO().KeyShift ? kEmptySquare : static_cast<int8_t>(pattern_brush_);
                    at                = right || at == want ? int8_t{-1} : want;
                    changed           = true;
                }
            }
        }
    }
    ImGui::SameLine(0, px(10));
    ImGui::BeginGroup();
    // The men a click puts: white then black.
    const float button = px(26);
    for (int side = 0; side < 2; ++side) {
        for (int kind = 5; kind >= 0; --kind) {
            const int man = side * 6 + kind;
            ImVec2 uv0, uv1;
            atlas_.uv(pieceOf(side, kind), uv0, uv1);
            ImGui::PushID(man);
            const bool chosen = pattern_brush_ == man;
            if (chosen) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(ui::ACCENT));
            if (ImGui::ImageButton("##man", atlas_.texture(button), ImVec2(button, button), uv0, uv1))
                pattern_brush_ = man;
            if (chosen) ImGui::PopStyleColor();
            ImGui::PopID();
            if (kind > 0 && kind % 3 != 0) ImGui::SameLine(0, px(2));
        }
    }
    ImGui::PushTextWrapPos();
    ImGui::TextDisabled("%s", step_by_board_ ? TR("Play the step's move: a click on the man, then on its square.")
                                             : TR("A click puts the man chosen (again: takes it away); Shift and a "
                                                  "click: an empty square; the rest: anything."));
    ImGui::PopTextWrapPos();
    ImGui::EndGroup();
    if (ImGui::Checkbox(TR("White has only these pawns"), &p.only_pawns[0])) changed = true;
    if (ImGui::Checkbox(TR("Black has only these pawns"), &p.only_pawns[1])) changed = true;
    {
        struct Example {
            const char* name;
            std::vector<std::pair<int, int>> men;  // square, man
        };
        const auto sq            = [](const char* name) { return (name[1] - '1') * 8 + (name[0] - 'a'); };
        const Example examples[] = {
            {TR("Short castling, pawns intact"),
             {{sq("g1"), 5},
              {sq("f1"), 3},
              {sq("f2"), 0},
              {sq("g2"), 0},
              {sq("h2"), 0},
              {sq("f3"), kEmptySquare},
              {sq("g3"), kEmptySquare},
              {sq("h3"), kEmptySquare}}},
            {TR("Maroczy bind"), {{sq("c4"), 0}, {sq("e4"), 0}, {sq("d6"), 6}}},
            {TR("King's fianchetto"), {{sq("g1"), 5}, {sq("g2"), 2}, {sq("f2"), 0}, {sq("g3"), 0}, {sq("h2"), 0}}},
        };
        for (std::size_t i = 0; i < std::size(examples); ++i) {
            if (i > 0) ui::sameLineOrWrap(examples[i].name);
            if (ImGui::SmallButton(examples[i].name)) {
                p.pattern.fill(-1);
                p.only_pawns = {};
                p.language   = study::Pattern{};
                for (const auto& [square, man] : examples[i].men)
                    p.pattern[static_cast<std::size_t>(square)] = static_cast<int8_t>(man);
                changed = true;
            }
        }
    }

    // The builder's rows: the other conditions of the position.
    ImGui::Spacing();
    Builder builder(p.language, p.pattern);
    for (std::size_t i = 0; i < p.language.conditions.size(); ++i) builder.row(i, p.language.conditions[i]);
    if (builder.remove) p.language.conditions.erase(p.language.conditions.begin() + static_cast<long>(*builder.remove));
    changed         = changed || builder.changed || builder.remove.has_value();
    builder.changed = false;
    builder.remove.reset();
    // A condition more.
    if (ImGui::SmallButton(TR("+ A condition"))) ImGui::OpenPopup("##add_condition");
    ImGui::SameLine();
    ImGui::PushTextWrapPos();
    ImGui::TextDisabled("%s", TR("is on, attacks, defends, pins … on, gives check, mates, is en prise, empty square, "
                                 "then… (a line)"));
    ImGui::PopTextWrapPos();
    if (ImGui::BeginPopup("##add_condition")) {
        using R        = study::Relation;
        const auto add = [&](R r, study::Men a, study::Men b = {}, study::Men c = {}) {
            study::Condition cond;
            cond.relation = r;
            cond.a.men    = a;
            cond.b.men    = b;
            cond.c.men    = c;
            p.language.conditions.push_back(cond);
            changed = true;
        };
        const auto men = [](uint16_t pieces, uint64_t squares = ~uint64_t{0}) {
            study::Men m;
            m.pieces  = pieces;
            m.squares = squares;
            return m;
        };
        using study::Men;
        if (ImGui::MenuItem(TR("is on…"))) add(R::On, men(Men::man(0, study::KNIGHT), bit(18) | bit(21)));
        if (ImGui::MenuItem(TR("attacks…")))
            add(R::Attacks, men(Men::man(0, study::KNIGHT)), men(Men::man(1, study::QUEEN)));
        if (ImGui::MenuItem(TR("defends…")))
            add(R::Defends, men(Men::man(0, study::PAWN)), men(Men::man(0, study::KNIGHT)));
        if (ImGui::MenuItem(TR("pins… on…")))
            add(R::Pins, men(Men::man(0, study::BISHOP)), men(Men::man(1, study::KNIGHT)),
                men(Men::man(1, study::KING)));
        if (ImGui::MenuItem(TR("gives check"))) add(R::Check, {});
        if (ImGui::MenuItem(TR("mates"))) add(R::Mate, {});
        if (ImGui::MenuItem(TR("is en prise"))) add(R::EnPrise, men(Men::man(1, study::QUEEN)));
        if (ImGui::MenuItem(TR("is passed"))) add(R::Passed, men(Men::man(0, study::PAWN)));
        if (ImGui::MenuItem(TR("empty square"))) add(R::On, men(Men::EMPTY, bit(21) | bit(22)));
        if (ImGui::MenuItem(TR("the side to move"))) add(R::BlackToMove, {});
        if (ImGui::MenuItem(TR("then… (a line)"))) {
            study::Step step;
            step.move       = study::PatternMove{};
            step.move->from = study::Subject{{Men::man(0, study::KNIGHT)}, -1};
            if (p.language.line.size() < study::Pattern::MAX_STEPS) p.language.line.push_back(step);
            changed = true;
        }
        ImGui::EndPopup();
    }
    // The presets, in Ctrl+K too.
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", TR("Presets:"));
    const auto& presets               = study::patternPresets();
    static const auto preset_patterns = [] {
        std::vector<study::Pattern> out;
        for (const auto& preset : study::patternPresets()) out.push_back(*study::readPattern(preset.text).pattern);
        return out;
    }();
    const auto now_pattern = fullPattern(p);
    for (std::size_t i = 0; i < presets.size(); ++i) {
        const char* name = presetName(presets[i].key);
        ui::sameLineOrWrap(name);
        const bool on = now_pattern == preset_patterns[i];  // lit while it is still that preset
        if (on) ImGui::PushStyleColor(ImGuiCol_Button, (ui::ACCENT & 0x00ffffff) | 0x60000000);
        if (ImGui::SmallButton(name)) applyPatternPreset(i, false);
        if (on) ImGui::PopStyleColor();
    }

    // The line: a step after another, each a move or a position.
    if (!p.language.line.empty()) {
        ImGui::Spacing();
        ImGui::TextUnformatted(TR("A line of positions"));
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::ACCENT), "%s", TR("Position"));
        ImGui::SameLine(px(72));
        ImGui::PushTextWrapPos();
        ImGui::TextDisabled("%s", TR("the men on the small board and the conditions above"));
        ImGui::PopTextWrapPos();
        // The plies between the steps: the same for all, as the chips set them.
        bool next_ply = true, same_gap = true;
        for (const auto& s : p.language.line) {
            next_ply = next_ply && s.max_gap == 0;
            same_gap = same_gap && s.min_gap == 0 && s.max_gap == p.language.line.front().max_gap;
        }
        const int gap_shown = same_gap && !next_ply ? static_cast<int>(p.language.line.front().max_gap) + 1 : -1;
        if (gap_shown > 0) pattern_gap_ = gap_shown;
        std::optional<std::size_t> drop;
        for (std::size_t i = 0; i < p.language.line.size(); ++i)
            if (builder.step(i, p.language.line[i], gap_shown)) drop = i;
        if (drop) p.language.line.erase(p.language.line.begin() + static_cast<long>(*drop));
        changed = changed || builder.changed || drop.has_value();
        // A step more: a move, a position, or a move played on the small board.
        if (ImGui::SmallButton(TR("+ A step"))) ImGui::OpenPopup("##add_step");
        if (ImGui::BeginPopup("##add_step")) {
            if (ImGui::MenuItem(TR("a move")) && p.language.line.size() < study::Pattern::MAX_STEPS) {
                study::Step step;
                step.move       = study::PatternMove{};
                step.move->from = study::Subject{{study::Men::BLACK}, -1};
                step.min_gap    = 0;
                step.max_gap    = next_ply ? 0 : static_cast<uint32_t>(pattern_gap_ - 1);
                p.language.line.push_back(step);
                changed = true;
            }
            if (ImGui::MenuItem(TR("a position")) && p.language.line.size() < study::Pattern::MAX_STEPS) {
                study::Step step;
                study::Condition c;
                c.relation = study::Relation::Check;
                step.conditions.push_back(c);
                step.max_gap = next_ply ? 0 : static_cast<uint32_t>(pattern_gap_ - 1);
                p.language.line.push_back(step);
                changed = true;
            }
            if (ImGui::MenuItem(TR("a move played on the small board"))) {
                step_by_board_ = true;
                step_from_     = -1;
            }
            ImGui::EndPopup();
        }
        ImGui::SameLine(0, px(10));
        const auto chip = [&](const std::string& label, bool on) {
            if (on) ImGui::PushStyleColor(ImGuiCol_Button, (ui::ACCENT & 0x00ffffff) | 0x60000000);
            const bool clicked = ImGui::SmallButton(label.c_str());
            if (on) ImGui::PopStyleColor();
            return clicked;
        };
        const auto set_gap = [&](int plies) {
            for (auto& s : p.language.line) {
                s.min_gap = 0;
                s.max_gap = static_cast<uint32_t>(std::max(0, plies - 1));
            }
            changed = true;
        };
        if (chip(TR("each step at the next ply"), next_ply)) set_gap(1);
        ui::sameLineOrWrap(px(160));
        if (chip(trf("within {} plies###within", pattern_gap_), gap_shown > 0)) set_gap(pattern_gap_);
        if (gap_shown > 0) {
            ImGui::SameLine(0, px(2));
            if (ImGui::SmallButton("−") && pattern_gap_ > 2) set_gap(--pattern_gap_);
            ImGui::SameLine(0, px(2));
            if (ImGui::SmallButton("+") && pattern_gap_ < static_cast<int>(study::Pattern::MAX_GAP))
                set_gap(++pattern_gap_);
        }
        ImGui::PushTextWrapPos();
        ImGui::TextDisabled("%s", TR("A step says a move (“the bishop takes on h7 with check”) or a position (“the "
                                     "queen on h5”). A move is also set by playing it on the small board."));
        ImGui::PopTextWrapPos();
    }
    if (changed) patternChanged();

    // The same pattern as text (Alt+T): changing one changes the other.
    ImGui::Spacing();
    if (ImGui::SmallButton(pattern_text_open_ ? TR("Hide the text") : TR("Text")))
        pattern_text_open_ = !pattern_text_open_;
    ImGui::SameLine(0, px(4));
    ImGui::TextDisabled("Alt+T");
    const auto written   = study::writePattern(fullPattern(p));
    pattern_text_active_ = false;
    if (pattern_text_open_) {
        ui::TopicRegion language_topic(guide::topics::PatternLanguage);
        ImGui::PushFont(ui::monoFont(), 0.0f);
        const float lines =
            std::max(5.0f, static_cast<float>(std::count(pattern_text_.begin(), pattern_text_.end(), '\n') + 2));
        if (editLines("##pattern_text", pattern_text_,
                      ImVec2(-1, ImGui::GetTextLineHeight() * std::min(lines, 14.0f) + px(8))))
            pattern_typed_at_ = ImGui::GetTime();
        pattern_text_active_ = ImGui::IsItemActive();
        ImGui::PopFont();
        // Read once the typing pauses, or the text is left: a word half typed starts no search.
        if (pattern_typed_at_ > 0 && (!pattern_text_active_ || ImGui::GetTime() - pattern_typed_at_ > 0.4)) {
            pattern_typed_at_ = 0;
            const auto read   = study::readPattern(pattern_text_);
            if (read.pattern) {
                setPattern(p, *read.pattern);
                pattern_error_.reset();
            } else {
                pattern_error_ = read.error;
            }
        }
        if (!pattern_text_active_ && pattern_typed_at_ == 0 && !pattern_error_ && pattern_text_ != written)
            pattern_text_ = written;
        if (pattern_error_) {  // a notice without a button: the text says where to correct it (#141)
            ImGui::PushTextWrapPos();
            drawNotice(Notice{Notice::Level::Error, errorText(*pattern_error_), {}, {}}, "pattern_error");
            ImGui::TextDisabled("%s", TR("The builder keeps the last version that reads; nothing is searched while "
                                         "the text does not read."));
            ImGui::PopTextWrapPos();
        }
        ImGui::PushTextWrapPos();
        ImGui::TextUnformatted(TR("The words, as in CQL"));
        ImGui::TextDisabled("%s",
                            TR("Men in FEN letters (K Q R B N P, lower case for Black; A and a: any man), a square "
                               "after the letter (Nf3), _ for an empty square; attacks, defends, pin from … "
                               "through … to, en prise, passed, check, mate, stalemate, move from … to … "
                               "(capture, promote), line --> for a line of moves (.{0,2} for plies of anything), "
                               "wtm and btm for the side to move, not, or, only P, piece $c in N to name a man. "
                               "The rest of CQL is not read: its line is told."));
        ImGui::PopTextWrapPos();
    } else {
        pattern_text_ = written;
        pattern_error_.reset();
    }

    // What cannot be found is told before searching: nothing is started.
    ImGui::Spacing();
    const auto full = fullPattern(p);
    if (const auto contrary = study::contradiction(full)) {
        const auto& first = full.conditions[static_cast<std::size_t>(contrary->first)];
        std::string text;
        if (contrary->second >= 0) {
            const auto& second = full.conditions[static_cast<std::size_t>(contrary->second)];
            text               = trf("“{}” and “{}” cannot both be true. Told before searching: nothing is started.",
                                     study::conditionText(full, first), study::conditionText(full, second));
        } else {
            text = trf("“{}” can never be true. Told before searching: nothing is started.",
                       study::conditionText(full, first));
        }
        box(true, TR("This pattern cannot find anything"), text);
        // Taking one away: the conditions of the small board or of the builder alike.
        for (const int which : {contrary->first, contrary->second}) {
            if (which < 0) continue;
            const auto& c = full.conditions[static_cast<std::size_t>(which)];
            if (ImGui::SmallButton(trf("Take “{}” away##{}", study::conditionText(full, c), which).c_str())) {
                auto next = full;
                next.conditions.erase(next.conditions.begin() + which);
                setPattern(p, next);
                patternChanged();
                break;
            }
            ImGui::SameLine();
        }
        ImGui::NewLine();
    } else {
        drawSearchState(db_pattern_job_, TR("Put men on the board, or add a condition: the games where they hold."),
                        TR("The pattern is not there. Fewer conditions, or a line with more plies between its steps."));
        if (!db_pattern_job_.of.empty()) {
            ImGui::PushTextWrapPos();
            ImGui::TextDisabled("%s", TR("In each game, the first position of the pattern; each game replayed up to it "
                                         "(the signatures leave out those that cannot have it)."));
            ImGui::PopTextWrapPos();
        }
    }
    drawFoundGame(db_pattern_job_, std::min(px(220), ImGui::GetContentRegionAvail().x * 0.6f));
    ImGui::PushTextWrapPos();
    ImGui::TextDisabled("%s", TR("The searches set add up: this pattern, and a material, and headers."));
    ImGui::PopTextWrapPos();
}

// ---------------------------------------------------------------------------------------------------------------
// The Manoeuvre tab

void App::drawRouteTab() {
    auto& r      = db_search_.route;
    bool changed = false;
    // The Board page's position, the route clicked on it; the most played continuation in grey.
    chess::Board shown;
    shown.set960(board_.chess960);
    if (r.from_board && !r.board_fen.empty())
        shown.setFen(r.board_fen);
    else
        shown = boardAt(board_.game, board_.chess960, board_.ply);
    const float size = std::min(px(240), ImGui::GetContentRegionAvail().x * 0.5f);
    ImGui::BeginGroup();
    ImGui::TextDisabled("%s", TR("The Board page's position, the route clicked"));
    BoardDecor decor;
    decor.eval_white = -1.0f;
    for (std::size_t i = 1; i < r.squares.size(); ++i)
        decor.arrows.push_back(
            {chess::Move::make<chess::Move::NORMAL>(chess::Square(r.squares[i - 1]), chess::Square(r.squares[i])),
             IM_COL32(70, 130, 220, 200)});
    if (!r.squares.empty()) decor.tints.push_back({chess::Square(r.squares[0]), IM_COL32(70, 130, 220, 90)});
    // Where it went next most often: an arrow in grey, not a condition.
    if (db_route_job_.ready || db_route_job_.cancelled) {
        int best = -1;
        for (int s = 0; s < 64; ++s)
            if (db_route_job_.stats.next[static_cast<std::size_t>(s)] &&
                (best < 0 || db_route_job_.stats.next[static_cast<std::size_t>(s)] >
                                 db_route_job_.stats.next[static_cast<std::size_t>(best)]))
                best = s;
        if (best >= 0 && !r.squares.empty())
            decor.arrows.push_back(
                {chess::Move::make<chess::Move::NORMAL>(chess::Square(r.squares.back()), chess::Square(best)),
                 IM_COL32(150, 150, 150, 170)});
    }
    const ImVec2 top = ImGui::GetCursorScreenPos();
    drawBoard(atlas_, shown, size, decor);
    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        if (const auto square = boardSquareAt(ImGui::GetMousePos(), top, size, decor)) {
            const auto piece = shown.at(*square);
            const int sq     = square->index();
            if (r.squares.size() <= 1 && piece != chess::Piece::NONE) {
                // The man: where it starts.
                r.side    = piece.color() == chess::Color::WHITE ? 0 : 1;
                r.kind    = static_cast<int>(piece.type());
                r.squares = {static_cast<uint8_t>(sq)};
                changed   = true;
            } else if (!r.squares.empty() && sq != r.squares.back() && r.squares.size() < 9) {
                r.squares.push_back(static_cast<uint8_t>(sq));
                changed = true;
            }
        }
    }
    ImGui::EndGroup();
    // Backspace takes the last square off.
    if (!ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Backspace) && !r.squares.empty()) {
        r.squares.pop_back();
        changed = true;
    }
    ImGui::SameLine(0, px(12));
    ImGui::BeginGroup();
    ImGui::PushTextWrapPos();
    ImGui::TextDisabled("%s", TR("The man"));
    ImGui::SameLine();
    if (r.squares.empty()) {
        ImGui::TextUnformatted(TR("a click on it, on the board"));
    } else {
        if (ImGui::SmallButton(
                trf("{} that left {}###route_man", manDefinite(r.side, r.kind), squareName(r.squares[0])).c_str()))
            ImGui::OpenPopup("##route_man_menu");
        if (ImGui::BeginPopup("##route_man_menu")) {
            for (int side = 0; side < 2; ++side)
                for (const int kind : {5, 4, 3, 2, 1, 0})
                    if (ImGui::MenuItem(manDefinite(side, kind), nullptr, r.side == side && r.kind == kind)) {
                        r.side  = side;
                        r.kind  = kind;
                        changed = true;
                    }
            ImGui::EndPopup();
        }
    }
    ImGui::TextDisabled("%s", TR("The route"));
    ImGui::SameLine();
    std::string route;
    for (std::size_t i = 0; i < r.squares.size(); ++i) route += (i ? " → " : "") + squareName(r.squares[i]);
    ImGui::TextUnformatted(route.empty() ? TR("a click on each square") : route.c_str());
    ImGui::TextDisabled("%s", TR("(a click on each square; Backspace takes the last off)"));
    ImGui::PopTextWrapPos();
    if (ImGui::Checkbox(TR("Other moves between"), &r.others_between)) changed = true;
    if (ImGui::Checkbox(TR("The same man all along"), &r.same_man)) changed = true;
    fieldPhrase(TR("In {} moves at most"), [&] {
        ImGui::SetNextItemWidth(px(90));
        if (ImGui::InputInt("##route_within", &r.within)) {
            r.within = std::clamp(r.within, 1, 200);
            changed  = true;
        }
    });
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", TR("From"));
    ImGui::SameLine();
    if (ImGui::RadioButton(TR("any position"), !r.from_board)) {
        r.from_board = false;
        changed      = true;
    }
    ui::sameLineOrWrap(ImGui::CalcTextSize(TR("the Board page's position")).x + ImGui::GetFrameHeight() + px(8));
    if (ImGui::RadioButton(TR("the Board page's position"), r.from_board)) {
        r.from_board = true;
        r.board_fen  = boardFen();
        changed      = true;
    }
    ImGui::EndGroup();
    (void)changed;  // the search follows the fields (pollSearches)

    drawSearchState(db_route_job_, TR("Click a man on the board, then the squares of its route."),
                    TR("The route is not there. Other moves between, or more moves."));
    // Where it goes next, and the routes that part from it: a click sets one.
    const auto& st = db_route_job_.stats;
    if (!db_route_job_.of.empty() && (db_route_job_.ready || db_route_job_.cancelled) && !db_route_job_.ids.empty()) {
        std::vector<std::pair<uint64_t, int>> next;
        for (int s = 0; s < 64; ++s)
            if (st.next[static_cast<std::size_t>(s)]) next.emplace_back(st.next[static_cast<std::size_t>(s)], s);
        std::sort(next.rbegin(), next.rend());
        const double found = static_cast<double>(db_route_job_.ids.size());
        if (!next.empty()) {
            std::string where = trf("on {} in {} % of these games", squareName(next[0].second),
                                    static_cast<int>(std::lround(100.0 * static_cast<double>(next[0].first) / found)));
            if (next.size() > 1)
                where = trf("{}, on {} in {} %", where, squareName(next[1].second),
                            static_cast<int>(std::lround(100.0 * static_cast<double>(next[1].first) / found)));
            ImGui::PushTextWrapPos();
            ImGui::TextUnformatted(trf("Next, {}: {}.", sameMan(r.kind), where).c_str());
            ImGui::PopTextWrapPos();
        }
        std::vector<std::pair<uint64_t, std::vector<uint8_t>>> parted;
        for (const auto& [route_, n] : st.parted) parted.emplace_back(n, route_);
        std::sort(parted.begin(), parted.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        if (!parted.empty() && st.started > 0) {
            ImGui::TextDisabled("%s", TR("The neighbouring routes, a click sets them:"));
            for (std::size_t i = 0; i < std::min<std::size_t>(parted.size(), 4); ++i) {
                std::string label;
                for (std::size_t k = 0; k < parted[i].second.size(); ++k)
                    label += (k ? "–" : "") + squareName(parted[i].second[k]);
                label = fmt::format("{} ({} %)##parted{}", label,
                                    static_cast<int>(std::lround(100.0 * static_cast<double>(parted[i].first) /
                                                                 static_cast<double>(st.started))),
                                    i);
                ui::sameLineOrWrap(label.c_str());
                if (ImGui::SmallButton(label.c_str())) r.squares = parted[i].second;
            }
            ImGui::NewLine();
        }
    }
    drawFoundGame(db_route_job_, std::min(px(200), ImGui::GetContentRegionAvail().x * 0.5f));
    ImGui::PushTextWrapPos();
    ImGui::TextDisabled("%s",
                        TR("From the Board page: Ctrl+K “This manoeuvre” takes the man under the mouse and its "
                           "moves in the game. The grey arrow is the most played continuation, not a condition."));
    ImGui::PopTextWrapPos();
}

// ---------------------------------------------------------------------------------------------------------------
// The Structure tab

void App::drawStructureTab() {
    auto& s                = db_search_.structure;
    const auto board_pawns = [&] {
        const auto board = boardAt(board_.game, board_.chess960, board_.ply);
        s.pawns          = {board.pieces(chess::PieceType::PAWN, chess::Color::WHITE).getBits(),
                            board.pieces(chess::PieceType::PAWN, chess::Color::BLACK).getBits()};
        s.said  = board_.ply > 0 ? trf("after {}", boardMoveLabel(board_.ply)) : std::string(TR("at the start"));
        s.taken = true;
    };
    if (!s.taken) board_pawns();
    // The pawns alone; a click changes a square while they are set up.
    const float size = std::min(px(220), ImGui::GetContentRegionAvail().x * 0.48f);
    ImGui::BeginGroup();
    ImGui::TextDisabled("%s", TR("The pawns alone"));
    std::array<chess::Piece, 64> pieces;
    pieces.fill(chess::Piece(chess::Piece::NONE));
    for (int sq = 0; sq < 64; ++sq) {
        if (s.pawns[0] & bit(sq)) pieces[static_cast<std::size_t>(sq)] = pieceOf(0, 0);
        if (s.pawns[1] & bit(sq)) pieces[static_cast<std::size_t>(sq)] = pieceOf(1, 0);
    }
    BoardDecor decor;
    decor.eval_white = -1.0f;
    decor.flipped    = board_.flip;
    const ImVec2 top = ImGui::GetCursorScreenPos();
    drawBoard(atlas_, pieces, size, decor);
    if (structure_editing_ && ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        if (const auto square = boardSquareAt(ImGui::GetMousePos(), top, size, decor)) {
            const int sq = square->index();
            if (sq >= 8 && sq < 56) {
                const bool white = s.pawns[0] & bit(sq), black = s.pawns[1] & bit(sq);
                s.pawns[0] &= ~bit(sq);
                s.pawns[1] &= ~bit(sq);
                if (!white && !black) s.pawns[0] |= bit(sq);
                if (white) s.pawns[1] |= bit(sq);
                s.said = TR("set up");
                s.set  = true;
            }
        }
    }
    // Its name, when the assistant's facts know it.
    std::string names;
    {
        auto b = commentary::Board::empty();
        for (int sq = 0; sq < 64; ++sq) {
            if (s.pawns[0] & bit(sq))
                b.setPieceAt(commentary::square(sq), {commentary::Color::White, commentary::Role::Pawn});
            if (s.pawns[1] & bit(sq))
                b.setPieceAt(commentary::square(sq), {commentary::Color::Black, commentary::Role::Pawn});
        }
        for (const auto& [formation, owner] : commentary::formations(b)) {
            using F          = commentary::Formation;
            const bool white = owner == commentary::Color::White;
            std::string name;
            switch (formation) {
                case F::Carlsbad:
                    name = TR("Carlsbad");
                    break;
                case F::Maroczy:
                    name = white ? TR("Maroczy bind (White's)") : TR("Maroczy bind (Black's)");
                    break;
                case F::Hedgehog:
                    name = white ? TR("hedgehog (White's)") : TR("hedgehog (Black's)");
                    break;
                case F::Stonewall:
                    name = white ? TR("Stonewall (White's)") : TR("Stonewall (Black's)");
                    break;
                case F::KingsideChain:
                case F::QueensideChain:
                    name = TR("pawn chains");
                    break;
                case F::Benoni:
                    name = TR("Benoni");
                    break;
                case F::Isolani:
                    name = white ? TR("isolated queen's pawn (White's)") : TR("isolated queen's pawn (Black's)");
                    break;
            }
            if (names.find(name) == std::string::npos) names += (names.empty() ? "" : " · ") + name;
        }
    }
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + size);
    if (!s.said.empty()) ImGui::TextDisabled("%s", s.said.c_str());
    if (!names.empty()) {
        ImGui::SameLine(0, px(4));
        ImGui::TextUnformatted(names.c_str());
    }
    ImGui::PopTextWrapPos();
    ImGui::EndGroup();
    ImGui::SameLine(0, px(12));
    ImGui::BeginGroup();
    ImGui::TextDisabled("%s", TR("The structure"));
    if (ImGui::SmallButton(TR("Take the Board page's pawns again"))) {
        board_pawns();
        s.set = true;
    }
    ui::sameLineOrWrap(structure_editing_ ? TR("Done") : TR("Set up the pawns…"));
    if (ImGui::SmallButton(structure_editing_ ? TR("Done") : TR("Set up the pawns…")))
        structure_editing_ = !structure_editing_;
    if (structure_editing_) {
        ImGui::PushTextWrapPos();
        ImGui::TextDisabled("%s", TR("A click on a square: a white pawn, a black pawn, nothing."));
        ImGui::PopTextWrapPos();
    }
    const char* distances[] = {TR("Exactly"), TR("One pawn away"), TR("Two pawns away")};
    for (int i = 0; i < 3; ++i) {
        if (i) ImGui::SameLine(0, px(2));
        const bool on = s.distance == i;
        if (on) ImGui::PushStyleColor(ImGuiCol_Button, (ui::ACCENT & 0x00ffffff) | 0x70000000);
        if (ImGui::SmallButton(distances[i])) {
            s.distance = i;
            s.set      = true;
        }
        if (on) ImGui::PopStyleColor();
    }
    if (ImGui::Checkbox(TR("Both sides"), &s.both_sides)) s.set = true;
    if (!s.both_sides) {
        // Each side on the same line while it fits.
        const auto side = [&](const char* label, int value) {
            ui::sameLineOrWrap(ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x +
                               ImGui::CalcTextSize(label, nullptr, true).x);
            if (ImGui::RadioButton(label, s.side == value)) s.side = value;
        };
        side(TR("White's pawns"), 0);
        side(TR("Black's pawns"), 1);
    }
    if (ImGui::Checkbox(TR("The colours reversed too"), &s.reversed)) s.set = true;
    fieldPhrase(TR("Held {} plies at least"), [&] {
        ImGui::SetNextItemWidth(px(90));
        if (ImGui::InputInt("##structure_plies", &s.plies)) {
            s.plies = std::clamp(s.plies, 1, 200);
            s.set   = true;
        }
    });
    if (ImGui::Button(s.set ? TR("Take out of the filter") : TR("Add to the filter"))) s.set = !s.set;
    ImGui::EndGroup();

    drawSearchState(db_structure_job_, TR("The structure of the Board page, or pawns set up."),
                    s.distance < 2 ? TR("The structure is not there: two pawns away, or the colours reversed too.")
                                   : TR("The structure is not there, even two pawns away."));
    const auto& job = db_structure_job_;
    if (!job.of.empty() && (job.ready || job.cancelled) && !job.ids.empty()) {
        const auto& st = job.stats;
        ImGui::PushTextWrapPos();
        if (s.distance > 0)
            ImGui::TextUnformatted(trf("Exactly: {} · {}: {}", ui::exactCount(st.exact), distances[s.distance],
                                       ui::exactCount(job.ids.size()))
                                       .c_str());
        // The openings it is reached by most.
        std::vector<std::pair<uint64_t, uint16_t>> ecos;
        for (const auto& [eco, n] : st.ecos)
            if (eco) ecos.emplace_back(n, eco);
        std::sort(ecos.rbegin(), ecos.rend());
        if (!ecos.empty()) {
            std::string by;
            const double all = static_cast<double>(job.ids.size());
            for (std::size_t i = 0; i < std::min<std::size_t>(ecos.size(), 3); ++i) {
                const auto code = study::ecoText(ecos[i].second);
                auto name       = commentary::ecoName(code);
                auto item       = name.empty() ? code : code + " " + name;
                item = fmt::format("{} ({} %)", item,
                                   static_cast<int>(std::lround(100.0 * static_cast<double>(ecos[i].first) / all)));
                by   = by.empty() ? item : trf("{}, {}", by, item);
            }
            ImGui::TextDisabled("%s", trf("Reached mostly by {}.", by).c_str());
        }
        ImGui::PopTextWrapPos();
        // The variants one pawn away, with their games and White's score: a click sets one.
        if (!st.variants.empty()) {
            std::vector<std::pair<uint64_t, uint32_t>> variants;
            for (const auto& [key, r] : st.variants) variants.emplace_back(r[0] + r[1] + r[2] + r[3], key);
            std::sort(variants.rbegin(), variants.rend());
            // A pawn gone on, added, or on another square: whole phrases, each colour its own.
            const auto describe = [](uint32_t key) {
                const int side = static_cast<int>(key >> 16), gone = static_cast<int>((key >> 8) & 0xff),
                          added  = static_cast<int>(key & 0xff);
                const bool white = side == 0;
                const auto file  = [](int square) { return std::string(1, static_cast<char>('a' + square % 8)); };
                if (gone < 64 && added < 64 && gone % 8 == added % 8)
                    return white ? trf("the white {}-pawn on {}", file(gone), squareName(added))
                                 : trf("the black {}-pawn on {}", file(gone), squareName(added));
                if (gone < 64 && added < 64)
                    return white ? trf("a white pawn on {} instead of {}", squareName(added), squareName(gone))
                                 : trf("a black pawn on {} instead of {}", squareName(added), squareName(gone));
                if (gone < 64)
                    return white ? trf("without the white {}-pawn", file(gone))
                                 : trf("without the black {}-pawn", file(gone));
                return white ? trf("a white pawn more on {}", squareName(added))
                             : trf("a black pawn more on {}", squareName(added));
            };
            if (ImGui::BeginTable(
                    "##variants", 3,
                    ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn(TR("One pawn away"), ImGuiTableColumnFlags_WidthStretch, 1.0f);
                ImGui::TableSetupColumn(TR("Games"), ImGuiTableColumnFlags_WidthFixed, px(70));
                ImGui::TableSetupColumn(TR("White"), ImGuiTableColumnFlags_WidthFixed, px(50));
                ImGui::TableHeadersRow();
                for (std::size_t i = 0; i < std::min<std::size_t>(variants.size(), 6); ++i) {
                    const auto key = variants[i].second;
                    const auto& r  = st.variants.at(key);
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::PushID(static_cast<int>(i));
                    const auto text = describe(key);
                    if (ImGui::Selectable("##variant", false,
                                          ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap)) {
                        // That structure, exactly.
                        const int side = static_cast<int>(key >> 16), gone = static_cast<int>((key >> 8) & 0xff),
                                  added = static_cast<int>(key & 0xff);
                        if (gone < 64) s.pawns[static_cast<std::size_t>(side)] &= ~bit(gone);
                        if (added < 64) s.pawns[static_cast<std::size_t>(side)] |= bit(added);
                        s.said = text;
                        s.set  = true;
                    }
                    ImGui::SameLine(0, 0);
                    fittedText(text);
                    ImGui::PopID();
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(ui::exactCount(variants[i].first).c_str());
                    ImGui::TableNextColumn();
                    const uint64_t scored = r[0] + r[1] + r[2];
                    if (scored)
                        ImGui::Text("%d %%", static_cast<int>(std::lround(
                                                 100.0 * (static_cast<double>(r[0]) + 0.5 * static_cast<double>(r[1])) /
                                                 static_cast<double>(scored))));
                }
                ImGui::EndTable();
            }
        }
    }
    drawFoundGame(db_structure_job_, std::min(px(200), ImGui::GetContentRegionAvail().x * 0.5f));
    ImGui::PushTextWrapPos();
    ImGui::TextDisabled("%s",
                        TR("“One pawn away”: a pawn more, fewer or on another square, on either side; the pieces do "
                           "not count. The name comes from the assistant's facts (Carlsbad, Maroczy, hedgehog, "
                           "Stonewall, the pawn chains, Benoni, isolated queen's pawn)."));
    ImGui::PopTextWrapPos();
}

}  // namespace fcg
