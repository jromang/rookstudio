#include "lichess_links.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <vector>

#include <fmt/include/fmt/format.h>
#include <study/import.hpp>
#include <study/store.hpp>

#include "https.hpp"

namespace fcg::lichess {

namespace {

bool alnum(std::string_view s) {
    return !s.empty() &&
           std::all_of(s.begin(), s.end(), [](char c) { return std::isalnum(static_cast<unsigned char>(c)); });
}

bool lowerLetters(std::string_view s) {
    return !s.empty() && std::all_of(s.begin(), s.end(), [](char c) { return c >= 'a' && c <= 'z'; });
}

// Lichess's pages whose name could pass for a game's id: eight letters of the alphabet.
bool page(std::string_view word) {
    static constexpr std::array<std::string_view, 14> PAGES = {
        "analysis", "training", "practice", "streamer", "insights", "settings", "timeline",
        "variants", "features", "contacts", "download", "accounts", "redirect", "external"};
    return std::find(PAGES.begin(), PAGES.end(), word) != PAGES.end();
}

bool gameId(std::string_view s) { return s.size() == 8 && alnum(s) && !page(s); }

bool userName(std::string_view s) {
    return s.size() >= 2 && s.size() <= 30 && std::all_of(s.begin(), s.end(), [](char c) {
               return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-';
           });
}

struct Address {
    std::vector<std::string> path;  // its segments, none empty
    std::string fragment;           // after '#'
};

// The path and fragment of an address on lichess.org (its host lichess.org, www. or a language's subdomain before it),
// none for anything else.
std::optional<Address> address(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) text.remove_prefix(1);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) text.remove_suffix(1);
    if (text.empty() ||
        std::any_of(text.begin(), text.end(), [](char c) { return std::isspace(static_cast<unsigned char>(c)); }))
        return std::nullopt;
    for (const std::string_view scheme : {"https://", "http://"}) {
        if (text.substr(0, scheme.size()) == scheme) {
            text.remove_prefix(scheme.size());
            break;
        }
    }
    const auto slash = text.find_first_of("/?#");
    std::string host(text.substr(0, slash));
    for (auto& c : host) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (host != "lichess.org") {
        const auto dot = host.find('.');
        if (dot == std::string::npos || host.substr(dot + 1) != "lichess.org" || !lowerLetters(host.substr(0, dot)))
            return std::nullopt;
    }
    Address out;
    std::string_view rest = slash == std::string_view::npos ? std::string_view() : text.substr(slash);
    if (const auto hash = rest.find('#'); hash != std::string_view::npos) {
        out.fragment = std::string(rest.substr(hash + 1));
        rest         = rest.substr(0, hash);
    }
    if (const auto query = rest.find('?'); query != std::string_view::npos) rest = rest.substr(0, query);
    std::size_t at = 0;
    while (at < rest.size()) {
        const auto next = rest.find('/', at);
        const auto part = rest.substr(at, next == std::string_view::npos ? std::string_view::npos : next - at);
        if (!part.empty()) out.path.emplace_back(part);
        if (next == std::string_view::npos) break;
        at = next + 1;
    }
    return out;
}

std::optional<Link> game(std::string_view id, std::optional<bool> white, const std::string& fragment) {
    if (id.size() == 12 && alnum(id)) id = id.substr(0, 8);  // a player's own id: the game's and four more
    if (!gameId(id)) return std::nullopt;
    Link link;
    link.kind  = LinkKind::Game;
    link.id    = std::string(id);
    link.white = white;
    if (!fragment.empty() &&
        std::all_of(fragment.begin(), fragment.end(), [](char c) { return c >= '0' && c <= '9'; }) &&
        fragment.size() <= 4)
        link.ply = std::stoi(fragment);
    return link;
}

Link simple(LinkKind kind, std::string id, std::string chapter = {}) {
    Link link;
    link.kind    = kind;
    link.id      = std::move(id);
    link.chapter = std::move(chapter);
    return link;
}

// What a link asks for, for the words of its failures: who may read it (401, 403), and a 404 at its address.
Asked askedOf(const Link& link) {
    switch (link.kind) {
        case LinkKind::BroadcastRound:
        case LinkKind::BroadcastTour:
            return Asked::Broadcast;
        case LinkKind::Bookmarks:
            return Asked::Bookmarks;
        default:
            return Asked::Page;
    }
}

}  // namespace

bool lichessAddress(std::string_view text) { return address(text).has_value(); }

std::optional<Link> parseLink(std::string_view text) {
    const auto a = address(text);
    if (!a) return std::nullopt;
    const auto& p = a->path;
    const auto n  = p.size();
    if (n == 0) return std::nullopt;
    if (p[0] == "game" && n >= 2) return game(n >= 3 && p[1] == "export" ? p[2] : p[1], std::nullopt, a->fragment);
    if (p[0] == "study" && n >= 2 && gameId(p[1])) {
        if (n >= 3 && gameId(p[2])) return simple(LinkKind::Chapter, p[1], p[2]);
        return simple(LinkKind::Study, p[1]);
    }
    if (p[0] == "broadcast" && n >= 3) {
        // /broadcast/<tournament>/<round>/<round id>[/<game>], /broadcast/<tournament>/<tournament id>
        if (n >= 5 && gameId(p[3]) && gameId(p[4])) return simple(LinkKind::Chapter, p[3], p[4]);
        if (n >= 4 && gameId(p[3])) return simple(LinkKind::BroadcastRound, p[3]);
        if (n == 3 && gameId(p[2])) return simple(LinkKind::BroadcastTour, p[2]);
        return std::nullopt;
    }
    if (p[0] == "tournament" && n >= 2 && gameId(p[1])) return simple(LinkKind::Arena, p[1]);
    if (p[0] == "swiss" && n >= 2 && gameId(p[1])) return simple(LinkKind::Swiss, p[1]);
    if (p[0] == "@" && n >= 2 && userName(p[1])) return simple(LinkKind::Player, p[1]);
    if (p[0] == "bookmarks" && n == 1) return simple(LinkKind::Bookmarks, {});
    if (n <= 2) {
        std::optional<bool> white;
        if (n == 2) {
            if (p[1] == "white")
                white = true;
            else if (p[1] == "black")
                white = false;
            else
                return std::nullopt;
        }
        return game(p[0], white, a->fragment);
    }
    return std::nullopt;
}

