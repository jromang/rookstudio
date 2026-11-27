#include "dialogs.hpp"

#include <SDL3/SDL_dialog.h>

namespace fcg {

struct Dialogs::Request {
    Dialogs* owner;
    Purpose purpose;
    Chosen chosen;
    // SDL keeps pointers to the filter strings until the callback has run.
    std::vector<std::pair<std::string, std::string>> filter_strings;
    std::vector<SDL_DialogFileFilter> filters;
    std::string start;
};

namespace {

enum Kind { OPEN_FILE, OPEN_FOLDER, SAVE_FILE };

}  // namespace

void Dialogs::callback(void* userdata, const char* const* files, int) {
    auto* request = static_cast<Request*>(userdata);
    auto* owner   = request->owner;

    {
        std::lock_guard<std::mutex> lock(owner->mutex_);
        if (files && files[0]) owner->answers_.push_back({request->purpose, files[0], std::move(request->chosen)});
        owner->pending_--;
    }

    delete request;
}

void Dialogs::show(Request* request, int kind) {
    for (const auto& [name, pattern] : request->filter_strings)
        request->filters.push_back({name.c_str(), pattern.c_str()});

    {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_++;
    }

    const auto* filters = request->filters.empty() ? nullptr : request->filters.data();
    const int count     = static_cast<int>(request->filters.size());
    const char* start   = request->start.empty() ? nullptr : request->start.c_str();
    switch (kind) {
        case OPEN_FILE:
            SDL_ShowOpenFileDialog(&Dialogs::callback, request, window_, filters, count, start, false);
            break;
        case OPEN_FOLDER:
            SDL_ShowOpenFolderDialog(&Dialogs::callback, request, window_, start, false);
            break;
        default:
            SDL_ShowSaveFileDialog(&Dialogs::callback, request, window_, filters, count, start);
            break;
    }
}

void Dialogs::openFile(Purpose purpose, const std::vector<std::pair<std::string, std::string>>& filters,
                       const std::string& start) {
    show(new Request{this, purpose, {}, filters, {}, start}, OPEN_FILE);
}

void Dialogs::openFolder(Purpose purpose, const std::string& start) {
    show(new Request{this, purpose, {}, {}, {}, start}, OPEN_FOLDER);
}

void Dialogs::saveFile(Purpose purpose, const std::vector<std::pair<std::string, std::string>>& filters,
                       const std::string& start) {
    show(new Request{this, purpose, {}, filters, {}, start}, SAVE_FILE);
}

void Dialogs::openFile(Chosen chosen, const std::vector<std::pair<std::string, std::string>>& filters,
                       const std::string& start) {
    show(new Request{this, {}, std::move(chosen), filters, {}, start}, OPEN_FILE);
}

void Dialogs::openFolder(Chosen chosen, const std::string& start) {
    show(new Request{this, {}, std::move(chosen), {}, {}, start}, OPEN_FOLDER);
}

void Dialogs::saveFile(Chosen chosen, const std::vector<std::pair<std::string, std::string>>& filters,
                       const std::string& start) {
    show(new Request{this, {}, std::move(chosen), filters, {}, start}, SAVE_FILE);
}

std::optional<Dialogs::Answer> Dialogs::take() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (answers_.empty()) return std::nullopt;

    auto answer = std::move(answers_.front());
    answers_.erase(answers_.begin());
    return answer;
}

bool Dialogs::busy() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return pending_ > 0;
}

}  // namespace fcg
