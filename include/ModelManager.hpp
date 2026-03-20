#pragma once

#include <string>
#include <vector>
#include <map>

namespace neuroswarm {

// A named model slot: independent model weights + inference context
struct ModelSlot {
    void* model  = nullptr;  // llama_model*
    void* ctx    = nullptr;  // llama_context*
    std::string path;
    int n_ctx    = 2048;
};

// A LoRA adapter applied to the base model's context at inference time
struct LoRAAdapter {
    void* adapter = nullptr;  // llama_adapter_lora*
    float scale   = 1.0f;
    std::string path;
};

class ModelManager {
public:
    // embed_model_path: optional dedicated embedding model (e.g. nomic-embed).
    // If empty, falls back to using the base model for embeddings.
    ModelManager(const std::string& base_model_path,
                 const std::string& embed_model_path = "");
    ~ModelManager();

    // Returns true if both the generative model weights and inference context are initialised
    bool is_alive() const { return gray_matter != nullptr && ctx_ptr != nullptr; }

    // Load an additional specialist model into a named slot (e.g. "coder", "critic")
    bool add_model(const std::string& name, const std::string& model_path, int n_ctx = 2048);

    // Load a LoRA adapter GGUF to be applied on the base model context during inference
    bool load_lora(const std::string& name, const std::string& lora_path, float scale = 1.0f);

    // Run autoregressive inference with optional GBNF grammar constraint; returns raw token output.
    // If adapter_name matches a loaded slot, that slot's model is used; otherwise falls back to base.
    std::string fire(const std::string& adapter_name, const std::string& prompt, const std::string& grammar_str = "");

    // Compute L2-normalised dense embedding for the given text using the dedicated embedding context
    std::vector<float> get_embeddings(const std::string& text);

private:
    void* gray_matter   = nullptr; // Generative model weights (llama_model*)
    void* ctx_ptr       = nullptr; // Generative inference context (llama_context*)
    void* embed_model   = nullptr; // Dedicated embedding model; may alias gray_matter when no separate model is configured
    void* embed_ctx     = nullptr; // Embedding inference context with mean-pooling enabled
    bool  own_embed_model = false; // True when embed_model was loaded from a separate file and must be freed independently
    std::map<std::string, ModelSlot> slots;       // Named specialist model slots
    std::map<std::string, LoRAAdapter> lora_adapters; // Named LoRA adapters on base model
};

} // namespace neuroswarm
