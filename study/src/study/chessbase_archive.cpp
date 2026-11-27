// Ported from cbvault 0.1.6, MIT License, Copyright (c) 2026 cbvault Contributors (archive/, des.rs).
//
// The .cbv archive and the .cbz, a .cbv enciphered with a password; and, written for RookStudio, the archive of a 2CBH
// database: a .2cbv, a plain zip archive of the database's files, or a .2cbz, that zip enciphered as a .cbz is, padded
// with zeros to the cipher's blocks of eight bytes.
//
// A .cbv is a header (08 00, the member count in two bytes, AD 00 03 00), a table of 173-byte member records (a
// NUL-terminated name in 128 bytes, then the member's offset, stored size and size, in 32 bits and again in 64), and
// the members' streams in the table's order. A stream is a run of blocks: a two-byte payload length, two bytes not
// used here, and the payload, whose first byte names how the rest is stored: as it is (0), as LZ tokens (1), as a
// Huffman code (2), or as a Huffman code whose output is LZ tokens (3). An LZ token may copy bytes produced by an
// earlier block of the member, 4,098 bytes back at most.
//
// A .cbz is a .cbv whose every byte is enciphered with DES in ECB mode, the key made of the password: the password
// itself when it has eight bytes, repeated when shorter, folded when longer.
//
// Members are unpacked as they are read: a file of names whole, the others a block at a time, the blocks read last
// kept, and every sixteenth block's last 4,098 bytes too, so that a block far back is unpacked again from there.

#include <algorithm>
#include <array>
#include <cstring>
#include <list>
#include <map>
#include <optional>
#include <stdexcept>

#include <zlib.h>
#include <tour/util.hpp>

#include "chessbase_detail.hpp"
#include "mapped_file.hpp"
#include "sources.hpp"

