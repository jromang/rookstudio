// The Chess space's errors with what repairs them (#141): each failure a page keeps made into its notice, and the
// repairs carried out, a Try again during its host's pause queued until it ends, a module's own too (#146).

#include <doctest/doctest.hpp>

#include <chrono>
#include <map>
#include <string>
#include <vector>

#include "guide.hpp"
#include "lichess_account.hpp"
#include "lichess_engine.hpp"
#include "repairs.hpp"

using namespace fcg;
using namespace std::chrono_literals;
namespace fs = std::filesystem;

namespace {

lichess::Failure failure(lichess::NetError net, int status = 0, lichess::Asked asked = lichess::Asked::Anything) {
    lichess::Failure f;
    f.net    = net;
    f.status = status;
    f.asked  = asked;
    return f;
}

NetFailure netFailure(NetFailure::Kind kind, int status = 0, std::string host = "theweekinchess.com",
                      fs::path path = {}) {
    NetFailure f;
    f.kind   = kind;
    f.status = status;
    f.host   = std::move(host);
    f.path   = std::move(path);
    return f;
}

LichessAccess linked(std::vector<std::string> scopes = {}, std::string needs = {}) {
    LichessAccess a;
    a.linked = true;
    a.scopes = std::move(scopes);
    a.needs  = std::move(needs);
    return a;
}

RetryState retry(Retry what, std::chrono::milliseconds pause = 0ms, bool queued = false,
                 std::string host = "lichess.org") {
    return RetryState{what, std::move(host), pause, queued};
}

bool contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

// What the repairs asked of the program.
struct FakeActions final : RepairActions {
    std::vector<std::string> links;
    std::vector<Retry> retried;
    std::map<std::string, std::chrono::milliseconds> pauses;
    std::vector<int> checked;
    int chosen = 0;
    std::vector<std::string> registered;

    void linkLichess(const std::string& scope) override { links.push_back(scope); }
    void retryNow(Retry what) override { retried.push_back(what); }
    [[nodiscard]] std::chrono::milliseconds pausedFor(const std::string& host) const override {
        const auto it = pauses.find(host);
        return it == pauses.end() ? 0ms : it->second;
    }
    void checkDatabase(int database) override { checked.push_back(database); }
    void chooseEngine() override { ++chosen; }
    void registerEngineAgain(const std::string& engine) override { registered.push_back(engine); }
};

}  // namespace

TEST_CASE("repairs: a token refused (401) offers Link Lichess again…, an error") {
    const auto n = lichessNotice(failure(lichess::NetError::Unauthorized, 401),
                                 "Lichess did not give the list: Lichess refused the token: link the account again.",
                                 linked({"study:read"}), retry(Retry::StudiesList));
    CHECK(n.level == Notice::Level::Error);
    CHECK(n.text == "Lichess refused the token: it was revoked or has expired.");
    REQUIRE(n.fixes.size() == 1);
    CHECK(n.fixes[0].kind == Fix::Kind::LinkLichess);
    CHECK(n.fixes[0].label == "Link Lichess again…");
    CHECK(n.fixes[0].path.empty());  // the same permissions
    CHECK(contains(n.fixes[0].tooltip, "lichess.org"));
    CHECK(contains(n.fixes[0].tooltip, "Nothing is sent"));
    CHECK(noticeTopic(n) == std::string(guide::topics::LichessLink));

    // The permission the request needs, which the account lacks, asked too.
    const auto more =
        lichessNotice(failure(lichess::NetError::Unauthorized, 401), "", linked({}, "study:write"), RetryState{});
    REQUIRE(more.fixes.size() == 1);
    CHECK(more.fixes[0].label == "Link Lichess again…");
    CHECK(more.fixes[0].path == "study:write");

    // No account linked: the link to make, the permission with it.
    const auto none = lichessNotice(failure(lichess::NetError::Unauthorized, 401), "", LichessAccess{}, RetryState{});
    REQUIRE(none.fixes.size() == 1);
    CHECK(none.fixes[0].label == "Link the Lichess account…");
    CHECK(none.text == "Lichess answers this to a linked account only.");

    // A page private, or another account's: no link opens it.
    const auto page = lichessNotice(failure(lichess::NetError::Unauthorized, 401, lichess::Asked::Page),
                                    "Lichess refuses it", linked(), RetryState{});
    CHECK(page.fixes.empty());
    CHECK(page.text == "Lichess refuses it");
}

