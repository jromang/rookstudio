#pragma once

// A file mapped in memory, read-only: the databases' segments are read in place, the system keeping in memory what is
// used and dropping what is not. A file never changes once written (segments are immutable), so a mapping stays
// valid as long as it is held.

#include <cstddef>
#include <cstdint>
#include <filesystem>

namespace fcg::study {

class MappedFile {
   public:
    MappedFile() = default;
    // Throws std::runtime_error when the file cannot be opened or mapped. An empty file maps to nothing.
    explicit MappedFile(const std::filesystem::path& file);
    ~MappedFile();
    MappedFile(MappedFile&& other) noexcept;
    MappedFile& operator=(MappedFile&& other) noexcept;
    MappedFile(const MappedFile&)            = delete;
    MappedFile& operator=(const MappedFile&) = delete;

    [[nodiscard]] const uint8_t* data() const noexcept { return data_; }
    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    // Read here and there rather than from its start: the system then reads the pages asked for, not their
    // neighbours too (POSIX; nothing elsewhere).
    void adviseRandom() const noexcept;

   private:
    void close() noexcept;

    const uint8_t* data_ = nullptr;
    std::size_t size_    = 0;
#ifdef _WIN32
    void* file_    = nullptr;
    void* mapping_ = nullptr;
#endif
};

}  // namespace fcg::study