namespace fcg::study::chessbase {

namespace {

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------------------------------------------
// DES, in ECB mode
// ---------------------------------------------------------------------------------------------------------------

constexpr uint8_t IP[64]     = {58, 50, 42, 34, 26, 18, 10, 2,  60, 52, 44, 36, 28, 20, 12, 4,  62, 54, 46, 38, 30, 22,
                                14, 6,  64, 56, 48, 40, 32, 24, 16, 8,  57, 49, 41, 33, 25, 17, 9,  1,  59, 51, 43, 35,
                                27, 19, 11, 3,  61, 53, 45, 37, 29, 21, 13, 5,  63, 55, 47, 39, 31, 23, 15, 7};
constexpr uint8_t IP_INV[64] = {40, 8,  48, 16, 56, 24, 64, 32, 39, 7,  47, 15, 55, 23, 63, 31, 38, 6,  46, 14, 54, 22,
                                62, 30, 37, 5,  45, 13, 53, 21, 61, 29, 36, 4,  44, 12, 52, 20, 60, 28, 35, 3,  43, 11,
                                51, 19, 59, 27, 34, 2,  42, 10, 50, 18, 58, 26, 33, 1,  41, 9,  49, 17, 57, 25};
constexpr uint8_t PC1[56]    = {57, 49, 41, 33, 25, 17, 9,  1,  58, 50, 42, 34, 26, 18, 10, 2,  59, 51, 43,
                                35, 27, 19, 11, 3,  60, 52, 44, 36, 63, 55, 47, 39, 31, 23, 15, 7,  62, 54,
                                46, 38, 30, 22, 14, 6,  61, 53, 45, 37, 29, 21, 13, 5,  28, 20, 12, 4};
constexpr uint8_t PC2[48]    = {14, 17, 11, 24, 1,  5,  3,  28, 15, 6,  21, 10, 23, 19, 12, 4,
                                26, 8,  16, 7,  27, 20, 13, 2,  41, 52, 31, 37, 47, 55, 30, 40,
                                51, 45, 33, 48, 44, 49, 39, 56, 34, 53, 46, 42, 50, 36, 29, 32};
constexpr uint8_t SHIFTS[16] = {1, 1, 2, 2, 2, 2, 2, 2, 1, 2, 2, 2, 2, 2, 2, 1};
constexpr uint8_t E[48]      = {32, 1,  2,  3,  4,  5,  4,  5,  6,  7,  8,  9,  8,  9,  10, 11,
                                12, 13, 12, 13, 14, 15, 16, 17, 16, 17, 18, 19, 20, 21, 20, 21,
                                22, 23, 24, 25, 24, 25, 26, 27, 28, 29, 28, 29, 30, 31, 32, 1};
constexpr uint8_t P[32]      = {16, 7, 20, 21, 29, 12, 28, 17, 1,  15, 23, 26, 5,  18, 31, 10,
                                2,  8, 24, 14, 32, 27, 3,  9,  19, 13, 30, 6,  22, 11, 4,  25};
constexpr uint8_t S[8][64]   = {
    {14, 4,  13, 1, 2,  15, 11, 8, 3, 10, 6, 12, 5,  9,  0,  7,  0,  15, 7,  4,  14, 2,
     13, 1,  10, 6, 12, 11, 9,  5, 3, 8,  4, 1,  14, 8,  13, 6,  2,  11, 15, 12, 9,  7,
     3,  10, 5,  0, 15, 12, 8,  2, 4, 9,  1, 7,  5,  11, 3,  14, 10, 0,  6,  13},
    {15, 1,  8, 14, 6,  11, 3,  4, 9, 7, 2,  13, 12, 0, 5, 10, 3,  13, 4,  7, 15, 2,  8, 14, 12, 0, 1, 10, 6, 9, 11, 5,
     0,  14, 7, 11, 10, 4,  13, 1, 5, 8, 12, 6,  9,  3, 2, 15, 13, 8,  10, 1, 3,  15, 4, 2,  11, 6, 7, 12, 0, 5, 14, 9},
    {10, 0,  9,  14, 6, 3,  15, 5,  1,  13, 12, 7, 11, 4,  2,  8,  13, 7, 0,  9, 3, 4,
     6,  10, 2,  8,  5, 14, 12, 11, 15, 1,  13, 6, 4,  9,  8,  15, 3,  0, 11, 1, 2, 12,
     5,  10, 14, 7,  1, 10, 13, 0,  6,  9,  8,  7, 4,  15, 14, 3,  11, 5, 2,  12},
    {7, 13, 14, 3, 0, 6,  9, 10, 1,  2, 8,  5, 11, 12, 4,  15, 13, 8,  11, 5, 6, 15,
     0, 3,  4,  7, 2, 12, 1, 10, 14, 9, 10, 6, 9,  0,  12, 11, 7,  13, 15, 1, 3, 14,
     5, 2,  8,  4, 3, 15, 0, 6,  10, 1, 13, 8, 9,  4,  5,  11, 12, 7,  2,  14},
    {2,  12, 4, 1,  7,  10, 11, 6, 8, 5,  3, 15, 13, 0,  14, 9,  14, 11, 2,  12, 4,  7,
     13, 1,  5, 0,  15, 10, 3,  9, 8, 6,  4, 2,  1,  11, 10, 13, 7,  8,  15, 9,  12, 5,
     6,  3,  0, 14, 11, 8,  12, 7, 1, 14, 2, 13, 6,  15, 0,  9,  10, 4,  5,  3},
    {12, 1,  10, 15, 9,  2,  6, 8,  0, 13, 3,  4,  14, 7,  5, 11, 10, 15, 4, 2, 7, 12,
     9,  5,  6,  1,  13, 14, 0, 11, 3, 8,  9,  14, 15, 5,  2, 8,  12, 3,  7, 0, 4, 10,
     1,  13, 11, 6,  4,  3,  2, 12, 9, 5,  15, 10, 11, 14, 1, 7,  6,  0,  8, 13},
    {4, 11, 2,  14, 15, 0, 8, 13, 3,  12, 9, 7, 5, 10, 6, 1, 13, 0,  11, 7, 4, 9, 1,  10, 14, 3, 5, 12, 2,  15, 8, 6,
     1, 4,  11, 13, 12, 3, 7, 14, 10, 15, 6, 8, 0, 5,  9, 2, 6,  11, 13, 8, 1, 4, 10, 7,  9,  5, 0, 15, 14, 2,  3, 12},
    {13, 2,  8, 4, 6, 15, 11, 1, 10, 9, 3,  14, 5,  0, 12, 7, 1, 15, 13, 8, 10, 3,  7, 4,  12, 5,  6, 11, 0, 14, 9, 2,
     7,  11, 4, 1, 9, 12, 14, 2, 0,  6, 10, 13, 15, 3, 5,  8, 2, 1,  14, 7, 4,  10, 8, 13, 15, 12, 9, 0,  3, 5,  6, 11},
};

uint64_t permute(uint64_t input, uint32_t width, const uint8_t* table, std::size_t size) {
    uint64_t out = 0;
    for (std::size_t i = 0; i < size; ++i) out |= ((input >> (width - table[i])) & 1) << (size - 1 - i);
    return out;
}

uint32_t feistel(uint64_t input) {
    uint32_t out = 0;
    for (int i = 0; i < 8; ++i) {
        const auto six    = static_cast<unsigned>((input >> (42 - 6 * i)) & 0x3F);
        const auto row    = ((six & 0x20) >> 4) | (six & 1);
        const auto column = (six >> 1) & 0x0F;
        out |= static_cast<uint32_t>(S[i][row * 16 + column]) << (28 - 4 * i);
    }
    return static_cast<uint32_t>(permute(out, 32, P, 32));
}

class Des {
   public:
    explicit Des(const std::array<uint8_t, 8>& key) {
        const uint64_t permuted = permute(be64(key.data()), 64, PC1, 56);
        uint64_t c = permuted >> 28, d = permuted & 0x0FFFFFFF;
        for (int round = 0; round < 16; ++round) {
            const unsigned shift = SHIFTS[round];
            c                    = ((c << shift) | (c >> (28 - shift))) & 0x0FFFFFFF;
            d                    = ((d << shift) | (d >> (28 - shift))) & 0x0FFFFFFF;
            schedule_[round]     = permute((c << 28) | d, 56, PC2, 48);
        }
    }

    void decrypt(uint8_t* block) const { crypt(block, true); }

    void crypt(uint8_t* block, bool decrypt) const {
        const uint64_t permuted = permute(be64(block), 64, IP, 64);
        auto left = static_cast<uint32_t>(permuted >> 32), right = static_cast<uint32_t>(permuted);
        for (int round = 0; round < 16; ++round) {
            const uint64_t mixed = permute(right, 32, E, 48) ^ schedule_[decrypt ? 15 - round : round];
            const uint32_t next  = left ^ feistel(mixed);
            left                 = right;
            right                = next;
        }
        const uint64_t out = permute((static_cast<uint64_t>(right) << 32) | left, 64, IP_INV, 64);
        for (int i = 0; i < 8; ++i) block[i] = static_cast<uint8_t>(out >> (56 - 8 * i));
    }

