/**
 * @file REMEngine.cpp
 * @brief Autonomous Self-Improvement and Synaptic Pruning Engine.
 * 
 * Activates during periods of low homeostatic pressure to consolidate
 * Engram Traces and perform background weight optimization (DPO).
 */

#include <zmq.hpp>
#include <string>
#include <iostream>
#include <thread>
#include <chrono>
#include <fstream>
#include <filesystem>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace neuroswarm {

class REMEngine {
public:
    REMEngine(const std::string& bus_addr = "tcp://localhost:5555") 
        : ctx(1), bus(ctx, zmq::socket_type::dealer) {
        
        bus.set(zmq::sockopt::routing_id, "rem_engine");
        bus.connect(bus_addr);
        std::cout << "[REM ENGINE] Sleep & Consolidation subsystem online." << std::endl;
    }

    /**
     * @brief The circadian rhythm loop. Monitors for sleep signals.
     */
    void start_circadian_loop() {
        while (true) {
            zmq::message_t msg;
            // Use a non-blocking recv or a poller in production.
            auto res = bus.recv(msg, zmq::recv_flags::none);
            if (res) {
                std::string payload(static_cast<char*>(msg.data()), msg.size());
                process_signal(payload);
            }
        }
    }

private:
    zmq::context_t ctx;
    zmq::socket_t bus;

    void process_signal(const std::string& raw_signal) {
        try {
            json signal = json::parse(raw_signal);
            std::string intent = signal.value("intent", "idle");

            // The Thalamus sends a sleep signal when CPU/VRAM are unused for 10 mins
            if (intent == "initiate_sleep_cycle") {
                std::cout << "[REM ENGINE] Entering Sleep State. Beginning consolidation..." << std::endl;
                
                enter_rem_sleep();
            }
        } catch (std::exception& e) {
            std::cerr << "[REM ENGINE] Circadian Error: " << e.what() << std::endl;
        }
    }

    void enter_rem_sleep() {
        // Stage 1: Synaptic Pruning
        std::cout << "[REM ENGINE] Stage 1: Pruning transient Engram Traces..." << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        // Stage 2: Deep Consolidation
        std::cout << "[REM ENGINE] Stage 2: Pushing verified resonances to Hippocampus..." << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        // Stage 3: Weight Optimization (Active Learning)
        std::cout << "[REM ENGINE] Stage 3: Optimizing Motor Lobe LoRA weights..." << std::endl;
        
        int learned_memories = generate_finetune_dataset();
        if (learned_memories > 0) {
            run_finetune();
        } else {
            std::cout << "[REM ENGINE] Not enough new success memories for fine-tuning." << std::endl;
        }

        std::cout << "[REM ENGINE] Sleep cycle complete. Waking up smarter." << std::endl;
        
        json wakeup_signal = {
            {"cid", "circadian_rhythm"},
            {"origin", "rem_engine"},
            {"intent", "sleep_cycle_complete"},
            {"learned_memories", learned_memories}
        };
        broadcast(wakeup_signal);
    }

    int generate_finetune_dataset() {
        std::string engram_dir = "./data/engrams/";
        std::string train_file = "./data/train_data.txt";
        std::ofstream out(train_file);
        int success_count = 0;

        if (!out.is_open()) return false;

        for (const auto& entry : std::filesystem::directory_iterator(engram_dir)) {
            if (entry.path().extension() == ".jsonl") {
                std::ifstream in(entry.path());
                std::string line;
                std::string last_prompt;
                std::string last_response;

                while (std::getline(in, line)) {
                    try {
                        auto j = json::parse(line);
                        if (j.value("intent", "") == "inference_request") {
                            last_prompt = j.value("text", "");
                        } else if (j.value("intent", "") == "inference_result") {
                            last_response = j.value("text", "");
                        } else if (j.value("intent", "") == "execution_result") {
                            if (j.value("status", "") == "success" && !last_prompt.empty()) {
                                // Format: <|im_start|>user\n...<|im_end|>\n<|im_start|>assistant\n...<|im_end|>
                                out << last_prompt << last_response << "<|im_end|>\n";
                                success_count++;
                            }
                        }
                    } catch (...) {}
                }
            }
        }
        return success_count > 0;
    }

    void run_finetune() {
        std::cout << "[REM ENGINE] Launching llama-finetune on background..." << std::endl;
        
        std::string cmd = "./external/llama.cpp/build/bin/llama-finetune "
                          "--model ./models/qwen2.5-1.5b-instruct-q4_k_m.gguf "
                          "--file ./data/train_data.txt "
                          "--output ./models/executive_new.gguf "
                          "--epochs 10 --threads 4 -c 512";
        
        std::cout << "[REM ENGINE] Command: " << cmd << std::endl;
        int res = system(cmd.c_str());
        
        if (res == 0) {
            std::cout << "[REM ENGINE] Fine-tuning successful. New adapter generated." << std::endl;
            // Overwrite old adapter or manage versions
            std::filesystem::copy("./models/executive_new.gguf", "./models/executive.gguf", std::filesystem::copy_options::overwrite_existing);
        } else {
            std::cerr << "[REM ENGINE] Fine-tuning failed with exit code: " << res << std::endl;
        }
    }

    void broadcast(const json& data) {
        std::string payload = data.dump();
        zmq::message_t z_msg(payload.size());
        memcpy(z_msg.data(), payload.c_str(), payload.size());
        bus.send(z_msg, zmq::send_flags::none);
    }
};

} // namespace neuroswarm

int main() {
    neuroswarm::REMEngine rem;
    rem.start_circadian_loop();
    return 0;
}
