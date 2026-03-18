#pragma once

#include <string>
#include <vector>
#include <map>

namespace neuroswarm {

class ModelManager {
public:
    // embed_model_path: optional dedicated embedding model (e.g. nomic-embed).
    // If empty, falls back to using the base model for embeddings.
    ModelManager(const std::string& base_model_path,
                 const std::string& embed_model_path = "");
    ~ModelManager();

    // Check if the brain is healthy
    bool is_alive() const { return gray_matter != nullptr && ctx_ptr != nullptr; }

    // Existing fire method
    std::string fire(const std::string& adapter_name, const std::string& prompt, const std::string& grammar_str = "");

    // New semantic embedding method
    std::vector<float> get_embeddings(const std::string& text);

private:
    void* gray_matter   = nullptr; // generative model weights
    void* ctx_ptr       = nullptr; // generative context
    void* embed_model   = nullptr; // dedicated embedding model (may equal gray_matter)
    void* embed_ctx     = nullptr; // embedding context
    bool  own_embed_model = false; // true if embed_model was loaded separately
    std::map<std::string, void*> loaded_adapters;

    void* get_or_load_adapter(const std::string& adapter_name);
};

} // namespace neuroswarm