   private:
    std::array<uint64_t, 16> schedule_{};
};

std::array<uint8_t, 8> keyOf(const std::string& password) {
    std::array<uint8_t, 8> key{};
    const auto n = password.size();
    if (n == 0) return key;
    if (n <= 8) {
        for (std::size_t i = 0; i < 8; ++i) key[i] = static_cast<uint8_t>(password[i % n]);
    } else {
        for (std::size_t i = 0; i < n; ++i)
            key[i % 8] = static_cast<uint8_t>((key[i % 8] << 1) ^ static_cast<uint8_t>(password[i]));
    }
    return key;
}

bool containerHeader(const uint8_t* h) {
    return h[0] == 0x08 && h[1] == 0x00 && h[4] == 0xAD && h[5] == 0x00 && h[6] == 0x03 && h[7] == 0x00;
}

// ---------------------------------------------------------------------------------------------------------------
// An archive's file, deciphered where it is read
// ---------------------------------------------------------------------------------------------------------------

class CipheredFile {
   public:
    CipheredFile(const fs::path& path, const std::string& password) : path_(path.string()), file_(path) {
        if (!password.empty()) des_.emplace(keyOf(password));
    }

    [[nodiscard]] const std::string& path() const noexcept { return path_; }
    [[nodiscard]] uint64_t size() const noexcept { return file_.size(); }
    [[nodiscard]] bool enciphered() const noexcept { return des_.has_value(); }

    // `size` bytes of the file from `offset`, deciphered; valid until the next call.
    std::span<const uint8_t> bytes(uint64_t offset, std::size_t size) {
        if (offset > file_.size() || size > file_.size() - offset)
            throw Damage(path_ + ": damaged archive (" + std::to_string(size) + " bytes wanted at byte " +
                         std::to_string(offset) + ", past its end)");
        if (!des_) return {file_.data() + offset, size};
        // A block of the cipher is deciphered whole: the range is widened to whole blocks of eight bytes.
        const uint64_t start = offset & ~uint64_t{7};
        const uint64_t end   = (offset + size + 7) & ~uint64_t{7};
        if (end > file_.size()) throw Damage(path_ + ": damaged archive (it does not end on a block of its cipher)");
        clear_.assign(file_.data() + start, file_.data() + end);
        for (std::size_t at = 0; at < clear_.size(); at += 8) des_->decrypt(clear_.data() + at);
        return {clear_.data() + (offset - start), size};
    }

   private:
    std::string path_;
    MappedFile file_;
    std::optional<Des> des_;
    std::vector<uint8_t> clear_;
};

// ---------------------------------------------------------------------------------------------------------------
// The .cbv archive: its member table
// ---------------------------------------------------------------------------------------------------------------

constexpr uint64_t TABLE_AT   = 8;
constexpr uint64_t ENTRY_SIZE = 173;
constexpr std::size_t TRIO    = 128;
constexpr std::size_t QUARTET = TRIO + 12 + 9;

struct Member {
    std::string name;
    uint64_t offset = 0, packed = 0, size = 0;
};

class Archive {
   public:
    // `file`, whose first bytes (deciphered) are a .cbv's header.
    explicit Archive(std::shared_ptr<CipheredFile> file) : file_(std::move(file)) {
        if (file_->size() < TABLE_AT + ENTRY_SIZE)
            throw std::runtime_error(path() + ": not a ChessBase archive (shorter than its table)");
        const auto head         = bytes(0, 8);
        const uint32_t declared = le16(head.data() + 2);
        const auto first        = bytes(TABLE_AT, ENTRY_SIZE);
        const uint64_t pool     = le32(first.data() + TRIO);
        if (pool < TABLE_AT + ENTRY_SIZE || (pool - TABLE_AT) % ENTRY_SIZE != 0 || pool > file_->size())
            throw std::runtime_error(path() +
                                     ": damaged archive (its member table does not end where its first member "
                                     "starts)");
        const uint64_t count = (pool - TABLE_AT) / ENTRY_SIZE;
        if (count != declared)
            throw std::runtime_error(path() + ": damaged archive (its header says " + std::to_string(declared) +
                                     " members, its table holds " + std::to_string(count) + ")");
        for (uint64_t i = 0; i < count; ++i) members_.push_back(member(i));
        for (std::size_t i = 1; i < members_.size(); ++i)
            if (members_[i].offset < members_[i - 1].offset + members_[i - 1].packed)
                throw std::runtime_error(path() + ": damaged archive (" + members_[i].name + " starts inside " +
                                         members_[i - 1].name + ")");
    }

    [[nodiscard]] const std::string& path() const noexcept { return file_->path(); }
    [[nodiscard]] const std::vector<Member>& members() const noexcept { return members_; }

    // `size` bytes of the file from `offset`, deciphered; valid until the next call.
    std::span<const uint8_t> bytes(uint64_t offset, std::size_t size) { return file_->bytes(offset, size); }

   private:
    Member member(uint64_t index) {
        const uint64_t at = TABLE_AT + index * ENTRY_SIZE;
        const auto r      = bytes(at, ENTRY_SIZE);
        const auto bad    = [&](const std::string& what) {
            return std::runtime_error(path() + ": damaged archive (member " + std::to_string(index) + ": " + what +
                                      ")");
        };
        const auto nul = std::find(r.begin(), r.begin() + TRIO, uint8_t{0});
        if (nul == r.begin() + TRIO) throw bad("its name has no end");
        if (nul == r.begin()) throw bad("it has no name");
        Member m;
        m.name.assign(r.begin(), nul);
        for (const char c : m.name)
            if (static_cast<unsigned char>(c) < 0x20) throw bad("its name holds a control byte");
        m.offset              = le32(r.data() + TRIO);
        m.packed              = le32(r.data() + TRIO + 4);
        m.size                = le32(r.data() + TRIO + 8);
        const uint64_t offset = le64(r.data() + QUARTET), packed = le64(r.data() + QUARTET + 8),
                       size = le64(r.data() + QUARTET + 16);
        if (offset != m.offset || packed != m.packed || size != m.size) throw bad("its two sizes disagree");
        if (m.packed == 0) throw bad("it stores nothing");
        if (m.offset + m.packed > file_->size()) throw bad("it runs past the archive's end");
        return m;
    }

