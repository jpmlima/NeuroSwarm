#include <zmq.hpp>
#include <nlohmann/json.hpp>
#include <iostream>
#include <string>
#include <chrono>
#include <thread>
#include <vector>

using json = nlohmann::json;

int main() {
    zmq::context_t ctx(1);
    zmq::socket_t pub(ctx, zmq::socket_type::pub);
    pub.connect("tcp://localhost:5555");
    zmq::socket_t sub(ctx, zmq::socket_type::sub);
    sub.connect("tcp://localhost:5556");
    sub.set(zmq::sockopt::subscribe, "");

    std::cout << "\n=== NEUROSWARM INTEGRATED TEST SUITE ===\n" << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(2));

    // TEST 1: Pensamento Simples
    std::string cid = "test_think_" + std::to_string(std::time(nullptr));
    json think_req = {
        {"cid", cid},
        {"origin", "broca_lobe"},
        {"intent", "user_input"},
        {"text", "Diz apenas a palavra 'OK' se me ouvires."}
    };
    
    std::cout << "[TEST 1] Testing Synaptic Loop (Inference)..." << std::endl;
    std::string s_think = think_req.dump();
    pub.send(zmq::message_t(s_think.data(), s_think.size()), zmq::send_flags::none);

    // TEST 2: Ação de Motor
    json motor_req = {
        {"cid", "test_motor_001"},
        {"origin", "frontal_executive"},
        {"intent", "execution_request"},
        {"command", "whoami"}
    };
    std::cout << "[TEST 2] Testing Motor Cortex (Bash Execution)..." << std::endl;
    std::string s_motor = motor_req.dump();
    pub.send(zmq::message_t(s_motor.data(), s_motor.size()), zmq::send_flags::none);

    // Monitoramento de Respostas
    auto start = std::chrono::steady_clock::now();
    bool inference_ok = false;
    bool motor_ok = false;

    while (std::chrono::steady_clock::now() - start < std::chrono::seconds(20)) {
        zmq::message_t msg;
        if (sub.recv(msg, zmq::recv_flags::dontwait)) {
            std::string raw(static_cast<char*>(msg.data()), msg.size());
            try {
                auto j = json::parse(raw);
                std::string origin = j.value("origin", "unknown");

                if (origin == "synaptic_controller" && j.value("cid", "") == cid) {
                    std::cout << "  [PASS] Synaptic Controller responded: " << j.value("text", "") << std::endl;
                    inference_ok = true;
                }
                if (origin == "motor_cortex") {
                    std::cout << "  [PASS] Motor Lobe executed command. Output: " << j.value("proprioception", "") << std::endl;
                    motor_ok = true;
                }
            } catch (...) {}
        }
        if (inference_ok && motor_ok) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    if (!inference_ok) std::cout << "  [FAIL] Synaptic Controller timeout." << std::endl;
    if (!motor_ok) std::cout << "  [FAIL] Motor Lobe timeout." << std::endl;

    return (inference_ok && motor_ok) ? 0 : 1;
}
