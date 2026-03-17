#include "ModelManager.hpp"
#include <iostream>

int main() {
    try {
        neuroswarm::ModelManager brain("/home/xenomai/Documents/NeuroSwarm/models/qwen2.5-1.5b-instruct-q4_k_m.gguf");
        std::cout << "[TEST] Firing Synaptic Controller..." << std::endl;
        std::string response = brain.fire("test", "Diz 'OK' se a 1080Ti estiver a funcionar.");
        std::cout << "[RESPONSE] " << response << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
