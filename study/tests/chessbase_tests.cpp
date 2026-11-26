#include <doctest/doctest.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <random>
#include <string>
#include <vector>

#include <chess.hpp>
#include <study/chessbase.hpp>
#include <study/chessbase_detail.hpp>
#include <study/import.hpp>
#include <study/pgn_reader.hpp>

#include "test_games.hpp"

using namespace fcg;
using namespace fcg::study;
using namespace fcg::test;
namespace fs = std::filesystem;

// study/tests/data/chessbase holds the 35 games of cbtest.pgn, games of our own, as a CBH database (cbtest1.*), as a
// 2CBH database (cbtest2.*), as archives of the CBH one (cbtest1.cbv, and cbtest1.cbz enciphered with the password
// TEST_PASSWORD) and as archives of the 2CBH one (cbtest2.2cbv, a zip of its files, and cbtest2.2cbz, enciphered with
// the same password). Game 1 has everything: a comment before
// its first move, comments after moves, symbols ($1 to $6, $10, $13, $14, $16, $18, $146), a variation with one of its
// own, coloured squares and arrows, [%clk], [%emt] and [%eval 0.31,24], accented names, teams, an annotator, round
// 3.2. Game 2 starts from a set-up position, game 3 promotes to a knight, game 4 has no result and takes en passant;
// the others are plain games, some without ratings.
//
// What the formats keep of them, found by reading them back:
// - both keep the tags (Event, Site, Date with its unknown parts, Round 3.2, the players, ratings, ECO, Annotator,
//   WhiteTeam and BlackTeam, EventDate), the start position, every move and variation, the comments, the symbols, the
//   coloured squares and arrows, [%emt] and [%eval] with its depth;
// - neither holds the [%clk] of game 1: no clock was stored with its moves;
// - CBH keeps its names in a Windows code page, where "ř" has no place: "Dvořák" is "Dvorák" there; 2CBH's are UTF-8;
// - both add the ECO code of every game from the standard start, which the PGN has for game 1 alone.

namespace {

// The password of the enciphered archives of our test games (a DES key: eight characters at most).
const std::string TEST_PASSWORD = "rstest20";

fs::path chessbaseData(const std::string& name) { return data("chessbase/" + name); }

void spit(const fs::path& file, const std::string& bytes) {
    std::ofstream out(file, std::ios::binary);
    out << bytes;
}

std::vector<GameTree> pgnGames() {
    const auto text = readFile(chessbaseData("cbtest.pgn"));
    std::vector<GameTree> games;
    for (std::string_view rest(text);;) {
        const auto read = readPgnGame(rest);
        if (!read.tree) break;
        games.push_back(*read.tree);
        rest.remove_prefix(read.end);
    }
    return games;
}

std::vector<ChessBaseGame> databaseGames(const fs::path& file, ChessBaseReport* report = nullptr,
                                         const ChessBaseOptions& options = {}) {
    ChessBaseDatabase db(file, options);
    std::vector<ChessBaseGame> games;
    db.forEach([&](ChessBaseGame&& g) {
        games.push_back(std::move(g));
        return true;
    });
    if (report) *report = db.report();
    return games;
}

std::map<std::string, std::string> tagMap(const std::vector<std::pair<std::string, std::string>>& tags) {
    return {tags.begin(), tags.end()};
}

// The tags of the PGN as a format keeps them: the ECO code it adds, "ř" where the code page has no place for it.
void asKept(std::map<std::string, std::string>& got, std::map<std::string, std::string>& want, int n,
            bool code_page_names) {
    if (!want.contains("ECO")) {
        CHECK(got.contains("ECO") == (n != 2));  // a set-up game has none
        if (got.contains("ECO")) CHECK(got["ECO"].size() == 3);
        got.erase("ECO");
    }
    if (code_page_names)
        for (auto& [k, v] : want)
            if (const auto at = v.find("ř"); at != std::string::npos) v.replace(at, std::string("ř").size(), "r");
}

void checkMove(const ReplayMove& got, const ReplayMove& want, const std::string& where) {
    INFO(where << " " << want.san);
    CHECK(got.move == want.move);
    CHECK(got.san == want.san);
    CHECK(got.mover == want.mover);
    CHECK(got.notes == want.notes);
    CHECK(got.score.has_value() == want.score.has_value());
    if (got.score && want.score) {
        CHECK(got.score->type == want.score->type);
        CHECK(got.score->value == want.score->value);
    }
    CHECK(got.depth == want.depth);
    // No clock in either format's files.
    CHECK_FALSE(got.clock_ms.has_value());
}

void checkTree(const GameTree& got, const GameTree& want) {
    CHECK(got.main.start_fen == want.main.start_fen);
    CHECK(got.main.result == want.main.result);
    CHECK(got.main.pre_comment == want.main.pre_comment);
    REQUIRE(got.main.moves.size() == want.main.moves.size());
    for (std::size_t i = 0; i < want.main.moves.size(); ++i) {
        auto expected = want.main.moves[i];
        expected.clock_ms.reset();
        checkMove(got.main.moves[i], expected, "move " + std::to_string(i + 1));
    }
    REQUIRE(got.variations.size() == want.variations.size());
    for (std::size_t v = 0; v < want.variations.size(); ++v) {
        INFO("variation " << v);
        CHECK(got.variations[v].parent == want.variations[v].parent);
        CHECK(got.variations[v].from == want.variations[v].from);
        REQUIRE(got.variations[v].moves.size() == want.variations[v].moves.size());
        for (std::size_t i = 0; i < want.variations[v].moves.size(); ++i)
            checkMove(got.variations[v].moves[i], want.variations[v].moves[i], "move " + std::to_string(i + 1));
    }
}

void checkDatabase(const fs::path& file, ChessBaseFormat format, bool code_page_names,
                   const ChessBaseOptions& options = {}) {
    const auto want = pgnGames();
    REQUIRE(want.size() == 35);
    ChessBaseDatabase db(file, options);
    CHECK(db.format() == format);
    CHECK(db.records() == 35);
    ChessBaseReport report;
    const auto games = databaseGames(file, &report, options);
    REQUIRE(games.size() == 35);
    for (std::size_t i = 0; i < games.size(); ++i) {
        const int n = static_cast<int>(i) + 1;
        INFO("game " << n);
        CHECK_FALSE(games[i].damaged);
        CHECK(games[i].problem.empty());
        CHECK(games[i].record == static_cast<uint32_t>(n));
        auto got_tags  = tagMap(games[i].tree.main.headers);
        auto want_tags = tagMap(want[i].main.headers);
        asKept(got_tags, want_tags, n, code_page_names);
        CHECK(got_tags == want_tags);
        CHECK(games[i].chess960 == false);
        checkTree(games[i].tree, want[i]);
    }
    // Nothing of these games is left out.
    CHECK(report.games == 35);
    CHECK(report.records == 35);
    CHECK(report.damaged + report.unreadable + report.texts + report.deleted + report.unknown == 0);
    CHECK(report.undecoded + report.other == 0);
    CHECK(report.training + report.pictures + report.sounds + report.videos + report.web_links + report.quotations +
              report.pawn_structures + report.piece_paths + report.variation_colours + report.correspondence ==
          0);
    CHECK(report.problems.empty());
}

// A copy of a database's files under another name, in a folder of the test's own.
fs::path copyDatabase(const std::string& from, const std::string& to, const std::string& folder) {
    const auto dir = fs::temp_directory_path() / folder;
    fs::remove_all(dir);
    fs::create_directories(dir);
    for (const auto& entry : fs::directory_iterator(chessbaseData("")))
        if (const auto name = entry.path().filename().string();
            name.starts_with(from + ".") && entry.path().extension() != ".cbv")
            fs::copy_file(entry.path(), dir / (to + entry.path().extension().string()));
    return dir;
}

bool throwsWith(const std::function<void()>& f, const std::string& part) {
    try {
        f();
    } catch (const std::runtime_error& e) {
        return std::string(e.what()).find(part) != std::string::npos;
    }
    return false;
}

// ---------------------------------------------------------------------------------------------------------------
// Archives made by the tests, in every way the format stores a member's blocks
// ---------------------------------------------------------------------------------------------------------------

void put16(std::string& out, uint32_t v) {
    out += static_cast<char>(v & 0xFF);
    out += static_cast<char>((v >> 8) & 0xFF);
}
void put32(std::string& out, uint32_t v) {
    put16(out, v & 0xFFFF);
    put16(out, v >> 16);
}
// Zip's CRC-32.
uint32_t crcOf(const std::string& bytes) {
    uint32_t crc = 0xFFFFFFFF;
    for (const unsigned char c : bytes) {
        crc ^= c;
        for (int k = 0; k < 8; ++k) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1)));
    }
    return ~crc;
}
void put64(std::string& out, uint64_t v) {
    put32(out, static_cast<uint32_t>(v));
    put32(out, static_cast<uint32_t>(v >> 32));
}

