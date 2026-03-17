#pragma once

#include <string>
#include <vector>
#include <map>

namespace neuroswarm {

class ModelManager {
public:
    ModelManager(const std::string& base_model_path);
    ~ModelManager();

    // Now accepts an optional adapter name (e.g., "bash_expert")
    std::string fire(const std::string& adapter_name, const std::string& prompt);

private:
    void* gray_matter = nullptr;
    std::map<std::string, void*> loaded_adapters;
    
    void* get_or_load_adapter(const std::string& adapter_name);
};

} // namespace neuroswarm
