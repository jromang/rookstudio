#include "theory_shelf.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <set>
#include <stdexcept>
#include <system_error>

#include <fmt/include/fmt/format.h>
#include <json.hpp>
#include <tour/files.hpp>
#include <tour/util.hpp>

#include "assistant_notes.hpp"
#include "jobs.hpp"
#include "lichess_studies.hpp"
#include "portable.hpp"

namespace fs = std::filesystem;

namespace fcg {

namespace {

const char* kindKey(TheorySource::Kind kind) { return kind == TheorySource::Kind::Lichess ? "lichess" : "file"; }

// A note told apart from another of the same source at the same position: the same words on the same move are one.
std::string sameness(bool on_move, const study::TheoryNote& n) {
    std::string out = on_move ? "m" : "p";
    out += n.san + '\x1f' + n.before + '\x1f' + n.after + '\x1f';
    for (const auto nag : n.nags) out += static_cast<char>(nag);
    if (n.eval) out += '\x1f' + std::to_string(static_cast<int>(n.eval->type)) + ':' + std::to_string(n.eval->value);
    for (const auto& m : n.arrows) out += fmt::format("|{}{}{}", m.colour, m.from, m.to);
    for (const auto& m : n.squares) out += fmt::format("|{}{}", m.colour, m.from);
    return out;
}

}  // namespace

TheoryShelf::TheoryShelf(fs::path profile, bool keep) : profile_(std::move(profile)), keep_(keep) { load(); }

TheoryShelf::~TheoryShelf() {
    // The readings under way stop; their futures wait for them as they go.
    for (auto* list : {&studies_, &repertoires_})
        for (auto& s : *list)
            if (s.cancel) s.cancel->store(true);
}

void TheoryShelf::load() {
    std::ifstream in(profile_ / "theory.json", std::ios::binary);
    if (!in) return;
    nlohmann::json j;
    try {
        j = nlohmann::json::parse(in);
    } catch (const std::exception&) {
        return;  // a file cut short: the list starts again
    }
    if (!j.is_object()) return;
    try {
        repertoires_shown_ = j.value("repertoires", true);
        const auto list    = j.find("studies");
        if (list == j.end() || !list->is_array()) return;
        for (const auto& e : *list) {
            if (!e.is_object()) continue;
            TheorySource s;
            s.shown = e.value("shown", true);
            if (e.value("kind", std::string()) == "lichess") {
                s.kind    = TheorySource::Kind::Lichess;
                s.lichess = e.value("id", std::string());
                if (s.lichess.empty()) continue;
                s.name = e.value("name", s.lichess);
                s.file = lichess::studyCache(profile_, s.lichess);
            } else {
                const auto file = e.value("file", std::string());
                if (file.empty()) continue;
                s.file = fs::path(portable::resolved(file));
                s.name = s.file.stem().string();
            }
            studies_.push_back(std::move(s));
        }
    } catch (const std::exception&) {
        // A value of another type, written by hand: the studies read up to it are kept.
    }
}

void TheoryShelf::save() const {
    if (!keep_) return;
    nlohmann::json list = nlohmann::json::array();
    for (const auto& s : studies_) {
        nlohmann::json e;
        e["kind"]  = kindKey(s.kind);
        e["shown"] = s.shown;
        if (s.kind == TheorySource::Kind::Lichess) {
            e["id"]   = s.lichess;
            e["name"] = s.name;
        } else {
            e["file"] = portable::stored(s.file.string());
        }
        list.push_back(std::move(e));
    }
    nlohmann::json j;
    j["repertoires"] = repertoires_shown_;
    j["studies"]     = std::move(list);
    std::error_code ec;
    fs::create_directories(profile_, ec);
    tour::replaceFile(profile_ / "theory.json", j.dump(1));
}

void TheoryShelf::read(TheorySource& s) {
    std::error_code ec;
    const auto stamp = fs::last_write_time(s.file, ec);
    if (ec) {
        s.missing = true;
        return;
    }
    const auto size = fs::file_size(s.file, ec);
    s.missing       = false;
    s.job_stamp     = stamp;
    s.job_size      = ec ? 0 : size;
    s.total         = s.job_size;
    s.done          = std::make_shared<std::atomic<uint64_t>>(0);
    s.cancel        = std::make_shared<std::atomic<bool>>(false);
    s.job           = std::async(std::launch::async, [file = s.file, done = s.done, cancel = s.cancel] {
        std::ifstream in(file, std::ios::binary);
        if (!in) throw std::runtime_error("it cannot be opened");
        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        study::TheoryOptions options;
        options.left_out = ASSISTANT_MARK;  // the assistant's comments are no notes
        return study::readTheory(text, options, cancel.get(), done.get());
    });
}

std::size_t TheoryShelf::addFile(const fs::path& file, bool* added) {
    std::error_code ec;
    auto wanted = fs::weakly_canonical(file, ec);
    if (ec) wanted = file;
    for (std::size_t i = 0; i < studies_.size(); ++i) {
        if (studies_[i].kind != TheorySource::Kind::File) continue;
        auto known = fs::weakly_canonical(studies_[i].file, ec);
        if (ec) known = studies_[i].file;
        if (known == wanted) {
            if (added) *added = false;
            return i;
        }
    }
    TheorySource s;
    s.file = wanted;
    s.name = wanted.stem().string();
    studies_.push_back(std::move(s));
    read(studies_.back());
    save();
    ++generation_;
    if (added) *added = true;
    return studies_.size() - 1;
}

std::size_t TheoryShelf::addLichess(const std::string& id, const std::string& name, bool* added) {
    if (const auto known = findLichess(id)) {
        if (added) *added = false;
        studies_[*known].name = name.empty() ? studies_[*known].name : name;
        return *known;
    }
    TheorySource s;
    s.kind    = TheorySource::Kind::Lichess;
    s.lichess = id;
    s.name    = name.empty() ? id : name;
    s.file    = lichess::studyCache(profile_, id);
    studies_.push_back(std::move(s));
    read(studies_.back());
    save();
    ++generation_;
    if (added) *added = true;
    return studies_.size() - 1;
}

std::optional<std::size_t> TheoryShelf::findLichess(const std::string& id) const {
    for (std::size_t i = 0; i < studies_.size(); ++i)
        if (studies_[i].kind == TheorySource::Kind::Lichess && studies_[i].lichess == id) return i;
    return std::nullopt;
}

std::optional<TheoryShelf::Removed> TheoryShelf::remove(std::size_t index) {
    if (index >= studies_.size()) return std::nullopt;
    auto& s = studies_[index];
    Removed r{s.kind, s.name, s.file, s.lichess, s.shown, index};
    if (s.cancel) s.cancel->store(true);
    studies_.erase(studies_.begin() + static_cast<std::ptrdiff_t>(index));
    save();
    ++generation_;
    return r;
}

std::size_t TheoryShelf::restore(const Removed& removed) {
    TheorySource s;
    s.kind    = removed.kind;
    s.name    = removed.name;
    s.file    = removed.file;
    s.lichess = removed.lichess;
    s.shown   = removed.shown;
    const auto at =
        std::min(removed.at, studies_.size());  // its place in the list, or the end when the list is shorter now
    studies_.insert(studies_.begin() + static_cast<std::ptrdiff_t>(at), std::move(s));
    read(studies_[at]);
    save();
    ++generation_;
    return at;
}

void TheoryShelf::setShown(std::size_t index, bool shown) {
    if (index >= studies_.size() || studies_[index].shown == shown) return;
    studies_[index].shown = shown;
    save();
    ++generation_;
}

void TheoryShelf::showRepertoires(bool shown) {
    if (repertoires_shown_ == shown) return;
    repertoires_shown_ = shown;
    save();
    ++generation_;
}

void TheoryShelf::relocate(std::size_t index, const fs::path& file) {
    if (index >= studies_.size() || studies_[index].kind != TheorySource::Kind::File) return;
    auto& s = studies_[index];
    if (s.cancel) s.cancel->store(true);
    if (s.job.valid()) s.job.wait();  // the old file's reading, dropped
    s.job  = {};
    s.file = file;
    s.name = file.stem().string();
    s.stamp.reset();
    s.notes.reset();
    s.error.clear();
    read(s);
    save();
    ++generation_;
}

void TheoryShelf::syncRepertoires(const std::vector<std::pair<std::string, fs::path>>& files) {
    const bool same = files.size() == repertoires_.size() &&
                      std::equal(files.begin(), files.end(), repertoires_.begin(),
                                 [](const auto& f, const auto& s) { return f.first == s.name && f.second == s.file; });
    if (same) return;
    std::vector<TheorySource> next;
    for (const auto& [name, file] : files) {
        const auto known = std::find_if(repertoires_.begin(), repertoires_.end(),
                                        [&](const TheorySource& s) { return s.file == file && !s.file.empty(); });
        if (known != repertoires_.end()) {
            known->name = name;
            next.push_back(std::move(*known));
            known->file.clear();  // taken
            continue;
        }
        TheorySource s;
        s.kind = TheorySource::Kind::Repertoire;
        s.name = name;
        s.file = file;
        next.push_back(std::move(s));  // read at the next look
    }
    for (auto& s : repertoires_)  // those gone: their readings stop
        if (s.cancel && !s.file.empty()) s.cancel->store(true);
    repertoires_ = std::move(next);
    looked_once_ = false;  // the new ones read at the next poll
    ++generation_;
}

bool TheoryShelf::poll(std::chrono::steady_clock::time_point now, std::chrono::milliseconds every) {
    bool changed    = false;
    const auto take = [&](TheorySource& s) {
        if (!isReady(s.job)) return;
        try {
            auto notes = s.job.get();
            s.notes    = std::make_shared<const study::TheoryNotes>(std::move(notes));
            s.error.clear();
        } catch (const std::exception& e) {
            s.error = e.what();
            s.notes.reset();
        }
        s.stamp   = s.job_stamp;
        s.size    = s.job_size;
        s.read_at = nowSeconds();
        ++s.readings;
        changed = true;
    };
    for (auto* list : {&studies_, &repertoires_})
        for (auto& s : *list) take(s);
    if (!looked_once_ || now - looked_ >= every) {
        looked_         = now;
        looked_once_    = true;
        const auto look = [&](TheorySource& s) {
            if (s.job.valid()) return;  // being read
            std::error_code ec;
            const auto stamp = fs::last_write_time(s.file, ec);
            if (ec) {
                if (!s.missing) changed = true;
                s.missing = true;
                return;
            }
            const auto size = fs::file_size(s.file, ec);
            if (s.missing) changed = true;
            s.missing = false;
            if (!s.stamp || *s.stamp != stamp || s.size != (ec ? 0 : size)) read(s);
        };
        for (auto* list : {&studies_, &repertoires_})
            for (auto& s : *list) look(s);
    }
    if (changed) ++generation_;
    return changed;
}

void TheoryShelf::wait() {
    for (auto* list : {&studies_, &repertoires_})
        for (auto& s : *list)
            if (s.job.valid()) s.job.wait();
    poll(std::chrono::steady_clock::now(), std::chrono::hours(1));
}

bool TheoryShelf::reading() const {
    for (const auto* list : {&studies_, &repertoires_})
        for (const auto& s : *list)
            if (s.job.valid()) return true;
    return false;
}

bool TheoryShelf::anyNotes() const {
    for (const auto& s : studies_)
        if (s.shown && s.notes && s.notes->counts().notes > 0) return true;
    if (repertoires_shown_)
        for (const auto& s : repertoires_)
            if (s.notes && s.notes->counts().notes > 0) return true;
    return false;
}

TheoryAt TheoryShelf::at(uint64_t key, const std::vector<std::string>* line) const {
    TheoryAt out;
    if (key == 0) return out;
    const auto from = [&](const TheorySource& s) {
        if (!s.notes) return;
        const auto about = s.notes->about(key);
        const auto moves = s.notes->from(key);
        if (about.empty() && moves.empty()) return;
        out.held.push_back(s.notes);
        std::set<std::string> seen;
        const auto& games = s.notes->games();
        const auto add    = [&](const study::TheoryNote* n, bool on_move) {
            if (!seen.insert(sameness(on_move, *n)).second) return;
            TheoryFound f;
            f.kind              = s.kind;
            f.source            = s.name;
            f.file              = s.file;
            f.lichess           = s.lichess;
            f.note              = n;
            f.games             = games.size();
            f.on_move           = on_move;
            const bool standard = n->game < games.size() && games[n->game].standard;
            if (n->game < games.size()) f.game = games[n->game].name;
            // Its moves to the position: those before its move for a note on a move from it.
            if (line && standard) {
                const auto end = n->path.end() - (on_move && !n->path.empty() ? 1 : 0);
                f.transposed   = !std::equal(n->path.begin(), end, line->begin(), line->end());
            }
            if (on_move) {
                auto& m = out.moves[n->san];
                for (const auto nag : n->nags)
                    if (std::find(m.symbols.begin(), m.symbols.end(), nag) == m.symbols.end()) m.symbols.push_back(nag);
                if (n->hasText() || !n->arrows.empty() || !n->squares.empty()) ++m.notes;
            }
            out.found.push_back(std::move(f));
        };
        for (const auto* n : about) add(n, false);
        for (const auto* n : moves) add(n, true);
    };
    for (const auto& s : studies_)
        if (s.shown) from(s);
    if (repertoires_shown_)
        for (const auto& s : repertoires_) from(s);
    return out;
}

}  // namespace fcg