class Bits {
   public:
    void put(uint32_t value, int count) {
        for (int i = count - 1; i >= 0; --i) {
            if (used_ % 8 == 0) bytes_ += '\0';
            if ((value >> i) & 1) bytes_.back() = static_cast<char>(bytes_.back() | (0x80 >> (used_ % 8)));
            ++used_;
        }
    }
    [[nodiscard]] const std::string& bytes() const { return bytes_; }

   private:
    std::string bytes_;
    std::size_t used_ = 0;
};

// A Huffman block's body: every byte value's code of eight bits, the value itself (a complete code).
std::string huffman(const std::string& text) {
    Bits bits;
    bits.put(static_cast<uint32_t>(text.size()), 16);
    for (uint32_t symbol = 0; symbol < 256; ++symbol) {
        bits.put(8, 4);
        bits.put(symbol, 8);
    }
    for (const char c : text) bits.put(static_cast<unsigned char>(c), 8);
    return bits.bytes();
}

// LZ tokens for `text`, copying from the member's bytes before it (`history`) and its own: copies of 3 to 15 bytes
// from at most 4,098 bytes back, never longer than their distance.
std::string lz(const std::string& history, const std::string& text) {
    const std::string all = history + text;
    std::string out, group;
    uint32_t control = 0;
    int tokens       = 0;
    const auto flush = [&] {
        put16(out, control);
        out += group;
        group.clear();
        control = 0;
        tokens  = 0;
    };
    for (std::size_t at = history.size(); at < all.size();) {
        std::size_t best = 0, best_offset = 0;
        for (std::size_t offset = 3; offset <= std::min<std::size_t>(4098, at); ++offset) {
            std::size_t n = 0;
            while (n < 15 && n < offset && at + n < all.size() && all[at + n] == all[at - offset + n]) ++n;
            if (n > best) best = n, best_offset = offset;
        }
        if (best >= 3) {
            control |= 1u << (15 - tokens);
            const std::size_t v = best_offset - 3;
            group += static_cast<char>(best << 4 | (v & 0x0F));
            group += static_cast<char>(v >> 4);
            at += best;
        } else {
            group += all[at++];
        }
        if (++tokens == 16) flush();
    }
    if (tokens) flush();
    return out;
}

struct ArchiveMember {
    std::string name, bytes;
};

// An archive of `members`, each cut in blocks of `block` bytes stored in turn as they are (0), as LZ tokens (1), as a
// Huffman code (2) and as a Huffman code of LZ tokens (3).
std::string archive(const std::vector<ArchiveMember>& members, std::size_t block) {
    std::vector<std::string> streams;
    for (const auto& m : members) {
        std::string stream;
        int mode = 0;
        for (std::size_t at = 0; at < m.bytes.size(); at += block, mode = (mode + 1) % 4) {
            const auto part    = m.bytes.substr(at, block);
            const auto history = m.bytes.substr(0, at);
            std::string body;
            switch (mode) {
                case 0:
                    body = part;
                    break;
                case 1:
                    body = lz(history, part);
                    break;
                case 2:
                    body = huffman(part);
                    break;
                default:
                    body = huffman(lz(history, part));
                    break;
            }
            put16(stream, static_cast<uint32_t>(body.size() + 1));
            put16(stream, 0xA5A5);
            stream += static_cast<char>(mode);
            stream += body;
        }
        streams.push_back(stream);
    }
    std::string out;
    out += '\x08';
    out += '\0';
    put16(out, static_cast<uint32_t>(members.size()));
    out += std::string("\xAD\x00\x03\x00", 4);
    uint64_t offset = 8 + 173 * members.size();
    for (std::size_t i = 0; i < members.size(); ++i) {
        std::string record = members[i].name;
        record.resize(128, '\0');
        put32(record, static_cast<uint32_t>(offset));
        put32(record, static_cast<uint32_t>(streams[i].size()));
        put32(record, static_cast<uint32_t>(members[i].bytes.size()));
        record += std::string(9, '\x01');
        put64(record, offset);
        put64(record, streams[i].size());
        put64(record, members[i].bytes.size());
        out += record;
        offset += streams[i].size();
    }
    for (const auto& s : streams) out += s;
    return out;
}

std::vector<ArchiveMember> cbhMembers(const std::string& base) {
    std::vector<ArchiveMember> members;
    for (const char* ext : {".cba", ".cbc", ".cbe", ".cbg", ".cbh", ".cbj", ".cbp", ".cbs", ".cbt"})
        members.push_back({base + ext, readFile(chessbaseData(std::string("cbtest1") + ext))});
    return members;
}

// ---------------------------------------------------------------------------------------------------------------
// 2CBH databases made by the tests, record by record
// ---------------------------------------------------------------------------------------------------------------

// A record: its kind (1 a game, 3 a text), the words of its moves, its annotations (none when empty).
struct TwoCbhRecord {
    uint8_t kind = 1;
    std::vector<uint16_t> moves;
    std::string notes;
};

// Words of the moves' table: moves, castling, the pieces of a set-up position.
constexpr uint16_t E2E4 = 0xAD3F, E7E5 = 0xB02D, F7F6 = 0xB08C, D2D4 = 0xACE1, D1H5 = 0x167B;
constexpr uint16_t WHITE_SHORT = 0xB12A, BLACK_LONG = 0xB12B;
constexpr uint16_t WHITE_KING_E1 = 0xC04D, WHITE_ROOK_A1 = 0xC12D, WHITE_ROOK_H1 = 0xC165, BLACK_KING_E8 = 0xC194,
                   BLACK_ROOK_A8 = 0xC274, BLACK_ROOK_H8 = 0xC2AC;
constexpr uint16_t PASS = 0xFFFA, SETUP = 0xFFFB, MOVES = 0xFFFC, END = 0xFFFF;

std::string le32Of(uint32_t v) {
    std::string out;
    put32(out, v);
    return out;
}

