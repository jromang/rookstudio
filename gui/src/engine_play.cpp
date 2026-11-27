#include "engine_play.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <tuple>

#include <fmt/include/fmt/format.h>

#include "console.hpp"
#include "i18n.hpp"
#include "profile_entries.hpp"
#include "storage.hpp"
#include "ui.hpp"

namespace fcg {

namespace fs = std::filesystem;

namespace {

constexpr std::array<int, 13> NODE_STEPS = {1, 3, 10, 30, 100, 300, 1000, 3000, 10000, 30000, 100000, 300000, 1000000};

// A FEN's placement as 64 squares (a1 = 0 … h8 = 63, '.' for none), and its other fields; none when it cannot be read.
struct FenParts {
    std::array<char, 64> squares{};
    std::string side, castling, ep, half, full;
};

std::optional<FenParts> splitFen(const std::string& fen) {
    std::istringstream in(fen);
    std::string placement;
    FenParts p;
    in >> placement >> p.side >> p.castling >> p.ep >> p.half >> p.full;
    if (placement.empty() || p.side.empty()) return std::nullopt;
    if (p.castling.empty()) p.castling = "-";
    if (p.ep.empty()) p.ep = "-";
    if (p.half.empty()) p.half = "0";
    if (p.full.empty()) p.full = "1";
    p.squares.fill('.');
    int rank = 7, file = 0;
    for (const char c : placement) {
        if (c == '/') {
            if (--rank < 0) return std::nullopt;
            file = 0;
        } else if (c >= '1' && c <= '8') {
            file += c - '0';
        } else {
            if (file > 7 || std::string_view("PNBRQKpnbrqk").find(c) == std::string_view::npos) return std::nullopt;
            p.squares[static_cast<std::size_t>(rank * 8 + file)] = c;
            ++file;
        }
        if (file > 8) return std::nullopt;
    }
    return p;
}

std::string joinFen(const FenParts& p) {
    std::string placement;
    for (int rank = 7; rank >= 0; --rank) {
        int empty = 0;
        for (int file = 0; file < 8; ++file) {
            const char c = p.squares[static_cast<std::size_t>(rank * 8 + file)];
            if (c == '.') {
                ++empty;
                continue;
            }
            if (empty) placement += static_cast<char>('0' + empty);
            empty = 0;
            placement += c;
        }
        if (empty) placement += static_cast<char>('0' + empty);
        if (rank) placement += '/';
    }
    return fmt::format("{} {} {} {} {} {}", placement, p.side, p.castling, p.ep, p.half, p.full);
}

int squareOf(const char* name) { return (name[1] - '1') * 8 + (name[0] - 'a'); }

std::string squareName(int square) {
    return {static_cast<char>('a' + square % 8), static_cast<char>('1' + square / 8)};
}

// The square of the man the handicap takes, -1 when it is not there.
int handicapSquare(const FenParts& p, Handicap handicap, chess::Color side) {
    const bool white = side == chess::Color::WHITE;
    char man         = 0;
    int square       = -1;
    switch (handicap) {
        case Handicap::None:
            return -1;
        case Handicap::Pawn:
            man    = white ? 'P' : 'p';
            square = squareOf(white ? "f2" : "f7");
            break;
        case Handicap::Knight:
            man    = white ? 'N' : 'n';
            square = squareOf(white ? "b1" : "b8");
            break;
        case Handicap::Rook:
            man    = white ? 'R' : 'r';
            square = squareOf(white ? "a1" : "a8");
            break;
        case Handicap::Queen: {
            man    = white ? 'Q' : 'q';
            square = squareOf(white ? "d1" : "d8");
            if (p.squares[static_cast<std::size_t>(square)] == man) return square;
            // Else the only queen it has.
            int found = -1, count = 0;
            for (int i = 0; i < 64; ++i) {
                if (p.squares[static_cast<std::size_t>(i)] == man) {
                    found = i;
                    ++count;
                }
            }
            return count == 1 ? found : -1;
        }
    }
    return p.squares[static_cast<std::size_t>(square)] == man ? square : -1;
}

}  // namespace

PlaySettings readPlaySettings(const fs::path& profile) {
    // Setting by setting (#143): one of another type keeps its default, said, the file as it was kept beside it, which
    // the next save writes without it; a file cut short is kept aside and said.
    return readProfileEntries<PlaySettings>(profile / "play.json");
}

bool writePlaySettings(const fs::path& profile, const PlaySettings& settings) {
    std::error_code ec;
    fs::create_directories(profile, ec);
    return writeJson(profile / "play.json", nlohmann::json(settings));
}

std::optional<EloRange> eloRange(const EngineEntry& engine) {
    for (const auto& o : engine.uci_options) {
        if (o.name != "UCI_Elo" || o.type != "spin") continue;
        try {
            const EloRange r{std::stoi(o.min), std::stoi(o.max)};
            if (r.min > 0 && r.max > r.min) return r;
        } catch (const std::exception&) {
        }
    }
    return std::nullopt;
}

std::span<const int> nodeSteps() { return NODE_STEPS; }

Strength strengthFor(const PlaySettings& settings, const EngineEntry& engine) {
    if (settings.full) return {};
    if (const auto range = eloRange(engine)) return {std::clamp(settings.elo, range->min, range->max), 0};
    return {0, static_cast<uint64_t>(std::clamp(settings.nodes, NODE_STEPS.front(), NODE_STEPS.back()))};
}

void applyStrength(EngineEntry& engine, const Strength& strength) {
    const bool limit = std::any_of(engine.uci_options.begin(), engine.uci_options.end(),
                                   [](const UciOptionInfo& o) { return o.name == "UCI_LimitStrength"; });
    if (strength.elo > 0) {
        engine.setOption("UCI_LimitStrength", "true");
        engine.setOption("UCI_Elo", std::to_string(strength.elo));
    } else if (limit) {
        // Its full strength, or the nodes a move: whatever its card says of its strength left aside for the game.
        engine.setOption("UCI_LimitStrength", "false");
    }
}

std::string strengthText(const Strength& strength) {
    if (strength.elo > 0) return trf("Elo {}", strength.elo);
    if (strength.nodes > 0)
        return trn("{} node a move", "{} nodes a move", strength.nodes, ui::exactCount(strength.nodes));
    return TR("full strength");
}

std::string strengthTag(const Strength& strength) {
    if (strength.elo > 0) return fmt::format("UCI_Elo {}", strength.elo);
    if (strength.nodes > 0) return fmt::format("nodes {}", strength.nodes);
    return "full";
}

std::optional<std::string> handicapFen(const std::string& fen, Handicap handicap, chess::Color side, bool chess960) {
    if (handicap == Handicap::None || chess960) return std::nullopt;
    auto p = splitFen(fen);
    if (!p) return std::nullopt;
    const int square = handicapSquare(*p, handicap, side);
    if (square < 0) return std::nullopt;
    p->squares[static_cast<std::size_t>(square)] = '.';
    if (handicap == Handicap::Rook) {
        // The a-rook gone, castling on the queen's side with it.
        std::erase(p->castling, side == chess::Color::WHITE ? 'Q' : 'q');
        if (p->castling.empty()) p->castling = "-";
    }
    if (handicap == Handicap::Pawn) p->ep = "-";  // the pawn that could take, or be taken, may be the one gone
    return joinFen(*p);
}

const char* handicapName(Handicap handicap) {
    switch (handicap) {
        case Handicap::None:
            return TR("nothing");
        case Handicap::Pawn:
            return TR("a pawn (f)");
        case Handicap::Knight:
            return TR("a knight (b)");
        case Handicap::Rook:
            return TR("a rook (a)");
        case Handicap::Queen:
            return TR("the queen");
    }
    return "";
}

std::string handicapTag(const std::string& fen, Handicap handicap, chess::Color side) {
    const auto p = splitFen(fen);
    if (!p || handicap == Handicap::None) return {};
    const int square = handicapSquare(*p, handicap, side);
    if (square < 0) return {};
    const char* man = handicap == Handicap::Pawn     ? "pawn"
                      : handicap == Handicap::Knight ? "knight"
                      : handicap == Handicap::Rook   ? "rook"
                                                     : "queen";
    return fmt::format("{} {}", man, squareName(square));
}

std::optional<chess::Move> pickOpening(std::vector<OpeningChoice> choices, bool vary, uint64_t least,
                                       std::mt19937& rng) {
    std::erase_if(choices, [&](const OpeningChoice& c) {
        return c.move == chess::Move::NO_MOVE || c.weight < std::max<uint64_t>(least, 1);
    });
    if (choices.empty()) return std::nullopt;
    if (!vary) {
        return std::max_element(choices.begin(), choices.end(),
                                [](const OpeningChoice& a, const OpeningChoice& b) {
                                    return std::tie(a.main, a.weight) < std::tie(b.main, b.weight);
                                })
            ->move;
    }
    uint64_t total = 0;
    for (const auto& c : choices) total += c.weight;
    std::uniform_int_distribution<uint64_t> pick(0, total - 1);
    uint64_t at = pick(rng);
    for (const auto& c : choices) {
        if (at < c.weight) return c.move;
        at -= c.weight;
    }
    return choices.back().move;
}

std::optional<std::string> OpeningBook::move(const chess::Board& board) {
    if (!source_) return std::nullopt;
    auto choices = source_(board);
    chess::Movelist legal;
    chess::movegen::legalmoves(legal, board);
    std::erase_if(choices,
                  [&](const OpeningChoice& c) { return std::find(legal.begin(), legal.end(), c.move) == legal.end(); });
    const auto move = pickOpening(std::move(choices), vary_, least_, rng_);
    if (!move) return std::nullopt;
    return chess::uci::moveToUci(*move, board.chess960());
}

void appendPlayedGame(const fs::path& folder, const PlayedGame& game) {
    std::error_code ec;
    fs::create_directories(folder, ec);
    (void)appendJsonLine(folder / "games.jsonl", game);
}

std::vector<PlayedGame> readPlayedGames(const fs::path& folder) {
    std::vector<PlayedGame> out;
    // A line of another type, or cut short by a crash, is left out and said (#143); the file, only added to, keeps it.
    const auto file   = folder / "games.jsonl";
    std::size_t wrong = 0;
    const auto cut    = forEachJsonLine(file, [&](const nlohmann::json& j) {
        try {
            out.push_back(j.get<PlayedGame>());
        } catch (const std::exception&) {
            ++wrong;
        }
    });
    if (const auto unread = wrong + cut; unread > 0)
        Console::instance().add(trn("{}: {} line could not be read and was left out",
                                    "{}: {} lines could not be read and were left out", unread, file.string(), unread),
                                true);
    return out;
}

std::optional<int> userScore(const PlayedGame& game) {
    if (game.result == "1/2-1/2") return 0;
    if (game.result == "1-0") return game.white ? 1 : -1;
    if (game.result == "0-1") return game.white ? -1 : 1;
    return std::nullopt;
}

PlayTally playTally(const std::vector<PlayedGame>& games, const std::string& engine, const std::string& strength) {
    PlayTally t;
    for (const auto& g : games) {
        if (g.engine != engine || g.strength != strength) continue;
        const auto score = userScore(g);
        if (!score) continue;
        if (g.aids) {
            ++t.with_aids;
            continue;
        }
        ++t.games;
        if (*score > 0) ++t.won;
        if (*score == 0) ++t.drawn;
        if (*score < 0) {
            ++t.lost;
            t.last_lost = g;  // the file is in the order the games ended
        }
    }
    return t;
}

std::string endingOf(const std::string& fen, int plies) {
    const auto p = splitFen(fen);
    if (!p) return {};
    int queens = 0, rooks = 0, minors = 0, material = 0;
    for (const char c : p->squares) {
        switch (std::tolower(static_cast<unsigned char>(c))) {
            case 'q':
                ++queens;
                material += 9;
                break;
            case 'r':
                ++rooks;
                material += 5;
                break;
            case 'b':
            case 'n':
                ++minors;
                material += 3;
                break;
            default:
                break;
        }
    }
    if (plies <= 20 && material >= 54) return TR("the opening");
    if (material > 26) return TR("the middlegame");
    if (queens + rooks + minors == 0) return TR("a pawn endgame");
    if (queens == 0 && minors == 0) return TR("a rook endgame");
    if (rooks == 0 && minors == 0) return TR("a queen endgame");
    if (queens == 0 && rooks == 0) return TR("an endgame of minor pieces");
    return TR("an endgame");
}

// ---------------------------------------------------------------------------
// The game being played, kept

bool writeKeptGame(const fs::path& folder, const KeptGame& g) {
    std::error_code ec;
    fs::create_directories(folder, ec);
    auto headers = nlohmann::json::array();
    for (const auto& [k, v] : g.headers) headers.push_back({k, v});
    auto clocks_before = nlohmann::json::array();
    for (const auto& c : g.clocks_before) clocks_before.push_back({c[0], c[1]});
    const nlohmann::json j = {{"version", 1},
                              {"settings", g.settings},
                              {"engine", g.engine},
                              {"elo", g.strength.elo},
                              {"nodes", g.strength.nodes},
                              {"openings", g.openings_tag},
                              {"openings_text", g.openings_text},
                              {"white", g.white},
                              {"chess960", g.chess960},
                              {"start_fen", g.start_fen},
                              {"headers", std::move(headers)},
                              {"moves", g.moves},
                              {"times", g.times},
                              {"clocks_before", std::move(clocks_before)},
                              {"clock_ms", {g.clock_ms[0], g.clock_ms[1]}},
                              {"clocks", {g.clocks[0], g.clocks[1]}},
                              {"takebacks", g.takebacks},
                              {"hints_used", g.hints_used},
                              {"aids", g.aids},
                              {"last_book", g.last_book},
                              {"book_out", g.book_out}};
    return writeJson(folder / "current.json", j);
}

std::optional<KeptGame> readKeptGame(const fs::path& folder) {
    std::ifstream in(folder / "current.json", std::ios::binary);
    if (!in) return std::nullopt;
    const auto j = nlohmann::json::parse(in, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return std::nullopt;
    try {
        KeptGame g;
        g.settings       = j.at("settings").get<PlaySettings>();
        g.engine         = j.at("engine").get<std::string>();
        g.strength.elo   = j.value("elo", 0);
        g.strength.nodes = j.value("nodes", uint64_t{0});
        g.openings_tag   = j.value("openings", std::string());
        g.openings_text  = j.value("openings_text", std::string());
        g.white          = j.value("white", true);
        g.chess960       = j.value("chess960", false);
        g.start_fen      = j.at("start_fen").get<std::string>();
        for (const auto& h : j.at("headers"))
            g.headers.emplace_back(h.at(0).get<std::string>(), h.at(1).get<std::string>());
        g.moves = j.at("moves").get<std::vector<std::string>>();
        g.times = j.value("times", std::vector<int64_t>{});
        for (const auto& c : j.value("clocks_before", nlohmann::json::array()))
            g.clocks_before.push_back({c.at(0).get<int64_t>(), c.at(1).get<int64_t>()});
        const auto& clock = j.at("clock_ms");
        g.clock_ms        = {clock.at(0).get<int64_t>(), clock.at(1).get<int64_t>()};
        const auto& tc    = j.at("clocks");
        g.clocks          = {tc.at(0).get<GameClock>(), tc.at(1).get<GameClock>()};
        g.takebacks       = j.value("takebacks", 0);
        g.hints_used      = j.value("hints_used", 0);
        g.aids            = j.value("aids", false);
        g.last_book       = j.value("last_book", -1);
        g.book_out        = j.value("book_out", false);
        g.times.resize(g.moves.size(), -1);
        if (g.clocks_before.size() != g.moves.size()) g.clocks_before.assign(g.moves.size(), g.clock_ms);
        if (g.engine.empty() || g.start_fen.empty()) return std::nullopt;
        return g;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

void removeKeptGame(const fs::path& folder) {
    std::error_code ec;
    fs::remove(folder / "current.json", ec);
}

std::optional<ReplayGame> keptReplay(const KeptGame& kept) {
    ReplayGame game;
    game.headers   = kept.headers;
    game.start_fen = kept.start_fen;
    chess::Board board;
    board.set960(kept.chess960);
    if (!board.setFen(kept.start_fen)) return std::nullopt;
    for (std::size_t i = 0; i < kept.moves.size(); ++i) {
        const auto move = chess::uci::uciToMove(board, kept.moves[i]);
        chess::Movelist legal;
        chess::movegen::legalmoves(legal, board);
        if (move == chess::Move::NO_MOVE || std::find(legal.begin(), legal.end(), move) == legal.end())
            return std::nullopt;
        ReplayMove m;
        m.move    = move;
        m.mover   = board.sideToMove();
        m.san     = chess::uci::moveToSan(board, move);
        m.time_ms = kept.times[i];
        board.makeMove<true>(move);
        game.moves.push_back(std::move(m));
    }
    return game;
}

}  // namespace fcg