TEST_CASE("repairs: a permission the account lacks (403) is named, with the button that asks for it") {
    const auto n = lichessNotice(failure(lichess::NetError::Forbidden, 403),
                                 "Lichess did not take the changes: the token may not do it.",
                                 linked({"study:read"}, lichess::SCOPE_STUDY_WRITE), RetryState{});
    CHECK(n.level == Notice::Level::Warning);
    CHECK(contains(n.text, "Writing to your studies"));
    CHECK(contains(n.text, "(study:write)"));
    REQUIRE(n.fixes.size() == 1);
    CHECK(n.fixes[0].kind == Fix::Kind::LinkLichess);
    CHECK(n.fixes[0].label == "Allow writing studies…");
    CHECK(n.fixes[0].path == "study:write");
    CHECK(contains(n.fixes[0].tooltip, "study:write"));
    CHECK(noticeTopic(n) == nullptr);  // as the mockup: the guide's ? on the plain link only

    CHECK(allowScopeLabel("study:read") == "Allow reading studies…");
    CHECK(allowScopeLabel("preference:write") == "Allow bookmarking games…");
    CHECK(allowScopeLabel("engine:write") == "Allow registering engines…");
    CHECK(allowScopeLabel("x:y") == "Allow x:y…");

    // The account has it: Lichess refuses for another reason (a study of another's), nothing to allow.
    const auto has = lichessNotice(failure(lichess::NetError::Forbidden, 403), "Lichess did not take the changes.",
                                   linked({"study:write"}, "study:write"), RetryState{});
    CHECK(has.fixes.empty());
    CHECK(has.text == "Lichess did not take the changes.");

    // A refusal for nothing in particular: the page's words.
    CHECK(lichessNotice(failure(lichess::NetError::Forbidden, 403), "words", linked(), RetryState{}).fixes.empty());
}

TEST_CASE("repairs: a 429 counts its pause down on Try again, which a click queues, and says Try again after") {
    const auto during =
        lichessNotice(failure(lichess::NetError::RateLimited, 429), "Too many requests in a short time…",
                      LichessAccess{}, retry(Retry::BoardTables, 42s, false, "tablebase.lichess.ovh"));
    CHECK(during.level == Notice::Level::Warning);
    CHECK(during.text == "Lichess asked for a pause after too many requests.");
    REQUIRE(during.fixes.size() == 1);
    const auto& fix = during.fixes[0];
    CHECK(fix.kind == Fix::Kind::RetryAfterPause);
    CHECK(fix.label == "Try again in 0:42");
    CHECK_FALSE(fix.disabled);
    CHECK(static_cast<Retry>(fix.value) == Retry::BoardTables);
    CHECK(fix.path == "tablebase.lichess.ovh");
    CHECK(contains(fix.tooltip, "nothing is sent before"));

    // Queued: it waits, disabled, and counts on.
    const auto queued = lichessNotice(failure(lichess::NetError::RateLimited, 429), "", LichessAccess{},
                                      retry(Retry::BoardTables, 41s, true, "tablebase.lichess.ovh"));
    REQUIRE(queued.fixes.size() == 1);
    CHECK(queued.fixes[0].label == "Asks again in 0:41");
    CHECK(queued.fixes[0].disabled);

    // The pause over: a plain Try again.
    const auto after = lichessNotice(failure(lichess::NetError::RateLimited, 429), "", LichessAccess{},
                                     retry(Retry::BoardTables, 0ms, false, "tablebase.lichess.ovh"));
    REQUIRE(after.fixes.size() == 1);
    CHECK(after.fixes[0].label == "Try again");
    CHECK_FALSE(after.fixes[0].disabled);

    // A page that asks nothing again (an online game's action): its words, no button.
    const auto none = lichessNotice(failure(lichess::NetError::RateLimited, 429),
                                    "The draw offer did not reach Lichess.", linked(), RetryState{});
    CHECK(none.fixes.empty());
    CHECK(none.text == "The draw offer did not reach Lichess.");

    // TWIC's own pause, from its NetFailure's host.
    const auto twic = netNotice(netFailure(NetFailure::Kind::RateLimited, 429), "theweekinchess.com asks to wait.",
                                RetryState{Retry::TwicIndex, {}, 75s, false});
    CHECK(twic.level == Notice::Level::Warning);  // a wait, not a fault, though the page says its errors in red
    REQUIRE(twic.fixes.size() == 1);
    CHECK(twic.fixes[0].label == "Try again in 1:15");
    CHECK(twic.fixes[0].path == "theweekinchess.com");

    CHECK(pauseClock(42000ms) == "0:42");
    CHECK(pauseClock(41001ms) == "0:42");  // rounded up: never 0:00 while it pauses
    CHECK(pauseClock(0ms) == "0:00");
    CHECK(pauseClock(-5ms) == "0:00");
}

