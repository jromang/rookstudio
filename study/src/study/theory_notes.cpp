#include "theory_notes.hpp"

#include <algorithm>

#include "game_tree.hpp"
#include "import.hpp"
#include "pgn_reader.hpp"
#include "scan_board.hpp"

namespace fcg::study {

namespace {

// A tag's value, "?" and "-" being none.
std::string known(const ReplayGame& game, const std::string& tag) {
    auto value = game.header(tag);
    return value == "?" || value == "-" || value == "??" ? std::string() : value;
}

}  // namespace

uint64_t theoryKey(const chess::Board& board) {
    ScanBoard scan;
    return scan.set(board) ? scan.key() : 0;
}

std::string theoryGameName(const ReplayGame& game) {
    if (auto chapter = known(game, "ChapterName"); !chapter.empty()) return chapter;
    const auto white = known(game, "White"), black = known(game, "Black");
    if (!white.empty() || !black.empty())
        return (white.empty() ? std::string("?") : white) + " – " + (black.empty() ? std::string("?") : black);
    return known(game, "Event");
}

std::vector<const TheoryNote*> TheoryNotes::about(uint64_t key) const {
    std::vector<const TheoryNote*> out;
    if (const auto it = about_.find(key); it != about_.end())
        for (const auto i : it->second) out.push_back(&notes_[i]);
    return out;
}

std::vector<const TheoryNote*> TheoryNotes::from(uint64_t key) const {
    std::vector<const TheoryNote*> out;
    if (const auto it = from_.find(key); it != from_.end())
        for (const auto i : it->second) out.push_back(&notes_[i]);
    return out;
}

void TheoryNotes::index(std::size_t note) {
    const auto& n = notes_[note];
    auto& about   = about_[n.to];
    if (about.empty()) ++counts_.positions;
    about.push_back(static_cast<uint32_t>(note));
    if (n.from != 0) from_[n.from].push_back(static_cast<uint32_t>(note));
    ++counts_.notes;
    counts_.comments += (n.before.empty() ? 0 : 1) + (n.after.empty() ? 0 : 1);
    counts_.symbols += n.nags.size();
    counts_.evaluations += n.eval ? 1 : 0;
    counts_.marks += n.arrows.size() + n.squares.size();
}

void TheoryNotes::skip(const std::string& why) {
    if (error_.empty()) error_ = "game " + std::to_string(games_.size() + 1) + ": " + why;
    TheoryGame g;
    g.read = false;
    games_.push_back(std::move(g));
    ++counts_.games;
    ++counts_.unreadable;
}

void TheoryNotes::add(const GameTree& tree, bool chess960, const TheoryOptions& options, const std::string& problem) {
    const auto game = static_cast<uint32_t>(games_.size());
    if (!problem.empty() && error_.empty()) error_ = "game " + std::to_string(game + 1) + ": " + problem;
    const auto& fen = tree.main.start_fen;
    TheoryGame info;
    info.name     = theoryGameName(tree.main);
    info.standard = fen.empty() || fen == chess::constants::STARTPOS;
    info.chess960 = chess960;
    games_.push_back(info);
    ++counts_.games;
    counts_.variations += tree.variations.size();

    chess::Board start;
    start.set960(chess960);
    start.setFen(fen.empty() ? std::string(chess::constants::STARTPOS) : fen);
    const auto start_key = theoryKey(start);
    if (start_key == 0) return;
    // The game's comment before its first move: about its start.
    if (!tree.main.pre_comment.empty()) {
        TheoryNote n;
        n.to     = start_key;
        n.number = static_cast<int>(start.fullMoveNumber());
        n.black  = start.sideToMove() == chess::Color::BLACK;
        n.after  = tree.main.pre_comment;
        n.game   = game;
        notes_.push_back(std::move(n));
        index(notes_.size() - 1);
    }
    // Each line from the start, its own moves only: a variation's first moves are its parents', whose notes were
    // taken with them.
    for (int line = -1; line < static_cast<int>(tree.variations.size()); ++line) {
        const auto full = lineOf(tree, line);
        const int own   = line < 0 ? 0 : std::max(0, tree.variations[static_cast<std::size_t>(line)].from);
        // The line's positions and moves as a board writes them, up to a null move or one that cannot be played.
        std::vector<uint64_t> keys{start_key};
        std::vector<std::string> sans;
        std::vector<std::pair<int, bool>> numbers;
        auto board = start;
        for (const auto& m : full.moves) {
            if (m.move == chess::Move(chess::Move::NULL_MOVE) || m.move == chess::Move(chess::Move::NO_MOVE)) break;
            numbers.emplace_back(static_cast<int>(board.fullMoveNumber()), board.sideToMove() == chess::Color::BLACK);
            sans.push_back(chess::uci::moveToSan(board, m.move));
            board.makeMove<true>(m.move);
            keys.push_back(theoryKey(board));
        }
        for (std::size_t ply = static_cast<std::size_t>(own); ply < sans.size(); ++ply) {
            const auto& m     = full.moves[ply];
            const auto& notes = m.notes;
            TheoryNote n;
            n.before = notes.before;
            if (!notes.after.empty()) {
                const bool marked = !options.left_out.empty() && std::find(notes.commands.begin(), notes.commands.end(),
                                                                           options.left_out) != notes.commands.end();
                if (marked)
                    ++counts_.left_out;
                else
                    n.after = notes.after;
            }
            n.nags    = notes.nags;
            n.arrows  = notes.arrows;
            n.squares = notes.squares;
            if (notes.eval_tag && m.score) {
                // The mover's point of view, as read, to White's.
                auto score = *m.score;
                if (m.mover == chess::Color::BLACK) score.value = -score.value;
                n.eval  = score;
                n.depth = m.depth;
            }
            if (n.before.empty() && n.after.empty() && n.nags.empty() && n.arrows.empty() && n.squares.empty() &&
                !n.eval)
                continue;
            if (keys[ply] == 0 || keys[ply + 1] == 0) continue;
            n.from   = keys[ply];
            n.to     = keys[ply + 1];
            n.san    = sans[ply];
            n.number = numbers[ply].first;
            n.black  = numbers[ply].second;
            n.game   = game;
            n.line   = line;
            n.ply    = static_cast<int>(ply) + 1;
            n.path.assign(sans.begin(), sans.begin() + static_cast<std::ptrdiff_t>(ply) + 1);
            const auto rest = std::min<std::size_t>(sans.size(), ply + 5);
            n.next.assign(sans.begin() + static_cast<std::ptrdiff_t>(ply) + 1,
                          sans.begin() + static_cast<std::ptrdiff_t>(rest));
            notes_.push_back(std::move(n));
            index(notes_.size() - 1);
        }
    }
}

TheoryNotes readTheory(std::string_view text, const TheoryOptions& options, const std::atomic<bool>* cancel,
                       std::atomic<uint64_t>* done) {
    TheoryNotes out;
    for (const auto game : splitGames(text)) {
        if (cancel && cancel->load(std::memory_order_relaxed)) break;
        if (done) done->store(static_cast<uint64_t>(game.data() + game.size() - text.data()));
        const auto read = readPgnGame(game);
        if (!read.tree) {
            out.skip(read.error.empty() ? std::string("no game in its text") : read.error);
            continue;
        }
        out.add(*read.tree, read.chess960, options, read.error);
    }
    if (done && !(cancel && cancel->load(std::memory_order_relaxed))) done->store(text.size());
    return out;
}

}  // namespace fcg::study
