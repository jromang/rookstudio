// A Lichess link pasted (#47): what it is, recognised without asking Lichess; its export; its games brought into a
// database, against a server on this machine that answers as Lichess does.
#include <doctest/doctest.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

#include <fmt/include/fmt/format.h>
#include <study/store.hpp>

#include "answering_server.hpp"
#include "lichess_api.hpp"
#include "lichess_links.hpp"
#include "scripted_server.hpp"

using namespace fcg;
using namespace fcg::lichess;
using namespace fcg::test;
namespace fs = std::filesystem;

namespace {

Link link(LinkKind kind, std::string id, std::string chapter = {}) {
    Link l;
    l.kind    = kind;
    l.id      = std::move(id);
    l.chapter = std::move(chapter);
    return l;
}

const char* const TWO_GAMES =
    "[Event \"Club openings: Najdorf\"]\n[Site \"https://lichess.org/study/AbCdEfGh/Ch1aaaaa\"]\n[Result \"*\"]\n\n"
    "1. e4 { The most direct. } c5 2. Nf3 d6 *\n\n\n"
    "[Event \"Club openings: Dragon\"]\n[Site \"https://lichess.org/study/AbCdEfGh/Ch2bbbbb\"]\n[Result \"*\"]\n\n"
    "1. e4 c5 2. Nf3 d6 3. d4 cxd4 4. Nxd4 Nf6 5. Nc3 g6 *\n\n\n";

// Lichess, as far as these tests need it: a study's export, one game's, and a broadcast that wants an account.
struct Lichess {
    Lichess()
        : server(0, [this](const std::string& head, net::TcpConnection& c) {
              const auto path = requestPath(head);
              {
                  std::lock_guard lock(mutex);
                  paths.push_back(path);
                  heads.push_back(head);
              }
              std::string body;
              int status = 200;
              if (path.rfind("/api/study/AbCdEfGh.pgn", 0) == 0) {
                  body = TWO_GAMES;
              } else if (path.rfind("/game/export/abcd1234", 0) == 0) {
                  body =
                      "[Event \"Rated blitz game\"]\n[White \"A\"]\n[Black \"B\"]\n[Result \"1-0\"]\n\n1. e4 e5 1-0\n";
              } else if (path.rfind("/api/broadcast/round/", 0) == 0) {
                  status = head.find("Authorization: Bearer") == std::string::npos ? 401 : 200;
                  body   = status == 200 ? TWO_GAMES : "";
              } else {
                  status = 404;
              }
              c.sendAll(
                  fmt::format("HTTP/1.1 {} X\r\nContent-Type: application/x-chess-pgn\r\nContent-Length: {}\r\n"
                              "Connection: close\r\n\r\n{}",
                              status, body.size(), body));
          }) {
        REQUIRE(server.ok());
        servers().api = fmt::format("http://127.0.0.1:{}", server.port());
    }
    ~Lichess() { servers() = Servers{}; }
    std::string lastHead() {
        std::lock_guard lock(mutex);
        return heads.empty() ? std::string() : heads.back();
    }
    std::mutex mutex;
    std::vector<std::string> paths, heads;
    ScriptedServer server;
};

std::shared_ptr<study::Database> emptyDatabase(const std::string& name) {
    const auto dir = fs::temp_directory_path() / name;
    fs::remove_all(dir);
    return study::Database::create(dir);
}

}  // namespace

TEST_CASE("Lichess links: a game, with its side and its move, in every form Lichess writes it") {
    auto g = parseLink("https://lichess.org/abcd1234");
    REQUIRE(g);
    CHECK(*g == link(LinkKind::Game, "abcd1234"));
    g = parseLink("  lichess.org/abcd1234/black  ");
    REQUIRE(g);
    CHECK(g->id == "abcd1234");
    REQUIRE(g->white.has_value());
    CHECK_FALSE(*g->white);
    CHECK(parseLink("https://lichess.org/abcd1234/white")->white == true);
    CHECK(parseLink("https://lichess.org/abcd1234wxyz")->id == "abcd1234");  // a player's own id
    CHECK(parseLink("https://lichess.org/game/abcd1234")->id == "abcd1234");
    CHECK(parseLink("https://lichess.org/game/export/abcd1234")->id == "abcd1234");
    CHECK(parseLink("https://lichess.org/abcd1234#32")->ply == 32);
    CHECK(parseLink("https://fr.lichess.org/abcd1234?theme=dark")->id == "abcd1234");
    CHECK(parseLink("http://www.lichess.org/abcd1234")->kind == LinkKind::Game);
}

