// A ChessBase database of the older format (CBH) in its own files, read as importInto() reads it from any of them
// (#119): the input holds the files, each its size in four bytes (little-endian) then its bytes, in this order: .cbh
// (the games' headers), .cbg (their moves), .cba (their annotations), .cbp, .cbt, .cbc and .cbs (the players,
// tournaments, annotators and sources), .cbe (the teams) and .cbj (the headers' wider fields). A file the input ends
// before is left out, a size past the input's end takes the rest. Each game read is written as PGN as a database's
// game is (importedPgn()) and given its key for the duplicates. The seeds are our own test games' CBH database
// (study/tests/data/chessbase/cbtest1.*) so packed: whole, without its teams and wider fields, without its wider
// fields.

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include <study/chessbase.hpp>
#include <study/import.hpp>
#include <study/store.hpp>

#include "fuzz_file.hpp"

namespace {

constexpr std::array<const char*, 9> EXTENSIONS{".cbh", ".cbg", ".cba", ".cbp", ".cbt", ".cbc", ".cbs", ".cbe", ".cbj"};

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, std::size_t size) {
    static const auto dir = fcg::fuzz::ownFolder("chessbase-files");
    std::size_t at        = 0;
    for (const char* extension : EXTENSIONS) {
        const auto file = dir / (std::string("Base") + extension);
        if (size - at < 4) {
            std::error_code ec;
            std::filesystem::remove(file, ec);  // left out: the last input's copy goes
            continue;
        }
        std::size_t bytes = data[at] | std::size_t{data[at + 1]} << 8 | std::size_t{data[at + 2]} << 16 |
                            std::size_t{data[at + 3]} << 24;
        at += 4;
        if (bytes > size - at) bytes = size - at;
        fcg::fuzz::writeFile(file, std::string(reinterpret_cast<const char*>(data + at), bytes));
        at += bytes;
    }
    fcg::study::ImportProgress progress;
    const std::atomic<bool> cancel{false};
    fcg::study::ChessBaseReport report;
    (void)fcg::study::importChessBase(
        dir / "Base.cbh", fcg::study::ImportOptions{1, 4096}, progress, cancel,
        [](std::vector<fcg::study::ImportedGame>&& batch) {
            for (const auto& game : batch) {
                (void)fcg::study::importedPgn(game);
                (void)fcg::study::duplicateKey(game);
            }
        },
        &report);
    return 0;
}
