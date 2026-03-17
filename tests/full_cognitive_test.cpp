#include <zmq.hpp>
#include <nlohmann/json.hpp>
#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <ctime>

using json = nlohmann::json;

int main() {
    zmq::context_t ctx(1);
    
    zmq::socket_t pub(ctx, zmq::socket_type::pub);
    pub.connect("tcp://localhost:5555");

    zmq::socket_t sub(ctx, zmq::socket_type::sub);
    sub.connect("tcp://localhost:5556");
    sub.set(zmq::sockopt::subscribe, "");

    std::cout << "[TEST] Initiating Full Cognitive Flow Test..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(1));

    std::string cid = "test_flow_" + std::to_string(std::time(nullptr));
    
    json req = {
        {"cid", cid},
        {"origin", "broca_lobe"},
        {"intent", "user_input"},
        {"text", "Create a bash script named 'hello.sh' that echoes 'hello world' and execute it."}
    };
    
    std::string s_req = req.dump();
    zmq::message_t z_req(s_req.size());
    memcpy(z_req.data(), s_req.c_str(), s_req.size());
    pub.send(z_req, zmq::send_flags::none);

    auto start_time = std::chrono::steady_clock::now();
    bool success = false;

    while (std::chrono::steady_clock::now() - start_time < std::chrono::seconds(120)) {
        zmq::message_t msg;
        if (sub.recv(msg, zmq::recv_flags::dontwait)) {
            std::string raw(static_cast<char*>(msg.data()), msg.size());
            try {
                auto j = json::parse(raw);
                std::string origin = j.value("origin", "");
                std::string intent = j.value("intent", "");
                std::string msg_cid = j.value("cid", "");

                // Only track messages related to our test or global events from the executive
                if (msg_cid == cid || msg_cid == "global") {
                    std::cout << "-> [" << origin << "] " << intent << std::endl;
                    
                    if (intent == "internal_thought") {
                        std::cout << "   * Executive pondering plan..." << std::endl;
                    }
                    if (intent == "inference_request" && j.value("adapter", "") == "critic") {
                        std::cout << "   * Critic validating plan..." << std::endl;
                    }
                    if (intent == "execution_request" && j.value("mode", "") == "dream") {
                        std::cout << "   * Entering Dream Simulation..." << std::endl;
                    }
                    if (intent == "execution_result" && j.value("mode", "") == "dream") {
                        std::cout << "   * Dream Simulation Result: " << j.value("status", "") << std::endl;
                    }
                    if (intent == "execution_request" && j.value("mode", "") == "reality") {
                        std::cout << "   * Collapsing Dream to Reality..." << std::endl;
                    }
                    if (intent == "execution_result" && j.value("mode", "") == "reality") {
                        std::cout << "   * Reality Execution Result: " << j.value("status", "") << std::endl;
                    }
                    if (intent == "task_complete" || (origin == "frontal_executive" && intent == "inference_result" && j.value("text", "").find("COMPLETED") != std::string::npos)) {
                        std::cout << "[TEST] Task Complete!" << std::endl;
                        success = true;
                        break;
                    }
                }
            } catch (...) {}
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    if (!success) {
        std::cerr << "[TEST] Timeout or failure in cognitive flow." << std::endl;
    }

    return success ? 0 : 1;
}
