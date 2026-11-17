// The board and the position against shakmaty 0.30.2, the prototype's: positions of the reference games and positions
// made to try the rules (castling, en passant, checks, promotions, the ends of a game), with every legal move in
// shakmaty's order, its UCI, SAN, debug text and the FEN after it.
//
// shakmaty-positions.jsonl.zst comes from a small Rust program printing what shakmaty 0.30.2 says of each FEN: see
// commentary/tests/data/README.md.

#include <doctest/doctest.hpp>

#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include <json.hpp>

#include <commentary/position.hpp>
#include <study/sources.hpp>

using namespace fcg::commentary;
namespace fs = std::filesystem;

namespace {

fs::path data(const std::string& name) {
    const char* env = std::getenv("FCG_SOURCE_DIR");
    return fs::path(env ? env : FCG_SOURCE_DIR) / "commentary" / "tests" / "data" / name;
}

std::vector<nlohmann::json> readLines(const fs::path& file) {
    std::string text;
    std::vector<char> buffer(1 << 16);
    for (auto& source : fcg::study::openSources(file)) {
        while (const auto n = source->read(buffer.data(), buffer.size())) text.append(buffer.data(), n);
    }
    std::vector<nlohmann::json> out;
    for (std::size_t at = 0; at < text.size();) {
        const auto end = std::min(text.find('\n', at), text.size());
        if (end > at) out.push_back(nlohmann::json::parse(text.substr(at, end - at)));
        at = end + 1;
    }
    return out;
}

}  // namespace

TEST_CASE("position: legal moves in shakmaty's order, with its UCI, SAN, debug text and FEN") {
    const auto records = readLines(data("shakmaty-positions.jsonl.zst"));
    REQUIRE(records.size() > 400);
    std::size_t moves = 0, wrong = 0;
    for (const auto& r : records) {
        const auto fen = r.at("fen").get<std::string>();
        CAPTURE(fen);
        std::string error;
        const auto pos = Position::fromFen(fen, &error);
        REQUIRE_MESSAGE(pos, error);
        CHECK(pos->fen() == r.at("legal_fen").get<std::string>());
        CHECK(pos->isCheck() == r.at("check").get<bool>());
        CHECK(pos->isCheckmate() == r.at("checkmate").get<bool>());
        CHECK(pos->isStalemate() == r.at("stalemate").get<bool>());
        CHECK(pos->isInsufficientMaterial() == r.at("insufficient").get<bool>());
        const auto legal = pos->legalMoves();
        const auto& want = r.at("moves");
        REQUIRE(legal.size() == want.size());
        for (std::size_t i = 0; i < legal.size(); ++i) {
            const auto& w = want[i];
            const auto& m = legal[i];
            ++moves;
            auto after = *pos;
            after.playUnchecked(m);
            const bool same =
                uci(m) == w.at("uci").get<std::string>() && pos->sanPlus(m) == w.at("san").get<std::string>() &&
                debug(m) == w.at("debug").get<std::string>() && after.fen() == w.at("after").get<std::string>() &&
                pos->parseUci(w.at("uci").get<std::string>()) == m;
            if (!same && ++wrong <= 5) {
                CHECK_MESSAGE(false, "move " << i << ": ours " << uci(m) << " " << pos->sanPlus(m) << " " << debug(m)
                                             << " -> " << after.fen() << "; shakmaty's " << w.dump());
            }
        }
    }
    MESSAGE(records.size() << " positions, " << moves << " moves");
    CHECK(wrong == 0);
}

TEST_CASE("position: a FEN that is no legal position is refused") {
    std::string why;
    CHECK_FALSE(Position::fromFen("8/8/8/8/8/8/8/8 w - - 0 1", &why));
    CHECK(why == "not one king each");
    CHECK_FALSE(Position::fromFen("4k3/8/8/8/8/8/8/4K2P w - - 0 1", &why));
    CHECK_FALSE(Position::fromFen("4k3/8/8/8/8/8/8/R3K3 w K - 0 1", &why));   // no rook on h1
    CHECK_FALSE(Position::fromFen("4k3/4R3/8/8/8/8/8/4K3 w - - 0 1", &why));  // Black in check, White to move
    CHECK_FALSE(Position::fromFen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq e3 0 1", &why));
    CHECK(Position::fromFen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1")->fen() ==
          Position::initial().fen());
}