TEST_CASE("repairs: an outage or a site in trouble offers Try again, in the page's words") {
    const auto lichess = lichessNotice(failure(lichess::NetError::Offline), "Lichess cannot be reached from here.",
                                       linked(), retry(Retry::StudiesList));
    CHECK(lichess.text == "Lichess cannot be reached from here.");
    REQUIRE(lichess.fixes.size() == 1);
    CHECK(lichess.fixes[0].label == "Try again");
    CHECK(contains(lichess.fixes[0].tooltip, "lichess.org"));

    const auto down = lichessNotice(failure(lichess::NetError::Server, 503), "Lichess answered 503", linked(),
                                    retry(Retry::StudiesList));
    CHECK(down.fixes.size() == 1);
    // Lichess refusing in its own words (a study's quota): nothing to try again.
    const auto quota = lichessNotice(failure(lichess::NetError::Server, 400), "This study holds 64 chapters.", linked(),
                                     retry(Retry::StudiesList));
    CHECK(quota.fixes.empty());

    const auto twic = netNotice(netFailure(NetFailure::Kind::Offline), "theweekinchess.com cannot be reached.",
                                retry(Retry::TwicIndex, 0ms, false, ""));
    CHECK(twic.level == Notice::Level::Error);
    REQUIRE(twic.fixes.size() == 1);
    CHECK(twic.fixes[0].label == "Try again");
    CHECK(twic.fixes[0].path == "theweekinchess.com");
    CHECK(static_cast<Retry>(twic.fixes[0].value) == Retry::TwicIndex);
    CHECK(netNotice(netFailure(NetFailure::Kind::Status, 503), "", retry(Retry::TwicIndex)).fixes.size() == 1);
    CHECK(netNotice(netFailure(NetFailure::Kind::Status, 403), "", retry(Retry::TwicIndex)).fixes.empty());
}