// The .2cbh, .2cbg, .2cba and .2lid of a database of `records`, in a folder of the test's own; no names.
fs::path twoCbhDatabase(const std::string& folder, const std::vector<TwoCbhRecord>& records) {
    const auto dir = fs::temp_directory_path() / folder;
    fs::remove_all(dir);
    fs::create_directories(dir);
    // Records that frame themselves after a 12-byte header (the file's size, where the first record starts).
    std::string moves(12, '\0'), notes(12, '\0');
    const auto frame = [](std::string& file, const std::string& content) {
        const uint64_t at = file.size();
        file += std::string("\x88\x77\x66\x55\x44\x33\x22\x11", 8);
        put32(file, static_cast<uint32_t>(content.size()));
        put32(file, 0);
        file += std::string(8, '\0');
        file += content + "\xff\xff";
        put64(file, 34 + content.size());
        return at;
    };
    std::string headers;
    std::string head(192, '\0');
    head[0x0A] = static_cast<char>(192);
    head.replace(0x10, 4, le32Of(static_cast<uint32_t>(records.size() + 1)));
    headers += head;
    for (const auto& r : records) {
        std::string words;
        for (const uint16_t w : r.moves) put16(words, w);
        std::string record;
        record += static_cast<char>(r.kind);
        record += std::string(7, '\0');
        put64(record, frame(moves, words));
        put64(record, r.notes.empty() ? 0 : frame(notes, r.notes));
        for (int id = 0; id < 7; ++id) put64(record, ~uint64_t{0});  // no player, tournament, annotator ... team
        record.resize(192, '\0');
        record[0x58] = 2;  // 1-0
        headers += record;
    }
    for (auto* file : {&moves, &notes}) {
        std::string size;
        put64(size, file->size());
        put16(size, 12);
        file->replace(0, 10, size);
    }
    // The names: a header of five kinds of 8-byte records, none of them.
    std::string names;
    for (const uint32_t v : {108u, 5u}) names += std::string{0, 0, 0, static_cast<char>(v)};
    for (int kind = 0; kind < 5; ++kind) names += std::string{0, 0, 0, 8} + std::string(16, '\0');
    spit(dir / "Made.2cbh", headers);
    spit(dir / "Made.2cbg", moves);
    spit(dir / "Made.2cba", notes);
    spit(dir / "Made.2lid", names);
    return dir / "Made.2cbh";
}

// Annotations: the word that starts them, groups of annotations by their move, ffff.
class TwoCbhNotes {
   public:
    TwoCbhNotes() { put16(bytes_, 0x2000); }
    // The annotations of move `position` (-1 the game), each its type and its data.
    void at(int32_t position, const std::vector<std::pair<uint16_t, std::string>>& items) {
        put32(bytes_, static_cast<uint32_t>(position));
        put32(bytes_, static_cast<uint32_t>(items.size()));
        for (const auto& [type, data] : items) {
            put16(bytes_, type);
            bytes_ += data;
        }
    }
    [[nodiscard]] std::string done() const { return bytes_ + "\xff\xff"; }

   private:
    std::string bytes_;
};

// A text with its length in 4 bytes.
std::string sized(const std::string& text) { return le32Of(static_cast<uint32_t>(text.size())) + text; }

// A comment after a move (type 02) in any language.
std::pair<uint16_t, std::string> comment(const std::string& text) {
    return {0x02, std::string(2, '\0') + std::string{static_cast<char>(chessbase::ANY_LANGUAGE), 0} + sized(text)};
}

// A list of a training question's: texts in languages 0, 1 ..., then arrows.
std::string trainingList(const std::vector<std::string>& texts, const std::string& arrows = {}) {
    std::string out;
    put16(out, static_cast<uint32_t>(texts.size() + (arrows.empty() ? 0 : 1)));
    for (std::size_t i = 0; i < texts.size(); ++i) {
        put16(out, static_cast<uint32_t>(i));
        out += sized(texts[i]);
    }
    if (!arrows.empty()) {
        put16(out, 7);
        out += sized(arrows);
    }
    return out;
}

// A training question of a move to find, with a hint of a text and arrows, and its answer.
std::string trainingMove() {
    std::string out = "\x01\x01" + le32Of(1) + le32Of(300) + std::string("\x0a\x00", 2);
    out += trainingList({"Find the move."}) + trainingList({}) + trainingList({"Not a sacrifice."}) +
           trainingList({"A pawn."}, "\x02\x2d\x2c");
    return out + "\x01" + std::string("\x1c\x1e\x00\x0a", 4) + trainingList({"Right."});
}

// A training question of a choice among two answers, in two languages.
std::string trainingChoice() {
    std::string out = "\x01\x01" + le32Of(2) + le32Of(600) + std::string("\x0f\x00", 2);
    out += trainingList({"The move is...", "Der Zug ist..."}) + trainingList({}) + trainingList({}) + trainingList({});
    out += "\x02";
    out += std::string(1, '\0') + trainingList({"...a mistake."}) + trainingList({});
    out += "\x0f" + trainingList({"...strong."}) + trainingList({"Right.", "Richtig."});
    return out;
}

// A game quoted: its names, its ratings' kinds, its start position when it is given (the standard one), its moves.
std::string quotation(bool with_board, int moves) {
    std::string out("\x01\x02\x00\x00\x00\x01\x00\x00\x00\x00", 10);
    for (const std::string text : {"White", "First", "Black", "Second", "Lyon", "Our own"})
        out += static_cast<char>(text.size() + 1) + text + '\0';
    out += std::string(79, '\0');
    for (int k = 0; k < 2; ++k) out += std::string("\x01\x00\x01\x00\x00", 5) + sized("FIDE");
    out += std::string(26, '\0');
    out += with_board ? '\0' : '\x01';
    if (with_board) {
        const std::string back = "\x05\x03\x04\x02\x01\x04\x03\x05";
        for (int f = 0; f < 8; ++f) out += std::string{back[f], 6, 0, 0, 0, 0, 14, static_cast<char>(back[f] + 8)};
        out += std::string(11, '\0');
    }
    out += std::string(2, '\0') + le32Of(static_cast<uint32_t>(moves));
    for (int k = 0; k < moves; ++k) out += std::string("\x21\x23\x00\x00\x00", 5);  // e2-e4, again and again
    return out + le32Of(0);
}

// A file of a zip made by the tests: stored as it is (method 0), deflated in deflate's stored blocks (8), or said to be
// in another method.
struct ZipFile {
    std::string name, bytes;
    uint16_t method = 0;
};

// `bytes` as deflate's stored blocks, of 65,535 bytes at most each.
std::string storedBlocks(const std::string& bytes) {
    std::string out;
    std::size_t at = 0;
    do {
        const auto n = std::min<std::size_t>(65535, bytes.size() - at);
        out += static_cast<char>(at + n == bytes.size() ? 1 : 0);  // BFINAL, BTYPE 00
        put16(out, static_cast<uint32_t>(n));
        put16(out, static_cast<uint32_t>(~n & 0xFFFF));
        out += bytes.substr(at, n);
        at += n;
    } while (at < bytes.size());
    return out;
}

// A zip of `files`, their local headers, their data, the central directory and its end.
std::string zipOf(const std::vector<ZipFile>& files) {
    std::string data, directory;
    for (const auto& f : files) {
        const auto stored = f.method == 8 ? storedBlocks(f.bytes) : f.bytes;
        const auto crc    = crcOf(f.bytes);
        const auto offset = static_cast<uint32_t>(data.size());
        const auto fields = [&](std::string& out) {
            put16(out, 0);  // flags
            put16(out, f.method);
            put32(out, 0);  // time and date
            put32(out, crc);
            put32(out, static_cast<uint32_t>(stored.size()));
            put32(out, static_cast<uint32_t>(f.bytes.size()));
            put16(out, static_cast<uint32_t>(f.name.size()));
            put16(out, 0);  // extra
        };
        std::string local = "PK\x03\x04";
        put16(local, 20);
        fields(local);
        data += local + f.name + stored;
        std::string entry = "PK\x01\x02";
        put16(entry, 20);
        put16(entry, 20);
        fields(entry);
        put16(entry, 0);  // comment
        put16(entry, 0);  // disk
        put16(entry, 0);  // internal attributes
        put32(entry, 0);  // external attributes
        put32(entry, offset);
        directory += entry + f.name;
    }
    std::string end = "PK\x05\x06";
    put16(end, 0);
    put16(end, 0);
    put16(end, static_cast<uint32_t>(files.size()));
    put16(end, static_cast<uint32_t>(files.size()));
    put32(end, static_cast<uint32_t>(directory.size()));
    put32(end, static_cast<uint32_t>(data.size()));
    put16(end, 0);
    return data + directory + end;
}

}  // namespace

