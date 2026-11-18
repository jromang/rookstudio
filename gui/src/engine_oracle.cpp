#include "engine_oracle.hpp"

#include <chrono>
#include <sstream>
#include <vector>

#include <fmt/include/fmt/format.h>

namespace fcg {

namespace {

// Waits for a line that begins with `word`, at most `within`; the lines before it are given to `seen`.
template <typename Seen>
bool waitFor(EngineProcess& p, std::string_view word, std::chrono::milliseconds within, Seen&& seen) {
    const auto end = std::chrono::steady_clock::now() + within;
    while (std::chrono::steady_clock::now() < end) {
        if (!p.running()) return false;
        (void)p.wait(std::chrono::milliseconds(50));
        for (const auto& line : p.take()) {
            if (line.kind != EngineProcess::Line::Kind::Out) continue;
            seen(line.text);
            if (line.text.rfind(word, 0) == 0) return true;
        }
    }
    return false;
}

}  // namespace

EngineOracle::EngineOracle(EngineEntry engine, uint64_t nodes) : engine_(std::move(engine)), nodes_(nodes) {}

EngineOracle::~EngineOracle() {
    std::lock_guard lock(mutex_);
    if (process_) process_->stop();
}

std::string EngineOracle::error() const {
    std::lock_guard lock(mutex_);
    return error_;
}

bool EngineOracle::ready() const {
    if (process_ && process_->running()) return true;
    process_ = std::make_unique<EngineProcess>();
    std::string error;
    if (!process_->start(engineLaunch(engine_), error)) {
        error_ = fmt::format("{} cannot be started: {}", engine_.name, error);
        process_.reset();
        return false;
    }
    process_->send("uci");
    if (!waitFor(*process_, "uciok", std::chrono::seconds(10), [](const std::string&) {})) {
        error_ = fmt::format("{} did not answer uci", engine_.name);
        process_->stop();
        process_.reset();
        return false;
    }
    // One thread, its own small hash: the same answers whatever the machine is busy with.
    process_->send("setoption name Threads value 1");
    process_->send("setoption name Hash value 64");
    process_->send("setoption name UCI_Chess960 value " + std::string(chess960_ ? "true" : "false"));
    process_->send("isready");
    if (!waitFor(*process_, "readyok", std::chrono::seconds(10), [](const std::string&) {})) {
        error_ = fmt::format("{} did not answer isready", engine_.name);
        process_->stop();
        process_.reset();
        return false;
    }
    return true;
}

std::optional<commentary::Searched> EngineOracle::search(const commentary::Position& pos,
                                                         std::span<const commentary::Move> only) const {
    std::lock_guard lock(mutex_);
    error_.clear();
    if (!ready()) return std::nullopt;
    process_->send("position fen " + pos.fen());
    std::string go = fmt::format("go nodes {}", nodes_);
    if (!only.empty()) {
        go += " searchmoves";
        for (const auto& m : only) go += " " + commentary::uci(m);
    }
    process_->send(go);
    // The last line of the first principal variation: its score and its moves.
    std::optional<commentary::Score> score;
    std::vector<std::string> pv;
    const auto seen = [&](const std::string& line) {
        if (line.rfind("info ", 0) != 0) return;
        std::istringstream in(line);
        std::string word;
        int multipv = 1;
        std::optional<commentary::Score> s;
        std::vector<std::string> moves;
        while (in >> word) {
            if (word == "multipv") {
                in >> multipv;
            } else if (word == "score") {
                std::string kind;
                int value = 0;
                in >> kind >> value;
                s = kind == "mate" ? commentary::Score::mate(value) : commentary::Score::cp(value);
            } else if (word == "pv") {
                while (in >> word) moves.push_back(word);
            }
        }
        if (multipv != 1 || !s || moves.empty()) return;
        score = s;
        pv    = std::move(moves);
    };
    if (!waitFor(*process_, "bestmove", std::chrono::seconds(60), seen)) {
        error_ = fmt::format("{} did not end its search", engine_.name);
        process_->stop();
        process_.reset();
        return std::nullopt;
    }
    if (!score || pv.empty()) {
        error_ = fmt::format("{} gave no line", engine_.name);
        return std::nullopt;
    }
    commentary::Searched out;
    out.score = *score;
    auto at   = pos;
    for (const auto& u : pv) {
        const auto m = at.parseUci(u);
        if (!m) break;
        out.moves.push_back(*m);
        at.playUnchecked(*m);
    }
    if (out.moves.empty()) return std::nullopt;
    return out;
}

}  // namespace fcg