TEST_CASE("repairs: a file that could not be written leaves the sentence, its folder a click away") {
    const fs::path file = fs::path("profile") / "sources" / "twic" / "twic1671g.zip";
    const auto n        = netNotice(netFailure(NetFailure::Kind::Unwritable, 0, "theweekinchess.com", file),
                                    "The games could not be added: the file could not be written (is the disk full?)",
                                    retry(Retry::FileAdd, 0ms, false, "theweekinchess.com"));
    CHECK(n.level == Notice::Level::Error);
    CHECK_FALSE(contains(n.text, "twic1671g.zip"));
    CHECK(n.detail == file.string());
    REQUIRE(n.fixes.size() == 2);
    CHECK(n.fixes[0].kind == Fix::Kind::RetryAfterPause);  // the repair first,
    CHECK(n.fixes[1].kind == Fix::Kind::OpenFolder);       // then the folder
    CHECK(n.fixes[1].label == "Open the folder");
    CHECK(n.fixes[1].path == file.parent_path().string());
    CHECK(n.fixes[1].tooltip == file.parent_path().string());

    // Without a Try again (a reading's Go on is beside it): the folder alone.
    const auto read = netNotice(netFailure(NetFailure::Kind::Unreadable, 0, "", file), "The reading stopped.", {},
                                Notice::Level::Warning);
    REQUIRE(read.fixes.size() == 1);
    CHECK(read.fixes[0].kind == Fix::Kind::OpenFolder);
    // Without a file: nothing to open.
    CHECK(netNotice(netFailure(NetFailure::Kind::Unwritable), "", {}).fixes.empty());
}

TEST_CASE("repairs: a damaged database offers its check, a full disk its folder") {
    const fs::path folder = fs::path("profile") / "databases" / "mine";
    const auto damaged    = storeNotice({"A file of the database is damaged.", study::StoreErrc::Damaged}, 3, folder);
    CHECK(damaged.level == Notice::Level::Error);
    CHECK(damaged.text == "A file of the database is damaged.");
    REQUIRE(damaged.fixes.size() == 1);
    CHECK(damaged.fixes[0].kind == Fix::Kind::CheckDatabase);
    CHECK(damaged.fixes[0].index == 3);
    CHECK(damaged.fixes[0].label == "Check the database…");
    CHECK(noticeTopic(damaged) == std::string(guide::topics::Maintenance));
    // Its index unknown (a tree of several databases): nothing to open.
    CHECK(storeNotice({"damaged", study::StoreErrc::Damaged}, -1, folder).fixes.empty());

    const auto full = storeNotice({"The disk is full.", study::StoreErrc::DiskFull}, 3, folder);
    REQUIRE(full.fixes.size() == 1);
    CHECK(full.fixes[0].kind == Fix::Kind::OpenFolder);
    CHECK(full.fixes[0].path == folder.string());

    CHECK(storeNotice({"read-only", study::StoreErrc::ReadOnly}, 3, folder).fixes.empty());
    CHECK(storeNotice({"words", study::StoreErrc::Other}, 3, folder).fixes.empty());

    const auto games = damagedGamesNotice(4, 2);
    CHECK(games.text == "4 games of this database could not be read: a file is damaged.");
    REQUIRE(games.fixes.size() == 1);
    CHECK(games.fixes[0].kind == Fix::Kind::CheckDatabase);
    CHECK(games.fixes[0].index == 2);
}

TEST_CASE("repairs: an engine Lichess no longer knows offers Register it again, while it is in the library") {
    const auto gone = engineOfferNotice(failure(lichess::NetError::NotFound, 404), "Lichess gave nothing.", "Gaia 1.0",
                                        true, linked({"engine:read", "engine:write"}, "engine:write"));
    CHECK(gone.text == "Lichess no longer knows “Gaia 1.0” as your engine.");
    REQUIRE(gone.fixes.size() == 1);
    CHECK(gone.fixes[0].kind == Fix::Kind::RegisterEngineAgain);
    CHECK(gone.fixes[0].engine == "Gaia 1.0");
    CHECK(gone.fixes[0].label == "Register it again");
    CHECK(contains(gone.fixes[0].tooltip, "Gaia 1.0"));
    CHECK(contains(gone.fixes[0].tooltip, "lichess.org"));

    // Out of the library: nothing to register.
    CHECK(engineOfferNotice(failure(lichess::NetError::NotFound, 404), "x", "Gaia", false, linked()).fixes.empty());
    // A permission missing: the link that asks for it.
    const auto scope = engineOfferNotice(failure(lichess::NetError::Forbidden, 403), "x", "Gaia", true,
                                         linked({}, lichess::SCOPE_ENGINE_WRITE));
    REQUIRE(scope.fixes.size() == 1);
    CHECK(scope.fixes[0].kind == Fix::Kind::LinkLichess);
    CHECK(scope.fixes[0].label == "Allow registering engines…");
}