TEST_CASE("ChessBase CBH database: the 35 games as the PGN they were made of") {
    checkDatabase(chessbaseData("cbtest1.cbh"), ChessBaseFormat::CBH, true);
}

TEST_CASE("ChessBase 2CBH database: the 35 games as the PGN they were made of") {
    checkDatabase(chessbaseData("cbtest2.2cbh"), ChessBaseFormat::TwoCBH, false);
}

TEST_CASE("ChessBase CBV archive: the CBH database's 35 games, its members unpacked as they are read") {
    checkDatabase(chessbaseData("cbtest1.cbv"), ChessBaseFormat::CBH, true);
    // Its members unpacked are the database's files, a block at a time or whole.
    for (const uint64_t whole : {uint64_t{0}, uint64_t{16} << 20}) {
        auto members = chessbase::archiveMembers(chessbaseData("cbtest1.cbv"), "", whole);
        for (const char* ext :
             {".cbh", ".cbg", ".cba", ".cbp", ".cbt", ".cbc", ".cbs", ".cbe", ".cbj", ".cbm", ".cbtt", ".cbl"}) {
            INFO(ext);
            auto member      = members->open(ext, false);
            const auto loose = readFile(chessbaseData(std::string("cbtest1") + ext));
            REQUIRE(member);
            REQUIRE(member->size() == loose.size());
            const auto bytes = member->read(0, loose.size());
            CHECK(std::string(bytes.begin(), bytes.end()) == loose);
        }
        CHECK_FALSE(members->has(".2cbh"));
    }
}

TEST_CASE("ChessBase game 1: what each format keeps of a game that has everything") {
    for (const char* file : {"cbtest1.cbh", "cbtest2.2cbh"}) {
        INFO(file);
        ChessBaseDatabase db(chessbaseData(file));
        const auto game = db.read(1);
        REQUIRE(game);
        const auto& main = game->tree.main;
        CHECK(main.header("Round") == "3.2");
        CHECK(main.header("White") == "Müller, Jörg");
        CHECK(main.header("Annotator") == "RookStudio");
        CHECK(main.header("WhiteTeam") == "Lyon");
        CHECK(main.header("BlackTeam") == "Paris");
        CHECK(main.header("EventDate") == "2026.11.18");
        CHECK(main.pre_comment == "A game of our own, annotated for the tests.");
        CHECK(main.moves[0].notes.emt_ms == 10000);
        CHECK(main.moves[9].notes.after == "The Najdorf.");
        CHECK(main.moves[10].notes.nags == std::vector<uint8_t>{1});
        CHECK(main.moves[10].notes.squares.size() == 3);
        CHECK(main.moves[10].notes.arrows.size() == 3);
        CHECK(main.moves[18].notes.nags == std::vector<uint8_t>{146});
        CHECK(main.moves[18].notes.eval_tag);
        CHECK(main.moves[18].score->value == 31);
        CHECK(main.moves[18].depth == 24);
        REQUIRE(game->tree.variations.size() == 2);
        CHECK(game->tree.variations[1].parent == 0);
        CHECK(game->tree.variations[1].moves[1].notes.nags == std::vector<uint8_t>{14});
        CHECK(game->tree.variations[0].moves[3].notes.nags == std::vector<uint8_t>{16});
    }
    // The set-up game, the promotion to a knight, the game without a result and its en passant.
    ChessBaseDatabase db(chessbaseData("cbtest2.2cbh"));
    CHECK(db.read(2)->tree.main.start_fen == "8/5pk1/6p1/8/5P2/6P1/5K2/3R1r2 w - - 0 50");
    CHECK(db.read(3)->tree.main.moves[8].san == "bxa8=N");
    CHECK(db.read(4)->tree.main.result == "*");
    CHECK(db.read(4)->tree.main.moves[4].move.typeOf() == chess::Move::ENPASSANT);
}

TEST_CASE("ChessBase database: opened by any of its files, refused with a readable message without one") {
    {
        const auto dir = copyDatabase("cbtest1", "Base", "chessbase-open-cbh");
        // The extensions in another case.
        fs::rename(dir / "Base.cbg", dir / "Base.CBG");
        for (const char* ext : {".cbh", ".CBG", ".cba", ".cbp", ".cbj"}) {
            INFO(ext);
            ChessBaseDatabase db(dir / (std::string("Base") + ext));
            CHECK(db.records() == 35);
            CHECK(db.read(35).has_value());
        }
        {
            ChessBaseDatabase db(dir / "Base.cbh");
            CHECK_THROWS_AS((void)db.read(0), std::out_of_range);
            CHECK_THROWS_AS((void)db.read(36), std::out_of_range);
            // Read out of order, the same games.
            const auto last = db.read(35), first = db.read(1);
            CHECK(first->tree.main.moves.size() == 49);
            CHECK(last->record == 35);
        }
        // Closed, its files may go (Windows keeps a mapped file until it is closed).
        fs::remove(dir / "Base.CBG");
        CHECK(throwsWith([&] { ChessBaseDatabase(dir / "Base.cbh"); }, ".cbg is missing"));
        fs::remove(dir / "Base.cbp");
        CHECK(throwsWith([&] { ChessBaseDatabase(dir / "Base.cba"); }, "is missing"));
        spit(dir / "Other.pgn", "1. e4 *\n");
        CHECK(throwsWith([&] { ChessBaseDatabase(dir / "Other.pgn"); }, "not a ChessBase database"));
        CHECK(throwsWith([&] { ChessBaseDatabase(dir / "None.cbh"); }, "no such file"));
    }
    {
        const auto dir = copyDatabase("cbtest2", "Base", "chessbase-open-2cbh");
        for (const char* ext : {".2cbh", ".2cbg", ".2cba", ".2lid"}) {
            INFO(ext);
            ChessBaseDatabase db(dir / (std::string("Base") + ext));
            CHECK(db.format() == ChessBaseFormat::TwoCBH);
            CHECK(db.records() == 35);
        }
        fs::remove(dir / "Base.2lid");
        CHECK(throwsWith([&] { ChessBaseDatabase(dir / "Base.2cbh"); }, ".2lid is missing"));
    }
}

TEST_CASE("ChessBase archive: blocks stored in every way the format has, members of many blocks") {
    // Blocks of 32 bytes stored in turn as they are, as LZ tokens copying across blocks, as a Huffman code and as a
    // Huffman code of LZ tokens; the header file and the moves read a block at a time, far back and forth.
    const auto dir = fs::temp_directory_path() / "chessbase-blocks";
    fs::create_directories(dir);
    spit(dir / "Blocks.cbv", archive(cbhMembers("Blocks"), 32));
    ChessBaseOptions options;
    options.unpack_whole = 0;
    checkDatabase(dir / "Blocks.cbv", ChessBaseFormat::CBH, true, options);
    ChessBaseDatabase db(dir / "Blocks.cbv", options);
    const auto loose = databaseGames(chessbaseData("cbtest1.cbh"));
    for (const uint32_t record : {35u, 1u, 18u, 2u, 34u, 1u}) {
        INFO(record);
        const auto game = db.read(record);
        REQUIRE(game);
        CHECK(game->tree.main.moves.size() == loose[record - 1].tree.main.moves.size());
        CHECK(game->tree.main.headers == loose[record - 1].tree.main.headers);
    }
    // The members, unpacked a block at a time, read at every place.
    auto members      = chessbase::archiveMembers(dir / "Blocks.cbv", "", 0);
    auto header       = members->open(".cbh", false);
    const auto source = readFile(chessbaseData("cbtest1.cbh"));
    for (uint64_t at = source.size(); at-- > 0;) {
        const std::size_t n = std::min<std::size_t>(97, source.size() - at);
        const auto bytes    = header->read(at, n);
        REQUIRE(std::string(bytes.begin(), bytes.end()) == source.substr(at, n));
    }
    CHECK_THROWS_AS((void)header->read(source.size(), 1), chessbase::Damage);
}

