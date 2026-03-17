#pragma once

#include <string>
#include <vector>
#include <map>

namespace neuroswarm {

class ModelManager {
public:
    ModelManager(const std::string& base_model_path);
    ~ModelManager();

    // Existing fire method
    std::string fire(const std::string& adapter_name, const std::string& prompt);

    // New semantic embedding method
    std::vector<float> get_embeddings(const std::string& text);

private:
    void* gray_matter = nullptr;
    std::map<std::string, void*> loaded_adapters;
    
    void* get_or_load_adapter(const std::string& adapter_name);
};

} // namespace neuroswarm
