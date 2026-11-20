#include "pgn_reader.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

#include <tour/chess_text.hpp>

#include "pgn_lexer.hpp"

namespace fcg::study {

namespace {

bool validUtf8(std::string_view s) {
    std::size_t i = 0;
    while (i < s.size()) {
        const auto c = static_cast<unsigned char>(s[i]);
        int extra    = 0;
        if (c < 0x80) {
            ++i;
            continue;
        }
        if ((c & 0xE0) == 0xC0 && c >= 0xC2)
            extra = 1;
        else if ((c & 0xF0) == 0xE0)
            extra = 2;
        else if ((c & 0xF8) == 0xF0 && c <= 0xF4)
            extra = 3;
        else
            return false;
        if (i + static_cast<std::size_t>(extra) >= s.size()) return false;
        for (int k = 1; k <= extra; ++k) {
            if ((static_cast<unsigned char>(s[i + k]) & 0xC0) != 0x80) return false;
        }
        i += static_cast<std::size_t>(extra) + 1;
    }
    return true;
}

void appendUtf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

// Windows-1252's characters 0x80 to 0x9F (the others are Latin-1's).
constexpr std::array<uint16_t, 32> CP1252{0x20AC, 0x81,   0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
                                          0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x8D,   0x017D, 0x8F,
                                          0x90,   0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
                                          0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x9D,   0x017E, 0x0178};

std::string unescapeTag(std::string_view value) {
    std::string out;
    out.reserve(value.size());
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '\\' && i + 1 < value.size() && (value[i + 1] == '"' || value[i + 1] == '\\')) ++i;
        out += value[i];
    }
    return utf8Text(out);
}

// Runs of spaces and line ends as one space, none at either end: a comment as it reads, however it was wrapped.
std::string tidy(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    bool space = false;
    for (const char c : text) {
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            space = !out.empty();
            continue;
        }
        if (space) out += ' ';
        space = false;
        out += c;
    }
    return out;
}

void join(std::string& to, const std::string& text) {
    if (text.empty()) return;
    if (!to.empty()) to += ' ';
    to += text;
}

int squareIndex(std::string_view s) {
    if (s.size() != 2 || s[0] < 'a' || s[0] > 'h' || s[1] < '1' || s[1] > '8') return -1;
    return (s[1] - '1') * 8 + (s[0] - 'a');
}

// "Ge2e4,Rd1d8" (arrows) or "Gd4,Ra1" (squares). False when one is no mark: the command is then kept as it is.
bool readMarks(std::string_view list, bool arrows, std::vector<BoardMark>& out) {
    std::vector<BoardMark> marks;
    while (!list.empty()) {
        const auto comma = list.find(',');
        auto item        = tidy(list.substr(0, comma));
        list             = comma == std::string_view::npos ? std::string_view{} : list.substr(comma + 1);
        if (item.empty()) continue;
        BoardMark mark;
        mark.colour = item[0];
        if (!(mark.colour >= 'A' && mark.colour <= 'Z')) return false;
        if (arrows) {
            if (item.size() != 5) return false;
            const int from = squareIndex(std::string_view(item).substr(1, 2));
            const int to   = squareIndex(std::string_view(item).substr(3, 2));
            if (from < 0 || to < 0) return false;
            mark.from = static_cast<int8_t>(from);
            mark.to   = static_cast<int8_t>(to);
        } else {
            if (item.size() != 3) return false;
            const int at = squareIndex(std::string_view(item).substr(1, 2));
            if (at < 0) return false;
            mark.from = static_cast<int8_t>(at);
        }
        marks.push_back(mark);
    }
    out.insert(out.end(), marks.begin(), marks.end());
    return true;
}