TEST_CASE("ChessBase CBZ archive: DES, and a .cbv enciphered with a password") {
    // DES's published test vectors, the key an eight-byte password.
    struct Vector {
        std::string key, plain, cipher;
    };
    const auto hex = [](const char* s) {
        std::string out;
        for (; s[0] && s[1]; s += 2) out += static_cast<char>(std::stoi(std::string(s, 2), nullptr, 16));
        return out;
    };
    for (const Vector& v : {Vector{hex("0123456789ABCDEF"), hex("4E6F772069732074"), hex("3FA40E8A984D4815")},
                            Vector{hex("0000000000000000"), hex("0000000000000000"), hex("8CA64DE9C1B123A7")},
                            Vector{hex("FFFFFFFFFFFFFFFF"), hex("FFFFFFFFFFFFFFFF"), hex("7359B2163E4EDC58")},
                            Vector{hex("3000000000000000"), hex("1000000000000001"), hex("958E6E627A05557B")}}) {
        std::vector<uint8_t> block(v.plain.begin(), v.plain.end());
        chessbase::desBlocks(v.key, block, false);
        CHECK(std::string(block.begin(), block.end()) == v.cipher);
        chessbase::desBlocks(v.key, block, true);
        CHECK(std::string(block.begin(), block.end()) == v.plain);
    }
    // The CBV archive enciphered as a .cbz is, with a short password (repeated) and a long one (folded).
    const auto dir = fs::temp_directory_path() / "chessbase-cbz";
    fs::create_directories(dir);
    for (const std::string password : {"pass", "my long password"}) {
        INFO(password);
        auto bytes = readFile(chessbaseData("cbtest1.cbv"));
        bytes.resize((bytes.size() + 7) / 8 * 8, '\0');
        std::vector<uint8_t> enciphered(bytes.begin(), bytes.end());
        chessbase::desBlocks(password, enciphered, false);
        spit(dir / "Base.cbz", std::string(enciphered.begin(), enciphered.end()));
        ChessBaseOptions options;
        options.password = password;
        checkDatabase(dir / "Base.cbz", ChessBaseFormat::CBH, true, options);
        CHECK(throwsWith([&] { ChessBaseDatabase(dir / "Base.cbz", ChessBaseOptions{"wrong"}); }, "wrong password"));
        CHECK(throwsWith([&] { ChessBaseDatabase(dir / "Base.cbz"); }, "needs its password"));
    }
}

TEST_CASE("ChessBase CBZ archive: our test games enciphered, read with their password, a wrong one refused") {
    const auto file = chessbaseData("cbtest1.cbz");
    CHECK(isChessBase(file));
    CHECK(isChessBaseArchive(file));
    CHECK(isEncipheredArchive(file));
    ChessBaseOptions options;
    options.password = TEST_PASSWORD;
    checkDatabase(file, ChessBaseFormat::CBH, true, options);
    // A wrong password shows in the first block, which does not decipher to an archive's header.
    CHECK(archivePasswordFits(file, TEST_PASSWORD));
    CHECK_FALSE(archivePasswordFits(file, "rstest22"));
    CHECK(archivePasswordFits(file, "rstest21"));  // DES leaves out the lowest bit of each byte of its key
    CHECK_FALSE(archivePasswordFits(file, ""));
    CHECK_FALSE(archivePasswordFits(chessbaseData("no such archive.cbz"), TEST_PASSWORD));
    CHECK(throwsWith([&] { ChessBaseDatabase(file, ChessBaseOptions{"wrong"}); }, "wrong password"));
    CHECK(throwsWith([&] { (void)ChessBaseDatabase(file); }, "needs its password"));
}

TEST_CASE("ChessBase 2CBV archive: a zip of the 2CBH database's files, its members unpacked as they are read") {
    const auto file = chessbaseData("cbtest2.2cbv");
    CHECK(isChessBase(file));
    CHECK(isChessBase("Base.2CBV"));
    CHECK(isChessBaseArchive(file));
    CHECK_FALSE(isEncipheredArchive(file));
    checkDatabase(file, ChessBaseFormat::TwoCBH, false);
    // Its members are the database's files: unpacked whole, or a piece at a time.
    for (const uint64_t whole : {uint64_t{0}, uint64_t{16} << 20}) {
        INFO(whole);
        auto members = chessbase::archiveMembers(file, "", whole);
        for (const char* ext : {".2cbh", ".2cbg", ".2cba", ".2lid", ".2lcd", ".2lgd"}) {
            INFO(ext);
            auto member      = members->open(ext, false);
            const auto loose = readFile(chessbaseData(std::string("cbtest2") + ext));
            REQUIRE(member);
            REQUIRE(member->size() == loose.size());
            const auto bytes = member->read(0, loose.size());
            CHECK(std::string(bytes.begin(), bytes.end()) == loose);
        }
        CHECK(members->has(".ini"));
        CHECK_FALSE(members->has(".cbh"));
    }
    // Read a piece at a time at every place, backward: each read before what is kept unpacks the member again.
    auto members      = chessbase::archiveMembers(file, "", 0);
    auto moves        = members->open(".2cbg", false);
    const auto source = readFile(chessbaseData("cbtest2.2cbg"));
    for (uint64_t at = source.size(); at-- > 0;) {
        const std::size_t n = std::min<std::size_t>(97, source.size() - at);
        const auto bytes    = moves->read(at, n);
        REQUIRE(std::string(bytes.begin(), bytes.end()) == source.substr(at, n));
    }
    CHECK_THROWS_AS((void)moves->read(source.size(), 1), chessbase::Damage);
    // Its games one by one, out of order.
    ChessBaseDatabase db(file, ChessBaseOptions{"", 0});
    const auto loose = databaseGames(chessbaseData("cbtest2.2cbh"));
    for (const uint32_t record : {35u, 1u, 18u, 2u, 34u, 1u}) {
        INFO(record);
        const auto game = db.read(record);
        REQUIRE(game);
        CHECK(game->tree.main.moves.size() == loose[record - 1].tree.main.moves.size());
        CHECK(game->tree.main.headers == loose[record - 1].tree.main.headers);
    }
}

TEST_CASE("ChessBase 2CBZ archive: the zip enciphered, read with its password, a wrong one refused") {
    const auto file = chessbaseData("cbtest2.2cbz");
    CHECK(isChessBase(file));
    CHECK(isChessBaseArchive(file));
    CHECK(isEncipheredArchive(file));
    CHECK(isEncipheredArchive("Base.2CbZ"));
    ChessBaseOptions options;
    options.password = TEST_PASSWORD;
    checkDatabase(file, ChessBaseFormat::TwoCBH, false, options);
    options.unpack_whole = 0;
    checkDatabase(file, ChessBaseFormat::TwoCBH, false, options);
    // The same games as the plain zip's, and as the database's own files.
    const auto plain = databaseGames(chessbaseData("cbtest2.2cbv"));
    const auto games = databaseGames(file, nullptr, options);
    REQUIRE(games.size() == plain.size());
    for (std::size_t i = 0; i < games.size(); ++i) {
        CHECK(games[i].tree.main.headers == plain[i].tree.main.headers);
        CHECK(games[i].tree.main.moves.size() == plain[i].tree.main.moves.size());
    }
    // It is the zip, enciphered as a .cbz is, padded with zeros to the cipher's blocks.
    auto zip = readFile(chessbaseData("cbtest2.2cbv"));
    zip.resize((zip.size() + 7) / 8 * 8, '\0');
    std::vector<uint8_t> enciphered(zip.begin(), zip.end());
    chessbase::desBlocks(TEST_PASSWORD, enciphered, false);
    CHECK(std::string(enciphered.begin(), enciphered.end()) == readFile(file));
    CHECK(archivePasswordFits(file, TEST_PASSWORD));
    CHECK_FALSE(archivePasswordFits(file, "RSTEST20"));
    CHECK(throwsWith([&] { ChessBaseDatabase(file, ChessBaseOptions{"RSTEST20"}); }, "wrong password"));
    CHECK(throwsWith([&] { (void)ChessBaseDatabase(file); }, "needs its password"));
    // The import reads it as a ChessBase database, not as a PGN.
    ImportProgress progress;
    std::atomic<bool> cancel{false};
    uint64_t given    = 0;
    const auto report = importChessBase(
        file, ImportOptions{}, progress, cancel, [&](std::vector<ImportedGame>&& batch) { given += batch.size(); },
        nullptr, options);
    CHECK(report.error.empty());
    CHECK(report.games == 35);
    CHECK(report.damaged == 0);
    CHECK(given == 35);
    const auto refused = importChessBase(
        file, ImportOptions{}, progress, cancel, [&](std::vector<ImportedGame>&&) {}, nullptr,
        ChessBaseOptions{"nope"});
    CHECK(refused.games == 0);
    CHECK(refused.error.find("wrong password") != std::string::npos);
}

