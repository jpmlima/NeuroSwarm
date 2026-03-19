#include "ModelManager.hpp"
#include <zmq.hpp>
#include <nlohmann/json.hpp>
#include <common/routing.hpp>
#include <iostream>
#include <fstream>
#include <string>

using json = nlohmann::json;

int main(int argc, char** argv) {
    std::string thalamus_ip = "localhost";
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--thalamus" && i + 1 < argc) {
            thalamus_ip = argv[i+1];
        }
    }

    try {
        // Prefer Phi-4-mini if available; fall back to Qwen2.5-1.5B
        auto pick_model = [](const std::string& preferred, const std::string& fallback) {
            return std::ifstream(preferred).good() ? preferred : fallback;
        };
        std::string gen_model = pick_model(
            "/home/xenomai/Documents/NeuroSwarm/models/Phi-4-mini-instruct-Q4_K_M.gguf",
            "/home/xenomai/Documents/NeuroSwarm/models/qwen2.5-1.5b-instruct-q4_k_m.gguf"
        );
        std::cout << "[BRAIN] Loading generative model: " << gen_model << std::endl;
        neuroswarm::ModelManager brain(
            gen_model,
            "/home/xenomai/Documents/NeuroSwarm/models/nomic-embed-text-v1.5.Q8_0.gguf"
        );
        
        if (!brain.is_alive()) {
            std::cerr << "FATAL: Brain Model failed to initialize. Exiting." << std::endl;
            return 1;
        }

        zmq::context_t ctx(1);
        zmq::socket_t sub(ctx, zmq::socket_type::sub);
        sub.connect("tcp://" + thalamus_ip + ":5556");
        routing::subscribe(sub, {"embedding_request", "inference_request"});

        zmq::socket_t pub(ctx, zmq::socket_type::pub);
        pub.connect("tcp://" + thalamus_ip + ":5555");

        std::cout << "[BRAIN] Synaptic Controller connected to Thalamus at " << thalamus_ip << std::endl;

        while (true) {
            auto j = routing::receive(sub);
            if (j.is_null()) continue;

            try {
                std::string intent = j.value("intent", "");
                std::string cid = j.value("cid", "unknown");

                if (j.value("origin", "") != "synaptic_controller" && intent == "embedding_request") {
                    std::string text = j.value("text", "");
                    auto vec = brain.get_embeddings(text);
                    
                    json resp = {
                        {"cid", cid},
                        {"origin", "synaptic_controller"},
                        {"intent", "embedding_result"},
                        {"embedding", vec}
                    };
                    routing::publish(pub, resp);
                }
                else if (j.value("origin", "") != "synaptic_controller" && j.contains("text")) {
                    std::string prompt = j["text"];
                    std::string cid = j.value("cid", "unknown");
                    
                    // Dynamic Adapter Selection: Use specific adapter if provided
                    std::string adapter = j.value("adapter", "default");
                    std::string grammar = j.value("grammar", "");
                    
                    std::string response = brain.fire(adapter, prompt, grammar);
                    std::cout << "[BRAIN] Inference completed for CID " << cid << ". Response size: " << response.size() << " chars." << std::endl;
                    
                    json resp = {
                        {"cid", cid},
                        {"origin", "synaptic_controller"},
                        {"intent", "inference_result"},
                        {"adapter", adapter},
                        {"text", response}
                    };
                    
                    routing::publish(pub, resp);
                }
            } catch (...) {}
        }
    } catch (const std::exception& e) {
        std::cerr << "FATAL BRAIN ERROR: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