TEST_CASE("Lichess links: studies, broadcasts, tournaments, a player, the bookmarks") {
    CHECK(*parseLink("https://lichess.org/study/AbCdEfGh") == link(LinkKind::Study, "AbCdEfGh"));
    CHECK(*parseLink("https://lichess.org/study/AbCdEfGh/IjKlMnOp") == link(LinkKind::Chapter, "AbCdEfGh", "IjKlMnOp"));
    CHECK(*parseLink("https://lichess.org/broadcast/open-2026/round-8/Xy12AbCd") ==
          link(LinkKind::BroadcastRound, "Xy12AbCd"));
    CHECK(*parseLink("https://lichess.org/broadcast/open-2026/round-8/Xy12AbCd/Qw12Er34") ==
          link(LinkKind::Chapter, "Xy12AbCd", "Qw12Er34"));
    CHECK(*parseLink("https://lichess.org/broadcast/open-2026/Tour1234") == link(LinkKind::BroadcastTour, "Tour1234"));
    CHECK(*parseLink("https://lichess.org/tournament/QwErTy12") == link(LinkKind::Arena, "QwErTy12"));
    CHECK(*parseLink("https://lichess.org/swiss/Sw1ssId0") == link(LinkKind::Swiss, "Sw1ssId0"));
    CHECK(*parseLink("https://lichess.org/@/Joueur_A") == link(LinkKind::Player, "Joueur_A"));
    CHECK(*parseLink("https://lichess.org/@/Joueur_A/all") == link(LinkKind::Player, "Joueur_A"));
    CHECK(*parseLink("https://lichess.org/bookmarks") == link(LinkKind::Bookmarks, ""));
}

TEST_CASE("Lichess links: an address Lichess has but RookStudio does not know, and what is no Lichess address") {
    for (const auto* known_not :
         {"https://lichess.org/", "https://lichess.org/analysis", "https://lichess.org/training",
          "https://lichess.org/abcd1234/analysis", "https://lichess.org/forum/general"}) {
        CAPTURE(known_not);
        CHECK_FALSE(parseLink(known_not));
        CHECK(lichessAddress(known_not));
    }
    for (const auto* other : {"https://example.org/abcd1234", "lichess.org.example.com/abcd1234", "1. e4 e5 2. Nf3",
                              "https://lichess.org/abcd 1234", "", "https://lichess.orgx/abcd1234",
                              "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"}) {
        CAPTURE(other);
        CHECK_FALSE(parseLink(other));
        CHECK_FALSE(lichessAddress(other));
    }
}

TEST_CASE("Lichess links: each export, and what its token needs") {
    servers().api = "https://lichess.org";
    CHECK(exportOf(link(LinkKind::Game, "abcd1234")).url ==
          "https://lichess.org/game/export/abcd1234?clocks=true&evals=true&opening=true");
    CHECK(exportOf(link(LinkKind::Study, "AbCdEfGh")).url ==
          "https://lichess.org/api/study/AbCdEfGh.pgn?clocks=true&comments=true&variations=true");
    CHECK(exportOf(link(LinkKind::Chapter, "AbCdEfGh", "IjKlMnOp")).url ==
          "https://lichess.org/api/study/AbCdEfGh/IjKlMnOp.pgn?clocks=true&comments=true&variations=true");
    const auto round = exportOf(link(LinkKind::BroadcastRound, "Xy12AbCd"));
    CHECK(round.url == "https://lichess.org/api/broadcast/round/Xy12AbCd.pgn");
    CHECK(round.scope == "study:read");
    CHECK(exportOf(link(LinkKind::BroadcastTour, "Tour1234")).url == "https://lichess.org/api/broadcast/Tour1234.pgn");
    CHECK(exportOf(link(LinkKind::Arena, "QwErTy12")).url ==
          "https://lichess.org/api/tournament/QwErTy12/games?clocks=true&evals=true&opening=true");
    CHECK(exportOf(link(LinkKind::Swiss, "Sw1ssId0")).url ==
          "https://lichess.org/api/swiss/Sw1ssId0/games?clocks=true&evals=true&opening=true");
    const auto bookmarks = exportOf(link(LinkKind::Bookmarks, ""));
    CHECK(bookmarks.url == "https://lichess.org/api/games/export/bookmarks?clocks=true&evals=true&opening=true");
    CHECK(bookmarks.account);
    CHECK(exportOf(link(LinkKind::Player, "Joueur_A")).url.empty());  // the sync of a player's games
}