TEST_CASE("ChessBase archive: a zip whose files are stored as they are, or with a compression it does not read") {
    // A zip of the 2CBH database's files, each stored as it is (method 0); then its moves file in method 12 (bzip2).
    const auto dir = fs::temp_directory_path() / "chessbase-stored";
    fs::create_directories(dir);
    const auto files = [](uint16_t moves_method) {
        std::vector<ZipFile> out;
        for (const char* ext : {".2cbh", ".2cbg", ".2cba", ".2lid"})
            out.push_back({std::string("Stored") + ext, readFile(chessbaseData(std::string("cbtest2") + ext)),
                           std::string(ext) == ".2cbg" ? moves_method : uint16_t{0}});
        return out;
    };
    spit(dir / "Stored.2cbv", zipOf(files(0)));
    checkDatabase(dir / "Stored.2cbv", ChessBaseFormat::TwoCBH, false);
    checkDatabase(dir / "Stored.2cbv", ChessBaseFormat::TwoCBH, false, ChessBaseOptions{"", 0});
    spit(dir / "Bzip2.2cbv", zipOf(files(12)));
    CHECK(throwsWith([&] { ChessBaseDatabase(dir / "Bzip2.2cbv"); }, "method 12"));
    // A zip without a database in it.
    auto other = zipOf(files(0));
    for (auto at = other.find("Stored.2cbh"); at != std::string::npos; at = other.find("Stored.2cbh"))
        other.replace(at, 11, "Stored.txt0");
    spit(dir / "Other.2cbv", other);
    CHECK(throwsWith([&] { ChessBaseDatabase(dir / "Other.2cbv"); }, "holds no ChessBase database"));
}

TEST_CASE("ChessBase archive: a large deflated member read a piece at a time, forward, back, and far ahead") {
    // Seven megabytes of their own in deflate's stored blocks, beside the database's files: more than the member keeps
    // behind its reads.
    std::mt19937 random(69);
    std::string big(7u << 20, '\0');
    for (auto& c : big) c = static_cast<char>(random() & 0x7F);
    auto files = std::vector<ZipFile>{{"Big.dat", big, 8}};
    for (const char* ext : {".2cbh", ".2cbg", ".2cba", ".2lid"})
        files.push_back({std::string("Big") + ext, readFile(chessbaseData(std::string("cbtest2") + ext)), 8});
    const auto dir = fs::temp_directory_path() / "chessbase-big";
    fs::create_directories(dir);
    spit(dir / "Big.2cbv", zipOf(files));
    checkDatabase(dir / "Big.2cbv", ChessBaseFormat::TwoCBH, false, ChessBaseOptions{"", 0});
    auto members = chessbase::archiveMembers(dir / "Big.2cbv", "", 0);
    auto member  = members->open(".dat", false);
    REQUIRE(member);
    REQUIRE(member->size() == big.size());
    const auto same = [&](uint64_t at, std::size_t n) {
        const auto bytes = member->read(at, n);
        return std::string(bytes.begin(), bytes.end()) == big.substr(at, n);
    };
    for (uint64_t at = 0; at < big.size(); at += 100000)
        REQUIRE(same(at, std::min<std::size_t>(100000, big.size() - at)));
    CHECK(same(big.size() - 10, 10));
    CHECK(same(0, 1000));                       // back to the start: unpacked again
    CHECK(same(6u << 20, 4096));                // far ahead in one read
    CHECK(same((6u << 20) - (512u << 10), 8));  // a little back: kept
    CHECK(same(3, 5u << 20));                   // a read of megabytes
    CHECK_THROWS_AS((void)member->read(big.size() - 1, 2), chessbase::Damage);
    // Whole, its CRC-32 checked; a member whose bytes are not those its CRC says is damaged.
    auto whole = chessbase::archiveMembers(dir / "Big.2cbv", "", 16u << 20)->open(".2cbh", false);
    CHECK(whole->size() == readFile(chessbaseData("cbtest2.2cbh")).size());
    auto wrong      = zipOf(files);
    const auto name = wrong.rfind("Big.2lid");  // its directory entry: the CRC 30 bytes before its name
    REQUIRE(name != std::string::npos);
    wrong[name - 30] = static_cast<char>(wrong[name - 30] ^ 1);
    spit(dir / "Wrong.2cbv", wrong);
    CHECK_THROWS_WITH_AS((void)chessbase::archiveMembers(dir / "Wrong.2cbv", "", 0)->open(".2lid", true),
                         doctest::Contains("CRC-32"), chessbase::Damage);
}

TEST_CASE("ChessBase databases: truncated and garbled copies are refused or read as damaged, never past their files") {
    struct Set {
        std::string from;
        std::vector<std::string> extensions;
        std::string password;
    };
    const std::vector<Set> sets = {
        {"cbtest1", {".cbh", ".cbg", ".cba", ".cbp", ".cbt", ".cbc", ".cbs", ".cbe", ".cbj"}, ""},
        {"cbtest2", {".2cbh", ".2cbg", ".2cba", ".2lid"}, ""},
        {"cbtest1", {".cbv"}, ""},
        {"cbtest2", {".2cbv"}, ""},
        {"cbtest2", {".2cbz"}, TEST_PASSWORD},
    };
    // Reads everything it can: only a readable error at opening may escape; damage is in the report.
    const auto readAll = [](const fs::path& file, const std::string& password, ChessBaseReport& report) {
        try {
            ChessBaseOptions options;
            options.password = password;
            ChessBaseDatabase db(file, options);
            db.forEach([](ChessBaseGame&& g) {
                // What was read of a damaged game is a tree a PGN writer and reader agree on.
                (void)g;
                return true;
            });
            for (uint32_t r = db.records(); r >= 1; --r) (void)db.read(r);
            report = db.report();
            return true;
        } catch (const std::runtime_error&) {
            return false;
        }
    };
    std::mt19937 random(20261120);
    int opened = 0, refused = 0, damaged = 0;
    for (const auto& set : sets) {
        for (const auto& ext : set.extensions) {
            const auto original = readFile(chessbaseData(set.from + ext));
            const auto dir      = fs::temp_directory_path() / "chessbase-damage";
            const auto reset    = [&] {
                fs::remove_all(dir);
                fs::create_directories(dir);
                for (const auto& e : set.extensions) fs::copy_file(chessbaseData(set.from + e), dir / ("Base" + e));
            };
            const auto file = dir / ("Base" + set.extensions.front());
            // Cut.
            for (const std::size_t cut :
                 {std::size_t{0}, std::size_t{1}, original.size() / 3, original.size() / 2, original.size() - 1}) {
                INFO(set.from << ext << " cut at " << cut);
                reset();
                spit(dir / ("Base" + ext), original.substr(0, cut));
                ChessBaseReport report;
                if (readAll(file, set.password, report)) {
                    ++opened;
                    damaged += report.damaged + report.unreadable > 0;
                } else {
                    ++refused;
                }
            }
            // Garbled: bytes changed at random places, a run of them set to ff.
            for (int round = 0; round < 12; ++round) {
                INFO(set.from << ext << " garbled, round " << round);
                reset();
                auto bytes = original;
                for (int k = 0; k < 1 + round % 4; ++k) bytes[random() % bytes.size()] = static_cast<char>(random());
                const std::size_t run = random() % bytes.size();
                for (std::size_t i = run; i < std::min(bytes.size(), run + 1 + round); ++i) bytes[i] = '\xff';
                spit(dir / ("Base" + ext), bytes);
                ChessBaseReport report;
                if (readAll(file, set.password, report))
                    ++opened;
                else
                    ++refused;
            }
        }
    }
    MESSAGE("damaged copies: " << opened << " opened (" << damaged << " of the cut ones with damage reported), "
                               << refused << " refused");
    CHECK(opened > 0);
    CHECK(refused > 0);
    CHECK(damaged > 0);
}

