#include <iostream>
#include <string>
#include <zmq.hpp>
#include <nlohmann/json.hpp>
#include <thread>
#include <chrono>
#include <atomic>

using json = nlohmann::json;

std::atomic<float> system_stress{0.0f};

void homeostasis_monitor(zmq::context_t* ctx) {
    zmq::socket_t sub(*ctx, zmq::socket_type::sub);
    sub.connect("tcp://localhost:5556");
    sub.set(zmq::sockopt::subscribe, "");
    
    while (true) {
        zmq::message_t msg;
        if (sub.recv(msg, zmq::recv_flags::none)) {
            try {
                auto j = json::parse(static_cast<char*>(msg.data()), static_cast<char*>(msg.data()) + msg.size());
                if (j.value("intent", "") == "homeostatic_pulse") {
                    float sr = j.value("success_rate", 1.0f);
                    system_stress = 1.0f - sr;
                }
            } catch (...) {}
        }
    }
}

int main() {
    zmq::context_t ctx(1);
    
    std::thread monitor(homeostasis_monitor, &ctx);
    monitor.detach();

    zmq::socket_t pub(ctx, zmq::socket_type::pub);
    pub.connect("tcp://localhost:5555");

    zmq::socket_t sub(ctx, zmq::socket_type::sub);
    sub.connect("tcp://localhost:5556");
    sub.set(zmq::sockopt::subscribe, ""); 

    std::cout << "--- NEUROSWARM INTERFACE ---" << std::endl;
    std::cout << "Type 'exit' to quit." << std::endl;

    std::string input;
    while (true) {
        std::string mood = (system_stress > 0.5f) ? "[STRESSED] " : "[OPTIMAL] ";
        std::cout << mood << "> ";
        if (!std::getline(std::cin, input) || input == "exit") break;
        if (input.empty()) continue;

        std::string cid = "int_" + std::to_string(std::time(nullptr));

        // Format prompt as ChatML for Qwen2.5-Instruct
        std::string chat_prompt = 
            "<|im_start|>system\n"
            "You are the NeuroSwarm Broca Lobe, the Natural Language Interface of a biomimetic AGI system.\n"
            "Be direct, highly intelligent, and technical. Respond to the user's intent with clarity.\n"
            "INTERNAL STATE: Stress=" + std::to_string(system_stress.load()) + "\n"
            "<|im_end|>\n"
            "<|im_start|>user\n" + input + "<|im_end|>\n"
            "<|im_start|>assistant\n";

        json req = {
            {"cid", cid},
            {"origin", "broca_lobe"},
            {"intent", "user_input"},
            {"text", input}
        };

        std::string s_req = req.dump();
        zmq::message_t z_req(s_req.size());
        memcpy(z_req.data(), s_req.c_str(), s_req.size());
        pub.send(z_req, zmq::send_flags::none);

        bool answered = false;
        auto start_time = std::chrono::steady_clock::now();

        while (!answered) {
            zmq::message_t msg;
            if (sub.recv(msg, zmq::recv_flags::dontwait)) {
                std::string raw(static_cast<char*>(msg.data()), msg.size());
                try {
                    auto j = json::parse(raw);
                    // Wait for task_complete or final response from Executive
                    if (j.value("cid", "") == cid && (j.value("intent", "") == "task_complete" || j.value("intent", "") == "inference_result" && j.value("origin", "") == "frontal_executive")) {
                        std::cout << "\n[BRAIN]: " << j.value("text", "") << std::endl;
                        answered = true;
                    }
                } catch (...) {}
            }

            if (std::chrono::steady_clock::now() - start_time > std::chrono::seconds(120)) {
                std::cout << "[SYSTEM]: Inference timeout (The Brain is thinking too deeply)." << std::endl;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    return 0;
}
