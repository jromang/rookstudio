#include "generator.hpp"

#include "vulkan_device.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <mutex>
#include <thread>
#include <vector>

#include <ggml-backend.h>
#include <jinja/lexer.h>
#include <jinja/parser.h>
#include <jinja/runtime.h>
#include <json.h>
#include <llama.h>

namespace fcg::assistant {

namespace {

const char* roleName(commentary::Speaker s) {
    switch (s) {
        case commentary::Speaker::System:
            return "system";
        case commentary::Speaker::User:
            return "user";
        default:
            return "assistant";
    }
}

// llama.cpp's backends are loaded once in a process, its logs silenced.
void initialise() {
    static std::once_flag once;
    std::call_once(once, [] {
        llama_log_set([](ggml_log_level, const char*, void*) {}, nullptr);
        ggml_backend_load_all();
        llama_backend_init();
    });
}

struct Sampler {
    llama_sampler* s = nullptr;
    explicit Sampler(llama_sampler* sampler) : s(sampler) {}
    ~Sampler() {
        if (s) llama_sampler_free(s);
    }
    Sampler(const Sampler&)            = delete;
    Sampler& operator=(const Sampler&) = delete;
};

struct Batch {
    llama_batch b;
    explicit Batch(int32_t size) : b(llama_batch_init(size, 0, 1)) {}
    ~Batch() { llama_batch_free(b); }
    Batch(const Batch&)            = delete;
    Batch& operator=(const Batch&) = delete;
    void add(llama_token token, llama_pos pos, bool logits) {
        b.token[b.n_tokens]     = token;
        b.pos[b.n_tokens]       = pos;
        b.n_seq_id[b.n_tokens]  = 1;
        b.seq_id[b.n_tokens][0] = 0;
        b.logits[b.n_tokens]    = logits;
        ++b.n_tokens;
    }
};

// The token `pick` chooses among the logits of the batch's `index`th token, the grammar applied first when given.
std::optional<llama_token> choose(llama_context* ctx, int32_t index, llama_sampler* pick, llama_sampler* grammar) {
    const auto* logits = llama_get_logits_ith(ctx, index);
    const auto n_vocab = llama_vocab_n_tokens(llama_model_get_vocab(llama_get_model(ctx)));
    std::vector<llama_token_data> data(static_cast<std::size_t>(n_vocab));
    for (llama_token t = 0; t < n_vocab; ++t) data[static_cast<std::size_t>(t)] = {t, logits[t], 0.0f};
    llama_token_data_array all{data.data(), data.size(), -1, false};
    if (grammar) llama_sampler_apply(grammar, &all);
    llama_sampler_apply(pick, &all);
    if (all.selected < 0) return std::nullopt;
    return all.data[all.selected].id;
}

// Picks the next token. With a grammar, the model's own choice is tried first and the whole vocabulary is constrained
// only when the grammar refuses it: checking one token is cheap, checking every token at every step is what makes
// constrained generation slow.
std::optional<llama_token> nextToken(llama_context* ctx, int32_t index, llama_sampler* pick, llama_sampler* grammar) {
    if (!grammar) return choose(ctx, index, pick, nullptr);
    auto token = choose(ctx, index, pick, nullptr);
    if (!token) return std::nullopt;
    llama_token_data one{*token, 0.0f, 0.0f};
    llama_token_data_array single{&one, 1, -1, false};
    llama_sampler_apply(grammar, &single);
    if (!std::isfinite(single.data[0].logit)) {
        token = choose(ctx, index, pick, grammar);
        if (!token) return std::nullopt;
    }
    llama_sampler_accept(grammar, *token);
    return token;
}

// The device the model's layers go to: the GPU with the most free memory, a discrete one before one built in (a
// machine with both would otherwise split the model between them, and the built-in one slows the other down); none
// without a GPU, the CPU then.
ggml_backend_dev_t bestGpu() {
    ggml_backend_dev_t best = nullptr;
    int best_rank           = 0;
    std::size_t best_free   = 0;
    for (std::size_t i = 0; i < ggml_backend_dev_count(); ++i) {
        auto* dev       = ggml_backend_dev_get(i);
        const auto type = ggml_backend_dev_type(dev);
        const int rank  = type == GGML_BACKEND_DEVICE_TYPE_GPU ? 2 : type == GGML_BACKEND_DEVICE_TYPE_IGPU ? 1 : 0;
        if (rank == 0) continue;
        std::size_t free = 0, total = 0;
        ggml_backend_dev_memory(dev, &free, &total);
        if (rank > best_rank || (rank == best_rank && free > best_free)) {
            best      = dev;
            best_rank = rank;
            best_free = free;
        }
    }
    return best;
}

std::string trim(const std::string& s) {
    const auto space  = [](unsigned char c) { return c == ' ' || (c >= '\t' && c <= '\r'); };
    std::size_t begin = 0, end = s.size();
    while (begin < end && space(static_cast<unsigned char>(s[begin]))) ++begin;
    while (end > begin && space(static_cast<unsigned char>(s[end - 1]))) --end;
    return s.substr(begin, end - begin);
}

}  // namespace

// A chat template, parsed once.
struct Template {
    jinja::program program;
    std::string source;
};

struct Generator::State {
    llama_model* model = nullptr;
    int threads        = 1;
    // The devices the model was given, null-terminated: the chosen GPU alone, or none for the CPU.
    std::vector<ggml_backend_dev_t> devices;
    std::unique_ptr<Template> jinja;  // none when the model's template does not parse
    std::string template_source;

