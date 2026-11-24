#include "publish_game.hpp"

#include <algorithm>
#include <functional>
#include <map>
#include <set>

#include <fmt/include/fmt/format.h>

#include <study/game_tree.hpp>

#include "board_text.hpp"
#include "i18n.hpp"
#include "publish_text.hpp"
#include "ui.hpp"

namespace fcg::publish {

std::vector<int> diagramPlies(const PrintGame& game, DiagramsAt at) {
    const auto& moves = game.tree.main.moves;
    const int count   = static_cast<int>(moves.size());
    std::vector<int> out;
    switch (at) {
        case DiagramsAt::None:
            break;
        case DiagramsAt::KeyMoments: {
            for (const int p : game.key_plies)
                if (p >= 1 && p <= count) out.push_back(p);
            if (out.empty()) {
                // The moves marked: ?? and !! first, then ? and !.
                for (const auto& group : {std::vector<uint8_t>{4, 3}, std::vector<uint8_t>{2, 1}})
                    for (int p = 1; p <= count; ++p)
                        for (const auto nag : moves[static_cast<std::size_t>(p - 1)].notes.nags)
                            if (std::find(group.begin(), group.end(), nag) != group.end() &&
                                std::find(out.begin(), out.end(), p) == out.end())
                                out.push_back(p);
            }
            std::sort(out.begin(), out.end());
            if (out.size() > 8) out.resize(8);
            break;
        }
        case DiagramsAt::MarkedComments:
            for (int p = 1; p <= count; ++p) {
                const auto& n = moves[static_cast<std::size_t>(p - 1)].notes;
                if (!n.arrows.empty() || !n.squares.empty() || n.after.find("[#]") != std::string::npos ||
                    std::find(n.nags.begin(), n.nags.end(), uint8_t{220}) != n.nags.end())
                    out.push_back(p);
            }
            break;
        case DiagramsAt::EveryTenMoves: {
            const auto start = boardAt(game.tree.main, game.chess960, 0);
            for (int p = 1; p <= count; ++p) {
                const auto after = boardAt(game.tree.main, game.chess960, p);
                if (after.sideToMove() == chess::Color::WHITE && (after.fullMoveNumber() - 1) % 10 == 0 &&
                    after.fullMoveNumber() > start.fullMoveNumber())
                    out.push_back(p);
            }
            break;
        }
    }
    return out;
}

namespace {

// A game's moves as spans, its variations in brackets after the move they replace, a diagram block after a move of
// the game at `diagrams`.
struct Movetext {
    const GameTree& tree;
    bool chess960;
    const GamePrintOptions* options = nullptr;  // none: comments only (a repertoire)
    std::set<int> diagrams;
    const MoveNotes* start_notes = nullptr;
    appearance::BoardColours colours;
    float diagram_size      = 150;
    std::vector<Block>* out = nullptr;
    std::vector<Span> cur;
    std::map<std::pair<int, int>, std::vector<int>> leaving;
    bool black_first       = false;
    int start_number       = 1;
    bool follow_variations = true;