    std::shared_ptr<CipheredFile> file_;
    std::vector<Member> members_;
};

// ---------------------------------------------------------------------------------------------------------------
// A member's blocks: LZ tokens, Huffman codes
// ---------------------------------------------------------------------------------------------------------------

constexpr std::size_t WINDOW = 4098;  // how far back an LZ token may copy

// LZ: groups of a two-byte control word (little-endian) and up to 16 tokens, its bits from the highest: 0 a literal
// byte; 1 a token whose first byte's high four bits say what it is: 0 a short run (the low bits + 3 copies of the next
// byte), 1 a long run (low + (b1 << 4) + 19 copies of b2), 2 a long copy (b2 + 16 bytes), 3 to 15 a short copy of
// that many bytes, a copy from (b1 << 4) + low + 3 bytes back. The stream ends when its bytes do.
void unpackLz(const std::string& name, std::span<const uint8_t> in, std::vector<uint8_t>& out) {
    const auto copyBack = [&](std::size_t offset, std::size_t length) {
        if (offset > out.size()) throw Damage(name + ": damaged archive (a copy from before the member's start)");
        const std::size_t from  = out.size() - offset;
        const std::size_t avail = out.size() - from;
        if (length <= avail) {
            const std::size_t end = out.size();
            out.resize(end + length);
            std::copy(out.begin() + static_cast<std::ptrdiff_t>(from),
                      out.begin() + static_cast<std::ptrdiff_t>(from + length),
                      out.begin() + static_cast<std::ptrdiff_t>(end));
            return;
        }
        // Longer than its distance: the bytes it copies, copied again.
        const std::size_t first = out.size();
        out.resize(first + avail);
        std::copy(out.begin() + static_cast<std::ptrdiff_t>(from), out.begin() + static_cast<std::ptrdiff_t>(first),
                  out.begin() + static_cast<std::ptrdiff_t>(first));
        std::size_t done = avail;
        while (done < length) {
            const std::size_t chunk = std::min(length - done, avail);
            const std::size_t base  = out.size() - chunk;
            out.resize(out.size() + chunk);
            std::copy(out.begin() + static_cast<std::ptrdiff_t>(base),
                      out.begin() + static_cast<std::ptrdiff_t>(base + chunk),
                      out.end() - static_cast<std::ptrdiff_t>(chunk));
            done += chunk;
        }
    };
    std::size_t at = 0;
    while (at < in.size()) {
        if (at + 2 > in.size()) {
            out.push_back(in[at]);
            break;
        }
        const uint32_t control = le16(in.data() + at);
        at += 2;
        for (int bit = 15; bit >= 0; --bit) {
            if (at >= in.size()) break;
            const uint8_t tag = in[at];
            if (!(control & (1u << bit))) {
                out.push_back(tag);
                ++at;
                continue;
            }
            ++at;
            const unsigned high = tag >> 4, low = tag & 0x0F;
            if (high == 0) {
                if (at >= in.size()) return;
                out.insert(out.end(), low + 3, in[at++]);
            } else if (high == 1) {
                if (at + 2 > in.size()) return;
                out.insert(out.end(), low + (static_cast<std::size_t>(in[at]) << 4) + 0x13, in[at + 1]);
                at += 2;
            } else if (high == 2) {
                if (at + 2 > in.size()) return;
                copyBack((static_cast<std::size_t>(in[at]) << 4) + low + 3, static_cast<std::size_t>(in[at + 1]) + 16);
                at += 2;
            } else {
                if (at >= in.size()) return;
                copyBack((static_cast<std::size_t>(in[at]) << 4) + low + 3, high);
                ++at;
            }
        }
    }
}

// Huffman: the output's length (16 bits, big-endian), then for each byte value in order the length of its code (4
// bits) and the code (that many bits), then the codes, each giving one byte; nothing is byte-aligned. The code is
// complete: its lengths' Kraft sum is exactly 1. Decoded with a 9-bit table and 6-bit tables for longer codes.
class Huffman {
   public:
    void unpack(const std::string& name, std::span<const uint8_t> in, std::vector<uint8_t>& out) {
        data_                  = in;
        at_                    = 0;
        acc_                   = 0;
        held_                  = 0;
        const std::size_t want = read(16);
        if (want == 0) return;
        build(name);
        out.reserve(out.size() + want);
        for (std::size_t produced = 0; produced < want; ++produced) {
            fill(ROOT_BITS);
            const Entry entry = root_[acc_ >> (64 - ROOT_BITS)];
            unsigned length = 0, taken = 0;
            uint8_t symbol = 0;
            if (entry & LONG) {
                acc_ <<= ROOT_BITS;
                held_ = held_ > ROOT_BITS ? held_ - ROOT_BITS : 0;
                fill(SECOND_BITS);
                const Entry leaf = second_[entry & 0x7FFF][acc_ >> (64 - SECOND_BITS)];
                length           = leaf >> 8;
                symbol           = static_cast<uint8_t>(leaf);
                taken            = ROOT_BITS;
            } else {
                length = entry >> 8;
                symbol = static_cast<uint8_t>(entry);
            }
            if (length == 0 || length <= taken)
                throw Damage(name + ": damaged archive (a Huffman code that its table does not hold)");
            const unsigned consume = length - taken;
            acc_ <<= consume;
            held_ = held_ > consume ? held_ - consume : 0;
            out.push_back(symbol);
        }
    }

   private:
    using Entry                         = uint16_t;  // a leaf: its code's length << 8 | the byte; or LONG | a table
    static constexpr unsigned ROOT_BITS = 9, SECOND_BITS = 6;
    static constexpr Entry LONG = 0x8000;

