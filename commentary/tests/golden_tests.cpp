// The port held to the Rust prototype on its held-out cases: for each move, what its briefs list (the position, the
// move in each language's notation, the facts the move achieves, what punishes it, what the better move would have
// done, the template comment) must come out the same. See data/README.md for how the reference files were made.
//
// FCG_GOLDEN_CASES and FCG_GOLDEN_BRIEFS point the tests at a larger corpus kept out of the repository, made the same
// way: a local check of the branches the held-out cases do not reach.

#include <doctest/doctest.hpp>

#include <cstdlib>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

#include <json.hpp>

#include <commentary/case.hpp>
#include <commentary/comment.hpp>
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

template <typename Each>
void eachLine(const fs::path& file, Each each) {
    std::string text;
    std::vector<char> buffer(1 << 16);
    for (auto& source : fcg::study::openSources(file)) {
        while (const auto n = source->read(buffer.data(), buffer.size())) text.append(buffer.data(), n);
    }
    for (std::size_t at = 0; at < text.size();) {
        const auto end = std::min(text.find('\n', at), text.size());
        if (end > at) each(nlohmann::json::parse(text.substr(at, end - at)));
        at = end + 1;
    }
}

// What a brief of the prototype says about a move, in one language.
struct Brief {
    std::string fen, played, prompt;
    std::optional<std::string> templ;
    std::vector<std::string> achieves, punished_by, missed;
};

struct Corpus {
    bool held_out = true;  // the corpus in the repository
    std::vector<Case> cases;
    std::map<std::pair<std::string, std::string>, Brief> briefs;  // by case id and language code

    const Brief& brief(const std::string& id, const std::string& lang) const {
        const auto it = briefs.find({id, lang});
        REQUIRE_MESSAGE(it != briefs.end(), "no brief for " << id << " (" << lang << ")");
        return it->second;
    }
};

const Corpus& goldenCorpus() {
    static const Corpus c = [] {
        Corpus out;
        const char* cases  = std::getenv("FCG_GOLDEN_CASES");
        const char* briefs = std::getenv("FCG_GOLDEN_BRIEFS");
        out.held_out       = !(cases && briefs);
        eachLine(out.held_out ? data("cases-heldout.jsonl.zst") : fs::path(cases),
                 [&](const nlohmann::json& j) { out.cases.push_back(Case::fromJson(j)); });
        eachLine(out.held_out ? data("briefs-heldout.jsonl.zst") : fs::path(briefs), [&](const nlohmann::json& j) {
            Brief b;
            b.fen         = j.at("fen").get<std::string>();
            b.played      = j.at("played").get<std::string>();
            b.prompt      = j.at("prompt").get<std::string>();
            b.achieves    = j.at("achieves").get<std::vector<std::string>>();
            b.punished_by = j.at("punished_by").get<std::vector<std::string>>();
            b.missed      = j.at("missed").get<std::vector<std::string>>();
            if (j.contains("template")) b.templ = j.at("template").get<std::string>();
            out.briefs[{j.at("id").get<std::string>(), j.at("lang").get<std::string>()}] = std::move(b);
        });
        return out;
    }();
    return c;
}

constexpr std::tuple<Lang, const char*, PieceLetters> LANGUAGES[] = {{Lang::En, "en", PieceLetters::English},
                                                                     {Lang::Fr, "fr", PieceLetters::French}};

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
    const auto& corpus = goldenCorpus();
    if (corpus.held_out) REQUIRE(corpus.cases.size() == 294);

    int compared = 0, wrong = 0;
    std::size_t facts_compared = 0;
    std::map<std::string, int> wrong_fields;
    for (const auto& c : corpus.cases) {
        CAPTURE(c.id);
        std::string error;
        const auto replayed = replay(c, &error);
        REQUIRE_MESSAGE(replayed, error);
        const auto ctx   = replayed->context();
        const auto facts = gather(ctx);
        for (const auto& [lang, code, letters] : LANGUAGES) {
            const auto& b = corpus.brief(c.id, code);
            ++compared;
            std::vector<std::string> differ;
            if (replayed->before.fen() != b.fen) differ.push_back("fen");
            if (moveText(replayed->before, replayed->played, letters) != b.played) differ.push_back("played");
            const std::tuple<const char*, const std::vector<Fact>*, const std::vector<std::string>*> lists[] = {
                {"achieves", &facts.achieves, &b.achieves},
                {"punished_by", &facts.punished_by, &b.punished_by},
                {"missed", &facts.missed, &b.missed}};
            for (const auto& [field, ours, theirs] : lists) {
                facts_compared += theirs->size();
                if (said(*ours) != *theirs) differ.push_back(field);
            }
            if (differ.empty()) continue;
            ++wrong;
            for (const auto& d : differ) ++wrong_fields[d];
            if (wrong <= 4) {
                std::string detail = c.id + " (" + code + "), " +
                                     moveText(replayed->before, replayed->played, letters) + " in " +
                                     replayed->before.fen() + ":";
                for (const auto& [field, ours, theirs] : lists) {
                    detail += std::string("\n  ") + field + ", ours:" + joined(said(*ours)) + "\n  " + field +
                              ", the prototype's:" + joined(*theirs);
                }
                FAIL_CHECK(detail);
            }
        }
    }
    std::string summary;
    for (const auto& [field, n] : wrong_fields) summary += " " + field + " " + std::to_string(n);
    MESSAGE(compared << " briefs compared, " << facts_compared << " facts in them, " << wrong << " differ:" << summary);
    if (corpus.held_out) CHECK(facts_compared > 1000);
    CHECK(wrong == 0);
}

TEST_CASE("golden: the comments the prototype's templates write on its held-out cases") {
    const auto& corpus = goldenCorpus();
    std::map<std::string, std::pair<int, int>> counts;  // per language: compared, wrong
    for (const auto& c : corpus.cases) {
        const auto replayed = replay(c);
        REQUIRE(replayed);
        const auto ctx = replayed->context();
        for (const auto& [lang, code, letters] : LANGUAGES) {
            const auto& b           = corpus.brief(c.id, code);
            const auto ours         = comment(ctx, lang, letters);
            auto& [compared, wrong] = counts[code];
            ++compared;
            if (ours == b.templ) continue;
            if (++wrong <= 6)
                FAIL_CHECK(c.id << " (" << code << "), " << moveText(replayed->before, replayed->played, letters)
                                << ":\n  ours:            " << ours.value_or("(none)")
                                << "\n  the prototype's: " << b.templ.value_or("(none)"));
        }
    }
    for (const auto& [code, n] : counts)
        MESSAGE(code << ": " << n.first << " comments compared, " << n.second << " differ");
    CHECK(counts["en"].second == 0);
    CHECK(counts["fr"].second == 0);
}
