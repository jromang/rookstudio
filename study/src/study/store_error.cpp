#include "store_error.hpp"

#include <system_error>

#include <tour/files.hpp>

namespace fcg::study {

StoreErrc errorCode(const std::exception& e) noexcept {
    if (const auto* store = dynamic_cast<const StoreError*>(&e)) return store->code();
    if (dynamic_cast<const tour::FolderBusy*>(&e)) return StoreErrc::Busy;
    if (const auto* system = dynamic_cast<const std::system_error*>(&e)) {
        const auto code = system->code();
        if (code == std::errc::no_space_on_device) return StoreErrc::DiskFull;
    }
    return StoreErrc::Other;
}

StoreErrc errorCode(const std::exception_ptr& e) noexcept {
    if (!e) return StoreErrc::Other;
    try {
        std::rethrow_exception(e);
    } catch (const std::exception& thrown) {
        return errorCode(thrown);
    } catch (...) {
    }
    return StoreErrc::Other;
}

}  // namespace fcg::study
