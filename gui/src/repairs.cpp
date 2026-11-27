#include "repairs.hpp"

#include <algorithm>

#include "i18n.hpp"

namespace fcg {

namespace {

void tryAgain(Notice& n, const RetryState& retry, const std::string& host) {
    if (retry.what != Retry::None) n.fixes.push_back(retryAfterPauseFix(retry.what, host, retry.pause, retry.queued));
}

// What a 403 says for a permission the account was linked without, by what it allows.
std::string lacksSentence(const std::string& scope) {
    if (scope == "study:read")
        return trf("Reading your studies needs a permission the account was linked without ({}).", scope);
    if (scope == "study:write")
        return trf("Writing to your studies needs a permission the account was linked without ({}).", scope);
    if (scope == "preference:write")
        return trf("Bookmarking a game needs a permission the account was linked without ({}).", scope);
    return trf("Lichess needs a permission the account was linked without ({}).", scope);
}

}  // namespace

Notice lichessNotice(const lichess::Failure& failure, std::string text, const LichessAccess& access,
                     const RetryState& retry, Notice::Level level) {
    using lichess::Asked;
    using lichess::NetError;
    Notice n{level, std::move(text), {}, {}};
    const bool lacks = !access.needs.empty() && (!access.linked || std::find(access.scopes.begin(), access.scopes.end(),
                                                                             access.needs) == access.scopes.end());
    const auto host  = retry.host;
    switch (failure.net) {
        case NetError::Unauthorized:
            if (failure.asked == Asked::Page || failure.asked == Asked::Link) break;  // private; a link's own card
            n.level = Notice::Level::Error;
            if (access.linked) {
                n.text   = TR("Lichess refused the token: it was revoked or has expired.");
                auto fix = linkLichessFix({}, true);
                if (lacks) fix.path = access.needs;  // asked too, as the request needs it
                n.fixes.push_back(std::move(fix));
            } else {
                n.text = TR("Lichess answers this to a linked account only.");
                n.fixes.push_back(linkLichessFix(access.needs, false));
            }
            break;
        case NetError::Forbidden:
            if (failure.asked == Asked::Link || !lacks) break;  // the user's no; a refusal no permission lifts
            n.level = Notice::Level::Warning;
            if (access.linked) {
                n.text = lacksSentence(access.needs);
            } else {
                n.text = TR("Lichess answers this to a linked account only.");
            }
            n.fixes.push_back(linkLichessFix(access.needs, access.linked));
            break;
        case NetError::RateLimited:
            if (retry.what == Retry::None) break;  // nothing asked again: the page's words
            n.level = Notice::Level::Warning;
            n.text  = TR("Lichess asked for a pause after too many requests.");
            tryAgain(n, retry, host);
            break;
        case NetError::Offline:
            tryAgain(n, retry, host);
            break;
        case NetError::Server:
            if (failure.status >= 500) tryAgain(n, retry, host);
            break;
        case NetError::None:
        case NetError::Cancelled:
        case NetError::TimedOut:
        case NetError::NotFound:
        case NetError::BadAnswer:
            break;
    }
    return n;
}

Notice linkNotice(const lichess::Failure& failure, std::string text) {
    Notice n{Notice::Level::Warning, std::move(text), {}, {}};
    if (failure.net == lichess::NetError::Unauthorized) {
        n.level = Notice::Level::Error;
        n.text  = TR("Lichess refused the token: it was revoked or has expired.");
    }
    if (failure.failed()) n.fixes.push_back(linkLichessFix({}, true));
    return n;
}

Notice engineOfferNotice(const lichess::Failure& failure, std::string text, const std::string& engine, bool in_library,
                         const LichessAccess& access) {
    if (failure.net == lichess::NetError::NotFound && in_library)
        return Notice{Notice::Level::Warning,
                      trf("Lichess no longer knows “{}” as your engine.", engine),
                      {},
                      {registerEngineAgainFix(engine)}};
    return lichessNotice(failure, std::move(text), access, {});
}

Notice netNotice(const NetFailure& failure, std::string text, const RetryState& retry, Notice::Level level) {
    // The host's pause is a wait, not a fault: a warning, as on the Lichess pages (the mockup's 429).
    if (failure.kind == NetFailure::Kind::RateLimited && level == Notice::Level::Error) level = Notice::Level::Warning;
    Notice n{level, std::move(text), {}, {}};
    const auto host = retry.host.empty() ? failure.host : retry.host;
    switch (failure.kind) {
        case NetFailure::Kind::Offline:
        case NetFailure::Kind::RateLimited:
        case NetFailure::Kind::Unexpected:
            tryAgain(n, retry, host);
            break;
        case NetFailure::Kind::Status:
            if (failure.status >= 500) tryAgain(n, retry, host);
            break;
        case NetFailure::Kind::Unwritable:
        case NetFailure::Kind::Unreadable:
            tryAgain(n, retry, host);
            if (!failure.path.empty()) {
                n.detail = failure.path.string();  // the file, out of the words
                n.fixes.push_back(openFolderFix(failure.path.parent_path().string()));
            }
            break;
        case NetFailure::Kind::None:
        case NetFailure::Kind::Cancelled:
        case NetFailure::Kind::NotFound:
            break;
    }
    return n;
}

Notice storeNotice(const StoreFailure& failure, int database, const std::filesystem::path& folder,
                   Notice::Level level) {
    Notice n{level, failure.said, {}, {}};
    switch (failure.code) {
        case study::StoreErrc::Damaged:
            if (database >= 0) n.fixes.push_back(checkDatabaseFix(database));
            break;
        case study::StoreErrc::DiskFull:
            if (!folder.empty()) n.fixes.push_back(openFolderFix(folder.string()));
            break;
        default:
            break;
    }
    return n;
}

Notice damagedGamesNotice(uint64_t count, int database) {
    return Notice{Notice::Level::Error,
                  trn("{} game of this database could not be read: a file is damaged.",
                      "{} games of this database could not be read: a file is damaged.", count, count),
                  {},
                  {checkDatabaseFix(database)}};
}

void RetryQueue::queue(Retry what, std::string host) {
    if (what == Retry::None || queued(what)) return;
    queued_.emplace_back(what, std::move(host));
}

bool RetryQueue::queued(Retry what) const {
    return std::any_of(queued_.begin(), queued_.end(), [&](const auto& q) { return q.first == what; });
}

void carryOutRetry(const Fix& fix, RetryQueue& queue, const PausedFor& paused_for, const AskAgain& ask) {
    const auto what = static_cast<Retry>(fix.value);
    if (what == Retry::None || queue.queued(what)) return;  // asked already: nothing more is sent
    if (paused_for(fix.path).count() > 0) {
        queue.queue(what, fix.path);
    } else {
        ask(what);
    }
}

RetryState OwnRetries::state(Retry what, const std::string& host) const {
    return RetryState{what, host, paused_for_(host), queue_.queued(what)};
}

bool OwnRetries::carryOut(const Fix& fix) {
    if (fix.kind != Fix::Kind::RetryAfterPause) return false;
    carryOutRetry(fix, queue_, paused_for_, ask_);
    return true;
}

void OwnRetries::poll() {
    if (queue_.empty()) return;
    for (const auto what : queue_.due(paused_for_)) ask_(what);
}

bool carryOutRepair(const Fix& fix, RepairActions& actions, RetryQueue& queue) {
    switch (fix.kind) {
        case Fix::Kind::LinkLichess:
            actions.linkLichess(fix.path);
            return true;
        case Fix::Kind::RetryAfterPause:
            carryOutRetry(
                fix, queue, [&](const std::string& host) { return actions.pausedFor(host); },
                [&](Retry what) { actions.retryNow(what); });
            return true;
        case Fix::Kind::CheckDatabase:
            actions.checkDatabase(fix.index);
            return true;
        case Fix::Kind::ChooseEngine:
            actions.chooseEngine();
            return true;
        case Fix::Kind::RegisterEngineAgain:
            actions.registerEngineAgain(fix.engine);
            return true;
        default:
            return false;
    }
}

}  // namespace fcg
