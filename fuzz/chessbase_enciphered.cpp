// A ChessBase archive enciphered with a password (#119), read as importInto() reads it once the password fits: a .cbz
// (a CBV archive, the older format's) or a .2cbz (the zip of a 2CBH database), every block of eight bytes enciphered
// with DES. The input is the file as it is, enciphered with our test games' password; named .2cbz when its first block
// deciphers to a zip's first bytes, else .cbz, as the reader tells the archive inside. Its members are read unpacked
// whole, as the program reads a small archive's, then a piece at a time, as it reads a large one's. Each game read is
// written as PGN as a database's game is (importedPgn()) and given its key for the duplicates. The seeds are our own
// test games' archives so enciphered (study/tests/data/chessbase/cbtest1.cbz and cbtest2.2cbz).

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <study/chessbase.hpp>
#include <study/chessbase_detail.hpp>
#include <study/import.hpp>
#include <study/store.hpp>

#include "fuzz_file.hpp"

namespace {

// The password of our test games' enciphered archives (study/tests/chessbase_tests.cpp).
const std::string PASSWORD = "rstest20";

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, std::size_t size) {
    std::array<uint8_t, 8> head{};
    std::copy_n(data, std::min(size, head.size()), head.begin());
    fcg::study::chessbase::desBlocks(PASSWORD, head, true);
    const bool zip  = size >= 2 && head[0] == 'P' && head[1] == 'K';
    const auto file = fcg::fuzz::inputFile(data, size, zip ? ".2cbz" : ".cbz");
    (void)fcg::study::archivePasswordFits(file, PASSWORD);
    for (const uint64_t whole : {fcg::study::ChessBaseOptions{}.unpack_whole, uint64_t{0}}) {
        fcg::study::ImportProgress progress;
        const std::atomic<bool> cancel{false};
        fcg::study::ChessBaseReport report;
        (void)fcg::study::importChessBase(
            file, fcg::study::ImportOptions{1, 4096}, progress, cancel,
            [](std::vector<fcg::study::ImportedGame>&& batch) {
                for (const auto& game : batch) {
                    (void)fcg::study::importedPgn(game);
                    (void)fcg::study::duplicateKey(game);
                }
            },
            &report, fcg::study::ChessBaseOptions{PASSWORD, whole});
    }
    return 0;
}
