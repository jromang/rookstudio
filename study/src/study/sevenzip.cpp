#include "sevenzip.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <vector>

#include <7z.h>
#include <7zCrc.h>
#include <Lzma2Dec.h>
#include <LzmaDec.h>

#include <tour/files.hpp>

namespace fcg::study {

namespace {

void* allocate(ISzAllocPtr, size_t size) { return size ? std::malloc(size) : nullptr; }
void release(ISzAllocPtr, void* address) { std::free(address); }
const ISzAlloc ALLOC{allocate, release};

bool seekTo(std::FILE* file, int64_t offset, int whence) {
#ifdef _WIN32
    return _fseeki64(file, offset, whence) == 0;
#else
    return fseeko(file, static_cast<off_t>(offset), whence) == 0;
#endif
}

int64_t tellOf(std::FILE* file) {
#ifdef _WIN32
    return _ftelli64(file);
#else
    return static_cast<int64_t>(ftello(file));
#endif
}

// The archive's file as the SDK reads it.
struct FileStream {
    ISeekInStream vt;
    std::FILE* file = nullptr;
};

SRes fileRead(ISeekInStreamPtr p, void* buf, size_t* size) {
    const auto* s = reinterpret_cast<const FileStream*>(p);
    if (*size == 0) return SZ_OK;
    *size = std::fread(buf, 1, *size, s->file);
    return std::ferror(s->file) ? SZ_ERROR_READ : SZ_OK;
}

SRes fileSeek(ISeekInStreamPtr p, Int64* pos, ESzSeek origin) {
    const auto* s    = reinterpret_cast<const FileStream*>(p);
    const int whence = origin == SZ_SEEK_SET ? SEEK_SET : origin == SZ_SEEK_CUR ? SEEK_CUR : SEEK_END;
    if (!seekTo(s->file, *pos, whence)) return SZ_ERROR_READ;
    *pos = tellOf(s->file);
    return SZ_OK;
}

class SevenZipSource final : public ByteSource {
   public:
    explicit SevenZipSource(const std::filesystem::path& file) : file_(tour::openFile(file, "rb")) {
        static const bool crc_table = (CrcGenerateTable(), true);
        (void)crc_table;
        name_ = file.filename().string();
        if (!file_) throw std::runtime_error("cannot open " + file.string());
        stream_.vt.Read = fileRead;
        stream_.vt.Seek = fileSeek;
        stream_.file    = file_.get();
        look_buf_.resize(std::size_t{1} << 18);
        LookToRead2_CreateVTable(&look_, False);
        look_.buf        = look_buf_.data();
        look_.bufSize    = look_buf_.size();
        look_.realStream = &stream_.vt;
        LookToRead2_INIT(&look_);
        SzArEx_Init(&db_);
        if (SzArEx_Open(&db_, &look_.vt, &ALLOC, &ALLOC) != SZ_OK)
            throw std::runtime_error(name_ + " is not a 7z archive this program can read");
        opened_  = true;
        bool any = false;
        pgn_.resize(db_.NumFiles);
        for (UInt32 i = 0; i < db_.NumFiles; ++i) {
            if (SzArEx_IsDir(&db_, i)) continue;
            std::vector<UInt16> name(SzArEx_GetFileNameUtf16(&db_, i, nullptr));
            SzArEx_GetFileNameUtf16(&db_, i, name.data());
            // ".pgn" at its end (before the terminating zero), whatever the case.
            const std::size_t n = name.empty() ? 0 : name.size() - 1;
            bool pgn            = n >= 4;
            const char suffix[] = ".pgn";
            for (std::size_t k = 0; pgn && k < 4; ++k) {
                UInt16 c = name[n - 4 + k];
                if (c >= 'A' && c <= 'Z') c = static_cast<UInt16>(c - 'A' + 'a');
                pgn = c == static_cast<UInt16>(suffix[k]);
            }
            pgn_[i] = pgn;
            any |= pgn;
        }
        if (!any) throw std::runtime_error(name_ + " holds no PGN file");
        in_.resize(std::size_t{1} << 16);
        decoded_.resize(std::size_t{1} << 20);
    }
    ~SevenZipSource() override {
        closeFolder();
        if (opened_) SzArEx_Free(&db_, &ALLOC);
    }

    std::size_t read(char* out, std::size_t size) override {
        std::size_t n = 0;
        while (n < size) {
            if (out_pos_ < out_.size()) {
                const auto take = std::min(size - n, out_.size() - out_pos_);
                std::copy_n(out_.data() + out_pos_, take, out + n);
                out_pos_ += take;
                n += take;
                continue;
            }
            if (!decodeMore()) break;
        }
        return n;
    }
    [[nodiscard]] uint64_t consumed() const override { return consumed_; }

   private:
    enum class Method { Copy, Lzma, Lzma2 };

