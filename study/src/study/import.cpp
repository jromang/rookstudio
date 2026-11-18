#include "import.hpp"

#include "background.hpp"

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <thread>

#include "pgn_lexer.hpp"
#include "pgn_reader.hpp"
#include "scan_board.hpp"
#include "sources.hpp"

namespace fcg::study {

namespace fs = std::filesystem;

namespace {

// "1:02:03.4", "2:03" or "3" into milliseconds; -1 when it is none.
int32_t clockMs(std::string_view s) {
    int64_t total = 0, part = 0, fraction = 0, scale = 0;
    bool digits = false, dot = false;
    for (const char c : s) {
        if (c >= '0' && c <= '9') {
            digits = true;
            if (dot) {
                if (scale < 1000) {
                    fraction = fraction * 10 + (c - '0');
                    scale    = scale == 0 ? 10 : scale * 10;
                }
            } else {
                part = part * 10 + (c - '0');
                if (part > 10000000) return -1;
            }
        } else if (c == ':' && !dot) {
            total = (total + part) * 60;
            part  = 0;
        } else if (c == '.' && !dot) {
            dot = true;
        } else {
            return -1;
        }
    }
    if (!digits) return -1;
    int64_t ms = (total + part) * 1000;
    if (scale > 0) ms += fraction * 1000 / scale;
    return ms <= INT32_MAX ? static_cast<int32_t>(ms) : -1;
}

// "0.17", "-1.2", "#3", "#-4" (White's point of view, ChessBase's ",depth" after it) into the stored value.
int16_t evalValue(std::string_view s) {
    s = s.substr(0, s.find(','));
    if (s.empty()) return ImportedGame::NO_EVAL;
    if (s.front() == '#') {
        int moves = 0, sign = 1;
        std::size_t i = 1;
        if (i < s.size() && (s[i] == '-' || s[i] == '+')) sign = s[i++] == '-' ? -1 : 1;
        for (; i < s.size(); ++i) {
            if (s[i] < '0' || s[i] > '9' || moves > 1000) return ImportedGame::NO_EVAL;
            moves = moves * 10 + (s[i] - '0');
        }
        return static_cast<int16_t>(sign > 0 ? 30000 - moves : -30000 + moves);
    }
    int sign      = 1;
    std::size_t i = 0;
    if (s[i] == '-' || s[i] == '+') sign = s[i++] == '-' ? -1 : 1;
    int64_t pawns = 0, hundredths = 0, scale = 0;
    bool any = false;
    for (; i < s.size(); ++i) {
        if (s[i] >= '0' && s[i] <= '9') {
            any = true;
            if (scale == 0) {
                pawns = std::min<int64_t>(pawns * 10 + (s[i] - '0'), 1000);
            } else if (scale < 100) {
                hundredths = hundredths * 10 + (s[i] - '0');
                scale *= 10;
            }
        } else if (s[i] == '.' && scale == 0) {
            scale = 1;
        } else {
            return ImportedGame::NO_EVAL;
        }
    }
    if (!any) return ImportedGame::NO_EVAL;
    if (scale == 10) hundredths *= 10;
    const int64_t cp = std::clamp<int64_t>(sign * (pawns * 100 + hundredths), -29000, 29000);
    return static_cast<int16_t>(cp);
}

// A comment after a move of the game: its clock and evaluation taken; true when something else is in it.
bool readComment(std::string_view comment, ImportedGame& game) {
    bool other    = false;
    std::size_t p = 0;
    while (p < comment.size()) {
        const auto open = comment.find("[%", p);
        const auto text = comment.substr(p, open == std::string_view::npos ? std::string_view::npos : open - p);
        if (text.find_first_not_of(" \t\r\n") != std::string_view::npos) other = true;
        if (open == std::string_view::npos) break;
        const auto close = comment.find(']', open);
        if (close == std::string_view::npos) {
            other = true;
            break;
        }
        auto body        = comment.substr(open + 2, close - open - 2);
        const auto space = body.find_first_of(" \t\r\n");
        const auto name  = body.substr(0, space);
        auto value       = space == std::string_view::npos ? std::string_view{} : body.substr(space + 1);
        while (!value.empty() && (value.front() == ' ' || value.front() == '\n' || value.front() == '\r'))
            value.remove_prefix(1);
        while (!value.empty() && (value.back() == ' ' || value.back() == '\n' || value.back() == '\r'))
            value.remove_suffix(1);
        const auto ply = game.plies - 1;
        if (name == "clk" && clockMs(value) >= 0) {
            if (game.clocks.empty()) game.clocks.assign(game.plies, -1);
            game.clocks.resize(game.plies, -1);
            game.clocks[ply] = clockMs(value);
        } else if (name == "eval" && evalValue(value) != ImportedGame::NO_EVAL) {
            if (game.evals.empty()) game.evals.assign(game.plies, ImportedGame::NO_EVAL);
            game.evals.resize(game.plies, ImportedGame::NO_EVAL);
            game.evals[ply] = evalValue(value);
        } else {
            other = true;
        }
        p = close + 1;
    }
    return other;
}

// Where the text can be cut so that the first part holds whole games: the start of the last game it has, by its
// [Event tag (or a tag after a blank line); npos when none is found after the first character.
std::size_t lastGameStart(std::string_view text) {
    auto at = text.rfind("\n[Event ");
    if (at != std::string_view::npos && at > 0) return at + 1;
    at = text.rfind("\n\n[");
    if (at != std::string_view::npos && at > 0) return at + 2;
    return std::string_view::npos;
}

// A game's text as it is kept, its lines ended by "\n" whatever the file's: a file written on Windows and its copy
// with Unix line ends make the same database.
std::string keptText(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '\r') {
            out += text[i];
        } else if (i + 1 == text.size() || text[i + 1] != '\n') {
            out += '\n';  // a line ended by "\r" alone
        }
    }
    return out;
}

}  // namespace

