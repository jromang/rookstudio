// The port held to the Rust prototype on its held-out cases: for each move, what its briefs list (the position, the
// move in each language's notation, the facts the move achieves, what punishes it, what the better move would have
// done, the template comment, the prompt its language model gets) must come out the same. See data/README.md for how
// the reference files were made.
//
// FCG_GOLDEN_CASES, FCG_GOLDEN_BRIEFS and FCG_GOLDEN_REVIEWS point the tests at a larger corpus kept out of the
// repository, made the same way: a local check of the branches the held-out cases do not reach.

#include <doctest/doctest.hpp>

#include <cstdlib>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

#include <json.hpp>

#include <commentary/brief.hpp>
#include <commentary/case.hpp>
#include <commentary/comment.hpp>
#include <commentary/gather.hpp>
#include <commentary/notation.hpp>
#include <commentary/openings.hpp>
#include <commentary/review.hpp>
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
struct Reference {
    std::string fen, played, prompt;
    std::optional<std::string> templ;
    std::vector<std::string> achieves, punished_by, missed;
};

struct Corpus {
    bool held_out = true;  // the corpus in the repository
    std::vector<Case> cases;
    std::map<std::pair<std::string, std::string>, Reference> briefs;  // by case id and language code

    const Reference& brief(const std::string& id, const std::string& lang) const {
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
            Reference b;
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

TEST_CASE("golden: the prompts the prototype gives its language model on its held-out cases") {
    const auto& corpus = goldenCorpus();
    std::map<std::string, std::pair<int, int>> counts;  // per language: compared, wrong
    for (const auto& c : corpus.cases) {
        std::string error;
        // The opening names, as the prototype's tool gives them.
        const auto replayed = replay(c, &error, &openingName);
        REQUIRE_MESSAGE(replayed, error);
        const auto ctx = replayed->context();
        for (const auto& [lang, code, letters] : LANGUAGES) {
            const auto& b           = corpus.brief(c.id, code);
            const auto ours         = Brief(ctx, lang, letters).prompt;
            auto& [compared, wrong] = counts[code];
            ++compared;
            if (ours == b.prompt) continue;
            if (++wrong <= 4)
                FAIL_CHECK(c.id << " (" << code << "), " << moveText(replayed->before, replayed->played, letters)
                                << ":\n-- ours:\n"
                                << ours << "\n-- the prototype's:\n"
                                << b.prompt);
        }
    }
    for (const auto& [code, n] : counts)
        MESSAGE(code << ": " << n.first << " prompts compared, " << n.second << " differ");
    CHECK(counts["en"].second == 0);
    CHECK(counts["fr"].second == 0);
}

TEST_CASE("golden: the review of each case's move, made again from the engine lines it keeps") {
    // The prototype's `rescore`: each case reviewed again by the present rules, those still worth a comment kept.
    const char* env = std::getenv("FCG_GOLDEN_REVIEWS");
    std::map<std::string, nlohmann::json> rescored;
    eachLine(env ? fs::path(env) : data("reviews-heldout.jsonl.zst"),
             [&](const nlohmann::json& j) { rescored[j.at("id").get<std::string>()] = j.at("review"); });
    const auto& corpus = goldenCorpus();
    int compared = 0, wrong = 0, dropped = 0;
    for (const auto& c : corpus.cases) {
        const auto replayed = replay(c);
        REQUIRE(replayed);
        const auto& old = c.review;
        // As the prototype's Case::reviewed_again: the case as it was when its lines do not allow a review.
        auto again          = old;
        auto again_judgment = replayed->report.judgment;
        auto after          = replayed->before;
        after.playUnchecked(replayed->played);
        // How a game ends in a position, when it does: shakmaty's outcome().
        std::optional<GameEnd> end;
        if (after.isCheckmate())
            end = GameEnd::checkmate(!after.turn());
        else if (after.isStalemate() || after.isInsufficientMaterial())
            end = GameEnd::draw();
        std::optional<PositionAnalysis> a_after;
        if (replayed->reply)
            a_after = PositionAnalysis{{*replayed->reply}, std::nullopt};
        else if (end)
            a_after = PositionAnalysis::ended(*end);
        std::optional<Weighed> weighed;
        if (replayed->best && a_after) {
            PositionAnalysis a_before{{*replayed->best}, std::nullopt};
            if (replayed->second) a_before.lines.push_back(*replayed->second);
            weighed = weigh(a_before, *a_after, replayed->before.turn(), replayed->played);
        }
        if (weighed) {
            const bool forced    = takesBackTheOnlyWay(replayed->before, replayed->previous, replayed->played);
            again_judgment       = old.book ? std::nullopt : weighed->judgment;
            const bool only_move = weighed->only && weighed->played_best && !old.book && !forced;
            again.win_before     = weighed->win_before;
            again.win_after      = weighed->win_after;
            again.accuracy       = weighed->accuracy;
            again.cp_loss        = weighed->cp_loss;
            again.played_best    = weighed->played_best;
            again.only_move      = only_move;
            // Without the second line, the lead over it is as the case had it.
            if (c.second) again.only_gap = weighed->only_gap;
            again.key = old.key && (again_judgment || only_move);
        }
        const bool worth = again_judgment || again.only_move || again.key || again.plan || again.novelty;
        const auto it    = rescored.find(c.id);
        if (it == rescored.end()) {
            ++dropped;
            CHECK_MESSAGE(!worth, c.id << ": still worth a comment, the prototype left it out");
            continue;
        }
        const auto& r = it->second;
        std::vector<std::string> differ;
        const auto judgment_name = [](std::optional<Judgment> j) -> std::optional<std::string> {
            if (!j) return std::nullopt;
            return *j == Judgment::Blunder ? "blunder" : *j == Judgment::Mistake ? "mistake" : "inaccuracy";
        };
        const auto theirs_judgment =
            r.contains("judgment") ? std::optional<std::string>(r.at("judgment").get<std::string>()) : std::nullopt;
        if (again.win_before != r.at("win_before").get<double>()) differ.push_back("win_before");
        if (again.win_after != r.at("win_after").get<double>()) differ.push_back("win_after");
        if (again.accuracy != r.at("accuracy").get<double>()) differ.push_back("accuracy");
        if (again.cp_loss != r.at("cp_loss").get<int32_t>()) differ.push_back("cp_loss");
        if (judgment_name(again_judgment) != theirs_judgment) differ.push_back("judgment");
        if (again.played_best != r.at("played_best").get<bool>()) differ.push_back("played_best");
        if (again.only_move != r.at("only_move").get<bool>()) differ.push_back("only_move");
        if (again.only_gap != r.at("only_gap").get<double>()) differ.push_back("only_gap");
        if (again.key != r.at("key").get<bool>()) differ.push_back("key");
        ++compared;
        if (differ.empty()) continue;
        if (++wrong <= 4) {
            std::string fields;
            for (const auto& d : differ) fields += " " + d;
            FAIL_CHECK(c.id << ":" << fields << "\n  ours: " << again.win_before << " -> " << again.win_after
                            << ", accuracy " << again.accuracy << ", cp " << again.cp_loss
                            << "\n  the prototype's: " << r.dump());
        }
    }
    MESSAGE(compared << " moves reviewed again, " << wrong << " differ; " << dropped
                     << " no longer worth a comment, as the prototype finds");
    CHECK(wrong == 0);
    if (!env) CHECK(compared == 254);
}
