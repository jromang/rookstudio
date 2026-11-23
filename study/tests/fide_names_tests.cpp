// FIDE's rating list read into RookStudio's file, and the players of a database matched with it: a list made here in
// FIDE's format, of invented players (FIDE's own list is never part of the tests).
#include <doctest/doctest.hpp>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <study/fide_list.hpp>
#include <study/names.hpp>
#include <study/store.hpp>

#include "test_games.hpp"

using namespace fcg::study;
using namespace fcg::test;
namespace fs = std::filesystem;

namespace {

std::string pad(std::string s, std::size_t width) {
    s.resize(std::max(s.size(), width), ' ');
    return s;
}

// The columns' widths of FIDE's combined list.
constexpr std::size_t WIDTHS[] = {15, 61, 4, 4, 5, 5, 15, 4, 6, 4, 3, 6, 4, 3, 6, 4, 3, 6, 4};

std::string row(const std::vector<std::string>& values) {
    std::string out;
    for (std::size_t c = 0; c < values.size(); ++c) out += pad(values[c], WIDTHS[c]);
    return out + "\r\n";
}

std::string header() {
    return row({"ID Number", "Name", "Fed", "Sex", "Tit", "WTit", "OTit", "FOA", "SRtng", "SGm", "SK", "RRtng", "RGm",
                "Rk", "BRtng", "BGm", "BK", "B-day", "Flag"});
}

// A player: id, name, federation, sex, title, standard, rapid, blitz, birth year, flag.
std::string player(const std::string& id, const std::string& name, const std::string& fed, const std::string& sex,
                   const std::string& title, int standard, int rapid, int blitz, const std::string& born,
                   const std::string& flag = "", const std::string& other_titles = "") {
    const auto r = [](int v) { return v ? std::to_string(v) : std::string("0"); };
    return row({id, name, fed, sex, title, "", other_titles, "", r(standard), "0", "20", r(rapid), "0", "20", r(blitz),
                "0", "20", born, flag});
}

fs::path listFile(const std::string& name, const std::string& text) {
    const auto file = fs::temp_directory_path() / name;
    std::ofstream out(file, std::ios::binary);
    out << text;
    return file;
}

std::string invented() {
    std::string text = header();
    text += player("90000001", "Moreau, Claire", "FRA", "F", "WIM", 2341, 2300, 0, "1994", "w");
    text += player("90000002", "Lindqvist, Erik", "SWE", "M", "IM", 2398, 0, 2410, "1991");
    text += player("90000003", "Kovalenko, Petro", "UKR", "M", "GM", 2604, 2580, 2550, "1988");
    text += player("90000004", "Martin, Paul", "FRA", "M", "", 2105, 0, 0, "1979");
    text += player("90000005", "Martin, Paul", "FRA", "M", "", 1642, 0, 0, "2006");
    text += player("90000006", "Martin, Paul", "CAN", "M", "", 0, 1980, 0, "1995", "i");
    text += player("90000007", "Brennan, Liam", "IRL", "M", "FM", 2290, 0, 0, "0");
    // A value of the other titles longer than its column: the columns after it pushed.
    std::string pushed = player("90000008", "Okafor, Ada", "NGR", "F", "", 1888, 0, 0, "2001", "", "SI,FI,LSI,SLI,NA");
    text += pushed;
    return text;
}

}  // namespace

TEST_CASE("fide list: FIDE's columns read into RookStudio's file, its players by id and by surname") {
    const auto source = listFile("fide-list.txt", invented());
    const auto out    = fs::temp_directory_path() / "fide" / "list.bin";
    FidePrepareProgress progress;
    std::atomic<bool> cancel{false};
    const auto info = prepareFideList(source, out, "2026-11", progress, cancel);
    CHECK(info.players == 8);
    CHECK(info.rated == 8);
    CHECK(info.standard == 7);
    CHECK(info.blitz == 2);

    const FideList list(out);
    CHECK(list.info().month == "2026-11");
    CHECK(list.size() == 8);
    const auto erik = list.byId(90000002);
    REQUIRE(erik);
    const auto p = list.player(*erik);
    CHECK(p.name == "Lindqvist, Erik");
    CHECK(p.federation == "SWE");
    CHECK(p.title == "IM");
    CHECK(p.standard == 2398);
    CHECK(p.blitz == 2410);
    CHECK(p.born == 1991);
    CHECK_FALSE(p.woman);
    CHECK(list.player(*list.byId(90000001)).woman);
    CHECK(list.player(*list.byId(90000006)).inactive);
    CHECK(list.player(*list.byId(90000007)).born == 0);
    CHECK(list.player(*list.byId(90000008)).standard == 1888);  // read in its pushed column
    CHECK(list.player(*list.byId(90000008)).born == 2001);
    CHECK_FALSE(list.byId(12345));
    CHECK(list.bySurname("martin").size() == 3);
    CHECK(list.bySurname("lindqvist").size() == 1);
    CHECK(list.bySurname("nobody").empty());
    const auto translit = list.bySkeleton(nameSkeleton("kowalenko"));
    REQUIRE(translit.size() == 1);
    CHECK(list.player(translit[0]).id == 90000003);

    // Columns that changed: refused, the list before kept.
    auto changed = invented();
    changed.replace(changed.find("SRtng"), 5, "STD  ");
    const auto other = listFile("fide-changed.txt", changed);
    CHECK_THROWS_AS(prepareFideList(other, out, "2026-12", progress, cancel), FideFormatError);
    CHECK(FideList(out).info().month == "2026-11");
    CHECK_THROWS_AS(prepareFideList(listFile("fide-empty.txt", "nothing here\n"), out, "2026-12", progress, cancel),
                    FideFormatError);
    cancel = true;
    CHECK_THROWS(prepareFideList(source, out, "2026-12", progress, cancel));
    CHECK_THROWS(FideList(source));  // not a list of RookStudio's
}