    void fill(unsigned want) {
        while (held_ < want && at_ < data_.size()) {
            acc_ |= static_cast<uint64_t>(data_[at_++]) << (56 - held_);
            held_ += 8;
        }
    }
    uint32_t read(unsigned n) {
        if (n == 0) return 0;
        fill(n);
        const auto value = static_cast<uint32_t>(acc_ >> (64 - n));
        acc_ <<= n;
        held_ = held_ > n ? held_ - n : 0;
        return value;
    }
    void build(const std::string& name) {
        root_.fill(0);
        used_          = 0;
        uint32_t kraft = 0;
        for (unsigned symbol = 0; symbol < 256; ++symbol) {
            const unsigned n = read(4);
            if (n == 0) continue;
            kraft += 1u << (16 - n);
            const uint32_t path = read(n);
            const auto leaf     = static_cast<Entry>(n << 8 | symbol);
            if (n <= ROOT_BITS) {
                const std::size_t base = static_cast<std::size_t>(path) << (ROOT_BITS - n);
                std::fill_n(root_.begin() + static_cast<std::ptrdiff_t>(base), std::size_t{1} << (ROOT_BITS - n), leaf);
            } else {
                const std::size_t index = path >> (n - ROOT_BITS);
                if (!(root_[index] & LONG)) {
                    if (used_ == second_.size())
                        throw Damage(name + ": damaged archive (a Huffman table of more long codes than it can hold)");
                    second_[used_].fill(0);
                    root_[index] = static_cast<Entry>(LONG | used_++);
                }
                auto& table            = second_[root_[index] & 0x7FFF];
                const unsigned rest    = n - ROOT_BITS;
                const std::size_t base = (path & ((1u << rest) - 1)) << (SECOND_BITS - rest);
                std::fill_n(table.begin() + static_cast<std::ptrdiff_t>(base), std::size_t{1} << (SECOND_BITS - rest),
                            leaf);
            }
        }
        if (kraft != 1u << 16) throw Damage(name + ": damaged archive (a Huffman table that is not a complete code)");
    }

    std::span<const uint8_t> data_;
    std::size_t at_ = 0;
    uint64_t acc_   = 0;
    unsigned held_  = 0;
    std::array<Entry, 1 << ROOT_BITS> root_{};
    std::array<std::array<Entry, 1 << SECOND_BITS>, 256> second_{};
    std::size_t used_ = 0;
};

// A member, a block at a time.
class MemberSource final : public Source {
   public:
    MemberSource(std::shared_ptr<Archive> archive, Member member)
        : archive_(std::move(archive)), member_(std::move(member)), name_(archive_->path() + ": " + member_.name) {
        // The blocks' places, from their heads alone.
        for (uint64_t at = 0; at < member_.packed;) {
            if (member_.packed - at < 4) throw Damage(name_ + ": damaged archive (a block's head is cut)");
            const auto head       = archive_->bytes(member_.offset + at, 4);
            const uint32_t length = le16(head.data());
            if (length == 0 || length > member_.packed - at - 4)
                throw Damage(name_ + ": damaged archive (a block runs past its member or holds nothing)");
            blocks_.push_back(Block{at, 0, 0});
            at += 4 + length;
        }
    }

    [[nodiscard]] const std::string& name() const noexcept override { return name_; }
    [[nodiscard]] uint64_t size() const noexcept override { return member_.size; }

    std::span<const uint8_t> read(uint64_t offset, std::size_t size) override {
        if (offset > member_.size || size > member_.size - offset) outside(offset, size);
        if (size == 0) return {};
        std::size_t k         = blockAt(offset);
        const auto* first     = cached(k);
        const uint64_t within = offset - blocks_[k].out;
        if (within + size <= first->data.size()) return {first->data.data() + within, size};
        scratch_.clear();
        scratch_.insert(scratch_.end(), first->data.begin() + static_cast<std::ptrdiff_t>(within), first->data.end());
        while (scratch_.size() < size) {
            if (++k >= known_) decodeNext();
            if (k >= known_) outside(offset, size);
            const auto* next       = cached(k);
            const std::size_t take = std::min<std::size_t>(size - scratch_.size(), next->data.size());
            scratch_.insert(scratch_.end(), next->data.begin(), next->data.begin() + static_cast<std::ptrdiff_t>(take));
        }
        return {scratch_.data(), size};
    }

    // The whole member.
    std::vector<uint8_t> all() {
        std::vector<uint8_t> out;
        out.reserve(std::min<uint64_t>(member_.size, 64u << 20));
        std::vector<uint8_t> tail;
        for (std::size_t k = 0; k < blocks_.size(); ++k) {
            auto data = unpack(k, tail);
            out.insert(out.end(), data.begin(), data.end());
            if (out.size() > member_.size) break;
            tail = tailOf(tail, data);
        }
        if (out.size() != member_.size)
            throw Damage(name_ + ": damaged archive (it unpacks to " + std::to_string(out.size()) + " bytes, not " +
                         std::to_string(member_.size) + ")");
        return out;
    }

   private:
    struct Block {
        uint64_t packed = 0;  // from the member's start
        uint64_t out    = 0;  // where its output starts, once known
        uint64_t size   = 0;  // its output's size, once known
    };
    struct Unpacked {
        std::size_t block = 0;
        std::vector<uint8_t> data, tail;  // its output; the member's last bytes up to its end
    };
    static constexpr std::size_t CACHED = 32, CHECKPOINT = 16;

    static std::vector<uint8_t> tailOf(const std::vector<uint8_t>& before, const std::vector<uint8_t>& data) {
        std::vector<uint8_t> tail;
        if (data.size() >= WINDOW) return {data.end() - WINDOW, data.end()};
        const std::size_t keep = std::min(before.size(), WINDOW - data.size());
        tail.assign(before.end() - static_cast<std::ptrdiff_t>(keep), before.end());
        tail.insert(tail.end(), data.begin(), data.end());
        return tail;
    }

