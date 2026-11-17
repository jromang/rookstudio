#include "pgn_store.hpp"
#include "i18n.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <limits>
#include <sstream>

#include <study/pgn_reader.hpp>
#include <tour/chess_text.hpp>

namespace fcg {

namespace fs = std::filesystem;

std::string exportSpans(const fs::path& source, const std::vector<PgnSpan>& spans, const fs::path& target,
                        const std::vector<fs::path>& own,
                        const std::function<std::string(std::size_t i, std::string text)>& counted) {
    std::error_code ec;
    if (fs::equivalent(source, target, ec) ||
        std::any_of(own.begin(), own.end(), [&](const fs::path& file) { return fs::equivalent(file, target, ec); })) {
        return trf("{} holds the games of the tournament: choose another file", target.string());
    }

    std::ifstream in(source, std::ios::binary);
    if (!in) return trf("Cannot read {}", source.string());

    std::ofstream out(target, std::ios::binary | std::ios::trunc);
    if (!out) return trf("Cannot write {}", target.string());

    std::string buffer;
    for (std::size_t i = 0; i < spans.size(); ++i) {
        const auto& span = spans[i];
        buffer.resize(span.length);
        in.clear();
        in.seekg(static_cast<std::streamoff>(span.offset));
        in.read(buffer.data(), span.length);
        buffer.resize(static_cast<std::size_t>(std::max<std::streamsize>(0, in.gcount())));
        if (buffer.empty()) continue;
        if (counted) buffer = counted(i, std::move(buffer));
        out.write(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    }

    return out ? "" : trf("Write error in {}", target.string());
}

std::string pgnTag(std::string_view pgn, std::string_view tag) {
    std::size_t pos = 0;
    while (pos < pgn.size()) {
        auto end = pgn.find('\n', pos);
        if (end == std::string_view::npos) end = pgn.size();
        auto line = pgn.substr(pos, end - pos);
        pos       = end + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.remove_suffix(1);
        if (line.empty()) continue;
        if (line.front() != '[') break;  // the moves: the header is over

        // [Tag "value"]
        line.remove_prefix(1);
        if (line.substr(0, tag.size()) != tag || line.size() <= tag.size() || line[tag.size()] != ' ') continue;
        const auto open  = line.find('"');
        const auto close = line.rfind('"');
        if (open == std::string_view::npos || close <= open) return {};
        return std::string(line.substr(open + 1, close - open - 1));
    }
    return {};
}

bool openingMatches(const std::string& eco, const std::string& name, const std::string& query) {
    auto upper = [](std::string s) {
        for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        return s;
    };
    std::string q;
    for (const char c : query) {
        if (c != ' ') q += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    if (q.empty()) return true;

    const auto is_eco = [](const std::string& s) {
        return s.size() == 3 && s[0] >= 'A' && s[0] <= 'E' && std::isdigit(static_cast<unsigned char>(s[1])) &&
               std::isdigit(static_cast<unsigned char>(s[2]));
    };
    const auto code = upper(eco);

    // A range of codes.
    if (const auto dash = q.find('-'); dash != std::string::npos) {
        const auto from = q.substr(0, dash), to = q.substr(dash + 1);
        if (is_eco(from) && is_eco(to)) return is_eco(code) && code >= from && code <= to;
    }
    // A code or its beginning: a letter A to E and up to two digits.
    if (q.size() <= 3 && q[0] >= 'A' && q[0] <= 'E' &&
        std::all_of(q.begin() + 1, q.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); })) {
        return code.rfind(q, 0) == 0;
    }
    // Words of the name, in any order: "sicilian najdorf".
    const auto haystack = upper(name);
    std::istringstream words(upper(query));
    for (std::string word; words >> word;) {
        if (haystack.find(word) == std::string::npos) return false;
    }
    return true;
}

void forEachMoveComment(std::string_view pgn, const std::function<void(bool, const ReplayMove&)>& f) {
    // The side to move first: White, unless a FEN header says otherwise.
    bool white = true;
    if (const auto fen = pgn.find("[FEN \""); fen != std::string_view::npos) {
        const auto space = pgn.find(' ', fen + 6);
        white            = space == std::string_view::npos || pgn.substr(space + 1, 1) != "b";
    }

    // Every move has a comment, {book} included.
    const auto body = pgn.find("\n\n");
    if (body == std::string_view::npos) return;
    for (auto open = pgn.find('{', body); open != std::string_view::npos; open = pgn.find('{', open)) {
        const auto close = pgn.find('}', open);
        if (close == std::string_view::npos) break;

        ReplayMove move;
        parseFastchessComment(pgn.substr(open + 1, close - open - 1), move);
        f(white, move);
        white = !white;
        open  = close;
    }
}

EvalFacts evalFacts(std::string_view pgn, int white_result) {
    // White's point of view, by ply; NaN for a book move or a move without a score.
    std::vector<float> eval;
    std::vector<bool> by_white;
    forEachMoveComment(pgn, [&](bool white, const ReplayMove& m) {
        float e = std::numeric_limits<float>::quiet_NaN();
        if (!m.book && m.score) {
            e = m.score->type == fastchess::ScoreType::MATE ? (m.score->value >= 0 ? 10.0f : -10.0f)
                                                            : std::clamp(m.score->value / 100.0f, -10.0f, 10.0f);
            if (!white) e = -e;
        }
        eval.push_back(e);
        by_white.push_back(white);
    });

    EvalFacts facts;
    const auto n     = static_cast<int>(eval.size());
    const auto known = [&](int i) { return i >= 0 && i < n && !std::isnan(eval[i]); };

    // The engines disagree for 6 plies in a row: the smallest gap between two consecutive plies of the window.
    constexpr int WINDOW = 6;
    for (int i = 0; i + WINDOW <= n; ++i) {
        float gap = std::numeric_limits<float>::max();
        for (int j = i; j + 1 < i + WINDOW; ++j) {
            gap = known(j) && known(j + 1) ? std::min(gap, std::abs(eval[j] - eval[j + 1])) : 0.0f;
            if (gap == 0) break;
        }
        if (gap > facts.disagreement) facts.disagreement = gap, facts.disagree_ply = static_cast<int16_t>(i);
    }

    // One engine's evaluation from one of its moves to the next.
    for (int i = 2; i < n; ++i) {
        if (!known(i) || !known(i - 2)) continue;
        const float change = std::abs(eval[i] - eval[i - 2]);
        if (change > facts.swing) facts.swing = change, facts.swing_ply = static_cast<int16_t>(i);
    }

    // The loser's best evaluation of its own position.
    if (white_result != 0) {
        const bool loser_white = white_result < 0;
        for (int i = 0; i < n; ++i) {
            if (!known(i) || by_white[i] != loser_white) continue;
            const float own = loser_white ? eval[i] : -eval[i];
            if (own > facts.loser_best) facts.loser_best = own, facts.loser_ply = static_cast<int16_t>(i);
        }
    }
    return facts;
}

std::optional<ReplayGame> parseReplay(const std::string& pgn) {
    auto read = study::readPgnGame(pgn);
    if (!read.tree) return std::nullopt;
    return std::move(read.tree->main);
}

}  // namespace fcg