TEST_CASE("repairs: a link that failed offers Link Lichess again…, a token refused said as such") {
    const auto refused = linkNotice(failure(lichess::NetError::Unauthorized, 401, lichess::Asked::Link),
                                    "Lichess gave nothing (Lichess refused the token: link the account again).");
    CHECK(refused.level == Notice::Level::Error);
    CHECK(refused.text == "Lichess refused the token: it was revoked or has expired.");
    REQUIRE(refused.fixes.size() == 1);
    CHECK(refused.fixes[0].label == "Link Lichess again…");

    const auto no = linkNotice(failure(lichess::NetError::Forbidden, 0, lichess::Asked::Link), "You refused it.");
    CHECK(no.level == Notice::Level::Warning);
    CHECK(no.text == "You refused it.");
    CHECK(no.fixes.size() == 1);
}

TEST_CASE("repairs: the cases no gesture repairs stay notices without a button") {
    // A chess.com player that does not exist.
    const auto player =
        netNotice(netFailure(NetFailure::Kind::NotFound, 404, "api.chess.com"),
                  "chess.com knows no player of that name.", retry(Retry::ChesscomMonths), Notice::Level::Warning);
    CHECK(player.fixes.empty());
    CHECK(player.level == Notice::Level::Warning);
    // Lichess has no such game, a cancelled request, an answer that cannot be read.
    for (const auto net : {lichess::NetError::NotFound, lichess::NetError::Cancelled, lichess::NetError::BadAnswer})
        CHECK(lichessNotice(failure(net), "words", linked(), retry(Retry::StudiesList)).fixes.empty());
    // A problem of the program's own (a database that refused the games).
    CHECK(
        lichessNotice(lichess::Failure::own("refused"), "refused", linked(), retry(Retry::StudiesList)).fixes.empty());
}

TEST_CASE("repairs: a Try again during the pause is queued and sent once, when it ends; the others at once") {
    FakeActions actions;
    RetryQueue queue;
    actions.pauses["tablebase.lichess.ovh"] = 42s;

    const auto during = retryAfterPauseFix(Retry::BoardTables, "tablebase.lichess.ovh", 42s);
    CHECK(carryOutRepair(during, actions, queue));
    CHECK(actions.retried.empty());  // nothing sent during the pause
    CHECK(queue.queued(Retry::BoardTables));
    CHECK(carryOutRepair(during, actions, queue));  // a second click: still once
    const auto paused_for = [&](const std::string& host) { return actions.pausedFor(host); };
    CHECK(queue.due(paused_for).empty());

    actions.pauses["tablebase.lichess.ovh"] = 0ms;  // the pause over
    const auto due                          = queue.due(paused_for);
    REQUIRE(due.size() == 1);
    CHECK(due[0] == Retry::BoardTables);
    CHECK_FALSE(queue.queued(Retry::BoardTables));
    CHECK(queue.due(paused_for).empty());

    // Outside a pause: at once, nothing queued.
    CHECK(carryOutRepair(retryAfterPauseFix(Retry::TwicIndex, "theweekinchess.com"), actions, queue));
    REQUIRE(actions.retried.size() == 1);
    CHECK(actions.retried[0] == Retry::TwicIndex);
    CHECK(queue.empty());

    CHECK(carryOutRepair(linkLichessFix("study:write"), actions, queue));
    CHECK(carryOutRepair(linkLichessFix(), actions, queue));
    CHECK(actions.links == std::vector<std::string>{"study:write", ""});
    CHECK(carryOutRepair(checkDatabaseFix(5), actions, queue));
    CHECK(actions.checked == std::vector<int>{5});
    CHECK(carryOutRepair(chooseEngineFix(), actions, queue));
    CHECK(actions.chosen == 1);
    CHECK(carryOutRepair(registerEngineAgainFix("Gaia"), actions, queue));
    CHECK(actions.registered == std::vector<std::string>{"Gaia"});
    // Not a repair: App::applyFix() carries it out.
    CHECK_FALSE(carryOutRepair(openFolderFix("/tmp"), actions, queue));
    CHECK_FALSE(carryOutRepair(openLogFix(), actions, queue));

    // The labels of the other repairs.
    CHECK(chooseEngineFix().label == "Choose an engine…");
    CHECK(contains(chooseEngineFix().tooltip, "engine picker"));
}