    // Block k's output, the member's bytes before it ending with `tail`.
    std::vector<uint8_t> unpack(std::size_t k, const std::vector<uint8_t>& tail) {
        const uint64_t at  = blocks_[k].packed;
        const uint64_t end = k + 1 < blocks_.size() ? blocks_[k + 1].packed : member_.packed;
        const auto payload = archive_->bytes(member_.offset + at + 4, end - at - 4);
        const uint8_t mode = payload[0];
        const auto body    = std::vector<uint8_t>(payload.begin() + 1, payload.end());
        std::vector<uint8_t> out(tail);  // what an LZ token may copy from
        switch (mode) {
            case 0:
                out.insert(out.end(), body.begin(), body.end());
                break;
            case 1:
                unpackLz(name_, body, out);
                break;
            case 2:
                huffman_.unpack(name_, body, out);
                break;
            case 3: {
                std::vector<uint8_t> tokens;
                huffman_.unpack(name_, body, tokens);
                unpackLz(name_, tokens, out);
                break;
            }
            default:
                throw Damage(name_ + ": damaged archive (a block stored in an unknown way, " + std::to_string(mode) +
                             ")");
        }
        out.erase(out.begin(), out.begin() + static_cast<std::ptrdiff_t>(tail.size()));
        return out;
    }

    void decodeNext() {
        if (known_ >= blocks_.size()) return;
        const std::size_t k = known_;
        std::vector<uint8_t> tail;
        if (k > 0) tail = tailBefore(k);
        Unpacked u;
        u.block         = k;
        u.data          = unpack(k, tail);
        u.tail          = tailOf(tail, u.data);
        blocks_[k].out  = k == 0 ? 0 : blocks_[k - 1].out + blocks_[k - 1].size;
        blocks_[k].size = u.data.size();
        ++known_;
        remember(std::move(u));
    }

    // The member's last bytes before block k (whose earlier blocks are known).
    std::vector<uint8_t> tailBefore(std::size_t k) {
        if (k == 0) return {};
        for (const auto& u : cache_)
            if (u.block == k - 1) return u.tail;
        if (const auto it = checkpoints_.find(k - 1); it != checkpoints_.end()) return it->second;
        return cached(k - 1)->tail;
    }

    const Unpacked* cached(std::size_t k) {
        for (auto it = cache_.begin(); it != cache_.end(); ++it) {
            if (it->block != k) continue;
            cache_.splice(cache_.begin(), cache_, it);
            return &cache_.front();
        }
        Unpacked u;
        u.block         = k;
        const auto tail = tailBefore(k);
        u.data          = unpack(k, tail);
        u.tail          = tailOf(tail, u.data);
        remember(std::move(u));
        return &cache_.front();
    }

    void remember(Unpacked u) {
        if (u.block % CHECKPOINT == CHECKPOINT - 1) checkpoints_[u.block] = u.tail;
        cache_.push_front(std::move(u));
        if (cache_.size() > CACHED) cache_.pop_back();
    }

    std::size_t blockAt(uint64_t offset) {
        while (known_ < blocks_.size() && (known_ == 0 || blocks_[known_ - 1].out + blocks_[known_ - 1].size <= offset))
            decodeNext();
        if (known_ == 0 || blocks_[known_ - 1].out + blocks_[known_ - 1].size <= offset) outside(offset, 1);
        const auto it = std::upper_bound(blocks_.begin(), blocks_.begin() + static_cast<std::ptrdiff_t>(known_), offset,
                                         [](uint64_t at, const Block& b) { return at < b.out; });
        return static_cast<std::size_t>(it - blocks_.begin()) - 1;
    }

    std::shared_ptr<Archive> archive_;
    Member member_;
    std::string name_;
    std::vector<Block> blocks_;
    std::size_t known_ = 0;  // the blocks whose output's place is known: the first ones
    std::list<Unpacked> cache_;
    std::map<std::size_t, std::vector<uint8_t>> checkpoints_;
    std::vector<uint8_t> scratch_;
    Huffman huffman_;
};

class ArchiveMembers final : public Members {
   public:
    ArchiveMembers(std::shared_ptr<CipheredFile> file, uint64_t whole)
        : archive_(std::make_shared<Archive>(std::move(file))), whole_(whole) {
        // The database: the member at the archive's root named <base>.cbh, else <base>.2cbh.
        for (const char* header : {".cbh", ".2cbh"}) {
            for (const auto& m : archive_->members()) {
                const auto name = lower(m.name);
                if (name.find('\\') != std::string::npos || name.find('/') != std::string::npos) continue;
                if (name.size() > std::strlen(header) && name.ends_with(header)) {
                    base_ = m.name.substr(0, m.name.size() - std::strlen(header));
                    return;
                }
            }
        }
        throw std::runtime_error(archive_->path() + ": the archive holds no ChessBase database (no .cbh or .2cbh)");
    }

    [[nodiscard]] std::string label() const override { return archive_->path() + ": " + base_; }

    [[nodiscard]] bool has(std::string_view extension) const override { return find(extension) != nullptr; }

    std::unique_ptr<Source> open(std::string_view extension, bool random) override {
        const Member* m = find(extension);
        if (!m) return nullptr;
        auto stream = std::make_unique<MemberSource>(archive_, *m);
        // A file of names is read anywhere, often: unpacked whole. So is a small file.
        if ((random && m->size <= (uint64_t{1} << 30)) || m->size <= whole_)
            return memorySource(archive_->path() + ": " + m->name, stream->all());
        return stream;
    }

   private:
    [[nodiscard]] const Member* find(std::string_view extension) const {
        const auto want = lower(base_ + std::string(extension));
        for (const auto& m : archive_->members())
            if (lower(m.name) == want) return &m;
        return nullptr;
    }

