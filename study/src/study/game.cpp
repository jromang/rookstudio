#include "game.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <string>

namespace fcg {

namespace {

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

}  // namespace

std::string ReplayGame::header(const std::string& key) const {
    for (const auto& [k, v] : headers) {
        if (k == key) return v;
    }
    return {};
}

void parseFastchessComment(std::string_view comment, ReplayMove& move) {
    while (!comment.empty() && isSpace(comment.front())) comment.remove_prefix(1);

    if (comment.rfind("book", 0) == 0) {
        move.book = true;
        return;
    }

    // <score>/<depth> <seconds>s[, ...]
    const auto slash = comment.find('/');
    if (slash == std::string_view::npos) return;

    const auto score = comment.substr(0, slash);
    if (score.size() >= 2 && (score[0] == '+' || score[0] == '-')) {
        const int sign = score[0] == '-' ? -1 : 1;
        if (score[1] == 'M') {
            // fastchess prints mates in plies; UCI and Score use moves. Counted in 64 bits, as Score: a PGN file
            // from elsewhere may hold any number there, and plies + 1 must not overflow.
            int plies = 0;
            std::from_chars(score.data() + 2, score.data() + score.size(), plies);
            const int64_t moves = sign > 0 ? (int64_t{plies} + 1) / 2 : plies / 2;
            move.score          = fastchess::Score{fastchess::ScoreType::MATE, sign * moves};
        } else {
            double pawns = 0;
            try {
                pawns = std::stod(std::string(score.substr(1)));
            } catch (...) {
            }
            // Within what a 64-bit integer holds once in centipawns ("+1e300", "+inf" or "+nan" would not be).
            pawns = std::isnan(pawns) ? 0 : std::clamp(pawns, -1e15, 1e15);
            move.score =
                fastchess::Score{fastchess::ScoreType::CP, static_cast<int64_t>(sign * pawns * 100.0 + sign * 0.5)};
        }
    } else if (score.size() >= 4 && score.substr(0, 4) == "0.00") {
        move.score = fastchess::Score{fastchess::ScoreType::CP, 0};
    }

    auto rest          = comment.substr(slash + 1);
    int depth          = 0;
    const auto [p, ec] = std::from_chars(rest.data(), rest.data() + rest.size(), depth);
    if (ec == std::errc()) move.depth = depth;

    rest = rest.substr(static_cast<std::size_t>(p - rest.data()));
    while (!rest.empty() && isSpace(rest.front())) rest.remove_prefix(1);

    const auto s_at = rest.find('s');
    if (s_at != std::string_view::npos) {
        try {
            move.time_ms = static_cast<int64_t>(std::stod(std::string(rest.substr(0, s_at))) * 1000.0 + 0.5);
        } catch (...) {
        }
    }

    // ", key=value" fields after the time.
    parseEngineFields(rest, move);
}

void parseEngineFields(std::string_view rest, ReplayMove& move) {
    if (const auto at = rest.find("nps="); at != std::string_view::npos) {
        const auto value = rest.substr(at + 4);
        std::from_chars(value.data(), value.data() + value.size(), move.nps);
        move.fields |= PGN_NPS;
    }

    // A key at the start, or after a space: a comment written again over several lines has a line end there.
    const auto field = [&](std::string_view key) -> std::optional<std::string_view> {
        for (auto at = rest.find(key); at != std::string_view::npos; at = rest.find(key, at + 1)) {
            if (at == 0 || isSpace(rest[at - 1])) return rest.substr(at + key.size());
        }
        return std::nullopt;
    };
    if (const auto v = field("n=")) {
        std::from_chars(v->data(), v->data() + v->size(), move.nodes);
        move.fields |= PGN_NODES;
    }
    if (const auto v = field("sd=")) {
        std::from_chars(v->data(), v->data() + v->size(), move.seldepth);
        move.fields |= PGN_SELDEPTH;
    }
    if (const auto v = field("tbhits=")) {
        std::from_chars(v->data(), v->data() + v->size(), move.tbhits);
        move.fields |= PGN_TBHITS;
    }
    if (const auto v = field("pv=\""); v && v->find('"') != std::string_view::npos) {
        move.pv = std::string(v->substr(0, v->find('"')));
        move.fields |= PGN_PV;
    }
    if (const auto v = field("dt=")) {
        move.fields |= PGN_DECISION;
        try {
            move.decision_ms = static_cast<int64_t>(std::stod(std::string(v->substr(0, v->find('s')))) * 1000.0 + 0.5);
        } catch (...) {
        }
    }

    if (const auto at = rest.find("wdl="); at != std::string_view::npos) {
        std::array<int64_t, 3> wdl{};
        const char* p   = rest.data() + at + 4;
        const char* end = rest.data() + rest.size();
        bool ok         = true;
        for (int i = 0; i < 3 && ok; ++i) {
            const auto r = std::from_chars(p, end, wdl[i]);
            ok           = r.ec == std::errc() && (i == 2 || (r.ptr < end && *r.ptr == '/'));
            p            = r.ptr + 1;
        }
        if (ok) {
            move.wdl = wdl;
            move.fields |= PGN_WDL;
        }
    }
}

void parseClockTag(std::string_view comment, ReplayMove& move) {
    const auto at = comment.find("[%clk ");
    if (at == std::string_view::npos) return;
    auto value = comment.substr(at + 6);
    value      = value.substr(0, value.find(']'));
    // h:mm:ss, m:ss or s, the seconds with a fraction or not.
    double total     = 0;
    std::size_t from = 0;
    for (int part = 0; part < 3 && from <= value.size(); ++part) {
        const auto colon = value.find(':', from);
        const auto piece = value.substr(from, colon == std::string_view::npos ? std::string_view::npos : colon - from);
        double v         = 0;
        try {
            v = std::stod(std::string(piece));
        } catch (...) {
            return;
        }
        total = total * 60 + v;
        if (colon == std::string_view::npos) break;
        from = colon + 1;
    }
    if (std::isfinite(total) && total >= 0 && total < 1e7)
        move.clock_ms = static_cast<int64_t>(std::lround(total * 1000));
}

void parseEvalTag(std::string_view comment, ReplayMove& move) {
    const auto at = comment.find("[%eval ");
    if (at == std::string_view::npos) return;
    auto value = comment.substr(at + 7);
    // ChessBase writes the depth after a comma: [%eval 0.35,18].
    if (const auto comma = value.find(','); comma != std::string_view::npos && comma < value.find(']')) {
        int depth = 0;
        std::from_chars(value.data() + comma + 1, value.data() + value.size(), depth);
        if (depth > 0 && depth < 1000) move.depth = depth;
    }
    value = value.substr(0, value.find_first_of(" ,]"));
    if (value.empty()) return;
    // White's point of view, in pawns, or #N for a mate in N moves (#-N for Black).
    const int sign = move.mover == chess::Color::WHITE ? 1 : -1;
    if (value.front() == '#') {
        int64_t moves      = 0;
        const auto [p, ec] = std::from_chars(value.data() + 1, value.data() + value.size(), moves);
        if (ec != std::errc() || moves < -100000 || moves > 100000) return;
        move.score = fastchess::Score{fastchess::ScoreType::MATE, sign * moves};
        return;
    }
    double pawns = 0;
    try {
        pawns = std::stod(std::string(value));
    } catch (...) {
        return;
    }
    if (std::isnan(pawns)) return;
    pawns      = std::clamp(pawns, -1e6, 1e6);
    move.score = fastchess::Score{fastchess::ScoreType::CP, static_cast<int64_t>(std::lround(sign * pawns * 100.0))};
}

}  // namespace fcg