TEST_CASE("repairs: a module's own Try again, at once, or queued through its host's pause and asked once (#146)") {
    std::map<std::string, std::chrono::milliseconds> pauses;
    std::vector<Retry> asked;
    OwnRetries retries(
        [&](const std::string& host) {
            const auto it = pauses.find(host);
            return it == pauses.end() ? 0ms : it->second;
        },
        [&](Retry what) { asked.push_back(what); });

    // Outside a pause: asked at once.
    CHECK(retries.state(Retry::TvChannels, "lichess.org").pause == 0ms);
    CHECK(retries.carryOut(retryAfterPauseFix(Retry::TvChannels, "lichess.org")));
    CHECK(asked == std::vector<Retry>{Retry::TvChannels});

    // During its host's pause: counted down on its button; a click queues it, nothing asked before the pause ends.
    pauses["lichess.org"] = 42s;
    auto state            = retries.state(Retry::ArenaList, "lichess.org");
    CHECK(state.what == Retry::ArenaList);
    CHECK(state.host == "lichess.org");
    CHECK(state.pause == 42s);
    CHECK_FALSE(state.queued);
    const auto during = lichessNotice(failure(lichess::NetError::RateLimited, 429), "", linked(), state);
    REQUIRE(during.fixes.size() == 1);
    CHECK(during.fixes[0].label == "Try again in 0:42");
    CHECK(retries.carryOut(during.fixes[0]));
    CHECK(retries.carryOut(during.fixes[0]));  // a second click: still once
    retries.poll();
    CHECK(asked.size() == 1);
    CHECK(retries.queued(Retry::ArenaList));
    state = retries.state(Retry::ArenaList, "lichess.org");
    CHECK(state.queued);
    const auto queued = lichessNotice(failure(lichess::NetError::RateLimited, 429), "", linked(), state);
    REQUIRE(queued.fixes.size() == 1);
    CHECK(queued.fixes[0].label == "Asks again in 0:42");
    CHECK(queued.fixes[0].disabled);
    // Another host's pause holds nothing of this one's.
    CHECK(retries.state(Retry::TvChannels, "tablebase.lichess.ovh").pause == 0ms);

    // The pause over: asked once.
    pauses["lichess.org"] = 0ms;
    retries.poll();
    CHECK(asked == std::vector<Retry>{Retry::TvChannels, Retry::ArenaList});
    CHECK_FALSE(retries.queued(Retry::ArenaList));
    retries.poll();
    CHECK(asked.size() == 2);

    // Not a Try again: left to the program (context::Repairs::repair()).
    CHECK_FALSE(retries.carryOut(linkLichessFix()));
    CHECK_FALSE(retries.carryOut(openFolderFix("/x")));
    CHECK_FALSE(retries.carryOut(checkDatabaseFix(1)));
    // A Try again that names nothing: nothing asked.
    CHECK(retries.carryOut(retryAfterPauseFix(Retry::None, "lichess.org")));
    CHECK(asked.size() == 2);
}