    std::shared_ptr<Archive> archive_;
    uint64_t whole_ = 0;
    std::string base_;
};

// ---------------------------------------------------------------------------------------------------------------
// The zip archive of a 2CBH database (.2cbv, .2cbz)
// ---------------------------------------------------------------------------------------------------------------

// The archive's directory read through its file, deciphered: zip's own (sources.hpp).
ArchiveBytes zipBytes(const std::shared_ptr<CipheredFile>& file) {
    return [file](uint64_t offset, std::size_t size) {
        if (offset >= file->size()) return std::vector<unsigned char>{};
        const auto n     = static_cast<std::size_t>(std::min<uint64_t>(size, file->size() - offset));
        const auto bytes = file->bytes(offset, n);
        return std::vector<unsigned char>(bytes.begin(), bytes.end());
    };
}

// A member stored as it is: read where it lies, into a buffer of its own (the file's is shared by the members).
class StoredMember final : public Source {
   public:
    StoredMember(std::shared_ptr<CipheredFile> file, std::string name, uint64_t data, uint64_t size)
        : file_(std::move(file)), name_(std::move(name)), data_(data), size_(size) {}
    [[nodiscard]] const std::string& name() const noexcept override { return name_; }
    [[nodiscard]] uint64_t size() const noexcept override { return size_; }
    std::span<const uint8_t> read(uint64_t offset, std::size_t size) override {
        if (offset > size_ || size > size_ - offset) outside(offset, size);
        const auto bytes = file_->bytes(data_ + offset, size);
        scratch_.assign(bytes.begin(), bytes.end());
        return scratch_;
    }

   private:
    std::shared_ptr<CipheredFile> file_;
    std::string name_;
    uint64_t data_ = 0, size_ = 0;
    std::vector<uint8_t> scratch_;
};

// A deflated member, inflated as it is read: on from where the reads have got to, from its start again for a read
// before what is kept. A database's files are mostly read in order (the moves and annotations of one game after
// another): a few megabytes are kept behind the last read, for one a little back.
class InflatedMember final : public Source {
   public:
    InflatedMember(std::shared_ptr<CipheredFile> file, std::string name, uint64_t data, uint64_t compressed,
                   uint64_t size)
        : file_(std::move(file)), name_(std::move(name)), data_(data), compressed_(compressed), size_(size) {
        if (inflateInit2(&z_, -15) != Z_OK) throw std::runtime_error("out of memory");
    }
    ~InflatedMember() override { inflateEnd(&z_); }
    InflatedMember(const InflatedMember&)            = delete;
    InflatedMember& operator=(const InflatedMember&) = delete;

    [[nodiscard]] const std::string& name() const noexcept override { return name_; }
    [[nodiscard]] uint64_t size() const noexcept override { return size_; }

    std::span<const uint8_t> read(uint64_t offset, std::size_t size) override {
        if (offset > size_ || size > size_ - offset) outside(offset, size);
        if (size == 0) return {};
        if (offset < kept_at_) restart();
        while (kept_at_ + kept_.size() < offset + size) {
            // What lies well before the read is let go, once there is much of it.
            if (offset - kept_at_ > BEHIND && kept_.size() > 4 * BEHIND) {
                const auto drop =
                    static_cast<std::size_t>(std::min<uint64_t>(kept_.size(), offset - kept_at_ - BEHIND));
                kept_.erase(kept_.begin(), kept_.begin() + static_cast<std::ptrdiff_t>(drop));
                kept_at_ += drop;
            }
            more();
        }
        return {kept_.data() + (offset - kept_at_), size};
    }

    // The whole member, its CRC-32 checked against `crc`.
    std::vector<uint8_t> all(uint32_t crc) {
        restart();
        kept_.reserve(static_cast<std::size_t>(std::min<uint64_t>(size_, 64u << 20)));
        while (kept_.size() < size_) more();
        uLong sum = crc32(0L, Z_NULL, 0);
        for (std::size_t at = 0; at < kept_.size(); at += 1u << 30)
            sum = crc32(sum, kept_.data() + at, static_cast<uInt>(std::min<std::size_t>(kept_.size() - at, 1u << 30)));
        if (static_cast<uint32_t>(sum) != crc)
            throw Damage(name_ + ": damaged archive (its bytes are not those its CRC-32 says)");
        return std::move(kept_);
    }

   private:
    static constexpr std::size_t BEHIND = 1u << 20;

    void restart() {
        inflateReset(&z_);
        z_.avail_in = 0;
        consumed_   = 0;
        kept_.clear();
        kept_at_ = 0;
        ended_   = false;
    }

    // Inflates the member's next bytes into what is kept.
    void more() {
        std::array<uint8_t, 1u << 16> out{};
        for (;;) {
            if (ended_) throw Damage(name_ + ": damaged archive (it unpacks to fewer bytes than its size)");
            if (z_.avail_in == 0) {
                if (consumed_ >= compressed_) throw Damage(name_ + ": damaged archive (its data is cut short)");
                const auto n     = static_cast<std::size_t>(std::min<uint64_t>(input_.size(), compressed_ - consumed_));
                const auto bytes = file_->bytes(data_ + consumed_, n);
                std::copy(bytes.begin(), bytes.end(), input_.begin());
                consumed_ += n;
                z_.next_in  = input_.data();
                z_.avail_in = static_cast<uInt>(n);
            }
            z_.next_out    = out.data();
            z_.avail_out   = static_cast<uInt>(out.size());
            const auto had = z_.avail_in;
            const int ret  = inflate(&z_, Z_NO_FLUSH);
            if (ret == Z_STREAM_END)
                ended_ = true;
            else if (ret != Z_OK && !(ret == Z_BUF_ERROR && z_.avail_in == 0 && had == 0))
                throw Damage(name_ + ": damaged archive (" + (z_.msg ? z_.msg : "its deflated data is not") + ")");
            const std::size_t produced = out.size() - z_.avail_out;
            if (kept_at_ + kept_.size() + produced > size_)
                throw Damage(name_ + ": damaged archive (it unpacks to more bytes than its size)");
            if (produced > 0) {
                kept_.insert(kept_.end(), out.begin(), out.begin() + static_cast<std::ptrdiff_t>(produced));
                return;
            }
        }
    }

