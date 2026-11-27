#include "net_failure.hpp"

#include "i18n.hpp"

namespace fcg {

std::string describe(const NetFailure& failure) {
    switch (failure.kind) {
        case NetFailure::Kind::None:
            return {};
        case NetFailure::Kind::Cancelled:
            return TR("cancelled");
        case NetFailure::Kind::Offline:
            return trf("{} cannot be reached from here", failure.host);
        case NetFailure::Kind::RateLimited:
            return trf("{} asks to wait: too many requests", failure.host);
        case NetFailure::Kind::NotFound:
        case NetFailure::Kind::Status:
            return trf("{} answered {}", failure.host, failure.status);
        case NetFailure::Kind::Unwritable:
            return TR("the file could not be written (is the disk full?)");
        case NetFailure::Kind::Unreadable:
            return TR("the file could not be read to its end");
        case NetFailure::Kind::Unexpected:
            return TR("the file that came is not the one expected");
    }
    return {};
}

std::string siteSentence(const NetFailure& failure, const std::string& site) {
    switch (failure.kind) {
        case NetFailure::Kind::Offline:
            return trf("{} cannot be reached from here.", site);
        case NetFailure::Kind::RateLimited:
            return trf("{} asks to wait: too many requests.", site);
        default:
            return {};
    }
}

}  // namespace fcg
