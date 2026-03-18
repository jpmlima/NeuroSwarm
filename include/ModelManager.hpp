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

    // Returns true if both the generative model weights and inference context are initialised
    bool is_alive() const { return gray_matter != nullptr && ctx_ptr != nullptr; }

    // Run autoregressive inference with optional GBNF grammar constraint; returns raw token output
    std::string fire(const std::string& adapter_name, const std::string& prompt, const std::string& grammar_str = "");

    // Compute L2-normalised dense embedding for the given text using the dedicated embedding context
    std::vector<float> get_embeddings(const std::string& text);

private:
    void* gray_matter   = nullptr; // Generative model weights (llama_model*)
    void* ctx_ptr       = nullptr; // Generative inference context (llama_context*)
    void* embed_model   = nullptr; // Dedicated embedding model; may alias gray_matter when no separate model is configured
    void* embed_ctx     = nullptr; // Embedding inference context with mean-pooling enabled
    bool  own_embed_model = false; // True when embed_model was loaded from a separate file and must be freed independently
    std::map<std::string, void*> loaded_adapters;

    void* get_or_load_adapter(const std::string& adapter_name);
};

} // namespace neuroswarm