Export exportOf(const Link& link) {
    const auto& api       = servers().api;
    const auto id         = urlEncode(link.id);
    constexpr auto extras = "clocks=true&evals=true&opening=true";
    Export e;
    switch (link.kind) {
        case LinkKind::Game:
            e.url = fmt::format("{}/game/export/{}?{}", api, id, extras);
            break;
        case LinkKind::Study:
            e.url = fmt::format("{}/api/study/{}.pgn?clocks=true&comments=true&variations=true", api, id);
            break;
        case LinkKind::Chapter:
            e.url = fmt::format("{}/api/study/{}/{}.pgn?clocks=true&comments=true&variations=true", api, id,
                                urlEncode(link.chapter));
            break;
        case LinkKind::BroadcastRound:
            e.url   = fmt::format("{}/api/broadcast/round/{}.pgn", api, id);
            e.scope = "study:read";
            break;
        case LinkKind::BroadcastTour:
            e.url   = fmt::format("{}/api/broadcast/{}.pgn", api, id);
            e.scope = "study:read";
            break;
        case LinkKind::Arena:
            e.url = fmt::format("{}/api/tournament/{}/games?{}", api, id, extras);
            break;
        case LinkKind::Swiss:
            e.url = fmt::format("{}/api/swiss/{}/games?{}", api, id, extras);
            break;
        case LinkKind::Bookmarks:
            e.url     = fmt::format("{}/api/games/export/bookmarks?{}", api, extras);
            e.account = true;
            break;
        case LinkKind::Player:
            break;  // the sync of a player's games
    }
    return e;
}

Answer<std::string> linkPgn(const Link& link, const std::string& token, const std::atomic<bool>* cancel) {
    const auto r = send(request("GET", exportOf(link).url, token, "application/x-chess-pgn"), cancel);
    auto a       = failedWith<std::string>(r);
    a.asked      = askedOf(link);
    if (!r.failed()) a.value = r.body;
    return a;
}

Answer<std::string> gamePgn(const std::string& id, const std::string& token, const std::atomic<bool>* cancel) {
    Link link;
    link.id = id;
    return linkPgn(link, token, cancel);
}

LinkImport importLink(study::Database& db, const Link& link, const std::string& token, std::atomic<uint64_t>& games,
                      const std::atomic<bool>& cancel, std::size_t commit_games) {
    LinkImport report;
    const auto e = exportOf(link);
    if (e.url.empty()) {
        report.error = "a player's games come through their sync";
        return report;
    }
    auto asked      = request("GET", e.url, token, "application/x-chess-pgn");
    asked.timeout_s = 60;

    std::string pending;  // what came of the games not read yet
    std::vector<study::ImportedGame> batch;
    std::string refused;
    const auto commit = [&] {
        if (batch.empty()) return true;
        try {
            const auto appended = db.append(std::move(batch));
            batch.clear();
            report.added += appended.added;
            report.duplicates += appended.duplicates;
            return true;
        } catch (const std::exception& ex) {
            refused = ex.what();
            return false;
        }
    };
    const auto take = [&](std::string_view text) {
        study::ImportedGame g;
        if (!study::readImportedGame(text, g)) return true;
        ++report.read;
        ++games;
        if (g.damaged) ++report.damaged;
        batch.push_back(std::move(g));
        return batch.size() < commit_games || commit();
    };

    HttpStream stream;
    stream.on_head = [&](int s, std::string_view) {
        report.status = s;
        return s == 200;
    };
    // Every game but the last that came is whole: another starts after it.
    stream.on_data = [&](std::string_view data) {
        pending.append(data);
        const auto pieces = study::splitGames(pending);
        if (pieces.size() >= 2) {
            for (std::size_t i = 0; i + 1 < pieces.size(); ++i)
                if (!take(pieces[i])) return false;
            pending.erase(0, static_cast<std::size_t>(pieces.back().data() - pending.data()));
        }
        return !cancel;
    };
    stream.cancel     = &cancel;
    stream.idle_s     = 60;
    const auto result = politeStream(asked, stream);

    report.cancelled = cancel;
    const bool whole = report.status == 200 && result.error.empty() && !report.cancelled && refused.empty();
    if (whole) {
        for (const auto piece : study::splitGames(pending))
            if (!take(piece)) break;
    }
    commit();
    if (!refused.empty()) {
        report.error = "the database could not take the games: " + refused;
    } else if (!whole && !report.cancelled) {  // refused by Lichess, or the network cut it
        static_cast<Failure&>(report) = responseOf(asked, result);
        report.asked                  = askedOf(link);
        if (!report.failed()) {
            report.net    = NetError::Offline;
            report.detail = "the games stopped coming";
        }
    }
    return report;
}

}  // namespace fcg::lichess
