// The assistant as the window uses it (assistant_service.hpp): its settings, its model's file, the model loaded at the
// first request and unloaded, on the scripted model (gui/tests/mock/assistant.cpp).

#ifdef FCG_ASSISTANT

#    include <doctest/doctest.hpp>

#    include <filesystem>
#    include <fstream>

#    include "../../tournament/tests/self_path.hpp"
#    include "assistant_service.hpp"

using namespace fcg;
namespace fs = std::filesystem;

namespace {

std::string mock() { return (test::selfPath().parent_path() / FCG_MOCK_ASSISTANT).string(); }

fs::path freshProfile(const char* name) {
    const auto dir = fs::temp_directory_path() / name;
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

commentary::Request asked(std::string text) {
    commentary::Request r;
    r.messages   = {{commentary::Speaker::User, std::move(text)}};
    r.max_tokens = 50;
    return r;
}

}  // namespace

TEST_CASE("assistant service: no model, no answer; the model chosen loads at the first request and unloads") {
    const auto profile = freshProfile("assistant_service");
    AssistantService service(profile, mock());
    CHECK_FALSE(service.installed());
    CHECK(service.state() == AssistantService::State::NoModel);
    std::string error;
    CHECK_FALSE(service.generate(asked("hello"), &error));
    CHECK(error.find("not on this machine") != std::string::npos);

    // A GGUF file of the disk in place of the catalogue's model.
    const auto file = profile / "commentary.gguf";
    std::ofstream(file) << "model";
    service.settings().model_file = file.string();
    CHECK(service.installed());
    CHECK(service.state() == AssistantService::State::Unloaded);
    const auto answer = service.generate(asked("hello"), &error);
    REQUIRE_MESSAGE(answer, error);
    CHECK(answer->text.find("hello") != std::string::npos);
    CHECK(service.state() == AssistantService::State::Ready);
    CHECK_FALSE(service.devices().empty());
    service.unload();
    CHECK(service.state() == AssistantService::State::Unloaded);
    // Loaded again by the next request.
    REQUIRE(service.generate(asked("again"), &error));
    CHECK(service.state() == AssistantService::State::Ready);
}

TEST_CASE("assistant service: its settings are kept in the profile") {
    const auto profile = freshProfile("assistant_settings");
    {
        AssistantService service(profile, mock());
        CHECK(service.settings().unload_minutes == 15);
        CHECK(service.settings().gpu);
        service.settings().model_file     = "/models/other.gguf";
        service.settings().unload_minutes = 0;
        service.settings().not_now        = true;
        service.saveSettings();
    }
    AssistantService again(profile, mock());
    CHECK(again.settings().model_file == "/models/other.gguf");
    CHECK(again.settings().unload_minutes == 0);
    CHECK(again.settings().not_now);
    CHECK(again.modelPath() == fs::path("/models/other.gguf"));
}

TEST_CASE("assistant service: a model that does not load says why, and the next request tries again") {
    const auto profile = freshProfile("assistant_missing");
    AssistantService service(profile, mock());
    // The scripted model refuses a file named "missing".
    const auto file = profile / "missing";
    std::ofstream(file) << "model";
    service.settings().model_file = file.string();
    std::string error;
    CHECK_FALSE(service.generate(asked("hello"), &error));
    CHECK_FALSE(error.empty());
    CHECK(service.state() == AssistantService::State::Failed);
    CHECK_FALSE(service.failure().empty());
}

#endif