// A tournament's comment: "book", or "<score>/<depth> ..." with the score +0.35, -1.20, 0.00, +M5 or -M4.
bool engineComment(std::string_view text) {
    if (text == "book") return true;
    std::size_t i = 0;
    if (text.substr(0, 4) == "0.00") {
        i = 4;
    } else {
        if (text.empty() || (text[0] != '+' && text[0] != '-')) return false;
        i = 1;
        if (i < text.size() && text[i] == 'M') ++i;
        const auto digits = i;
        while (i < text.size() && ((text[i] >= '0' && text[i] <= '9') || text[i] == '.')) ++i;
        if (i == digits) return false;
    }
    if (i >= text.size() || text[i] != '/') return false;
    ++i;
    const auto depth = i;
    while (i < text.size() && text[i] >= '0' && text[i] <= '9') ++i;
    return i > depth && (i == text.size() || text[i] == ' ' || text[i] == ',');
}

// The moves of one line of the tree, and where reading it has got to.
struct Line {
    int which = -1;        // -1 the game, else the index of a variation; -2 a variation left out whole
    int base  = 0;         // the ply of its first move
    int count = 0;         // its moves read
    chess::Board board;    // after its last move
    chess::Board before;   // before its last move: where a variation of that move starts
    std::string pending;   // a comment met before its first move
    bool stopped = false;  // an illegal move: the rest of the line is left out
};

// The variations left with no move (a variation of an illegal move, or an empty "()") taken out, the others'
// indexes renumbered.
void dropEmptyVariations(GameTree& tree) {
    std::vector<int> index(tree.variations.size(), -1);
    std::vector<Variation> kept;
    for (std::size_t v = 0; v < tree.variations.size(); ++v) {
        auto& var = tree.variations[v];
        if (var.moves.empty()) continue;
        if (var.parent >= 0 && index[static_cast<std::size_t>(var.parent)] < 0) continue;
        var.parent = var.parent < 0 ? -1 : index[static_cast<std::size_t>(var.parent)];
        index[v]   = static_cast<int>(kept.size());
        kept.push_back(std::move(var));
    }
    tree.variations = std::move(kept);
}

}  // namespace

std::string utf8Text(std::string_view text) {
    if (validUtf8(text)) return std::string(text);
    std::string out;
    out.reserve(text.size() + text.size() / 8);
    for (const char ch : text) {
        const auto c = static_cast<unsigned char>(ch);
        if (c < 0x80)
            out += ch;
        else if (c < 0xA0)
            appendUtf8(out, CP1252[c - 0x80]);
        else
            appendUtf8(out, c);
    }
    return out;
}