    void openFolder(UInt32 f) {
        const auto* coders = db_.db.CodersData + db_.db.FoCodersOffsets[f];
        CSzData sd{coders, db_.db.FoCodersOffsets[f + 1] - db_.db.FoCodersOffsets[f]};
        CSzFolder folder;
        if (SzGetNextFolderItem(&folder, &sd) != SZ_OK || folder.NumCoders != 1 || folder.NumPackStreams != 1)
            throw std::runtime_error(name_ + " is compressed in a way this program does not read (filters)");
        const auto& coder = folder.Coders[0];
        const Byte* props = coders + coder.PropsOffset;
        if (coder.MethodID == 0) {
            method_ = Method::Copy;
        } else if (coder.MethodID == 0x30101) {
            method_ = Method::Lzma;
            LzmaDec_CONSTRUCT(&lzma_);
            if (LzmaDec_Allocate(&lzma_, props, coder.PropsSize, &ALLOC) != SZ_OK)
                throw std::runtime_error(name_ + " is damaged");
            LzmaDec_Init(&lzma_);
        } else if (coder.MethodID == 0x21) {
            method_ = Method::Lzma2;
            Lzma2Dec_CONSTRUCT(&lzma2_);
            if (coder.PropsSize != 1 || Lzma2Dec_Allocate(&lzma2_, props[0], &ALLOC) != SZ_OK)
                throw std::runtime_error(name_ + " is damaged");
            Lzma2Dec_Init(&lzma2_);
        } else {
            throw std::runtime_error(name_ +
                                     " is compressed in a way this program does not read (only LZMA and LZMA2)");
        }
        folder_open_     = true;
        const UInt32 ps  = db_.db.FoStartPackStreamIndex[f];
        const auto start = db_.dataPos + db_.db.PackPositions[ps];
        pack_left_       = db_.db.PackPositions[ps + 1] - db_.db.PackPositions[ps];
        unpack_left_     = SzAr_GetFolderUnpackSize(&db_.db, f);
        if (!seekTo(file_.get(), static_cast<int64_t>(start), SEEK_SET))
            throw std::runtime_error(name_ + " is cut short");
        in_pos_ = in_len_ = 0;
        next_file_        = db_.FolderToFile[f];
        file_left_        = 0;
    }
    void closeFolder() {
        if (!folder_open_) return;
        if (method_ == Method::Lzma) LzmaDec_Free(&lzma_, &ALLOC);
        if (method_ == Method::Lzma2) Lzma2Dec_Free(&lzma2_, &ALLOC);
        folder_open_ = false;
    }
    // The decoded bytes of the folder into the files they belong to: the PGN files' kept.
    void route(const Byte* data, std::size_t len) {
        while (len > 0) {
            if (file_left_ == 0) {
                while (next_file_ < db_.NumFiles &&
                       (db_.FileToFolder[next_file_] != folder_ || SzArEx_GetFileSize(&db_, next_file_) == 0))
                    ++next_file_;
                if (next_file_ >= db_.NumFiles) return;  // more bytes than its files: left
                file_left_ = SzArEx_GetFileSize(&db_, next_file_);
                giving_    = pgn_[next_file_];
                ++next_file_;
                if (giving_ && last_ != 0 && last_ != '\n') out_ += '\n';  // a line break between two files
            }
            const auto take = static_cast<std::size_t>(std::min<uint64_t>(len, file_left_));
            if (giving_ && take > 0) {
                out_.append(reinterpret_cast<const char*>(data), take);
                last_ = static_cast<char>(data[take - 1]);
            }
            file_left_ -= take;
            data += take;
            len -= take;
        }
    }
    bool decodeMore() {
        out_.clear();
        out_pos_ = 0;
        while (out_.empty()) {
            if (folder_ >= db_.db.NumFolders) return false;
            if (!folder_open_) openFolder(folder_);
            if (unpack_left_ == 0) {
                closeFolder();
                ++folder_;
                continue;
            }
            if (in_pos_ == in_len_ && pack_left_ > 0) {
                const auto want = static_cast<std::size_t>(std::min<uint64_t>(in_.size(), pack_left_));
                in_len_         = std::fread(in_.data(), 1, want, file_.get());
                in_pos_         = 0;
                if (in_len_ == 0) throw std::runtime_error(name_ + " is cut short");
                pack_left_ -= in_len_;
            }
            SizeT dest = static_cast<SizeT>(std::min<uint64_t>(decoded_.size(), unpack_left_));
            SizeT src  = in_len_ - in_pos_;
            if (method_ == Method::Copy) {
                dest = std::min(dest, src);
                std::copy_n(in_.data() + in_pos_, dest, decoded_.data());
                src = dest;
            } else {
                ELzmaStatus status;
                const SRes res = method_ == Method::Lzma
                                     ? LzmaDec_DecodeToBuf(&lzma_, decoded_.data(), &dest, in_.data() + in_pos_, &src,
                                                           LZMA_FINISH_ANY, &status)
                                     : Lzma2Dec_DecodeToBuf(&lzma2_, decoded_.data(), &dest, in_.data() + in_pos_, &src,
                                                            LZMA_FINISH_ANY, &status);
                if (res != SZ_OK) throw std::runtime_error(name_ + " is damaged");
            }
            in_pos_ += src;
            consumed_ += src;
            if (dest == 0 && src == 0 && in_pos_ == in_len_ && pack_left_ == 0)
                throw std::runtime_error(name_ + " is cut short");  // its data ended before its files
            unpack_left_ -= dest;
            route(decoded_.data(), dest);
        }
        return true;
    }

    tour::File file_;
    FileStream stream_;
    CLookToRead2 look_{};
    std::vector<Byte> look_buf_;
    CSzArEx db_{};
    bool opened_ = false;
    std::vector<bool> pgn_;  // by file: a PGN file, given
    UInt32 folder_    = 0;
    bool folder_open_ = false;
    Method method_    = Method::Copy;
    CLzmaDec lzma_{};
    CLzma2Dec lzma2_{};
    uint64_t pack_left_ = 0, unpack_left_ = 0;
    std::vector<Byte> in_, decoded_;
    std::size_t in_pos_ = 0, in_len_ = 0;
    UInt32 next_file_   = 0;  // the next file of the folder
    uint64_t file_left_ = 0;
    bool giving_        = false;
    char last_          = 0;
    std::string out_;
    std::size_t out_pos_ = 0;
    uint64_t consumed_   = 0;
};

}  // namespace

std::unique_ptr<ByteSource> sevenZipSource(const std::filesystem::path& file) {
    return std::make_unique<SevenZipSource>(file);
}

}  // namespace fcg::study
