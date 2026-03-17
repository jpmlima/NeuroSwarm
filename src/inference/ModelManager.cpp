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

ModelManager::ModelManager(const std::string& base_model_path) {
    ggml_backend_load_all();
    llama_model_params mparams = llama_model_default_params();
    mparams.n_gpu_layers = 0; // Stability: Force CPU until batching is fixed
    
    log_step("Loading base model (STABLE MODE): " + base_model_path);
    gray_matter = llama_model_load_from_file(base_model_path.c_str(), mparams);
    
    if (!gray_matter) {
        log_step("FATAL: Model loading failed!");
    }
}

ModelManager::~ModelManager() {
    if (gray_matter) llama_model_free((llama_model*)gray_matter);
}

void* ModelManager::get_or_load_adapter(const std::string& name) {
    return nullptr; // Temporarily disable adapters to fix core crash
}

std::string ModelManager::fire(const std::string& adapter_name, const std::string& prompt) {
    if (!gray_matter) return "ERROR: Gray matter not loaded.";

    auto* model = (llama_model*)gray_matter;
    llama_context_params cparams = llama_context_default_params();
    cparams.n_ctx = 4096;
    cparams.n_batch = 2048; // THE FIX
    cparams.n_ubatch = 1024;
    
    auto* ctx = llama_init_from_model(model, cparams);
    if (!ctx) return "ERROR: Context init failed.";

    const auto* vocab = llama_model_get_vocab(model);
    std::vector<llama_token> tokens(prompt.size() + 128);
    int n_tokens = llama_tokenize(vocab, prompt.c_str(), prompt.size(), tokens.data(), tokens.size(), true, true);
    if (n_tokens < 0) {
        tokens.resize(-n_tokens);
        n_tokens = llama_tokenize(vocab, prompt.c_str(), prompt.size(), tokens.data(), tokens.size(), true, true);
    }
    tokens.resize(n_tokens);

    llama_batch batch = llama_batch_get_one(tokens.data(), tokens.size());
    if (llama_decode(ctx, batch) != 0) {
        llama_free(ctx);
        return "ERROR: Decode failed (Batch size issue?).";
    }

    auto* smpl = llama_sampler_chain_init(llama_sampler_chain_default_params());
    llama_sampler_chain_add(smpl, llama_sampler_init_temp(0.7f));
    llama_sampler_chain_add(smpl, llama_sampler_init_dist(1234));

    std::string response = "";
    for (int i = 0; i < 256; i++) {
        llama_token id = llama_sampler_sample(smpl, ctx, -1);
        if (llama_vocab_is_eog(vocab, id)) break;

        char buf[128];
        int n = llama_token_to_piece(vocab, id, buf, sizeof(buf), 0, false);
        if (n < 0) break;
        response += std::string(buf, n);

        llama_token next[] = {id};
        batch = llama_batch_get_one(next, 1);
        if (llama_decode(ctx, batch) != 0) break;
    }

    llama_sampler_free(smpl);
    llama_free(ctx);
    return response;
}

std::vector<float> ModelManager::get_embeddings(const std::string& text) { return {}; }

} // namespace neuroswarm
