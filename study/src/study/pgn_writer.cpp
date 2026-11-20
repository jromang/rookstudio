#include "pgn_writer.hpp"

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <string_view>
#include <utility>
#include <vector>

#include <fmt/include/fmt/format.h>

#include <tour/pgn_file.hpp>

#include "pgn_lexer.hpp"
#include "pgn_reader.hpp"

namespace fcg::study {

namespace {

// The score as a tournament's comment writes it: +0.35, -1.20, 0.00, +M5 (a mate in plies).
std::string commentScore(const fastchess::Score& score) {
    if (score.type == fastchess::ScoreType::MATE) {
        const int64_t plies = score.value > 0 ? score.value * 2 - 1 : -score.value * 2;
        return fmt::format("{}M{}", score.value > 0 ? "+" : "-", plies);
    }
    if (score.value == 0) return "0.00";
    return fmt::format("{:+.2f}", static_cast<double>(score.value) / 100.0);
}

// [%eval]'s value: White's point of view, in pawns, or #N for a mate in N moves.
std::string evalValue(const ReplayMove& m) {
    const int sign = m.mover == chess::Color::WHITE ? 1 : -1;
    std::string out;
    if (m.score->type == fastchess::ScoreType::MATE)
        out = fmt::format("#{}", sign * m.score->value);
    else
        out = fmt::format("{:.2f}", static_cast<double>(sign * m.score->value) / 100.0);
    if (m.depth > 0) out += fmt::format(",{}", m.depth);
    return out;
}

// h:mm:ss, with the milliseconds when there are some.
std::string duration(int64_t ms) {
    const int64_t s = ms / 1000;
    auto out        = fmt::format("{}:{:02}:{:02}", s / 3600, (s / 60) % 60, s % 60);
    if (ms % 1000 != 0) out += fmt::format(".{:03}", ms % 1000);
    return out;
}

std::string square(int index) {
    return std::string{static_cast<char>('a' + index % 8), static_cast<char>('1' + index / 8)};
}

std::string marks(const std::vector<BoardMark>& list, bool arrows) {
    std::string out;
    for (const auto& m : list) {
        if (!out.empty()) out += ',';
        out += m.colour;
        out += square(m.from);
        if (arrows) out += square(m.to);
    }
    return out;
}

// The words of the moves, wrapped at 80 characters. A variation's brackets are glued to what they hold: a "(" that
// would end a line goes to the next with what it opens, a ")" stays with the move it closes. A comment's words wrap
// like the others, and its text is never changed.
class Lines {
   public:
    explicit Lines(std::string& out) : out_(out) {}

    void put(std::string word, bool open = false, bool close = false) {
        const bool glued = !cur_.empty() && (close || cur_.back().open);
        std::size_t add  = word.size() + (cur_.empty() || glued ? 0 : 1);
        if (!cur_.empty() && !close && len_ + add > 80) {
            std::vector<Word> carry;
            if (cur_.back().open) {
                carry.push_back(std::move(cur_.back()));
                cur_.pop_back();
            }
            if (!cur_.empty()) lines_.push_back(std::move(cur_));
            cur_ = std::move(carry);
            len_ = cur_.empty() ? 0 : cur_.back().text.size();
            add  = word.size();
        }
        len_ += add;
        cur_.push_back(Word{std::move(word), open, close});
    }
    void comment(const std::string& text) {
        for (auto& w : commentWords(text)) put(std::move(w));
    }
    // A comment glued to the word before it ("Nf3 {+0.35/18"): a move and its engine's comment start on the same
    // line, as other readers expect them; the comment's other words wrap as any.
    void commentAfter(std::string word, const std::string& text) {
        auto words    = commentWords(text);
        words.front() = std::move(word) + " " + words.front();
        for (auto& w : words) put(std::move(w));
    }
    void finish() {
        if (!cur_.empty()) lines_.push_back(std::move(cur_));
        for (const auto& words : lines_) {
            for (std::size_t i = 0; i < words.size(); ++i) {
                if (i > 0 && !words[i - 1].open && !words[i].close) out_ += ' ';
                out_ += words[i].text;
            }
            out_ += '\n';
        }
    }

