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
        // Its words, a [%...] command kept whole: other programs may not read one cut by a line end.
        std::vector<std::string> words;
        std::size_t at = 0;
        while (at < text.size()) {
            if (text[at] == ' ') {
                ++at;
                continue;
            }
            auto end = text.compare(at, 2, "[%") == 0 ? text.find(']', at) : std::string::npos;
            end      = end == std::string::npos ? text.find(' ', at) : end + 1;
            if (end == std::string::npos) end = text.size();
            words.push_back(text.substr(at, end - at));
            at = end;
        }
        if (words.empty()) words.emplace_back();
        // A brace comment cannot hold a "}" (a ";" comment can): written as ")".
        for (auto& w : words) std::replace(w.begin(), w.end(), '}', ')');
        words.front().insert(0, "{");
        words.back() += '}';
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

std::string noteComment(const ReplayMove& m, bool assistant_mark) {
    std::vector<std::string> parts;
    if (m.clock_ms) parts.push_back("[%clk " + duration(*m.clock_ms) + "]");
    if (m.notes.eval_tag && m.score) parts.push_back("[%eval " + evalValue(m) + "]");
    if (m.notes.emt_ms) parts.push_back("[%emt " + duration(*m.notes.emt_ms) + "]");
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
            // A move and its engine comment on the same line, as other readers expect them.
            std::string word = san;
            if (m.book) {
                word += " {book}";
            } else if (m.score && !m.notes.eval_tag) {
                word += " {" + commentScore(*m.score) + "/" + std::to_string(m.depth);
                if (m.time_ms >= 0) word += fmt::format(" {:.3f}s", static_cast<double>(m.time_ms) / 1000.0);
                word += "}";
            }
            if (which < 0 && options.annotate) {
                if (const auto extra = options.annotate(ply + 1); !extra.empty()) word += " " + extra;
            }
            lines.put(std::move(word));
            for (const int nag : nags) lines.put(fmt::format("${}", nag));
            if (const auto note = noteComment(m, options.assistant_mark); !note.empty()) {
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
