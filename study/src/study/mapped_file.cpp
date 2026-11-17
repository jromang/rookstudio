#include "mapped_file.hpp"

#include <stdexcept>
#include <utility>

#ifdef _WIN32
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    include <windows.h>
#else
#    include <fcntl.h>
#    include <sys/mman.h>
#    include <sys/stat.h>
#    include <unistd.h>
#endif

namespace fcg::study {

MappedFile::MappedFile(const std::filesystem::path& file) {
#ifdef _WIN32
    file_ = CreateFileW(file.wstring().c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr,
                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file_ == INVALID_HANDLE_VALUE) {
        file_ = nullptr;
        throw std::runtime_error("cannot open " + file.string());
    }
    LARGE_INTEGER size;
    if (!GetFileSizeEx(file_, &size)) {
        close();
        throw std::runtime_error("cannot read the size of " + file.string());
    }
    size_ = static_cast<std::size_t>(size.QuadPart);
    if (size_ == 0) return;
    mapping_ = CreateFileMappingW(file_, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (!mapping_) {
        close();
        throw std::runtime_error("cannot map " + file.string());
    }
    data_ = static_cast<const uint8_t*>(MapViewOfFile(mapping_, FILE_MAP_READ, 0, 0, 0));
    if (!data_) {
        close();
        throw std::runtime_error("cannot map " + file.string());
    }
#else
    const int fd = ::open(file.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) throw std::runtime_error("cannot open " + file.string());
    struct stat st{};
    if (::fstat(fd, &st) != 0) {
        ::close(fd);
        throw std::runtime_error("cannot read the size of " + file.string());
    }
    size_ = static_cast<std::size_t>(st.st_size);
    if (size_ > 0) {
        void* p = ::mmap(nullptr, size_, PROT_READ, MAP_SHARED, fd, 0);
        if (p == MAP_FAILED) {
            ::close(fd);
            size_ = 0;
            throw std::runtime_error("cannot map " + file.string());
        }
        data_ = static_cast<const uint8_t*>(p);
    }
    ::close(fd);  // the mapping keeps the file
#endif
}

MappedFile::~MappedFile() { close(); }

MappedFile::MappedFile(MappedFile&& other) noexcept { *this = std::move(other); }

MappedFile& MappedFile::operator=(MappedFile&& other) noexcept {
    if (this != &other) {
        close();
        data_ = std::exchange(other.data_, nullptr);
        size_ = std::exchange(other.size_, 0);
#ifdef _WIN32
        file_    = std::exchange(other.file_, nullptr);
        mapping_ = std::exchange(other.mapping_, nullptr);
#endif
    }
    return *this;
}

void MappedFile::close() noexcept {
#ifdef _WIN32
    if (data_) UnmapViewOfFile(data_);
    if (mapping_) CloseHandle(mapping_);
    if (file_) CloseHandle(file_);
    mapping_ = nullptr;
    file_    = nullptr;
#else
    if (data_) ::munmap(const_cast<uint8_t*>(data_), size_);
#endif
    data_ = nullptr;
    size_ = 0;
}

}  // namespace fcg::study