    ~State() {
        if (model) llama_model_free(model);
    }
};

Generator::Generator(std::unique_ptr<State> state) : state_(std::move(state)) {}
Generator::~Generator() = default;

std::unique_ptr<Generator> Generator::load(const std::filesystem::path& path, int gpu_layers, std::string& error) {
    chooseVulkanDevice(gpu_layers > 0);
    initialise();
    auto state          = std::make_unique<State>();
    auto params         = llama_model_default_params();
    params.n_gpu_layers = gpu_layers;
    if (gpu_layers > 0) {
        if (auto* gpu = bestGpu()) state->devices = {gpu, nullptr};
    }
    if (state->devices.empty()) {
        // The CPU alone: no layer, and no operation either, sent to a GPU.
        params.n_gpu_layers = 0;
        state->devices      = {nullptr};
    }
    params.devices = state->devices.data();
    state->model   = llama_model_load_from_file(path.string().c_str(), params);
    if (!state->model) {
        error = path.string() + ": the model does not load";
        return nullptr;
    }
    state->threads = std::max(1, static_cast<int>(std::thread::hardware_concurrency()) / 2);
    if (const char* src = llama_model_chat_template(state->model, nullptr)) {
        state->template_source = src;
        try {
            jinja::lexer lexer;
            auto tokens  = lexer.tokenize(state->template_source);
            state->jinja = std::make_unique<Template>(Template{jinja::parse_from_tokens(tokens), tokens.source});
        } catch (const std::exception&) {
            state->jinja.reset();
        }
    }
    return std::unique_ptr<Generator>(new Generator(std::move(state)));
}

std::optional<std::string> Generator::prompt(const commentary::Request& request, std::string* error) const {
    if (state_->jinja) {
        try {
            // The context the prototype rendered with: the messages as they are, thinking off, and no BOS or EOS
            // token in the template's hands, the tokenizer writing the special tokens it wants.
            auto messages = common_json::array();
            for (const auto& [speaker, text] : request.messages)
                messages.push_back(common_json::object({{"role", roleName(speaker)}, {"content", text}}));
            auto context = common_json::object({{"messages", messages},
                                                {"bos_token", ""},
                                                {"eos_token", ""},
                                                {"enable_thinking", false},
                                                {"add_generation_prompt", true}});
            jinja::context ctx(state_->jinja->source);
            jinja::global_from_json(ctx, context, true);
            jinja::runtime runtime(ctx);
            const jinja::value results = runtime.execute(state_->jinja->program);
            return jinja::runtime::gather_string_parts(results)->as_string().str();
        } catch (const std::exception&) {
            // The built-in rendering below.
        }
    }
    std::vector<llama_chat_message> chat;
    for (const auto& [speaker, text] : request.messages) chat.push_back({roleName(speaker), text.c_str()});
    const char* tmpl = state_->template_source.empty() ? nullptr : state_->template_source.c_str();
    std::vector<char> buffer(4096);
    auto n = llama_chat_apply_template(tmpl, chat.data(), chat.size(), true, buffer.data(),
                                       static_cast<int32_t>(buffer.size()));
    if (n > static_cast<int32_t>(buffer.size())) {
        buffer.resize(static_cast<std::size_t>(n));
        n = llama_chat_apply_template(tmpl, chat.data(), chat.size(), true, buffer.data(),
                                      static_cast<int32_t>(buffer.size()));
    }
    if (n < 0) {
        if (error) *error = "the model's chat template cannot be applied";
        return std::nullopt;
    }
    return std::string(buffer.data(), static_cast<std::size_t>(n));
}

std::optional<commentary::Generation> Generator::generate(const commentary::Request& request, std::string* error) {
    const auto fail = [&](const std::string& why) -> std::optional<commentary::Generation> {
        if (error) *error = why;
        return std::nullopt;
    };
    const auto start = std::chrono::steady_clock::now();
    const auto text  = prompt(request, error);
    if (!text) return std::nullopt;
    const auto* vocab = llama_model_get_vocab(state_->model);
    // The template writes the special tokens it wants, BOS included.
    const auto size = static_cast<int32_t>(text->size());
    int32_t n       = -llama_tokenize(vocab, text->data(), size, nullptr, 0, false, true);
    std::vector<llama_token> tokens(static_cast<std::size_t>(std::max(n, 0)));
    if (n <= 0 || llama_tokenize(vocab, text->data(), size, tokens.data(), n, false, true) != n)
        return fail("empty prompt");
    const auto n_ctx        = static_cast<uint32_t>(tokens.size() + request.max_tokens + 8);
    auto cparams            = llama_context_default_params();
    cparams.n_ctx           = n_ctx;
    cparams.n_batch         = n_ctx;
    cparams.n_threads       = state_->threads;
    cparams.n_threads_batch = state_->threads;
    std::unique_ptr<llama_context, decltype(&llama_free)> ctx(llama_init_from_model(state_->model, cparams),
                                                              &llama_free);
    if (!ctx) return fail("no context for the model");
    Batch batch(static_cast<int32_t>(n_ctx));
    for (std::size_t i = 0; i < tokens.size(); ++i)
        batch.add(tokens[i], static_cast<llama_pos>(i), i + 1 == tokens.size());
    if (llama_decode(ctx.get(), batch.b) != 0) return fail("the prompt does not decode");

    std::optional<Sampler> grammar;
    if (request.grammar) {
        auto* g = llama_sampler_init_grammar(vocab, request.grammar->c_str(), "root");
        if (!g) return fail("the grammar does not parse");
        grammar.emplace(g);
    }
    std::optional<Sampler> pick;
    if (request.temperature <= 0.0f) {
        pick.emplace(llama_sampler_init_greedy());
    } else {
        auto* chain = llama_sampler_chain_init(llama_sampler_chain_default_params());
        llama_sampler_chain_add(chain, llama_sampler_init_temp(request.temperature));
        llama_sampler_chain_add(chain, llama_sampler_init_dist(request.seed));
        pick.emplace(chain);
    }

    std::string bytes;
    auto position         = static_cast<llama_pos>(tokens.size());
    std::size_t generated = 0;
    while (generated < request.max_tokens) {
        const auto last  = batch.b.n_tokens - 1;
        const auto token = nextToken(ctx.get(), last, pick->s, grammar ? grammar->s : nullptr);
        if (!token) return fail("no token left to pick");
        if (llama_vocab_is_eog(vocab, *token)) break;
        std::vector<char> piece(64);
        auto len = llama_token_to_piece(vocab, *token, piece.data(), static_cast<int32_t>(piece.size()), 0, false);
        if (len < 0) {
            piece.resize(static_cast<std::size_t>(-len));
            len = llama_token_to_piece(vocab, *token, piece.data(), static_cast<int32_t>(piece.size()), 0, false);
        }
        if (len > 0) bytes.append(piece.data(), static_cast<std::size_t>(len));
        ++generated;
        batch.b.n_tokens = 0;
        batch.add(*token, position, true);
        ++position;
        if (llama_decode(ctx.get(), batch.b) != 0) return fail("a token does not decode");
    }
    commentary::Generation g;
    g.text          = trim(bytes);
    g.prompt_tokens = tokens.size();
    g.tokens        = generated;
    g.seconds       = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    return g;
}

std::string Generator::devices() const {
    const auto named = [](ggml_backend_dev_t dev) {
        return std::string(ggml_backend_dev_name(dev)) + " (" + ggml_backend_dev_description(dev) + ")";
    };
    if (!state_->devices.empty() && state_->devices.front()) return named(state_->devices.front());
    for (std::size_t i = 0; i < ggml_backend_dev_count(); ++i) {
        auto* dev = ggml_backend_dev_get(i);
        if (ggml_backend_dev_type(dev) == GGML_BACKEND_DEVICE_TYPE_CPU) return named(dev);
    }
    return "CPU";
}

}  // namespace fcg::assistant
