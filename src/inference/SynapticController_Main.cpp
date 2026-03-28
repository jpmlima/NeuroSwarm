#include "ModelManager.hpp"
#include <zmq.hpp>
#include <nlohmann/json.hpp>
#include <common/routing.hpp>
#include <iostream>
#include <fstream>
#include <string>
#include <filesystem>

namespace fs = std::filesystem;
using json = nlohmann::json;

int main(int argc, char** argv) {
    std::string thalamus_ip = "localhost";
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--thalamus" && i + 1 < argc) {
            thalamus_ip = argv[i+1];
        }
    }

    try {
        // Model priority: Qwen3-8B → Qwen2.5-7B → Phi-4-mini → Qwen2.5-1.5B
        auto pick_model = [](const std::vector<std::string>& candidates) {
            for (auto& path : candidates) {
                if (std::ifstream(path).good()) return path;
            }
            return candidates.back();
        };
        std::string gen_model = pick_model({
            "/home/xenomai/Documents/NeuroSwarm/models/Qwen3-8B-Q4_K_M.gguf",
            "/home/xenomai/Documents/NeuroSwarm/models/Qwen2.5-7B-Instruct-Q4_K_M.gguf",
            "/home/xenomai/Documents/NeuroSwarm/models/Phi-4-mini-instruct-Q4_K_M.gguf",
            "/home/xenomai/Documents/NeuroSwarm/models/qwen2.5-1.5b-instruct-q4_k_m.gguf"
        });
        std::cout << "[BRAIN] Loading generative model: " << gen_model << std::endl;
        neuroswarm::ModelManager brain(
            gen_model,
            "/home/xenomai/Documents/NeuroSwarm/models/nomic-embed-text-v1.5.Q8_0.gguf"
        );
        
        if (!brain.is_alive()) {
            std::cerr << "FATAL: Brain Model failed to initialize. Exiting." << std::endl;
            return 1;
        }

        // Load specialist model slots — scan models/ for known patterns
        const std::string models_dir = "/home/xenomai/Documents/NeuroSwarm/models/";

        // "coder" slot: prefer Qwen2.5-Coder if available, otherwise use base model (7B)
        // The 1.5B fallback generated too many degenerate commands (id, whoami) — 7B is needed
        bool coder_loaded = false;
        for (const auto& entry : fs::directory_iterator(models_dir)) {
            std::string name = entry.path().filename().string();
            if (name.find("qwen2.5-coder") != std::string::npos && name.find(".gguf") != std::string::npos) {
                coder_loaded = brain.add_model("coder", entry.path().string());
                break;
            }
        }
        if (!coder_loaded)
            std::cout << "[BRAIN] No dedicated coder model. 'coder' adapter uses base model (7B)." << std::endl;

        // "critic" slot: scan for any model with "critic" in the name
        bool critic_loaded = false;
        for (const auto& entry : fs::directory_iterator(models_dir)) {
            std::string name = entry.path().filename().string();
            if (name.find("critic") != std::string::npos && name.find(".gguf") != std::string::npos) {
                critic_loaded = brain.add_model("critic", entry.path().string());
                break;
            }
        }
        if (!critic_loaded)
            std::cout << "[BRAIN] No critic model found. 'critic' adapter will fall back to base model." << std::endl;

        // Load latest fine-tuned model from models/finetuned/ as "finetuned" slot
        const std::string finetuned_dir = models_dir + "finetuned/";
        if (fs::exists(finetuned_dir) && fs::is_directory(finetuned_dir)) {
            std::string latest_ft;
            fs::file_time_type latest_time{};
            for (const auto& entry : fs::directory_iterator(finetuned_dir)) {
                std::string name = entry.path().filename().string();
                if (name.find(".gguf") != std::string::npos) {
                    auto mtime = fs::last_write_time(entry.path());
                    if (latest_ft.empty() || mtime > latest_time) {
                        latest_ft = entry.path().string();
                        latest_time = mtime;
                    }
                }
            }
            if (!latest_ft.empty()) {
                if (brain.add_model("finetuned", latest_ft))
                    std::cout << "[BRAIN] Fine-tuned model loaded as 'finetuned' slot: " << latest_ft << std::endl;
            }
        }

        // Load LoRA adapters from models/lora/
        const std::string lora_dir = models_dir + "lora/";
        if (fs::exists(lora_dir) && fs::is_directory(lora_dir)) {
            for (const auto& entry : fs::directory_iterator(lora_dir)) {
                std::string name = entry.path().filename().string();
                if (name.find(".gguf") != std::string::npos) {
                    std::string stem = entry.path().stem().string();
                    brain.load_lora(stem, entry.path().string());
                }
            }
        }

        zmq::context_t ctx(1);
        zmq::socket_t sub(ctx, zmq::socket_type::sub);
        sub.connect("tcp://" + thalamus_ip + ":5556");
        routing::subscribe(sub, {"embedding_request", "inference_request", "training_complete"});

        zmq::socket_t pub(ctx, zmq::socket_type::pub);
        pub.connect("tcp://" + thalamus_ip + ":5555");

        std::cout << "[BRAIN] Synaptic Controller connected to Thalamus at " << thalamus_ip << std::endl;

        while (true) {
            auto j = routing::receive(sub);
            if (j.is_null()) continue;

            try {
                std::string intent = j.value("intent", "");
                std::string cid = j.value("cid", "unknown");

                if (intent == "training_complete") {
                    std::string model_path = j.value("model_path", "");
                    std::string type = j.value("type", "model");
                    std::string adapter_name = j.value("adapter_name", "finetuned");

                    if (type == "lora" && !model_path.empty()) {
                        // Hot-load LoRA adapter without restart
                        bool ok = brain.load_lora(adapter_name, model_path);
                        std::cout << "[BRAIN] REM LoRA hot-loaded: " << model_path
                                  << " as '" << adapter_name << "' — "
                                  << (ok ? "success" : "FAILED") << std::endl;
                        if (ok) {
                            // Broadcast availability
                            json avail = {
                                {"origin", "synaptic_controller"},
                                {"intent", "lora_available"},
                                {"adapter_name", adapter_name},
                                {"model_path", model_path}
                            };
                            routing::publish(pub, avail);
                        }
                    } else if (!model_path.empty()) {
                        bool ok = brain.add_model("finetuned", model_path);
                        std::cout << "[BRAIN] REM fine-tuned model loaded: " << model_path
                                  << " — " << (ok ? "success" : "FAILED") << std::endl;
                    }
                }
                else if (j.value("origin", "") != "synaptic_controller" && intent == "embedding_request") {
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
                    float temperature = j.value("temperature", -1.0f);

                    std::string response = brain.fire(adapter, prompt, grammar, temperature);
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
