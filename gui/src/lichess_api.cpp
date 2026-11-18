#include "lichess_api.hpp"

#include <algorithm>
#include <sstream>

#include <json.hpp>

#include <fmt/include/fmt/format.h>

#include "https.hpp"

namespace fcg::lichess {

namespace {

std::string reason(const HttpResult& r) {
    if (r.status == 0) return r.error.empty() ? std::string("Lichess did not answer") : r.error;
    if (r.status == 401) return "Lichess asks for a linked account";
    if (r.status == 429) return "Lichess asks to wait a minute: too many requests";
    return fmt::format("Lichess answered {}", r.status);
}

HttpRequest get(std::string url, std::string token = {}) {
    HttpRequest r;
    r.url    = std::move(url);
    r.method = "GET";
    r.token  = std::move(token);
    r.headers.emplace_back("Accept", "application/json");
    return r;
}

std::optional<int> intOf(const nlohmann::json& j, const char* key) {
    const auto it = j.find(key);
    if (it == j.end() || !it->is_number_integer()) return std::nullopt;
    return it->get<int>();
}

std::string textOf(const nlohmann::json& j, const char* key) {
    const auto it = j.find(key);
    return it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
}

uint64_t countOf(const nlohmann::json& j, const char* key) {
    const auto it = j.find(key);
    return it != j.end() && it->is_number_unsigned()  ? it->get<uint64_t>()
           : it != j.end() && it->is_number_integer() ? static_cast<uint64_t>(std::max<int64_t>(0, it->get<int64_t>()))
                                                      : 0;
}

Outcome outcomeOf(std::string_view c) {
    if (c == "win" || c == "syzygy-win") return Outcome::Win;
    if (c == "cursed-win") return Outcome::CursedWin;
    if (c == "maybe-win") return Outcome::MaybeWin;
    if (c == "draw") return Outcome::Draw;
    if (c == "maybe-loss") return Outcome::MaybeLoss;
    if (c == "blessed-loss") return Outcome::BlessedLoss;
    if (c == "loss" || c == "syzygy-loss") return Outcome::Loss;
    return Outcome::Unknown;
}

// Reads a body as JSON into `a` with `read`; a body that is not what Lichess sends says so.
template <typename T, typename Read>
void readInto(Answer<T>& a, const std::string& body, Read&& read) {
    try {
        a.value = read(nlohmann::json::parse(body));
    } catch (const std::exception& e) {
        a.value.reset();
        a.error = std::string("Lichess's answer could not be read: ") + e.what();
    }
}

ExplorerGame gameOf(const nlohmann::json& g) {
    ExplorerGame out;
    out.id            = textOf(g, "id");
    const auto winner = textOf(g, "winner");
    out.winner        = winner == "white" ? 'w' : winner == "black" ? 'b' : '=';
    if (const auto w = g.find("white"); w != g.end() && w->is_object()) {
        out.white        = textOf(*w, "name");
        out.white_rating = intOf(*w, "rating").value_or(0);
    }
    if (const auto b = g.find("black"); b != g.end() && b->is_object()) {
        out.black        = textOf(*b, "name");
        out.black_rating = intOf(*b, "rating").value_or(0);
    }
    out.year  = intOf(g, "year").value_or(0);
    out.month = textOf(g, "month");
    out.uci   = textOf(g, "uci");
    return out;
}

Explorer explorerOf(const nlohmann::json& j) {
    Explorer e;
    e.white = countOf(j, "white");
    e.draws = countOf(j, "draws");
    e.black = countOf(j, "black");
    if (const auto moves = j.find("moves"); moves != j.end() && moves->is_array()) {
        for (const auto& m : *moves) {
            ExplorerMove move;
            move.uci            = textOf(m, "uci");
            move.san            = textOf(m, "san");
            move.white          = countOf(m, "white");
            move.draws          = countOf(m, "draws");
            move.black          = countOf(m, "black");
            move.average_rating = intOf(m, "averageRating").value_or(0);
            if (const auto g = m.find("game"); g != m.end() && g->is_object()) move.game = textOf(*g, "id");
            e.moves.push_back(std::move(move));
        }
    }
    for (const auto& [key, list] : {std::pair{"topGames", &e.top_games}, std::pair{"recentGames", &e.recent_games}}) {
        if (const auto games = j.find(key); games != j.end() && games->is_array())
            for (const auto& g : *games) list->push_back(gameOf(g));
    }
    if (const auto o = j.find("opening"); o != j.end() && o->is_object()) {
        e.eco     = textOf(*o, "eco");
        e.opening = textOf(*o, "name");
    }
    return e;
}

std::string joined(const std::vector<std::string>& words) {
    std::string out;
    for (const auto& w : words) out += (out.empty() ? "" : ",") + urlEncode(w);
    return out;
}

}  // namespace

Servers& servers() {
    static Servers s;
    return s;
}

std::string urlEncode(std::string_view value) {
    static constexpr char HEX[] = "0123456789ABCDEF";
    std::string out;
    for (const unsigned char c : value) {
        const bool plain = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' ||
                           c == '_' || c == '.' || c == '~';
        if (plain) {
            out += static_cast<char>(c);
        } else {
            out += '%';
            out += HEX[c >> 4];
            out += HEX[c & 15];
        }
    }
    return out;
}

Answer<CloudEval> cloudEval(std::string_view fen, int lines, const std::atomic<bool>* cancel) {
    Answer<CloudEval> a;
    const auto r = politeRequest(
        get(fmt::format("{}/api/cloud-eval?fen={}&multiPv={}", servers().api, urlEncode(fen), std::clamp(lines, 1, 5))),
        cancel);
    a.status = r.status;
    if (r.status == 404) return a;  // not in the cloud
    if (r.status != 200) {
        a.error = reason(r);
        return a;
    }
    readInto(a, r.body, [](const nlohmann::json& j) {
        CloudEval e;
        e.depth  = intOf(j, "depth").value_or(0);
        e.knodes = countOf(j, "knodes");
        for (const auto& pv : j.at("pvs")) {
            CloudLine line;
            std::istringstream words(textOf(pv, "moves"));
            for (std::string w; words >> w;) line.moves.push_back(w);
            line.cp   = intOf(pv, "cp");
            line.mate = intOf(pv, "mate");
            e.lines.push_back(std::move(line));
        }
        return e;
    });
    return a;
}

Answer<Tablebase> tablebase(std::string_view fen, const std::atomic<bool>* cancel) {
    Answer<Tablebase> a;
    const auto r = politeRequest(get(fmt::format("{}/standard?fen={}", servers().tablebase, urlEncode(fen))), cancel);
    a.status     = r.status;
    if (r.status != 200) {
        a.error = reason(r);
        return a;
    }
    readInto(a, r.body, [](const nlohmann::json& j) {
        Tablebase t;
        t.outcome               = outcomeOf(textOf(j, "category"));
        t.dtz                   = intOf(j, "dtz");
        t.dtm                   = intOf(j, "dtm");
        t.checkmate             = j.value("checkmate", false);
        t.stalemate             = j.value("stalemate", false);
        t.insufficient_material = j.value("insufficient_material", false);
        for (const auto& m : j.at("moves")) {
            TablebaseMove move;
            move.uci       = textOf(m, "uci");
            move.san       = textOf(m, "san");
            move.outcome   = outcomeOf(textOf(m, "category"));
            move.dtz       = intOf(m, "dtz");
            move.dtm       = intOf(m, "dtm");
            move.zeroing   = m.value("zeroing", false);
            move.checkmate = m.value("checkmate", false);
            move.stalemate = m.value("stalemate", false);
            t.moves.push_back(std::move(move));
        }
        return t;
    });
    return a;
}

Answer<Explorer> explorer(const ExplorerQuery& q, const std::atomic<bool>* cancel) {
    Answer<Explorer> a;
    std::string url = servers().explorer;
    switch (q.source) {
        case ExplorerSource::Masters:
            url += "/masters?fen=" + urlEncode(q.fen) + fmt::format("&moves={}&topGames={}", q.moves, q.top_games);
            break;
        case ExplorerSource::Lichess:
            url += "/lichess?variant=standard&fen=" + urlEncode(q.fen) +
                   fmt::format("&moves={}&topGames={}&recentGames={}", q.moves, q.top_games, q.recent_games);
            if (!q.speeds.empty()) url += "&speeds=" + joined(q.speeds);
            if (!q.ratings.empty()) {
                std::vector<std::string> groups;
                for (const int r : q.ratings) groups.push_back(std::to_string(r));
                url += "&ratings=" + joined(groups);
            }
            break;
        case ExplorerSource::Player:
            url += "/player?variant=standard&player=" + urlEncode(q.player) +
                   "&color=" + (q.player_white ? "white" : "black") + "&fen=" + urlEncode(q.fen) +
                   fmt::format("&moves={}&recentGames={}", q.moves, q.recent_games);
            if (!q.speeds.empty()) url += "&speeds=" + joined(q.speeds);
            break;
    }
    if (!q.since.empty()) url += "&since=" + urlEncode(q.since);
    if (!q.until.empty()) url += "&until=" + urlEncode(q.until);
    if (q.source != ExplorerSource::Player) {
        const auto r = politeRequest(get(url, q.token), cancel);
        a.status     = r.status;
        if (r.status != 200) {
            a.error = reason(r);
            return a;
        }
        readInto(a, r.body, explorerOf);
        return a;
    }
    // A player's games are counted as they are read: each line the answer so far, the last one whole.
    std::string last;
    LineSplitter lines([&](std::string_view line) {
        last.assign(line);
        return true;
    });
    HttpStream stream;
    stream.on_head = [&](int status, std::string_view) {
        a.status = status;
        return status == 200;
    };
    stream.on_data                = [&](std::string_view data) { return lines.feed(data); };
    stream.cancel                 = cancel;
    auto request                  = get(url, q.token);
    request.headers.back().second = "application/x-ndjson";
    const auto r                  = politeStream(request, stream);
    lines.flush();
    if (a.status != 200 || !r.error.empty()) {
        HttpResult failed = r;
        failed.status     = a.status;
        a.error           = a.status == 200 ? r.error : reason(failed);
        return a;
    }
    readInto(a, last, explorerOf);
    return a;
}

}  // namespace fcg::lichess