std::string applyMoveComment(std::string_view comment, ReplayMove& move) {
    const auto text = utf8Text(comment);
    std::string rest;
    std::size_t at = 0;
    bool assistant = false;  // the comment has the assistant's mark: its text goes to its own slot (#60)
    // What the commands were, for a tournament's comment in its ChessBase style: [%eval], [%clk] and [%emt] alone,
    // each read, and an [%emt] among them.
    bool engine_commands = true, emt = false;
    while (at < text.size()) {
        const auto open = text.find("[%", at);
        if (open == std::string::npos) {
            rest.append(text, at, std::string::npos);
            break;
        }
        const auto close = text.find(']', open);
        if (close == std::string::npos) {
            rest.append(text, at, std::string::npos);
            break;
        }
        rest.append(text, at, open - at);
        rest += ' ';
        // Its spaces and line ends made one space: a command cut by a line end reads like any other.
        const auto command = tidy(std::string_view(text.data() + open, close + 1 - open));
        const std::string_view body(command.data() + 2, command.size() - 3);  // "clk 0:01:00"
        const auto space = body.find_first_of(" \t\r\n");
        const auto name  = body.substr(0, space);
        const auto args  = space == std::string_view::npos ? std::string_view{} : body.substr(space + 1);
        bool taken       = false;
        if (command == ASSISTANT_MARK) {
            assistant = true;
            taken     = true;
        } else if (name == "clk") {
            ReplayMove probe;
            parseClockTag(command, probe);
            if (probe.clock_ms) {
                move.clock_ms = probe.clock_ms;
                taken         = true;
            }
        } else if (name == "eval") {
            ReplayMove probe;
            probe.mover = move.mover;
            parseEvalTag(command, probe);
            if (probe.score) {
                move.score          = probe.score;
                move.depth          = probe.depth != 0 ? probe.depth : move.depth;
                move.notes.eval_tag = true;
                taken               = true;
            }
        } else if (name == "emt") {
            ReplayMove probe;
            parseClockTag("[%clk " + std::string(args) + "]", probe);
            if (probe.clock_ms) {
                move.notes.emt_ms = probe.clock_ms;
                taken             = true;
                emt               = true;
            }
        } else if (name == "cal") {
            taken = readMarks(args, true, move.notes.arrows);
        } else if (name == "csl") {
            taken = readMarks(args, false, move.notes.squares);
        }
        if (!taken) move.notes.commands.push_back(command);
        engine_commands = engine_commands && taken && (name == "eval" || name == "clk" || name == "emt");
        at              = close + 1;
    }
    auto words = tidy(rest);
    // A "[%" left in the text has no end in this comment: kept as "[ %", it can never become a command when the
    // comment is joined to the next one, or written and read back.
    for (auto at = words.find("[%"); at != std::string::npos; at = words.find("[%", at + 3)) words.insert(at + 1, " ");
    // The assistant's comment, beside another or alone: never an engine's, never joined to the other.
    if (assistant) {
        join(move.notes.assistant, words);
        return {};
    }
    // A tournament's comment in fastchess's style: "+0.35/18 1.234s, n=…", kept as it was read (#87).
    if (engineComment(words)) {
        parseFastchessComment(words, move);
        if (move.engine_comment.empty() && words == tidy(text)) move.engine_comment = words;
        return words;
    }
    // In its ChessBase style: [%eval], [%clk] and [%emt], then its fields after a comma (", n=…, pv=\"…\", wdl=…"),
    // and how the game ended after its last move. The fields read as the other style's, the comment kept as it was
    // read: none of it goes to the notes (its commands were all read into the move).
    if (emt && engine_commands && move.engine_comment.empty() && (words.empty() || words.rfind(", ", 0) == 0)) {
        parseEngineFields(words, move);
        move.engine_comment = tidy(text);
        return words;
    }
    join(move.notes.after, words);
    return {};
}