bool readImportedGame(std::string_view text, ImportedGame& out) {
    out = ImportedGame{};
    PgnLexer lex(text);
    auto word = lex.next();
    while (word.kind == PgnToken::Tag) {
        std::string value;
        value.reserve(word.value.size());
        for (std::size_t i = 0; i < word.value.size(); ++i) {
            if (word.value[i] == '\\' && i + 1 < word.value.size() &&
                (word.value[i + 1] == '"' || word.value[i + 1] == '\\'))
                ++i;
            value += word.value[i];
        }
        value = utf8Text(value);
        if (word.text == "FEN") out.start_fen = value;
        if (word.text == "Result")
            out.result = value == "1-0" ? '1' : value == "0-1" ? '0' : value == "1/2-1/2" ? '=' : '*';
        if (word.text == "Variant") {
            std::string v = value;
            std::transform(v.begin(), v.end(), v.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (v.find("960") != std::string::npos || v.find("fischer") != std::string::npos) out.chess960 = true;
        }
        out.tags.emplace_back(std::string(word.text), std::move(value));
        word = lex.next();
    }
    if (out.tags.empty() && word.kind == PgnToken::End) return false;

    ScanBoard board;
    if (!board.setFen(
            out.start_fen.empty() ? std::string_view(chess::constants::STARTPOS) : std::string_view(out.start_fen),
            out.chess960)) {
        out.damaged = true;
        out.text    = keptText(text);
        return true;
    }
    SignatureMaker signature(board);
    int depth    = 0;
    bool stopped = false;
    for (; word.kind != PgnToken::End && word.kind != PgnToken::Result && word.kind != PgnToken::Tag;
         word = lex.next()) {
        switch (word.kind) {
            case PgnToken::San: {
                if (depth > 0) {
                    out.annotated = true;
                    break;
                }
                if (stopped) break;
                const auto move = board.parseSan(word.text);
                if (!move) {
                    out.damaged = stopped = true;
                    break;
                }
                if (!word.value.empty()) out.annotated = true;  // a glyph glued to the move
                const int code = board.encode(*move);
                if (board.codeCount() > 256) out.moves.push_back(static_cast<uint8_t>(code >> 8));
                out.moves.push_back(static_cast<uint8_t>(code & 0xFF));
                signature.before(board, *move, out.plies);
                board.play(*move);
                ++out.plies;
                break;
            }
            case PgnToken::Comment:
                if (depth > 0 || out.plies == 0 || stopped || readComment(word.text, out)) out.annotated = true;
                break;
            case PgnToken::Nag:
            case PgnToken::VariationStart:
                out.annotated = true;
                if (word.kind == PgnToken::VariationStart) ++depth;
                break;
            case PgnToken::VariationEnd:
                if (depth > 0) --depth;
                break;
            default:
                break;
        }
    }
    out.signature = signature.finish(board);
    if (!out.clocks.empty()) out.clocks.resize(out.plies, -1);
    if (!out.evals.empty()) out.evals.resize(out.plies, ImportedGame::NO_EVAL);
    if (out.annotated || out.damaged) out.text = keptText(text);
    return true;
}

std::vector<std::string_view> splitGames(std::string_view text) {
    std::vector<std::string_view> games;
    PgnLexer lex(text);
    std::size_t start = std::string_view::npos;
    bool moves        = false;
    for (auto word = lex.next();; word = lex.next()) {
        if (word.kind == PgnToken::End) {
            if (start != std::string_view::npos) games.push_back(text.substr(start));
            break;
        }
        if (word.kind == PgnToken::Tag) {
            if (start == std::string_view::npos || moves) {
                if (start != std::string_view::npos) games.push_back(text.substr(start, word.at - start));
                start = word.at;
                moves = false;
            }
            continue;
        }
        if (start == std::string_view::npos) start = word.at;
        moves = true;
        if (word.kind == PgnToken::Result) {
            games.push_back(text.substr(start, lex.offset() - start));
            start = std::string_view::npos;
            moves = false;
        }
    }
    return games;
}

ImportReport importGames(const fs::path& file, const ImportOptions& options, ImportProgress& progress,
                         const std::atomic<bool>& cancel,
                         const std::function<void(std::vector<ImportedGame>&&)>& sink) {
    ImportReport report;
    std::vector<std::unique_ptr<ByteSource>> sources;
    try {
        sources = openSources(file);
    } catch (const std::exception& e) {
        report.error = e.what();
        return report;
    }
    std::error_code ec;
    progress.total_bytes = fs::file_size(file, ec);
    const int threads =
        options.threads > 0 ? options.threads : static_cast<int>(std::max(1u, std::thread::hardware_concurrency()));
    const std::size_t piece = std::max<std::size_t>(options.piece_bytes, 4096);

    std::mutex mutex;
    std::condition_variable work, ready;
    std::deque<std::pair<uint64_t, std::string>> todo;
    std::map<uint64_t, std::vector<ImportedGame>> done;
    uint64_t pieces     = 0;  // made by the reader
    std::size_t working = 0;  // pieces a worker reads now
    bool read_all   = false;
    bool stop       = false;
    std::string error;

    const auto worker = [&] {
        for (;;) {
            std::pair<uint64_t, std::string> job;
            {
                std::unique_lock lock(mutex);
                work.wait(lock, [&] { return stop || !todo.empty() || read_all; });
                if (stop || (todo.empty() && read_all)) return;
                job = std::move(todo.front());
                todo.pop_front();
                ++working;
            }
            work.notify_all();  // room for the reader
            std::vector<ImportedGame> games;
            for (const auto text : splitGames(job.second)) {
                if (cancel.load(std::memory_order_relaxed)) break;
                ImportedGame game;
                if (readImportedGame(text, game)) games.push_back(std::move(game));
            }
            {
                std::lock_guard lock(mutex);
                done.emplace(job.first, std::move(games));
                --working;
            }
            ready.notify_all();
        }
    };
    const auto reader = [&] {
        uint64_t before = 0;  // the bytes of the sources already read
        std::string pending;
        std::vector<char> buffer(1 << 20);
        const auto give = [&](std::string text) {
            std::unique_lock lock(mutex);
            // No more than three pieces a worker, and 24 in all, waiting, being read, or read and not given yet: the
            // memory stays bounded whatever the file's size and the machine's cores (24 pieces keep 8 workers busy,
            // and more workers wait on the commits anyway).
            const auto most = std::min<std::size_t>(static_cast<std::size_t>(3 * threads), 24);
            work.wait(lock, [&] { return stop || todo.size() + working + done.size() < most; });
            if (stop) return false;
            todo.emplace_back(pieces++, std::move(text));
            lock.unlock();
            work.notify_one();
            ready.notify_all();
            return true;
        };
        try {
            for (auto& source : sources) {
                for (;;) {
                    if (cancel.load(std::memory_order_relaxed)) {
                        std::lock_guard lock(mutex);
                        stop = true;
                        break;
                    }
                    const auto n = source->read(buffer.data(), buffer.size());
                    progress.bytes.store(before + source->consumed(), std::memory_order_relaxed);
                    if (n > 0) pending.append(buffer.data(), n);
                    if (n == 0 || pending.size() >= piece) {
                        auto cut = n == 0 ? pending.size() : lastGameStart(pending);
                        // One game larger than a piece: read on, unless it has grown far past it.
                        if (cut == std::string::npos && pending.size() > 8 * piece) cut = pending.size();
                        if (cut != std::string::npos && cut > 0) {
                            std::string rest = pending.substr(cut);
                            pending.resize(cut);
                            if (!give(std::move(pending))) break;
                            pending = std::move(rest);
                        }
                    }
                    if (n == 0) break;
                }
                before += source->consumed();
                {
                    std::lock_guard lock(mutex);
                    if (stop) break;
                }
            }
        } catch (const std::exception& e) {
            std::lock_guard lock(mutex);
            error = e.what();
        }
        {
            std::lock_guard lock(mutex);
            read_all = true;
        }
        work.notify_all();
        ready.notify_all();
    };

    std::vector<std::thread> pool;
    pool.reserve(static_cast<std::size_t>(threads));
    const Inherited inherited;
    for (int i = 0; i < threads; ++i)
        pool.emplace_back([&worker, inherited] {
            inherited.apply();
            worker();
        });
    std::thread reading([&reader, inherited] {
        inherited.apply();
        reader();
    });

    // The batches given in the file's order.
    for (uint64_t next = 0;;) {
        std::vector<ImportedGame> batch;
        {
            std::unique_lock lock(mutex);
            ready.wait_for(lock, std::chrono::milliseconds(100),
                           [&] { return done.count(next) > 0 || (read_all && next >= pieces) || stop; });
            if (cancel.load(std::memory_order_relaxed)) {
                stop = true;
                work.notify_all();
                break;
            }
            if (const auto it = done.find(next); it != done.end()) {
                batch = std::move(it->second);
                done.erase(it);
                ++next;
            } else if ((read_all && next >= pieces) || stop) {
                break;
            } else {
                continue;
            }
        }
        work.notify_all();  // room for the reader
        for (const auto& game : batch) report.damaged += game.damaged;
        report.games += batch.size();
        progress.games.store(report.games, std::memory_order_relaxed);
        if (!batch.empty()) sink(std::move(batch));
    }
    {
        std::lock_guard lock(mutex);
        stop = true;
    }
    work.notify_all();
    reading.join();
    for (auto& t : pool) t.join();
    report.cancelled = cancel.load();
    report.error     = error;
    return report;
}

}  // namespace fcg::study
