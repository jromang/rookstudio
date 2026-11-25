// A database's segment whose bytes were changed after it was written, a disk's damage or a file made to break the
// program (#102): the input's changes applied to a sound segment's five files (cols.bin, moves.bin, annex.bin,
// names.bin, sig.bin), made once from games of our own. The segment opens when its header and sizes still fit, else
// it reads as a segment that cannot be read; then every game is read as the pages read it, and checked.

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

#include <study/store.hpp>

#include "fuzz_database.hpp"
#include "fuzz_file.hpp"

namespace {

constexpr std::array<const char*, 5> FILES{"cols.bin", "moves.bin", "annex.bin", "names.bin", "sig.bin"};

struct Template {
    std::filesystem::path dir;  // the database written for each input
    std::string manifest, segment;
    std::array<std::string, 5> files;
};

const Template& made() {
    static const Template t = [] {
        Template t;
        const auto base = fcg::fuzz::ownFolder("segment-template");
        std::filesystem::remove_all(base);
        {
            auto db = fcg::study::Database::create(base);
            db->append(fcg::fuzz::studyGames(), true);
            t.segment = db->snapshot()->segmentInfos().at(0).name;
        }
        t.manifest = fcg::fuzz::readFile(base / "manifest.json");
        for (std::size_t f = 0; f < FILES.size(); ++f) t.files[f] = fcg::fuzz::readFile(base / t.segment / FILES[f]);
        t.dir = fcg::fuzz::ownFolder("segment");
        std::filesystem::create_directories(t.dir / t.segment);
        fcg::fuzz::writeFile(t.dir / "manifest.json", t.manifest);
        return t;
    }();
    return t;
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, std::size_t size) {
    const auto& t = made();
    auto files    = t.files;
    fcg::fuzz::applyChanges(data, size, {&files[0], &files[1], &files[2], &files[3], &files[4]});
    for (std::size_t f = 0; f < FILES.size(); ++f) fcg::fuzz::writeFile(t.dir / t.segment / FILES[f], files[f]);
    const auto db = fcg::study::Database::openReadOnly(t.dir);  // the manifest is sound: never throws
    fcg::fuzz::readEverything(*db);
    return 0;
}