PgnRead readPgnGame(std::string_view text) {
    PgnRead read;
    PgnLexer lex(text);
    GameTree tree;
    auto& game = tree.main;

    // The tags.
    auto word = lex.next();
    while (word.kind == PgnToken::Tag) {
        const std::string name(word.text);
        auto value = unescapeTag(word.value);
        if (name == "FEN") game.start_fen = value;
        if (name == "Result") game.result = value;
        if (name == "Variant" && (value == "Chess960" || value == "chess960" || value == "fischerandom" ||
                                  value == "Fischerandom" || value == "Chess 960"))
            read.chess960 = true;
        game.headers.emplace_back(name, std::move(value));
        word = lex.next();
    }
    if (word.kind == PgnToken::End && game.headers.empty()) {
        read.end = text.size();
        return read;
    }

    chess::Board board;
    board.set960(read.chess960);
    if (game.start_fen.empty()) game.start_fen = std::string(chess::constants::STARTPOS);
    // A file of someone else's may hold any FEN: one the board reader cannot take, or a position the move generator
    // cannot, is no game.
    if (!tour::readableFen(game.start_fen)) {
        read.error = "the FEN tag is no position";
        read.end   = text.size();
        return read;
    }
    board.setFen(game.start_fen);
    if (!tour::legalPosition(board)) {
        read.error = "the FEN tag is no legal position";
        read.end   = text.size();
        return read;
    }

    std::vector<Line> lines;
    lines.push_back(Line{-1, 0, 0, board, board, {}, false});
    std::string last_engine_comment;  // the game's last move's, for how it ended
    const auto movesOf = [&](const Line& line) -> std::vector<ReplayMove>& {
        return line.which < 0 ? game.moves : tree.variations[static_cast<std::size_t>(line.which)].moves;
    };
    const auto fail = [&](std::string why) {
        if (read.error.empty()) read.error = std::move(why);
    };

    for (;; word = lex.next()) {
        if (word.kind == PgnToken::End || word.kind == PgnToken::Result) break;
        if (word.kind == PgnToken::Tag) {
            // The next game, which this one ran into without a result.
            read.end = word.at;
            break;
        }
        auto& line = lines.back();
        switch (word.kind) {
            case PgnToken::MoveNumber:
                break;
            case PgnToken::VariationStart: {
                if (line.which == -2 || line.stopped || line.count == 0) {
                    // Nothing it could replace: left out whole.
                    lines.push_back(Line{-2, 0, 0, line.board, line.board, {}, true});
                    break;
                }
                Variation var;
                var.parent = line.which;
                var.from   = line.base + line.count - 1;
                tree.variations.push_back(std::move(var));
                const int which = static_cast<int>(tree.variations.size()) - 1;
                lines.push_back(Line{which, line.base + line.count - 1, 0, line.before, line.before, {}, false});
                break;
            }
            case PgnToken::VariationEnd:
                if (lines.size() > 1) lines.pop_back();
                break;
            case PgnToken::Nag:
                if (line.which != -2 && !line.stopped && line.count > 0 && word.nag > 0 && word.nag < 256)
                    movesOf(line).back().notes.nags.push_back(static_cast<uint8_t>(word.nag));
                break;
            case PgnToken::Comment: {
                if (line.which == -2) break;
                if (line.count == 0) {
                    join(line.which < 0 ? game.pre_comment : line.pending, tidy(utf8Text(word.text)));
                    break;
                }
                if (line.stopped) break;
                auto& move        = movesOf(line).back();
                const auto engine = applyMoveComment(word.text, move);
                if (line.which < 0) {
                    if (!engine.empty()) last_engine_comment = engine;
                    game.fields |= move.fields;
                }
                break;
            }
            case PgnToken::San: {
                if (line.which == -2 || line.stopped) break;
                ReplayMove move;
                move.san   = std::string(word.text);
                move.mover = line.board.sideToMove();
                if (word.text == "--" || word.text == "Z0") {
                    move.move = chess::Move(chess::Move::NULL_MOVE);
                } else {
                    try {
                        move.move =
                            tour::readableSan(word.text) ? chess::uci::parseSan(line.board, word.text) : chess::Move();
                    } catch (...) {
                        move.move = chess::Move();
                    }
                    if (move.move == chess::Move::NO_MOVE) {
                        fail("illegal move " + std::string(word.text));
                        line.stopped = true;
                        break;
                    }
                }
                if (!word.value.empty()) {
                    if (const int nag = glyphNag(word.value.substr(0, 2)); nag != 0)
                        move.notes.nags.push_back(static_cast<uint8_t>(nag));
                }
                if (line.count == 0 && line.which >= 0) move.notes.before = std::move(line.pending);
                line.pending.clear();
                line.before = line.board;
                if (move.move == chess::Move::NULL_MOVE)
                    line.board.makeNullMove();
                else
                    line.board.makeMove<true>(move.move);
                ++line.count;
                movesOf(line).push_back(std::move(move));
                break;
            }
            default:
                break;
        }
    }
    if (word.kind != PgnToken::Tag) read.end = lex.offset();
    if (lines.size() > 1) fail("a variation is not closed");

    // How a tournament's game ended: its last move's comment, after the engine's data; a field ("dt=0.001s") is none.
    if (!game.moves.empty() && !last_engine_comment.empty()) {
        if (const auto comma = last_engine_comment.rfind(", "); comma != std::string::npos) {
            const auto last = std::string_view(last_engine_comment).substr(comma + 2);
            const auto key  = last.find_first_not_of("abcdefghijklmnopqrstuvwxyz_");
            if (key == 0 || key == std::string_view::npos || last[key] != '=') game.final_comment = std::string(last);
        }
    }
    dropEmptyVariations(tree);
    read.tree = std::move(tree);
    return read;
}

}  // namespace fcg::study