TEST_CASE("names: spellings brought together by FIDE ids, by FIDE's list, or left to confirm") {
    const auto source = listFile("fide-names.txt", invented());
    const auto file   = fs::temp_directory_path() / "fide-names" / "list.bin";
    FidePrepareProgress prepare;
    std::atomic<bool> cancel{false};
    prepareFideList(source, file, "2026-11", prepare, cancel);
    const FideList list(file);

    const auto game = [](const std::string& white, const std::string& black, int white_elo, int black_elo,
                         const std::string& white_fide = "", int round = 1) {
        std::string text = "[Event \"Autumn Open\"]\n[Round \"" + std::to_string(round) +
                           "\"]\n[Date \"2026.10.20\"]\n[White \"" + white + "\"]\n[Black \"" + black + "\"]\n";
        if (white_elo) text += "[WhiteElo \"" + std::to_string(white_elo) + "\"]\n";
        if (black_elo) text += "[BlackElo \"" + std::to_string(black_elo) + "\"]\n";
        if (!white_fide.empty()) text += "[WhiteFideId \"" + white_fide + "\"]\n";
        text += "[Result \"1-0\"]\n\n1. e4 e5 1-0\n";
        ImportedGame g;
        REQUIRE(readImportedGame(text, g));
        return g;
    };
    auto db = database(
        "names-analysis",
        {game("Lindqvist, Erik", "Martin, Paul", 2398, 2100, "90000002", 1),
         game("Lindqvist,E", "Kowalenko, Petro", 2398, 2600, "", 2), game("Erik Lindqvist", "Leroy, Hugo", 0, 0, "", 3),
         game("Leroy H", "Moreau, Claire", 0, 2341, "", 4), game("NN", "Brennan, Liam", 0, 2290, "", 5)});
    NamesProgress progress;
    const auto analysis = analyseNames(db->snapshot(), &list, progress, cancel);
    CHECK_FALSE(analysis.cancelled);
    CHECK(analysis.spellings.size() == 9);  // NN is no one
    CHECK(analysis.list_month == "2026-11");
    const auto proposal = [&](const std::string& spelling) -> const PlayerProposal& {
        for (const auto& p : analysis.proposals)
            for (const auto s : p.spellings)
                if (db->snapshot()->name(analysis.spellings[s].name) == spelling) return p;
        FAIL("no proposal for " << spelling);
        return analysis.proposals.front();
    };
    const auto& erik = proposal("Lindqvist,E");
    CHECK(erik.spellings.size() == 3);
    CHECK(erik.match == NameMatch::Auto);
    CHECK(erik.reason == NameReason::FideIdInGames);
    CHECK(erik.name == "Lindqvist, Erik");
    const auto& petro = proposal("Kowalenko, Petro");
    CHECK(petro.match == NameMatch::Auto);
    CHECK(petro.reason == NameReason::Transliteration);
    CHECK(petro.name == "Kovalenko, Petro");
    const auto& paul = proposal("Martin, Paul");
    CHECK(paul.match == NameMatch::Confirm);
    REQUIRE(paul.candidates.size() == 3);
    CHECK(list.player(paul.candidates.front()).id == 90000004);  // the nearest to his games' rating
    const auto& hugo = proposal("Leroy H");
    CHECK(hugo.spellings.size() == 2);
    CHECK(hugo.match == NameMatch::None);
    CHECK(hugo.reason == NameReason::NotOnList);
    CHECK(proposal("Moreau, Claire").reason == NameReason::OneOnList);
    CHECK(analysis.proposals.front().match == NameMatch::Confirm);  // to confirm first

    // Brought together: the automatic ones, Paul Martin as confirmed, Hugo Leroy by hand.
    std::vector<Bringing> chosen;
    for (std::size_t i = 0; i < analysis.proposals.size(); ++i) {
        const auto& p = analysis.proposals[i];
        if (p.match == NameMatch::Auto)
            chosen.push_back({i, p.candidates.empty() ? std::nullopt : std::optional<std::size_t>(0)});
        if (&p == &paul) chosen.push_back({i, 0});
        if (&p == &hugo) chosen.push_back({i, std::nullopt});
    }
    const auto table = bringTogether(*db->snapshot()->players(), analysis, chosen, &list, *db->snapshot());
    db->setPlayers(table);
    const auto snap = db->snapshot();
    const auto ids  = snap->rowIds(1);
    CHECK(snap->shownName(ids.white) == "Lindqvist, Erik");
    CHECK(snap->shownName(ids.black) == "Kovalenko, Petro");
    const auto* p = snap->players()->of(ids.white);
    REQUIRE(p);
    CHECK(p->fide == 90000002);
    CHECK(p->federation == "SWE");
    CHECK(p->title == "IM");
    CHECK(p->list == "2026-11");
    CHECK(p->how == "fide-id");
    CHECK(snap->players()->of(snap->rowIds(0).black)->fide == 90000004);
    CHECK(snap->players()->of(snap->rowIds(2).black)->how == "hand");
    CHECK(snap->shownName(snap->rowIds(3).white) == "Leroy, Hugo");

    // Analysed again: what the table has is done.
    const auto again = analyseNames(snap, &list, progress, cancel);
    CHECK(again.count(NameMatch::Done) == 6);
    CHECK(again.bringing() == 0);

    // Without the list: the FIDE ids of the games only.
    auto other      = database("names-no-list", {game("Lindqvist, Erik", "Martin, Paul", 0, 0, "90000002", 1),
                                                 game("Lindqvist,E", "Martin, Paul", 0, 0, "90000002", 2)});
    const auto bare = analyseNames(other->snapshot(), nullptr, progress, cancel);
    CHECK(bare.list_month.empty());
    CHECK(bare.count(NameMatch::Auto) == 1);
    CHECK(bare.count(NameMatch::None) == 1);
}

