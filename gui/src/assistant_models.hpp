#pragma once

// The language models the assistant offers, where they are kept and how they come: downloaded once into the profile,
// resumed after a break, checked against their published hash. A model published once is never changed: a new one
// gets a new name. Any other GGUF file may be chosen instead.

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>

#include "https.hpp"

namespace fcg {

struct AssistantModel {
    std::string_view id;         // "commentary-v11"
    std::string_view file;       // the file's name, here and where it is published
    std::string_view url;        // where it is downloaded from
    std::string_view sha256;     // of the file, in hexadecimal
    uint64_t size = 0;           // of the file, in bytes
    std::string_view license;    // SPDX
    std::string_view languages;  // the languages it comments in
};

// The models the program offers, the recommended one first.
[[nodiscard]] std::span<const AssistantModel> assistantModels();

// Where the models are kept: <profile>/models.
[[nodiscard]] std::filesystem::path modelsFolder(const std::filesystem::path& profile);
[[nodiscard]] std::filesystem::path modelFile(const std::filesystem::path& profile, const AssistantModel& model);
// The model is there, whole: a file of its size (its hash was checked when it came).
[[nodiscard]] bool modelInstalled(const std::filesystem::path& profile, const AssistantModel& model);

// Downloads `model` into the profile, from where an earlier try stopped, and checks it against its hash. `url`, when
// given, is fetched instead of the model's own.
[[nodiscard]] Download downloadModel(const std::filesystem::path& profile, const AssistantModel& model,
                                     DownloadProgress* progress, const std::atomic<bool>* cancel,
                                     std::string_view url = {});

}  // namespace fcg