TEST_CASE("ChessBase database: its games as the PGN import gives them, ready to be copied into a database") {
    // The PGN import of the same games.
    const auto pgn = readFile(chessbaseData("cbtest.pgn"));
    std::vector<ImportedGame> want;
    for (const auto text : splitGames(pgn)) {
        ImportedGame g;
        REQUIRE(readImportedGame(text, g));
        want.push_back(std::move(g));
    }
    REQUIRE(want.size() == 35);
    for (const char* file : {"cbtest1.cbh", "cbtest2.2cbh", "cbtest1.cbv"}) {
        INFO(file);
        std::vector<ImportedGame> got;
        int batches = 0;
        ImportProgress progress;
        std::atomic<bool> cancel{false};
        ChessBaseReport left_out;
        ImportOptions options;
        options.piece_bytes = 10 * 1024;  // ten games a batch
        const auto report   = importChessBase(
            chessbaseData(file), options, progress, cancel,
            [&](std::vector<ImportedGame>&& batch) {
                ++batches;
                CHECK(batch.size() <= 10);
                for (auto& g : batch) got.push_back(std::move(g));
            },
            &left_out);
        CHECK(report.error.empty());
        CHECK(report.games == 35);
        CHECK(report.damaged == 0);
        CHECK_FALSE(report.cancelled);
        CHECK(batches == 4);
        CHECK(progress.games == 35);
        CHECK(progress.bytes == progress.total_bytes);
        CHECK(left_out.games == 35);
        REQUIRE(got.size() == 35);
        const bool code_page = std::string(file).find("cbtest1") != std::string::npos;
        for (std::size_t i = 0; i < 35; ++i) {
            const int n = static_cast<int>(i) + 1;
            INFO("game " << n);
            auto got_tags  = tagMap(got[i].tags);
            auto want_tags = tagMap(want[i].tags);
            asKept(got_tags, want_tags, n, code_page);
            got_tags.erase("SetUp"), want_tags.erase("SetUp");
            CHECK(got_tags == want_tags);
            CHECK(got[i].start_fen == want[i].start_fen);
            CHECK(got[i].moves == want[i].moves);
            CHECK(got[i].plies == want[i].plies);
            CHECK(got[i].result == want[i].result);
            CHECK(got[i].signature == want[i].signature);
            CHECK(got[i].evals == want[i].evals);
            CHECK(got[i].annotated == want[i].annotated);
            CHECK(got[i].damaged == false);
            // Game 1's clocks are not in the databases.
            if (n == 1)
                CHECK(got[i].clocks.empty());
            else
                CHECK(got[i].clocks == want[i].clocks);
        }
        // An annotated game's text, read back, is the database's game.
        REQUIRE(got[0].annotated);
        const auto read = readPgnGame(got[0].text);
        REQUIRE(read.tree);
        ChessBaseDatabase db(chessbaseData(file));
        checkTree(*read.tree, db.read(1)->tree);
        CHECK(importedPgn(got[1]).find("50. Kxf1") != std::string::npos);
    }
    // Cancelled before the first batch, after the first, and a database that cannot be opened.
    std::atomic<bool> cancel{true};
    ImportProgress progress;
    ImportOptions options;
    options.piece_bytes = 10 * 1024;
    std::size_t given   = 0;
    auto report         = importChessBase(chessbaseData("cbtest1.cbh"), options, progress, cancel,
                                          [&](std::vector<ImportedGame>&& b) { given += b.size(); });
    CHECK(report.cancelled);
    CHECK(given == 0);
    cancel = false;
    report =
        importChessBase(chessbaseData("cbtest1.cbh"), options, progress, cancel, [&](std::vector<ImportedGame>&& b) {
            given += b.size();
            cancel = true;
        });
    CHECK(report.cancelled);
    CHECK(given == 10);
    report = importChessBase(chessbaseData("none.cbh"), options, progress, cancel, [](std::vector<ImportedGame>&&) {});
    CHECK(report.error.find("no such file") != std::string::npos);
}

TEST_CASE("ChessBase databases: the time to read a game") {
    for (const char* file : {"cbtest1.cbh", "cbtest2.2cbh", "cbtest1.cbv"}) {
        ChessBaseDatabase db(chessbaseData(file));
        const auto start  = std::chrono::steady_clock::now();
        std::size_t games = 0, plies = 0;
        constexpr int ROUNDS = 20;
        for (int round = 0; round < ROUNDS; ++round)
            db.forEach([&](ChessBaseGame&& g) {
                ++games;
                plies += g.tree.main.moves.size();
                return true;
            });
        const double us =
            std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count() / games;
        MESSAGE(std::string(file) << ": " << us << " us a game (" << plies / games << " plies a game on average)");
        CHECK(games == 35 * ROUNDS);
    }
}

TEST_CASE("ChessBase databases: copied into a database, then again: only new games come in") {
    CHECK(isChessBase("Games.2cbh"));
    CHECK(isChessBase("old.CBH"));
    CHECK(isChessBase("archive.cbv"));
    CHECK_FALSE(isChessBase("games.pgn"));
    const auto dir = std::filesystem::temp_directory_path() / "chessbase-into";
    std::filesystem::remove_all(dir);
    auto db = Database::create(dir / "Copy.fcgdb");
    for (const char* name : {"cbtest2.2cbh", "cbtest1.cbh"}) {
        CAPTURE(name);
        ImportProgress progress;
        std::atomic<bool> cancel{false};
        const auto done = importInto(*db, data(std::string("chessbase/") + name), {}, progress, cancel);
        CHECK(done.error.empty());
        REQUIRE(done.chessbase.has_value());
        CHECK(done.chessbase->games == 35);
        // The first copy brings every game; the second, the same games in the other format, brings none but the one
        // whose name the older format wrote otherwise ("Dvořák" in a Windows code page).
        if (std::string(name) == "cbtest2.2cbh") {
            CHECK(done.added == 35);
        } else {
            CHECK(done.added + done.duplicates == 35);
            CHECK(done.added <= 1);
        }
    }
    CHECK(db->snapshot()->games() >= 35);
}