TEST_CASE("Lichess links: one game for the board, and a study's chapters into a database") {
    Lichess lichess;
    const auto pgn = gamePgn("abcd1234", "");
    REQUIRE(pgn.value);
    CHECK(pgn.value->find("[White \"A\"]") != std::string::npos);
    CHECK(lichess.lastHead().find("Authorization") == std::string::npos);  // no token: none sent
    CHECK_FALSE(gamePgn("zzzz9999", "").value);
    CHECK(gamePgn("zzzz9999", "").status == 404);
    CHECK(describe(gamePgn("zzzz9999", "")) == "Lichess has nothing at this address");

    const auto db = emptyDatabase("fcg-lichess-links-study");
    std::atomic<uint64_t> games{0};
    std::atomic<bool> cancel{false};
    const auto report = importLink(*db, link(LinkKind::Study, "AbCdEfGh"), "", games, cancel);
    CHECK_FALSE(report.failed());
    CHECK(report.read == 2);
    CHECK(report.added == 2);
    CHECK(games == 2);
    CHECK(db->snapshot()->games() == 2);
    // Again: nothing new.
    const auto again = importLink(*db, link(LinkKind::Study, "AbCdEfGh"), "", games, cancel);
    CHECK(again.added == 0);
    CHECK(again.duplicates == 2);
}

TEST_CASE("Lichess links: a broadcast without the account it needs says so; with a token it comes") {
    Lichess lichess;
    const auto db = emptyDatabase("fcg-lichess-links-round");
    std::atomic<uint64_t> games{0};
    std::atomic<bool> cancel{false};
    const auto refused = importLink(*db, link(LinkKind::BroadcastRound, "Xy12AbCd"), "", games, cancel);
    CHECK(refused.status == 401);
    CHECK(refused.net == NetError::Unauthorized);
    CHECK(describe(refused) == "Lichess refuses this without an account allowed to read broadcasts");
    CHECK(refused.added == 0);
    const auto taken = importLink(*db, link(LinkKind::BroadcastRound, "Xy12AbCd"), "a-token", games, cancel);
    CHECK_FALSE(taken.failed());
    CHECK(taken.added == 2);
    CHECK(lichess.lastHead().find("Authorization: Bearer a-token") != std::string::npos);
}

TEST_CASE("Lichess links: each failure of a link's export told by its kind, in the link's words") {
    AnsweringServer server;
    REQUIRE(server.ok());
    HostGate::instance().setPause(std::chrono::milliseconds(50));
    const auto db = emptyDatabase("fcg-lichess-links-failures");
    std::atomic<uint64_t> games{0};
    std::atomic<bool> cancel{false};
    const auto study = link(LinkKind::Study, "AbCdEfGh");
    const auto round = link(LinkKind::BroadcastRound, "Xy12AbCd");
    struct Case {
        int status;
        const char* body;
        NetError net;
    };
    for (const auto& [status, body, net] :
         {Case{401, "{}", NetError::Unauthorized}, Case{403, "{}", NetError::Forbidden},
          Case{404, "{}", NetError::NotFound}, Case{429, "{}", NetError::RateLimited},
          Case{502, "<html>Bad gateway</html>", NetError::Server}}) {
        CAPTURE(status);
        server.answer(status, body);
        CHECK(linkPgn(study, "").net == net);
        CHECK(gamePgn("abcd1234", "").net == net);
        const auto report = importLink(*db, round, "", games, cancel);
        CHECK(report.net == net);
        CHECK(report.status == status);
        CHECK(report.added == 0);
    }
    // Who may read it, said for what the link names.
    server.answer(403, "{}");
    CHECK(describe(linkPgn(study, "")) == "Lichess refuses it: private, or for another account");
    CHECK(describe(importLink(*db, round, "", games, cancel)) ==
          "Lichess refuses this without an account allowed to read broadcasts");
    CHECK(describe(linkPgn(link(LinkKind::Bookmarks, ""), "")) == "Lichess's bookmarks need a linked account");
    server.answer(404, "{}");
    CHECK(describe(importLink(*db, round, "", games, cancel)) == "Lichess has nothing at this address");
    // Nothing listens; cancelled in a pause; a cancel during the import is no failure.
    pointNowhere();
    CHECK(linkPgn(study, "").net == NetError::Offline);
    CHECK(importLink(*db, study, "", games, cancel).net == NetError::Offline);
    pointAtPausedHost("lichess-links.invalid");
    cancel = true;
    CHECK(linkPgn(study, "", &cancel).net == NetError::Cancelled);
    const auto stopped = importLink(*db, study, "", games, cancel);
    CHECK(stopped.cancelled);
    CHECK_FALSE(stopped.failed());
}