    Movetext(const GameTree& t, bool c960) : tree(t), chess960(c960) {
        for (std::size_t v = 0; v < tree.variations.size(); ++v)
            leaving[{tree.variations[v].parent, tree.variations[v].from + 1}].push_back(static_cast<int>(v));
        const auto start = boardAt(tree.main, chess960, 0);
        black_first      = start.sideToMove() == chess::Color::BLACK;
        start_number     = start.fullMoveNumber();
    }
    [[nodiscard]] int number(int ply) const { return start_number + (ply - 1 + (black_first ? 1 : 0)) / 2; }
    [[nodiscard]] bool white(int ply) const { return (ply - 1 + (black_first ? 1 : 0)) % 2 == 0; }
    void add(std::string text, Rgba colour, FontId font = FontId::Regular, float scale = 1, std::string cls = {}) {
        Span s(std::move(text), font, colour);
        s.scale = scale;
        s.cls   = std::move(cls);
        cur.push_back(std::move(s));
    }
    void flush() {
        if (cur.empty()) return;
        while (!cur.empty() && !cur.back().text.empty() && cur.back().text.back() == ' ') {
            cur.back().text.pop_back();
            if (cur.back().text.empty()) cur.pop_back();
        }
        Block b;
        b.spans = std::move(cur);
        out->push_back(std::move(b));
        cur.clear();
    }
    void trimSpace() {
        if (!cur.empty() && !cur.back().text.empty() && cur.back().text.back() == ' ') cur.back().text.pop_back();
    }
    [[nodiscard]] std::string moveName(int line, int ply) const {
        const auto& moves = line < 0 ? tree.main.moves : tree.variations[static_cast<std::size_t>(line)].moves;
        const int base    = line < 0 ? 0 : tree.variations[static_cast<std::size_t>(line)].from;
        const auto& m     = moves[static_cast<std::size_t>(ply - base - 1)];
        return fmt::format("{}{}{}", number(ply), white(ply) ? "." : "…", localSan(m.san));
    }

