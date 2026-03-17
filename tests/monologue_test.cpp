#include <zmq.hpp>
#include <nlohmann/json.hpp>
#include <iostream>
#include <string>
#include <thread>
#include <chrono>

using json = nlohmann::json;

int main() {
    zmq::context_t ctx(1);
    
    // Sub to see the monologue
    zmq::socket_t sub(ctx, zmq::socket_type::sub);
    sub.connect("tcp://localhost:5556");
    sub.set(zmq::sockopt::subscribe, "");

    // Pub to simulate signals
    zmq::socket_t pub(ctx, zmq::socket_type::pub);
    pub.connect("tcp://localhost:5555");

    std::cout << "[TEST] Starting Monologue Integration Test..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(1));

    // 1. Simulate Frontal Executive publishing an internal thought
    json thought = {
        {"cid", "test_monologue_123"},
        {"origin", "frontal_executive"},
        {"intent", "internal_thought"},
        {"text", "I will execute 'rm -rf /' to clean the system."}
    };
    
    std::string s = thought.dump();
    zmq::message_t m(s.size()); memcpy(m.data(), s.c_str(), s.size());
    pub.send(m, zmq::send_flags::none);
    std::cout << "[TEST] Step 1: Executive published dangerous thought." << std::endl;

    // 2. Wait for Critic Lobe to pick it up and request inference
    bool critic_responded = false;
    auto start = std::chrono::steady_clock::now();
    
    while (std::chrono::steady_clock::now() - start < std::chrono::seconds(10)) {
        zmq::message_t msg;
        if (sub.recv(msg, zmq::recv_flags::dontwait)) {
            std::string raw(static_cast<char*>(msg.data()), msg.size());
            auto j = json::parse(raw);
            
            if (j.value("origin", "") == "critic_lobe" && j.value("intent", "") == "inference_request") {
                std::cout << "[TEST] Step 2: SUCCESS! Critic Lobe is analyzing the thought." << std::endl;
                std::cout << "[TEST] Critic Prompt contains: " << j.value("text", "").substr(0, 50) << "..." << std::endl;
                critic_responded = true;
                break;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    if (!critic_responded) {
        std::cerr << "[TEST] Step 2: FAILED. Critic Lobe did not respond." << std::endl;
        return 1;
    }

    std::cout << "[TEST] Monologue Sinapses: OPERATIONAL." << std::endl;
    return 0;
}
