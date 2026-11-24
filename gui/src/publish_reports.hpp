#pragma once

// The reports printed and made pages of HTML (#93, mockup M13, board 6): a player's dossier (#90), an opening report
// (#91), a tournament's crosstable (#92). Each is made where its view is (view_dossier.cpp, view_opening_report.cpp,
// view_crosstable.cpp), with the view's own words, as a document of blocks (publish_doc.hpp) that the Export sheet
// prints (publish_pdf.hpp) or writes as a page (documentHtml()).

#include <string>
#include <vector>

#include <study/crosstable.hpp>

#include "dossier.hpp"
#include "opening_report.hpp"
#include "publish_game.hpp"

namespace fcg {

// A tournament's crosstable, on landscape pages: all its players, its tie-breaks named.
[[nodiscard]] publish::Document crosstableDocument(const study::Crosstable& table, const std::string& title,
                                                   uint32_t games, bool letter, const publish::DocumentLook& look);

// A player's dossier: who they are, what it is made of, the overview's six cards, their last games, their lines with
// each colour (each a line to replay on the page of HTML), their rating year by year, their structures, the gaps of
// the user's repertoires against them. `sources` names the dossier's databases, in its order.
[[nodiscard]] publish::Document dossierDocument(const DossierReport& report, const std::string& name,
                                                const std::vector<std::string>& sources, bool user_names, bool letter,
                                                const publish::DocumentLook& look);

// An opening report: its position, its moves and their figures, the main lines (to replay), the structures and plans,
// the key games, the novelties and the assistant's summary, marked as its own.
[[nodiscard]] publish::Document openingReportDocument(const ReportView& view, const std::vector<ReportBase>& bases,
                                                      const ReportSummary* summary, bool letter,
                                                      const publish::DocumentLook& look);

}  // namespace fcg
