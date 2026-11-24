#include "repertoires.hpp"

#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <system_error>

#include <study/game_tree.hpp>
#include <study/pgn_reader.hpp>
#include <study/pgn_writer.hpp>
#include <study/scan_board.hpp>
#include <tour/files.hpp>

namespace fs = std::filesystem;

namespace fcg {

namespace {

std::string readText(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

// The position after `path` from the standard start, as chess.hpp keys it (Polyglot's, as the fast board's).
chess::Board after(std::span<const chess::Move> path) {
    chess::Board board;
    for (const auto& m : path) board.makeMove<false>(m);
    return board;
}

}  // namespace

RepertoireShelf::RepertoireShelf(fs::path profile) : dir_(std::move(profile) / "repertoires") {
    std::error_code ec;
    for (fs::directory_iterator it(dir_, ec), end; !ec && it != end; it.increment(ec)) {
        if (it->path().extension() != ".pgn") continue;
        const auto read = study::readPgnGame(readText(it->path()));
        if (!read.tree) continue;
        RepertoireFile entry;
        entry.name  = it->path().stem().string();
        entry.file  = it->path();
        entry.lines = *read.tree;
        entry.side  = entry.lines.main.header("RepertoireSide") == "Black" ? chess::Color::BLACK : chess::Color::WHITE;
        entries_.push_back(std::move(entry));
    }
    std::sort(entries_.begin(), entries_.end(),
              [](const RepertoireFile& a, const RepertoireFile& b) { return a.name < b.name; });
    maps_.resize(entries_.size());
    const auto active = readText(dir_ / "active");
    for (std::size_t i = 0; i < entries_.size(); ++i)
        if (entries_[i].name == active) active_ = static_cast<int>(i);
    if (active_ < 0 && !entries_.empty()) active_ = 0;
}

void RepertoireShelf::setActive(int index) {
    if (index < 0 || index >= static_cast<int>(entries_.size())) return;
    active_ = index;
    std::error_code ec;
    fs::create_directories(dir_, ec);
    tour::replaceFile(dir_ / "active", entries_[static_cast<std::size_t>(index)].name);
}

std::size_t RepertoireShelf::create(const std::string& wanted, chess::Color side) {
    auto base = wanted.empty() ? std::string(side == chess::Color::WHITE ? "White" : "Black") : wanted;
    for (auto& c : base)
        if (c == '/' || c == '\\' || c == ':') c = '_';
    auto name = base;
    for (int n = 2;
         std::any_of(entries_.begin(), entries_.end(), [&](const RepertoireFile& e) { return e.name == name; }); ++n)
        name = base + " (" + std::to_string(n) + ")";
    RepertoireFile entry;
    entry.name               = name;
    entry.file               = dir_ / (name + ".pgn");
    entry.side               = side;
    entry.lines.main.headers = {{"Event", "Repertoire"},
                                {"RepertoireSide", side == chess::Color::WHITE ? "White" : "Black"}};
    entry.lines.main.result  = "*";
    std::error_code ec;
    fs::create_directories(dir_, ec);
    entries_.push_back(std::move(entry));
    maps_.emplace_back();
    save(entries_.size() - 1);
    setActive(static_cast<int>(entries_.size()) - 1);
    return entries_.size() - 1;
}

std::size_t RepertoireShelf::activeOrNew(chess::Color side) {
    if (active_ >= 0) return static_cast<std::size_t>(active_);
    return create(side == chess::Color::WHITE ? "White" : "Black", side);
}

const study::Repertoire& RepertoireShelf::map(std::size_t index) {
    auto& cached = maps_.at(index);
    if (!cached) cached.emplace(entries_[index].lines, entries_[index].side);
    return *cached;
}

bool RepertoireShelf::has(std::size_t index, std::span<const chess::Move> path, chess::Move move) {
    if (index >= entries_.size()) return false;
    const auto board = after(path);
    study::ScanBoard scan;
    if (!scan.set(board)) return false;
    const auto* pos = map(index).at(scan.key());
    if (!pos) return false;
    const auto san = chess::uci::moveToSan(board, move);
    return std::any_of(pos->moves.begin(), pos->moves.end(),
                       [&](const study::RepertoireMove& m) { return m.san == san; });
}

bool RepertoireShelf::toggle(std::size_t index, std::span<const chess::Move> path, chess::Move move) {
    if (index >= entries_.size()) return false;
    auto& lines = entries_[index].lines;
    if (!has(index, path, move)) {
        if (!study::addMove(lines, false, path, move)) return false;
    } else {
        // Out wherever the lines play it from that position, with what follows it there.
        const auto target = after(path).hash();
        for (int guard = 0; guard < 1000; ++guard) {
            bool removed = false;
            for (int line = -1; line < static_cast<int>(lines.variations.size()) && !removed; ++line) {
                const auto game = lineOf(lines, line);
                auto board      = boardAt(game, false, 0);
                for (std::size_t ply = 0; ply < game.moves.size(); ++ply) {
                    if (board.hash() == target && game.moves[ply].move == move) {
                        truncateAt(lines, line, static_cast<int>(ply) + 1);
                        removed = true;
                        break;
                    }
                    board.makeMove<false>(game.moves[ply].move);
                }
            }
            if (!removed) break;
        }
    }
    changed(index);
    return has(index, path, move);
}

std::size_t RepertoireShelf::addLine(std::size_t index, std::span<const chess::Move> path,
                                     std::span<const chess::Move> line) {
    if (index >= entries_.size()) return 0;
    std::vector<chess::Move> at(path.begin(), path.end());
    std::size_t added = 0;
    for (const auto move : line) {
        if (!has(index, at, move)) {
            if (!study::addMove(entries_[index].lines, false, at, move)) break;
            maps_[index].reset();  // has() reads the lines as they now are
            ++added;
        }
        at.push_back(move);
    }
    if (added) changed(index);
    return added;
}

void RepertoireShelf::restore(std::size_t index, GameTree lines) {
    if (index >= entries_.size()) return;
    entries_[index].lines = std::move(lines);
    changed(index);
}

void RepertoireShelf::changed(std::size_t index) {
    maps_[index].reset();
    entries_[index].revision = ++revisions_;
    save(index);
}

void RepertoireShelf::save(std::size_t index) const {
    tour::replaceFile(entries_[index].file, study::writePgnGame(entries_[index].lines));
}

}  // namespace fcg
