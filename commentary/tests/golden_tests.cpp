// The port held to the Rust prototype on its held-out cases: for each move, what its briefs list (the position, the
// move in each language's notation, the facts the move achieves, what punishes it, what the better move would have
// done) must come out the same. See data/README.md for how the reference files were made.

#include <doctest/doctest.hpp>

#include <cstdlib>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include <json.hpp>

#include <commentary/case.hpp>
#include <commentary/gather.hpp>
#include <commentary/notation.hpp>
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

std::vector<std::string> said(const std::vector<Fact>& facts) {
    std::vector<std::string> out;
    for (const auto& f : facts) out.push_back(debug(f));
    return out;
}

std::string joined(const std::vector<std::string>& lines) {
    std::string out;
    for (const auto& l : lines) out += "\n    " + l;
    return out.empty() ? " (none)" : out;
}

}  // namespace

TEST_CASE("golden: the facts of the prototype's held-out cases, as its briefs list them") {
    const auto cases  = readLines(data("cases-heldout.jsonl.zst"));
    const auto briefs = readLines(data("briefs-heldout.jsonl.zst"));
    REQUIRE(cases.size() == 294);
    std::map<std::pair<std::string, std::string>, const nlohmann::json*> by_case;
    for (const auto& b : briefs) by_case[{b.at("id").get<std::string>(), b.at("lang").get<std::string>()}] = &b;

    int compared = 0, wrong = 0;
    std::size_t facts_compared = 0;
    std::map<std::string, int> wrong_fields;
    for (const auto& j : cases) {
        const auto c = Case::fromJson(j);
        CAPTURE(c.id);
        std::string error;
        const auto replayed = replay(c, &error);
        REQUIRE_MESSAGE(replayed, error);
        const auto ctx   = replayed->context();
        const auto facts = gather(ctx);
        for (const auto& [lang, letters] :
             {std::pair{"en", PieceLetters::English}, std::pair{"fr", PieceLetters::French}}) {
            const auto it = by_case.find({c.id, lang});
            REQUIRE(it != by_case.end());
            const auto& b = *it->second;
            ++compared;
            std::vector<std::string> differ;
            if (replayed->before.fen() != b.at("fen").get<std::string>()) differ.push_back("fen");
            if (moveText(replayed->before, replayed->played, letters) != b.at("played").get<std::string>())
                differ.push_back("played");
            const std::pair<const char*, const std::vector<Fact>*> lists[] = {
                {"achieves", &facts.achieves}, {"punished_by", &facts.punished_by}, {"missed", &facts.missed}};
            for (const auto& [field, list] : lists) {
                facts_compared += b.at(field).size();
                if (said(*list) != b.at(field).get<std::vector<std::string>>()) differ.push_back(field);
            }
            if (differ.empty()) continue;
            ++wrong;
            for (const auto& d : differ) ++wrong_fields[d];
            if (wrong <= 4) {
                std::string detail = c.id + " (" + lang + "), " +
                                     moveText(replayed->before, replayed->played, letters) + " in " +
                                     replayed->before.fen() + ":";
                for (const auto& [field, list] : lists) {
                    detail += std::string("\n  ") + field + ", ours:" + joined(said(*list)) + "\n  " + field +
                              ", the prototype's:" + joined(b.at(field).get<std::vector<std::string>>());
                }
                FAIL_CHECK(detail);
            }
        }
    }
    std::string summary;
    for (const auto& [field, n] : wrong_fields) summary += " " + field + " " + std::to_string(n);
    MESSAGE(compared << " briefs compared, " << facts_compared << " facts in them, " << wrong << " differ:" << summary);
    CHECK(facts_compared > 1000);
    CHECK(wrong == 0);
}
