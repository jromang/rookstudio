#include "vision.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

#include <json.hpp>

#include "i18n.hpp"
#include "storage.hpp"

namespace fcg {

namespace fs = std::filesystem;

namespace {

const std::vector<HeadOpening> OPENINGS = {
    {N_("Ruy Lopez"), "e4 e5 Nf3 Nc6 Bb5 a6 Ba4 Nf6 O-O Be7 Re1 b5 Bb3 d6 c3 O-O h3 Nb8 d4 Nbd7"},
    {N_("Sicilian, Najdorf"), "e4 c5 Nf3 d6 d4 cxd4 Nxd4 Nf6 Nc3 a6 Be3 e5 Nb3 Be6 f3 Be7 Qd2 O-O O-O-O Nbd7"},
    {N_("French, Winawer"), "e4 e6 d4 d5 Nc3 Bb4 e5 c5 a3 Bxc3+ bxc3 Ne7 Qg4 Qc7 Qxg7 Rg8 Qxh7 cxd4 Ne2 Nbc6"},
    {N_("Caro-Kann"), "e4 c6 d4 d5 Nc3 dxe4 Nxe4 Bf5 Ng3 Bg6 h4 h6 Nf3 Nd7 h5 Bh7 Bd3 Bxd3 Qxd3 e6"},
    {N_("Queen's Gambit Declined"), "d4 d5 c4 e6 Nc3 Nf6 Bg5 Be7 e3 O-O Nf3 h6 Bh4 b6 cxd5 Nxd5 Bxe7 Qxe7 Nxd5 exd5"},
    {N_("King's Indian"), "d4 Nf6 c4 g6 Nc3 Bg7 e4 d6 Nf3 O-O Be2 e5 O-O Nc6 d5 Ne7 Ne1 Nd7 Nd3 f5"},
    {N_("Nimzo-Indian"), "d4 Nf6 c4 e6 Nc3 Bb4 Qc2 O-O a3 Bxc3+ Qxc3 b6 Bg5 Bb7 f3 h6 Bh4 d5 e3 Nbd7"},
    {N_("Italian Game"), "e4 e5 Nf3 Nc6 Bc4 Bc5 c3 Nf6 d3 d6 O-O O-O Re1 a6 Bb3 Ba7 h3 h6 Nbd2 Re8"},
    {N_("Scotch"), "e4 e5 Nf3 Nc6 d4 exd4 Nxd4 Nf6 Nxc6 bxc6 e5 Qe7 Qe2 Nd5 c4 Ba6 b3 g6 f4 f6"},
    {N_("Slav"), "d4 d5 c4 c6 Nf3 Nf6 Nc3 dxc4 a4 Bf5 e3 e6 Bxc4 Bb4 O-O Nbd7 Qe2 Bg6 e4 O-O"},
    {N_("Grünfeld"), "d4 Nf6 c4 g6 Nc3 d5 cxd5 Nxd5 e4 Nxc3 bxc3 Bg7 Nf3 c5 Be3 Qa5 Qd2 O-O Rc1 cxd4"},
    {N_("English Opening"), "c4 e5 Nc3 Nf6 Nf3 Nc6 g3 d5 cxd5 Nxd5 Bg2 Nb6 O-O Be7 d3 O-O a3 Be6 b4 f6"},
    {N_("Catalan Opening"), "d4 Nf6 c4 e6 g3 d5 Bg2 Be7 Nf3 O-O O-O dxc4 Qc2 a6 Qxc4 b5 Qc2 Bb7 Bd2 Nc6"},
    {N_("Sicilian, Dragon"), "e4 c5 Nf3 d6 d4 cxd4 Nxd4 Nf6 Nc3 g6 Be3 Bg7 f3 O-O Qd2 Nc6 Bc4 Bd7 O-O-O Rc8"},
    {N_("Petroff"), "e4 e5 Nf3 Nf6 Nxe5 d6 Nf3 Nxe4 d4 d5 Bd3 Nc6 O-O Be7 c4 Nb4 Be2 O-O Nc3 Bf5"},
    {N_("London System"), "d4 d5 Bf4 Nf6 e3 c5 c3 Nc6 Nd2 e6 Ngf3 Bd6 Bg3 O-O Bd3 b6 Ne5 Bb7 f4 Ne7"},
};

// A side's pieces of a kind.
chess::Bitboard piecesOf(const chess::Board& board, chess::Color color, chess::PieceType type) {
    return board.pieces(type, color);
}

bool lightSquare(chess::Square sq) { return (sq.file() + sq.rank()) % 2 == 1; }

// A move in SAN, NO_MOVE when it is not one here.
chess::Move sanMove(const chess::Board& board, const std::string& san) {
    try {
        return chess::uci::parseSan(board, san);
    } catch (const std::exception&) {
        return chess::Move::NO_MOVE;
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// The coordinates

std::string coordKey(CoordMode mode, CoordSide side) {
    const char* m = mode == CoordMode::Find ? "find" : "name";
    const char* s = side == CoordSide::White ? "white" : side == CoordSide::Black ? "black" : "random";
    return std::string(m) + "-" + s;
}

chess::Square nextCoordinate(std::mt19937& rng, std::optional<chess::Square> last) {
    std::uniform_int_distribution<int> any(0, 63);
    for (;;) {
        const chess::Square sq(any(rng));
        if (!last || sq != *last) return sq;
    }
}

std::optional<chess::Square> typedSquare(std::string_view text) {
    if (text.size() != 2) return std::nullopt;
    const char f = static_cast<char>(std::tolower(static_cast<unsigned char>(text[0])));
    const char r = text[1];
    if (f < 'a' || f > 'h' || r < '1' || r > '8') return std::nullopt;
    return chess::Square(chess::File(f - 'a'), chess::Rank(r - '1'));
}

std::string squareName(chess::Square square) {
    if (square == chess::Square::NO_SQ) return {};
    return {static_cast<char>('a' + static_cast<int>(square.file())),
            static_cast<char>('1' + static_cast<int>(square.rank()))};
}

// ---------------------------------------------------------------------------
// Blindfold

std::array<chess::Piece, 64> shownPieces(const chess::Board& board, Hidden hidden, bool peek) {
    std::array<chess::Piece, 64> out;
    out.fill(chess::Piece::NONE);
    for (int i = 0; i < 64; ++i) {
        const auto piece = board.at(chess::Square(i));
        if (piece == chess::Piece::NONE) continue;
        bool shown = peek;
        if (!shown) {
            switch (hidden) {
                case Hidden::All:
                    break;
                case Hidden::AfterTen:
                    shown = board.fullMoveNumber() <= 10;
                    break;
                case Hidden::AllButPawns:
                    shown = piece.type() == chess::PieceType::PAWN;
                    break;
            }
        }
        if (shown) out[static_cast<std::size_t>(i)] = piece;
    }
    return out;
}

std::optional<PieceQuestion> pieceQuestion(const chess::Board& board, std::mt19937& rng) {
    struct Name {
        chess::Color color;
        chess::PieceType type;
        const char* text;
    };
    static const Name NAMES[] = {
        {chess::Color::WHITE, chess::PieceType::QUEEN, N_("the white queen")},
        {chess::Color::BLACK, chess::PieceType::QUEEN, N_("the black queen")},
        {chess::Color::WHITE, chess::PieceType::ROOK, N_("the white rook")},
        {chess::Color::BLACK, chess::PieceType::ROOK, N_("the black rook")},
        {chess::Color::WHITE, chess::PieceType::KNIGHT, N_("the white knight")},
        {chess::Color::BLACK, chess::PieceType::KNIGHT, N_("the black knight")},
        {chess::Color::WHITE, chess::PieceType::BISHOP, N_("the white bishop")},
        {chess::Color::BLACK, chess::PieceType::BISHOP, N_("the black bishop")},
    };
    std::vector<PieceQuestion> candidates;
    for (const auto& n : NAMES) {
        const auto set = piecesOf(board, n.color, n.type);
        if (set.count() == 1) candidates.push_back({n.text, chess::Square(set.lsb())});
    }
    // Two bishops of a side, on squares of both colours: each by its squares' colour.
    for (const auto color : {chess::Color::WHITE, chess::Color::BLACK}) {
        const auto set = piecesOf(board, color, chess::PieceType::BISHOP);
        if (set.count() != 2) continue;
        const chess::Square a(set.lsb()), b(set.msb());
        if (lightSquare(a) == lightSquare(b)) continue;
        const bool white = color == chess::Color::WHITE;
        for (const auto sq : {a, b}) {
            const char* text =
                lightSquare(sq) ? (white ? N_("the white light-squared bishop") : N_("the black light-squared bishop"))
                                : (white ? N_("the white dark-squared bishop") : N_("the black dark-squared bishop"));
            candidates.push_back({text, sq});
        }
    }
    if (candidates.empty()) {  // the kings are always there
        for (const auto color : {chess::Color::WHITE, chess::Color::BLACK})
            candidates.push_back(
                {color == chess::Color::WHITE ? N_("the white king") : N_("the black king"), board.kingSq(color)});
    }
    std::uniform_int_distribution<std::size_t> pick(0, candidates.size() - 1);
    return candidates[pick(rng)];
}

// ---------------------------------------------------------------------------
// A line read in one's head

const std::vector<HeadOpening>& headOpenings() { return OPENINGS; }

HeadLine headLine(std::mt19937& rng) {
    std::uniform_int_distribution<std::size_t> which(0, OPENINGS.size() - 1);
    for (int attempt = 0; attempt < 64; ++attempt) {
        const auto& o = OPENINGS[which(rng)];
        std::vector<std::string> sans;
        {
            std::istringstream words(o.moves);
            std::string w;
            while (words >> w) sans.push_back(w);
        }
        if (static_cast<int>(sans.size()) < 4 + HEAD_MOVES) continue;
        // Shown after two moves of each side at least, so that the position is one of the opening.
        std::uniform_int_distribution<int> from(4, static_cast<int>(sans.size()) - HEAD_MOVES);
        const int start = from(rng);
        chess::Board board;
        bool legal = true;
        for (int i = 0; i < start && legal; ++i) {
            const auto m = sanMove(board, sans[static_cast<std::size_t>(i)]);
            if (m == chess::Move::NO_MOVE)
                legal = false;
            else
                board.makeMove(m);
        }
        if (!legal) continue;
        HeadLine line;
        line.opening = o.name;
        line.fen     = board.getFen();
        auto end     = board;
        std::vector<chess::Square> touched;
        for (int i = start; i < start + HEAD_MOVES && legal; ++i) {
            const auto m = sanMove(end, sans[static_cast<std::size_t>(i)]);
            if (m == chess::Move::NO_MOVE) {
                legal = false;
                break;
            }
            line.moves.push_back(m);
            touched.push_back(m.from());
            touched.push_back(
                m.typeOf() == chess::Move::CASTLING
                    ? chess::Square(m.to().index() > m.from().index() ? chess::File::FILE_G : chess::File::FILE_C,
                                    m.from().rank())
                    : m.to());
            end.makeMove(m);
        }
        if (!legal) continue;
        // A square whose piece the moves changed, the later moves' squares first.
        std::vector<chess::Square> changed;
        for (auto it = touched.rbegin(); it != touched.rend(); ++it)
            if (board.at(*it) != end.at(*it) && std::find(changed.begin(), changed.end(), *it) == changed.end())
                changed.push_back(*it);
        if (changed.empty()) continue;
        std::uniform_int_distribution<std::size_t> pick(0, std::min<std::size_t>(changed.size(), 4) - 1);
        line.square = changed[pick(rng)];
        line.answer = end.at(line.square);
        return line;
    }
    return {};
}

std::optional<chess::Piece> typedPiece(char key) {
    if (key == '-' || key == '0') return chess::Piece::NONE;
    const bool white = std::isupper(static_cast<unsigned char>(key));
    const auto color = white ? chess::Color::WHITE : chess::Color::BLACK;
    switch (std::tolower(static_cast<unsigned char>(key))) {
        case 'k':
            return chess::Piece(chess::PieceType::KING, color);
        case 'q':
            return chess::Piece(chess::PieceType::QUEEN, color);
        case 'r':
            return chess::Piece(chess::PieceType::ROOK, color);
        case 'b':
            return chess::Piece(chess::PieceType::BISHOP, color);
        case 'n':
            return chess::Piece(chess::PieceType::KNIGHT, color);
        case 'p':
            return chess::Piece(chess::PieceType::PAWN, color);
        default:
            break;
    }
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// The records

VisionRecord readVisionRecord(const fs::path& profile) {
    VisionRecord out;
    std::ifstream in(profile / "vision.json", std::ios::binary);
    if (!in) return out;
    const auto j = nlohmann::json::parse(in, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return out;
    if (const auto it = j.find("coordinates"); it != j.end() && it->is_object())
        for (const auto& [key, v] : it->items())
            if (v.is_number_integer()) out.coordinates[key] = v.get<int>();
    out.lines       = j.value("lines", 0);
    out.lines_right = j.value("lines_right", 0);
    out.blind       = j.value("blind", 0);
    out.where       = j.value("where", 0);
    out.where_right = j.value("where_right", 0);
    return out;
}

bool writeVisionRecord(const fs::path& profile, const VisionRecord& r) {
    std::error_code ec;
    fs::create_directories(profile, ec);
    return writeJson(profile / "vision.json", {{"version", 1},
                                               {"coordinates", r.coordinates},
                                               {"lines", r.lines},
                                               {"lines_right", r.lines_right},
                                               {"blind", r.blind},
                                               {"where", r.where},
                                               {"where_right", r.where_right}});
}

}  // namespace fcg
