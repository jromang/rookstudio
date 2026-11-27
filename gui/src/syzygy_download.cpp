#include "syzygy_download.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>

#include "https.hpp"

namespace fcg {

namespace fs = std::filesystem;

namespace {

// The files of Lichess's mirror, their sizes as there in November 2026: 983,957,920 bytes in all.
const std::vector<SyzygyFile> FILES = {
    {"KBvK.rtbw", 80ull},         {"KBvK.rtbz", 80ull},         {"KNvK.rtbw", 80ull},
    {"KNvK.rtbz", 80ull},         {"KPvK.rtbw", 7824ull},       {"KPvK.rtbz", 4176ull},
    {"KQvK.rtbw", 272ull},        {"KQvK.rtbz", 5392ull},       {"KRvK.rtbw", 208ull},
    {"KRvK.rtbz", 7632ull},       {"KBBvK.rtbw", 58000ull},     {"KBBvK.rtbz", 136272ull},
    {"KBNvK.rtbw", 7632ull},      {"KBNvK.rtbz", 461840ull},    {"KBPvK.rtbw", 81424ull},
    {"KBPvK.rtbz", 75792ull},     {"KBvKB.rtbw", 1232ull},      {"KBvKB.rtbz", 80ull},
    {"KBvKN.rtbw", 2256ull},      {"KBvKN.rtbz", 80ull},        {"KBvKP.rtbw", 107472ull},
    {"KBvKP.rtbz", 5968ull},      {"KNNvK.rtbw", 1360ull},      {"KNNvK.rtbz", 80ull},
    {"KNPvK.rtbw", 93200ull},     {"KNPvK.rtbz", 111056ull},    {"KNvKN.rtbw", 1168ull},
    {"KNvKN.rtbz", 80ull},        {"KNvKP.rtbw", 148048ull},    {"KNvKP.rtbz", 10640ull},
    {"KPPvK.rtbw", 25104ull},     {"KPPvK.rtbz", 8656ull},      {"KPvKP.rtbw", 245328ull},
    {"KPvKP.rtbz", 54480ull},     {"KQBvK.rtbw", 4944ull},      {"KQBvK.rtbz", 139088ull},
    {"KQNvK.rtbw", 3600ull},      {"KQNvK.rtbz", 156432ull},    {"KQPvK.rtbw", 12496ull},
    {"KQPvK.rtbz", 32208ull},     {"KQQvK.rtbw", 7056ull},      {"KQQvK.rtbz", 25936ull},
    {"KQRvK.rtbw", 4560ull},      {"KQRvK.rtbz", 44112ull},     {"KQvKB.rtbw", 6672ull},
    {"KQvKB.rtbz", 195216ull},    {"KQvKN.rtbw", 10064ull},     {"KQvKN.rtbz", 162640ull},
    {"KQvKP.rtbw", 58064ull},     {"KQvKP.rtbz", 205776ull},    {"KQvKQ.rtbw", 16528ull},
    {"KQvKQ.rtbz", 6736ull},      {"KQvKR.rtbw", 20496ull},     {"KQvKR.rtbz", 309008ull},
    {"KRBvK.rtbw", 2832ull},      {"KRBvK.rtbz", 261776ull},    {"KRNvK.rtbw", 2320ull},
    {"KRNvK.rtbz", 295632ull},    {"KRPvK.rtbw", 5136ull},      {"KRPvK.rtbz", 12944ull},
    {"KRRvK.rtbw", 1936ull},      {"KRRvK.rtbz", 53520ull},     {"KRvKB.rtbw", 32912ull},
    {"KRvKB.rtbz", 9936ull},      {"KRvKN.rtbw", 100048ull},    {"KRvKN.rtbz", 93200ull},
    {"KRvKP.rtbw", 179408ull},    {"KRvKP.rtbz", 193424ull},    {"KRvKR.rtbw", 12944ull},
    {"KRvKR.rtbz", 3408ull},      {"KBBBvK.rtbw", 739600ull},   {"KBBBvK.rtbz", 1938576ull},
    {"KBBNvK.rtbw", 106832ull},   {"KBBNvK.rtbz", 9450128ull},  {"KBBPvK.rtbw", 1167824ull},
    {"KBBPvK.rtbz", 1075152ull},  {"KBBvKB.rtbw", 351824ull},   {"KBBvKB.rtbz", 53072ull},
    {"KBBvKN.rtbw", 5064400ull},  {"KBBvKN.rtbz", 8136912ull},  {"KBBvKP.rtbw", 7783952ull},
    {"KBBvKP.rtbz", 6765008ull},  {"KBBvKQ.rtbw", 2217040ull},  {"KBBvKQ.rtbz", 7933776ull},
    {"KBBvKR.rtbw", 574864ull},   {"KBBvKR.rtbz", 89360ull},    {"KBNNvK.rtbw", 92496ull},
    {"KBNNvK.rtbz", 9450128ull},  {"KBNPvK.rtbw", 478800ull},   {"KBNPvK.rtbz", 556880ull},
    {"KBNvKB.rtbw", 716816ull},   {"KBNvKB.rtbz", 114576ull},   {"KBNvKN.rtbw", 3575952ull},
    {"KBNvKN.rtbz", 1826640ull},  {"KBNvKP.rtbw", 10146256ull}, {"KBNvKP.rtbz", 32367120ull},
    {"KBNvKQ.rtbw", 1054544ull},  {"KBNvKQ.rtbz", 12536144ull}, {"KBNvKR.rtbw", 1328144ull},
    {"KBNvKR.rtbz", 118480ull},   {"KBPPvK.rtbw", 274128ull},   {"KBPPvK.rtbz", 116624ull},
    {"KBPvKB.rtbw", 15290512ull}, {"KBPvKB.rtbz", 3497488ull},  {"KBPvKN.rtbw", 20381328ull},
    {"KBPvKN.rtbz", 7242128ull},  {"KBPvKP.rtbw", 13821584ull}, {"KBPvKP.rtbz", 7172304ull},
    {"KBPvKQ.rtbw", 7497872ull},  {"KBPvKQ.rtbz", 23906768ull}, {"KBPvKR.rtbw", 14246864ull},
    {"KBPvKR.rtbz", 1573776ull},  {"KNNNvK.rtbw", 85712ull},    {"KNNNvK.rtbz", 3821712ull},
    {"KNNPvK.rtbw", 882576ull},   {"KNNPvK.rtbz", 1262288ull},  {"KNNvKB.rtbw", 67024ull},
    {"KNNvKB.rtbz", 30544ull},    {"KNNvKN.rtbw", 81360ull},    {"KNNvKN.rtbz", 32912ull},
    {"KNNvKP.rtbw", 13729360ull}, {"KNNvKP.rtbz", 10331216ull}, {"KNNvKQ.rtbw", 3241744ull},
    {"KNNvKQ.rtbz", 4940624ull},  {"KNNvKR.rtbw", 313680ull},   {"KNNvKR.rtbz", 62928ull},
    {"KNPPvK.rtbw", 186512ull},   {"KNPPvK.rtbz", 111312ull},   {"KNPvKB.rtbw", 13148688ull},
    {"KNPvKB.rtbz", 4021776ull},  {"KNPvKN.rtbw", 14200272ull}, {"KNPvKN.rtbz", 4930192ull},
    {"KNPvKP.rtbw", 18616208ull}, {"KNPvKP.rtbz", 8974160ull},  {"KNPvKQ.rtbw", 10056720ull},
    {"KNPvKQ.rtbz", 15554128ull}, {"KNPvKR.rtbw", 19810256ull}, {"KNPvKR.rtbz", 4874960ull},
    {"KPPPvK.rtbw", 56080ull},    {"KPPPvK.rtbz", 22672ull},    {"KPPvKB.rtbw", 7269520ull},
    {"KPPvKB.rtbz", 517520ull},   {"KPPvKN.rtbw", 7707472ull},  {"KPPvKN.rtbz", 699472ull},
    {"KPPvKP.rtbw", 7499088ull},  {"KPPvKP.rtbz", 669776ull},   {"KPPvKQ.rtbw", 3090576ull},
    {"KPPvKQ.rtbz", 3358672ull},  {"KPPvKR.rtbw", 9068688ull},  {"KPPvKR.rtbz", 5114512ull},
    {"KQBBvK.rtbw", 160464ull},   {"KQBBvK.rtbz", 3094096ull},  {"KQBNvK.rtbw", 255888ull},
    {"KQBNvK.rtbz", 2967312ull},  {"KQBPvK.rtbw", 824272ull},   {"KQBPvK.rtbz", 1347600ull},
    {"KQBvKB.rtbw", 232656ull},   {"KQBvKB.rtbz", 6845584ull},  {"KQBvKN.rtbw", 387664ull},
    {"KQBvKN.rtbz", 4369104ull},  {"KQBvKP.rtbw", 2925136ull},  {"KQBvKP.rtbz", 7444880ull},
    {"KQBvKQ.rtbw", 4092496ull},  {"KQBvKQ.rtbz", 1260112ull},  {"KQBvKR.rtbw", 684048ull},
    {"KQBvKR.rtbz", 8740752ull},  {"KQNNvK.rtbw", 113680ull},   {"KQNNvK.rtbz", 3680656ull},
    {"KQNPvK.rtbw", 648784ull},   {"KQNPvK.rtbz", 1295184ull},  {"KQNvKB.rtbw", 218320ull},
    {"KQNvKB.rtbz", 8035600ull},  {"KQNvKN.rtbw", 333456ull},   {"KQNvKN.rtbz", 5411152ull},
    {"KQNvKP.rtbw", 2662096ull},  {"KQNvKP.rtbz", 7902992ull},  {"KQNvKQ.rtbw", 3139152ull},
    {"KQNvKQ.rtbz", 785296ull},   {"KQNvKR.rtbw", 759824ull},   {"KQNvKR.rtbz", 10843024ull},
    {"KQPPvK.rtbw", 255568ull},   {"KQPPvK.rtbz", 132240ull},   {"KQPvKB.rtbw", 785744ull},
    {"KQPvKB.rtbz", 2151824ull},  {"KQPvKN.rtbw", 1694352ull},  {"KQPvKN.rtbz", 2055632ull},
    {"KQPvKP.rtbw", 2464016ull},  {"KQPvKP.rtbz", 997904ull},   {"KQPvKQ.rtbw", 20403600ull},
    {"KQPvKQ.rtbz", 16695824ull}, {"KQPvKR.rtbw", 2030736ull},  {"KQPvKR.rtbz", 6202512ull},
    {"KQQBvK.rtbw", 227600ull},   {"KQQBvK.rtbz", 806160ull},   {"KQQNvK.rtbw", 186064ull},
    {"KQQNvK.rtbz", 920848ull},   {"KQQPvK.rtbw", 725200ull},   {"KQQPvK.rtbz", 583760ull},
    {"KQQQvK.rtbw", 165648ull},   {"KQQQvK.rtbz", 207248ull},   {"KQQRvK.rtbw", 247696ull},
    {"KQQRvK.rtbz", 610512ull},   {"KQQvKB.rtbw", 32976ull},    {"KQQvKB.rtbz", 1086480ull},
    {"KQQvKN.rtbw", 63952ull},    {"KQQvKN.rtbz", 1028688ull},  {"KQQvKP.rtbw", 372240ull},
    {"KQQvKP.rtbz", 1878992ull},  {"KQQvKQ.rtbw", 666832ull},   {"KQQvKQ.rtbz", 2793424ull},
    {"KQQvKR.rtbw", 82256ull},    {"KQQvKR.rtbz", 1261584ull},  {"KQRBvK.rtbw", 280720ull},
    {"KQRBvK.rtbz", 2226576ull},  {"KQRNvK.rtbw", 211920ull},   {"KQRNvK.rtbz", 2435344ull},
    {"KQRPvK.rtbw", 907600ull},   {"KQRPvK.rtbz", 1170832ull},  {"KQRRvK.rtbw", 200528ull},
    {"KQRRvK.rtbz", 880848ull},   {"KQRvKB.rtbw", 159568ull},   {"KQRvKB.rtbz", 2664848ull},
    {"KQRvKN.rtbw", 250832ull},   {"KQRvKN.rtbz", 1878608ull},  {"KQRvKP.rtbw", 984976ull},
    {"KQRvKP.rtbz", 4687056ull},  {"KQRvKQ.rtbw", 3095888ull},  {"KQRvKQ.rtbz", 9245968ull},
    {"KQRvKR.rtbw", 279824ull},   {"KQRvKR.rtbz", 3421968ull},  {"KRBBvK.rtbw", 134416ull},
    {"KRBBvK.rtbz", 6354384ull},  {"KRBNvK.rtbw", 207696ull},   {"KRBNvK.rtbz", 9500944ull},
    {"KRBPvK.rtbw", 531600ull},   {"KRBPvK.rtbz", 951632ull},   {"KRBvKB.rtbw", 916624ull},
    {"KRBvKB.rtbz", 20128272ull}, {"KRBvKN.rtbw", 998224ull},   {"KRBvKN.rtbz", 17507152ull},
    {"KRBvKP.rtbw", 5300624ull},  {"KRBvKP.rtbz", 9324304ull},  {"KRBvKQ.rtbw", 9588688ull},
    {"KRBvKQ.rtbz", 5323152ull},  {"KRBvKR.rtbw", 2035280ull},  {"KRBvKR.rtbz", 575440ull},
    {"KRNNvK.rtbw", 98832ull},    {"KRNNvK.rtbz", 6841744ull},  {"KRNPvK.rtbw", 506960ull},
    {"KRNPvK.rtbz", 810960ull},   {"KRNvKB.rtbw", 1607824ull},  {"KRNvKB.rtbz", 21521616ull},
    {"KRNvKN.rtbw", 691728ull},   {"KRNvKN.rtbz", 21964816ull}, {"KRNvKP.rtbw", 6367376ull},
    {"KRNvKP.rtbz", 9350416ull},  {"KRNvKQ.rtbw", 8863888ull},  {"KRNvKQ.rtbz", 6803856ull},
    {"KRNvKR.rtbw", 1491856ull},  {"KRNvKR.rtbz", 287632ull},   {"KRPPvK.rtbw", 165072ull},
    {"KRPPvK.rtbz", 171408ull},   {"KRPvKB.rtbw", 9939984ull},  {"KRPvKB.rtbz", 15630032ull},
    {"KRPvKN.rtbw", 9449680ull},  {"KRPvKN.rtbz", 12376144ull}, {"KRPvKP.rtbw", 4446224ull},
    {"KRPvKP.rtbz", 1408272ull},  {"KRPvKQ.rtbw", 17606928ull}, {"KRPvKQ.rtbz", 31920016ull},
    {"KRPvKR.rtbw", 16378576ull}, {"KRPvKR.rtbz", 13725456ull}, {"KRRBvK.rtbw", 159504ull},
    {"KRRBvK.rtbz", 2703248ull},  {"KRRNvK.rtbw", 100624ull},   {"KRRNvK.rtbz", 2725776ull},
    {"KRRPvK.rtbw", 241872ull},   {"KRRPvK.rtbz", 630032ull},   {"KRRRvK.rtbw", 54800ull},
    {"KRRRvK.rtbz", 561488ull},   {"KRRvKB.rtbw", 351376ull},   {"KRRvKB.rtbz", 4341328ull},
    {"KRRvKN.rtbw", 286288ull},   {"KRRvKN.rtbz", 3313680ull},  {"KRRvKP.rtbw", 1680272ull},
    {"KRRvKP.rtbz", 1885520ull},  {"KRRvKQ.rtbw", 4068432ull},  {"KRRvKQ.rtbz", 1127952ull},
    {"KRRvKR.rtbw", 334224ull},   {"KRRvKR.rtbz", 7700816ull},
};

}  // namespace

const std::vector<SyzygyFile>& syzygy345Files() { return FILES; }

uint64_t syzygy345Bytes() {
    uint64_t n = 0;
    for (const auto& f : FILES) n += f.size;
    return n;
}

int syzygyFilePieces(const char* name) {
    const char* dot   = std::strchr(name, '.');
    const auto length = dot ? static_cast<int>(dot - name) : static_cast<int>(std::strlen(name));
    return length - 1;  // the letters but the 'v'
}

bool syzygyFileValid(const fs::path& file) {
    std::error_code ec;
    const auto size = fs::file_size(file, ec);
    if (ec || size < 80 || size % 64 != 16) return false;
    std::array<unsigned char, 4> mark{};
    std::ifstream in(file, std::ios::binary);
    if (!in.read(reinterpret_cast<char*>(mark.data()), mark.size())) return false;
    static constexpr std::array<unsigned char, 4> WDL{0x71, 0xe8, 0x23, 0x5d}, DTZ{0xd7, 0x66, 0x0c, 0xa5};
    const auto ext = file.extension().string();
    return ext == ".rtbw" ? mark == WDL : ext == ".rtbz" ? mark == DTZ : false;
}

const char* syzygy345Url() { return "https://tablebase.lichess.ovh/tables/standard/3-4-5/"; }

namespace {

// The files of `files` missing in `folder` downloaded from `base_url`, one after the other: done when every file is
// there, else cancelled or failed, with why it stopped. On the download's thread, holding all it uses.
TaskEnd downloadSyzygy(const fs::path& folder, const std::string& base_url, const std::vector<SyzygyFile>& files,
                       std::atomic<uint64_t>& generation, TaskState<SyzygyDownload::Progress>& job) {
    auto& progress = static_cast<SyzygyDownload::Progress&>(job);
    TaskEnd end;
    uint64_t done     = 0;
    std::size_t count = 0;
    // What is there already counts, checked.
    std::vector<const SyzygyFile*> todo;
    for (const auto& f : files) {
        std::error_code ec;
        const auto path = folder / f.name;
        if (fs::is_regular_file(path, ec) && syzygyFileValid(path)) {
            done += fs::file_size(path, ec);
            ++count;
        } else {
            todo.push_back(&f);
        }
    }
    // The most pieces whose files are all there, those of `todo` from `next` on still missing.
    const auto readyBefore = [&](std::size_t next) {
        const int first = next < todo.size() ? syzygyFilePieces(todo[next]->name) : 6;
        return first <= 3 ? 0 : first - 1;
    };
    progress.done  = done;
    progress.files = count;
    progress.ready = readyBefore(0);
    for (std::size_t i = 0; i < todo.size(); ++i) {
        if (job.cancel) {
            end = TaskEnd::cancel();
            break;
        }
        const auto& f          = *todo[i];
        progress.current.bytes = 0;
        progress.current.size  = 0;
        // After the host's pause, taking no turn: a file may take minutes, which the online tables' questions to the
        // same host do not wait for.
        const auto got = politeDownload(base_url + f.name, folder / f.name, &progress.current, &job.cancel);
        if (!got.ok) {  // its cancel stopped it, or it failed: said without the file's path, kept beside (#141)
            end = job.cancel ? TaskEnd::cancel() : TaskEnd::of(got.failure);
            break;
        }
        if (!syzygyFileValid(folder / f.name)) {
            std::error_code ec;
            fs::remove(folder / f.name, ec);
            NetFailure other;
            other.kind   = NetFailure::Kind::Unexpected;
            other.host   = urlHost(base_url);
            other.path   = folder / f.name;
            other.detail = std::string(f.name) + ": not a Syzygy table";
            end          = TaskEnd::of(std::move(other));
            break;
        }
        std::error_code ec;
        done += fs::file_size(folder / f.name, ec);
        progress.current.bytes = 0;
        progress.done          = done;
        progress.files         = ++count;
        const int ready        = readyBefore(i + 1);
        if (ready != progress.ready) {
            progress.ready = ready;
            ++generation;
        }
    }
    return end;
}

}  // namespace

SyzygyDownload::SyzygyDownload(JobGroup& jobs, fs::path folder, std::string base_url, std::vector<SyzygyFile> files)
    : folder_(std::move(folder)), base_url_(std::move(base_url)), files_(std::move(files)), job_(jobs) {
    if (!base_url_.empty() && base_url_.back() != '/') base_url_ += '/';
}

SyzygyDownload::State SyzygyDownload::state() const {
    State s;
    int missing_below = 6;  // the fewest pieces with a file missing
    for (const auto& f : files_) {
        const auto path = folder_ / f.name;
        std::error_code ec;
        if (fs::is_regular_file(path, ec) && syzygyFileValid(path)) {
            ++s.files;
            s.bytes += fs::file_size(path, ec);
        } else {
            missing_below = std::min(missing_below, syzygyFilePieces(f.name));
        }
    }
    s.ready    = missing_below <= 3 ? 0 : missing_below - 1;
    s.complete = s.files == files_.size();
    return s;
}

void SyzygyDownload::start() {
    if (busy()) return;
    // The task holds what it reads and what it tells: it may outlive this object, parked.
    job_.start([folder = folder_, base_url = base_url_, files = files_, generation = generation_](
                   TaskState<Progress>& job) { return downloadSyzygy(folder, base_url, files, *generation, job); });
}

std::optional<TaskEnd> SyzygyDownload::finished() {
    if (!job_.ready()) return std::nullopt;
    return job_.take();
}

}  // namespace fcg