   private:
    // A comment's words in its braces, a [%...] command kept whole: other programs may not read one cut by a line end.
    // What touches a command stays with it ("[%emt 0:00:01.234]," as a tournament writes it).
    static std::vector<std::string> commentWords(const std::string& text) {
        std::vector<std::string> words;
        std::size_t at = 0;
        while (at < text.size()) {
            if (text[at] == ' ') {
                ++at;
                continue;
            }
            auto end = at;
            while (end < text.size() && text[end] != ' ') {
                const auto close = text.compare(end, 2, "[%") == 0 ? text.find(']', end) : std::string::npos;
                end              = close == std::string::npos ? end + 1 : close + 1;
            }
            words.push_back(text.substr(at, end - at));
            at = end;
        }
        if (words.empty()) words.emplace_back();
        // A brace comment cannot hold a "}" (a ";" comment can): written as ")".
        for (auto& w : words) std::replace(w.begin(), w.end(), '}', ')');
        words.front().insert(0, "{");
        words.back() += '}';
        return words;
    }

    struct Word {
        std::string text;
        bool open  = false;  // a variation's "("
        bool close = false;  // its ")"
    };
    std::string& out_;
    std::vector<std::vector<Word>> lines_;
    std::vector<Word> cur_;
    std::size_t len_ = 0;
};

}  // namespace

namespace {

std::string joined(const std::vector<std::string>& parts) {
    std::string out;
    for (const auto& p : parts) {
        if (!out.empty()) out += ' ';
        out += p;
    }
    return out;
}

}  // namespace

namespace {

bool sameScore(const std::optional<fastchess::Score>& a, const std::optional<fastchess::Score>& b) {
    return a.has_value() == b.has_value() && (!a || (a->type == b->type && a->value == b->value));
}

// What the tournament's comment read on a move gives back, when it still says what the move holds: read again, the
// same data (a score changed since, an annotation's evaluation in its place, and it is written anew). Its commands
// are then its own, not the notes'.
struct HeldComment {
    bool clk = false, eval = false, emt = false;
};
std::optional<HeldComment> heldComment(const ReplayMove& m) {
    if (m.engine_comment.empty()) return std::nullopt;
    ReplayMove probe;
    probe.mover = m.mover;
    (void)applyMoveComment(m.engine_comment, probe);
    if (probe.engine_comment != m.engine_comment) return std::nullopt;
    if ((probe.clock_ms && probe.clock_ms != m.clock_ms) ||
        (probe.notes.emt_ms && probe.notes.emt_ms != m.notes.emt_ms))
        return std::nullopt;
    if (probe.book != m.book || !sameScore(probe.score, m.score) || probe.depth != m.depth ||
        probe.time_ms != m.time_ms || probe.notes.eval_tag != m.notes.eval_tag || probe.nodes != m.nodes ||
        probe.seldepth != m.seldepth || probe.nps != m.nps || probe.tbhits != m.tbhits || probe.pv != m.pv ||
        probe.decision_ms != m.decision_ms || probe.wdl != m.wdl || probe.fields != m.fields)
        return std::nullopt;
    return HeldComment{probe.clock_ms.has_value(), probe.notes.eval_tag, probe.notes.emt_ms.has_value()};
}

// The fields after a tournament's score and time, those it wrote ("…, n=1234, pv=\"e2e4 e7e5\""), from the move's data.
std::string engineFields(const ReplayMove& m) {
    std::string out;
    if (m.fields & PGN_NODES) out += fmt::format(", n={}", m.nodes);
    if (m.fields & PGN_SELDEPTH) out += fmt::format(", sd={}", m.seldepth);
    if (m.fields & PGN_NPS) out += fmt::format(", nps={}", m.nps);
    if (m.fields & PGN_TBHITS) out += fmt::format(", tbhits={}", m.tbhits);
    if (m.fields & PGN_PV) out += fmt::format(", pv=\"{}\"", m.pv);
    if ((m.fields & PGN_WDL) && m.wdl) out += fmt::format(", wdl={}/{}/{}", (*m.wdl)[0], (*m.wdl)[1], (*m.wdl)[2]);
    if ((m.fields & PGN_DECISION) && m.decision_ms)
        out += fmt::format(", dt={:.3f}s", static_cast<double>(*m.decision_ms) / 1000.0);
    return out;
}

std::string engineText(const ReplayMove& m, bool held) {
    if (held) return m.engine_comment;
    if (m.book) return "book";
    if (!m.score || m.notes.eval_tag) return {};
    auto out = commentScore(*m.score) + "/" + std::to_string(m.depth);
    if (m.time_ms >= 0) out += fmt::format(" {:.3f}s", static_cast<double>(m.time_ms) / 1000.0);
    return out + engineFields(m);
}

std::string noteText(const ReplayMove& m, bool assistant_mark, const std::optional<HeldComment>& held) {
    std::vector<std::string> parts;
    if (m.clock_ms && !(held && held->clk)) parts.push_back("[%clk " + duration(*m.clock_ms) + "]");
    if (m.notes.eval_tag && m.score && !(held && held->eval)) parts.push_back("[%eval " + evalValue(m) + "]");
    if (m.notes.emt_ms && !(held && held->emt)) parts.push_back("[%emt " + duration(*m.notes.emt_ms) + "]");
    if (!m.notes.arrows.empty()) parts.push_back("[%cal " + marks(m.notes.arrows, true) + "]");
    if (!m.notes.squares.empty()) parts.push_back("[%csl " + marks(m.notes.squares, false) + "]");
    for (const auto& c : m.notes.commands) parts.push_back(c);
    if (!m.notes.after.empty()) {
        parts.push_back(m.notes.after);
    } else if (!m.notes.assistant.empty()) {
        // The assistant's comment alone: one comment, as the assistant's were always written.
        if (assistant_mark) parts.emplace_back(ASSISTANT_MARK);
        parts.push_back(m.notes.assistant);
    }
    return joined(parts);
}

}  // namespace

std::string noteComment(const ReplayMove& m, bool assistant_mark) {
    return noteText(m, assistant_mark, heldComment(m));
}

std::string engineComment(const ReplayMove& m) { return engineText(m, heldComment(m).has_value()); }

std::string assistantComment(const ReplayMove& m, bool assistant_mark) {
    if (m.notes.after.empty() || m.notes.assistant.empty()) return {};
    return assistant_mark ? joined({std::string(ASSISTANT_MARK), m.notes.assistant}) : m.notes.assistant;
}

std::string writePgnGame(const GameTree& tree, const PgnWriteOptions& options) {
    const auto& game    = tree.main;
    const bool chess960 = options.chess960;
    const auto tag      = [](const std::string& name, const std::string& value) {
        return fmt::format("[{} \"{}\"]\n", name, tour::pgnTagValue(value));
    };
    const auto value = [&](const char* name, const char* fallback) {
        const auto v = game.header(name);
        return v.empty() ? std::string(fallback) : v;
    };
    // A result is one of the four the movetext can end with; anything else read in a tag is "*".
    std::string result = game.result == "\xC2\xBD-\xC2\xBD" ? "1/2-1/2" : game.result;
    if (result != "1-0" && result != "0-1" && result != "1/2-1/2") result = "*";
    const std::string start = game.start_fen.empty() ? std::string(chess::constants::STARTPOS) : game.start_fen;

    std::string out;
    out += tag("Event", value("Event", "?"));
    out += tag("Site", value("Site", "?"));
    out += tag("Date", value("Date", "????.??.??"));
    out += tag("Round", value("Round", "-"));
    out += tag("White", value("White", "?"));
    out += tag("Black", value("Black", "?"));
    out += tag("Result", result);
    static constexpr std::string_view written[] = {"Event", "Site",   "Date",  "Round", "White",
                                                   "Black", "Result", "SetUp", "FEN",   "PlyCount"};
    for (const auto& [name, v] : game.headers) {
        if (std::find(std::begin(written), std::end(written), name) != std::end(written)) continue;
        if (name == "Variant" && chess960) continue;
        out += tag(name, v);
    }
    if (chess960) out += tag("Variant", "Chess960");
    if (chess960 || start != chess::constants::STARTPOS) {
        out += tag("SetUp", "1");
        out += tag("FEN", start);
    }
    out += tag("PlyCount", std::to_string(game.moves.size()));
    out += "\n";

    chess::Board board;
    board.set960(chess960);
    board.setFen(start);
    Lines lines(out);
    if (!game.pre_comment.empty()) lines.comment(game.pre_comment);

    // A line's moves from absolute ply `base`, each followed by the variations that replace it.
    const std::function<void(int, int, chess::Board)> write = [&](int which, int base, chess::Board at) {
        const auto& moves = which < 0 ? game.moves : tree.variations[static_cast<std::size_t>(which)].moves;
        bool number       = true;
        for (std::size_t i = 0; i < moves.size(); ++i) {
            const auto& m = moves[i];
            const int ply = base + static_cast<int>(i);
            if (!m.notes.before.empty()) {
                lines.comment(m.notes.before);
                number = true;
            }
            if (at.sideToMove() == chess::Color::WHITE)
                lines.put(fmt::format("{}.", at.fullMoveNumber()));
            else if (number)
                lines.put(fmt::format("{}...", at.fullMoveNumber()));
            number = false;

            std::string san = m.move == chess::Move::NULL_MOVE ? "--" : chess::uci::moveToSan(at, m.move);
            // The first of the symbols !, ?, !!, ??, !?, ?! glued to the move, the others as $n after it.
            bool glued = false;
            std::vector<int> nags;
            for (const auto nag : m.notes.nags) {
                if (!glued && !nagGlyph(nag).empty()) {
                    san += nagGlyph(nag);
                    glued = true;
                } else {
                    nags.push_back(nag);
                }
            }
            // The engine's comment right after the move, its first word on the move's line: the tournament's as it was
            // read, else made from the move's data (#87).
            const auto held  = heldComment(m);
            std::string lead = engineText(m, held.has_value());
            std::string note = noteText(m, options.assistant_mark, held);
            // Notes that will read back as a tournament's comment ([%eval], [%clk] and [%emt] alone) take its place, so
            // that what is written reads back and is written again the same.
            if (lead.empty() && nags.empty() && !note.empty()) {
                ReplayMove probe;
                probe.mover = m.mover;
                (void)applyMoveComment(note, probe);
                if (!probe.engine_comment.empty()) lead = std::exchange(note, {});
            }
            // What the caller adds after the move (the analysis's evaluation and symbol), glued to it when it has no
            // engine's comment.
            std::string extra = which < 0 && options.annotate ? options.annotate(ply + 1) : std::string();
            if (lead.empty()) {
                lines.put(extra.empty() ? std::move(san) : san + " " + extra);
            } else {
                lines.commentAfter(std::move(san), lead);
                if (!extra.empty()) lines.put(std::move(extra));
            }
            for (const int nag : nags) lines.put(fmt::format("${}", nag));
            if (!note.empty()) {
                lines.comment(note);
                number = true;
            }
            // The assistant's beside it, a comment of its own (#60).
            if (const auto said = assistantComment(m, options.assistant_mark); !said.empty()) lines.comment(said);
            for (std::size_t v = 0; v < tree.variations.size(); ++v) {
                const auto& var = tree.variations[v];
                if (var.parent != which || var.from != ply || var.moves.empty()) continue;
                lines.put("(", true);
                write(static_cast<int>(v), ply, at);
                lines.put(")", false, true);
                number = true;
            }
            if (m.move == chess::Move::NULL_MOVE)
                at.makeNullMove();
            else
                at.makeMove<true>(m.move);
        }
    };
    write(-1, 0, board);
    lines.put(result);
    lines.finish();
    out += '\n';
    return out;
}

}  // namespace fcg::study