    void line(int which, int depth, int from_ply = 0) {
        bool need_number  = true;
        const auto& moves = which < 0 ? tree.main.moves : tree.variations[static_cast<std::size_t>(which)].moves;
        const int base    = which < 0 ? 0 : tree.variations[static_cast<std::size_t>(which)].from;
        const Rgba colour = depth == 0 ? TEXT_COLOUR : VARIATION_COLOUR;
        for (int k = 0; k < static_cast<int>(moves.size()); ++k) {
            const int ply = base + k + 1;
            if (ply <= from_ply) continue;
            const auto& m = moves[static_cast<std::size_t>(k)];
            if (!m.notes.before.empty()) {
                add(m.notes.before + " ", COMMENT_COLOUR, FontId::Regular, 1, "cm");
                need_number = true;
            }
            if (white(ply))
                add(fmt::format("{}.", number(ply)), MUTED_COLOUR, FontId::Regular, 1, "num");
            else if (need_number)
                add(fmt::format("{}…", number(ply)), MUTED_COLOUR, FontId::Regular, 1, "num");
            need_number     = false;
            std::string san = localSan(m.san);
            for (const auto nag : m.notes.nags)
                if (nag <= 9) san += nagSign(nag);
            add(san + " ", colour, depth == 0 && options ? FontId::SemiBold : FontId::Regular);
            for (const auto nag : m.notes.nags)
                if (nag > 9) add(nagSign(nag) + " ", colour);
            if (options && options->evals && m.score)
                add(evalText(*m.score, m.mover) + " ", MUTED_COLOUR, FontId::Regular, 0.85f, "ev");
            if (!m.notes.after.empty()) {
                std::string comment = m.notes.after;
                for (std::size_t at; (at = comment.find("[#]")) != std::string::npos;) comment.erase(at, 3);
                while (!comment.empty() && comment.back() == ' ') comment.pop_back();
                if (!comment.empty()) {
                    add(comment + " ", COMMENT_COLOUR, FontId::Regular, 1, "cm");
                    need_number = true;
                }
            }
            if (options && options->assistant && !m.notes.assistant.empty()) {
                add(std::string(TR("Assistant:")) + " ", ASSISTANT_COLOUR, FontId::SemiBold, 0.85f, "as");
                add(m.notes.assistant + " ", ASSISTANT_COLOUR, FontId::Italic, 1, "as");
                need_number = true;
            }
            if (follow_variations)
                if (const auto v = leaving.find({which, ply}); v != leaving.end()) {
                    for (const int var : v->second) {
                        add("(", VARIATION_COLOUR, FontId::Regular, 1, "var");
                        line(var, depth + 1);
                        trimSpace();
                        add(") ", VARIATION_COLOUR, FontId::Regular, 1, "var");
                    }
                    need_number = true;
                }
            if (depth == 0 && which < 0 && diagrams.count(ply)) {
                flush();
                Block d;
                d.kind         = Block::Kind::Diagram;
                d.diagram      = diagramAt(tree.main, chess960, ply, start_notes, colours);
                d.caption      = trf("after {}", moveName(-1, ply));
                d.diagram_size = diagram_size;
                out->push_back(std::move(d));
                need_number = true;
            }
        }
    }
};

}  // namespace

Document gameDocument(const PrintGame& game, const GamePrintOptions& options, const DocumentLook& look) {
    Document d;
    d.letter  = options.letter;
    d.columns = std::clamp(options.columns, 1, 2);
    d.pieces  = look.pieces;
    d.notice  = look.notice;
    d.colours = look.colours;
    d.lang    = look.lang;
    HtmlGame h;
    h.tree         = game.tree;
    h.chess960     = game.chess960;
    h.white        = game.white;
    h.black        = game.black;
    d.title        = gameTitle(h);
    const auto sub = gameSubtitle(h);
    d.header       = sub.empty() ? d.title : trf("{} · {}", d.title, sub);
    if (options.headers) {
        d.blocks.push_back(title(gameTitle(h)));
        if (!sub.empty()) d.blocks.push_back(subtitle(sub));
    }
    Movetext m(game.tree, game.chess960);
    m.options      = &options;
    const auto at  = diagramPlies(game, options.diagrams);
    m.diagrams     = std::set<int>(at.begin(), at.end());
    m.start_notes  = &game.start_notes;
    m.colours      = look.colours;
    m.diagram_size = d.columns == 2 ? 150.0f : 200.0f;
    m.out          = &d.blocks;
    if (!game.tree.main.pre_comment.empty()) m.add(game.tree.main.pre_comment + " ", COMMENT_COLOUR);
    m.line(-1, 0);
    m.add(resultText(game.tree.main.result), TEXT_COLOUR, FontId::SemiBold);
    m.flush();
    return d;
}

std::vector<Span> lineSpans(const std::vector<std::string>& sans, int game, int first_ply, bool black_first,
                            int first_number) {
    std::vector<Span> out;
    for (std::size_t i = 0; i < sans.size(); ++i) {
        const int half   = static_cast<int>(i) + (black_first ? 1 : 0);
        const int number = first_number + half / 2;
        if (half % 2 == 0) {
            Span n(fmt::format("{}.", number), FontId::Regular, MUTED_COLOUR);
            n.cls = "num";
            out.push_back(std::move(n));
        } else if (i == 0) {
            Span n(fmt::format("{}…", number), FontId::Regular, MUTED_COLOUR);
            n.cls = "num";
            out.push_back(std::move(n));
        }
        Span s(localSan(sans[i]) + (i + 1 < sans.size() ? " " : ""));
        if (game >= 0) {
            s.game = game;
            s.node = first_ply + static_cast<int>(i);
        }
        out.push_back(std::move(s));
    }
    return out;
}

std::optional<HtmlGame> lineGame(const std::vector<std::string>& sans) {
    HtmlGame g;
    chess::Board board;
    for (const auto& san : sans) {
        const auto move = chess::uci::parseSan(board, san);
        if (move == chess::Move::NO_MOVE) return std::nullopt;
        ReplayMove m;
        m.san   = chess::uci::moveToSan(board, move);
        m.move  = move;
        m.mover = board.sideToMove();
        g.tree.main.moves.push_back(std::move(m));
        board.makeMove(move);
    }
    g.tree.main.result = "*";
    return g;
}

Document repertoireDocument(const std::string& name, chess::Color side, const GameTree& lines,
                            const std::optional<study::Coverage>& coverage, bool letter, const DocumentLook& look) {
    Document d;
    d.letter                = letter;
    d.pieces                = look.pieces;
    d.notice                = look.notice;
    d.colours               = look.colours;
    d.lang                  = look.lang;
    d.title                 = name;
    const std::size_t count = lines.variations.size() + (lines.main.moves.empty() ? 0 : 1);
    d.header                = trf("{} · {}", name, side == chess::Color::WHITE ? TR("White") : TR("Black"));
    d.blocks.push_back(title(name));
    d.blocks.push_back(subtitle(trf("{} · {}", side == chess::Color::WHITE ? TR("White") : TR("Black"),
                                    trn("{} line", "{} lines", count, count))));
    Movetext m(lines, false);
    m.follow_variations = false;
    m.out               = &d.blocks;
    m.colours           = look.colours;
    // The main line, then each line under the one it leaves, in the order they leave it; a diagram at each branching.
    std::map<int, std::vector<int>> children;
    for (std::size_t v = 0; v < lines.variations.size(); ++v)
        children[lines.variations[v].parent].push_back(static_cast<int>(v));
    for (auto& [parent, list] : children)
        std::stable_sort(list.begin(), list.end(), [&](int a, int b) {
            return lines.variations[static_cast<std::size_t>(a)].from <
                   lines.variations[static_cast<std::size_t>(b)].from;
        });
    d.blocks.push_back(heading(TR("The main line")));
    m.line(-1, 0);
    m.flush();
    std::set<std::pair<int, int>> drawn;  // the branchings with their diagram
    const std::function<void(int)> under = [&](int parent) {
        const auto it = children.find(parent);
        if (it == children.end()) return;
        for (const int v : it->second) {
            const auto& var = lines.variations[static_cast<std::size_t>(v)];
            const auto full = lineOf(lines, v);
            std::string where;
            if (var.from > 0) {
                const auto before = boardAt(full, false, var.from - 1);
                const auto& mv    = full.moves[static_cast<std::size_t>(var.from - 1)];
                where = fmt::format("{}{}{}", before.fullMoveNumber(), mv.mover == chess::Color::WHITE ? "." : "…",
                                    localSan(mv.san));
            }
            d.blocks.push_back(heading(where.empty() ? std::string(TR("From the start")) : trf("After {}", where)));
            // The position where the lines part, once.
            if (drawn.insert({var.parent, var.from}).second) {
                Block b;
                b.kind         = Block::Kind::Diagram;
                b.diagram      = diagramAt(full, false, var.from, nullptr, look.colours);
                b.diagram_size = 140;
                b.caption      = where.empty() ? std::string(TR("the start")) : trf("after {}", where);
                d.blocks.push_back(std::move(b));
            }
            m.line(v, 1);
            m.flush();
            under(v);
        }
    };
    under(-1);
    if (coverage) {
        d.blocks.push_back(heading(TR("Its coverage and its gaps")));
        d.blocks.push_back(
            paragraph(trf("{} % of the games along the repertoire find an answer to the end of its lines.",
                          static_cast<int>(std::lround(100 * coverage->covered)))));
        if (!coverage->gaps.empty()) {
            Block t;
            t.kind        = Block::Kind::Table;
            t.table.head  = {TR("Reached"), TR("The line"), TR("Missing"), TR("Games")};
            t.table.align = {1, 0, 0, 1};
            for (std::size_t i = 0; i < coverage->gaps.size() && i < 60; ++i) {
                const auto& g = coverage->gaps[i];
                std::string moves;
                for (std::size_t k = 0; k < g.line.size(); ++k)
                    moves += (k % 2 == 0 ? std::to_string(k / 2 + 1) + "." : std::string()) + localSan(g.line[k]) + " ";
                if (!moves.empty()) moves.pop_back();
                t.table.rows.push_back(
                    {{fmt::format("{:.1f} %", 100 * g.reach)},
                     {moves},
                     {g.move.empty() ? std::string(TR("no move of yours here")) : "→ " + localSan(g.move)},
                     {ui::exactCount(g.games)}});
            }
            d.blocks.push_back(std::move(t));
        }
    }
    return d;
}

}  // namespace fcg::publish