TEST_CASE("names: an import's new spellings join their players by themselves, the doubtful ones left") {
    const auto game = [](const std::string& white, const std::string& black, const std::string& white_fide, int n) {
        std::string text = "[Event \"Winter Open\"]\n[Round \"" + std::to_string(n) + "\"]\n[White \"" + white +
                           "\"]\n[Black \"" + black + "\"]\n";
        if (!white_fide.empty()) text += "[WhiteFideId \"" + white_fide + "\"]\n";
        text += "[Result \"1-0\"]\n\n1. e4 e5 1-0\n";
        ImportedGame g;
        REQUIRE(readImportedGame(text, g));
        return g;
    };
    auto db   = database("names-join", {game("Lindqvist, Erik", "Adams, Zoe", "90000002", 1)});
    auto snap = db->snapshot();
    Player erik, zoe;
    erik.name      = "Lindqvist, Erik";
    erik.fide      = 90000002;
    erik.spellings = {snap->rowIds(0).white};
    zoe.name       = "Adams, Zoe";
    zoe.spellings  = {snap->rowIds(0).black};
    db->setPlayers(PlayerTable({erik, zoe}));
    CHECK_FALSE(joinNewSpellings(*db->snapshot(), 1));  // nothing new

    // Next week's games: Erik by his FIDE id under another spelling, Zoe by her name written the other way round,
    // "Adams, Z." (an initial) and a stranger left alone.
    db->append({game("Lindqvist,E", "Zoe Adams", "90000002", 2), game("Adams, Z.", "Novak, Ivo", "", 3)}, true);
    snap             = db->snapshot();
    const auto table = joinNewSpellings(*snap, 1);
    REQUIRE(table);
    db->setPlayers(*table);
    snap = db->snapshot();
    CHECK(snap->shownName(snap->rowIds(1).white) == "Lindqvist, Erik");
    CHECK(snap->shownName(snap->rowIds(1).black) == "Adams, Zoe");
    CHECK(snap->shownName(snap->rowIds(2).white) == "Adams, Z.");
    CHECK(snap->shownName(snap->rowIds(2).black) == "Novak, Ivo");
    CHECK_FALSE(joinNewSpellings(*snap, 1));
}
