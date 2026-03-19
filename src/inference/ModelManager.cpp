#include "ModelManager.hpp"
#include "llama.h"
#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <filesystem>
#include <cmath>
#include <algorithm>

namespace fs = std::filesystem;

namespace neuroswarm {

static void log_step(const std::string& msg) {
    std::ofstream f("/home/xenomai/Documents/NeuroSwarm/logs/brain_step.log", std::ios::app);
    f << "[STEP] " << msg << std::endl;
}

ModelManager::ModelManager(const std::string& base_model_path,
                           const std::string& embed_model_path) {
    ggml_backend_load_all();
    llama_model_params mparams = llama_model_default_params();
    mparams.n_gpu_layers = -1;

    log_step("Loading base model (ACCELERATED): " + base_model_path);
    gray_matter = llama_model_load_from_file(base_model_path.c_str(), mparams);
    if (!gray_matter) {
        std::cerr << "FATAL: Model loading failed!" << std::endl;
        return;
    }

    llama_context_params cparams = llama_context_default_params();
    cparams.n_ctx    = 2048;  // 2048 tokens is sufficient for FE prompts; smaller KV cache reduces latency
    cparams.n_batch  = 512;   // Previously 32 — batch size was the dominant throughput bottleneck
    cparams.n_ubatch = 512;
    ctx_ptr = llama_init_from_model((llama_model*)gray_matter, cparams);

    // Load dedicated embedding model if a separate path was given
    bool use_dedicated = !embed_model_path.empty() && embed_model_path != base_model_path
                         && fs::exists(embed_model_path);
    if (use_dedicated) {
        log_step("Loading dedicated embedding model: " + embed_model_path);
        llama_model_params eparams_m = llama_model_default_params();
        eparams_m.n_gpu_layers = -1;
        embed_model    = llama_model_load_from_file(embed_model_path.c_str(), eparams_m);
        own_embed_model = (embed_model != nullptr);
        if (!embed_model) {
            std::cerr << "[BRAIN] Warning: Embed model failed to load. Falling back to base model." << std::endl;
            embed_model = gray_matter;
        }
    } else {
        embed_model = gray_matter; // Reuse base model weights for embeddings; a separate context provides sufficient isolation
    }

    llama_context_params ectx = llama_context_default_params();
    ectx.n_ctx        = 2048; // nomic-embed-text supports up to 8192 tokens; 2048 is a conservative, memory-efficient cap
    ectx.n_batch      = 512;
    ectx.embeddings   = true;
    ectx.pooling_type = LLAMA_POOLING_TYPE_MEAN;
    embed_ctx = llama_init_from_model((llama_model*)embed_model, ectx);
    if (!embed_ctx)
        std::cerr << "[BRAIN] Warning: Embedding context failed. Semantic search unavailable." << std::endl;
    else
        std::cout << "[BRAIN] Embedding context ready. Model: "
                  << (use_dedicated ? embed_model_path : "base (shared)") << std::endl;
}

ModelManager::~ModelManager() {
    // Free specialist model slots
    for (auto& [name, slot] : slots) {
        if (slot.ctx)   llama_free((llama_context*)slot.ctx);
        if (slot.model) llama_model_free((llama_model*)slot.model);
        log_step("Freed model slot: " + name);
    }
    slots.clear();

    if (embed_ctx)                      llama_free((llama_context*)embed_ctx);
    if (ctx_ptr)                        llama_free((llama_context*)ctx_ptr);
    if (own_embed_model && embed_model) llama_model_free((llama_model*)embed_model);
    if (gray_matter)                    llama_model_free((llama_model*)gray_matter);
}

bool ModelManager::add_model(const std::string& name, const std::string& model_path, int n_ctx) {
    if (slots.count(name)) {
        log_step("Model slot '" + name + "' already loaded, skipping.");
        return true;
    }

    log_step("Loading specialist model '" + name + "': " + model_path);

    llama_model_params mparams = llama_model_default_params();
    mparams.n_gpu_layers = -1;

    void* m = llama_model_load_from_file(model_path.c_str(), mparams);
    if (!m) {
        std::cerr << "[BRAIN] Failed to load model slot '" << name << "': " << model_path << std::endl;
        return false;
    }

    llama_context_params cparams = llama_context_default_params();
    cparams.n_ctx    = n_ctx;
    cparams.n_batch  = 512;
    cparams.n_ubatch = 512;

    void* c = llama_init_from_model((llama_model*)m, cparams);
    if (!c) {
        std::cerr << "[BRAIN] Failed to create context for slot '" << name << "'" << std::endl;
        llama_model_free((llama_model*)m);
        return false;
    }

    ModelSlot slot;
    slot.model = m;
    slot.ctx   = c;
    slot.path  = model_path;
    slot.n_ctx = n_ctx;
    slots[name] = slot;

    std::cout << "[BRAIN] Model slot '" << name << "' loaded: " << model_path << " (n_ctx=" << n_ctx << ")" << std::endl;
    return true;
}

std::string ModelManager::fire(const std::string& adapter_name, const std::string& prompt, const std::string& grammar_str) {
    if (!gray_matter || !ctx_ptr) return "ERROR: Brain not initialized.";

    // Route to specialist slot if available, otherwise fall back to base model
    llama_model* model;
    llama_context* ctx;
    auto slot_it = slots.find(adapter_name);
    if (slot_it != slots.end() && slot_it->second.model && slot_it->second.ctx) {
        model = (llama_model*)slot_it->second.model;
        ctx   = (llama_context*)slot_it->second.ctx;
        log_step("Using specialist slot '" + adapter_name + "' for inference");
    } else {
        model = (llama_model*)gray_matter;
        ctx   = (llama_context*)ctx_ptr;
        if (adapter_name != "default" && adapter_name != "executive")
            log_step("Slot '" + adapter_name + "' not found, falling back to base model");
    }

    // Stable context reset: clear sequence 0
    llama_memory_seq_rm(llama_get_memory(ctx), 0, -1, -1);

    // Pass the prompt through without modification; system identity is injected upstream by the requesting lobe
    std::string full_prompt = prompt;

    const auto* vocab = llama_model_get_vocab(model);
    std::vector<llama_token> tokens(full_prompt.size() + 128);
    int n_tokens = llama_tokenize(vocab, full_prompt.c_str(), full_prompt.size(), tokens.data(), tokens.size(), true, true);
    if (n_tokens < 0) {
        tokens.resize(-n_tokens);
        n_tokens = llama_tokenize(vocab, prompt.c_str(), prompt.size(), tokens.data(), tokens.size(), true, true);
    }
    tokens.resize(n_tokens);

    // SAFETY: Truncate if prompt is too big for KV cache (reserve space for generation)
    if (tokens.size() > 1700) {  // Reserve 300 tokens for generation headroom within n_ctx=2048
        tokens.erase(tokens.begin(), tokens.end() - 1700);
    }

    // Process prompt in n_batch-sized chunks; 16× faster than the previous stride of 32, safe for any prompt length
    llama_batch batch;
    const int CHUNK = 512;
    for (size_t i = 0; i < tokens.size(); i += CHUNK) {
        size_t n = std::min((size_t)CHUNK, tokens.size() - i);
        batch = llama_batch_get_one(&tokens[i], (int32_t)n);
        if (llama_decode(ctx, batch) != 0) return "ERROR: Decode failed.";
    }

    auto* smpl = llama_sampler_chain_init(llama_sampler_chain_default_params());
    llama_sampler_chain_add(smpl, llama_sampler_init_temp(0.7f));
    llama_sampler_chain_add(smpl, llama_sampler_init_penalties(64, 1.2f, 0.2f, 0.2f)); // Elevated repetition penalties to suppress degenerate token loops
    
    if (!grammar_str.empty()) {
        auto* g_smpl = llama_sampler_init_grammar(vocab, grammar_str.c_str(), "root");
        if (g_smpl) llama_sampler_chain_add(smpl, g_smpl);
    }

    llama_sampler_chain_add(smpl, llama_sampler_init_dist(1234));

    std::string response = "";
    for (int i = 0; i < 300; i++) {  // GBNF grammar constrains output to compact JSON; 300 tokens is a safe upper bound
        llama_token id = llama_sampler_sample(smpl, ctx, -1);
        if (llama_vocab_is_eog(vocab, id)) break;

        char buf[128];
        int n = llama_token_to_piece(vocab, id, buf, sizeof(buf), 0, false);
        if (n >= 0) response += std::string(buf, n);

        llama_token next[] = {id};
        batch = llama_batch_get_one(next, 1);
        if (llama_decode(ctx, batch) != 0) break;
    }

    llama_sampler_free(smpl);
    return response;
}

std::vector<float> ModelManager::get_embeddings(const std::string& text) {
    if (!embed_model || !embed_ctx) return {};

    auto* model = (llama_model*)embed_model;
    auto* ctx   = (llama_context*)embed_ctx;

    llama_memory_seq_rm(llama_get_memory(ctx), 0, -1, -1);

    const auto* vocab = llama_model_get_vocab(model);
    std::string input = text.substr(0, 2048); // safety cap before tokenization
    std::vector<llama_token> tokens(input.size() + 16);
    int n = llama_tokenize(vocab, input.c_str(), input.size(), tokens.data(), tokens.size(), true, true);
    if (n <= 0) return {};
    tokens.resize(std::min(n, 512)); // cap at 512 tokens

    // Mark all tokens for embedding output
    llama_batch batch = llama_batch_get_one(tokens.data(), (int32_t)tokens.size());
    if (llama_decode(ctx, batch) != 0) return {};

    int n_embd = llama_model_n_embd(model);

    // Prefer mean-pooled sequence embedding; fall back to last token
    const float* raw = llama_get_embeddings_seq(ctx, 0);
    if (!raw) raw = llama_get_embeddings_ith(ctx, -1);
    if (!raw) return {};

    std::vector<float> result(raw, raw + n_embd);

    // L2 normalise so cosine similarity = dot product
    float norm = 0.0f;
    for (float v : result) norm += v * v;
    norm = std::sqrt(norm);
    if (norm > 1e-8f) for (float& v : result) v /= norm;

    return result;
}

} // namespace neuroswarm
