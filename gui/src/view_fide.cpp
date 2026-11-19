// FIDE cards for the players of the databases (#57, mockup M8, board "Prep"): asked of Lichess on demand only (a right
// click on a name of the list, or a button in the game's preview), by the game's FIDE id or by the name, the user
// confirming the match among namesakes; shown under the mouse once known, kept in the profile.

#include <cfloat>
#include <chrono>
#include <ctime>
#include <future>

#include <SDL3/SDL.h>
#include <imgui.h>

#include <fmt/include/fmt/format.h>

#include "app.hpp"
#include "i18n.hpp"
#include "ui.hpp"

namespace fcg {

lichess::FideCards& App::fideCards() {
    if (!fide_cards_)
        fide_cards_ = std::make_unique<lichess::FideCards>(storage_.root() / "lichess-cache" / "fide.json");
    return *fide_cards_;
}

void App::askFideCard(std::string name, int64_t id) {
    if (name.empty() || (fide_ask_ && (fide_ask_->card.valid() || fide_ask_->search.valid()))) return;
    fide_popup_       = true;
    fide_ask_         = FideAsk{};
    auto& a           = *fide_ask_;
    a.name            = std::move(name);
    a.id              = id;
    const auto* known = id > 0 ? fideCards().byId(id) : fideCards().byName(a.name);
    if (known) {
        a.shown = *known;
        return;
    }
    if (id > 0)
        a.card = std::async(std::launch::async, [id] { return lichess::fidePlayer(id); });
    else
        a.search = std::async(std::launch::async, [name = a.name] { return lichess::fideSearch(name); });
}

void App::pollFideCards() {
    using namespace std::chrono_literals;
    if (!fide_ask_) return;
    auto& a = *fide_ask_;
    if (a.card.valid() && a.card.wait_for(0s) == std::future_status::ready) {
        auto got = a.card.get();
        if (got.value) {
            fideCards().keep(*got.value, a.name);  // the game gave its id: the card of that name there
            fideCards().save();
            a.shown = std::move(got.value);
        } else {
            a.error = got.error;
        }
    }
    if (a.search.valid() && a.search.wait_for(0s) == std::future_status::ready) {
        auto got = a.search.get();
        if (!got.value) {
            a.error = got.error;
        } else if (auto match = lichess::fideMatch(*got.value, a.name)) {
            fideCards().keep(*match, a.name);
            fideCards().save();
            a.shown = std::move(match);
        } else if (got.value->empty()) {
            a.error = trf("No player called “{}” on FIDE's list.", a.name);
        } else {
            if (got.value->size() > 12) got.value->resize(12);
            a.choices = std::move(got.value);  // the user chooses
        }
    }
}

void App::drawFideCard(const lichess::FideCard& c) {
    ImGui::TextUnformatted((c.title.empty() ? c.name : fmt::format("{} {}", c.title, c.name)).c_str());
    std::string line = c.federation.empty() ? std::string() : c.federation;
    if (c.year > 0) line += (line.empty() ? "" : " · ") + trf("born {}", c.year);
    line += (line.empty() ? "" : " · ") + fmt::format("FIDE {}", c.id);
    if (c.inactive) line += std::string(" · ") + TR("inactive");
    ImGui::TextDisabled("%s", line.c_str());
    const auto rating = [](int r) { return r > 0 ? std::to_string(r) : std::string("–"); };
    ImGui::TextUnformatted(
        trf("Standard {} · Rapid {} · Blitz {}", rating(c.standard), rating(c.rapid), rating(c.blitz)).c_str());
}

void App::drawFidePopup() {
    if (std::exchange(fide_popup_, false)) ImGui::OpenPopup("##fide_card");
    ImGui::SetNextWindowSizeConstraints(ImVec2(ImGui::GetFontSize() * 18, 0),
                                        ImVec2(ImGui::GetFontSize() * 30, FLT_MAX));
    if (!ImGui::BeginPopup("##fide_card")) return;
    if (!fide_ask_) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    auto& a = *fide_ask_;
    drawFideAsk(a);
    ImGui::EndPopup();
}

void App::drawFideAsk(FideAsk& a) {
    const ui::TopicRegion topic(guide::topics::FideCards);  // its group closed before the popup
    ImGui::PushTextWrapPos(0);
    if (a.card.valid() || a.search.valid()) {
        ImGui::TextUnformatted(trf("Asking Lichess for {}'s FIDE card…", a.name).c_str());
        ImGui::TextDisabled("%s", a.id > 0 ? TR("Sent: the FIDE id the game gives, without an account.")
                                           : TR("Sent: the name, without an account."));
    } else if (!a.error.empty()) {
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s", a.error.c_str());
    } else if (a.choices) {
        ImGui::TextUnformatted(trf("Players called like “{}” on FIDE's list: which one is it?", a.name).c_str());
        std::optional<lichess::FideCard> chosen;
        for (const auto& c : *a.choices) {
            std::string line = c.title.empty() ? c.name : fmt::format("{} {}", c.title, c.name);
            if (!c.federation.empty()) line += " · " + c.federation;
            if (c.year > 0) line += " · " + trf("born {}", c.year);
            if (c.standard > 0) line += fmt::format(" · {}", c.standard);
            if (ImGui::Selectable(fmt::format("{}##fide{}", line, c.id).c_str(), false,
                                  ImGuiSelectableFlags_DontClosePopups))
                chosen = c;
        }
        ImGui::TextDisabled("%s", TR("The one chosen is kept for this name, in your profile."));
        if (chosen) {
            fideCards().keep(*chosen, a.name);
            fideCards().save();
            a.shown = std::move(chosen);
            a.choices.reset();
        }
    } else if (a.shown) {
        drawFideCard(*a.shown);
        if (ImGui::SmallButton(TR("On lichess.org"))) SDL_OpenURL(lichess::fideUrl(*a.shown).c_str());
        if (a.shown->asked > 0) {
            char day[32]           = "";
            const std::time_t when = static_cast<std::time_t>(a.shown->asked);
            if (const auto* tm = std::localtime(&when)) std::strftime(day, sizeof day, "%Y-%m-%d", tm);
            ImGui::TextDisabled(
                "%s", trf("From Lichess, which keeps FIDE's list; asked on {}, kept in your profile.", day).c_str());
        }
    }
    ImGui::PopTextWrapPos();
}

}  // namespace fcg