TEST_CASE("ChessBase 2CBH records: a null move, refused in check") {
    const std::vector<TwoCbhRecord> records = {{1, {1, MOVES, E2E4, PASS, D2D4, END}, {}},
                                               {1, {1, MOVES, E2E4, F7F6, D1H5, PASS, END}, {}}};
    ChessBaseDatabase db(twoCbhDatabase("chessbase-2cbh-null", records));
    const auto passes = db.read(1);
    REQUIRE(passes);
    CHECK_FALSE(passes->damaged);
    REQUIRE(passes->tree.main.moves.size() == 3);
    CHECK(passes->tree.main.moves[1].san == "--");
    CHECK(passes->tree.main.moves[2].san == "d4");
    // Black in check after 1. e4 f6 2. Qh5+ cannot pass: the moves before are kept.
    const auto in_check = db.read(2);
    REQUIRE(in_check);
    CHECK(in_check->damaged);
    CHECK(in_check->problem.find("a null move in check") != std::string::npos);
    CHECK(in_check->tree.main.moves.size() == 3);
}

TEST_CASE("ChessBase 2CBH records: texts, deleted games and records of no kind, counted apart from the games") {
    const std::vector<TwoCbhRecord> records = {{3, {1, MOVES, END}, {}},
                                               {1, {1, MOVES, E2E4, END}, {}},
                                               {0x81, {1, MOVES, E2E4, END}, {}},
                                               {0, {1, MOVES, END}, {}}};
    ChessBaseDatabase db(twoCbhDatabase("chessbase-2cbh-kinds", records));
    int games = 0;
    db.forEach([&](ChessBaseGame&&) {
        ++games;
        return true;
    });
    CHECK(games == 1);
    CHECK(db.report().texts == 1);
    CHECK(db.report().deleted == 1);
    CHECK(db.report().unknown == 1);
    CHECK_FALSE(db.read(1));
}

TEST_CASE("ChessBase 2CBH records: a set-up position's side to move and castling rights") {
    // Black to move, White's king's side and Black's queen's side: 0x06 in the high byte, 1 in the low.
    std::vector<uint16_t> start = {1, SETUP, 20, 0x0601, 0};
    start.insert(start.end(),
                 {WHITE_KING_E1, WHITE_ROOK_A1, WHITE_ROOK_H1, BLACK_KING_E8, BLACK_ROOK_A8, BLACK_ROOK_H8, MOVES});
    auto castles = start;
    castles.insert(castles.end(), {BLACK_LONG, WHITE_SHORT, END});
    auto none = start;
    none.push_back(END);
    ChessBaseDatabase db(twoCbhDatabase("chessbase-2cbh-setup", {{1, none, {}}, {1, castles, {}}}));
    const auto still = db.read(1);
    REQUIRE(still);
    CHECK(still->tree.main.start_fen == "r3k2r/8/8/8/8/8/8/R3K2R b Kq - 0 20");
    const auto castled = db.read(2);
    REQUIRE(castled);
    CHECK_FALSE(castled->damaged);
    REQUIRE(castled->tree.main.moves.size() == 2);
    CHECK(castled->tree.main.moves[0].san == "O-O-O");
    CHECK(castled->tree.main.moves[1].san == "O-O");
}

TEST_CASE("ChessBase 2CBH records: a Chess960 game, its start by its number, its castling that start's") {
    // Start 292 is QBBNRKRN: the king on f1 between rooks on e1 and g1, either side may castle short at once. Its
    // castling words are the 294th four (the standard start's first): b5bd to b5c0.
    const uint16_t white_short = 0xB129 + 4 * 293 + 1, black_short = 0xB129 + 4 * 293 + 3;
    const std::vector<TwoCbhRecord> records = {{1, {2, SETUP, 292, MOVES, white_short, black_short, END}, {}},
                                               {1, {2, SETUP, 292, MOVES, WHITE_SHORT, END}, {}},
                                               {1, {2, SETUP, 960, MOVES, END}, {}}};
    ChessBaseDatabase db(twoCbhDatabase("chessbase-2cbh-960", records));
    const auto game = db.read(1);
    REQUIRE(game);
    CHECK(game->chess960);
    CHECK_FALSE(game->damaged);
    CHECK(game->tree.main.header("Variant") == "Chess960");
    CHECK(game->tree.main.start_fen == "qbbnrkrn/pppppppp/8/8/8/8/PPPPPPPP/QBBNRKRN w GEge - 0 1");
    REQUIRE(game->tree.main.moves.size() == 2);
    CHECK(game->tree.main.moves[0].san == "O-O");
    CHECK(game->tree.main.moves[1].san == "O-O");
    // The standard start's castling in a Chess960 game, a start numbered 960: damaged.
    const auto other = db.read(2);
    REQUIRE(other);
    CHECK(other->damaged);
    CHECK(other->problem.find("the castling of another start") != std::string::npos);
    const auto past = db.read(3);
    REQUIRE(past);
    CHECK(past->damaged);
    CHECK(past->problem.find("its Chess960 start is not one") != std::string::npos);
    // Every start, its four castling rights those of its rooks.
    std::vector<TwoCbhRecord> starts;
    for (uint16_t number = 0; number < 960; ++number) starts.push_back({1, {2, SETUP, number, MOVES, END}, {}});
    ChessBaseDatabase all(twoCbhDatabase("chessbase-2cbh-960-all", starts));
    int read = 0;
    all.forEach([&](ChessBaseGame&& g) {
        CHECK_FALSE(g.damaged);
        chess::Board board;
        board.set960(true);
        board.setFen(g.tree.main.start_fen);
        using Side = chess::Board::CastlingRights::Side;
        for (const auto colour : {chess::Color::WHITE, chess::Color::BLACK})
            for (const auto side : {Side::KING_SIDE, Side::QUEEN_SIDE}) CHECK(board.castlingRights().has(colour, side));
        ++read;
        return true;
    });
    CHECK(read == 960);
    CHECK(all.read(519)->tree.main.start_fen == "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w HAha - 0 1");
}

TEST_CASE("ChessBase 2CBH records: annotations of every type, those after each still read") {
    TwoCbhNotes notes;
    // The game's clocks and medals.
    notes.at(-1, {{0x16, le32Of(306700)}, {0x17, le32Of(228200)}, {0x22, le32Of(0x1008)}});
    // A training question of a move to find, a game quoted, a pawn structure, a piece's path.
    notes.at(
        0,
        {{0x09, trainingMove()}, {0x13, quotation(false, 0)}, {0x14, "\x01"}, {0x15, sized("path")}, comment("one")});
    // A web link, a video and a time in it, a variation's colour, a type not named, a game quoted with its start
    // position, a choice, a critical position.
    notes.at(1, {{0x1C, "\x01" + sized("https://example.org") + sized("a page")},
                 {0x20, std::string(4, '\0') + sized("video")},
                 {0x25, le32Of(1000)},
                 {0x23, le32Of(2)},
                 {0x08, le32Of(0)},
                 {0x13, quotation(true, 2)},
                 {0x09, trainingChoice()},
                 {0x18, "\x02"},
                 comment("two")});
    ChessBaseDatabase db(twoCbhDatabase("chessbase-2cbh-notes", {{1, {1, MOVES, E2E4, E7E5, END}, notes.done()}}));
    const auto game = db.read(1);
    REQUIRE(game);
    CHECK_FALSE(game->damaged);
    const auto& main = game->tree.main;
    CHECK(main.header("WhiteClock") == "0:05:06.700");
    CHECK(main.header("BlackClock") == "0:03:48.200");
    CHECK(main.header("Medals") == "4104");
    REQUIRE(main.moves.size() == 2);
    CHECK(main.moves[0].notes.after == "one");
    CHECK(main.moves[1].notes.after.find("two") != std::string::npos);
    CHECK(main.moves[1].notes.after.find("Critical position") != std::string::npos);
    const auto& report = db.report();
    CHECK(report.training == 2);
    CHECK(report.quotations == 2);
    CHECK(report.pawn_structures == 1);
    CHECK(report.piece_paths == 1);
    CHECK(report.web_links == 1);
    CHECK(report.videos == 2);
    CHECK(report.variation_colours == 1);
    CHECK(report.other == 1);
    CHECK(report.undecoded == 0);
    CHECK(report.damaged == 0);
}
