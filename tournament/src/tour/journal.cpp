#include "journal.hpp"

#include <fstream>
#include <stdexcept>
#include <string>

#include "files.hpp"

namespace fcg::tour {

namespace fs = std::filesystem;

std::vector<nlohmann::json> Journal::read(const fs::path& file, bool repair, std::vector<std::string>* damaged,
                                          uint64_t* consumed) {
    std::vector<nlohmann::json> events;
    if (consumed) *consumed = 0;

    std::ifstream in(file, std::ios::binary);
    if (!in) return events;

    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();

    std::size_t pos  = 0;
    std::size_t line = 0;
    while (pos < text.size()) {
        const auto end = text.find('\n', pos);
        if (end == std::string::npos) {
            // Cut short by a crash: forget it, and remove it so that the next line does not get glued to it.
            if (repair) {
                std::error_code ec;
                fs::resize_file(file, pos, ec);
            }
            break;
        }

        ++line;
        const std::string_view content(text.data() + pos, end - pos);
        pos = end + 1;
        if (consumed) *consumed = pos;
        if (content.find_first_not_of(" \t\r") == std::string_view::npos) continue;

        try {
            events.push_back(nlohmann::json::parse(content));
        } catch (const nlohmann::json::exception&) {
            const auto what = file.string() + ", line " + std::to_string(line) + ": not a valid event";
            if (!damaged) throw std::runtime_error(what);
            damaged->push_back(what);
        }
    }

    return events;
}

uint64_t Journal::readFrom(const fs::path& file, uint64_t from, const Listener& line) {
    std::ifstream in(file, std::ios::binary);
    if (!in || !in.seekg(static_cast<std::streamoff>(from))) return from;
    uint64_t at = from;
    for (std::string text; std::getline(in, text);) {
        if (in.eof()) break;  // its newline not written yet
        const uint64_t end = at + text.size() + 1;
        if (text.find_first_not_of(" \t\r") != std::string::npos) {
            nlohmann::json event;
            bool read = true;
            try {
                event = nlohmann::json::parse(text);
            } catch (const nlohmann::json::exception&) {
                read = false;  // damaged
            }
            if (read) line(at, end, event);
        }
        at = end;
    }
    return at;
}

Journal::Journal(const fs::path& file) {
    // A partial last line that could not be cut (read() without repair, or the cut failed) becomes a line of its
    // own, which read() reports, instead of spoiling the next one.
    bool cut_short = false;
    {
        std::ifstream in(file, std::ios::binary | std::ios::ate);
        if (in && in.tellg() > 0) {
            in.seekg(-1, std::ios::end);
            cut_short = in.get() != '\n';
        }
    }

    file_.reset(openFile(file, "ab"));
    if (!file_) throw std::runtime_error("cannot write " + file.string());
    if (cut_short && (std::fputc('\n', file_.get()) == EOF || !syncFile(file_.get()))) {
        file_.reset();
        throw std::runtime_error("cannot write " + file.string());
    }
    std::error_code ec;
    size_ = fs::file_size(file, ec);
    if (ec) size_ = 0;
}

void Journal::setListener(Listener listener) {
    std::lock_guard<std::mutex> lock(mutex_);
    listener_ = std::move(listener);
}

void Journal::append(const nlohmann::json& event) {
    const auto line = event.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace) + '\n';
    std::lock_guard<std::mutex> lock(mutex_);
    if (failed_) throw std::runtime_error("cannot write the tournament journal");
    if (std::fwrite(line.data(), 1, line.size(), file_.get()) != line.size() || !syncFile(file_.get())) {
        failed_ = true;
        throw std::runtime_error("cannot write the tournament journal (is the disk full?)");
    }
    const auto at = size_;
    size_ += line.size();
    if (listener_) listener_(at, size_, event);
}

}  // namespace fcg::tour
