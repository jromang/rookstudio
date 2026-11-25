#pragma once

// For the readers that take a file name: the input written to a file of this process's own, the same one each time for
// an extension.
// For those that take a folder (a database's): a folder of this process's own, and the input read as changes to files
// a target made sound (a template), so that the fuzzer works past the checks a file meets when it opens.

#include <unistd.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <string>

namespace fcg::fuzz {

inline std::filesystem::path inputFile(const uint8_t* data, std::size_t size, const std::string& extension) {
    const auto file = std::filesystem::temp_directory_path() / ("fcg-fuzz-" + std::to_string(getpid()) + extension);
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
    return file;
}

// A folder `name` of this process's own, made when missing.
inline std::filesystem::path ownFolder(const std::string& name) {
    const auto dir = std::filesystem::temp_directory_path() / ("fcg-fuzz-" + std::to_string(getpid())) / name;
    std::filesystem::create_directories(dir);
    return dir;
}

inline void writeFile(const std::filesystem::path& file, const std::string& bytes) {
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

inline std::string readFile(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

// The input as changes to `files`, four bytes each: the file (the first byte's low three bits, modulo their number),
// where in it (two bytes, little-endian, modulo its size) and a byte written there. With the first byte's 0x80 bit, the
// file is cut or grown (with that byte) to that size instead; with its 0x40 bit, the eight bytes that follow are
// written there, a number (an offset, a count) at once. A file empty when a byte is written is left so.
inline void applyChanges(const uint8_t* data, std::size_t size, std::initializer_list<std::string*> files) {
    if (files.size() == 0) return;
    for (std::size_t at = 0; at + 4 <= size;) {
        const uint8_t how       = data[at];
        std::string& file       = **(files.begin() + (how & 7) % files.size());
        const std::size_t where = data[at + 1] | std::size_t{data[at + 2]} << 8;
        const char value        = static_cast<char>(data[at + 3]);
        at += 4;
        if (how & 0x80) {
            file.resize(where, value);
        } else if ((how & 0x40) && at + 8 <= size) {
            if (file.size() >= 8) std::memcpy(file.data() + where % (file.size() - 7), data + at, 8);
            at += 8;
        } else if (!file.empty()) {
            file[where % file.size()] = value;
        }
    }
}

}  // namespace fcg::fuzz
