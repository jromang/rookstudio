#include "export_sheet.hpp"

#include <fstream>
#include <system_error>

#include <SDL3/SDL_misc.h>
#include <SDL3/SDL_process.h>
#include <SDL3/SDL_stdinc.h>

#include <fmt/include/fmt/format.h>

#include "i18n.hpp"
#include "ui.hpp"

namespace fcg {

namespace fs = std::filesystem;

ExportSettings loadExportSettings(const fs::path& profile) {
    std::ifstream in(profile / "export.json");
    if (!in) return {};
    try {
        return nlohmann::json::parse(in).get<ExportSettings>();
    } catch (const std::exception&) {
        return {};
    }
}

void saveExportSettings(const fs::path& profile, const ExportSettings& settings) {
    std::error_code ec;
    fs::create_directories(profile, ec);
    const auto file = profile / "export.json";
    const auto tmp  = profile / "export.json.tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return;
        out << nlohmann::json(settings).dump(2) << "\n";
        if (!out) return;
    }
    fs::rename(tmp, file, ec);
}

bool folderWritable(const fs::path& folder) {
    std::error_code ec;
    if (!fs::is_directory(folder, ec)) return false;
    const auto probe = folder / ".rookstudio-write-test";
    {
        std::ofstream out(probe, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out << "x";
        if (!out) return false;
    }
    fs::remove(probe, ec);
    return true;
}

bool systemHasPdfReader() {
#if defined(_WIN32) || defined(__APPLE__)
    return true;
#else
    const char* args[]   = {"xdg-mime", "query", "default", "application/pdf", nullptr};
    SDL_Process* process = SDL_CreateProcess(args, true);
    if (!process) return true;  // no xdg-mime: opening the file tells
    std::size_t size = 0;
    int code         = 0;
    void* out        = SDL_ReadProcess(process, &size, &code);
    std::string text = out ? std::string(static_cast<const char*>(out), size) : std::string();
    SDL_free(out);
    SDL_DestroyProcess(process);
    if (code != 0) return true;
    return text.find_first_not_of(" \t\r\n") != std::string::npos;
#endif
}

bool openInSystem(const fs::path& file) {
    const auto u8 = file.u8string();
    const std::string text(u8.begin(), u8.end());
#ifdef _WIN32
    return SDL_OpenURL(text.c_str());
#else
    return SDL_OpenURL(ui::fileUrl(file).c_str());
#endif
}

std::string fileWeight(uint64_t bytes) {
    if (bytes < 1000 * 1000) return trf("{} KB", std::max<uint64_t>(1, (bytes + 500) / 1000));
    return trf("{:.1f} MB", static_cast<double>(bytes) / 1e6);
}

}  // namespace fcg
