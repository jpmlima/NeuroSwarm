#include "ModelManager.hpp"
#include "llama.h"
#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <cstring>
#include <filesystem>
#include <cmath>

namespace fs = std::filesystem;

namespace neuroswarm {

static void log_step(const std::string& msg) {
    std::ofstream f("/home/xenomai/Documents/NeuroSwarm/logs/brain_step.log", std::ios::app);
    f << "[STEP] " << msg << std::endl;
}

ModelManager::ModelManager(const std::string& model_path) {
    ggml_backend_load_all();
    llama_model_params mparams = llama_model_default_params();
    mparams.n_gpu_layers = 99; 
    
    log_step("Loading base model: " + model_path);
    gray_matter = llama_model_load_from_file(model_path.c_str(), mparams);
    
    if (!gray_matter) {
        log_step("FATAL: Model loading failed!");
    }
}

ModelManager::~ModelManager() {
    for (auto& pair : loaded_adapters) {
        // llama_adapter_lora_free is deprecated in some versions but we use it for cleanup if available
        // llama_free_adapter(pair.second); // Alternative if available
    }
    if (gray_matter) llama_model_free((llama_model*)gray_matter);
}

void* ModelManager::get_or_load_adapter(const std::string& name) {
    if (name == "default" || name.empty()) return nullptr;
    if (loaded_adapters.count(name)) return loaded_adapters[name];

    std::string path = "/home/xenomai/Documents/NeuroSwarm/models/" + name + ".gguf";
    if (!fs::exists(path)) {
        log_step("Adapter not found: " + path);
        return nullptr;
    }

    log_step("Loading LoRA Adapter: " + name);
    auto* adapter = llama_adapter_lora_init((llama_model*)gray_matter, path.c_str());
    if (adapter) {
        loaded_adapters[name] = adapter;
    }
    return adapter;
}

std::string ModelManager::fire(const std::string& adapter_name, const std::string& prompt) {
    if (!gray_matter) return "ERROR: Brain not loaded";

    auto* model = (llama_model*)gray_matter;
    const auto* vocab = llama_model_get_vocab(model);

    // Tokenization: Use add_bos=false as ChatML tags serve as markers
    const int n_prompt_tokens = -llama_tokenize(vocab, prompt.c_str(), prompt.size(), NULL, 0, false, true);
    std::vector<llama_token> tokens(n_prompt_tokens);
    llama_tokenize(vocab, prompt.c_str(), prompt.size(), tokens.data(), tokens.size(), false, true);

    // Context setup
    llama_context_params cparams = llama_context_default_params();
    cparams.n_ctx = n_prompt_tokens + 256;
    auto* ctx = llama_init_from_model(model, cparams);
    
    // LoRA Multiplexing: Apply adapter dynamically
    if (!adapter_name.empty() && adapter_name != "none") {
        void* adapter_ptr = get_or_load_adapter(adapter_name);
        if (adapter_ptr) {
            auto* adapter = (struct llama_adapter_lora*)adapter_ptr;
            float scale = 1.0f;
            // Use the modern API to apply adapters to the context
            // Correct signature: context, adapters**, count, scales*
            llama_set_adapters_lora(ctx, &adapter, 1, &scale);
            log_step("Applied LoRA adapter: " + adapter_name);
        } else {
            log_step("WARNING: Failed to apply adapter " + adapter_name + ", using base model.");
        }
    }
    
    // Sampler
    auto* smpl = llama_sampler_chain_init(llama_sampler_chain_default_params());
    llama_sampler_chain_add(smpl, llama_sampler_init_temp(0.7f));
    llama_sampler_chain_add(smpl, llama_sampler_init_dist(1337)); // Seed 1337 or LLAMA_DEFAULT_SEED

    log_step("Inference firing with adapter: " + (adapter_name.empty() ? "none" : adapter_name));
    
    llama_batch batch = llama_batch_get_one(tokens.data(), tokens.size());
    
    if (llama_decode(ctx, batch) != 0) {
        llama_sampler_free(smpl);
        llama_free(ctx);
        return "ERROR: Decode failed";
    }

    std::string response = "";
    for (int i = 0; i < 128; i++) {
        llama_token id = llama_sampler_sample(smpl, ctx, -1);
        if (llama_vocab_is_eog(vocab, id)) break;

        char buf[256];
        int n = llama_token_to_piece(vocab, id, buf, sizeof(buf), 0, true);
        if (n > 0) response += std::string(buf, n);

        batch = llama_batch_get_one(&id, 1);
        if (llama_decode(ctx, batch) != 0) break;
    }

    llama_sampler_free(smpl);
    llama_free(ctx);
    return response;
}

std::vector<float> ModelManager::get_embeddings(const std::string& text) {
    if (!gray_matter) return {};

    auto* model = (llama_model*)gray_matter;
    const auto* vocab = llama_model_get_vocab(model);

    // Tokenize
    const int n_tokens = -llama_tokenize(vocab, text.c_str(), text.size(), NULL, 0, false, true);
    std::vector<llama_token> tokens(n_tokens);
    llama_tokenize(vocab, text.c_str(), text.size(), tokens.data(), tokens.size(), false, true);

    // Context with embeddings enabled
    llama_context_params cparams = llama_context_default_params();
    cparams.embeddings = true;
    cparams.n_ctx = n_tokens;
    auto* ctx = llama_init_from_model(model, cparams);

    llama_batch batch = llama_batch_get_one(tokens.data(), tokens.size());
    if (llama_decode(ctx, batch) != 0) {
        llama_free(ctx);
        return {};
    }

    const float* embd = llama_get_embeddings(ctx);
    if (!embd) {
        // Modern API might use llama_get_embeddings_ith(ctx, -1) for pooling
        embd = llama_get_embeddings_ith(ctx, -1);
    }

    int n_embd = llama_model_n_embd(model);
    std::vector<float> res(n_embd);
    if (embd) {
        memcpy(res.data(), embd, n_embd * sizeof(float));
        
        // Normalize for cosine similarity
        float norm = 0.0f;
        for (float v : res) norm += v * v;
        norm = sqrt(norm);
        if (norm > 0) for (float& v : res) v /= norm;
    }

    llama_free(ctx);
    return res;
}

} // namespace neuroswarm