    std::shared_ptr<CipheredFile> file_;
    std::string name_;
    uint64_t data_ = 0, compressed_ = 0, size_ = 0;
    z_stream z_{};
    std::array<uint8_t, 1u << 16> input_{};
    uint64_t consumed_ = 0;  // of the compressed data
    std::vector<uint8_t> kept_;
    uint64_t kept_at_ = 0;  // where what is kept starts in the member
    bool ended_       = false;
};

class ZipMembers final : public Members {
   public:
    ZipMembers(std::shared_ptr<CipheredFile> file, uint64_t whole) : file_(std::move(file)), whole_(whole) {
        try {
            entries_ = zipEntries(file_->size(), zipBytes(file_), file_->path());
        } catch (const Damage& e) {
            throw std::runtime_error(e.what());
        }
        // The database: the file at the archive's root named <base>.2cbh, else <base>.cbh.
        for (const char* header : {".2cbh", ".cbh"}) {
            for (const auto& e : entries_) {
                const auto name = lower(e.name);
                if (name.find('\\') != std::string::npos || name.find('/') != std::string::npos) continue;
                if (name.size() > std::strlen(header) && name.ends_with(header)) {
                    base_ = e.name.substr(0, e.name.size() - std::strlen(header));
                    return;
                }
            }
        }
        throw std::runtime_error(file_->path() + ": the archive holds no ChessBase database (no .2cbh or .cbh)");
    }

    [[nodiscard]] std::string label() const override { return file_->path() + ": " + base_; }

    [[nodiscard]] bool has(std::string_view extension) const override { return find(extension) != nullptr; }

    std::unique_ptr<Source> open(std::string_view extension, bool random) override {
        const ZipEntry* e = find(extension);
        if (!e) return nullptr;
        const auto name = file_->path() + ": " + e->name;
        if (e->method != 0 && e->method != 8)
            throw Damage(name + ": stored in a way this program does not read (method " + std::to_string(e->method) +
                         ")");
        const auto data = zipData(*e, zipBytes(file_), file_->path());
        if (e->method == 0) {
            if (e->compressed != e->size)
                throw Damage(name + ": damaged archive (a file stored as it is, of two sizes)");
            if (data > file_->size() || e->size > file_->size() - data)
                throw Damage(name + ": damaged archive (it runs past the archive's end)");
            return std::make_unique<StoredMember>(file_, name, data, e->size);
        }
        if (data > file_->size() || e->compressed > file_->size() - data)
            throw Damage(name + ": damaged archive (it runs past the archive's end)");
        auto stream = std::make_unique<InflatedMember>(file_, name, data, e->compressed, e->size);
        // A file of names is read anywhere, often: unpacked whole. So is a small file.
        if ((random && e->size <= (uint64_t{1} << 30)) || e->size <= whole_)
            return memorySource(name, stream->all(e->crc));
        return stream;
    }

   private:
    [[nodiscard]] const ZipEntry* find(std::string_view extension) const {
        const auto want = lower(base_ + std::string(extension));
        for (const auto& e : entries_)
            if (lower(e.name) == want) return &e;
        return nullptr;
    }

    std::shared_ptr<CipheredFile> file_;
    uint64_t whole_ = 0;
    std::vector<ZipEntry> entries_;
    std::string base_;
};

bool zipHeader(const uint8_t* h) { return h[0] == 'P' && h[1] == 'K' && h[2] == 3 && h[3] == 4; }

}  // namespace

void desBlocks(const std::string& password, std::span<uint8_t> data, bool decrypt) {
    const Des des(keyOf(password));
    for (std::size_t at = 0; at + 8 <= data.size(); at += 8) des.crypt(data.data() + at, decrypt);
}

std::unique_ptr<Members> archiveMembers(const fs::path& file, const std::string& password, uint64_t whole) {
    try {
        auto ciphered    = std::make_shared<CipheredFile>(file, password);
        const auto& path = ciphered->path();
        if (ciphered->size() < 8)
            throw std::runtime_error(path + ": not a ChessBase archive (shorter than its header)");
        // What it is shows in its first block, once deciphered: a .cbv's header, or a zip's first file.
        std::array<uint8_t, 8> head{};
        const auto first = ciphered->bytes(0, 8);
        std::copy(first.begin(), first.end(), head.begin());
        if (containerHeader(head.data())) return std::make_unique<ArchiveMembers>(std::move(ciphered), whole);
        if (zipHeader(head.data())) return std::make_unique<ZipMembers>(std::move(ciphered), whole);
        if (ciphered->enciphered()) throw std::runtime_error(path + ": wrong password");
        throw std::runtime_error(path +
                                 ": not a ChessBase archive (its header is not one); a .cbz or a .2cbz needs its "
                                 "password");
    } catch (const Damage& e) {
        throw std::runtime_error(e.what());
    }
}

bool passwordFits(const fs::path& file, const std::string& password) {
    try {
        CipheredFile ciphered(file, password);
        if (ciphered.size() < 8) return false;
        const auto head = ciphered.bytes(0, 8);
        return containerHeader(head.data()) || zipHeader(head.data());
    } catch (const std::exception&) {
        return false;
    }
}

}  // namespace fcg::study::chessbase
